/*
*	applejuice music player
*	this is beerware! you are strongly encouraged to invite the authors of 
*	this software to a beer if you happen to run into them.
*	also, this code is licensed under teh GPL, i guess. whatever.
*	copyright 'n shit: year MMX by maep
*/

#ifndef _AVSOURCE_H_
#define _AVSOURCE_H_

#include <string>

#include <boost/cstdint.hpp>
#include <boost/scoped_ptr.hpp>

#include "abstractplugin.h"

class AvSource : public AbstractSource
{
public:
	AvSource();
	virtual ~AvSource();
	static bool probe_name(std::string url);	

	// manipulators from AbstractSource
	bool load(std::string file_name);
	void process(AudioStream& stream, uint32_t const frames);
	void seek(uint64_t frame);

	// observers from AbstractSource
	std::string name() const;
	uint32_t channels() const;
	uint32_t samplerate() const;
	uint64_t length() const;
	float bitrate() const;
	bool seekable() const;
	std::string metadata(std::string key) const;

private:
	struct Pimpl;
	boost::scoped_ptr<Pimpl> pimpl;
};

#endif
