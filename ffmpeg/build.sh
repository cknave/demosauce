#!/bin/sh
source_tar='ffmpeg-1.1.3.tar.bz2'
source_url="http://www.ffmpeg.org/releases/$source_tar"
dir_ffmpeg='ffmpeg-1.1.3'
dir_install=`pwd`
flags_configure="--disable-debug --enable-static --disable-shared --enable-gpl --enable-nonfree --disable-doc --disable-ffmpeg --disable-ffplay --disable-ffprobe --disable-ffserver --disable-avdevice --disable-swscale --disable-network --disable-encoders --disable-muxers --disable-devices --disable-filters --disable-vaapi"

if test -f "$source_tar" -a -f "libavcodec.a"; then exit 0; fi

if test ! -f "$source_tar"; then
    rm -rf *.a
    echo "attempting to download $source_url"
    wget "$source_url"
    if test $? -ne 0; then exit 1; fi
fi

tar -jxf "$source_tar"

cd "$dir_ffmpeg"

./configure --libdir=${dir_install} --shlibdir=${dir_install} --incdir=${dir_install} ${flags_configure}
if test $? -ne 0; then exit 1; fi

make
if test $? -ne 0; then exit 1; fi

make install
if test $? -ne 0; then exit 1; fi

cd ..

rm -rf $dir_ffmpeg
