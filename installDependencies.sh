#!/bin/sh
# a small script to install required dependencies

# Check the distribution and install dependencies
if [ `whoami` != "root" ] ; then
	echo "To run installDependencies.sh you have to be root (install packages)"
	exit
fi

# Debian (and Ubuntu obviously)
if [ -f /etc/debian_version ] ; then
	aptitude -y install g++  lame ladspa-sdk libsamplerate-dev libshout-dev libid3tag0-dev libicu-dev libboost1.42-dev libboost-system-dev libboost-date-time-dev libboost-thread-dev libboost-filesystem-dev libboost-program-options-dev
	exit
fi

# RedHat (and Fedora)
if [ -f /etc/redhat-release ] ; then
	yum -y install gcc-c++ lame ladspa-sdk libsamplerate-devel libshout3-devel libid3tag-devel libicu-devel boost-devel
	exit
fi

# Slackware
if [ -f /etc/slackware-version ] ; then
	exit
fi

# Gentoo
# G++ is assumed already installed
if [ -f /etc/gentoo-release ] ; then
	emerge -avuDN lame ladspa-sdk libshout libsamplerate libid3tag icu boost
	exit
fi
