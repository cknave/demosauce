/*
*	applejuice music player
*	this is beerware! you are strongly encouraged to invite the authors of 
*	this software to a beer if you happen to run into them.
*	also, this code is licensed under teh GPL, i guess. whatever.
*	copyright 'n shit: year MMX by maep
*/

#ifndef _ABSTRACTPLUGIN_H_
#define _ABSTRACTPLUGIN_H_

#include "audiostream.h"

/* 	metadata keys:
*	codec_type
*	artist
*	title
*	album
*/

class AbstractSource : public Machine
{
public:
	// manipulators
	// deprecated, should work on randoooooooooom(key stuck) access streams instead
	virtual bool load(std::string url) = 0; 
	virtual void seek(uint64_t frame) = 0;

	// observers
	virtual uint64_t length() const = 0;
	virtual uint32_t channels() const = 0;
	virtual uint32_t samplerate() const = 0;
	virtual bool seekable() const = 0;
	virtual std::string metadata(std::string key) const = 0;
	// implement settings interface
};

typedef boost::shared_ptr<AbstractSource> SourcePtr;

class AbstractSink
{
public:
	// manipulators
	template<typename T> void set_source(T& machine);
	virtual void set_source(MachinePtr& machine) = 0;
	virtual void start() = 0;
	virtual void stop() = 0;

	// observers
	virtual bool active() = 0;
	// implement settings interface
};

template<typename T>
inline void AbstractSink::set_source(T& machine)
{
	MachinePtr base_machine = boost::static_pointer_cast<Machine>(machine);
	set_source(base_machine);
}

class AbstractEffect: public Machine
{
	// todo implement settings interface
};

#endif