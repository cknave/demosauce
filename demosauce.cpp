/*
	fancy streaming engine for scenemusic 
	slapped together by maep 2009, 2010
	
	BEHOLD, A FUCKING PONY!
	
	           .,,.
         ,;;*;;;;,
        .-'``;-');;.
       /'  .-.  /;;;
     .'    \d    \;;               .;;;,
    / o      `    \;    ,__.     ,;*;;;*;,
    \__, _.__,'   \_.-') __)--.;;;;;*;;;;,
     `""`;;;\       /-')_) __)  `\' ';;;;;;
        ;*;;;        -') `)_)  |\ |  ;;;;*;
        ;;;;|        `---`    O | | ;;*;;;
        *;*;\|                 O  / ;;;;;*
       ;;;;;/|    .-------\      / ;*;;;;;
      ;;;*;/ \    |        '.   (`. ;;;*;;;
      ;;;;;'. ;   |          )   \ | ;;;;;;
      ,;*;;;;\/   |.        /   /` | ';;;*;
       ;;;;;;/    |/       /   /__/   ';;;
       '*jgs/     |       /    |      ;*;
            `""""`        `""""`     ;'
	
	pony source:
	http://www.geocities.com/SoHo/7373/index.htm#home
	(yeah... geocities, haha)
*/

#include <cstdlib>
#include <ctime>
#include <iostream>

#include "globals.h"
#include "settings.h"
#include "basscast.h"

#ifdef REVISION_NR
	#define REVISION -REVISION_NR
#else
	#define REVISION
#endif

#define STR(arg) #arg
#define EXPAND(arg) STR(arg)

int main(int argc, char* argv[])
{
	std::cout << "demosauce 0.2.3" EXPAND(REVISION) " - Now with TWICE the BITS!\n";
	srand(time(0));
	try
	{
        InitSettings(argc, argv);
		log_set_console_level(setting::log_console_level);
		log_set_file(setting::log_file, setting::log_file_level);
		BassCast cast;
		std::cout << "streamin'\n";
		cast.Run();
	} 
	catch (std::exception& e)
	{
		FATAL("%1%"), e.what();
	}
	return EXIT_SUCCESS;
}
