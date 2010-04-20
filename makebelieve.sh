#!/bin/bash
#builds the backend

svn_revision=`svnversion .`
flags="-Wall -Wfatal-errors -Iffmpeg -DREVISION_NR=$svn_revision"
flags_debug='-g -DDEBUG'
flags_release='-s -O3 -mtune=native -msse2 -mfpmath=sse'
libs_ffmpeg='-Lffmpeg -Wl,-rpath=ffmpeg -lavcodec -lavformat'

replaygain_a='libreplaygain/libreplaygain.a'
samplerate_a='libsamplerate/libsamplerate.a'
avcodec_so='ffmpeg/libavcodec.so'

src_common='avsource.cpp convert.cpp basscast.cpp dsp.cpp basssource.cpp sockets.cpp logror.cpp'

src_scan='scan.cpp'
input_scan="avsource.o dsp.o logror.o basssource.o $replaygain_a"
libs_scan="$libs_ffmpeg -lbass -lbass_aac -lbassflac -lboost_system-mt -lboost_date_time-mt"

src_demosauce='settings.cpp  demosauce.cpp'
input_demosauce="avsource.o convert.o dsp.o logror.o basssource.o sockets.o basscast.o $samplerate_a"
libs_demosauce="$libs_ffmpeg -lbass -lbassenc -lbass_aac -lbassflac -lboost_system-mt -lboost_thread-mt -lboost_filesystem-mt -lboost_program_options-mt -lboost_date_time-mt"

src_applejuice='applejuice.cpp'
input_applejuice="avsource.o dsp.o convert.o logror.o basssource.o $replaygain_a $samplerate_a"
libs_applejuice="$libs_ffmpeg -lSDL -lSDL_image -lbass -lbass_aac -lbassflac -lboost_system-mt -lboost_date_time-mt"
res_applejuice='res/background.png res/buttons.png res/icon.png res/font_synd.png'

build_debug=
build_rebuild=
build_lazy=
build_applejuice=

for var in "$@"
do
	case "$var" in
	'debug') build_debug=1;;
	'rebuild') build_rebuild=1;;
	'lazy') build_lazy=1;;
	'aj') build_applejuice=1;;
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

# libsamplerate
if test ! -f "$samplerate_a" -o "$build_rebuild"; then
	cd libsamplerate
	./build.sh
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

if test $build_applejuice; then
	echo 'building applejuice'
	ld -r -b binary -o res.o $res_applejuice
	g++ -o applejuice -Wl,-rpath=. $src_applejuice $flags $flags_bass $libs_applejuice $input_applejuice res.o
	if test $? -ne 0; then exit 1; fi

else
	echo 'building scan'
	g++ -o scan $src_scan $flags $flags_bass $libs_scan $input_scan
	if test $? -ne 0; then exit 1; fi

	echo 'building demosauce'
	g++ -o demosauce $src_demosauce $flags $flags_bass $libs_demosauce `icu-config --ldflags` $input_demosauce
	if test $? -ne 0; then exit 1; fi

fi

if test ! $build_lazy; then rm -f *.o; fi
