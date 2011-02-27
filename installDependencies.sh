#!/bin/bash
# Check the distribution and install dependencies
if [ `whoami` != "root" ] ; then
	echo "To run installDependencies.sh you have to be root (install packages)"
	exit
fi

# Debian (and Ubuntu obviously)
if [ -a /etc/debian_version ] ; then
	aptitude -y install g++ libboost1.42-dev libboost-system-dev libboost-date-time-dev libboost-thread-dev libboost-filesystem-dev libboost-program-options-dev libsamplerate-dev libicu-dev lame
	exit
fi

# RedHat (and Fedora)
if [ -a /etc/redhat-release ] ; then
	yum -y install gcc-c++ boost-devel libsamplerate-devel libicu-devel lame
	exit
fi

# Slackware
if [ -a /etc/slackware-version ] ; then
	exit
fi

# Gentoo
# G++ is assumed already installed
if [ -a /etc/gentoo-release ] ; then
	emerge -avuDN boost libsamplerate icu lame
	exit
fi

