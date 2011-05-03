#!/bin/sh
# Check the distribution and install dependencies
if [ `whoami` != "root" ] ; then
	echo "To run installDependencies.sh you have to be root (install packages)"
	exit
fi

# Debian (and Ubuntu obviously)
if [ -f /etc/debian_version ] ; then
	aptitude -y install g++ libboost1.42-dev libboost-system-dev libboost-date-time-dev libboost-thread-dev libboost-filesystem-dev libboost-program-options-dev libsamplerate-dev libshout-dev libicu-dev lame
	exit
fi

# RedHat (and Fedora)
if [ -f /etc/redhat-release ] ; then
	yum -y install gcc-c++ boost-devel libshout3-dev libsamplerate-devel libicu-devel lame
	exit
fi

# Slackware
if [ -f /etc/slackware-version ] ; then
	exit
fi

# Gentoo
# G++ is assumed already installed
if [ -f /etc/gentoo-release ] ; then
	emerge -avuDN boost libsamplerate libshout icu lame
	exit
fi

