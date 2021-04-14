/*++

Copyright (c) 1997-1998  Microsoft Corporation

Module Name:

    ismip.c

Abstract:

   This module is a plug-in DLL for the Inter-Site Messaging service, which is part of
   the mail-based replication subsystem in the Directory Service.

   This DLL, as is any instance of the ISM plug-in DLL class, provides a set of abstract
   transport functions, such as send, receive, and so on.  See plugin.h for details.

   This implementation is based on sockets, using the TCP protocol.  This is used for testing.
   A true implementation would not require the server to be up at the time of send.

Author:

    Will Lees (wlees) 25-Nov-1997

Environment:

    User-mode, win32 dll
    To be loaded by another image

Notes:

    optional-notes

Revision History:


--*/

#define UNICODE 1

#include <ntdspch.h>

#include <rpc.h>
#include <rpcndr.h>   // midl_user_free
#include <ismapi.h>
#include <debug.h>
#include <fileno.h>
#include <drs.h>     // DSTIME
#include <ntdsa.h>   // Option flags

// TODO: better place to put these?
typedef ULONG MessageId;
typedef ULONG ATTRTYP;
#include <dsevent.h>

#include "private.h"

// Needed by dscommon.lib.
DWORD ImpersonateAnyClient(   void ) { return ERROR_CANNOT_IMPERSONATE; }
VOID  UnImpersonateAnyClient( void ) { ; }

#define DEBSUB "ISMIP:"
#define FILENO FILENO_ISMSERV_ISMIP

// Set this non-zero to display debug messages
#define UNIT_TEST_DEBUG 0

/* External */

// Event logging config (as exported from ismserv.exe).
DS_EVENT_CONFIG * gpDsEventConfig = NULL;

/* Static */

// Lock on instances list
CRITICAL_SECTION TransportListLock;

// List head of transport instances
LIST_ENTRY TransportListHead;

/* Forward */ /* Generated by Emacs 19.34.1 on Wed Nov 04 09:54:07 1998 */

BOOL
WINAPI
DllMain(
     IN HINSTANCE hinstDll,
     IN DWORD     fdwReason,
     IN LPVOID    lpvContext OPTIONAL
     );

DWORD
IsmStartup(
    IN  LPCWSTR         pszTransportDN,
    IN  ISM_NOTIFY *    pNotifyFunction,
    IN  HANDLE          hNotify,
    OUT HANDLE          *phIsm
    );

DWORD
IsmRefresh(
    IN  HANDLE          hIsm,
    IN  ISM_REFRESH_REASON_CODE eReason,
    IN  LPCWSTR         pszObjectDN              OPTIONAL
    );

void
IsmShutdown(
    IN  HANDLE          hIsm,
    IN  ISM_SHUTDOWN_REASON_CODE eReason
    );

DWORD
IsmSend(
    IN  HANDLE          hIsm,
    IN  LPCWSTR         pszRemoteTransportAddress,
    IN  LPCWSTR         pszServiceName,
    IN  const ISM_MSG *       pMsg
    );

DWORD
IsmReceive(
    IN  HANDLE          hIsm,
    IN  LPCWSTR         pszServiceName,
    OUT ISM_MSG **      ppMsg
    );

void
IsmFreeMsg(
    IN  HANDLE          hIsm,
    IN  ISM_MSG *       pMsg
    );

DWORD
IsmGetConnectivity(
    IN  HANDLE                  hIsm,
    OUT ISM_CONNECTIVITY **     ppConnectivity
    );

void
IsmFreeConnectivity(
    IN  HANDLE              hIsm,
    IN  ISM_CONNECTIVITY *  pConnectivity
    );

DWORD
IsmGetTransportServers(
    IN  HANDLE               hIsm,
    IN  LPCWSTR              pszSiteDN,
    OUT ISM_SERVER_LIST **   ppServerList
    );

void
IsmFreeTransportServers(
    IN  HANDLE              hIsm,
    IN  ISM_SERVER_LIST *   pServerList
    );

DWORD
IsmGetConnectionSchedule(
    IN  HANDLE              hIsm,
    IN  LPCWSTR             pszSite1DN,
    IN  LPCWSTR             pszSite2DN,
    OUT ISM_SCHEDULE **     ppSchedule
    );

void
IsmFreeConnectionSchedule(
    IN  HANDLE              hIsm,
    IN  ISM_SCHEDULE *      pSchedule
    );

/* End Forward */


DWORD
InitializeCriticalSectionHelper(
    CRITICAL_SECTION *pcsCriticalSection
    )

/*++

Routine Description:

Wrapper function to handle exception handling in the
InitializeCriticalSection() function.

Arguments:

    pcsCriticalSection - pointer to critical section

Return Value:

    DWORD - status code

--*/

{
    DWORD status;

    __try {
        InitializeCriticalSection( pcsCriticalSection );
        status = ERROR_SUCCESS;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        status = GetExceptionCode();
    }
    return status;
} /* initializeCriticalSectionHelper */

WINAPI
DllMain(
     IN HINSTANCE hinstDll,
     IN DWORD     fdwReason,
     IN LPVOID    lpvContext OPTIONAL
     )
/*++

 Routine Description:

   This function DllLibMain() is the main initialization function for
    this DLL. It initializes local variables and prepares it to be invoked
    subsequently.

 Arguments:

   hinstDll          Instance Handle of the DLL
   fdwReason         Reason why NT called this DLL
   lpvReserved       Reserved parameter for future use.

 Return Value:

    Returns TRUE is successful; otherwise FALSE is returned.
--*/
{
    DWORD status;
    BOOL  fReturn = TRUE;
    LPSTR rgpszDebugParams[] = {"lsass.exe", "-noconsole"};
    DWORD cNumDebugParams = sizeof(rgpszDebugParams)/sizeof(rgpszDebugParams[0]);

    switch (fdwReason )
    {
    case DLL_PROCESS_ATTACH:
    {
        // DO QUICK STUFF HERE - DO THE REST IN STARTUP/FIRSTTIME

        // Debug library is already initialized in dll, ismserv.exe,
        // where the library is exported from.

        // Get event logging config (as exported from ismserv.exe).
        gpDsEventConfig = DsGetEventConfig();

#if DBG
#if UNIT_TEST_DEBUG
        DebugInfo.severity = 1;
        strcpy( DebugInfo.DebSubSystems, "ISMIP:IPDGRPC:" ); 
//        DebugInfo.severity = 3;
//        strcpy( DebugInfo.DebSubSystems, "*" ); 
#endif
        DebugMemoryInitialize();
#endif
        
        if (ERROR_SUCCESS != InitializeCriticalSectionHelper( &TransportListLock )) {
            fReturn = FALSE;
            goto exit;
        }
        InitializeListHead( &TransportListHead );
        break;
    }
    case DLL_PROCESS_DETACH:
    {
        // Make sure all threads stopped
        // Empty address list
        DeleteCriticalSection( &TransportListLock );
        if (!IsListEmpty( &TransportListHead )) {
            DPRINT( 0, "Warning: Not all transport instances were shutdown\n" );
        }
#if DBG
        DebugMemoryTerminate();
#endif

        // Debug library is terminated in dll, ismserv.exe,
        // where the library is exported from.

        break;
    }
    default:
        break;
    }   /* switch */

exit:
    return ( fReturn);
}  /* DllLibMain() */

DWORD
IsmStartup(
    IN  LPCWSTR         pszTransportDN,
    IN  ISM_NOTIFY *    pNotifyFunction,
    IN  HANDLE          hNotify,
    OUT HANDLE          *phIsm
    )
/*++

Routine Description:

    Initialize the plug-in.

Arguments:

    pszTransportDN (IN) - The DN of the Inter-Site-Transport that named this
        DLL as its plug-in.  The DS object may contain additional configuration
        information for the transport (e.g., the name of an SMTP server for
        an SMTP transport).

    pNotifyFunction (IN) - Function to call to notify the ISM service of pending
        messages.

    hNotify (IN) - Parameter to supply to the notify function.

    phIsm (OUT) - On successful return, holds a handle to be used in
        future calls to the plug-in for the named Inter-Site-Transport.  Note
        that it is possible for more than one Inter-Site-Transport object to
        name a given DLL as its plug-in, in which case IsmStartup() will be
        called for each such object.

Return Values:

    NO_ERROR - Successfully initialized.

    other - Failure.
        
--*/
{
    DWORD length;
    PTRANSPORT_INSTANCE instance;
    DWORD status;
    BOOLEAN firsttime;
    BOOLEAN fNotifyInit = FALSE;

    DPRINT1( 1, "IsmStartup, transport='%ws'\n", pszTransportDN );

    // Check validity of arguments

    if (phIsm == NULL) {
        status = ERROR_INVALID_PARAMETER;
        LogUnhandledError( status );
        return status;
    }

    length = wcslen( pszTransportDN );
    if (length == 0) {
        status = ERROR_INVALID_PARAMETER;
        LogUnhandledError( status );
        return status;
    }

    // Restrict to only one transport instance.
    EnterCriticalSection(&TransportListLock);
    __try {
        firsttime = IsListEmpty( &(TransportListHead) );
    }
    __finally {
        LeaveCriticalSection(&TransportListLock);
    }
    if (!firsttime) {
        status = ERROR_INVALID_PARAMETER;
        LogUnhandledError( status );
        return status;
    }

    // Allocate a new transport instance
    // Zero memory to simply cleanup
    instance = NEW_TYPE_ZERO( TRANSPORT_INSTANCE );
    if (instance == NULL) {
        // error: insufficient resources
        status = ERROR_NOT_ENOUGH_MEMORY;
        LogUnhandledError( status );
        return status;
    }

    // INITIALIZE TRANSPORT INSTANCE HERE
    // All values initially zero

    instance->Size = sizeof( TRANSPORT_INSTANCE );
    Assert( instance->ReferenceCount == 0 );
    instance->pNotifyFunction = pNotifyFunction;
    instance->hNotify = hNotify;
    InitializeListHead( &(instance->ServiceListHead) );
    status = InitializeCriticalSectionHelper( &(instance->Lock) );
    if (ERROR_SUCCESS != status) {
        goto cleanup;
    }
    // ReplInterval is 0, meaning the application should take default
    // Default is schedules significant, bridges not required (transitive)
    instance->Options = 0;

    // INITIALIZE TRANSPORT INSTANCE HERE

    instance->Name = NEW_TYPE_ARRAY( (length + 1), WCHAR );
    if (instance->Name == NULL) {
        status = ERROR_NOT_ENOUGH_MEMORY;
        LogUnhandledError( status );
        goto cleanup;
    }
    wcscpy( instance->Name, pszTransportDN );

    // ***********************************************************************

    // Create event to signal shutdown.
    instance->hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (NULL == instance->hShutdownEvent) {
        status = GetLastError();
        DPRINT1(0, "Failed to create shutdown event, error %d.\n", status);
        LogUnhandledError( status );
        goto cleanup;
    }

    // Open connection to directory service
    status = DirOpenConnection( &instance->DirectoryConnection );
    if (status != ERROR_SUCCESS) {
        LogUnhandledError( status );
        goto cleanup;
    }

    // Make sure key exists
    status = DirReadTransport( instance->DirectoryConnection, instance );
    if (status != ERROR_SUCCESS) {
        LogUnhandledError( status );
        goto cleanup;
    }

    // Start monitoring for routing changes
    status = DirStartNotifyThread( instance );
    if (status != ERROR_SUCCESS) {
        LogUnhandledError( status );
        goto cleanup;
    }
    fNotifyInit = TRUE;

    // Insert this instance into the list.
    
    // Note that, assuming the ISM service is functioning correctly, this list
    // cannot contain duplicates (where "duplicate" is defined as an entry with
    // the same DN).
    
    EnterCriticalSection(&TransportListLock);
    __try {
        InsertTailList( &TransportListHead, &(instance->ListEntry) );
    }
    __finally {
        LeaveCriticalSection(&TransportListLock);
    }

    InterlockedIncrement( &(instance->ReferenceCount) );  // 1 for the lifetime of this transport

    *phIsm = instance;

    return ERROR_SUCCESS;

cleanup:
    instance->fShutdownInProgress = TRUE;

    if (fNotifyInit) {
        (void) DirEndNotifyThread( instance );
    }

    if (instance->DirectoryConnection) {
        (void) DirCloseConnection( instance->DirectoryConnection );
    }

    if (instance->hShutdownEvent != NULL) {
        CloseHandle( instance->hShutdownEvent );
    }

    if (instance->Name != NULL) {
        FREE_TYPE( instance->Name );
    }

    Assert( instance->ReferenceCount == 0 );
    FREE_TYPE(instance);

    return status;
}

DWORD
IsmRefresh(
    IN  HANDLE          hIsm,
    IN  ISM_REFRESH_REASON_CODE eReason,
    IN  LPCWSTR         pszObjectDN              OPTIONAL
    )
/*++

Routine Description:

    Called whenever changes occur to the Inter-Site-Transport object specified
    in the IsmStartup() call.

Arguments:

    hIsm (IN) - Handle returned by a prior call to IsmStartup().

    eReason (IN) - Reason code for refresh

    pszObjectDN (IN) - DN of the Inter-Site-Transport object.  This is
        guaranteed to be the same as the DN passed in IsmStartup, as inter-site
        transport objects cannot be renamed.

Return Values:

    0 or Win32 error code.
    
--*/
{
    PTRANSPORT_INSTANCE instance = (PTRANSPORT_INSTANCE) hIsm;
    DWORD status, oldOptions, oldReplInterval;

    DPRINT2( 1, "IsmRefresh, reason = %d, new name = %ws\n",
             eReason,
             pszObjectDN ? pszObjectDN : L"not supplied" );

    if (instance->Size != sizeof( TRANSPORT_INSTANCE )) {
        return ERROR_INVALID_PARAMETER;
    }

    if ( (eReason == ISM_REFRESH_REASON_RESERVED) ||
         (eReason >= ISM_REFRESH_REASON_MAX) ) {
        return ERROR_INVALID_PARAMETER;
    }

    // If a site changed in any way, just invalidate the cache

    if (eReason == ISM_REFRESH_REASON_SITE) {
        // Invalidate connectivity cache
        RouteInvalidateConnectivity( instance );
        status = ERROR_SUCCESS;
        goto cleanup;
    }

    Assert( eReason == ISM_REFRESH_REASON_TRANSPORT );

    // Inter-site transport objects cannot be renamed.
    Assert((pszObjectDN == NULL)
           || (0 == _wcsicmp(pszObjectDN, instance->Name)));

    oldOptions = instance->Options;
    oldReplInterval = instance->ReplInterval;

    // Reread parameters from the registry 
    status = DirReadTransport( instance->DirectoryConnection, instance );
    if (status != ERROR_SUCCESS) {
        goto cleanup;
    }

    if ( ( oldOptions != instance->Options) ||
         ( oldReplInterval != instance->ReplInterval ) ) {
        // Invalidate connectivity cache
        RouteInvalidateConnectivity( instance );
    }

cleanup:

    return status;
}

void
IsmShutdown(
    IN  HANDLE          hIsm,
    IN  ISM_SHUTDOWN_REASON_CODE eReason
    )
/*++

Routine Description:

    Uninitialize transport plug-in.

Arguments:

    hIsm (IN) - Handle returned by a prior call to IsmStartup().
    eReason (IN) - Reason for shutdown

Return Values:

    None.

--*/
{
    DWORD status;
    PTRANSPORT_INSTANCE transport = (PTRANSPORT_INSTANCE) hIsm;
    BOOL fFailed;
    LONG count;

    DPRINT2( 1, "IsmShutdown %ws, Reason %d\n", transport->Name, eReason );

    // Validate
    if (transport->Size != sizeof( TRANSPORT_INSTANCE )) {
        // error: invalid parameter
        return;
    }
    transport->fShutdownInProgress = TRUE;

    SetEvent(transport->hShutdownEvent);

    // Remove this instance from the list.
    EnterCriticalSection(&TransportListLock);
    __try {
        RemoveEntryList( &(transport->ListEntry) );
    }
    __finally {
        LeaveCriticalSection(&TransportListLock);
    }

    DeleteCriticalSection( &(transport->Lock) );

    DirEndNotifyThread( transport );

    count = InterlockedDecrement( &(transport->ReferenceCount) );  // 1 for the lifetime of this transport
    if (count == 0) {
        // Clean up only if thread is finished

        // Release any routing state
        RouteFreeState( transport );

        // Close connection to directory
        status = DirCloseConnection( transport->DirectoryConnection );
        // ignore error

        CloseHandle( transport->hShutdownEvent );
        transport->hShutdownEvent = NULL;

        // RUNDOWN TRANSPORT INSTANCE HERE

        transport->Size = 0; // clear signature to prevent reuse
        FREE_TYPE( transport->Name );
        FREE_TYPE( transport );

        // RUNDOWN TRANSPORT INSTANCE HERE
    } else {
        DPRINT2( 0, "Transport %ws not completely shutdown, %d references still exist.\n",
                 transport->Name, count );
    }
}

DWORD
IsmSend(
    IN  HANDLE          hIsm,
    IN  LPCWSTR         pszRemoteTransportAddress,
    IN  LPCWSTR         pszServiceName,
    IN  const ISM_MSG *       pMsg
    )
/*++

Routine Description:

    Send a message over this transport.

Arguments:

    hIsm (IN) - Handle returned by a prior call to IsmStartup().

    pszRemoteTransportAddress (IN) - Transport address of the destination
        server.

    pszServiceName (IN) - Name of the service on the remote machine that is the
        intended receiver of the message.

Return Values:

    0 or Win32 error.

--*/
{
    // The IP transport does not support send/receive.
    return ERROR_NOT_SUPPORTED;
}

DWORD
IsmReceive(
    IN  HANDLE          hIsm,
    IN  LPCWSTR         pszServiceName,
    OUT ISM_MSG **      ppMsg
    )
/*++

Routine Description:

    Return the next waiting message (if any).  If no message is waiting, a NULL
    message is returned.  If a non-NULL message is returned, the ISM service
    is responsible for calling IsmFreeMsg(*ppMsg) when the message is no longer
    needed.

    If a non-NULL message is returned, it is immediately dequeued.  (I.e., once
    a message is returned through IsmReceive(), the transport is free to destroy
    it.)

This routine is pretty simple.  It finds the service, and dequeues a message if there is any.
Queue the message is done by the listener thread behind the scenes.

Arguments:

    hIsm (IN) - Handle returned by a prior call to IsmStartup().

    ppMsg (OUT) - On successful return, holds a pointer to the received message
        or NULL.

Return Values:

    0 or Win32 error.

--*/
{
    // The IP transport does not support send/receive.
    // Return "no message waiting."
    *ppMsg = NULL;
    return 0;
}


void
IsmFreeMsg(
    IN  HANDLE          hIsm,
    IN  ISM_MSG *       pMsg
    )
/*++

Routine Description:

    Frees a message returned by IsmReceive().

Arguments:

    hIsm (IN) - Handle returned by a prior call to IsmStartup().

    pMsg (IN) - Message to free.

Return Values:

    None.

--*/
{
    // The IP transport does not support send/receive.
    ;
}

DWORD
IsmGetConnectivity(
    IN  HANDLE                  hIsm,
    OUT ISM_CONNECTIVITY **     ppConnectivity
    )
/*++

Routine Description:

    Compute the costs associated with transferring data amongst sites.

    On successful return, the ISM service will eventually call
    IsmFreeConnectivity(hIsm, *ppConnectivity);

The transport has associated with it some lingering state.  The matrix of schedules is not
freed at the end of this routine.  It remains, tied to the transport handle, for the benefit
of the GetConnectionSchedule api.

There is no time-based caching of this information.  Each time this routine is called, the
information is regnerated.  GetConnectionSchedule api uses the matrix of schedules from the
last time this call was made, regardless of time.

Arguments:

    hIsm (IN) - Handle returned by a prior call to IsmStartup().

    ppConnectivity (OUT) - On successful return, holds a pointer to the
        ISM_CONNECTIVITY structure describing the interconnection of sites
        along this transport.

Return Values:

    NO_ERROR - Success.
    ERROR_* - Failure.

--*/
{
    DWORD status, numberSites, i;
    PTRANSPORT_INSTANCE transport = (PTRANSPORT_INSTANCE) hIsm;
    PWSTR *pSiteList;
    PISM_LINK pLinkArray;
    PISM_CONNECTIVITY pConnectivity;

    DPRINT( 2, "IsmGetConnectivity\n" );

    // Validate

    if (transport->Size != sizeof( TRANSPORT_INSTANCE )) {
        return ERROR_INVALID_PARAMETER;
    }


    InterlockedIncrement( &(transport->ReferenceCount) );  // 1 for this call

    __try {

        // Get the site list and connectivity matrix

        status = RouteGetConnectivity( transport, &numberSites, &pSiteList,
                                       &pLinkArray, transport->Options,
                                       transport->ReplInterval );

        if (status != ERROR_SUCCESS) {
            DPRINT1( 0, "failed to get connectivity, error %d\n", status );
            __leave;
        }

        // Return null structure to indicate no sites
        if (numberSites == 0) {
            ppConnectivity = NULL; // No connectivity
            status = ERROR_SUCCESS;
            __leave;
        }

        Assert( pLinkArray );
        Assert( pSiteList );

        // Build a connectivity structure to return

        pConnectivity = NEW_TYPE( ISM_CONNECTIVITY );
        if (pConnectivity == NULL) {
            DPRINT( 0, "failed to allocate memory for ISM CONNECTIVITY\n" );

            // Cleanup the pieces
            DirFreeSiteList( numberSites, pSiteList );
            RouteFreeLinkArray( transport, pLinkArray );

            status = ERROR_NOT_ENOUGH_MEMORY;
            __leave;
        }

        pConnectivity->cNumSites = numberSites;
        pConnectivity->ppSiteDNs = pSiteList;
        pConnectivity->pLinkValues = pLinkArray;
        *ppConnectivity = pConnectivity;

        status = ERROR_SUCCESS;
    } __finally {
        InterlockedDecrement( &(transport->ReferenceCount) );  // 1 for this call
    }

    return status;
}

void
IsmFreeConnectivity(
    IN  HANDLE              hIsm,
    IN  ISM_CONNECTIVITY *  pConnectivity
    )
/*++

Routine Description:

    Frees the structure returned by IsmGetConnectivity().

Arguments:

    hIsm (IN) - Handle returned by a prior call to IsmStartup().

    pSiteConnectivity (IN) - Structure to free.

Return Values:

    None.

--*/
{
    PTRANSPORT_INSTANCE transport = (PTRANSPORT_INSTANCE) hIsm;
    DWORD i;

    DPRINT( 2, "IsmFreeConnectivity\n" );

    // Validate

    if (transport->Size != sizeof( TRANSPORT_INSTANCE )) {
        return; // error: invalid parameter
    }

    if (pConnectivity == NULL) {
        return;
    }

    // Free individual components

    if (pConnectivity->cNumSites > 0) {
        DirFreeSiteList( pConnectivity->cNumSites, pConnectivity->ppSiteDNs );

        RouteFreeLinkArray( transport, pConnectivity->pLinkValues );
    }

    FREE_TYPE( pConnectivity );
}

DWORD
IsmGetTransportServers(
    IN  HANDLE               hIsm,
    IN  LPCWSTR              pszSiteDN,
    OUT ISM_SERVER_LIST **   ppServerList
    )
/*++

Routine Description:

    Retrieve the DNs of servers in a given site that are capable of sending and
    receiving data via this transport.

    On successful return of a non-NULL list, the ISM service will eventually call
    IsmFreeTransportServers(hIsm, *ppServerList);

Arguments:

    hIsm (IN) - Handle returned by a prior call to IsmStartup().

    pszSiteDN (IN) - Site to query.

    ppServerList - On successful return, holds a pointer to a structure
        containing the DNs of the appropriate servers or NULL.  If NULL, any
        server with a value for the transport address type attribute can be
        used.

Return Values:

    NO_ERROR - Success.
    ERROR_* - Failure.

--*/
{
    PTRANSPORT_INSTANCE transport = (PTRANSPORT_INSTANCE) hIsm;
    PISM_SERVER_LIST pIsmServerList;
    DWORD numberServers, status, i;
    PWSTR *serverList;

    DPRINT1( 2, "IsmGetTransportServers, site = %ws\n", pszSiteDN );

    // Validate

    if (transport->Size != sizeof( TRANSPORT_INSTANCE )) {
        return ERROR_INVALID_PARAMETER;
    }

    InterlockedIncrement( &(transport->ReferenceCount) );  // 1 for this call

    __try {
        // Get the server list

        status = DirGetSiteBridgeheadList( transport, transport->DirectoryConnection,
                                           pszSiteDN, &numberServers, &serverList );
        if (status != ERROR_SUCCESS) {

            if (status == ERROR_FILE_NOT_FOUND) {
                *ppServerList = NULL; // All servers
                status = ERROR_SUCCESS;
                __leave;
            }

            DPRINT1( 0, "failed to get registry server list, error = %d\n", status );
            __leave;
        }

        // Return null structure to indicate no servers
        if (numberServers == 0) {
            *ppServerList = NULL; // All servers
            status = ERROR_SUCCESS;
            __leave;
        }

        // Construct the server structure

        pIsmServerList = NEW_TYPE( ISM_SERVER_LIST );
        if (pIsmServerList == NULL) {
            DPRINT( 0, "failed to allocate memory for ISM SERVER LIST\n" );

            // Clean up the pieces
            for( i = 0; i < numberServers; i++ ) {
                FREE_TYPE( serverList[i] );
            }
            FREE_TYPE( serverList );

            status = ERROR_NOT_ENOUGH_MEMORY;
            __leave;
        }

        pIsmServerList->cNumServers = numberServers;
        pIsmServerList->ppServerDNs = serverList;

        *ppServerList = pIsmServerList;

        status = ERROR_SUCCESS;
    } __finally {
        InterlockedDecrement( &(transport->ReferenceCount) );  // 1 for this call
    }

    return status;
}

void
IsmFreeTransportServers(
    IN  HANDLE              hIsm,
    IN  ISM_SERVER_LIST *   pServerList
    )

/*++

Routine Description:

    Frees the structure returned by IsmGetTransportServers().

Arguments:

    hIsm (IN) - Handle returned by a prior call to IsmStartup().

    pServerList (IN) - Structure to free.

Return Values:

    None.

--*/
{
    PTRANSPORT_INSTANCE transport = (PTRANSPORT_INSTANCE) hIsm;
    DWORD i;

    DPRINT( 2, "IsmFreeTransportServers\n" );

    // Validate

    if (transport->Size != sizeof( TRANSPORT_INSTANCE )) {
        return; // error: invalid parameter
    }

    if (pServerList == NULL) {
        return;
    }

    // Free individual components

    if ( (pServerList->cNumServers != 0) && (pServerList->ppServerDNs != NULL) ) {
        for( i = 0; i < pServerList->cNumServers; i++ ) {
            FREE_TYPE( pServerList->ppServerDNs[i] );
        }
        FREE_TYPE( pServerList->ppServerDNs );
    }

    FREE_TYPE( pServerList );
}

DWORD
IsmGetConnectionSchedule(
    IN  HANDLE              hIsm,
    IN  LPCWSTR             pszSite1DN,
    IN  LPCWSTR             pszSite2DN,
    OUT ISM_SCHEDULE **     ppSchedule
    )

/*++

Routine Description:

    Retrieve the schedule by which two given sites are connected via this
    transport.

    On successful return, it is the ISM service's responsibility to eventually
    call IsmFreeSchedule(*ppSchedule);

The transport has associated with it some lingering state.  The matrix of schedules is not
freed at the end of this routine.  It remains, tied to the transport handle, for the benefit
of the GetConnectionSchedule api.

There is no time-based caching of this information.  Each time the get conn routine is called, the
information is regnerated.  GetConnectionSchedule api uses the matrix of schedules from the
last time this call was made, regardless of time.

The actual semantics of this routine are that it returns a non-default schedule
if there is one.  Otherwise, it returns the default, all available schedule.
If you desire to know if there is a path between the two sites, consult the
cost matrix first. It is a feature of this routine that inquiring a schedule
for a non-connected pair of sites will return NULL for all available.

Arguments:

    hIsm (IN) - Handle returned by a prior call to IsmStartup().

    pszSite1DN, pszSite2DN (IN) - Sites to query.

    ppSchedule - On successful return, holds a pointer to a structure
        describing the schedule by which the two given sites are connected via
        the transport, or NULL if the sites are always connected.

Return Values:

    NO_ERROR - Success.
    ERROR_* - Failure.

--*/
{
    DWORD status, length;
    PTRANSPORT_INSTANCE transport = (PTRANSPORT_INSTANCE) hIsm;
    PBYTE pSchedule;

    DPRINT2( 2, "IsmGetConnectionSchedule, site1 = %ws, site2 = %ws\n",
            pszSite1DN, pszSite2DN );

    // Validate

    if (transport->Size != sizeof( TRANSPORT_INSTANCE )) {
        return ERROR_INVALID_PARAMETER;
    }

    status = RouteGetPathSchedule( transport,
                                   pszSite1DN,
                                   pszSite2DN,
                                   &pSchedule,
                                   &length );
    if (status != ERROR_SUCCESS) {
        return status;
    }

    if (pSchedule == NULL) {
        *ppSchedule = NULL; // always connected
    } else {
        *ppSchedule = NEW_TYPE( ISM_SCHEDULE );
        if (*ppSchedule == NULL) {
            FREE_TYPE( pSchedule );
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        (*ppSchedule)->cbSchedule = length;
        (*ppSchedule)->pbSchedule = pSchedule;
    }

    return ERROR_SUCCESS;
}

void
IsmFreeConnectionSchedule(
    IN  HANDLE              hIsm,
    IN  ISM_SCHEDULE *      pSchedule
    )

/*++

Routine Description:

    Frees the structure returned by IsmGetTransportServers().

Arguments:

    hIsm (IN) - Handle returned by a prior call to IsmStartup().

    pSchedule (IN) - Structure to free.

Return Values:

    None.

--*/
{
    PTRANSPORT_INSTANCE transport = (PTRANSPORT_INSTANCE) hIsm;

    DPRINT( 2, "IsmFreeConnectionSchedule\n" );

    // Validate

    if (transport->Size != sizeof( TRANSPORT_INSTANCE )) {
        return; // error: invalid parameter
    }

    if (pSchedule == NULL) {
        return;
    }

    Assert( pSchedule->cbSchedule != 0 );
    Assert( pSchedule->pbSchedule );

    FREE_TYPE( pSchedule->pbSchedule );

    pSchedule->pbSchedule = NULL;
    pSchedule->cbSchedule = 0;

    FREE_TYPE( pSchedule );
}


DWORD
IsmQuerySitesByCost(
    IN  HANDLE                      hIsm,
    IN  LPCWSTR                     pszFromSite,
    IN  DWORD                       cToSites,
    IN  LPCWSTR*                    rgszToSites,
    IN  DWORD                       dwFlags,
    OUT ISM_SITE_COST_INFO_ARRAY**  prgSiteInfo
    )
/*++

Routine Description:

    Determine the individual costs between the From site and the To sites.

Arguments:

    pszFromSite (IN) - The name (not distinguished) of the From site.

    rgszToSites (IN) - An array containing the names of the To sites.

    cToSites (IN) - The number of entries in the rgszToSites array.

    dwFlags (IN) - Unused.

    prgSiteInfo (IN) - On successful return, holds a pointer to a structure
        containing the costs between the From site and the To sites.
    
Return Values:

    NO_ERROR - Success.
    ERROR_* - Failure.

--*/
{
    PTRANSPORT_INSTANCE transport = (PTRANSPORT_INSTANCE) hIsm;
    DWORD status;

    DPRINT2( 2, "IsmQuerySitesByCost (FromSite=%ls, cToSites=%d)\n",
        pszFromSite, cToSites );

    // Validate
    if (transport->Size != sizeof( TRANSPORT_INSTANCE )) {
        return ERROR_INVALID_PARAMETER;
    }

    InterlockedIncrement( &(transport->ReferenceCount) );  // 1 for this call

    __try {

        status = RouteQuerySitesByCost( transport, pszFromSite,
                    cToSites, rgszToSites, dwFlags, prgSiteInfo );
        if (status != ERROR_SUCCESS) {
            DPRINT1( 0, "Failed to get query sites, error %d\n", status );
            __leave;
        } else {
            DPRINT( 2, "RouteQuerySitesByCost succeeded\n" );
        }

    } __finally {
        InterlockedDecrement( &(transport->ReferenceCount) );  // 1 for this call
    }

    return status;
}


VOID
IsmFreeSiteCostInfo(
    IN  HANDLE                     hIsm,
    IN  ISM_SITE_COST_INFO_ARRAY  *rgSiteCostInfo
    )
/*++

Routine Description:

    Frees the structure returned by ISM_QUERY_SITES_BY_COST().

Arguments:

    hIsm (IN) - Handle returned by a prior call to IsmStartup().

    rgSiteCostInfo (IN) - Structure to free.

Return Values:

    None.

--*/
{
    DPRINT( 2, "IsmFreeSiteCostInfo\n" );
    RouteFreeSiteCostInfo( rgSiteCostInfo );
}