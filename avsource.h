#ifndef _H_AVSOURCE_
#define _H_AVSOURCE_

#include <string>

#include <boost/cstdint.hpp>
#include <boost/scoped_ptr.hpp>

#include "dsp.h"

class AvSource : public Machine
{
public:
	AvSource();
	virtual ~AvSource();
	bool Load(std::string fileName);
	static bool CheckExtension(std::string fileName);
	
	//overwrite
	void Process(AudioStream & stream, uint32_t const frames);
	std::string Name() const {return "AvCodec Source"; }
	
	uint32_t Channels() const;
	uint32_t Samplerate() const;
	uint32_t Bitrate() const;
	double Duration() const;
	std::string CodecType() const;

private:
	struct Pimpl;
	boost::scoped_ptr<Pimpl> pimpl;
};

#endif
