//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//
//  Copyright (C) Microsoft Corporation, 1996 - 1999
//
//  File:       dllmain.cpp
//
//  Contents:   Microsoft Internet Security Trust Provider
//
//  Functions:  DllMain
//              DllRegisterServer
//              DllUnregisterServer
//
//  History:    28-May-1997 pberkman   created
//
//--------------------------------------------------------------------------

#include    "global.hxx"

#include    "ossfunc.h"

HANDLE      hMeDLL = NULL;

//
//  provider lists
//
LIST_LOCK       sProvLock;

//
//  store lists
//
LIST_LOCK       sStoreLock;
HANDLE          hStoreEvent;

CCatalogCache g_CatalogCache;

// The following is set for a successful DLL_PROCESS_DETACH.
static BOOL g_fEnableProcessDetach = FALSE;

//////////////////////////////////////////////////////////////////////////////////////
//
// standard DLL exports ...
//
//

extern BOOL WINAPI WintrustDllMain (HANDLE hInstDLL, DWORD fdwReason, LPVOID lpvReserved);
extern BOOL WINAPI SoftpubDllMain (HANDLE hInstDLL, DWORD fdwReason, LPVOID lpvReserved);
extern BOOL WINAPI mssip32DllMain (HANDLE hInstDLL, DWORD fdwReason, LPVOID lpvReserved);
extern BOOL WINAPI mscat32DllMain (HANDLE hInstDLL, DWORD fdwReason, LPVOID lpvReserved);

typedef BOOL (WINAPI *PFN_DLL_MAIN_FUNC) (
                HANDLE hInstDLL,
                DWORD fdwReason,
                LPVOID lpvReserved
                );

// For process/thread attach, called in the following order. For process/thread
// detach, called in reverse order.
const PFN_DLL_MAIN_FUNC rgpfnDllMain[] = {
    WintrustDllMain,
    SoftpubDllMain,
    mssip32DllMain,
    mscat32DllMain,
};
#define DLL_MAIN_FUNC_COUNT (sizeof(rgpfnDllMain) / sizeof(rgpfnDllMain[0]))

STDAPI WintrustDllRegisterServer(void);
STDAPI WintrustDllUnregisterServer(void);
STDAPI SoftpubDllRegisterServer(void);
STDAPI SoftpubDllUnregisterServer(void);
STDAPI mssip32DllRegisterServer(void);
STDAPI mssip32DllUnregisterServer(void);
STDAPI mscat32DllRegisterServer(void);
STDAPI mscat32DllUnregisterServer(void);

typedef HRESULT (STDAPICALLTYPE *PFN_DLL_REGISTER_SERVER) (void);
const PFN_DLL_REGISTER_SERVER rgpfnDllRegisterServer[] = {
    WintrustDllRegisterServer,
    SoftpubDllRegisterServer,
    mssip32DllRegisterServer,
    mscat32DllRegisterServer,
};
#define DLL_REGISTER_SERVER_COUNT   \
    (sizeof(rgpfnDllRegisterServer) / sizeof(rgpfnDllRegisterServer[0]))

typedef HRESULT (STDAPICALLTYPE *PFN_DLL_UNREGISTER_SERVER) (void);
const PFN_DLL_UNREGISTER_SERVER rgpfnDllUnregisterServer[] = {
    WintrustDllUnregisterServer,
    SoftpubDllUnregisterServer,
    mssip32DllUnregisterServer,
    mscat32DllUnregisterServer,
};
#define DLL_UNREGISTER_SERVER_COUNT   \
    (sizeof(rgpfnDllUnregisterServer) / sizeof(rgpfnDllUnregisterServer[0]))


#if DBG
#include <crtdbg.h>

#ifndef _CRTDBG_LEAK_CHECK_DF
#define _CRTDBG_LEAK_CHECK_DF 0x20
#endif

#define DEBUG_MASK_LEAK_CHECK       _CRTDBG_LEAK_CHECK_DF     /* 0x20 */

static int WINAPI DbgGetDebugFlags()
{
    char    *pszEnvVar;
    char    *p;
    int     iDebugFlags = 0;

    if (pszEnvVar = getenv("DEBUG_MASK"))
        iDebugFlags = strtol(pszEnvVar, &p, 16);

    return iDebugFlags;
}
#endif

WINAPI
I_IsProcessDetachFreeLibrary(
    LPVOID lpvReserved      // Third parameter passed to DllMain
    )
{
    if (NULL == lpvReserved)
        return TRUE;

#if DBG
    if (DbgGetDebugFlags() & DEBUG_MASK_LEAK_CHECK)
        return TRUE;
#endif
    return FALSE;
}

BOOL WINAPI DllMain(
                HANDLE hInstDLL,
                DWORD fdwReason,
                LPVOID lpvReserved
                )
{
    BOOL    fReturn = TRUE;
    int     i,j;

    switch (fdwReason) {
        case DLL_PROCESS_DETACH:
            if (!g_fEnableProcessDetach)
                return TRUE;
            else
                g_fEnableProcessDetach = FALSE;

            //
            // This is to prevent unloading the dlls at process exit
            //
            if (!I_IsProcessDetachFreeLibrary(lpvReserved))
            {
                return TRUE;
            }

            // fall through if not process exit and unload the dlls
        case DLL_THREAD_DETACH:
            for (i = DLL_MAIN_FUNC_COUNT - 1; i >= 0; i--)
                fReturn &= rgpfnDllMain[i](hInstDLL, fdwReason, lpvReserved);
            break;

        case DLL_PROCESS_ATTACH:
        case DLL_THREAD_ATTACH:
        default:
            for (i = 0; i < DLL_MAIN_FUNC_COUNT; i++)
            {
                if (!rgpfnDllMain[i](hInstDLL, fdwReason, lpvReserved))
                {
                    //
                    // force the dllmain's which already succeeded to clean up
                    //
                    for (j = i-1; j >= 0; j--)
                    {
                        rgpfnDllMain[j](hInstDLL, DLL_PROCESS_DETACH, lpvReserved);
                    }   
                    fReturn = FALSE;
                    break;
                }
                
            }

            if ((DLL_PROCESS_ATTACH == fdwReason) && fReturn)
                g_fEnableProcessDetach = TRUE;

            break;
    }

    return(fReturn);
}

STDAPI DllRegisterServer(void)
{
    HRESULT hr = S_OK;
    int i;

    for (i = 0; i < DLL_REGISTER_SERVER_COUNT; i++) {
        HRESULT hr2;

        hr2 = rgpfnDllRegisterServer[i]();
        if (S_OK == hr)
            hr = hr2;
    }

    return hr;
}

STDAPI DllUnregisterServer(void)
{
    HRESULT hr = S_OK;
    int i;

    for (i = 0; i < DLL_UNREGISTER_SERVER_COUNT; i++) {
        HRESULT hr2;

        hr2 = rgpfnDllUnregisterServer[i]();
        if (S_OK == hr)
            hr = hr2;
    }

    return hr;
}

BOOL WINAPI WintrustDllMain(HANDLE hInstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:

            //
            //  assign me so that further calls to WVT that would load me will just
            //  use this handle....  otherwise, we would deadlock on detatch!
            //
            hMeDLL = hInstDLL;

            DisableThreadLibraryCalls((HINSTANCE)hInstDLL);

            //
            // Initialize critical section to protect lists.
            //
            if (!(InitializeListLock(&sProvLock, DBG_SS_TRUST)))
            {
                return(FALSE);
            }

            if (!(InitializeListLock(&sStoreLock, DBG_SS_TRUST)))
            {
                LockFree(&sProvLock);
                return(FALSE);
            }

            if (!(InitializeListEvent(&hStoreEvent)))
            {
                LockFree(&sProvLock);
                LockFree(&sStoreLock);
                return(FALSE);
            }

            if ( g_CatalogCache.Initialize() == FALSE )
            {
                LockFree(&sProvLock);
                LockFree(&sStoreLock);
                EventFree(hStoreEvent);
                return( FALSE );
            }

            //
            //  we want to open the stores the first time accessed.
            //
            SetListEvent(hStoreEvent);

            break;

        case DLL_PROCESS_DETACH:
            g_CatalogCache.Uninitialize();
            WintrustUnloadProviderList();
            StoreProviderUnload();
            LockFree(&sProvLock);
            LockFree(&sStoreLock);
            EventFree(hStoreEvent);
            break;
    }

    return(ASNDllMain((HINSTANCE)hInstDLL, fdwReason, lpvReserved));
}

STDAPI WintrustDllRegisterServer(void)
{
    //
    //  register our ASN routines
    //
    return(ASNRegisterServer(W_MY_NAME));
}


STDAPI WintrustDllUnregisterServer(void)
{
    return(ASNUnregisterServer());
}


