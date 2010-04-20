#!/bin/sh

dir_ffmpeg='ffmpeg'
dir_install=`pwd`
#--disable-debug
flags_configure="--enable-shared --enable-gpl --enable-nonfree --disable-doc --disable-ffmpeg --disable-ffplay --disable-ffprobe --disable-ffserver --disable-avdevice --disable-swscale --disable-network --disable-encoders --disable-muxers --disable-devices --disable-filters --disable-sse --disable-ssse3"

echo 'fetching latest ffmpeg'
if test ! -d "$dir_ffmpeg"; then
	svn checkout svn://svn.ffmpeg.org/ffmpeg/trunk ffmpeg
	if test $? -ne 0; then echo 'svn checkout failed'; exit 1; fi
fi

cd "$dir_ffmpeg"

svn update
if test $? -ne 0; then echo 'svn update failed'; exit 1; fi

./configure --libdir=${dir_install} --shlibdir=${dir_install} --incdir=${dir_install} ${flags_configure}
if test $? -ne 0; then exit 1; fi

make
if test $? -ne 0; then exit 1; fi

make install
if test $? -ne 0; then exit 1; fi

make clean

cd ..
