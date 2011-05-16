/*
*   demosauce - fancy icecast source client
*
*   this source is published under the gpl license. google it yourself.
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

/*
    a fancy source client for scenemusic.net
    slapped together by maep 2009 - 2011

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

    pony source (yeah... geocities, haha):
    http://www.geocities.com/SoHo/7373/index.htm#home
*/

#include <cstdlib>
#include <ctime>
#include <iostream>

#include "settings.h"
#include "shoutcast.h"

#ifdef REVISION_NR
    #define REVISION -REVISION_NR
#else
    #define REVISION
#endif

#define STR(arg) #arg
#define EXPAND(arg) STR(arg)

int main(int argc, char* argv[])
{
    std::cout << "demosauce 0.3.2" EXPAND(REVISION) " - less BASS, more SHOUT!\n";
    try
    {
        init_settings(argc, argv);
        log_set_console_level(setting::log_console_level);
        log_set_file(setting::log_file, setting::log_file_level);
        ShoutCast cast;
        std::cout << "streamin' in the wind\n";
        cast.Run();
    }
    catch (std::exception& e)
    {
        FATAL("%1%"), e.what();
    }
    return EXIT_SUCCESS;
}
