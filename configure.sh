#!/bin/sh
CXX=g++
CFLAGS='-Wall -O2 -s'
LDIR=''
# remove old output files
rm -f makebelieve.sh 

have_lib() {
    echo -n "checking for $1 ... "
    if ! pkg-config "--exists" $1 2>/dev/null; then
        echo "no"
        return 1
    fi
    echo "yes"
    return 0
}

assert_lib() {
    if ! have_lib "$1"; then 
        echo "missing lib: $1"
        exit 1
    fi
}

assert_version() {
    if ! pkg-config "--atleast-version=$2" "$1"; then
        echo "need $1 $2, have `pkg-config --modversion $1`"
        exit 1
    fi
    return 0
}

have_file() {
    echo -n "checking for $1 ... "
    if test -e "$1"; then
        echo "yes"
        return 0
    fi
    echo "no"
    return 1
}

have_exe() {
    echo -n "checking for $1 ... "
    which $1 >/dev/null 2>/dev/null
    if test $? -ne 0; then
        echo "no"
        return 1
    fi
    echo "yes"
    return 0
}

assert_exe() {
   if ! have_exe $1; then
       echo "missing: $1"
       exit 1
   fi
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

# build environment
assert_exe "$CXX"
assert_exe "pkg-config"

# in case there is local stuff
if test -d "/usr/local/include"; then CFLAGS="$CFLAGS -I/usr/local/include"; fi
if test -d '/usr/local/lib'; then LDIR='-L/usr/local/lib'; fi

# boost boost boost. y u no have pkg-config /o\ ?
echo -n 'checking for boost ... '
echo '#include <boost/version.hpp>' | $CXX $CFLAGS -E -xc++ -o /dev/null - 2>/dev/null
if test $? -ne 0; then printf "no\nmissing lib: boost\n"; exit 1; fi
echo 'main(){}' | $CXX $LDIR -xc++ -lboost_system-mt -o /dev/null - 2>/dev/null
if test $? -eq 0; then BS='-mt'; fi
echo 'yes'

# libshout 
assert_lib 'shout'
assert_version 'shout' '2.2.2'

# libsamplerate 
assert_lib 'samplerate'

# libicu
assert_exe 'icu-config'

# logger
build '-c logror.cpp'

# ladspa
if have_exe 'listplugins'; then
    CFLAGS="$CFLAGS -DENABLE_LADSPA"
    LADSPAO='ladspahost.o'
    build '-c ladspahost.cpp'
    build '-c ladspainfo.cpp'
    build 'ladspainfo.o logror.o ladspahost.o -ldl -lboost_system$BS -lboost_filesystem$BS -lboost_date_time$BS -o ladspainfo'
fi

# bass
check_bass() {
    if have_file 'bass/bass.h' && have_file 'bass/libbass.so'; then
        assert_lib 'id3tag'
        CFLAGS="$CFLAGS -DENABLE_BASS"
        BASSO='libbass.o basssource.o'
        BASSL="`pkg-config --libs id3tag` -ldl -lz"
        build '-I../bass -c libbass.c'
        build "`pkg-config --cflags id3tag` -I../bass -c basssource.cpp"
        return 0
    fi
    return 1
}

# only for linux
if test `uname -s` = 'Linux' && ! check_bass; then
    if ask '==> download BASS for mod playback?'; then
        assert_exe 'wget'
        assert_exe 'unzip'
        run_script getbass.sh bass
        if ! check_bass; then exit 1; fi
    fi 
fi

#ffmpeg
echo "==>  due to problems with libavcodec on some distros you can build a custom version. in general, the distro's libavcodec should be preferable, but might be incompatible with demosauce. you'll need the 'yasm' assember."
if ask "==> use custom libavcodec?"; then
    assert_exe 'yasm'
    assert_exe 'make'
    run_script build.sh ffmpeg
    if test $? -ne 0; then echo 'error while building libavcodec'; exit 1; fi
    AVCODECL="-Lffmpeg -pthread -lavformat -lavcodec -lavutil"
    build '-Iffmpeg -c avsource.cpp'
else
    assert_lib 'libavcodec'
    assert_lib 'libavformat'
    assert_lib 'libavutil'
    AVCFLAGS="`pkg-config --cflags libavformat libavcodec libavutil`"
    AVCODECL="`pkg-config --libs libavformat libavcodec libavutil`"
    echo '#include <avcodec.h>' | $CXX $CFLAGS $AVCFLAGS -E -xc++ -o /dev/null - 2> /dev/null
    if test $? -eq 0; then AVCFLAGS="-DAVCODEC_FIX0 $AVCFLAGS"; fi
    build "$AVCFLAGS -c avsource.cpp"
fi

# replaygain
if ! have_file 'libreplaygain/libreplaygain.a'; then
    run_script build.sh libreplaygain
fi

if have_exe 'ccache'; then
    CXX="ccache $CXX"
fi

# compile rest
build "-c demosauce.cpp"
build "`pkg-config --cflags shout` -I. -c shoutcast.cpp"
build "`pkg-config --cflags samplerate` -I../libreplaygain -c scan.cpp"
build " -c settings.cpp"
build "`pkg-config --cflags samplerate` -c convert.cpp"
build '-c effects.cpp'
build '-c sockets.cpp'

# link
INPUT="scan.o avsource.o effects.o logror.o convert.o $BASSO"
LIBS="-L../libreplaygain -lreplaygain -lboost_system$BS -lboost_filesystem$BS"
build "$LDIR $INPUT $LIBS $BASSL $AVCODECL `pkg-config --libs samplerate` -o scan"

INPUT="settings.o demosauce.o avsource.o convert.o effects.o logror.o sockets.o shoutcast.o $BASSO $LADSPAO"
LIBS="-lboost_system$BS -lboost_thread$BS -lboost_filesystem$BS -lboost_program_options$BS"
build "$LDIR $INPUT $LIBS $BASSL $AVCODECL `pkg-config --libs shout samplerate` `icu-config --ldflags` -o demosauce"

# generate build script
printf "#!/bin/sh\n#generated build script\nCFLAGS='$CFLAGS'\ncompile(){\n\techo $CXX \$@\n\tif ! $CXX \$@; then exit 1; fi\n}\nrm -f demosauce scan ladspainfo\ncd src\n$BUILD\nrm -f *.o" >> makebelieve.sh
chmod a+x makebelieve.sh

echo "run ./makebelieve to build demosauce"
