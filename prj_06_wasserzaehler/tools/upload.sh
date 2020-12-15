#!/bin/bash

TARGET=`gawk -F= '/upload_port=esp32_/{print $2}' platformio.ini`

echo $TARGET

# ensure compilable
rm -fR .pio
pio run

# wait esp32_05 coming online
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
