/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

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

class Sockets : boost::noncopyable
{
    public:
        Sockets(std::string host, uint32_t port);
        virtual ~Sockets();

        /** Tries to obtain the information from the demovibes host.
        *   Guarantees to return or fail with style.
        *   @return string that will contain a list of key/values.
        */
        std::string get_next_song();

    private:
        struct Pimpl;
        boost::scoped_ptr<Pimpl> pimpl;
};

// helper function, bass seems to be a bit buggy on that part
bool resolve_ip(std::string host, std::string& ipAddress);

#endif
