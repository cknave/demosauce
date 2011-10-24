#!/bin/sh
if test `uname -s` = 'Linux'; then
    sofile='libbass.so'
    if test `uname -m` = 'x86_64'; then sofile="x64/$sofile"; fi
    wget -nc 'http://us.un4seen.com/files/bass24-linux.zip'
    unzip -oj bass24-linux.zip bass.h $sofile
    exit 0
else
    echo unsupported OS
    exit 1
fi
