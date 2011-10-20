#!/bin/sh
OUTPUT='libreplaygain.a'
CMD="gcc -Wall -std=gnu99 -O3 -c gain_analysis.c replay_gain.c"
echo $CMD
$CMD
if test $? -eq 0; then
	rm -f $OUTPUT
	CMD="ar rs $OUTPUT *.o"
	echo $CMD
	$CMD
	rm *.o
fi
