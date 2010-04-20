#include <cstdlib>
#include <queue>
#include <fstream>
#include <iostream>

#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "logror.h"

using std::string;
using namespace boost::posix_time;

namespace logror
{

Level consoleLevel = warning;
Level fileLevel = nothing;
std::ofstream log;
std::queue<ptime> errorTimes;

void LogSetConsoleLevel(Level level)
{
	consoleLevel = level;
}

void LogSetFileLevel(Level level)
{
	if (log.is_open() && !log.fail())
		fileLevel = level;
}
		
void LogSetFile(string fileName, Level level)
{
	log.exceptions(std::ifstream::goodbit); // don't throw exception if something fails
	log.open(fileName.c_str());
	if (log.fail())
		std::cout << "WARNING: could not open log file\n";
	else
		fileLevel = level;
}

LogBlob LogAction(Level level, bool takeAction, string message)
{
	if (level != nothing && (level >= fileLevel || level >= consoleLevel))
	{
		string msg;
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
		return LogBlob(level, takeAction, msg);
	}
	return LogBlob(nothing, takeAction, "");
}

LogBlob::LogBlob (Level level, bool takeAction, string message):
	level(level),
	takeAction(takeAction),
	formater(message)
{
	formater.exceptions(boost::io::no_error_bits);
}

LogBlob::~LogBlob()
{
	const bool fatalQuit = takeAction && (level == fatal);
	bool errorQuit = false;
	if (level == error)
	{
		ptime const now = second_clock::local_time();
		errorTimes.push(now);
		if (errorTimes.size() > 10)
			errorTimes.pop();
		errorQuit = takeAction && (errorTimes.size() > 9) && (errorTimes.front() > (now - minutes(10)));
	}
	if (level != nothing)
	{
		string msg = str(formater);
		if (fatalQuit)
			msg.append("\nterminated (fatal error)");
		if (errorQuit)
			msg.append("\nterminated (too many errors)");
		if (level >= consoleLevel)
			std::cout <<  msg << std::endl;
		if (level >= fileLevel)
			log << msg << std::endl;
	}
	if (fatalQuit || errorQuit)
		exit(EXIT_FAILURE);
}

bool StringToLevel(const string & levelString, Level & level)
{
	string str = levelString;
	boost::to_lower(str);
	if (str == "debug")
		level = debug;
	else if (str == "info")
		level = info;
	else if (str == "warn")
		level = warning;
	else if (str == "error")
		level = error;
	else if (str =="fatal")
		level = fatal;
	else if (str =="nothing")
		level = nothing;
	else 
		return false;
	return true;
}

}
