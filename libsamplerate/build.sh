#!/bin/sh

source_dir='libsamplerate-0.1.7'
source_tar='libsamplerate-0.1.7.tar.gz'
source_url="http://www.mega-nerd.com/SRC/$source_tar"

if test -f "libsamplerate.a"; then exit 0; fi
rm -rf "$source_dir"

if test ! -f "$source_tar"; then
	echo "attempting to download $source_url"
	wget "$source_url"
	if test $? -ne 0; then exit 1; fi
fi

tar -zxf "$source_tar"

cd "$source_dir"
./configure
make
cd ..

cp "$source_dir/src/samplerate.h" .
cp "$source_dir/src/.libs/libsamplerate.a" .

rm -rf "$source_dir"
