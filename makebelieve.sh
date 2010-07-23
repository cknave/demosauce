#!/bin/bash
#builds the backend

svn_revision=`svnversion .`
flags="-Wall -Wfatal-errors -Iffmpeg -DREVISION_NR=$svn_revision"
flags_debug='-g -DDEBUG'
flags_release='-s -O3 -mtune=native -msse2 -mfpmath=sse'
libs_ffmpeg='-Lffmpeg -Wl,-rpath=ffmpeg -lavcodec -lavformat'

replaygain_a='libreplaygain/libreplaygain.a'
avcodec_so='ffmpeg/libavcodec.so'

src_common='avsource.cpp convert.cpp basscast.cpp effects.cpp basssource.cpp sockets.cpp logror.cpp'

src_scan='scan.cpp'
input_scan="avsource.o effects.o logror.o basssource.o $replaygain_a"
libs_scan="$libs_ffmpeg -lbass -lbass_aac -lbassflac -lsamplerate -lboost_system-mt -lboost_date_time-mt"

src_demosauce='settings.cpp  demosauce.cpp'
input_demosauce="avsource.o convert.o effects.o logror.o basssource.o sockets.o basscast.o $samplerate_a"
libs_demosauce="$libs_ffmpeg -lbass -lbassenc -lbass_aac -lbassflac -lsamplerate -lboost_system-mt -lboost_thread-mt -lboost_filesystem-mt -lboost_program_options-mt -lboost_date_time-mt"

build_debug=
build_rebuild=
build_lazy=

for var in "$@"
do
	case "$var" in
	'debug') build_debug=1;;
	'rebuild') build_rebuild=1;;
	'lazy') build_lazy=1;;
	'clean') rm -f *.o; exit 0;;
	esac
done

echo -n "configuration: "
if test $build_debug; then
	echo -n 'debug'
	flags="$flags $flags_debug" #-pedantic
else
	echo -n 'release'
	flags="$flags $flags_release"
fi

if test `uname -m` = 'x86_64'; then
	echo ' 64 bit'
	dir_bass='bass/bin_linux64'
else
	echo ' 32 bit'
	dir_bass='bass/bin_linux'
fi

# libreplaygain
if test ! -f "$replaygain_a" -o "$build_rebuild"; then
	cd libreplaygain
	./build.sh ${build_debug:+debug}
	if test $? -ne 0; then exit 1; fi
	cd ..
fi

# ffmpeg
if test ! -f "$avcodec_so" -o "$build_rebuild"; then
	cd ffmpeg
	./build.sh
	if test $? -ne 0; then exit 1; fi
	cd ..
fi

echo "building common"
for input in $src_common
do
	output=${input/%.cpp/.o}
	if test "$input" -nt "$output" -o ! "$build_lazy"; then
		g++ $flags -c $input -o $output
		if test $? -ne 0; then exit 1; fi
	fi
done

flags_bass="-L$dir_bass -Wl,-rpath=$dir_bass"

echo 'building scan'
g++ -o scan $src_scan $flags $flags_bass $libs_scan $input_scan
if test $? -ne 0; then exit 1; fi

echo 'building demosauce'
g++ -o demosauce $src_demosauce $flags $flags_bass $libs_demosauce `icu-config --ldflags` $input_demosauce
if test $? -ne 0; then exit 1; fi

if test ! $build_lazy; then rm -f *.o; fi
