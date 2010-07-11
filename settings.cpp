#include <cstdlib>
#include <string>
#include <iostream>
#include <fstream>

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include "globals.h"

using namespace std;
using namespace boost::program_options;
using namespace logror;

namespace setting
{
	string demovibes_host		= "localhost";
	uint32_t demovibes_port		= 32167;
	uint32_t encoder_samplerate	= 44100;
	uint32_t encoder_bitrate	= 128;
	uint32_t encoder_channels	= 2;
	string cast_host			= "127.0.0.1";
	uint32_t cast_port			= 8000;
	string cast_mount			= "ices";
	string cast_password;
	string cast_name;
	string cast_url;
	string cast_genre;
	string cast_description;
	string error_tune;
	string error_title			= "sorry, we're having some trouble";
	string error_fallback_dir;
	string log_file				= "demosauce.log";
	Level log_file_level		= info;
	Level log_console_level		= warning;
	float amiga_channel_ratio	= 0;
	
	string debug_file;
}

using namespace setting;

string configFileName = "demosauce.conf";
string castForcePassword;
string logFileLevel;
string logConsoleLevel;

void BuildDescriptions(options_description & settingsDesc, options_description & optionsDesc)
{
	settingsDesc.add_options()
	("demovibes_host", value<string>(&demovibes_host))
	("demovibes_port", value<uint32_t>(&demovibes_port))
	("encoder_samplerate", value<uint32_t>(&encoder_samplerate))
	("encoder_bitrate", value<uint32_t>(&encoder_bitrate))
	("encoder_channels", value<uint32_t>(&encoder_channels))
	("cast_host", value<string>(&cast_host))
	("cast_port", value<uint32_t>(&cast_port))
	("cast_mount", value<string>(&cast_mount))
	("cast_password", value<string>(&cast_password))
	("cast_name", value<string>(&cast_name))
	("cast_url", value<string>(&cast_url))
	("cast_genre", value<string>(&cast_genre))
	("cast_description", value<string>(&cast_description))
	("error_tune", value<string>(&error_tune))
	("error_title", value<string>(&error_title))
	("error_fallback_dir", value<string>(&error_fallback_dir))
	("log_file", value<string>(&log_file))
	("log_file_level", value<string>(&logFileLevel))
	("log_console_level", value<string>(&logConsoleLevel))
	("amiga_channel_ratio", value<float>(&amiga_channel_ratio))
	;

	optionsDesc.add_options()
	("help", "what do you think this does? make you a pizza maybe?")
	("config_file,c", value<string>(&configFileName), "use config file, default: demosauce.conf")
	("cast_password,p", value<string>(&castForcePassword), "password for cast server, overwrites setting from config file")
	("debug_file,f", value<string>(&debug_file), "load specific file, intended for testing and debugging")
	;
}

void CheckSanity()
{
	int e = 0;
	if (demovibes_port < 1 || demovibes_port > 65535)
		e = 1, std::cout << "setting demovibes_port out of range (1-65535)\n";
	if (encoder_samplerate <  8000 || encoder_samplerate > 192000)
		e = 1, std::cout << "setting encoder_samplerate out of range (8000-192000)\n";
	if (encoder_bitrate > 10000)
		e = 1, std::cout << "setting encoder_bitrate too high >10000\n";
	if (encoder_channels < 1 and encoder_channels > 2)
		e = 1, std::cout << "setting encoder_channels out of range (1-2)\n";
	if (cast_port < 1 || cast_port > 65535)
		e = 1, std::cout << "setting cast_port out of range (1-65535)\n";
	if (amiga_channel_ratio < 0 || amiga_channel_ratio > 1)
		e = 1, std::cout << "setting amiga_channel_ratio out of range (0-1)";
	
	if (e ==1)
		exit(EXIT_FAILURE);
}

void InitSettings(int argc, char* argv[])
{
	options_description settingsDesc("settings");
	options_description optionsDesc("allowed options");
	BuildDescriptions(settingsDesc, optionsDesc);
	
	variables_map optionsMap;
	store(parse_command_line(argc, argv, optionsDesc), optionsMap);
	notify(optionsMap);    

	if (optionsMap.count("help")) 
	{
		cout << optionsDesc;
		exit(EXIT_SUCCESS);
	}
	
	if (!boost::filesystem::exists(configFileName))
	{
		std::cout << "cannot find config file: " << configFileName << endl;
		exit(EXIT_FAILURE);
	}

	ifstream configFile(configFileName.c_str(), ifstream::in);
	if (configFile.fail())
	{
		std::cout << "failed to open config file: " << configFileName << endl;
		exit(EXIT_FAILURE);
	}
	cout << "reading config file\n";
	variables_map settingsMap;
	store(parse_config_file(configFile, settingsDesc), settingsMap);
	notify(settingsMap);
	
	if (optionsMap.count("cast_password")) 
		cast_password = castForcePassword;
	if (settingsMap.count("log_file_level") && !log_string_to_level(logFileLevel, log_file_level))
		std::cout << "setting log_file_level: unknown level\n";
	if (settingsMap.count("log_console_level") && !log_string_to_level(logConsoleLevel, log_console_level))
		std::cout << "setting log_console_level: unknown level\n";
	
	CheckSanity();
}
