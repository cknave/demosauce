/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
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
#include <iostream>

#include "settings.h"
#include "shoutcast.h"
#include "basssource.h"

const char* demosauce_version = "demosauce 0.3.3 - less BASS more SHOUT";

int main(int argc, char* argv[])
{
    LIBBASS_LOAD(argv);
    try {
        init_settings(argc, argv);
        log_set_console_level(setting::log_console_level);
        log_set_file(setting::log_file.c_str(), setting::log_file_level);
        ShoutCast cast;
        std::cout << "the spice must flow!\n";
        cast.Run();
    } catch (std::exception& e) {
        FATAL(e.what());
    }
    return EXIT_SUCCESS;
}
