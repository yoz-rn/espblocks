# PROJECT BANNER

```
 .----------------.  .----------------.  .----------------.  .----------------.  .----------------.  .----------------.  .----------------.  .----------------.  .----------------. 
| .--------------. || .--------------. || .--------------. || .--------------. || .--------------. || .--------------. || .--------------. || .--------------. || .--------------. |
| |  _________   | || |    _______   | || |   ______     | || |   ______     | || |   _____      | || |     ____     | || |     ______   | || |  ___  ____   | || |    _______   | |
| | |_   ___  |  | || |   /  ___  |  | || |  |_   __ \   | || |  |_   _ \    | || |  |_   _|     | || |   .'    `.   | || |   .' ___  |  | || | |_  ||_  _|  | || |   /  ___  |  | |
| |   | |_  \_|  | || |  |  (__ \_|  | || |    | |__) |  | || |    | |_) |   | || |    | |       | || |  /  .--.  \  | || |  / .'   \_|  | || |   | |_/ /    | || |  |  (__ \_|  | |
| |   |  _|  _   | || |   '.___`-.   | || |    |  ___/   | || |    |  __'.   | || |    | |   _   | || |  | |    | |  | || |  | |         | || |   |  __'.    | || |   '.___`-.   | |
| |  _| |___/ |  | || |  |`\____) |  | || |   _| |_      | || |   _| |__) |  | || |   _| |__/ |  | || |  \  `--'  /  | || |  \ `.___.'\  | || |  _| |  \ \_  | || |  |`\____) |  | |
| | |_________|  | || |  |_______.'  | || |  |_____|     | || |  |_______/   | || |  |________|  | || |   `.____.'   | || |   `._____.'  | || | |____||____| | || |  |_______.'  | |
| |              | || |              | || |              | || |              | || |              | || |              | || |              | || |              | || |              | |
| '--------------' || '--------------' || '--------------' || '--------------' || '--------------' || '--------------' || '--------------' || '--------------' || '--------------' |
 '----------------'  '----------------'  '----------------'  '----------------'  '----------------'  '----------------'  '----------------'  '----------------'  '----------------' 

```

> This project idea came to me while I was taking a ~sh*t~ in a bathroom without an android on my hand (I was forced to reflect on my life journey)

# PROJECT SUMMARY

**espblocks** is a proof-of-concept IoT telemetry pipeline that tries to answer one question: *can you prove that a piece of sensor data really came from your device, and that nobody quietly edited it afterwards?*

An ESP32 generates dummy telemetry, **signs every message on the device** (ECDSA secp256k1), and sends it over **MQTT with TLS**. A backend verifies the signature, stores the record, and periodically anchors a **Merkle root** of the stored data to a smart contract on a blockchain. Change one stored record later, and its proof no longer matches the root on-chain.

```
 ESP32                       Broker                 Backend                     Blockchain
┌──────────────┐  MQTTS   ┌──────────┐        ┌────────────────────┐      ┌─────────────────┐
│ dummy data   │ ───────► │ Mosquitto│ ─────► │ verify signature   │      │ smart contract  │
│ SHA-256      │  (TLS,   │ (TLS,    │        │ anti-replay        │ ───► │ stores Merkle   │
│ sign (secp256│   QoS 1) │  ACL)    │        │ store (SQLite)     │ root │ roots (owner    │
│ k1)          │          └──────────┘        │ build Merkle tree  │      │ only)           │
└──────────────┘                              └────────────────────┘      └─────────────────┘
```

> [!NOTE]
> This project is a **learning-oriented PoC**. It uses dummy sensor data, a single device, and a local or test blockchain. It is built solo and on zero budget, so some things are intentionally left out (see [Known Limitations](#known-limitations)).

## What it protects against (and what it doesn't)

| Threat | Protected? | How |
| --- | --- | --- |
| Someone sniffing the network | ✅ | TLS |
| Man-in-the-middle / fake broker | ✅ | Broker certificate verification, per-message signatures |
| Someone publishing fake data as your device | ✅ | MQTT credentials + ACL, signature checked against the registered public key |
| Replaying an old valid message | ✅ | `boot` + `seq` counters inside the signed payload |
| Someone quietly editing stored data later | ✅ (after the batch is anchored) | Merkle root on-chain |
| Physical access to the ESP32 (key extraction) | ❌ | Out of scope, see Phase 5 |
| Tampered sensor or wiring ("garbage in, garbage out") | ❌ | Out of scope |

## Message format

Each MQTT message is two lines. The first line is the JSON, the second is the signature in hex:

```
{"d":"esp01","b":7,"n":1234,"t":1760000000,"temp":27.5,"rpm":1500}
3045a1...(128 hex characters, raw r||s)
```

| Field | Meaning |
| --- | --- |
| `d` | Device ID (must match the MQTT topic `espblocks/<device_id>/telemetry`) |
| `b` | Boot counter, stored in NVS, +1 on every boot |
| `n` | Sequence number, restarts at 0 on every boot |
| `t` | Unix timestamp from NTP |
| other | Your own data fields, free-form |

The signed bytes are everything before the last newline, and the backend verifies those raw bytes **before** parsing the JSON. Maximum payload size and send interval are build-time settings (`MAX_PAYLOAD_BYTES`, `TELEMETRY_INTERVAL_MS`), so you can plug in your own data.

## Tech Stack

<!-- Firmware -->
![PlatformIO](https://img.shields.io/badge/PlatformIO-F05F30?style=for-the-badge&logo=PlatformIO&logoColor=white)
![Arduino](https://img.shields.io/badge/Arduino_&_ESP--IDF-00979D?style=for-the-badge&logo=Arduino&logoColor=white)
![C++](https://img.shields.io/badge/C++-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)
![FreeRTOS](https://img.shields.io/badge/FreeRTOS-20232A?style=for-the-badge&logo=freertos&logoColor=20B2AA)

<!-- Transport -->
![MQTT](https://img.shields.io/badge/MQTT-660066?style=for-the-badge&logo=mqtt&logoColor=white)
![Mosquitto](https://img.shields.io/badge/Mosquitto-3C5280?style=for-the-badge&logo=eclipsemosquitto&logoColor=white)

<!-- Backend & Blockchain -->
![Node.js](https://img.shields.io/badge/Node.js-339933?style=for-the-badge&logo=nodedotjs&logoColor=white)
![SQLite](https://img.shields.io/badge/SQLite-003B57?style=for-the-badge&logo=sqlite&logoColor=white)
![Solidity](https://img.shields.io/badge/Solidity-363636?style=for-the-badge&logo=solidity&logoColor=white)
![Ethereum](https://img.shields.io/badge/Ethereum-3C3C3D?style=for-the-badge&logo=ethereum&logoColor=white)

| Layer | What is used |
| --- | --- |
| Firmware | PlatformIO (`framework = arduino, espidf`), FreeRTOS, `esp-mqtt`, mbedTLS (secp256k1, deterministic ECDSA), `ArduinoJson` |
| Broker | Mosquitto (local, own CA) |
| Backend | Node.js, Ethers.js, a secp256k1 library, `@openzeppelin/merkle-tree`, SQLite |
| Blockchain | Solidity smart contract, local chain first (Anvil/Hardhat), testnet later |

# ROADMAP & MILESTONES

This project is developed in phases, each with a clear "done when" condition.

### Phase 1: Proof of Concept & TLS
Focus on getting a device to talk to a broker securely.
- [x] **Project Setup:** PlatformIO hybrid project, secrets kept out of Git, custom partition table
- [x] **Wi-Fi with Auto-Reconnect:** Credentials come from NVS, never from the source code
- [x] **NTP Sync:** Time is synced before any TLS connection is opened
- [x] **Local Broker:** Mosquitto with TLS, username/password, and per-device ACL
- [x] **MQTTS Publish:** `esp-mqtt` over port 8883 with certificate verification
- [x] **Dummy Telemetry Task:** A FreeRTOS task producing JSON at a fixed interval
- [x] **Measurements:** Free heap after TLS, outbox size, default `MAX_PAYLOAD_BYTES`

*Done when* dummy messages reach the broker over TLS, Wireshark shows no plaintext, and the measurements are recorded.

### Phase 2: Edge Signing
Focus on making every message verifiable.
- [ ] **secp256k1 via mbedTLS:** Enabled in `sdkconfig` (with a quick spike first to confirm support)
- [ ] **Key Pair Generation:** Created once on the device, stored in NVS, never sent out
- [ ] **Hash & Sign:** SHA-256 plus deterministic ECDSA, raw 64-byte signature
- [ ] **Two-Line Payload:** JSON line + signature line
- [ ] **Independent Verifier:** A script on the computer that checks the signatures

*Done when* 1,000 consecutive signatures pass the verifier.

### Phase 3: Backend & Blockchain
Focus on storing, proving, and anchoring.
- [ ] **MQTT Subscriber:** Receives and verifies messages (signature first, counters second)
- [ ] **Device Enrollment & Anti-Replay:** Public key allowlist and `boot` + `seq` checks
- [ ] **Storage:** SQLite
- [ ] **Merkle Batches:** `open → sealed → anchored`, built with `@openzeppelin/merkle-tree`
- [ ] **Smart Contract:** Owner-only `anchor`, root storage, `verify` via `MerkleProof`
- [ ] **Proof Verification Tool:** Check a record against the on-chain root

*Done when* editing one database row after its batch is anchored makes its verification fail.

### Phase 4: Testing & Documentation
Focus on trying to break it.
- [ ] **Security Audit:** Sniffing, bad certificates, tampered or replayed payloads, unauthorized publishers, database tampering
- [ ] **Robustness Test:** Wi-Fi drops and power cycles with no panic and no lost state
- [ ] **72-Hour Burn-in:** Heap, minimum heap, and largest free block stay flat
- [ ] **Write-up:** Results, plus a short `PubSubClient` vs `esp-mqtt` comparison

### Phase 5: Future Work (Your Turn!)
Things deliberately left out of this PoC. Pull requests, forks, and experiments welcome.
- [ ] **Flash Encryption & Secure Boot V2:** Start in development mode and use a spare board. These burn eFuses, which are permanent
- [ ] **Proper NVS Encryption** and disabling JTAG/UART download
- [ ] **Secure Element** for real key storage
- [ ] **OTA Updates & Certificate Rotation**
- [ ] **Cellular Connectivity** (AT commands / PPPoS)
- [ ] **Event-Driven Sending** with a periodic heartbeat
- [ ] **Mutual TLS** for device authentication
- [ ] **Real Sensors** and protection against sensor tampering

# GETTING STARTED

See [GETTING-STARTED.md](docs/GETTING-STARTED.md) for more details.

# KNOWN LIMITATIONS

* The ESP32's private key is **not protected against physical access**. Anyone who can touch the board can extract it. This device is not a secure key store.
* Cryptography only protects data *after* it enters the chip. A faulty or tampered sensor still gets its readings signed perfectly.
* The blockchain proves that data **existed at a given time**, not that the data is **true**.
* Tampering is only detectable **after a batch has been anchored**. The batch window is how long that gap stays open.
* Wi-Fi only, indoor PoC.
* The embedded root CA can expire, and there is no OTA to replace it.
* Heap is tight: a TLS handshake needs roughly 40 to 60 KB at once, so fragmentation is a real risk over long uptimes.
* No multi-day stability test yet. That is exactly what Phase 4 is for.

# CREDITS

* Research and reference
    * https://doi.org/10.1016/j.egyr.2021.08.190
    * [esp32-hdwallet](https://github.com/AliAkrami1375/esp32-hdwallet)

* Library
    * [ArduinoJson](https://arduinojson.org/)
    * [esp-mqtt](https://github.com/espressif/esp-mqtt) and [mbedTLS](https://github.com/Mbed-TLS/mbedtls) via ESP-IDF
    * [Ethers.js](https://ethers.org/)
    * [OpenZeppelin Merkle Tree](https://github.com/OpenZeppelin/merkle-tree)
    * [Eclipse Mosquitto](https://mosquitto.org/)

* Standards
    * RFC 6979: Deterministic Usage of DSA and ECDSA

* Tools
    * [ClaudeAI](https://claude.ai/)