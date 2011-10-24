#!/bin/sh
CXX=g++

#remove old ouput files
rm -f config.h
rm -f build2.h

check_header() {
    echo -n "checking for $1 ... "
    echo "#include $1" | $CXX -E -xc++ -o /dev/null - > /dev/null 2> /dev/null
    if test $? -ne 0; then
        echo "no"
        return 1
    fi
    echo "yes"
    return 0
}

check_file() {
    echo -n "checking for $1 ... "
    if test -e "$1"; then
        echo "yes"
        return 0
    fi
    echo "no"
    return 1
}

ask() {
    while true; do
        read -p "$1 [y/n] " reply
        case "$reply" in 
            Y|y) return 0;;
            N|n) return 1;;
        esac
    done
}

run_script() {
    old_dir=`pwd`
    if test -n "$2"; then cd $2; fi     
    ./$1
    cd $old_dir
}

build() {
    BUILD="$BUILD$1\n"
}

# requirements
if ! check_header "<shout/shout.h>"; then exit 1; fi
if ! check_header "<unicode/ucnv.h>"; then exit 1; fi
if ! check_header "<boost/version.hpp>"; then exit 1; fi

#ladspa
if check_header '<ladspa.h>'; then
    build 'LDL="-ldl"'
    build 'LADSPA="-DENABLE_LADSPA"'
    build 'LADSPAO="ladspahost.o"'
    build 'compile $CFLAGS -c ladspahost.cpp'
    build 'compile $CFLAGS -o ladspainfo ladspainfo.cpp logror.o ladspahost.o -ldl -lboost_filesystem-mt -lboost_date_time-mt'
fi

#bass
check_bass() {
    if check_header '"bass/bass.h"' && check_file "bass/libbass.so"; then
        build 'BASS="-DENABLE_BASS"'
        build 'BASSO="basssource.o"'
        build 'LBASS="-Lbass -Wl,-rpath=bass -lbass -lid3tag -lz"'
        build 'compile $CFLAGS -c basssource.cpp'
        return 0
    fi
    return 1
}

if ! check_bass; then
    if ask "download BASS for mod playback?"; then
        run_script getbass.sh bass
        if ! check_bass; then exit 1; fi
    fi 
fi 
