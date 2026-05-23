#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define DSP_HDRVER 0x20

struct winampDSPModule;

typedef struct winampDSPHeader {
    int version;
    char *description;
    struct winampDSPModule* (__cdecl *getModule)(int num);
} winampDSPHeader;

typedef struct winampDSPHeaderEx {
    int version;
    char *description;
    struct winampDSPModule* (__cdecl *getModule)(int num);
    int (__cdecl *sf)(int v);
} winampDSPHeaderEx;

typedef struct winampDSPModule {
    char *description;
    HWND hwndParent;
    HINSTANCE hDllInstance;

    void (__cdecl *Config)(struct winampDSPModule *this_mod);
    int (__cdecl *Init)(struct winampDSPModule *this_mod);
    int (__cdecl *ModifySamples)(struct winampDSPModule *this_mod, 
                                 short int *samples, 
                                 int numsamples, 
                                 int bps, 
                                 int nch, 
                                 int srate);
    void (__cdecl *Quit)(struct winampDSPModule *this_mod);

    void *userData;
} winampDSPModule;

typedef winampDSPHeader* (__cdecl *winampDSPGetHeaderProc)();
