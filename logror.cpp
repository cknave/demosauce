// build_lflags -lboost_date_time-mt

/*
*	applejuice music player
*	this is beerware! you are strongly encouraged to invite the authors of 
*	this software to a beer if you happen to run into them.
*	also, this code is licensed under teh GPL, i guess. whatever.
*	copyright 'n shit: year MMX by maep
*/

#include <cstdlib>
#include <queue>
#include <fstream>
#include <iostream>

#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "logror.h"

using namespace boost::posix_time;

logror::Level console_level = logror::warning;
logror::Level file_level = logror::nothing;
std::ofstream log_stream;
std::queue<ptime> error_times;

void log_set_console_level(logror::Level level)
{
	console_level = level;
}

void log_set_file_level(logror::Level level)
{
	if (log_stream.is_open() && !log_stream.fail())
		file_level = level;
}
		
void log_set_file(std::string file_name, logror::Level level)
{
	// don't throw exception if something fails
	log_stream.exceptions(std::ifstream::goodbit);
	
	std::string time = to_simple_string(second_clock::local_time());
	boost::format formater(file_name);
	formater.exceptions(boost::io::no_error_bits);
	std::string formatted_name = str(formater % time);
	
	log_stream.open(formatted_name.c_str());
	if (log_stream.fail())
		std::cout << "WARNING: could not open log file\n";
	else
		file_level = level;
}

namespace logror
{
LogBlob log_action(Level level, bool take_action, std::string message)
{
	if (level != nothing && (level >= file_level || level >= console_level))
	{
		std::string msg;
		msg.reserve(160); // should be enough for most
		switch (level)
		{
			case debug:   msg.append("DEBUG\t"); break;
			case info:    msg.append("INFO \t"); break;
			case warning: msg.append("WARN \t"); break;
			case error:   msg.append("ERROR\t"); break;
			case fatal:   msg.append("DOOM \t"); break;
			default:;
		}
		msg.append(to_simple_string(second_clock::local_time()));
		msg.append("\t");
		msg.append(message);
		return LogBlob(level, take_action, msg);
	}
	return LogBlob(nothing, take_action, "");
}

LogBlob::LogBlob (Level level, bool take_action, std::string message):
	level(level),
	take_action(take_action),
	formater(message)
{
	formater.exceptions(boost::io::no_error_bits);
}

LogBlob::~LogBlob()
{
	const bool fatalQuit = take_action && (level == fatal);
	bool errorQuit = false;
	if (level == error)
	{
		ptime const now = second_clock::local_time();
		error_times.push(now);
		if (error_times.size() > 10)
			error_times.pop();
		errorQuit = take_action && (error_times.size() > 9) && (error_times.front() > (now - minutes(10)));
	}
	if (level != nothing)
	{
		std::string msg = str(formater);
		if (fatalQuit)
			msg.append("\nterminated (fatal error)");
		if (errorQuit)
			msg.append("\nterminated (too many errors)");
		if (level >= console_level)
			std::cout <<  msg << std::endl;
		if (level >= file_level)
			log_stream << msg << std::endl;
	}
	if (fatalQuit || errorQuit)
		exit(EXIT_FAILURE);
}

}

bool log_string_to_level(std::string level_string, logror::Level& level)
{
	std::string str = level_string;
	boost::to_lower(str);
	if (str == "debug")
		level = logror::debug;
	else if (str == "info")
		level = logror::info;
	else if (str == "warn")
		level = logror::warning;
	else if (str == "error")
		level = logror::error;
	else if (str =="fatal")
		level = logror::fatal;
	else if (str =="nothing")
		level = logror::nothing;
	else 
		return false;
	return true;
}
