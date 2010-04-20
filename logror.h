/*
	logging and "error handling" stuff
	to log use (everything is in logror namespace):
	Log(level, "message %1% foo %2%"), param1, param2;
	
	if 'level' is debug, feel encouraged to use the macro, define DEBUG to enable/disable it
	LogDebug("message %1% foo %2%"), param1, param2;
	
	Error keeps track of the last error and exits the application if too many errors apper
	(currently 10 in less than den minutes). Log(error, "foo") does not expose this behaviour
	Error("message %1% foo %2%"), param1, param2;
	
	Fatal logs the message and the exits the application. Log(fatal, "foo") does not expose this behaviour.
	Fatal("message %1% foo %2%"), param1, param2;
	
	other functions you might need:
	void LogSetConsoleLevel(Level level);
	void LogSetFileLevel(Level level);
	void LogSetFile(string fileName, Level level = info);
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
	template<typename T> LogBlob & operator, (T right);
private:
	Level const level;
	bool takeAction;
	boost::format formater;
};

LogBlob LogAction(Level level, bool takeAction, std::string message);
inline LogBlob Log(Level level, std::string message) { return LogAction(level, false, message);  }
inline LogBlob Error(std::string message) { return LogAction(error, true, message); }
inline LogBlob Fatal(std::string message) { return LogAction(fatal, true, message); }

#ifdef DEBUG
	#define LogDebug(message) logror::LogAction(logror::debug, false, message)
#else
	// got this idea from http://www.ddj.com/developement-tools/184401612
	// optimizing compiler should remove dead code
	#define LogDebug(message) if(false) logror::LogAction(logror::nothing, false, "")
#endif

void LogSetConsoleLevel(Level level);
void LogSetFileLevel(Level level);
void LogSetFile(std::string fileName, Level level = info);
bool StringToLevel(const std::string & levelString, Level & level);

template <typename T>
LogBlob & LogBlob::operator, (T right)
{
	if (level != nothing) formater % right;
	return *this;
}

}

#endif
