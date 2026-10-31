# ESPTOOL CHEATSHEET

Perintah `esptool` yang dipakai di proyek espblocks (ESP32 klasik, flash 4 MB). Semua perintah diuji dengan esptool **v3.3.4-dev** dari paket ESP-IDF 4.4.7 milik PlatformIO.

> **Peringatan**
> - Tutup serial monitor sebelum memakai esptool (satu port tidak bisa dipakai dua program).
> - Partisi `state` (0x13000) akan berisi **private key** begitu Fase 2 berjalan. `erase_flash`, `erase_region`, dan `write_flash` yang mengenainya **menghapus key**, dan perangkat harus didaftarkan ulang (FR8). Hasil `read_flash` dari `state` juga **rahasia**: jangan di-commit.
> - Versi esptool yang lebih baru mengganti nama perintah (garis bawah menjadi tanda hubung, misalnya `write-flash`). Cek dengan `--help` bila perintah di bawah ditolak.

## 1. Persiapan

Cari lokasi esptool, lalu simpan di variabel supaya perintah tidak panjang:

```bash
find ~/.platformio/packages -name esptool.py
```

```bash
ESPTOOL="python3 /home/yoz/.platformio/packages/framework-espidf@3.40407.240606/components/esptool_py/esptool/esptool.py"
PORT=/dev/ttyUSB0
```

Variabel ini hanya hidup di terminal yang sama. Cek nama port dengan `ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null`.

Pola semua perintah:

```bash
$ESPTOOL --chip esp32 --port $PORT <perintah> [argumen]
```

## 2. Info perangkat (aman, hanya membaca)

| Tujuan | Perintah |
|---|---|
| Jenis chip, revisi, MAC | `chip_id` |
| Pabrikan dan ukuran flash | `flash_id` |
| Hanya MAC | `read_mac` |
| Daftar perintah | `--help` |

Contoh: `$ESPTOOL --chip esp32 --port $PORT flash_id`

## 3. Peta flash proyek

| Area | Alamat | Ukuran |
|---|---|---|
| Bootloader (ESP32 klasik) | `0x1000` | |
| Tabel partisi | `0x8000` | `0x1000` |
| `nvs` (sistem) | `0x9000` | `0x6000` |
| `prov` (hasil provisioning) | `0xF000` | `0x4000` |
| `state` (key pair, boot counter) | `0x13000` | `0x4000` |
| `phy_init` | `0x19000` | `0x1000` |
| `factory` (aplikasi) | `0x20000` | `0x1E0000` |

Alamat dan ukuran untuk `erase_region` dan `read_flash` harus kelipatan `0x1000`.

## 4. Menulis ke flash

**Image provisioning** (hanya mengganti `prov`, firmware dan `state` tidak tersentuh):

```bash
$ESPTOOL --chip esp32 --port $PORT write_flash 0xF000 secrets/prov.bin
```

**Beberapa berkas sekaligus** (pola alamat lalu berkas):

```bash
$ESPTOOL --chip esp32 --port $PORT write_flash \
  0x1000 bootloader.bin 0x8000 partitions.bin 0x20000 firmware.bin
```

**Verifikasi** isi flash terhadap berkas:

```bash
$ESPTOOL --chip esp32 --port $PORT verify_flash 0xF000 secrets/prov.bin
```

Esptool menghapus sektor yang akan ditulis sendiri, jadi tidak perlu `erase` terpisah.

## 5. Menghapus

| Tujuan | Perintah | Risiko |
|---|---|---|
| Seluruh flash | `erase_flash` | **Menghapus semuanya**, termasuk `state`. Setelah itu wajib Upload ulang firmware |
| Satu partisi | `erase_region 0xF000 0x4000` (contoh: `prov`) | Hanya area itu, tapi jangan arahkan ke `state` |

Setelah `erase_flash`, perangkat tidak punya firmware. Upload ulang lewat PlatformIO.

## 6. Membaca flash

**Dump satu partisi:**

```bash
$ESPTOOL --chip esp32 --port $PORT read_flash 0xF000 0x4000 prov_dump.bin
```

**Cadangan seluruh flash 4 MB:**

```bash
$ESPTOOL --chip esp32 --port $PORT read_flash 0x0 0x400000 full_backup.bin
```

**Membaca tabel partisi yang benar-benar ada di perangkat:**

```bash
$ESPTOOL --chip esp32 --port $PORT read_flash 0x8000 0x1000 pt.bin
python3 <PATH_ESP-IDF>/components/partition_table/gen_esp32part.py pt.bin
```

Lokasi `gen_esp32part.py` dicari dengan `find ~/.platformio/packages -name gen_esp32part.py`.

Dump apa pun dari `state` atau `prov` memuat rahasia. Simpan di `secrets/`, jangan di-commit.

## 7. Membuat image NVS

```bash
python3 <PATH_ESP-IDF>/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py \
  generate secrets/provisioning.csv secrets/prov.bin 0x4000
```

`0x4000` harus sama dengan ukuran partisi tujuan. Contoh CSV ada di `docs/provisioning.example.csv`; yang asli ada di `secrets/` (tidak di-commit).

## 8. eFuse (hanya baca)

Berkas `espefuse.py` berada di folder yang sama dengan `esptool.py`:

```bash
python3 <FOLDER_ESPTOOL>/espefuse.py --port $PORT summary
```

`summary` hanya membaca. Perintah yang diawali `burn_` **membakar eFuse dan tidak bisa dibatalkan**. Itu di luar scope proyek ini (lihat Future Work), jadi jangan dijalankan.

## 9. Pemecahan masalah

| Gejala | Kemungkinan penyebab | Tindakan |
|---|---|---|
| `Failed to connect` / `Connecting......` terus | Mode bootloader tidak aktif | Tahan tombol **BOOT** saat tulisan `Connecting` muncul, lepas setelah tersambung |
| Sama, tetap gagal | Kabel hanya untuk daya, atau port salah | Ganti kabel data; cek `ls /dev/ttyUSB*` |
| `Permission denied: /dev/ttyUSB0` | Pengguna belum ada di grup `dialout` | `sudo usermod -aG dialout $USER`, lalu login ulang |
| `Could not open port ... busy` | Serial monitor masih terbuka | Tutup monitor |
| `Timed out waiting for packet header` | Kabel/sambungan tidak stabil, atau board tidak masuk bootloader | Coba kabel/port lain, ulangi dengan tombol BOOT |
| `Wrong chip` | `--chip` tidak cocok | Gunakan `--chip esp32` |
| Transfer lambat | Kecepatan bawaan | Tambah `-b 460800` setelah `--port` |

## 10. Opsi berguna

| Opsi | Fungsi |
|---|---|
| `-b 460800` | Kecepatan transfer lebih tinggi |
| `--after no_reset` | Tetap di mode bootloader setelah perintah selesai |
| `--after hard_reset` | Reset setelah selesai (bawaan) |