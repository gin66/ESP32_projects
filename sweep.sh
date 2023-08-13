#!/bin/sh

rm -fR sweep
mkdir sweep

i=0
while [ $i -le 255 ]
do
	echo $i
	i=$((i+1))
	curl -o sweep/$i.html -m 5 http://192.168.1.$i &
done

