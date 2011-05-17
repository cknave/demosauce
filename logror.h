/*
*   demosauce - icecast source client
*
*   this source is published under the gpl license. google it yourself.
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

/*
    logging and "error handling" stuff
    to log, use
    LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR, LOG_FATAL
    LOG_DEBUG will only be compiled with DEBUG macro

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

#ifndef _H_LOGROR_
#define _H_LOGROR_

#include <string>

#include <boost/format.hpp>

namespace logror
{

enum Level
{
    debug,
    info,
    warning,
    error,
    fatal,
    nothing
};

class LogBlob
{
public:
    LogBlob (Level level, bool takeAction, std::string message);
    virtual ~LogBlob();
    template<typename T> LogBlob & operator , (T right);
private:
    Level const level;
    bool take_action;
    boost::format formater;
};

template <typename T>
LogBlob& LogBlob::operator , (T right)
{
    if (level != nothing)
        formater % right;
    return *this;
}

LogBlob log_action(Level level, bool take_action, std::string message);

}

#ifdef DEBUG
    #define LOG_DEBUG(message) logror::log_action(logror::debug, false, message)
#else
    // got this idea from http://www.ddj.com/developement-tools/184401612
    // optimizing compiler should remove dead code
    #define LOG_DEBUG(message) if(false) logror::log_action(logror::nothing, false, "")
#endif

#define LOG_INFO(message) logror::log_action(logror::info, false, message)
#define LOG_WARNING(message) logror::log_action(logror::warning, false, message)
#define LOG_ERROR(message) logror::log_action(logror::error, false, message)
#define LOG_FATAL(message) logror::log_action(logror::fatal, false, message)
#define ERROR(message) logror::log_action(logror::error, true, message)
#define FATAL(message) logror::log_action(logror::fatal, true, message)

void log_set_console_level(logror::Level level);
void log_set_file_level(logror::Level level);
void log_set_file(std::string fileName, logror::Level level = logror::info);
bool log_string_to_level(std::string levelString, logror::Level& level);

#endif
