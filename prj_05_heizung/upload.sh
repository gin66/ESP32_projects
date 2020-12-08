#!/bin/bash

# ensure compilable
rm -fR .pio
pio run

# wait esp32_05 coming online
while true
do
	ping -c 1 esp32_05.local
	if [ $? == 0 ]
	then
		break
	fi
done

# upload
pio run --target upload
