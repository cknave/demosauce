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

#ifdef __linux__

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <bass.h>

static void* dl_handle = 0;

static BOOL     (*SetConfig)(BOOL, DWORD) = 0;
static BOOL     (*Init)(int, DWORD, DWORD, void*, void *) = 0;
static int      (*ErrorGetCode)() = 0;

static HSTREAM  (*StreamCreateFile)(BOOL, const void*, QWORD, QWORD, DWORD) = 0;
static BOOL     (*StreamFree)(HSTREAM) = 0;
static QWORD    (*StreamGetFilePosition)(HSTREAM, DWORD) = 0;

static HMUSIC   (*MusicLoad)(BOOL, const void*, QWORD, DWORD, DWORD, DWORD) = 0;
static BOOL     (*MusicFree)(HMUSIC) = 0;

static DWORD    (*ChannelFlags)(DWORD, DWORD, DWORD) = 0;
static BOOL     (*ChannelGetInfo)(DWORD, BASS_CHANNELINFO*) = 0;
static QWORD    (*ChannelGetLength)(DWORD, DWORD) = 0;
static DWORD    (*ChannelGetData)(DWORD, void*, DWORD) = 0;
static BOOL     (*ChannelSetPosition)(DWORD, QWORD, DWORD) = 0;
static const char* (*ChannelGetTags)(DWORD, DWORD) = 0;

static void* bind(const char* symbol)
{
    void* sym = dlsym(dl_handle, symbol);
    if (!sym) {
        printf("libbass.so missing symbol: %s\n", symbol);
        exit(EXIT_FAILURE);
    }
    return sym;
}

static int load(const char* file)
{
    dl_handle = dlopen(file, RTLD_LAZY);
    if (!dl_handle)
        return 0;
    SetConfig = (BOOL (*)(BOOL, DWORD)) bind("BASS_SetConfig");
    Init = (BOOL (*)(int, DWORD, DWORD, void*, void*)) bind("BASS_Init");
    ErrorGetCode = (int (*)()) bind("BASS_ErrorGetCode");

    StreamCreateFile = (HSTREAM (*)(BOOL, const void*, QWORD, QWORD, DWORD)) bind("BASS_StreamCreateFile");
    StreamFree = (BOOL (*)(HSTREAM)) bind("BASS_StreamFree");
    StreamGetFilePosition = (QWORD (*)(HSTREAM, DWORD)) bind("BASS_StreamGetFilePosition");
    
    MusicLoad = (HMUSIC (*)(BOOL, const void*, QWORD, DWORD, DWORD, DWORD)) bind("BASS_MusicLoad");
    MusicFree = (BOOL (*)(HMUSIC)) bind("BASS_MusicFree");

    ChannelGetData = (DWORD (*)(DWORD, void*, DWORD)) bind("BASS_ChannelGetData");
    ChannelGetTags = (const char* (*)(DWORD, DWORD)) bind("BASS_ChannelGetTags");
    ChannelFlags = (DWORD (*)(DWORD, DWORD, DWORD)) bind("BASS_ChannelFlags");
    ChannelGetInfo = (BOOL (*)(DWORD, BASS_CHANNELINFO*)) bind("BASS_ChannelGetInfo");
    ChannelGetLength = (QWORD (*)(DWORD, DWORD)) bind("BASS_ChannelGetLength");
    ChannelSetPosition = (BOOL (*)(DWORD, QWORD, DWORD)) bind("BASS_ChannelSetPosition");

    return 1;
}

BOOL BASSDEF(BASS_SetConfig)(DWORD option, DWORD value)
{
    return SetConfig(option, value);
}

BOOL BASSDEF(BASS_Init)(int device, DWORD freq, DWORD flags, void *win, void *dsguid)
{
    return Init(device, freq, flags, win, dsguid);
}

int BASSDEF(BASS_ErrorGetCode)() 
{
    return ErrorGetCode();
}

DWORD BASSDEF(BASS_ChannelFlags)(DWORD handle, DWORD flags, DWORD mask)
{
    return ChannelFlags(handle, flags, mask);
}

HSTREAM BASSDEF(BASS_StreamCreateFile)(BOOL mem, const void *file, QWORD offset, QWORD length, DWORD flags)
{
    return StreamCreateFile(mem, file, offset, length, flags);
}

HMUSIC BASSDEF(BASS_MusicLoad)(BOOL mem, const void *file, QWORD offset, DWORD length, DWORD flags, DWORD freq)
{
    return MusicLoad(mem, file, offset, length, flags, freq);
}

BOOL BASSDEF(BASS_ChannelGetInfo)(DWORD handle, BASS_CHANNELINFO *info)
{
    return ChannelGetInfo(handle, info);
}

QWORD BASSDEF(BASS_ChannelGetLength)(DWORD handle, DWORD mode)
{
    return ChannelGetLength(handle, mode);
}

BOOL BASSDEF(BASS_MusicFree)(HMUSIC handle)
{
    return MusicFree(handle);
}

BOOL BASSDEF(BASS_StreamFree)(HSTREAM handle)
{
    return StreamFree(handle);
}

DWORD BASSDEF(BASS_ChannelGetData)(DWORD handle, void *buffer, DWORD length)
{
    return ChannelGetData(handle, buffer, length);
}

QWORD BASSDEF(BASS_StreamGetFilePosition)(HSTREAM handle, DWORD mode)
{
    return StreamGetFilePosition(handle, mode);
}

const char *BASSDEF(BASS_ChannelGetTags)(DWORD handle, DWORD tags)
{
    return ChannelGetTags(handle, tags);
}

BOOL BASSDEF(BASS_ChannelSetPosition)(DWORD handle, QWORD pos, DWORD mode)
{
    return ChannelSetPosition(handle, pos, mode);
}

void libbass_load(char** argv)
{
    char path[4000];
    char* path_end = 0;
    if (strlen(argv[0]) >= 4000) {
        puts("path is too log");
        exit(EXIT_FAILURE);
    }

    strcpy(path, argv[0]);
    path_end = strrchr(path, '/') + 1;

    strcpy(path_end, "libbass.so");
    if (load(path))
        return;
    strcpy(path_end, "bass/libbass.so");
    if (load(path))
        return;
    if (load("/usr/local/lib/libbass.so"))
        return;
    if (load("/usr/lib/libbass.so"))
        return;
    
    puts("can't find libbass.so");
    exit(EXIT_FAILURE);
}

#endif

