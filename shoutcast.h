/*
*   demosauce - fancy icecast source client
*
*   this source is published under the gpl license. google it yourself.
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#ifndef _H_SHOUTCAST_
#define _H_SHOuTCAST_

#include <boost/utility.hpp>
#include <boost/scoped_ptr.hpp>

struct ShoutCastPimpl;

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
