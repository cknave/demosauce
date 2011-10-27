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
    logging and "error handling" stuff
    to log, use
    LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR, LOG_FATAL
    LOG_DEBUG will only be compiled without NDEBUG macro

    example:
    LOG_INFO("something unimportant happend");
    int foo = 10;
    string bar = mongo-moose
    LOG_DEBUG("i see %1% %2%!"), foo, bar; // prints "i see 10 mongo-moose!"

    for error "handling", use
    ERROR, FATAL
    example:
    ERROR("DOOOOOM!! /o\ message: %1%"), error_message;
    FATAL("FFFFFFFFUUUUUUUUUUUUUUUUUUUU %1%"), reason;

    ERROR keeps track of the last errors and calls exit(1) if too many errors appear
    (currently 10 errors in less than 10 minutes)
    FATAL logs the message and then calls exit(1).

    other functions you might need:
    void log_set_console_level(Level level);
    void log_set_file_level(Level level);
    void log_set_file(string file_name, Level level);
    bool log_string_to_level(string level_string, Level& level);
*/

#ifndef LOGROR_H
#define LOGROR_H

#include <string>
#include <boost/format.hpp>

namespace logror
{

enum Level
{
    debug = 0,
    info,
    warn,
    error,
    fatal,
    nothing
};

class LogBlob
{
public:
    LogBlob (Level level, bool takeAction, std::string message);
    virtual ~LogBlob();
    template<typename T> LogBlob & operator , (T& right);
private:
    Level const level;
    bool take_action;
    boost::format formater;
};

template <typename T> LogBlob& LogBlob::operator , (T& right)
{
    if (level != nothing)
        formater % right;
    return *this;
}

LogBlob log_action(Level level, bool take_action, std::string message);

}
// add terminator function like endl
#define LOG(lvl, act, msg, ...) logror::log_action(lvl, act, msg), __VA_ARGS__

#ifndef NDEBUG
    #define LOG_DEBUG(...) LOG(logror::debug, false, __VA_ARGS__, "") 
#else
    #define LOG_DEBUG(...)
#endif

#define LOG_INFO(...) LOG(logror::info, false, __VA_ARGS__, "") 
#define LOG_WARN(...) LOG(logror::warn, false, __VA_ARGS__, "") 
#define LOG_ERROR(...) LOG(logror::error, false, __VA_ARGS__, "") 
#define LOG_FATAL(...) LOG(logror::fatal, false, __VA_ARGS__, "") 
#define ERROR(...) LOG(logror::error, true, __VA_ARGS__, "")
#define FATAL(...) LOG(logror::fatal, true, __VA_ARGS__, "")

void log_set_console_level(logror::Level level);
void log_set_file_level(logror::Level level);
void log_set_file(std::string fileName, logror::Level level = logror::info);
bool log_string_to_level(std::string levelString, logror::Level& level);

#endif
