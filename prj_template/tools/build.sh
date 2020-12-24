#!/bin/bash

# ensure compilable
rm -fR .pio

(cd ..;make links)

pio run

