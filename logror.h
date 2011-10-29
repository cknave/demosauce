/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*
*   logging and "error handling" stuff. to log use macros:
*   LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR, LOG_FATAL
*   LOG_DEBUG will only be compiled without NDEBUG macro
*
*   you can use it like printf:
*   LOG_INFO("foo"); // logs "INFO  <time> foo"
*   int foo = 10; string bar = mongo-moose
*   LOG_DEBUG("i see %1% %2%!", foo, bar.s_str());
*   // logs "DEBUG <time> i see 10 mongo-moose!"
*
*   for error "handling", use ERROR and FATAL macros:
*   ERROR("DOOOOOM!! /o\ message: %1%"), error_message;
*   FATAL("FFFFFFFFUUUUUUUUUUUUUUUUUUUU %1%"), reason;
*
*   ERROR keeps track of the last errors and calls exit(1) after
*   after more than 10 errors in less than 10 minutes
*   FATAL will exit immediateloy after logging
*
*   functions for changing logging behaviour:
*   void log_set_console_level(Level level);
*   void log_set_file_level(Level level);
*   void log_set_file(string file_name, Level level);
*   function for converting a string to log level
*   bool log_string_to_level(string level_string, Level* level);
*/

#ifndef LOGROR_H
#define LOGROR_H

enum LogLevel
{
    log_debug = 0,
    log_info,
    log_warn,
    log_error,
    log_error_quit,
    log_fatal,
    log_fatal_quit,
    log_off
};

#ifndef NDEBUG
    #define LOG_DEBUG(...) log_log(log_debug, __VA_ARGS__)
#else
    #define LOG_DEBUG(...)
#endif

#define LOG_INFO(...) log_log(log_info, __VA_ARGS__) 
#define LOG_WARN(...) log_log(log_warn, __VA_ARGS__) 
#define LOG_ERROR(...) log_log(log_error __VA_ARGS__) 
#define LOG_FATAL(...) log_log(log_fatal __VA_ARGS__) 
#define ERROR(...) log_log(log_error_quit, __VA_ARGS__)
#define FATAL(...) log_log(log_fatal_quit, __VA_ARGS__)

void log_log(LogLevel lvl, const char* fmt, ...);
void log_set_console_level(LogLevel level);
void log_set_file_level(LogLevel level);
void log_set_file(const char* file, LogLevel level);
bool log_string_to_level(const char* name, LogLevel* level);

#endif
