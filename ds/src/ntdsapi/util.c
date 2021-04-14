/*++

Copyright (c) 1997  Microsoft Corporation

Module Name:

    util.c

Abstract:

   Common utility routines
   For internal use inside ntdsapi.dll
   DO NOT EXPOSE IN NTDSAPI.DEF

Author:

    Will Lees (wlees) 02-Feb-1998

Environment:

    optional-environment-info (e.g. kernel mode only...)

Notes:

    optional-notes

Revision History:

    most-recent-revision-date email-name
        description
        .
        .
    least-recent-revision-date email-name
        description

--*/

#define UNICODE 1

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include <winsock.h>
#include <winerror.h>
#include <rpc.h>            // RPC defines
#include <stdlib.h>         // atoi, itoa
#include <dsdebug.h>

#include <drs.h>            // wire function prototypes
#include <bind.h>           // BindState

#include <drserr.h>         // DRS error codes
#define DEFS_ONLY
#include <draatt.h>         // Dra option flags for replication
#undef DEFS_ONLY

#include "util.h"           // ntdsapi utility functions

#if DBG
#include <stdio.h>          // printf for debugging
#endif

/* External */

ULONG
_cdecl
DbgPrint(
    PCH Format,
    ...
    );

/* Static */

static LONG WinSockInitialized = FALSE;

/* Forward */ /* Generated by Emacs 19.34.1 on Wed Oct 07 16:18:49 1998 */

DWORD
InitializeWinsockIfNeeded(
    VOID
    );

VOID
TerminateWinsockIfNeeded(
    VOID
    );

DWORD
AllocConvertNarrow(
    IN LPCWSTR StringW,
    OUT LPSTR *pStringA
    );

DWORD
AllocConvertNarrowUTF8(
    IN LPCWSTR StringW,
    OUT LPSTR *pStringA
    );

static DWORD
allocConvertNarrowCodePage(
    IN DWORD CodePage,
    IN LPCWSTR StringW,
    OUT LPSTR *pStringA
    );

DWORD
AllocConvertWide(
    IN LPCSTR StringA,
    OUT LPWSTR *pStringW
    );

DWORD
AllocConvertWideBuffer(
    IN  DWORD   LengthA,
    IN  PCCH    BufferA,
    OUT PWCHAR  *OutBufferW
    );

DWORD
AllocBuildDsname(
    IN LPCWSTR StringDn,
    OUT DSNAME **ppName
    );

DWORD
ConvertScheduleToReplTimes(
    PSCHEDULE pSchedule,
    REPLTIMES *pReplTimes
    );

/* End Forward */


DWORD
InitializeWinsockIfNeeded(
    VOID
    )

/*++

Routine Description:

Initialize winsock dll if not initialized already.
Moved here from dllEntry because of dll ordering problems.

Arguments:

    VOID -

Return Value:

    DWORD -

--*/

{
    WSADATA wsaData;
    DWORD status;
    LONG oldValue;

#ifndef WIN95
    // Compare the synchonization variable against FALSE
    // If it is FALSE, set it to TRUE and return FALSE
    // If it is TRUE, return TRUE
    oldValue = InterlockedCompareExchange(
        &WinSockInitialized,       // Destination
        TRUE,                      // Exchange
        FALSE                      // Comperand
        );

    // If already initialized, no need to call startup
    if (oldValue == TRUE) {
        return ERROR_SUCCESS;
    }
#else
    if (WinSockInitialized == FALSE) {
        WinSockInitialized = TRUE;
    } else {
        return ERROR_SUCCESS;
    }
#endif

    // Initialize winsock
    // Look for Winsock 1.1 because that is the default on win95
    status = WSAStartup(MAKEWORD(1,1),&wsaData);
    if (status != 0) {
#ifndef WIN95
        DbgPrint( "ntdsapi: WSAStartup failed %d\n", status );
#else
        NULL;
#endif
        // Clear initialized flag on failure
#ifndef WIN95
        InterlockedExchange(
            &WinSockInitialized,   // Target
            FALSE                   // Value
            );
#else
        WinSockInitialized = FALSE;
#endif
    }

    return status;
} /* InitializeWinsockIfNeeded */


VOID
TerminateWinsockIfNeeded(
    VOID
    )

/*++

Routine Description:

Terminate winsock dll if initialized.
Moved here from dllEntry because of dll ordering problems.

Arguments:

    VOID -

Return Value:

    DWORD -

--*/

{
    WSADATA wsaData;
    DWORD status;
    LONG oldValue;

#ifndef WIN95
    // Compare the synchonization variable against TRUE
    // If it is TRUE, set it to FALSE and return TRUE
    // If it is FALSE, return FALSE
    oldValue = InterlockedCompareExchange(
        &WinSockInitialized,       // Destination
        FALSE,                     // Exchange
        TRUE                       // Comperand
        );

    // If not initialized, no need to clean up
    if (oldValue == FALSE) {
        return;
    }
#else
    if (WinSockInitialized == TRUE) {
        WinSockInitialized = FALSE;
    } else {
        return;
    }
#endif

    // Cleanup winsock
    WSACleanup();

} /* TerminateWinsockIfNeeded */


DWORD
AllocConvertNarrow(
    IN LPCWSTR StringW,
    OUT LPSTR *pStringA
    )

/*++

Routine Description:

Helper routine to convert a wide string to a newly allocated narrow one

Arguments:

    StringW -
    pStringA -

Return Value:

    DWORD -

--*/

{
    return allocConvertNarrowCodePage( CP_ACP, StringW, pStringA );
}


DWORD
AllocConvertNarrowUTF8(
    IN LPCWSTR StringW,
    OUT LPSTR *pStringA
    )

/*++

Routine Description:

Helper routine to convert a wide string to a newly allocated narrow one

Arguments:

    StringW -
    pStringA -

Return Value:

    DWORD -

--*/

{
    return allocConvertNarrowCodePage( CP_UTF8, StringW, pStringA );
}


static DWORD
allocConvertNarrowCodePage(
    IN DWORD CodePage,
    IN LPCWSTR StringW,
    OUT LPSTR *pStringA
    )

/*++

Routine Description:

Helper routine to convert a wide string to a newly allocated narrow one

Arguments:

    StringW -
    pStringA -

Return Value:

    DWORD -

--*/

{
    DWORD numberNarrowChars, numberConvertedChars, status;
    LPSTR stringA;

    if (pStringA == NULL) {
        return ERROR_INVALID_PARAMETER;
    }

    if (StringW == NULL) {
        *pStringA = NULL;
        return ERROR_SUCCESS;
    }

    // Get the needed length
    numberNarrowChars = WideCharToMultiByte(
        CodePage,
        0,
        StringW,              // input buffer
        -1,                   // null terminated
        NULL,                 // output buffer
        0,                    // output length
        NULL,                 // default char
        NULL                  // default used
        );

    if (numberNarrowChars == 0) {
        return ERROR_INVALID_PARAMETER;
    }

    // Allocate the new buffer
    stringA = LocalAlloc( LPTR, (numberNarrowChars + 1) * sizeof( CHAR ) );
    if (stringA == NULL) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    // Do the conversion into the new buffer
    numberConvertedChars = WideCharToMultiByte(
        CodePage,
        0,
        StringW,         //input
        -1,
        stringA,         // output
        numberNarrowChars + 1,
        NULL,            // default char
        NULL             // default used
        );
    if (numberConvertedChars == 0) {
        LocalFree( stringA );
        return ERROR_INVALID_PARAMETER;
    }

    // return user parameter
    *pStringA = stringA;

    return ERROR_SUCCESS;
} /* allocConvertNarrow */


DWORD
AllocConvertWide(
    IN LPCSTR StringA,
    OUT LPWSTR *pStringW
    )

/*++

Routine Description:

Helper routine to convert a narrow string to a newly allocated wide one

Arguments:

    StringA -
    pStringW -

Return Value:

    DWORD -

--*/

{
    DWORD numberWideChars, numberConvertedChars, status;
    LPWSTR stringW;

    if (pStringW == NULL) {
        return ERROR_INVALID_PARAMETER;
    }

    if (StringA == NULL) {
        *pStringW = NULL;
        return ERROR_SUCCESS;
    }

    // Get the needed length
    numberWideChars = MultiByteToWideChar(
        CP_ACP,
        MB_PRECOMPOSED,
        StringA,
        -1,
        NULL,
        0);

    if (numberWideChars == 0) {
        return ERROR_INVALID_PARAMETER;
    }

    // Allocate the new buffer
    stringW = LocalAlloc( LPTR, (numberWideChars + 1) * sizeof( WCHAR ) );
    if (stringW == NULL) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    // Do the conversion into the new buffer
    numberConvertedChars = MultiByteToWideChar(
        CP_ACP,
        MB_PRECOMPOSED,
        StringA,
        -1,
        stringW,
        numberWideChars + 1);
    if (numberConvertedChars == 0) {
        LocalFree( stringW );
        return ERROR_INVALID_PARAMETER;
    }

    // return user parameter
    *pStringW = stringW;

    return ERROR_SUCCESS;
} /* allocConvertWide */


DWORD
AllocConvertWideBuffer(
    IN  DWORD   LengthA,
    IN  PCCH    BufferA,
    OUT PWCHAR  *OutBufferW
    )

/*++

Routine Description:

    Converts narrow buffer to newly allocated wide one

Arguments:

    LengthA    - number of chars in BufferA
    BufferA    - Buffer of narrow chars
    OutBufferW - Address of buffer of wide chars

Return Value:

    Win32 Status

--*/
{
    DWORD   Status;
    DWORD   NumberWideChars;
    DWORD   ConvertedChars;
    PWCHAR  BufferW;

    //
    // no output buffer address; error
    //
    if (OutBufferW == NULL) {
        return ERROR_INVALID_PARAMETER;
    }
    *OutBufferW = NULL;

    //
    // No input buffer; return NULL
    //
    if (BufferA == NULL || LengthA == 0) {
        *OutBufferW = NULL;
        return ERROR_SUCCESS;
    }

    //
    // Get the needed length in chars
    //
    NumberWideChars = MultiByteToWideChar(CP_ACP,
                                          MB_PRECOMPOSED,
                                          BufferA,
                                          LengthA,
                                          NULL,
                                          0);

    if (NumberWideChars == 0) {
        return ERROR_INVALID_PARAMETER;
    }

    //
    // Allocate the new buffer
    //
    BufferW = LocalAlloc(LPTR,
                         NumberWideChars * sizeof(WCHAR));
    if (BufferW == NULL) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    //
    // Do the conversion into the new buffer
    //
    ConvertedChars = MultiByteToWideChar(CP_ACP,
                                         MB_PRECOMPOSED,
                                         BufferA,
                                         LengthA,
                                         BufferW,
                                         NumberWideChars);
    if (ConvertedChars == 0) {
        LocalFree(BufferW);
        return ERROR_INVALID_PARAMETER;
    }

    // return user parameter
    *OutBufferW = BufferW;

    return ERROR_SUCCESS;
} /* AllocConvertWideBuffer */


DWORD
AllocBuildDsname(
    IN LPCWSTR StringDn,
    OUT DSNAME **ppName
    )

/*++

Routine Description:

    Description

Arguments:

    None

Return Value:

    None

--*/

{
    DWORD length, dsnameBytes;
    DSNAME *pName;

    if (StringDn == NULL) {
        *ppName = NULL;
        return ERROR_SUCCESS;
    }

    length = wcslen( StringDn );
    dsnameBytes = DSNameSizeFromLen( length );

    pName = (DSNAME *) LocalAlloc( LPTR, dsnameBytes );
    if (pName == NULL) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    pName->NameLen = length;
    pName->structLen = dsnameBytes;
    wcscpy( pName->StringName, StringDn );

    *ppName = pName;

    return ERROR_SUCCESS;
}


DWORD
ConvertScheduleToReplTimes(
    PSCHEDULE pSchedule,
    REPLTIMES *pReplTimes
    )

/*++

Routine Description:

Convert a public SCHEDULE into a REPLTIMES structure.

The public schedule must be of INTERVAL type, and only contain one header.

The difference between the data in a public SCHEDULE and the data in a
REPLTIMES is that the former only uses 1 byte for each hour, with the high
nybble unused, while the latter encodes two hours in each byte.

Arguments:

    pSchedule -
    pReplTimes -

Return Value:

    DWORD -

--*/

{
    PUCHAR pData = (PUCHAR) (pSchedule + 1);  // point just after structure
    DWORD hour;

    if ( (pSchedule == NULL) ||
         (pReplTimes == NULL) ||
         (pSchedule->Size != sizeof( SCHEDULE ) + SCHEDULE_DATA_ENTRIES) ||
         (pSchedule->NumberOfSchedules != 1) ||
         (pSchedule->Schedules[0].Type != SCHEDULE_INTERVAL) ||
         (pSchedule->Schedules[0].Offset != sizeof( SCHEDULE ) ) ) {
        return ERROR_INVALID_PARAMETER;
    }

    for( hour = 0; hour < SCHEDULE_DATA_ENTRIES; hour += 2 ) {
        pReplTimes->rgTimes[hour/2] =
            (UCHAR) (((pData[hour + 1] & 0xf) << 4) | (pData[hour] & 0xf));
    }

    return ERROR_SUCCESS;
} /* ConvertScheduleToReplTimes */

VOID
HandleClientRpcException(
    DWORD    dwErr,
    HANDLE * phDs
    )
/*++
Routine Description:
    
    Procedure to handle client rpc exceptions in ntdsapi.dll on calls to
    an ntdsa.dll rpc interface.
    
Arguments:
    
    dwErr - returned from RpcExceptionCode inside an exception block.

Return Value:
    None.
    
Notes:
    
    GregJohn 6/12/01 - Eventually this function should be shared code with
    drsuapi.c so both client interfaces can share this logic(ie CHECK_RPC_SERVER_NOT_REACHABLE).  
    Also, we can put MAP_SECURITY_PACKAGE_ERROR in this function at that time.
--*/
{

    CHECK_RPC_SERVER_NOT_REACHABLE(*phDs, dwErr);
    DPRINT_RPC_EXTENDED_ERROR( dwErr ); 
}

#ifdef _NTDSAPI_POSTXP_ASLIB_

/*

Only, defined in ntdsapi_postxp_aslib.lib, because it's only needed there,
and I want to make sure that the actual ntdsapi.dll doesn't load this, because
if it were, you may be having a infinite circular recursion.  Basically, if
we're in the actual DLL, then you shouldn't need to call this function, because
any time you'd think of loading ntdsapi.dll you'd already be in the DLL.

*/

HMODULE 
NtdsapiLoadLibraryHelper(
    WCHAR * szDllName
    )
{
    HMODULE hDll = NULL;

    hDll = LoadLibraryW(szDllName);

    Assert(hDll || GetLastError());
    return hDll;    
}

#endif

/* end util.c */
