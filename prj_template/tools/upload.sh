#!/bin/bash

TARGET=$1
if [ "$TARGET" == "" ]
then
	TARGET=`gawk 'BEGIN{FS=" *= *"} /hostname/ && /esp32_/{print $2".local"}' platformio.ini`
fi

echo $TARGET

if [ "$TARGET" == "" ]
then
	echo Cannot get target
	exit
fi

if [ ! -f ~/.cargo/bin/websocat ]
then
	cargo install websocat
fi

# wait esp coming online
while true
do
	echo -n .
	ping -c 1 $TARGET
	if [ $? == 0 ]
	then
		break
	fi
	sleep 1
done

while true
do
	echo -n .
	nc -z $TARGET 3232
	echo $?
	if [ $? == 0 ]
	then
		break
	fi
	sleep 1
done

# upload
#pio run --target upload
python3 ~/.platformio/packages/framework-arduinoespressif32/tools/espota.py --debug --progress -i $TARGET -f .pio/build/esp32ota/firmware.bin

