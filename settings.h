#ifndef _H_SETTINGS_
#define _H_SETTINGS_

#include <string>
#include <boost/cstdint.hpp>

#include "logror.h"

void init_settings(int argc, char* argv[]);

// seddings, actual instances are in settings.cpp
namespace setting
{
	extern std::string		demovibes_host;
	extern uint32_t			demovibes_port;

    extern std::string      encoder_command;
    extern std::string      encoder_type;
	extern uint32_t			encoder_samplerate;
	extern uint32_t			encoder_bitrate;
	extern uint32_t			encoder_channels;

	extern std::string		cast_host;
	extern uint32_t			cast_port;
	extern std::string		cast_mount;
	extern std::string		cast_password;
	extern std::string		cast_name;
	extern std::string		cast_url;
	extern std::string		cast_genre;
	extern std::string		cast_description;

	extern std::string  	error_tune;
	extern std::string		error_title;
	extern std::string		error_fallback_dir;

	extern std::string		log_file;
	extern logror::Level	log_file_level;
	extern logror::Level	log_console_level;

	extern std::string		debug_file;
}

#endif
