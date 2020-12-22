#!/bin/sh
ADDR2LINE=/home/jochen/.arduino15/packages/esp32/tools/xtensa-esp32-elf-gcc/1.22.0-80-g6c4433a-5.2.0/bin/xtensa-esp32-elf-addr2line

java -jar tools/EspStackTraceDecoder.jar $ADDR2LINE .pio/build/esp32ota/firmware.elf
