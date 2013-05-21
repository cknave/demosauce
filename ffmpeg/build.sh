#!/bin/sh
# old releases for testing
#release='ffmpeg-0.5.12'
#release='ffmpeg-0.6.6'
#release='ffmpeg-0.7.15'
#release='ffmpeg-0.8.14'
#release='ffmpeg-0.9.2'
#release='ffmpeg-0.10.7'
#release='ffmpeg-0.11.3'
#release='ffmpeg-1.0.7'
#release='ffmpeg-1.1.5'
release='ffmpeg-1.2.1'
source_tar="$release.tar.bz2"
source_url="http://www.ffmpeg.org/releases/$source_tar"
dir_install=`pwd`
flags_configure="--disable-debug --enable-static --disable-shared --enable-gpl --enable-nonfree --disable-doc --disable-ffmpeg --disable-ffplay --disable-ffprobe --disable-ffserver --disable-avdevice --disable-swscale --disable-network --disable-encoders --disable-muxers --disable-devices --disable-filters --disable-vaapi"

if test -f "$source_tar" -a -f "libavcodec.a"; then exit 0; fi

if test ! -f "$source_tar"; then
    rm -rf *.a
    wget $source_url
    if test $? -ne 0; then exit 1; fi
fi

tar -jxf $source_tar

cd $release

./configure --libdir=${dir_install} --shlibdir=${dir_install} --incdir=${dir_install} ${flags_configure}
if test $? -ne 0; then exit 1; fi

make
if test $? -ne 0; then exit 1; fi

make install
if test $? -ne 0; then exit 1; fi

cd ..

rm -rf $release
