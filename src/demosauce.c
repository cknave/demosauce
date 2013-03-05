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

#include <stdio.h>

#include "settings.h"
#include "cast.h"
#include "basssource.h"

#define XSTR_(s) "-"#s
#define XSTR(s) XSTR_(s)
#ifndef BUILD_ID
    #define BUILD_ID
#endif

const char* demosauce_version = "demosauce 0.4.0" XSTR(BUILD_ID) " - C++ is to C as Lung Cancer is to Lung";

int main(int argc, char** argv)
{
    bass_load_so(argv);
    settings_init(argc, argv);
    log_set_console_level(settings_log_console_level);
    log_set_file(settings_log_file, settings_log_file_level);
    cast_init();
    puts("The spice must flow!");
    cast_run();
    return EXIT_SUCCESS;
}
