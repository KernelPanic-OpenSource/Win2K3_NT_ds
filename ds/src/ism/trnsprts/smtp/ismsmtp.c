/*++

Copyright (c) 1998 Microsoft Corporation.
All rights reserved.

MODULE NAME:

    imssmtp.c

ABSTRACT:

    This module is a plug-in DLL for the Inter-Site Messaging service, which is
    part of the mail-based replication subsystem in the Directory Service.

    This DLL, as is any instance of the ISM plug-in DLL class, provides a set
    of abstract transport functions, such as send, receive, and so on.  See
    ismapi.h for details.

    This implementation is based on e-mail, using the SMTP protocol.

DETAILS:

CREATED:

    1/28/98     Jeff Parham (jeffparh)
                (Largely copied from WLees's ismip.c.)

REVISION HISTORY:

--*/

#include <ntdspch.h>

#include <ismapi.h>
#include <debug.h>
#include <ntdsa.h>   // Option flags

// TODO: better place to put these?
typedef ULONG MessageId;
typedef ULONG ATTRTYP;
#include <dsevent.h>

#include <fileno.h>
#define  FILENO FILENO_ISMSERV_ISMSMTP

#include "common.h"
#include "ismsmtp.h"

// Needed by dscommon.lib.
DWORD ImpersonateAnyClient(   void ) { return ERROR_CANNOT_IMPERSONATE; }
VOID  UnImpersonateAnyClient( void ) { ; }

#define DEBSUB "ISMSMTP:"

// Here is a way to enable debugging dynamically
// Run ismserv.exe under ntsd
// break in to the ismserv process
// !dsexts.dprint /m:ismsmtp level 4
// !dsexts.dprint /m:ismsmtp add *

// Set this to 1 for unit test debug
#define UNIT_TEST_DEBUG 0


/* External */

// Event logging config (as exported from ismserv.exe).
DS_EVENT_CONFIG * gpDsEventConfig = NULL;

/* Static */

// Lock on instances list
CRITICAL_SECTION TransportListLock;
// Lock on drop directory
CRITICAL_SECTION DropDirectoryLock;
// Lock on queue directory
CRITICAL_SECTION QueueDirectoryLock;

// Note that aligned 32 bit access is naturally atomic. However, if you ever
// require any atomic test-and-set functionality, you must use Interlocked
// instructions.
// [Nickhar] Aligned 32-bit reads and writes are atomic. Increments and
// decrements, however, are not.
volatile DWORD gcRefCount = 0;
#define ENTER_CALL() (InterlockedIncrement(&gcRefCount))
#define EXIT_CALL() (InterlockedDecrement(&gcRefCount))
#define CALLS_IN_PROGRESS() (gcRefCount)
#define NO_CALLS_IN_PROGRESS() (gcRefCount == 0)

// List head of transport instances
LIST_ENTRY TransportListHead;

/* Forward */ /* Generated by Emacs 19.34.1 on Wed Nov 04 10:12:42 1998 */

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

void
IsmShutdown(
    IN  HANDLE          hIsm,
    IN  ISM_SHUTDOWN_REASON_CODE eReason
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

BOOL
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
    HANDLE hevLogging;

    switch (fdwReason )
    {
    case DLL_PROCESS_ATTACH:
    {

        // Debug library is already initialize in dll, ismserv.exe,
        // where the library is exported from.

        // Get event logging config (as exported from ismserv.exe).
        gpDsEventConfig = DsGetEventConfig();

#if DBG
#if UNIT_TEST_DEBUG
        DebugInfo.severity = 1;
        strcpy( DebugInfo.DebSubSystems, "ISMSMTP:XMITRECV:CDOSUPP:" ); 
//        DebugInfo.severity = 3;
//        strcpy( DebugInfo.DebSubSystems, "*" ); 
#endif
        DebugMemoryInitialize();
#endif
        
        if ( (ERROR_SUCCESS != InitializeCriticalSectionHelper( &TransportListLock ) ) ||
             (ERROR_SUCCESS != InitializeCriticalSectionHelper( &DropDirectoryLock ) ) ||
             (ERROR_SUCCESS != InitializeCriticalSectionHelper( &QueueDirectoryLock ) ) ) {
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
        DeleteCriticalSection( &DropDirectoryLock );
        DeleteCriticalSection( &QueueDirectoryLock );
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
    DWORD status, hr;
    BOOLEAN firsttime;
    BOOLEAN fSmtpInit = FALSE;
    BOOLEAN fNotifyInit = FALSE;

    DPRINT1( 1, "IsmStartup, transport='%ws'\n", pszTransportDN );

    MEMORY_CHECK_ALL();
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
    if( ERROR_SUCCESS!=status ) {
        FREE_TYPE( instance );
        LogUnhandledError( status );
        return status;
    }

    // ReplInterval is 0, meaning the application should take default

    // Default is schedules not significant, bridges not required (transitive)
    instance->Options = NTDSTRANSPORT_OPT_IGNORE_SCHEDULES;

    // INITIALIZE TRANSPORT INSTANCE HERE

    instance->Name = NEW_TYPE_ARRAY( (length + 1), WCHAR );
    if (instance->Name == NULL) {
        status = ERROR_NOT_ENOUGH_MEMORY;
        LogUnhandledError( status );
        goto cleanup;
    }
    wcscpy( instance->Name, pszTransportDN );

    MEMORY_CHECK_ALL();

    // ************************************************************************

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

    hr = SmtpInitialize( instance );
    if (FAILED(hr)) {
        status = hr;
        // Event already logged
        goto cleanup;
    }
    fSmtpInit = TRUE;

    MEMORY_CHECK_ALL();

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

    // Start a listener thread for this instance
    // Note, make sure instance fully initialized before starting this thread
    // Note, instance cannot be destroyed until thread is stopped
    instance->ListenerThreadHandle = (HANDLE)
        _beginthreadex(NULL, 0, SmtpRegistryNotifyThread, instance, 0,
                       &instance->ListenerThreadID);
    if (NULL == instance->ListenerThreadHandle) {
        status = _doserrno;
        Assert(status);
        DPRINT1(0, "Failed to create listener thread, error %d.\n", status);
        LogUnhandledError( status );
        goto cleanup;
    }

    InterlockedIncrement( &(instance->ReferenceCount) );  // 1 for the lifetime of this transport

    *phIsm = instance;

    MEMORY_CHECK_ALL();

    return ERROR_SUCCESS;

cleanup:

    instance->fShutdownInProgress = TRUE;

    if (fNotifyInit) {
        (void) DirEndNotifyThread( instance );
    }

    if (fSmtpInit) {
        (void) SmtpTerminate( instance, FALSE /* not removal */ );
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

    MEMORY_CHECK_ALL();

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

    eReason (in) - Reason for refresh

    pszObjectDN (IN) - DN of the Inter-Site-Transport object.  This is
        guaranteed to be the same as the DN passed in IsmStartup, as inter-site
        transport objects cannot be renamed.

Return Values:

    0 or Win32 error code.

--*/
{
    PTRANSPORT_INSTANCE instance = (PTRANSPORT_INSTANCE) hIsm;
    DWORD status, oldReplInterval, oldOptions;

    ENTER_CALL();

    DPRINT2( 1, "IsmRefresh, reason = %d, new name = %ws\n",
             eReason,
             pszObjectDN ? pszObjectDN : L"not supplied" );

    if (instance->Size != sizeof( TRANSPORT_INSTANCE )) {
        status = ERROR_INVALID_PARAMETER;
        goto cleanup;
    }

    if ( (eReason == ISM_REFRESH_REASON_RESERVED) ||
         (eReason >= ISM_REFRESH_REASON_MAX) ) {
        status = ERROR_INVALID_PARAMETER;
        goto cleanup;
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

    EXIT_CALL();

    return status;
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

    The local message is implementated as four segments:
    1. Length of service, 4 bytes
    2. Service name itself, terminated, unicode
    3. Length of data, 4 bytes
    4. Data itself

Arguments:

    hIsm (IN) - Handle returned by a prior call to IsmStartup().

    pszRemoteTransportAddress (IN) - Transport address of the destination
        server.

    pszServiceName (IN) - Name of the service on the remote machine that is the
        intended receiver of the message.

Return Values:

    0 or Win32 error code.

--*/
{
    PTRANSPORT_INSTANCE transport = (PTRANSPORT_INSTANCE) hIsm;
    DWORD status;

    ENTER_CALL();

    DPRINT2( 1, "IsmSend, address = %ws, service = %ws\n",
            pszRemoteTransportAddress, pszServiceName);

    // Validate arguments

    if ( (transport->Size != sizeof( TRANSPORT_INSTANCE )) ||
         (pszServiceName == NULL) ||
         (*pszServiceName == L'\0') ||
         (pMsg == NULL) ||
         (pMsg->cbData == 0) ||
         (pMsg->pbData == NULL) ) {
        status = ERROR_INVALID_PARAMETER;
        goto cleanup;
    }

    if (transport->fShutdownInProgress) {
        status = ERROR_SHUTDOWN_IN_PROGRESS;
        goto cleanup;
    }

    InterlockedIncrement( &(transport->ReferenceCount) );  // 1 for this call
    __try {
        status = SmtpSend(transport,
                          pszRemoteTransportAddress,
                          pszServiceName,
                          pMsg);
    } __finally {
        InterlockedDecrement( &(transport->ReferenceCount) );  // 1 for this call
    }

cleanup:

    EXIT_CALL();

    return status;
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

    This routine is pretty simple.  It finds the service, and dequeues a message
    if there is any.  Queue the message is done by the listener thread behind
    the scenes.

Arguments:

    hIsm (IN) - Handle returned by a prior call to IsmStartup().

    ppMsg (OUT) - On successful return, holds a pointer to the received message
        or NULL.

Return Values:

    0 or Win32 error code.

--*/
{
    DWORD status;
    PTRANSPORT_INSTANCE transport = (PTRANSPORT_INSTANCE) hIsm;

    ENTER_CALL();

    DPRINT1( 2, "IsmReceive, service name = %ws\n", pszServiceName );

    // Validate arguments

    if (transport->Size != sizeof( TRANSPORT_INSTANCE )) {
        status = ERROR_INVALID_PARAMETER;
        goto cleanup;
    }

    if (ppMsg == NULL) {
        status = ERROR_INVALID_PARAMETER;
        goto cleanup;
    }

    if (transport->fShutdownInProgress) {
        status = ERROR_SHUTDOWN_IN_PROGRESS;
        goto cleanup;
    }

    InterlockedIncrement( &(transport->ReferenceCount) );  // 1 for this call
    __try {
        status = SmtpReceive(transport, pszServiceName, ppMsg);
    } __finally {
        InterlockedDecrement( &(transport->ReferenceCount) );  // 1 for this call
    }

    DPRINT1( 2, "IsmReceive, size = %d\n", *ppMsg ? (*ppMsg)->cbData : 0 );

cleanup:

    EXIT_CALL();

    return status;
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
    PTRANSPORT_INSTANCE instance = (PTRANSPORT_INSTANCE) hIsm;

    ENTER_CALL();

    DPRINT1( 1, "IsmFreeMsg, size = %d\n", pMsg->cbData );

    // Validate arguments

    if ( (instance != NULL) &&
         (instance->Size != sizeof( TRANSPORT_INSTANCE )) ) {
        // error: invalid parameter
        Assert( FALSE );
        goto cleanup;
    }

    SmtpFreeMessage( pMsg );

cleanup:

    EXIT_CALL();

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

    The transport has associated with it some lingering state.  The matrix of
    schedules is notfreed at the end of this routine.  It remains, tied to
    the transport handle, for the benefit of the GetConnectionSchedule api.

    There is no time-based caching of this information.  Each time this routine
    is called, theinformation is regnerated.  GetConnectionSchedule api uses the
    matrix of schedules from the last time this call was made, regardless of
    time.

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

    DPRINT( 1, "IsmGetConnectivity\n" );

    ENTER_CALL();
    InterlockedIncrement( &(transport->ReferenceCount) );  // 1 for this call

    __try {
        // Validate
        if (transport->Size != sizeof( TRANSPORT_INSTANCE )) {
            status = ERROR_INVALID_PARAMETER;
            __leave;
        }

        if (transport->fShutdownInProgress) {
            status = ERROR_SHUTDOWN_IN_PROGRESS;
            __leave;
        }

        // Get the site list and connectivity matrix

        status = RouteGetConnectivity( transport,
                                       &numberSites,
                                       &pSiteList,
                                       &pLinkArray,
                                       transport->Options,
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
        EXIT_CALL();
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

    ENTER_CALL();

    DPRINT( 1, "IsmFreeConnectivity\n" );

    // Validate

    if (transport->Size != sizeof( TRANSPORT_INSTANCE )) {
        // error: invalid parameter
        goto cleanup;
    }

    if (pConnectivity == NULL) {
        goto cleanup;
    }

    // Free individual components

    if (pConnectivity->cNumSites > 0) {
        DirFreeSiteList( pConnectivity->cNumSites, pConnectivity->ppSiteDNs );

        RouteFreeLinkArray( transport, pConnectivity->pLinkValues );
    }

    FREE_TYPE( pConnectivity );

cleanup:

    EXIT_CALL();
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

    On successful return of a non-NULL list, the ISM service will eventually
    call IsmFreeTransportServers(hIsm, *ppServerList);

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

    DPRINT1( 1, "IsmGetTransportServers, site = %ws\n", pszSiteDN );

    ENTER_CALL();
    InterlockedIncrement( &(transport->ReferenceCount) );  // 1 for this call

    __try {
        // Validate

        if (transport->Size != sizeof( TRANSPORT_INSTANCE )) {
            status = ERROR_INVALID_PARAMETER;
            __leave;
        }

        if (transport->fShutdownInProgress) {
            status = ERROR_SHUTDOWN_IN_PROGRESS;
            __leave;
        }

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
        EXIT_CALL();

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

    ENTER_CALL();

    DPRINT( 1, "IsmFreeTransportServers\n" );

    // Validate

    if (transport->Size != sizeof( TRANSPORT_INSTANCE )) {
// error: invalid parameter
        goto cleanup;
    }

    if (pServerList == NULL) {
        goto cleanup;
    }

    // Free individual components

    if ( (pServerList->cNumServers != 0) && (pServerList->ppServerDNs != NULL) ) {
        for( i = 0; i < pServerList->cNumServers; i++ ) {
            FREE_TYPE( pServerList->ppServerDNs[i] );
        }
        FREE_TYPE( pServerList->ppServerDNs );
    }

    FREE_TYPE( pServerList );

cleanup:

    EXIT_CALL();
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

    The transport has associated with it some lingering state.  The matrix of
    schedules is not freed at the end of this routine.  It remains, tied to the
    transport handle, for the benefit of the GetConnectionSchedule api.

    There is no time-based caching of this information.  Each time the get conn
    routine is called, the information is regnerated.  GetConnectionSchedule api
    uses the matrix of schedules from the last time this call was made,
    regardless of time.

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

    ENTER_CALL();

    DPRINT2( 1, "IsmGetConnectionSchedule, site1 = %ws, site2 = %ws\n",
            pszSite1DN, pszSite2DN );

    // Validate

    if (transport->Size != sizeof( TRANSPORT_INSTANCE )) {
        status = ERROR_INVALID_PARAMETER;
        goto cleanup;
    }

    if (transport->fShutdownInProgress) {
        status = ERROR_SHUTDOWN_IN_PROGRESS;
        goto cleanup;
    }

    status = RouteGetPathSchedule( transport,
                                     pszSite1DN,
                                     pszSite2DN,
                                     &pSchedule,
                                     &length );
    if (status != ERROR_SUCCESS) {
        goto cleanup;
    }

    if (pSchedule == NULL) {
        *ppSchedule = NULL; // always connected
    } else {
        *ppSchedule = NEW_TYPE( ISM_SCHEDULE );
        if (*ppSchedule == NULL) {
            FREE_TYPE( pSchedule );
            status = ERROR_NOT_ENOUGH_MEMORY;
            goto cleanup;
        }
        (*ppSchedule)->cbSchedule = length;
        (*ppSchedule)->pbSchedule = pSchedule;
    }

    status = ERROR_SUCCESS;
cleanup:

    EXIT_CALL();

    return status;
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

    ENTER_CALL();

    DPRINT( 1, "IsmFreeConnectionSchedule\n" );

    // Validate

    if (transport->Size != sizeof( TRANSPORT_INSTANCE )) {
// error: invalid parameter
        goto cleanup;
    }

    if (pSchedule == NULL) {
        goto cleanup;
    }

    Assert( pSchedule->cbSchedule != 0 );
    Assert( pSchedule->pbSchedule );

    FREE_TYPE( pSchedule->pbSchedule );

    pSchedule->pbSchedule = NULL;
    pSchedule->cbSchedule = 0;

    FREE_TYPE( pSchedule );

cleanup:

    EXIT_CALL();
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
    DWORD               status;
    DWORD               waitStatus;
    PTRANSPORT_INSTANCE transport = (PTRANSPORT_INSTANCE) hIsm;
    LONG count;

    DPRINT1( 1, "IsmShutdown, Reason %d\n", eReason );

    // Validate

    if (transport->Size != sizeof( TRANSPORT_INSTANCE )) {
        // error: invalid parameter
        return;
    }

    transport->fShutdownInProgress = TRUE;

    SetEvent(transport->hShutdownEvent);

    // Shutdown the listener thread
    // Note, shut this down before invalidating the instance

    waitStatus = WaitForSingleObject(transport->ListenerThreadHandle, 5*1000);
    if ( (WAIT_OBJECT_0 != waitStatus) &&
         (WAIT_TIMEOUT != waitStatus) ) {
        status = GetLastError();
        DPRINT3(0, "Shutdown failed, wait status=%d, GLE=%d, TID=0x%x.\n",
                waitStatus, status, transport->ListenerThreadID);
    }

    // If calls in progress, give them a chance to finish
    // The service and rpc should guarantee that this entrypoint is
    // not called while calls are in progress. I suspect this is not
    // always true.  Be defensive.

    if (CALLS_IN_PROGRESS()) {
        Sleep( 5 * 1000 );
    }

    // Remove this instance from the list.
    EnterCriticalSection(&TransportListLock);
    __try {
        RemoveEntryList( &(transport->ListEntry) );
    }
    __finally {
        LeaveCriticalSection(&TransportListLock);
    }

    // Release any routing state
    RouteFreeState( transport );

    DeleteCriticalSection( &(transport->Lock) );

    if (NO_CALLS_IN_PROGRESS()) {
        // Clean up only if all calls complete
        (void) SmtpTerminate( transport, (eReason == ISM_SHUTDOWN_REASON_REMOVAL) );
    } else {
        Assert( FALSE && "calls in progress did not exit" );
    }

    DirEndNotifyThread( transport );

    count = InterlockedDecrement( &(transport->ReferenceCount) );  // 1 for the lifetime of this transport

    if (count == 0) {
        // Clean up only if thread is finished

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

    ENTER_CALL();

    DPRINT2( 0, "IsmQuerySitesByCost (FromSite=%ls, cToSites=%d)\n",
        pszFromSite, cToSites );

    // Validate
    if (transport->Size != sizeof( TRANSPORT_INSTANCE )) {
        status = ERROR_INVALID_PARAMETER;
        goto Cleanup;
    }

    if (transport->fShutdownInProgress) {
        status = ERROR_SHUTDOWN_IN_PROGRESS;
        goto Cleanup;
    }

    status = RouteQuerySitesByCost( transport, pszFromSite,
                cToSites, rgszToSites, dwFlags, prgSiteInfo );
    if (status != ERROR_SUCCESS) {
        DPRINT1( 0, "Failed to get query sites, error %d\n", status );
    } else {
        DPRINT( 0, "RouteQuerySitesByCost succeeded\n" );
    }

Cleanup:
    
    EXIT_CALL();
    
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
    ENTER_CALL();
    RouteFreeSiteCostInfo( rgSiteCostInfo );
    EXIT_CALL();
}