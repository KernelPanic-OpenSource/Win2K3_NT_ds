//*************************************************************
//
//  Copyright (c) Microsoft Corporation 1998
//  All rights reserved
//
//  util.hxx
//
//*************************************************************

#include <strsafe.h>

#if !defined(__CUTIL_HXX__)
#define __CUTIL_HXX__

#define REMAP_DARWIN_STATUS( Status ) \
    Status = ((ERROR_SUCCESS_REBOOT_INITIATED == Status) || \
              (ERROR_SUCCESS_REBOOT_REQUIRED == Status) || \
              (ERROR_INSTALL_SUSPEND == Status)) \
    ? ERROR_SUCCESS : Status


#define GUID_LENGTH 38

inline void * __cdecl
operator new (size_t Size)
{
    return LocalAlloc(0, Size);
}

inline void __cdecl
operator delete (void * pMem)
{
    LocalFree( pMem );
}

void
DwordToString(
    DWORD   Number,
    WCHAR * wszNumber
    );

BOOL
LoadUser32Funcs();

BOOL
LoadLoadString();

void
FreeApplicationInfo(
    APPLICATION_INFO * ApplicationInfo
    );

PSID
AppmgmtGetUserSid(
    HANDLE  hUserToken = 0
    );

void
GuidToString(
    GUID & Guid,
    PWCHAR pwszGuid
    );

void GuidToString(
    GUID & Guid,
    PWCHAR * ppwszGuid
    );

void StringToGuid(
    PWCHAR pwszGuid,
    GUID * pGuid
    );

inline LPWSTR StringDuplicate(LPWSTR wszSource)
{
    LPWSTR wszDest;
    LONG   lSize;

    if ( ! wszSource )
    {
        return NULL;
    }

    lSize = lstrlen(wszSource) + 1;

    if (wszDest = new WCHAR [lSize])
    {
        (void) StringCchCopy(wszDest, lSize, wszSource);
    }

    return wszDest;
}

HRESULT CreateGuid(GUID *pGuid);

DWORD ReadStringValue( HKEY hKey, WCHAR * pwszValueName, WCHAR ** ppwszValue );

DWORD
GetSidString(
    HANDLE          hToken,
    UNICODE_STRING* pSidString
    );

class CLoadMsi
{
public:
    CLoadMsi( DWORD &Status );
    ~CLoadMsi();
private:
    HINSTANCE hMsi;
};

#endif __CUTIL_HXX__
