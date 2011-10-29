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
#include <queue>
#include <fstream>
#include <iostream>

#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "logror.h"

using std::string;
using std::cout;
using std::endl;
using namespace boost::posix_time;

logror::Level console_level = logror::off;
logror::Level file_level = logror::off;
std::ofstream log_stream;
std::queue<ptime> error_times;

void log_set_console_level(logror::Level level)
{
    console_level = level;
}

void log_set_file_level(logror::Level level)
{
    if (log_stream.is_open() && !log_stream.fail()) {
        file_level = level;
    }
}

void log_set_file(string file_name, logror::Level level)
{
    // don't throw exception if something fails
    log_stream.exceptions(std::ifstream::goodbit);

    if (level == logror::off) {
        return;
    }

    string time = to_simple_string(second_clock::local_time());
    string tmp_name = file_name;
    boost::replace_all(tmp_name, "%date%", "%1%");

    boost::format formater(tmp_name);
    formater.exceptions(boost::io::no_error_bits);
    tmp_name = str(formater % time);

    log_stream.open(tmp_name.c_str());
    if (log_stream.fail()) {
        cout << "WARNING: could not open log file" << endl;
    } else {
        file_level = level;
    }
}

namespace logror {

LogBlob log_action(Level level, bool take_action, string message)
{
    if (level != off && (level >= file_level || level >= console_level)) {
        string msg;
        msg.reserve(160); // should be enough for most
        switch (level) {
            case debug: msg.append("DEBUG "); break;
            case info:  msg.append("INFO  "); break;
            case warn:  msg.append("WARN  "); break;
            case error: msg.append("ERROR "); break;
            case fatal: msg.append("DOOM  "); break;
            default:;
        }
        msg.append(to_simple_string(second_clock::local_time()));
        msg.append(" ");
        msg.append(message);
        return LogBlob(level, take_action, msg);
    }
    return LogBlob(off, take_action, "");
}

LogBlob::LogBlob (Level level, bool take_action, string message):
    level(level),
    take_action(take_action),
    formater(message)
{
    formater.exceptions(boost::io::no_error_bits);
}

void LogBlob::flush() 
{
    bool fatalQuit = take_action && (level == fatal);
    bool errorQuit = false;
    if (level == error) {
        ptime now = second_clock::local_time();
        error_times.push(now);
        if (error_times.size() > 10) {
            error_times.pop();
        }
        errorQuit = take_action && (error_times.size() > 9) && (error_times.front() > (now - minutes(10)));
    }
    if (level != off) {
        string msg = str(formater);
        if (fatalQuit) {
            msg.append("\nterminated (fatal error)");
        }
        if (errorQuit) {
            msg.append("\nterminated (too many errors)");
        }
        if (level >= console_level) {
            cout << msg << endl;
        }
        if (level >= file_level) {
            log_stream << msg << endl;
        }
    }
    if (fatalQuit || errorQuit) {
        exit(EXIT_FAILURE);
    }
}

}

bool log_string_to_level(string level_string, logror::Level& level)
{
    string str = level_string;
    boost::to_lower(str);
    if (str == "debug") {
        level = logror::debug;
    } else if (str == "info") {
        level = logror::info;
    } else if (str == "warn") {
        level = logror::warn;
    } else if (str == "error") {
        level = logror::error;
    } else if (str == "fatal") {
        level = logror::fatal;
    } else if (str == "off" || str == "nothing") {
        level = logror::off;
    } else {
        return false;
    }
    return true;
}
