/*
	when I started working on this I never used boost asio or sockets in general before. I got it
	to work quickly and for now it works. boost::asio is a bit overwhelming at first and now I saw 
	they provide services (datagram_socket_service) which might be exactly what we need. If we change
	the socket implementaion we might as well use that (should be more robust). ~meap
*/

#ifndef _H_SOCKETS_
#define _H_SOCKETS_

#include <string>

#include <boost/cstdint.hpp>
#include <boost/utility.hpp>
#include <boost/scoped_ptr.hpp>
	
struct SongInfo
{
	// all strings are in utf-8
	std::string fileName;
	std::string artist; 
	std::string title;
	float gain;
	float loopDuration;
	SongInfo() : gain(0), loopDuration(0) {}
};

class Sockets : boost::noncopyable
{
	public:
		Sockets(std::string const & host, uint32_t const port);
		virtual ~Sockets();

		/**	Tries to obtain the information from the designated host.
		*	Guarantees to return or fail with style. If something goes wrong,
		*	the values from error_tune and error_title will be used.
		*	@param info any content may be overwritten.
		*/
		void GetSong(SongInfo & info);
		
	private:
		struct Pimpl;
		boost::scoped_ptr<Pimpl> pimpl;
};

// helper function, bass seems to be a bit buggy on that part
bool ResolveIp(std::string host, std::string &ipAddress);

#endif
