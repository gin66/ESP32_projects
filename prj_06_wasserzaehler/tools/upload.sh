#!/bin/bash

TARGET=`gawk 'BEGIN{FS=" *= *"} /upload_port/ && /esp32_/{print $2}' platformio.ini`

echo $TARGET

if [ "$TARGET" == "" ]
then
	echo Cannot get target
	exit
fi

# ensure compilable
rm -fR .pio

(cd ..;make links)

pio run

# wait esp coming online
while true
do
	ping -c 1 $TARGET
	if [ $? == 0 ]
	then
		break
	fi
done

wget http://$TARGET/nosleep

# upload
pio run --target upload
