#!/bin/sh

SOURCE_FILES='gain_analysis.c replay_gain.c'
OUTPUT='libreplaygain.a'

echo -n "libreplaygain configuration: "
if test "$1" = "debug"; then
	echo "debug"
	FLAGS_RELEASE='-g -DDEBUG' #-pedantic
else
	echo "release"
	FLAGS_RELEASE='-s -O3 -mtune=native -msse2 -mfpmath=sse'
fi

FLAGS="-Wall -Wfatal-errors -c -std=gnu99 $FLAGS_RELEASE" 
gcc $FLAGS $SOURCE_FILES

if test $? -eq 0; then
	rm -f $OUTPUT
	ar rs $OUTPUT *.o
	rm *.o
fi
