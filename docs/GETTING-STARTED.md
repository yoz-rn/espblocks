# GETTING STARTED

How to reproduce the current state of espblocks from scratch: a local MQTT broker with TLS, per-device accounts and ACL, plus an ESP32 that reads its provisioned settings from flash.

> **Where the project is today**
> This guide covers **Phase 1, up to the provisioning check**. The ESP32 does **not** connect to Wi-Fi or the broker yet, and nothing is signed yet. Those steps will be added as the phases are completed. See the [roadmap in the README](../README.md#roadmap--milestones)

> **Tested on:** Linux Mint (Debian/Ubuntu family), Mosquitto 2.x, VS Code with the PlatformIO extension, ESP32 DevKitC board. Windows and macOS are untested.

## Contents

1. [What you need](#1-what-you-need)
2. [Get the project and protect your secrets](#2-get-the-project-and-protect-your-secrets)
3. [Install the broker](#3-install-the-broker)
4. [Create the certificates](#4-create-the-certificates)
5. [Configure TLS, accounts and ACL](#5-configure-tls-accounts-and-acl)
6. [Verify the broker](#6-verify-the-broker)
7. [Build and upload the firmware](#7-build-and-upload-the-firmware)
8. [Provision the device](#8-provision-the-device)
9. [Troubleshooting](#9-troubleshooting)

## 1. What you need

* An ESP32 board (developed on an ESP32 DevKitC with 4 MB flash) and a USB cable that carries **data**, not just power
* [VS Code](https://code.visualstudio.com/) with the [PlatformIO](https://platformio.org/platformio-ide) extension
* [Git](https://git-scm.com/), Python 3, and OpenSSL
* A Linux computer on the same network as the ESP32 for the broker
* Wi-Fi: the ESP32 supports **2.4 GHz only**

Toolchain used (pinned in `platformio.ini`): PlatformIO platform `espressif32` **6.12.0**, which provides ESP-IDF **4.4.7**, with `framework = arduino, espidf`.

## 2. Get the project and protect your secrets

```bash
git clone https://github.com/yoz-rn/espblocks.git
cd espblocks
```

Everything sensitive lives in `secrets/`, which must **never** be committed. Make sure `.gitignore` contains at least:

```
secrets/
*.key
*.pem
platformio-device-monitor-*.log
.pio/
sdkconfig
```

Then prove that the rule works:

```bash
mkdir -p secrets/pki
git check-ignore -v secrets/pki
```

It should print the matching `.gitignore` line. If it prints nothing, fix `.gitignore` before going further.

## 3. Install the broker

```bash
sudo apt install mosquitto mosquitto-clients
systemctl status mosquitto --no-pager
```

Look for `active (running)`. As a quick sanity check, use two terminals.

Terminal A:

```bash
mosquitto_sub -h localhost -t 'test/#' -v
```

Terminal B:

```bash
mosquitto_pub -h localhost -t test/hello -m 'hello'
```

Terminal A should print `test/hello hello`. This check is plaintext on `localhost` and only proves the broker is alive. It stops working once TLS is enabled in step 5, which is expected.

## 4. Create the certificates

Run **all** commands in this section from `secrets/pki`, not from the repository root:

```bash
cd secrets/pki
```

### 4.1 Certificate authority (CA)

The CA is your own "stamp of approval". The ESP32 will later trust anything signed by it.

```bash
openssl ecparam -name prime256v1 -genkey -noout -out ca.key
openssl req -x509 -new -key ca.key -sha256 -days 3650 \
  -subj "/CN=espblocks-dev-ca" -out ca.crt
chmod 600 ca.key
```

`ca.key` can mint certificates that your devices will trust. Keep it private. After you have issued the server certificate, consider moving it out of the workspace.

### 4.2 Server certificate

The certificate must contain the **exact address** the ESP32 will use to reach the broker. Find your LAN address:

```bash
BROKER_IP=$(hostname -I | awk '{print $1}')
echo $BROKER_IP
```

`hostname -I` can print several addresses (for example from Docker). If the result is not your LAN address, set it manually, for example `BROKER_IP=192.168.1.50`. Prefer a fixed address (a DHCP reservation in your router), otherwise see [the troubleshooting entry](#9-troubleshooting) about changed addresses.

```bash
openssl ecparam -name prime256v1 -genkey -noout -out server.key
openssl req -new -key server.key -subj "/CN=$BROKER_IP" -out server.csr
printf "subjectAltName=DNS:$BROKER_IP,IP:$BROKER_IP\n" > san.cnf
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -days 365 -sha256 -extfile san.cnf -out server.crt
```

The address is written twice (`DNS:` and `IP:`) because different TLS libraries check different entry types.

Check the result:

```bash
openssl verify -CAfile ca.crt server.crt
openssl x509 -in server.crt -noout -text | grep -A1 "Alternative"
```

Expected: `server.crt: OK` and your address listed under both `DNS:` and `IP Address:`.

```bash
cd ../..
```

## 5. Configure TLS, accounts and ACL

### 5.1 Install the certificates for the broker

```bash
sudo mkdir -p /etc/mosquitto/certs
sudo cp secrets/pki/{ca.crt,server.crt,server.key} /etc/mosquitto/certs/
sudo chown root:mosquitto /etc/mosquitto/certs/server.key
sudo chmod 640 /etc/mosquitto/certs/server.key
```

`ca.key` is **not** copied: the broker proves its identity with `server.key` and never needs to issue certificates.

### 5.2 Create the accounts

Each device gets its own account, and the **username must equal the device ID** (the ACL below relies on it). Generate a long random password for every account and store it in `secrets/`:

```bash
openssl rand -base64 18
```

Create the first account **with** `-c` (this creates or overwrites the file), then add the others **without** it:

```bash
sudo mosquitto_passwd -c /etc/mosquitto/passwd esp01
sudo mosquitto_passwd /etc/mosquitto/passwd backend
sudo chown mosquitto:mosquitto /etc/mosquitto/passwd
sudo chmod 600 /etc/mosquitto/passwd
```

Use a **different** password for each account. The device password is stored unencrypted on the board, so anyone holding the board can read it; if it were shared with `backend`, they could read every device's telemetry.

Check that both accounts exist (prints names only):

```bash
sudo cut -d: -f1 /etc/mosquitto/passwd
```

### 5.3 Access control list (ACL)

Create `/etc/mosquitto/acl`:

```
# each device may only write to its own topic
pattern write espblocks/%u/telemetry

# the backend may only read telemetry of all devices
user backend
topic read espblocks/+/telemetry
```

```bash
sudo chown mosquitto:mosquitto /etc/mosquitto/acl
sudo chmod 600 /etc/mosquitto/acl
```

`%u` stands for the username, so adding a device later only needs a new account.

### 5.4 Broker configuration

Create `/etc/mosquitto/conf.d/espblocks.conf`:

```
listener 8883
cafile   /etc/mosquitto/certs/ca.crt
certfile /etc/mosquitto/certs/server.crt
keyfile  /etc/mosquitto/certs/server.key
allow_anonymous false
password_file /etc/mosquitto/passwd
acl_file /etc/mosquitto/acl
```

Defining your own `listener` removes the default local port 1883. That is intended.

```bash
sudo systemctl restart mosquitto
systemctl status mosquitto --no-pager
```

If it fails to start, read the reason with `sudo journalctl -u mosquitto -n 20 --no-pager`. If your firewall is active (`sudo ufw status`), allow port 8883 from your LAN.

## 6. Verify the broker

Set up two variables in **every terminal** you use for these tests. They only live in the terminal where they were created:

```bash
export CAFILE="$HOME/path/to/espblocks/secrets/pki/ca.crt"
read -rs PW_ESP01
read -rs PW_BACKEND
```

`read -rs` waits for you to type the password without showing it, so it does not end up in your shell history.

| # | Test | Command (abridged) | Expected result |
|---|---|---|---|
| 1 | Valid publish and subscribe | Terminal A: `mosquitto_sub -h $BROKER_IP -p 8883 --cafile "$CAFILE" -u backend -P "$PW_BACKEND" -t 'espblocks/+/telemetry' -v`. Terminal B: `mosquitto_pub ... -u esp01 -P "$PW_ESP01" -t espblocks/esp01/telemetry -m ok` | Terminal A prints the message |
| 2 | Publish to another device's topic | Same as above, with `-t espblocks/esp02/telemetry` | Publish returns without error, but terminal A prints **nothing** (ACL drops it silently) |
| 3 | Wrong password | `... -u esp01 -P 'wrong' ...` | `Connection Refused: not authorised` |
| 4 | Wrong hostname | `-h localhost` with the same options | `host name verification failed` |
| 5 | Content is encrypted | See below | Both counts are `0` |

Full form of test 1 (publisher side):

```bash
mosquitto_pub -h $BROKER_IP -p 8883 --cafile "$CAFILE" \
  -u esp01 -P "$PW_ESP01" -t espblocks/esp01/telemetry -m 'ok'
```

For test 4 you can also ask OpenSSL directly:

```bash
openssl s_client -connect localhost:8883 -CAfile "$CAFILE" \
  -verify_hostname localhost </dev/null 2>&1 | grep -iE "verif|hostname"
```

Expect `hostname mismatch` while the chain itself verifies.

**Test 5, the encryption check.** When the client and broker run on the same computer, Linux routes the traffic through the loopback interface, so capture on `lo`, not on your Wi-Fi/Ethernet card:

```bash
sudo tcpdump -i lo -w /tmp/mqtt.pcap 'tcp port 8883' &
mosquitto_pub -h $BROKER_IP -p 8883 --cafile "$CAFILE" \
  -u esp01 -P "$PW_ESP01" -t espblocks/esp01/telemetry -m 'SECRET-12345'
sudo pkill tcpdump
strings /tmp/mqtt.pcap | grep -c 'SECRET'
strings /tmp/mqtt.pcap | grep -ci 'espblocks'
```

Both numbers should be `0`: neither the message nor the topic name is readable. An empty capture file also gives `0`, so check that tcpdump reported captured packets.

## 7. Build and upload the firmware

### 7.1 Files

`platformio.ini` (replace the version with the one in your build header):

```ini
[env:rymcu-esp32-devkitc]
platform = espressif32 @ 6.12.0
board = rymcu-esp32-devkitc
framework = arduino, espidf
monitor_speed = 115200
monitor_filters = log2file
board_build.partitions = partitions.csv
lib_deps =
    bblanchon/ArduinoJson @ 7.4.3
```

`sdkconfig.defaults`:

```
CONFIG_FREERTOS_USE_TRACE_FACILITY=y
CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS=y
CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID=y
CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y
CONFIG_FREERTOS_HZ=1000
CONFIG_AUTOSTART_ARDUINO=y
```

`CONFIG_AUTOSTART_ARDUINO` and `CONFIG_FREERTOS_HZ=1000` are required in hybrid mode. The four trace/statistics options are used for profiling during Phase 1. Note: `sdkconfig.defaults` is only read when `sdkconfig` is first generated. After changing it, delete `sdkconfig` and rebuild.

`partitions.csv`:

```
# Name,    Type, SubType, Offset,   Size
nvs,       data, nvs,     0x9000,   0x6000,
prov,      data, nvs,     0xF000,   0x4000,
state,     data, nvs,     0x13000,  0x4000,
phy_init,  data, phy,     0x19000,  0x1000,
factory,   app,  factory, 0x20000,  0x1E0000,
```

| Partition | Purpose |
|---|---|
| `nvs` | System data (the label must stay `nvs`) |
| `prov` | Provisioned settings; can be re-flashed any time |
| `state` | Reserved for the device key pair and boot counter (Phase 2); **never** overwritten by provisioning |
| `factory` | The application, single slot for now |

### 7.2 Build and upload

1. In PlatformIO, run **Erase Flash** (sidebar → *Project Tasks* → *Platform*). This is safe now because the device holds nothing valuable yet. **Once a key pair exists in `state`, never erase the whole flash**, because that deletes the key.
2. Run **Build**, then **Upload**.
3. Open the serial monitor (115200 baud). You should see the firmware banner and the partition list, matching `partitions.csv`.

At this point the provisioning check reports `prov: namespace tidak dibuka: ESP_ERR_NVS_NOT_FOUND`. That is the expected result for an empty `prov` partition.

## 8. Provision the device

The settings (Wi-Fi, broker address, credentials, CA certificate, send interval) live in the `prov` partition, **not** in the firmware.

### 8.1 Describe the settings

Copy [`docs/provisioning.example.csv`](provisioning.example.csv) to `secrets/provisioning.csv` and fill in **your** values directly in that file:

```
key,type,encoding,value
espblocks,namespace,,
wifi_ssid,data,string,YOUR_SSID
wifi_pass,data,string,YOUR_WIFI_PASSWORD
mqtt_host,data,string,192.168.100.92
mqtt_port,data,u16,8883
mqtt_user,data,string,esp01
mqtt_pass,data,string,YOUR_MQTT_PASSWORD
device_id,data,string,esp01
interval_ms,data,u32,10000
ca_cert,file,string,secrets/pki/ca.crt
```

`mqtt_host` must be the same address that is in the server certificate, and `mqtt_user` must equal `device_id`.

### 8.2 Build the NVS image

Locate the generator that ships with PlatformIO's ESP-IDF package:

```bash
find ~/.platformio -name nvs_partition_gen.py
```

```bash
python3 <PATH_TO>/nvs_partition_gen.py generate secrets/provisioning.csv secrets/prov.bin 0x4000
```

`0x4000` is the size of the `prov` partition. The resulting `secrets/prov.bin` contains your passwords in readable form, so it stays in `secrets/`.

### 8.3 Flash the image

Close the serial monitor first (only one program can use the port). Then, using `esptool` as described in the [esptool cheatsheet](ESPTOOL-CHEATSHEET.md):

```bash
$ESPTOOL --chip esp32 --port $PORT write_flash 0xF000 secrets/prov.bin
```

### 8.4 Check the result

Open the serial monitor again. Expected output (secrets are printed as **lengths only**):

```
device_id=esp01
mqtt_host=192.168.100.92
mqtt_port=8883
interval_ms=10000
wifi_pass: <n> byte
mqtt_pass: <n> byte
ca_cert: <n> byte
```

Then do a normal **Upload** from PlatformIO, without touching `prov.bin`, and read the serial output again. The values must still be there: re-uploading firmware does not erase the `prov` partition.

## 9. Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `Problem setting TLS options: File not found` | `--cafile` path is relative and you are in another directory | Use the absolute `$CAFILE` variable |
| `Connection Refused: not authorised` | Wrong username/password, or `$PW_...` is empty in this terminal | Variables only live in the terminal where you created them; run `read -rs` again |
| `host name verification failed` / `Protocol error` | The `-h` address is not in the certificate | Use the address from the certificate (not `localhost`) |
| Publish succeeds but nobody receives it | The ACL silently dropped it (MQTT 3.1.1 gives no error) | Topic must be `espblocks/<username>/telemetry` |
| Broker does not start | Wrong paths or permissions | `sudo journalctl -u mosquitto -n 20 --no-pager` |
| `tcpdump` capture is empty or has no matches | Captured on the wrong interface | Use `lo` when client and broker run on the same computer |
| Your computer's IP changed | DHCP gave a new address, so the certificate no longer matches | Re-run step 4.2 with the new `BROKER_IP` (the CA stays), copy `server.crt` and `server.key` to `/etc/mosquitto/certs/`, restart the broker, then update `mqtt_host` in `provisioning.csv` and regenerate and re-flash `prov.bin` |
| `ESP_ERR_NVS_NOT_FOUND` on the device | `prov.bin` has not been flashed yet | Do step 8.3 |
| Config options in `sdkconfig.defaults` have no effect | `sdkconfig` already exists | Delete `sdkconfig` and rebuild |
| esptool cannot connect, port busy, permission denied | See the [esptool cheatsheet](ESPTOOL-CHEATSHEET.md) | |

## What is not covered yet

| Step | Planned in |
|---|---|
| ESP32 connects to Wi-Fi and syncs time (NTP) | Phase 1 |
| ESP32 connects to the broker over TLS (`esp-mqtt`) and publishes dummy telemetry | Phase 1 |
| Key pair generation and message signing | Phase 2 |
| Backend, Merkle tree and smart contract | Phase 3 |
| Security audit, robustness and burn-in tests | Phase 4 |