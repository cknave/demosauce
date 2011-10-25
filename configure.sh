#!/bin/sh
CXX=g++
CFLAGS="-Wall -O2"
#remove old ouput files
rm -f config.h
rm -f makebelieve.sh

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
    result=$?
    cd $old_dir
    return $result
}

build() {
    BUILD="${BUILD}compile \$CFLAGS $1\n"
}

# requirements
if ! check_header "<shout/shout.h>"; then echo 'libshout missing'; exit 1; fi
if ! check_header "<unicode/ucnv.h>"; then echo 'libicu missing'; exit 1; fi
if ! check_header "<boost/version.hpp>"; then echo 'libboost missing'; exit 1; fi

build '-c logror.cpp'

# ladspa
if check_header '<ladspa.h>'; then
    CFLAGS="$CFLAGS -DENABLE_LADSPA"
    LDL="-ldl"
    LADSPAO="ladspahost.o"
    build '-c ladspahost.cpp'
    build '-o ladspainfo ladspainfo.cpp logror.o ladspahost.o -ldl -lboost_filesystem-mt -lboost_date_time-mt'
fi

# bass
check_bass() {
    if check_header '"bass/bass.h"' && check_file "bass/libbass.so"; then
        if ! check_header "<id3tag.h>"; then echo 'libid3tag missing'; exit 1; fi
        CFLAGS="$CFLAGS -DENABLE_BASS"
        BASSO="basssource.o"
        BASSL="-Lbass -Wl,-rpath=bass -lbass -lid3tag -lz"
        build '-c basssource.cpp'
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

echo "due to problems with libavcodec on some distros you can build a custom"
echo "version. in general, the distro's libavcodec should be preferable, but"
echo -n "might be incompatible with demosauce. "
if ask "use custom libavcodec?"; then
    run_script build.sh ffmpeg
    if test $? -ne 0; then echo 'error while building libavcodec'; exit 1; fi
    AVCODECL="-Lffmpeg -Wl,-rpath=ffmpeg -lavcodec -lavformat"
    build '-Iffmpeg -c avsource.cpp'
else
    if ! check_header '<libavcodec/avcodec.h>'; then echo 'libavcodec missing'; exit 1; fi
    if ! check_header '<libavformat/avformat.h>'; then echo 'libaformat missing'; exit 1; fi
    AVCODECL="-lavcodec -lavformat"
    build '-c avsource.cpp'
fi

# other build steps
build '-c demosauce.cpp'
build '-I. -c shoutcast.cpp'
build '-c scan.cpp'
build '-c settings.cpp'
build '-c convert.cpp'
build '-c effects.cpp'
build '-c sockets.cpp'

INPUT="scan.o avsource.o effects.o logror.o convert.o $BASSO libreplaygain/libreplaygain.a"
LIBS="-lsamplerate -lboost_system-mt -lboost_date_time-mt"
build "-o scan $INPUT $LIBS $AVL $BASSL $AVCODECL"

INPUT="settings.o demosauce.o avsource.o convert.o effects.o logror.o sockets.o shoutcast.o $BASSO $LADSPAO"
LIBS="-lshout -lsamplerate -lboost_system-mt -lboost_thread-mt -lboost_filesystem-mt -lboost_program_options-mt -lboost_date_time-mt"
build "compile -o demosauce $INPUT $LIBS $BASS $AVCODECL $LDL `icu-config --ldflags`"

# generate build script
echo -e "#!/bin/sh\n#generated build script\nCFLAGS='$CFLAGS'" >> makebelieve.sh
echo -e "compile(){\n\techo $CXX \$@\n\tif ! $CXX \$@; then exit 1; fi\n}" >> makebelieve.sh
echo -e "$BUILD\nrm -f *.o" >> makebelieve.sh
chmod a+x makebelieve.sh

echo "run ./makebelieve to build demosauce"
