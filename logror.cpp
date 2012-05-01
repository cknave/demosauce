/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <cstdarg>
#include <queue>

#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>

#include "logror.h"

#define TEN_MINUTES ((time_t)(CLOCKS_PER_SEC * 60 * 60))

using std::string;

static LogLevel console_level = log_off;
static LogLevel file_level = log_off;
static FILE* logfile = 0;
static std::queue<time_t> error_times;

void log_set_console_level(LogLevel level)
{
    console_level = level;
}

void log_set_file_level(LogLevel level)
{
    file_level = level;
}

void log_set_file(const char* file_name, LogLevel level)
{
    time_t rawtime;
    char buf[30] = {0};
    if (level == log_off)
        return;

    time(&rawtime);
    if (!strftime(buf, 30, "%d.%m.Y %H:%M", localtime(&rawtime)))
        buf[0] = 0;
    string tmp_name = file_name;
    boost::replace_all(tmp_name, "%date%", "%1%");
    boost::format formater(tmp_name);
    formater.exceptions(boost::io::no_error_bits);
    tmp_name = str(formater % buf);

    logfile = fopen(tmp_name.c_str(), "w");
    if (!logfile)
        puts("WARNING: could not open log file");
    else 
        file_level = level;
}

static void fvlog(FILE* f, LogLevel lvl, const char* fmt, va_list args)
{
    static const char* levels[] = {"DEBUG", "INFO ", "WARN ", "ERROR", "ERROR", "DOOOM", "DOOOM"};
    time_t rawtime;
    char buf[30] = {0};
    time(&rawtime);
    if (!strftime(buf, 30, "%d.%m.%Y %X", localtime(&rawtime)))
        buf[0] = 0;
    fprintf(f, "%s %s ", levels[lvl], buf);
    vfprintf(f, fmt, args);
    fputc('\n', f);
    fflush(f);
}

void log_log(LogLevel lvl, const char* fmt, ...)
{
    if (lvl == log_off || (lvl < file_level && lvl < console_level)) 
        return;

    bool quit = (lvl == log_fatal_quit);
    if (lvl == log_error_quit) {
        time_t now = clock();
        error_times.push(now);
        if (error_times.size() > 10)
            error_times.pop();
        quit |= (error_times.size() >= 10) && (error_times.front() > (now - TEN_MINUTES));
    }

    if (lvl != log_off) {
        va_list args;

        if (lvl >= console_level) {
            va_start(args, fmt);
            fvlog(stdout, lvl, fmt, args);
            va_end(args);
        }

        if (lvl >= file_level) {
            va_start(args, fmt);
            fvlog(logfile, lvl, fmt, args);
            va_end(args);
        }
    }

    if (quit) {
        puts("terminted\n");
        if (logfile) fputs("terminated\n", logfile);
        exit(EXIT_FAILURE);
    }
}

bool log_string_to_level(const char* name, LogLevel* level)
{
    static const char* lstr[] = {"debug", "info", "warn", "error", "fatal", "off", "nothing"};
    static const LogLevel llvl[] = {log_debug, log_info, log_warn, log_error, log_fatal, log_off, log_off};
    string str = name;
    boost::to_lower(str);
    for (int i = 0; i < 7; i++) {
        if (str == lstr[i]) {
            *level = llvl[i];
            return true;
        }
    }
    return false;
}
