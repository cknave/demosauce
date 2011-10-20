#!/bin/sh
# a small script to build the source client
# here is the deal: I don't like make or ./configure. to change the build configuration you'll
# have to edit this file. check the comments to see what do do.
# remember to run installDependencies.sh before you compile for the first time

# swap to create a debug build
cflags='-Wall -O2' 
#cflags='-Wall -g -DDEBUG'

#compiler
CXX=g++

compile() {
    echo $CXX $@
    $CXX $@
    if test $? -ne 0; then exit 1; fi
}

compile $cflags -c logror.cpp

# comment/uncomment the next 5 lines to disable/enable LADSPA effect support
cflags_ladspa="-DENABLE_LADSPA"
ladspa_o="ladspahost.o"
ldl="-ldl"
compile $cflags -c ladspahost.cpp
compile $cflags -o ladspainfo ladspainfo.cpp logror.o ladspahost.o -ldl -lboost_filesystem-mt -lboost_date_time-mt

# comment/uncomment next 4 line to disable/enable BASS playback support
cflags_bass="-DENABLE_BASS"
bass_o="basssource.o"
lbass="-Lbass -Wl,-rpath=bass -lbass -lid3tag -lz"
compile $cflags -c basssource.cpp

# swap next lines to use your distro's ffmpeg
lavcodec="-Lffmpeg -Wl,-rpath=ffmpeg -lavcodec -lavformat"
compile $cflags -Iffmpeg -c avsource.cpp
#lavcodec="-lavcodec -lavformat"
#compile $cflags -c avsource.cpp

compile $cflags -c demosauce.cpp
compile $cflags $cflags_bass $cflags_ladspa -I. -c shoutcast.cpp
compile $cflags $cflags_bass -c scan.cpp
compile $cflags $cflags_ladspa -c settings.cpp
#compile $cflags $cflags_bass -c preview.cpp
compile $cflags -c convert.cpp
compile $cflags -c effects.cpp
compile $cflags -c sockets.cpp

#link scan
input="scan.o avsource.o effects.o logror.o convert.o $bass_o $replaygain_a"
libs="-lsamplerate -lboost_system-mt -lboost_date_time-mt"
compile -o scan $input $libs $lavcodec $lbass

#link demosauce
input="settings.o demosauce.o avsource.o convert.o effects.o logror.o sockets.o shoutcast.o $bass_o $ladspa_o"
libs="-lshout -lsamplerate -lboost_system-mt -lboost_thread-mt -lboost_filesystem-mt -lboost_program_options-mt -lboost_date_time-mt"
compile -o demosauce $input $libs $lavcodec $lbass $ldl `icu-config --ldflags`

# clean up
rm -f *.o

