#!/bin/bash

TARGET=`gawk 'BEGIN{FS=" *= *"} /hostname/ && /esp32_/{print $2".local"}' platformio.ini`

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
	nc -z $TARGET 80
	if [ $? == 0 ]
	then
		break
	fi
	sleep 1
done

wget -T 1 http://$TARGET/nosleep

# upload
pio run --target upload
