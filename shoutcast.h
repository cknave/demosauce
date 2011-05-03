#ifndef _H_SHOUTCAST_
#define _H_SHOuTCAST_

#include <boost/utility.hpp>
#include <boost/scoped_ptr.hpp>

struct ShoutCastPimpl; // cannot be private class, because callbacks need to know this

class ShoutCast : boost::noncopyable
{
public:
	ShoutCast();
	virtual ~ShoutCast();
	/**	Starts casting and keeps running. Never returns. */
	void Run();
private:
	boost::scoped_ptr<ShoutCastPimpl> pimpl;
};

#endif
