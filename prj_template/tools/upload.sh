#!/bin/bash

TARGET=`gawk 'BEGIN{FS=" *= *"} /hostname/ && /esp32_/{print $2".local"}' platformio.ini`

echo $TARGET

if [ "$TARGET" == "" ]
then
	echo Cannot get target
	exit
fi

if [ ! -f "~/.cargo/bin/websocat" ]
then
	cargo install websocat
fi

# ensure compilable
rm -fR .pio

(cd ..;make links)

pio run

# wait esp coming online
while true
do
	echo -n p
	ping -c 1 $TARGET
	if [ $? == 0 ]
	then
		break
	fi
	sleep 1
done

while true
do
	echo -n n
	nc -z $TARGET 3232
	echo $?
	if [ $? == 0 ]
	then
		break
	fi
	sleep 1
done

# upload
pio run --target upload

