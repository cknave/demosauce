#ifndef _H_BASSCAST_
#define _H_BASSCAST_

#include <boost/utility.hpp>
#include <boost/scoped_ptr.hpp>

struct BassCastPimpl; // cannot be private class, because callbacks need to know this

class BassCast : boost::noncopyable
{
public:
	BassCast();
	virtual ~BassCast();
	/**	Starts casting and keeps running. Never returns. */
	void Run();
private:
	boost::scoped_ptr<BassCastPimpl> pimpl;
};

#endif
