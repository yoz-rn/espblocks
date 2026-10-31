ESPTOOL="python3 /home/yoz/.platformio/packages/framework-espidf@3.40407.240606/components/esptool_py/esptool/esptool.py"
PORT=/dev/ttyUSB0

python3 /home/yoz/.platformio/packages/framework-espidf@3.40407.240606/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py \
  generate secrets/provisioning.csv secrets/prov.bin 0x4000

$ESPTOOL --chip esp32 --port $PORT write_flash 0xF000 ../secrets/prov.bin
$ESPTOOL --chip esp32 --port $PORT verify_flash 0xF000 ../secrets/prov.bin