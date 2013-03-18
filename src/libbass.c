/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*
*   this file handles dynamic linking to libbass.so
*   it was nessessary because the system's dynamic 
*   linking isn't flexible enough
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <bass.h>

// and now for some major preprocessor hackery
#define JOIN2(a,b)  a##b
#define JOIN(a,b)   JOIN2(a,b)

#define NARG2(_6,_5,_4,_3,_2,_1,n,...) n
#define NARG(...) NARG2(__VA_ARGS__,6,5,4,3,2,1)

#define ARG_1(type)         type a1
#define ARG_2(type, ...)    ARG_1(__VA_ARGS__),type a2
#define ARG_3(type, ...)    ARG_2(__VA_ARGS__),type a3
#define ARG_4(type, ...)    ARG_3(__VA_ARGS__),type a4
#define ARG_5(type, ...)    ARG_4(__VA_ARGS__),type a5
#define ARG_6(type, ...)    ARG_5(__VA_ARGS__),type a6
#define ARG_N(n, ...)       JOIN(ARG_,n)(__VA_ARGS__)
#define ARG_DECL(...)       ARG_N(NARG(__VA_ARGS__),__VA_ARGS__)
#define ARG_CALL(...)       ARG_N(NARG(__VA_ARGS__),)

// x macros for bass functions
#define FUNCTION_DEFS                                                                   \
    X(BOOL,         SetConfig,              BOOL,DWORD)                                 \
    X(BOOL,         Init,                   int,DWORD,void*,void*)                      \
    X(int,          ErrorGetCode,           void)                                       \
    X(HSTERAM,      StreamCreateFile,       BOOL,const void*,QWORD,QWORD,DWORD)         \
    X(BOOL,         StreamFree,             HSTREAM)                                    \
    X(QWORD,        StreamGetFilePosition,  HSTREAM,DWORD)                              \
    X(HMUSIC,       MusicLoad,              BOOL,const void*,QWORD,DWORD,DWORD,DWORD)   \
    X(BOOL,         MusicFree,              HMUSIC)                                     \
    X(DWORD,        ChannelFlags,           DWORD,DWORD,DWORD)                          \
    X(BOOL,         ChannelGetInfo,         DWORD,BASS_CHANNELINFO*)                    \
    X(QWORD,        ChannelGetData,         DWORD,void*,DWORD)                          \
    X(BOOL,         ChannelGetPosition,     DWORD,QWORD,DWORD)                          \
    X(const char*,  ChannelGetTags,         DWORD,DWORD)

#define X(ret, name, ...) static ret (*name)(__VA_ARGS__);
FUNCTION_DEFS
#undef X

#define X(ret, name, ...) ret BASSDEF(BASS_##name)(ARG_DECL(__VA_ARGS__)){return name(ARG_CALL(__VA_ARGS__));}
FUNCTION_DEFS
#undef X

static void* handle;
static int   error;

static void* bind(const char* symbol)
{
    void* sym = dlsym(handle, symbol);
    if (!sym) {
        error = 1;
        printf("libbass.so missing symbol: %s\n", symbol);
    }
    return sym;
}

static int load(const char* file)
{
    handle = dlopen(file, RTLD_LAZY);
    if (!handle)
        return 0;
    
    #define X(ret, name, ...) name=(ret(*)(__VA_ARGS__))bind(#name);
    FUNCTION_DEFS
    #undef X

    return error ? 0 : 1;
}

int bass_loadso(char** argv)
{
    char path[4096];
    if (strlen(argv[0]) < sizeof(path) - 32) {
        strcpy(path, argv[0]);
        char* path_end = strrchr(path, '/') + 1;
        strcpy(path_end, "libbass.so");
        if (load(path))
            return 1;
        strcpy(path_end, "bass/libbass.so");
        if (load(path))
            return 1;
    }
    if (load("/usr/local/lib/libbass.so"))
        return 1;
    if (load("/usr/lib/libbass.so"))
        return 1;
    puts("can't locate libbass.so");
    return 0;
}

