#!/bin/sh

source_tar='ffmpeg-0.6.tar.bz2'
source_url="http://www.ffmpeg.org/releases/$source_tar"
dir_ffmpeg='ffmpeg-0.6'
dir_install=`pwd`
#--disable-debug --disable-sse --disable-ssse3
flags_configure="--enable-shared --enable-gpl --enable-nonfree --disable-doc --disable-ffmpeg --disable-ffplay --disable-ffprobe --disable-ffserver --disable-avdevice --disable-swscale --disable-network --disable-encoders --disable-muxers --disable-devices --disable-filters"

if test -f "libavformat.so" -a  -f "libavcodec.so"; then exit 0; fi

if test ! -f "$source_tar"; then
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

make clean

cd ..
