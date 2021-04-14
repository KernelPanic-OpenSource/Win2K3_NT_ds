//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//
//  Copyright (C) Microsoft Corporation, 1987 - 1999
//
//  File:       drsuapi.c
//
//--------------------------------------------------------------------------

/*++

ABSTRACT:

    Wrappers for DRS interface RPC client functions.  Historically the DRS
    interface was used only for installation and replication, though for NT
    its responsibilities have grown to cover other DS-to-DS and arbitrary
    client-to-DS communication.

DETAILS:

CREATED:

REVISION HISTORY:

    Greg Johnson (gregjohn) 2/28/01
        RPC cancel functions added.  In order to use the RPC cancel call
        functions each thread wishing to set a specific time to cancel must
        have it's own copy of the context handle.  This is simply a copy of
        the handle however, so only when all copies are no longer in use
        may the handle be freed.
        
        To reduce contention and network serialization instead of freeing a 
        handle when it is no longer used, it is placed on a queue.  Later, 
        a dedicated thread frees the handles on the queue.

    Will Lees (wlees) 11/18/02
        The binding cache does not have a negative cache. Part of the reason for this
        is the multiple levels of retry that may occur. Binding disconnects and rebinds
        are a normal part of the negotiation process. FBindSzDrs() is called multiple
        times in a loop depending on the call completion logic in drsIsCallComplete.
        Even after exhausting all attempts at rebind, it may not be safe to conclude that
        all binds on all NCs will fail. For example, we use a different SPN depending on
        whether we are binding to a domain or not. Also, we must be extremely careful
        not to block a sync on another NC that might correct the reason we cannot bind.
        We felt it safest to put binding suppression at the replication queue layer, and
        this layer only returns an indicator whether a binding coudl be obtained
        (see pfBindSuccess).

--*/

#include <NTDSpch.h>
#pragma hdrstop

#include <ntdsa.h>
#include <drs.h>
#include "dsevent.h"
#include "mdcodes.h"
#include "dsexcept.h"
#include "drserr.h"
#include <heurist.h>
#include "bhcache.h"
#include "mdglobal.h"   // for eServiceShutdown
#include "anchor.h"     // for gAnchor
#include "scache.h"     // reqd. for mdlocal.h
#include "dbglobal.h"   // reqd. for mdlocal.h
#include "mdlocal.h"    // for DsaIsInstalling()
#include <drautil.h>
#include <dramail.h>
#include <dsatools.h>
#include "objids.h"     // Defines for selected classes and atts
#include <dsconfig.h>   // GetConfigParam
#include <mdcodes.h>    // Event codes
#include <winsock2.h>   // inet_addr
#include <pek.h>
#include <dsutil.h>     // MAP_SECURITY_PACKAGE_ERROR
#include <ntdsctr.h>    // perf counters
#include <dstaskq.h>
#include <dnsresl.h>    // for GetIpAddrByDnsNameW
#include <drarpc.h>
#include <kerberos.h>   // for purging kerberos cache
#include "gcverify.h"   // for FindDC/InvalidateGC

#include   "debug.h"         /* standard debugging header */
#define DEBSUB     "DRSUAPI:" /* define the subsystem for debugging */

#include "drsuapi.h"
extern RPC_IF_HANDLE _drsuapi_ClientIfHandle;

#include <fileno.h>
#define  FILENO FILENO_DRSUAPI

#define GCSpnType       L"GC"

// Does the SPN specify the domain name that the target account is in (by
// appending @domain.com)?
#define IS_DOMAIN_QUALIFIED_SPN(pszSpn) (NULL != wcschr((pszSpn), L'@'))
#define GET_DOMAIN_FROM_QUALIFIED_SPN(pszSpn) (wcschr((pszSpn), L'@')+1)

void
DRSExpireContextHandles(
    IN  VOID *  pvArg,
    OUT VOID ** ppvNextArg,
    OUT DWORD * pcSecsUntilNextRun
    );

// Time limit (in minutes) of various RPC operations.
ULONG gulDrsRpcBindTimeoutInMins;
ULONG gulDrsRpcReplicationTimeoutInMins;
ULONG gulDrsRpcGcLookupTimeoutInMins;
ULONG gulDrsRpcMoveObjectTimeoutInMins;
ULONG gulDrsRpcNT4ChangeLogTimeoutInMins;
ULONG gulDrsRpcObjectExistenceTimeoutInMins;
ULONG gulDrsRpcGetReplInfoTimeoutInMins;

// How long do we keep DRS context handles in the cache?  (in sec)
ULONG gulDrsCtxHandleLifetimeIntrasite;
ULONG gulDrsCtxHandleLifetimeIntersite;

// How often do we check to see if context handles have expired? (in sec)
ULONG gulDrsCtxHandleExpiryCheckInterval;

// Do we want a different RPC association than the default?
BOOL gfUseUniqueAssoc = FALSE;

// FBindSzDRS flags

#define FBINDSZDRS_NO_CACHED_HANDLES (1)
#define FBINDSZDRS_LOCK_HANDLE       (2)
#define FBINDSZDRS_CRYPTO_BIND       (4)

// Tracks DRS context handle state.  Returned by FBindSzDRS() and freed by
// DRSFreeContextInfo().

typedef struct _DRS_CONTEXT_INFO {
    LPWSTR     pszServerName;          // Net name of the server.
    DRS_HANDLE hDrs;                   // The DRS context handle.
    unsigned   fIsHandleFromCache : 1; // Did we reuse an already cached handle?
    unsigned   fIsHandleInCache   : 1; // Is this handle currently in the cache
                                       //   (i.e., was already there or we got a
                                       //   new one and added it to the cache)?
    union {                            // The server's extensions set.
        BYTE            rgbExt[CURR_MAX_DRS_EXT_STRUCT_SIZE];
        DRS_EXTENSIONS  ext;
    };
    ULONG      ulFlags;                // flags used to get this context
    HANDLE *   phThread;               // thread handle of thread using this context handle
    DWORD      dwThreadId;             // thread id
    DWORD      dwBindingTimeoutMins;   // Cancellation time, if set
    DSTIME     dstimeCreated;          // Time created
    DRS_CONTEXT_CALL_TYPE eCallType;   // Call type
    LIST_ENTRY ListEntry;
} DRS_CONTEXT_INFO;

typedef struct _DRS_HANDLE_LIST_ELEM {
    LPWSTR     pszServerName;          // Net name of the server
    DRS_HANDLE hDrs;                   // The DRS context handle.
    LIST_ENTRY ListEntry;
} DRS_HANDLE_LIST_ELEM;


// Has this library been initialized?
BOOL gfIsDrsClientLibInitialized = FALSE;

// List of outstanding async RPC calls.
LIST_ENTRY gDrsAsyncRpcList;
RTL_CRITICAL_SECTION gcsDrsAsyncRpcListLock;

// List of outstanding sync RPC handles - uses DRS_CONTEXT_INFO
// This list's sole purpose is to enable the cancelation of 
// all sync RPC calls if a shutdown is intiated.
LIST_ENTRY gDrsRpcServerCtxList;
CRITICAL_SECTION gcsDrsRpcServerCtxList;
BOOL gfDrsRpcServerCtxListInitialized = FALSE;
DWORD gcNumDrsRpcServerCtxListEntries = 0;

// List of sync RPC handles to free - uses DRS_HANDLE_LIST_ELEM
// In order to decrease contention between threads, RPC handles
// to be freed are placed on this list to be freed later by
// a thread dedicated to the task.
LIST_ENTRY gDrsRpcFreeHandleList;
CRITICAL_SECTION gcsDrsRpcFreeHandleList;
BOOL gfDrsRpcFreeHandleListInitialized = FALSE;
DWORD gcNumDrsRpcFreeHandleListEntries = 0;

// Table of timeout information for async rpc calls
struct {
    DRS_CALL_TYPE   CallType;
    DWORD           dwMsgID;
    ULONG *         pcNumMinutesUntilRpcTimeout;
} rgCallTypeTable[] = {
    {DRS_ASYNC_CALL_NONE,        0,                      NULL},   // placeholder
    {DRS_ASYNC_CALL_GET_CHANGES, DIRLOG_RPC_CALL_GETCHG, &gulDrsRpcReplicationTimeoutInMins},
};

BOOL
getContextBindingHelper(
    IN      THSTATE *           pTHS,
    IN      LPWSTR              szServer,
    IN      LPWSTR              pszDomainName,  OPTIONAL
    IN      ULONG               ulFlags,
    IN OUT  DRS_CONTEXT_INFO ** ppContextInfo,
    IN OUT  DWORD *             pdwStatus
    );

void
DRSFreeContextInfo(
    IN      THSTATE           * pTHS,
    IN OUT  DRS_CONTEXT_INFO ** ppContextInfo
    );

//
// Credential manangement routines
//
//
CRITICAL_SECTION            csCredentials;
PSEC_WINNT_AUTH_IDENTITY_W  gCredentials = NULL;
UCHAR                       gCredentialSeed = 0;
HANDLE                      gInstallClientToken = 0;

VOID
DrspFreeCredentials(
    PSEC_WINNT_AUTH_IDENTITY_W pCred
    )
{
    Assert(pCred);

    free(pCred->User);
    free(pCred->Domain);
    free(pCred->Password);
    free(pCred);
}

DWORD
DRSSetCredentials(
    IN HANDLE ClientToken,
    IN WCHAR *User,
    IN WCHAR *Domain,
    IN WCHAR *Password,
    IN ULONG  PasswordLength
    )
//
// This function translates the parameters into a RPC_AUTH_IDENTITY_HANDLE
// that can be used when establishing an RPC connection
//
{

    ULONG                      ret;
    PSEC_WINNT_AUTH_IDENTITY_W pNewCred = NULL;
    PSEC_WINNT_AUTH_IDENTITY_W pOldCred = NULL;
    ULONG                      Length;
    WCHAR                      *wsUser = NULL, *wsDomain = NULL, *wsPassword = NULL;
    BOOL                       fInCriticalSection = FALSE;
    UNICODE_STRING             EPassword;

    if ( !gfIsDrsClientLibInitialized )
    {
        DRSClientCacheInit();
    }

    __try
    {

        if (!User && !Domain && !Password) {
            //
            // Set the credentials to NULL
            //
            EnterCriticalSection( &csCredentials );
            fInCriticalSection = TRUE;

            pOldCred =  gCredentials;
            gCredentials = NULL;

            ret = DRAERR_Success;

            __leave;

        }

        if (!User || !Domain || !Password) {
            ret = DRAERR_InvalidParameter;
            __leave;
        }

        //
        // At this point we have a new set of acceptable credentials
        //
        pNewCred = (PSEC_WINNT_AUTH_IDENTITY_W) malloc(sizeof(SEC_WINNT_AUTH_IDENTITY_W));
        if (!pNewCred) {
            ret = DRAERR_OutOfMem;
            __leave;
        }
        memset(pNewCred, 0, sizeof(SEC_WINNT_AUTH_IDENTITY));

        Length = wcslen(User);
        wsUser = (WCHAR*) malloc((Length+1)*sizeof(WCHAR));
        if (!wsUser) {
            ret = DRAERR_OutOfMem;
            __leave;
        }
        memset(wsUser, 0, (Length+1)*sizeof(WCHAR));
        wcscpy(wsUser, User);
        pNewCred->UserLength = Length;
        pNewCred->User       = wsUser;

        Length = wcslen(Domain);
        wsDomain = (WCHAR*) malloc((Length+1)*sizeof(WCHAR));
        if (!wsDomain) {
            ret = DRAERR_OutOfMem;
            __leave;
        }
        memset(wsDomain, 0, (Length+1)*sizeof(WCHAR));
        wcscpy(wsDomain, Domain);
        pNewCred->DomainLength = Length;
        pNewCred->Domain       = wsDomain;

        Length = PasswordLength;
        wsPassword = (WCHAR*) malloc((Length+1)*sizeof(WCHAR));
        if (!wsPassword) {
            ret = DRAERR_OutOfMem;
            __leave;
        }
        memset(wsPassword, 0, (Length+1)*sizeof(WCHAR));
        memcpy(wsPassword, Password, (Length)*sizeof(WCHAR));
        pNewCred->PasswordLength = Length;
        pNewCred->Password       = wsPassword;

        pNewCred->Flags = SEC_WINNT_AUTH_IDENTITY_UNICODE;

        //
        // Set the global Credentials
        //
        EnterCriticalSection( &csCredentials );
        fInCriticalSection  = TRUE;

        //
        // Encrypt the password
        //
        RtlInitUnicodeString( &EPassword, pNewCred->Password );
        RtlRunEncodeUnicodeString( &gCredentialSeed, &EPassword );

        pOldCred =  gCredentials;
        gCredentials = pNewCred;
        gInstallClientToken = ClientToken;

        ret = DRAERR_Success;

    }
    __finally
    {
        if (fInCriticalSection) {
            LeaveCriticalSection( &csCredentials );
        }

        if (ret != DRAERR_Success) {

            if (pNewCred) {
                free(pNewCred);
            }
            if (wsUser) {
                free(wsUser);
            }
            if (wsDomain) {
                free(wsDomain);
            }
            if (wsPassword) {
                free(wsPassword);
            }
        }

        if (pOldCred) {
            DrspFreeCredentials(pOldCred);
        }

    }

    return ret;
}

DWORD
DRSImpersonateInstallClient(
    OUT BOOL *pfImpersonate
    )
//
// This routine impersonates the global client token if one
// is present.  This is only necessary during install.
//
// gInstallClientToken will be set during installation.
//
{
    *pfImpersonate = FALSE;
    if (gInstallClientToken) {
        *pfImpersonate = ImpersonateLoggedOnUser(gInstallClientToken);
        if (!(*pfImpersonate)) {
            return GetLastError();
        }
    }
    return ERROR_SUCCESS;
}

ULONG
DrspGetCredentials(
    OUT PSEC_WINNT_AUTH_IDENTITY_W *ppCred
    )
//
// This routine returns the globally stored credentials to
// perform a DsBind with.  This is only necessary during install.
//
{

    ULONG                      ret;
    PSEC_WINNT_AUTH_IDENTITY_W pNewCred = NULL;
    PSEC_WINNT_AUTH_IDENTITY_W pOldCred;
    ULONG                      Length;
    WCHAR                      *wsUser = NULL, *wsDomain = NULL, *wsPassword = NULL;
    UNICODE_STRING             EPassword;

    EnterCriticalSection( &csCredentials );
    __try
    {
        //
        // If there are no credentials, bail out now
        //
        if (!gCredentials) {
            *ppCred = NULL;
            ret = DRAERR_Success;
            __leave;
        }

        pNewCred = (PSEC_WINNT_AUTH_IDENTITY_W) malloc(sizeof(SEC_WINNT_AUTH_IDENTITY_W));
        if (!pNewCred) {
            ret = DRAERR_OutOfMem;
            __leave;
        }
        memset(pNewCred, 0, sizeof(SEC_WINNT_AUTH_IDENTITY));

        Length = wcslen(gCredentials->User);
        wsUser = (WCHAR*) malloc((Length+1)*sizeof(WCHAR));
        if (!wsUser) {
            ret = DRAERR_OutOfMem;
            __leave;
        }
        memset(wsUser, 0, (Length+1)*sizeof(WCHAR));
        wcscpy(wsUser, gCredentials->User);
        pNewCred->UserLength = Length;
        pNewCred->User       = wsUser;

        Length = wcslen(gCredentials->Domain);
        wsDomain = (WCHAR*) malloc((Length+1)*sizeof(WCHAR));
        if (!wsDomain) {
            ret = DRAERR_OutOfMem;
            __leave;
        }
        memset(wsDomain, 0, (Length+1)*sizeof(WCHAR));
        wcscpy(wsDomain, gCredentials->Domain);
        pNewCred->DomainLength = Length;
        pNewCred->Domain       = wsDomain;

        Length = gCredentials->PasswordLength;
        wsPassword = (WCHAR*) malloc((Length+1)*sizeof(WCHAR));
        if (!wsPassword) {
            ret = DRAERR_OutOfMem;
            __leave;
        }
        memset(wsPassword, 0, (Length+1)*sizeof(WCHAR));
        memcpy(wsPassword, gCredentials->Password, (Length)*sizeof(WCHAR));
        pNewCred->PasswordLength = Length;
        pNewCred->Password       = wsPassword;

        //
        // Unencrypt the password
        //
        RtlInitUnicodeString( &EPassword, pNewCred->Password );
        RtlRunDecodeUnicodeString( gCredentialSeed, &EPassword );

        pNewCred->Flags = gCredentials->Flags;

        //
        // Return the copy of the credentials
        //
        *ppCred =  pNewCred;

        ret = DRAERR_Success;

    }
    __finally
    {
        if (ret != DRAERR_Success) {

            if (pNewCred) {
                free(pNewCred);
            }
            if (wsUser) {
                free(wsUser);
            }
            if (wsDomain) {
                free(wsDomain);
            }
            if (wsPassword) {
                free(wsPassword);
            }
        }

        LeaveCriticalSection( &csCredentials );

    }

    return ret;

}

VOID
DRSSetRpcCancelTime(
    IN      ULONG               MinutesTillCancel,
    IN OUT  DRS_HANDLE *        phDrs
    )
{
    RPC_BINDING_HANDLE hRpc;
    ULONG rpcstatus = RPC_S_OK;

    // get the binding handle.  (RPC says not to free this handle)
    rpcstatus = RpcSsGetContextBinding(*phDrs, &hRpc);
    if (rpcstatus!=RPC_S_OK) {
        DRA_EXCEPT(rpcstatus,0); 
    }

    // set the cancel time in milliseconds
    rpcstatus = RpcBindingSetOption(hRpc, RPC_C_OPT_CALL_TIMEOUT, MinutesTillCancel*60*1000); 
    if (rpcstatus!=RPC_S_OK) {
        DRA_EXCEPT(rpcstatus,0); 
    }
}

VOID
DRSSetContextCancelTime(
    IN      DRS_CONTEXT_CALL_TYPE eCallType,
    IN      ULONG               MinutesTillCancel,
    IN      DRS_CONTEXT_INFO   *pContextInfo
    )
{
    DRSSetRpcCancelTime(MinutesTillCancel, &pContextInfo->hDrs );  
    pContextInfo->eCallType = eCallType;
    pContextInfo->dwBindingTimeoutMins = MinutesTillCancel;
}

void
InitRpcSessionEncryption(
    THSTATE             *pTHS,
    ULONG               ulBindFlags,
    DRS_CONTEXT_INFO    *pContextInfo,
    RPC_BINDING_HANDLE  ExistingBindingHandle
    )
{
    RPC_STATUS          RpcStatus;
    RPC_BINDING_HANDLE  RpcBindingHandle;

    // Caller should have either binding handle or context, but not both.
    Assert(    ( pContextInfo && !ExistingBindingHandle)
            || (!pContextInfo &&  ExistingBindingHandle) );

    // Callers like GetNCChanges use "one shot" session encryption which
    // is not tied to the context handle.  Many GetNCChanges calls can use
    // the same cached handle and each gets a new session key on each call.
    // Callers like InterDomainMove are associating a session key with
    // the context handle and will use the same key on one or more subsequent
    // RPC calls.  Although I don't see any obvious conflict with the two
    // modes, for now we assert that context handle based session keys
    // require an exclusively owned handle.

    Assert(FBINDSZDRS_CRYPTO_BIND & ulBindFlags
                ? FBINDSZDRS_LOCK_HANDLE & ulBindFlags
                : TRUE);

    // Clear any existing key on the thread state.

    PEKClearSessionKeys(pTHS);

    // Register a notification call back with RPC.  This will call a call
    // back function telling us the security context used.  This security
    // context is then used to query the session key and that session key
    // is then set on the thread state. When the DRA subsequently reads
    // the database all "secret" data are then re-encrypted using the
    // session key.

    if ( ExistingBindingHandle )
    {
        RpcBindingHandle = ExistingBindingHandle;
        RpcStatus = RPC_S_OK;
    }
    else
    {
        RpcStatus = RpcSsGetContextBinding(
                                        pContextInfo->hDrs,
                                        &RpcBindingHandle);
    }

    if ( RPC_S_OK == RpcStatus )
    {
        RpcStatus = RpcBindingSetOption(
                                        RpcBindingHandle,
                                        RPC_C_OPT_SECURITY_CALLBACK,
                                        (ULONG_PTR) PEKSecurityCallback);

        // After checking with RPC folks should not free
        // the binding handle returned by this function
    }

    if ( RPC_S_OK != RpcStatus )
    {
        // Could not establish session key - raise exception

        RaiseDsaExcept( DSA_CRYPTO_EXCEPTION,
                        RpcStatus,
                        0,
                        DSID(FILENO,__LINE__),
                        DS_EVENT_SEV_MINIMAL);
    }
}

CRITICAL_SECTION csBHCache;

// Uninitialized elements are default initialized to zero.
static BHCacheElement NullBHCacheElement = { 0 };

BHCacheElement rgBHCache[BHCacheSize];

void DRSClientCacheInit(void)
{
    static BOOL fAreCritSecsInitialized = FALSE;

    if (!fAreCritSecsInitialized)
    {
        //
        // This has nothing to do with the client cache, but is put in this
        // in this function to be initialized when starting up
        //
        __try {
            InitializeCriticalSection( &csCredentials );

            InitializeCriticalSection( &csBHCache );

            fAreCritSecsInitialized = TRUE;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            fAreCritSecsInitialized = FALSE;
        }

        if(!fAreCritSecsInitialized) {
            DsaExcept(DSA_MEM_EXCEPTION, 0, 0);
        }
    }

    memset(rgBHCache, 0, sizeof(rgBHCache));

    if (gfTaskSchedulerInitialized          // not so in DCPROMO
        && !gfDisableBackgroundTasks) {     // not doing performance testing
        InsertInTaskQueue(TQ_DRSExpireContextHandles,
                          NULL,
                          gulDrsCtxHandleExpiryCheckInterval);
    }

    gfIsDrsClientLibInitialized = TRUE;
}


ULONG
DRSHandleRpcClientException(
    IN  EXCEPTION_POINTERS *  pExceptPtrs,
    IN  LPWSTR                pszServerName,
    IN  DRS_CONTEXT_INFO *    pContextInfo      OPTIONAL,
    IN  ULONG                 ulRpcCallTimeoutMins,
    OUT ULONG *               pulErrorCode
    )
/*++

Routine Description:

    Handle RPC exceptions generated while making DRS client calls using an
    already bound DRS context handle acquired via a prior call to FBindSzDRS().
    Called as part of an __except wrapping an _IDL_DRS* call.

Arguments:

    pExceptPtrs (IN) - Exception data returned by GetExceptionInformation(),
        as retrieved in the __except.

    pszServerName (IN) - Server to which we're making the RPC call.
    
    pContextInfo (IN, OPTIONAL) - Context info associated with current binding
        to pszServerName.

    pulErrorCode (OUT) - Exception code for caller to use

Return Values:

    EXCEPTION_EXECUTE_HANDLER - Exception handled.
    EXCEPTION_CONTINUE_SEARCH - Exception not handled.

--*/
{
    ULONG   ulResult;
    ULONG   ulExceptCode;
    VOID *  pExceptAddr;
    ULONG   ulExtErrorCode;
    DWORD   dwWin32Error;
    THSTATE * pTHS = pTHStls;

    Assert( pulErrorCode );

    *pulErrorCode = ERROR_SUCCESS; // GetExceptData does not always set this

    ulResult = GetExceptionData(pExceptPtrs, &ulExceptCode, &pExceptAddr,
                                pulErrorCode, &ulExtErrorCode);

    // If there was not an error data parameter associated with the exception,
    // turn the exception code itself into something meaningful.  If the
    // exception looks like a NTSTATUS, convert to Win32.

    if (*pulErrorCode == ERROR_SUCCESS) {
        if ( (ulExceptCode & 0xf0000000) == 0xc0000000 ) {
            *pulErrorCode = RtlNtStatusToDosError( ulExceptCode );
        } else {
            *pulErrorCode = ulExceptCode;
        }
    }

    Assert( *pulErrorCode != RPC_S_OK );

    if (EXCEPTION_EXECUTE_HANDLER == ulResult) {
        // Derive Win32 error from exception data.
        switch (ulExceptCode) {
        case DRA_GEN_EXCEPTION:
            // DRA exceptions always have an acompanying Win32 error code.
            dwWin32Error = *pulErrorCode;
            break;

        case DSA_EXCEPTION:
        case DSA_MEM_EXCEPTION:
        case DSA_DB_EXCEPTION:
        case DSA_BAD_ARG_EXCEPTION:
        case DSA_CRYPTO_EXCEPTION:
            // These NTDSA exceptions don't specify Win32 counterparts.
            // Fall through and consider the Win32 error code to be the
            // exception code for purposes of determining which event to log.
        
        default:
            // System-generated exceptions (such as those generated by RPC)
            // use a Win32 error code as the exception code.
            dwWin32Error = ulExceptCode;
            break;
        }

        // Log the exception code (if logging is cranked high enough).
        switch (dwWin32Error) {
        case RPC_S_CALL_CANCELLED:
            if (!eServiceShutdown) { 
                LogEvent8(DS_EVENT_CAT_RPC_CLIENT,
                          DS_EVENT_SEV_ALWAYS,
                          DIRLOG_DRA_DISPATCHER_CANCELED,
                          szInsertHex(GetCurrentThreadId()),
                          szInsertWC(pszServerName),    
                          szInsertUL(ulRpcCallTimeoutMins),
                          szInsertHex((ULONG)(pExceptPtrs->ExceptionRecord->ExceptionInformation)[2]),
                          NULL,NULL,NULL,NULL); 
            } else {
                LogEvent(DS_EVENT_CAT_RPC_CLIENT,
                          DS_EVENT_SEV_MINIMAL,
                          DIRLOG_DRA_DISPATCHER_CANCELED_SHUTDOWN,
                          szInsertHex(GetCurrentThreadId()),
                          szInsertWC(pszServerName),
                          szInsertHex((ULONG)(pExceptPtrs->ExceptionRecord->ExceptionInformation)[2])); 
            }
            
            break;

        case ERROR_DS_DIFFERENT_REPL_EPOCHS:
            // Only NULL on binds, and this error is not generated on binds.
            Assert(NULL != pContextInfo);

            DPRINT3(0, "RPC to %ls denied - replication epoch mismatch (remote %d, local %d).\n",
                    pszServerName,
                    REPL_EPOCH_FROM_DRS_EXT(&pContextInfo->ext),
                    gAnchor.pLocalDRSExtensions->dwReplEpoch);
            
            LogEvent(DS_EVENT_CAT_RPC_CLIENT,
                     DS_EVENT_SEV_ALWAYS,
                     DIRLOG_REPL_EPOCH_MISMATCH_COMMUNICATION_REJECTED,
                     szInsertWC(pszServerName),
                     pContextInfo
                        ? szInsertUL(REPL_EPOCH_FROM_DRS_EXT(&pContextInfo->ext))
                        : szInsertSz(""),
                     szInsertUL(gAnchor.pLocalDRSExtensions->dwReplEpoch));
            break;
        
        default:
            // uncaught rpc error, let the default logging suffice here 
            break;
        }

        LogRpcExtendedErrorInfo(pTHS, dwWin32Error, pszServerName, (ULONG)(pExceptPtrs->ExceptionRecord->ExceptionInformation)[2]);
    }

    return ulResult;
}

void
DRSFreeContextHandle(
    IN      LPWSTR          pszServerName,
    IN OUT  DRS_HANDLE *    phDrs
    )
/*++

Routine Description:

    Free a given DRS_HANDLE (a context handle for the DRS interface).

    During normal operation we call the server to unbind, which frees both its
    associated resources and our own resources.  During shutdown, we must avoid
    going off-machine, so we free only our own resources and rely on the
    server's context handle rundown routine to free its resources.  

Arguments:

    pszServerName (IN) - The server to which the context handle corresponds.
        Used for logging if there are any complications.

    phDrs (IN/OUT) - on input, holds the client handle to free.  Reset to
        NULL on success.

Return Values:

    None.

--*/
{
    ULONG ulErrorCode;

    if (NULL != *phDrs) {
        BOOL fFreed = FALSE; 

        if (!eServiceShutdown) {
            __try {
                ULONG ret = RPC_S_OK; 
                DRSSetRpcCancelTime(gulDrsRpcBindTimeoutInMins, phDrs);
                _IDL_DRSUnbind(phDrs);   
                fFreed = TRUE;
            }
            __except (DRSHandleRpcClientException(GetExceptionInformation(),
                                                  pszServerName,
                                                  NULL,
                                                  gulDrsRpcBindTimeoutInMins,
                                                  &ulErrorCode)) {
                ;
            }
        }

        if (!fFreed) {
            // We're either shutting down or the unbind attempt failed;  the
            // remote resources will have to be freed by the context handle
            // rundown routine.  Free our local resources.
            RpcSsDestroyClientContext(phDrs);
        }
    }
}

void
DRSInsertFreeHandleQueue(
    IN      LPWSTR          pszServerName,
    IN      DRS_HANDLE      hDrs
    )
/*++

Routine Description:

    The purpose of this routine is to reduce contention between threads.  Using this
    queue of handles to be freed, only a single thread attempts to free handles, and only
    while not holding any crit. sec.  
    

Arguments:

    pszServerName - the name of the server which to free the handle from
    hDrs - the handle to free

Return Values:

    None.

--*/
{
    if (NULL != hDrs) {
        DRS_HANDLE_LIST_ELEM * pInsHandle;
        ULONG                   cbServerName;

        // allocate element and Server string name (accessable outside of this
        // threads lifetime
        pInsHandle = (DRS_HANDLE_LIST_ELEM *)malloc(sizeof(DRS_HANDLE_LIST_ELEM)); 
        if (pInsHandle==NULL) {
            // we don't have the resources to unbind this handle.  free our resources 
            RpcSsDestroyClientContext(&hDrs);
            DRA_EXCEPT(ERROR_NOT_ENOUGH_MEMORY,0);
        }

        cbServerName = (wcslen(pszServerName)+1)*sizeof(WCHAR);
        pInsHandle->pszServerName = malloc(cbServerName);
        if (pInsHandle->pszServerName==NULL) {
            // we don't have the resources to unbind this handle.  free our resources   
            RpcSsDestroyClientContext(&hDrs);
            free(pInsHandle);
            DRA_EXCEPT(ERROR_NOT_ENOUGH_MEMORY,0);
        }

        wcscpy(pInsHandle->pszServerName, pszServerName);
        pInsHandle->hDrs = hDrs;

        // insert into queue     
        EnterCriticalSection(&gcsDrsRpcFreeHandleList);
        __try {
            if (!gfDrsRpcFreeHandleListInitialized) {
                InitializeListHead(&gDrsRpcFreeHandleList);  
                Assert(0 == gcNumDrsRpcFreeHandleListEntries);
                gfDrsRpcFreeHandleListInitialized = TRUE;
            }
            InsertTailList(&gDrsRpcFreeHandleList, &pInsHandle->ListEntry);
            ++gcNumDrsRpcFreeHandleListEntries; 
        }
        __finally {
            LeaveCriticalSection(&gcsDrsRpcFreeHandleList);
        } 
    }

}

void
DRSExpireClientCtxHandles(
    void
    ) 
{
    if (gfDrsRpcFreeHandleListInitialized) { 
        
        // this is the only deletion task on the queue, so we
        // don't need to hold the critsec to check IsListEmpty 
        while (!IsListEmpty(&gDrsRpcFreeHandleList)) {
            PLIST_ENTRY listEntry = NULL;
            DRS_HANDLE_LIST_ELEM * phFreeHandle = NULL;
            ULONG rpcstatus = 0; 

	    // release the critsec before going off machine
	    EnterCriticalSection(&gcsDrsRpcFreeHandleList);
	    __try {
		listEntry = RemoveHeadList(&gDrsRpcFreeHandleList);
		gcNumDrsRpcFreeHandleListEntries--;
		phFreeHandle = CONTAINING_RECORD(listEntry,DRS_HANDLE_LIST_ELEM,ListEntry); 
	    }
	    __finally {
		LeaveCriticalSection(&gcsDrsRpcFreeHandleList);
	    }

            // free the context handle (do the rpc call to unbind)
            DRSFreeContextHandle(phFreeHandle->pszServerName, &phFreeHandle->hDrs);
            free(phFreeHandle->pszServerName);
            free(phFreeHandle); 
        }
    }
}

void DRSClientCacheUninit(
    void
    )
{
    ULONG i;

    if (gfIsDrsClientLibInitialized) {

        // Reject any new callers.
        gfIsDrsClientLibInitialized = FALSE;

        // Release references to all cached binding handles.  Free each of them
        // if no one else claims a reference.
        EnterCriticalSection(&csBHCache);
        __try
        {
            for (i=0; i < BHCacheSize; i++)
            {
                VALIDATE_BH_ENTRY(i);

                if (    ( NULL != rgBHCache[i].hDrs )
    		     && ( rgBHCache[i].fDontUse == FALSE ) // make sure it's not already expired
                     && ( 0 == --rgBHCache[i].cRefs )
                   )
                {    
                    Assert(eServiceShutdown || DsaIsSingleUserMode());
                    // DRSFreeContextHandles does not go off machine while
                    // eServiceShutdown is true.
                    DRSFreeContextHandle(rgBHCache[i].pszServer,
                                         &rgBHCache[i].hDrs);
              
                    free(rgBHCache[i].pszServer);
                    if (rgBHCache[i].pszServerPrincName) {
                        free(rgBHCache[i].pszServerPrincName);
                    }
                    memset(&rgBHCache[i], 0, sizeof(rgBHCache[i]));
                }
            }
        }
        __finally
        {
            LeaveCriticalSection(&csBHCache);
        }
    }
}


ULONG
UlBHCacheHash(
    IN  LPWSTR  szServer
    )
{
    ULONG ulHashVal = 0;
    ULONG key = 0x0a1;

    while (*szServer)
    {
        ULONG hash1 = ((ulHashVal << 4) ^ *szServer);
        ULONG hash2 = ((ulHashVal & 0x80) ? key : 0);
        ULONG hash3 = ((ulHashVal & 0x40) ? key << 1 : 0);
        ULONG hash4 = ((ulHashVal & 0x20) ? key << 2 : 0);
        ULONG hash5 = ((ulHashVal & 0x10) ? key << 3 : 0);
        ulHashVal = hash1 ^ hash2 ^ hash3 ^ hash4 ^ hash5;
        szServer++;
    }

    ulHashVal %= BHCacheSize;
    return ulHashVal;
}


void
BHCacheDecRef(
    IN OUT  BHCacheElement *  pEntry
    )
/*++

Routine Description:

    Decrease the refcount of an entry in the handle cache.  If the refcount
    drops to zero, free the entry.

Arguments:

    pEntry (IN/OUT) - Entry for which to decrement the refcount.

Return Values:

    None.

--*/
{
    Assert(OWN_CRIT_SEC(csBHCache));
    Assert((BYTE *)pEntry >= (BYTE *)rgBHCache);
    Assert((BYTE *)pEntry < (BYTE *)rgBHCache + sizeof(rgBHCache));

    pEntry->cRefs -= 1;

    if (0 == pEntry->cRefs) {
        DRSInsertFreeHandleQueue(pEntry->pszServer, pEntry->hDrs);
        free(pEntry->pszServer);
        if (pEntry->pszServerPrincName) {
            free(pEntry->pszServerPrincName);
        }
        memset(pEntry, 0, sizeof(*pEntry));
    }
    else {
        // Update "last used" timestamp.
        pEntry->cTickLastUsed = GetTickCount(); 
    }
}

void
BHCacheFreeContext(
    IN      LPWSTR       szServer,
    IN OUT  DRS_HANDLE * pHandle
    )
{
    ULONG i;

    i = UlBHCacheHash(szServer);

    EnterCriticalSection(&csBHCache);
    __try
    {
        VALIDATE_BH_ENTRY(i);

        // The server name should match.
        Assert(    ( NULL != rgBHCache[i].pszServer )
                && !_wcsicmp( rgBHCache[i].pszServer, szServer )
              );

        // And the pointer to the context handle passed in by the caller must
        // not point to the handle in the cache -- otherwise we'll zero it
        // out below (in RpcSsDestroyClientContext), even if it still has a positive refcount.
        Assert(&rgBHCache[i].hDrs != pHandle);


        // Make sure it's our server.
        if (    ( NULL != rgBHCache[i].pszServer )
             && !_wcsicmp( rgBHCache[i].pszServer, szServer )
           )
        {
            // unlock the handle ( if it was locked )
            rgBHCache[i].fLocked = FALSE; 
            BHCacheDecRef(&rgBHCache[i]);
        } 

	Assert(pHandle);
	if (pHandle!=NULL) {
	    RpcSsDestroyClientContext(pHandle);	
	}
    }
    __finally
    {
        LeaveCriticalSection(&csBHCache);
    }
}

void BHCacheVoidServer(
    IN  LPWSTR      szServer
    )
{
    ULONG i;

    i = UlBHCacheHash(szServer);

    EnterCriticalSection(&csBHCache);
    __try
    {
        VALIDATE_BH_ENTRY(i);
        if (    ( NULL != rgBHCache[i].pszServer )
             && !_wcsicmp(rgBHCache[i].pszServer, szServer)
             && !rgBHCache[i].fDontUse )
        {
            // Set fDontUse flag.  This will stop entry from being handed
            // out and insure that it is nuked after the last deref.
            rgBHCache[i].fDontUse = TRUE;

            // Deref the entry's self-reference.
            BHCacheDecRef(&rgBHCache[i]);
        }
    }
    __finally
    {
        LeaveCriticalSection(&csBHCache);
    }
}

// Helper to copy (possibly NULL) source extensions to
// destination extensions.

VOID
CopyExtensions(
    DRS_EXTENSIONS *pextSrc,
    DRS_EXTENSIONS *pextDst)
{
    // We assume destination extensions are allocated to hold at least
    // CURR_MAX_DRS_EXT_FIELD_LEN bytes in their rgb field (i.e., that the
    // extensions *structure* is at least CURR_MAX_DRS_EXT_STRUCT_SIZE bytes).

    DWORD cb;

    if ( NULL == pextSrc )
    {
        pextDst->cb = 0;
    }
    else
    {
        // If we've never heard of an extension, we're not going
        // to ask about it, so cut the stream to fit our buffer
        // size (eliminating the need to dynamically allocate
        // extension buffers).

        pextDst->cb = min(pextSrc->cb, CURR_MAX_DRS_EXT_FIELD_LEN);
        memcpy(pextDst->rgb, pextSrc->rgb, pextDst->cb);
    }
}

//
// Flags for BHCacheGetDrsContext
//

#define BHCACHE_FLAGS_LOCK_HANDLE (1)

BOOL
BHCacheGetDrsContext(
    IN  THSTATE *         pTHS,
    IN  DRS_EXTENSIONS *  pextCurrentLocal,
    IN  LPWSTR            pszServer,
    IN  LPWSTR            pszDomain,
    IN  ULONG             flags,
    OUT DRS_HANDLE *      phDrs,
    OUT DRS_EXTENSIONS *  pextRemote
    )
/*++

Routine Description:

    Retrieve the DRS context handle and extensions associated with a given
    server from the cache, if an appropriate cache entry is available.

Arguments:

    pTHS -
    pextCurrentLocal - the local server extensions
    pszServer (IN) - name of the server for which to retrieve handle/extensions.
    pszDomain - the domain name of the server/spn which to retrieve handle/extensions.
    flags (IN) Controls operation of the routine. Defined flags are as follows

                  BHCACHE_FLAGS_LOCK_HANDLE -- Indicates that the handle be locked
                                               before returning. If this flag is
                                               specified then this routine will
                                               only return handles with a ref count
                                               of 1 after setting the locked flags

    phDrs (OUT) - on successful return, holds the associated context handle.
    pextRemote (OUT) - on successful return, holds the associated server extensions.

Return Values:

    TRUE - success.
    FALSE - no cache entry exists for this server.

--*/
{
    BOOL fSuccess = FALSE;
    int  i;
    ULONG rpcstatus = RPC_S_OK;
    LPWSTR pszCachedDomain = NULL;

    Assert((NULL != pszServer) && (NULL != phDrs) && (NULL != pextRemote));

    i = UlBHCacheHash(pszServer);

    EnterCriticalSection(&csBHCache);
    __try {
        VALIDATE_BH_ENTRY(i);

        // Do we have a cached handle to *this* server which
        // can be returned - i.e. is not flagged for expiry?

        if (    (NULL != rgBHCache[i].pszServer)
             && !_wcsicmp(pszServer, rgBHCache[i].pszServer)
             && (!rgBHCache[i].fDontUse)
             && (!rgBHCache[i].fLocked)
             && ((1==rgBHCache[i].cRefs) || (!(flags & BHCACHE_FLAGS_LOCK_HANDLE)))){
            if ((pextCurrentLocal->cb != rgBHCache[i].extLocal.cb)
                  || (0 != memcmp(pextCurrentLocal->rgb,
                                  rgBHCache[i].extLocal.rgb,
                                  pextCurrentLocal->cb))) {
                // Our DRS extensions have changed since we bound to this remote
                // DSA.  Unbind and rebind so we can inform the remote DSA of
                // our latest extensions.
                
                DPRINT1(0, "Forcing rebind to %ls because our DRS_EXTENSIONS have changed.\n",
                        rgBHCache[i].pszServer);
            
                // Set fDontUse flag.  This will stop entry from being handed
                // out and ensure that it is nuked after the last deref.
                rgBHCache[i].fDontUse = TRUE;
    
                // Deref the entry's self-reference.
                BHCacheDecRef(&rgBHCache[i]);

                Assert(!fSuccess);
            
            } else if (
                       (pszDomain!=NULL) &&
                       ((!IS_DOMAIN_QUALIFIED_SPN(rgBHCache[i].pszServerPrincName)) || 
                        ((IS_DOMAIN_QUALIFIED_SPN(rgBHCache[i].pszServerPrincName)) &&
                         (pszCachedDomain = GET_DOMAIN_FROM_QUALIFIED_SPN(rgBHCache[i].pszServerPrincName)) &&
                         (_wcsicmp(pszCachedDomain, pszDomain))))
                       )
                {  
                // We have been requested to bind with an SPN which has pszDomain as the domain
                // suffix but the cached info has a different suffix.  Since there exists cached info
                // it succeeded in binding with the cached domain - which implies that the domain
                // of the source server is the cached domain.  This would seem to be a security risk
                // as the source server is in domain A but we are requesting a server who's SPN 
                // assumes that the server is in domain B.  Either the DC has two writable domains, or
                // the user is calling ReplicateSingleObject on an object in a different domain.  Either 
                // way, the resolution should be failed.  In case of error, since this shouldn't be a
                // commonly occuring code path, rebind the handle.
            
                LogEvent(DS_EVENT_CAT_REPLICATION,
                         DS_EVENT_SEV_ALWAYS,
                         DIRLOG_DRA_UNAUTHORIZED_NC,
                         szInsertWC(rgBHCache[i].pszServer),
                         szInsertWC(pszDomain),
                         NULL );        

                // Set fDontUse flag.  This will stop entry from being handed
                // out and ensure that it is nuked after the last deref.
                rgBHCache[i].fDontUse = TRUE;
    
                // Deref the entry's self-reference.
                BHCacheDecRef(&rgBHCache[i]);

                Assert(!fSuccess);
            } else {

                // Return a copy of the cached handle.  We don't return
                // a pointer to the handle itself so that we can set
                // per handle variables, such as time to cancel.  IE
                // if we returned a pointer to the cached handle, all
                // calls using this handle, no matter with what purpose
                // would have the same time to cancel.  This function
                // has been overloaded to copy context handles, but 
                // RpcBindingFree has not been, so this is freed with
                // RpcSsDestroyClientContext
                rpcstatus = RpcBindingCopy(rgBHCache[i].hDrs, phDrs);  
                if (rpcstatus != RPC_S_OK) {
                    fSuccess = FALSE; 
                    __leave;
                }
                CopyExtensions(&rgBHCache[i].extRemote, pextRemote);
                rgBHCache[i].cRefs += 1;
                if (flags & BHCACHE_FLAGS_LOCK_HANDLE) {
                    rgBHCache[i].fLocked = TRUE;
                }

                fSuccess = TRUE;
            }
        }
    }
    __finally {
        LeaveCriticalSection(&csBHCache);
    }

    return fSuccess;
}


BOOL
findDomainNcFast(
    THSTATE *pTHS,
    DBPOS *pDBTmp,
    LPWSTR *ppszDomainNcName
    )

/*++

Routine Description:

Helper routine to extract the domain guid.  This routine uses two methods to
obtain the name of the domain associated with the current NTDSA object.
1. First we try to read the msds-hasdomainncs attribute. This will not be set on
w2k dcs and dc's upgraded from .net betas.
2. It tries to obtain the domain from the masterNC list.  This may not always work
if the list does not have the expected contents.

We expect the "old" hasMasterNcs list to have three items: schema nc, config nc
and the domain nc.  Match the schema and config nc's by guid to eliminate them,
and we are left with the domain nc.  This could legitimately fail if:
1. "old" hasMasterNCs not populated during install scenarios for some reason
2. Due to exchange compatibility requirements, the "old" hasMasterNCs will only
   ever have 3 NCs.

Expect we are positioned on a NTDSA object

Arguments:

    pDBTmp -
    puuidDomain -

Return Value:

    BOOL - Whether the masterNC list was in expected form.  It may not be
    and this is not an error.
    Exceptions raised for errors.

--*/

{
    BOOL fFound = FALSE, fSeenSchema = FALSE, fSeenConfig = FALSE;
    ULONG NthValIndex = 0;
    ULONG len, nameLength;
    DSNAME *pNC = NULL;

    Assert(pTHS->JetCache.transLevel > 0);

    // See if we have a domain nc link we can take advantage of
    if (!(DBGetAttVal(pDBTmp,
                      1,
                      ATT_MS_DS_HAS_DOMAIN_NCS,
                      0,
                      0, &len, (PUCHAR *)&pNC))) {
        // FUTURE-2002/03/21-BrettSh - Why doesn't this code just return the DSNAME?
        nameLength = wcslen( pNC->StringName ) + 1;
        *ppszDomainNcName = THAllocEx( pTHS, nameLength * sizeof(WCHAR) );
        wcscpy( *ppszDomainNcName, pNC->StringName );
        DPRINT1( 2, "Using msds-hasdomainncs to calculate domain %ws\n", pNC->StringName );
        THFree( pNC );
        return TRUE;
    }

    // Loop through the hasMasterNCs list
    while (!(DBGetAttVal(pDBTmp,
                         ++NthValIndex,
                         GetRightHasMasterNCsAttr(pDBTmp),
                         0,
                         0, &len, (PUCHAR *)&pNC))) {

        // Config NC
        if ( 0 == memcmp( &(pNC->Guid), &(gAnchor.pConfigDN->Guid), sizeof( UUID ) ) ) {
            Assert( !fSeenConfig );
            fSeenConfig = TRUE;
            DPRINT1( 2, "dnConfig = %ws\n", pNC->StringName );
            THFree( pNC );
            continue;
        }

        // Schema NC
        if ( 0 == memcmp( &(pNC->Guid), &(gAnchor.pDMD->Guid), sizeof( UUID ) ) ) {
            Assert( !fSeenSchema );
            fSeenSchema = TRUE;
            DPRINT1( 2, "dnSchema = %ws\n", pNC->StringName );
            THFree( pNC );
            continue;
        }

        // Its a domain NC.  In the future, may be more than one
        // ISSUE-2001/01/19-JeffParh - Should bail early (and free string) if
        // we've already seen one non-config/schema NC.
        nameLength = wcslen( pNC->StringName ) + 1;
        *ppszDomainNcName = THAllocEx( pTHS, nameLength * sizeof(WCHAR) );
        wcscpy( *ppszDomainNcName, pNC->StringName );
        DPRINT1( 2, "dnDomain = %ws\n", pNC->StringName );
        THFree( pNC );
    }

    // Must only be three NCs in the list, and have a schema and config nc
    if ( (4 == NthValIndex) && fSeenSchema && fSeenConfig ) {
        fFound = TRUE;
    }

    // This may not be true if the server has its domain nc removed, for example
    // by deleting its cross-ref object, or if the ntdsDsa object itself has been
    // deleted (in which case it has no ATT_HAS_MASTER_NCS or ATT_MS_DS_HAS_MASTER_NCS).

    return fFound;
} /* findDomainNcFast */


VOID
findDomainNcGeneral(
    THSTATE *pTHS,
    DBPOS *pDBTmp,
    LPWSTR *ppszDomainNcName
    )

/*++

Routine Description:

Helper routine to extract the domain NC name.  This algorithm will work
regardless of the number of master NC's a server may host.

Assumed pDBTmp is positioned on the server object, which is valid and
present.

Arguments:

    pDBTmp -
    puuidDomain -

Return Value:

   None.
   Exceptions raised on error

--*/

{
    DWORD dbErr;
    DSNAME *pdnComputer = NULL;
    COMMARG commArg;
    CROSS_REF * pCR;
    DWORD cb, nameLength;

    __try {

        // Get the computer dn
        dbErr = DBGetAttVal(pDBTmp, 1, ATT_SERVER_REFERENCE,
                         0, 0, &cb, (BYTE **) &pdnComputer);
        if (dbErr) {
            DPRINT1(0, "Can't find server reference, db error %d.\n", dbErr);
            
            // Server object (parent of NTDS Settings) lacks a server reference
            // attribute.  This might occur if the server object had been
            // deleted, for example, or if the server reference has not yet been
            // written,
            DRA_EXCEPT(ERROR_DS_CANT_DERIVE_SPN_WITHOUT_SERVER_REF, dbErr);
        }

        DPRINT1( 2, "dnComputer = %ws\n", pdnComputer->StringName );

        // If the server reference contains mangled components, it is not a useful
        // or valid input to FindBestCrossRef. Don't even try.
        // This check is to help diagnose cases where the server reference is mangled.
        // If we did not make this check, FindBestCrossRef might return a parent
        // cross-ref, resulting in an valid but wrong SPN being generated.  The user
        // would see this as wrong account name when A calls B, and not know to check
        // the server reference attribute for B on A.

        if (IsMangledDSNAME(pdnComputer, NULL)) {
            DRA_EXCEPT(ERROR_DS_CANT_DERIVE_SPN_FOR_DELETED_DOMAIN, dbErr);
        }

        // Find the naming context of the computer
        // This could be a phantom on this machine if we don't hold nc
        memset( &commArg, 0, sizeof( COMMARG ) );  // not used
        pCR = FindBestCrossRef( pdnComputer, &commArg );
        if (NULL == pCR) {
            DPRINT1(0, "Can't find cross ref for %ws.\n",
                    pdnComputer->StringName );
            DRA_EXCEPT(ERROR_DS_NO_CROSSREF_FOR_NC, DRAERR_InternalError);
        }

        nameLength = wcslen( pCR->pNC->StringName ) + 1;
        *ppszDomainNcName = THAllocEx( pTHS, nameLength * sizeof(WCHAR) );
        wcscpy( *ppszDomainNcName, pCR->pNC->StringName );

        DPRINT1( 2, "NC = %ws\n", pCR->pNC->StringName );

    } _finally {

        if (NULL != pdnComputer) {
            THFree(pdnComputer);
        }
    }

} /* findDomainNcGeneral */


VOID
findDnsHostDomainNames(
    THSTATE *pTHS,
    UUID *pguidNtdsaObject,
    LPWSTR *ppszDnsDomainName
    )

/*++

Routine Description:

Given the guid of an NTDS DSA object, calculate the dns hostname and the
dns domain name.

The hostname comes from the ATT_DNS_HOST_NAME attribute on the parent object, which
is the server object.

The dns domain name comes by
1. Determining the domain nc that the server is in
2. Converting the domain nc to a dns domain name

Arguments:

    pTHS -
    pguidNtdsaObject - guid of ntdsa object
    ppszDnsDomainName - pointer to pointer to receive dns domain name

Return Value:

    VOID - raises exceptions

--*/

{
    DWORD cb, dbErr, status, length;
    DBPOS *pDBTmp = NULL;
    DSNAME dnNtdsaByGuid = {0};
    BOOL fDomainFound;
    LPWSTR pszDomainNC = NULL;
    PDS_NAME_RESULTW pResult = NULL;

#ifdef INCLUDE_UNIT_TESTS
    Assert( !pTHS->pDB && "should not have a primary dbpos open across an rpc call" );
#endif

    Assert( pguidNtdsaObject );

    // Create a dsname with just the server's guid in it
    dnNtdsaByGuid.structLen = sizeof(dnNtdsaByGuid);
    dnNtdsaByGuid.Guid = *pguidNtdsaObject;

    // Use another DBPOS so not as to disturb callers thread db state
    DBOpen (&pDBTmp);
    __try {
        // Look up the server's NTDSA object
        dbErr = DBFindDSName(pDBTmp, &dnNtdsaByGuid);
        if (dbErr)
        {
            DPRINT1( 0, "DbFindDsName(guid) failed with db err %d\n", dbErr );
            DRA_EXCEPT(ERROR_DS_CANT_FIND_DSA_OBJ, dbErr);
        }

        //
        // Find the domain name
        //

        fDomainFound = findDomainNcFast( pTHS, pDBTmp, &pszDomainNC );
        if (!fDomainFound) {
            // If we failed to find the fast way, try the general way
            // Reposition to parent (server obj) if needed
            DBFindDNT( pDBTmp, pDBTmp->PDNT );
            findDomainNcGeneral( pTHS, pDBTmp, &pszDomainNC );
        }

        // Convert the NC to a DNS name

        status = DsCrackNamesW (
            NULL,
            DS_NAME_FLAG_SYNTACTICAL_ONLY,
            DS_FQDN_1779_NAME,
            DS_CANONICAL_NAME_EX,
            1,
            &pszDomainNC,
            &pResult);
        if (status != ERROR_SUCCESS) {
            DPRINT1( 0, "DsCrackNamesW failed, error %d\n", status );
            DRA_EXCEPT(ERROR_DS_BAD_NAME_SYNTAX, status);
        }
        if ( (pResult->cItems != 1) ||
             ( (pResult->rItems[0].status != DS_NAME_NO_ERROR) &&
               (pResult->rItems[0].status != DS_NAME_ERROR_DOMAIN_ONLY) ) ) {
            DPRINT1( 0, "DsCrackNamesW returned unexpected results %d\n",
                     pResult->rItems[0].status );
            // ERROR: domain is in wrong form, can't crack
            DRA_EXCEPT(ERROR_DS_BAD_NAME_SYNTAX, status);
        }

        // Copy out the domain name
        length = wcslen( pResult->rItems[0].pDomain ) + 1;
        *ppszDnsDomainName = THAllocEx( pTHS, length * sizeof(WCHAR) );
        wcscpy( *ppszDnsDomainName, pResult->rItems[0].pDomain );
        DPRINT1( 2, "dnsDomain = %ws\n", *ppszDnsDomainName );

    } _finally {
        if (pDBTmp) {
            DBClose (pDBTmp, !AbnormalTermination());
        }
        if (pszDomainNC) {
            THFreeEx(pTHS, pszDomainNC);
        }
        if (pResult) {
            DsFreeNameResultW( pResult );
        }
    }
} /* findDnsHostDomainNames */


BOOL
DRSMakeFQDnsName(
    IN  THSTATE *   pTHS,
    IN  LPWSTR      pszServerName,
    OUT LPWSTR *    ppszFQDnsName
    )
/*++

Routine Description:

    Converts a non-qualified DNS name (e.g., L"172.31.238.167") to a fully
    qualified DNS name (e.g., L"ntdsdc0.ntdev.microsoft.com").

Arguments:

    pszServerName (IN) - server name to convert.

    ppszFQDnsName (OUT) - On successful return, holds the fully qualified
        DNS name of the server.  Optional.

Return Values:

    TRUE - success.
    FALSE - failure.

--*/
{
    BOOL              fSuccess = FALSE;
    LPSTR             paszServerName;
    struct hostent *  pHostEntry = NULL;

    paszServerName = String8FromUnicodeString(TRUE, CP_ACP, pszServerName, -1,
                                              NULL, NULL);
    Assert(NULL != paszServerName);
    Assert('\0' != *paszServerName);

    pHostEntry = gethostbyname(paszServerName);
    if (pHostEntry) {
        if (ppszFQDnsName) {
            *ppszFQDnsName = UnicodeStringFromString8(CP_ACP,
                                                      pHostEntry->h_name, -1);
            Assert(NULL != *ppszFQDnsName);
            Assert(L'\0' != **ppszFQDnsName);
        }

        fSuccess = TRUE;
    }

    THFreeEx(pTHS, paszServerName);

    return fSuccess;
}


DWORD
DRSMakeOneWaySpn(
    IN  THSTATE *           pTHS,
    IN  LPWSTR              pszTargetServerName,
    IN  RPC_BINDING_HANDLE  hServer,
    OUT LPWSTR *            ppszSpn
    )

/*++

Routine Description:

Create a one-way authentication SPN.  This is done by asking the server what name
he would like us to use.  Not very secure this way.

Arguments:

    pTHS - Thread state
    pszTargetServerName - Name of target server
    hServer - RPC binding handle to target server
    ppszSpn - pointer to pointer, to receive pointer to allocated string

Return Value:

    DWORD -

--*/

{
    DWORD status, length;
    LPWSTR pszRpcSpn = NULL, pszSpnTemp;

    DPRINT1( 0, "Rpc Handle to %ls is using one-way authentication.\n",
             pszTargetServerName );

    status = RpcMgmtInqServerPrincNameW(hServer,
                                        RPC_C_AUTHN_GSS_KERBEROS,
                                        &pszRpcSpn);

    Assert((status != RPC_S_BINDING_INCOMPLETE)
           && "Contact RPC dev -- this shouldn't be returned by"
           "RpcMgmtInqServerPrincName().");

    if (RPC_S_OK != status) {
        DPRINT1(0, "RpcMgmtInqServerPrincName failed with %d\n", status);
        return status;
    }

    Assert( pszRpcSpn && wcslen(pszRpcSpn) );

    // Spn is expected to returned from malloc heap - reallocate
    length = wcslen( pszRpcSpn ) + 1;
    pszSpnTemp = malloc(length * sizeof(WCHAR));
    if (NULL == pszSpnTemp) {
        status = ERROR_NOT_ENOUGH_MEMORY;
    } else {
        wcscpy(pszSpnTemp, pszRpcSpn);
        *ppszSpn = pszSpnTemp;
    }

    RpcStringFreeW(&pszRpcSpn);

    return status;
} /* DRSMakeOneWaySpn */


DWORD
DRSMakeMutualAuthSpn(
    IN  THSTATE *   pTHS,
    IN  LPWSTR      pszTargetServerName,
    IN  LPWSTR      pszTargetDomainName,    OPTIONAL
    OUT LPWSTR *    ppszSpn
    )
/*++

Routine Description:

   Construct a SPN for use in communicating with the desired server.

   The SPN's which the server is expecting are written in
   dsamain\src\servinfo.c

   According to the SPN specification by Paulle, client side replication spns
   are as follows:

   DCGUID = guid of the host server DC
   DDNS = fully qualified DNS name of domain of the host server DC

   Form the SPN "E3514235-4B06-11D1-AB04-00C04FC2DCD2/DCGUID/DDNS". (The GUID
   is that of the replication RPC interface.) This is what is written to the
   servicePrincipalName attribute of the DCs account object

   A replication client adds "@DDNS" to the above SPN to pass in to the
   pszTargetName parameter of InitializeSecurityContext or the ServerPrincName
   parameter of RpcBndingSetAuthInfo. The reason for the "@DDNS" is so that
   the KDC does not try and look the SPN up in the GC, which we don't want to
   do because the GC depends on replication, so we'd have a circular
   dependency.  The "@DDNS" does not get written to the servicePrincipalName
   attribute.

Arguments:

    pTHS (IN)
    pszTargetServerName (IN) - dns name of server to be called
    pszTargetDomainName (IN, OPTIONAL) - dns name of domain for which the target
        server is a DC
    ppszSpn (OUT) - receives pointer to buffer to new spn

    A note on variable usage:

    pszTargetServerName - incoming server name, never modified
    pszServiceName - The domain name part of the spn. This is a pointer to
           whatever storage we decide to use. Never freed itself.
    pszInstanceName - The host name part of the spn. This is a pointer to
           whatever storage we decide to use. Never freed itself.
    pszSpn - actual spn to be returned to the user. Only freed on error.
    pszTemp, pszDnsHostName, pszDnsDomainName - temporary internal allocations
            Always freed on exit.

Return Value:

    DWORD - win32 status

--*/
{
    DWORD status, length, nameLength;
    LPWSTR pszServiceName, pszInstanceName, pszClassName;
    WCHAR szServerUuid[SZGUIDLEN + 1];
    UUID uuidServer, uuidDomain;
    BOOL fGuidBasedName = FALSE;
    LPWSTR pszSpn = NULL, pszTemp = NULL;
    LPWSTR pszDnsHostName = NULL, pszDnsDomainName = NULL;

    DPRINT1( 1, "DRSMakeSpn: server = %ls\n", pszTargetServerName );

    Assert( pszTargetServerName );
    Assert( ppszSpn );

    // Start with the name we are given
    pszInstanceName = pszTargetServerName;

    // Name transformations

    // Treat netbios names like dns names, remove \\ prefix
    if (*pszInstanceName == L'\\') {
        pszInstanceName++;
        if (*pszInstanceName == L'\\') {
            pszInstanceName++;
        }
    }

    // If server name has trailing '.', make a copy and remove it
    nameLength = wcslen( pszInstanceName );
    Assert( nameLength );
    if (pszInstanceName[nameLength - 1] == L'.') {
        pszTemp = THAllocEx(pTHS, nameLength * sizeof(WCHAR));
        nameLength--;
        wcsncpy(pszTemp, pszInstanceName, nameLength);
        pszTemp[nameLength] = L'\0';
        pszInstanceName = pszTemp;
    }

    // Determine what kind of name this.

    fGuidBasedName = DSAGuidFromGuidDNSName(pszInstanceName, NULL, szServerUuid, TRUE);

    if (fGuidBasedName) {
        // GUID-based DNS name.
        //
        // SPN = drs idl guid / ntdsa-guid / domain @ domain
        //

        // If caller supplied GUID-based DNS name and target domain name, then
        // use the target domain name in the SPN to ensure the resolution of the
        // SPN is done in the given domain.  Else do the resolution in the 
        // domain of the server who's spn we are creating.
        if (pszTargetDomainName!=NULL) {
            pszClassName = DRS_IDL_UUID_W;
            pszInstanceName = szServerUuid;
            pszServiceName = pszTargetDomainName; 
        } else {
            DWORD rpcErr = RPC_S_OK;
            rpcErr = UuidFromStringW(szServerUuid, &uuidServer);
            // since IsGuidBasedName returned true, this function will succeed.
            Assert(rpcErr==RPC_S_OK);

            __try {
                findDnsHostDomainNames( pTHS,
                                        &uuidServer,
                                        &pszDnsDomainName );
                status = ERROR_SUCCESS;
            } __except (GetDraException((GetExceptionInformation()), &status)) {
                ;
            }
            if (status != ERROR_SUCCESS) {
                goto cleanup;
            }

            pszClassName = DRS_IDL_UUID_W;
            pszInstanceName = szServerUuid;
            pszServiceName = pszDnsDomainName;
            pszTargetDomainName = pszDnsDomainName; 
        }
    } else {
        // HOST-based DNS name
        //
        // SPN = GC / Hostname / forest (aka Root DNS name )
        //
        pszClassName = GCSpnType;
        // pszInstanceName already set to incoming target host name
        pszServiceName = gAnchor.pwszRootDomainDnsName;
        // pszTargetDomainName is set if present
    }

    // Construct host-based replication SPN
    // Note, malloc is used to allocate the principal name because it is stored
    // in the binding cache and must outlast the current thread.

    Assert( pszInstanceName );
    Assert( pszServiceName );

    length = SZGUIDLEN + 1 +
        wcslen( pszInstanceName ) + 1 +
        wcslen( pszServiceName ) + 1 +
        (pszTargetDomainName ? wcslen( pszTargetDomainName ) : 0) + 1;

    pszSpn = (LPWSTR) malloc(length * sizeof(WCHAR));
    if (NULL == pszSpn) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    status = DsMakeSpnW( pszClassName,
                         pszServiceName,
                         pszInstanceName,
                         0,
                         NULL,
                         &length,
                         pszSpn );
    if (status != ERROR_SUCCESS) {
        DPRINT1( 0, "DsMakeSpn failed with status %d\n", status );
        goto cleanup;
    }

    // If we have a target domain name, use it
    // The @ form is needed when making a call to the GC
    if (pszTargetDomainName) {
        wcscat( pszSpn, L"@" );
        wcscat( pszSpn, pszTargetDomainName );
    }


    DPRINT2( 1, "DRSMakeSpn: server=%ws, new SPN = %ls\n",
             pszTargetServerName, pszSpn );

    *ppszSpn = pszSpn;
    pszSpn = NULL; // don't clean up

    // fall through for cleanup of temporaries

cleanup:
    if (pszSpn) {
        free( pszSpn );
    }
    if (pszTemp) {
        THFreeEx(pTHS, pszTemp);
    }
    if (pszDnsHostName) {
        THFreeEx(pTHS, pszDnsHostName);
    }
    if (pszDnsDomainName) {
        THFreeEx(pTHS, pszDnsDomainName);
    }

    return status;
} /* DRSMakeSpn */

RPC_STATUS
DRSGetRpcBinding(
    IN  THSTATE *             pTHS,
    IN  LPWSTR                pszServer,
    IN  LPWSTR                pszDomain,            OPTIONAL
    IN  BOOL                  fUseMutualAuthSpn,
    IN  BOOL                  fUseUniqueAssoc,
    OUT RPC_BINDING_HANDLE *  prpc_handle,
    OUT LPWSTR *              ppszServerPrincName
    )
/*++

Description:

    Generate a new RPC_BINDING_HANDLE for the given server.

Arguments:

    pTHS (IN)

    pszServer (IN) - server of interest.

    pszDomain (IN, OPTIONAL) - DNS domain name of server.
    
    fUseMutualAuthSpn - Sort of what it sounds like, but the actual Mutual Auth 
        protocol is used irrespective of this input, but if FALSE, then we
        query the remote machine for what SPN should be used and then the protocol
        validates that.
        
    fUseUniqueAssoc - Whether or not to use a unique RPC Association for the binding.
        Used to ensure refreshed security context information.  
        see drsIsCallComplete for details.   

    prpc_handle (OUT) - on success, holds a pointer to RPC_BINDING_HANDLE for
        szServer.

    ppszServerPrincName (IN/OUT) on success, holds a pointer to the server
        principal name as returned by RpcMgmtInqServerPrincName.  Should be
        freed via RpcStringFree.

Return value:

    RPC_STATUS - Win32 error code

--*/
{
    RPC_IF_HANDLE               IfHandle = _drsuapi_ClientIfHandle;
    RPC_STATUS                  status;
    ULONG                       DrsError;
    PSEC_WINNT_AUTH_IDENTITY_W  Credentials = NULL;
    LPWSTR                      pszStringBinding = NULL;
    LPWSTR                      pszAddress;
    WCHAR                       pszIpAddress[IPADDRSTR_SIZE];
    BOOL                        fFallback = FALSE;
    RPC_SECURITY_QOS            RpcSecQOS;

    Assert(NULL != pszServer);
    Assert(NULL != prpc_handle);
    Assert(NULL != ppszServerPrincName);

    *ppszServerPrincName = NULL;
    *prpc_handle = NULL;

    __try {
        status = DrspGetCredentials(&Credentials);
        if (0 != status) {
            // This function should only fail on resource allocation failures.
            DPRINT1(0, "DRSGetRpcBinding: DrspGetCredentials failed (%d)\n", status);
            __leave;
        }

        // Strip leading backslashes.
        for (pszAddress = pszServer; L'\\' == *pszAddress; pszAddress++) {
            ;
        }

        if (IsOurGuidAddr(pszAddress)) {
            status = GetIpAddrByDnsNameW(L"localhost", pszIpAddress);
            Assert(status == 0); // Can we fail when we lookup localhost?
        } else {
            status = GetIpAddrByDnsNameW(pszAddress, pszIpAddress);
        }

        if(status){
            LogEvent8(DS_EVENT_CAT_RPC_CLIENT,
                      DS_EVENT_SEV_MINIMAL,
                      DIRLOG_DS_DNS_HOST_RESOLUTION_FAILED,
                      szInsertWC(pszAddress),
                      szInsertWin32Msg(status),
                      szInsertWin32ErrCode(status),
                      NULL, NULL, NULL, NULL, NULL );

            DPRINT2(1, "DRSGetRpcBinding: GetIpAddrByDnsNameW() failed to resolve hostname %ws Winsock failed with %d\n",
                    pszAddress, status);
            
            status = ERROR_DS_DNS_LOOKUP_FAILURE;
            __leave;
        }

        status = RpcStringBindingComposeW(0,
                                          L"ncacn_ip_tcp",
                                          pszIpAddress,
                                          NULL,
                                          0,
                                          &pszStringBinding);
        if (RPC_S_OK != status) {
            DPRINT1(0, "DRSGetRpcBinding: RpcStringBindingComposeW failed (%d)\n",
                    status);
            __leave;
        }

        status = RpcBindingFromStringBindingW(pszStringBinding, prpc_handle);
        if (RPC_S_OK != status) {
            DPRINT1(0, "DRSGetRpcBinding: RpcBindingFromStringBindingW failed (%d)\n",
                    status);
            __leave;
        }

        Assert(NULL != *prpc_handle);

        // if we want a unique association, we have to call it now, before we fully bind
        // the handle.
        if (fUseUniqueAssoc) {
            status = RpcBindingSetOption(*prpc_handle, RPC_C_OPT_UNIQUE_BINDING, 1);  
            if (status!=RPC_S_OK) {
                Assert(!"Unable to set option RPC_C_OPT_UNIQUE_BINDING");
                LogUnhandledError(status);
                __leave;
            }
        }

        // Binding handle created successfully.  Resolve the partially bound
        // handle into a fully bound handle.
        status = RpcEpResolveBinding(*prpc_handle, IfHandle);
        if (RPC_S_OK != status) {
            DPRINT1(1, "DRSGetRpcBinding: RpcEpResolveBinding failed (%d)\n",
                    status);
            __leave;
        }

        // Make mutual auth spn if requested.
        // There are two types of fallback here: construction time and bind time.
        // If we fail to construct the spn for an expected reason, construct a one-
        // way spn.  If we construct ok, but fail to bind, this routine gets called
        // again with the fUseMutual flag false, at which we build a one-way spn.
        if (fUseMutualAuthSpn) {
            //
            // Caller wants a mutual authenticated binding
            //
            status = DRSMakeMutualAuthSpn(pTHS, pszAddress, pszDomain,
                                          ppszServerPrincName);
            if (status) {
                if ((0 == GetConfigParam(DRA_SPN_FALLBACK, &fFallback, sizeof(BOOL)))
                    && fFallback) {
                    // We've been configured to allow fallback to one-way auth
                    // if we fail to construct a mutual-auth SPN.
                    DPRINT2(0, "Handling error %d by falling back to one-way auth for %ws.\n",
                            status, pszAddress);
                    status = DRSMakeOneWaySpn( pTHS,pszAddress,*prpc_handle,ppszServerPrincName );  
                } else {
                    LogEvent8(DS_EVENT_CAT_RPC_CLIENT,
                              DS_EVENT_SEV_ALWAYS,
                              DIRLOG_BUILD_SPN_FAILURE,
                              szInsertWC(pszAddress),
                              szInsertWin32Msg(status),
                              szInsertWin32ErrCode(status),
                              NULL, NULL, NULL, NULL, NULL );
                    DPRINT2( 0, "Error %d creating mutual auth SPN for %ws.\n",
                             status, pszAddress);
                }
            }
        } else {
            //
            // Caller wants a one-way authenticated binding
            //
            status = DRSMakeOneWaySpn( pTHS,pszAddress,*prpc_handle,ppszServerPrincName ); 
        }
        if (RPC_S_OK != status) {
            DPRINT1(0, "DRSGetRpcBinding: DRSMakeSpn failed (%d)\n", status);
            __leave;
        }

        Assert(L'\0' != *ppszServerPrincName);

        RpcSecQOS.IdentityTracking = RPC_C_QOS_IDENTITY_STATIC;
        RpcSecQOS.ImpersonationType = RPC_C_IMP_LEVEL_IMPERSONATE;
        RpcSecQOS.Version = RPC_C_SECURITY_QOS_VERSION;
        RpcSecQOS.Capabilities = RPC_C_QOS_CAPABILITIES_MUTUAL_AUTH; 

        status = RpcBindingSetAuthInfoExW(*prpc_handle,
                                          *ppszServerPrincName,
                                          RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
                                          RPC_C_AUTHN_GSS_KERBEROS,
                                          Credentials,
                                          RPC_C_AUTHZ_NONE,
                                          &RpcSecQOS);
        if (RPC_S_OK != status) {
            DPRINT1(0, "DRSGetRpcBinding: RpcBindingSetAuthInfo failed (%d)\n",
                    status);
            __leave;
        }

        // Success!
        Assert(RPC_S_OK == status);
        Assert(NULL != *prpc_handle);
        Assert(NULL != *ppszServerPrincName);
    }
    __finally {
        if (NULL != pszStringBinding) {
            RpcStringFreeW(&pszStringBinding);
        }

        if ((RPC_S_OK != status) || AbnormalTermination()) {
            if (NULL != *prpc_handle) {
                RpcBindingFree(prpc_handle);
                Assert(NULL == *prpc_handle)
            }

            if (NULL != *ppszServerPrincName) {
                free(*ppszServerPrincName);
                *ppszServerPrincName = NULL;
            }
        }

        if (NULL != Credentials) {
            DrspFreeCredentials(Credentials);
        }
    }

    // We should return a binding handle if and only if we we return success.
    if (RPC_S_OK == status) {
        Assert(NULL != *prpc_handle);
        Assert(NULL != *ppszServerPrincName);
    }
    else {
        Assert(NULL == *prpc_handle);
        Assert(NULL == *ppszServerPrincName);
    }

    return status;
}

VOID
RpcCancelAll()
{
    ULONG i;
    PLIST_ENTRY listEntry = &gDrsRpcServerCtxList;

    EnterCriticalSection(&gcsDrsRpcServerCtxList);
    __try {
        if (!gfDrsRpcServerCtxListInitialized) {
           __leave;
        }

        while (listEntry->Flink!=&gDrsRpcServerCtxList) {
            
            DRS_CONTEXT_INFO *pContextInfo = NULL;
            ULONG rpcstatus = 0; 
            ULONG ulCancelWait = 1; //ms
            // note: there is no reason to wait long - the cancel time is the time the server code
            // has to notice a cancel has been called - and we rarely check (RpcTestCancel) in any
            // server stubs.  We'd prefer our client threads to leave ASAP and try to 
            // free the server memory.  The server is protected (see draserv.c) in the case that
            // it has two threads running from us, one doing some work, and one trying to unbind since
            // we are exiting.
 
            listEntry = listEntry->Flink;
            // Check list sanity
            Assert((listEntry->Flink->Blink)==listEntry);
            Assert((listEntry->Blink->Flink)==listEntry);

            pContextInfo = CONTAINING_RECORD(listEntry,DRS_CONTEXT_INFO,ListEntry); 
            if ((pContextInfo->phThread!=NULL) && (*(pContextInfo->phThread)!=INVALID_HANDLE_VALUE)) { 
                rpcstatus = RpcCancelThreadEx(*(pContextInfo->phThread), ulCancelWait);  
                // don't remove these entries from the list!  The RpcCancelThreadEx will return 
                // control to the threads themselves and they will clean up and take themselves off 
                // the ServerCtxList.
            }
            else {
                Assert(!"Trying to cancel invalid thread!");
            }
        }
    }
    __finally {
        LeaveCriticalSection(&gcsDrsRpcServerCtxList);
    }

}

BOOL
FBindSzDRS(
    IN      THSTATE *           pTHS,
    IN      LPWSTR              szServer,
    IN      LPWSTR              pszDomainName,  OPTIONAL
    IN      ULONG               ulFlags,
    IN OUT  DRS_CONTEXT_INFO ** ppContextInfo,
    IN OUT  DWORD *             pdwStatus
    )
/*++

Routine Description:

    Retrieve a DRS context handle and extension set for the given server.

    Optionally searches the cache for a pre-existing handle.  Otherwise or
    if no appropriate entry is found in the cache, contacts the server for a
    new context handle.

    If a cache search is requested but none is found, and a new context
    handle is succesfully generated, this function adds the handle to the
    cache if a free entry is available.  If a cached handle exists (either
    pre-existing or created) then a copy of the handle is returned.
    
    A context handle has 2 parts, a client component (binding handle) and a 
    pointer to a component on the server.  A copy of a context handle 
    creates a new client component with a pointer to the component 
    on the server.  The elements of the client component of
    the handle cannot be shared between threads/calls (specifically for our 
    implemenation the cancellation time we don't want to share) so we 
    return a copy of cached handles.  Because the server component is shared
    we reference count the copies.  We free the resources of the client copies
    by calling RpcSsDestroyClientContext, and call IDL_DRSUnbind on the original
    handle to free the server component, and it's client component.

Arguments:

    szServer (IN) - Server for which a context handle is desired.  This string
        buffer must be valid for the lifetime of the DRS_CONTEXT_INFO structure.

    pszDomainName (IN, OPTIONAL) - Domain name of the server; used to construct
        the SPN.  If the server name supplied is not a GUID-based DNS name and
        the pszDomainName is absent, one-way (not mutual) authentication will be
        performed.

    ulFlags (IN) - Zero or more of the following bits:
        FBINDSZDRS_NO_CACHED_HANDLES - Do not return cached context handles, and do
            not cache the fresh context handle returned from this function to
            be used by other threads.
        FBINDSZDRS_LOCK_HANDLE - Allows the use of cached handles which reduces the
            number of binds needed. But also ensures that this handle is exclusively
            owned by this thread till a DeReferenceHandle is called.

    eCallType (IN) - type of call being made on the context

    ppContextInfo (IN/OUT) - Should hold NULL before the first call.  On
        successful return, hold a valid context handle and extension set for the
        given server (amongst other state info).

    pdwStatus (IN OUT) - Detailed error code indicating why function failed
        If the error of this function is "no more bindings to try", we leave
        a non-success value of this error alone
        There is an assumption that if we run out of bindings, there must have
        been a previous error.

Return Values:

    TRUE on success, FALSE on failure.

--*/
{
    BOOL                fSuccess = FALSE;
    ULONG               bhflags=0;
    DRS_EXTENSIONS *    pextLocal = (DRS_EXTENSIONS *) gAnchor.pLocalDRSExtensions;

    Assert( pdwStatus );

    if (!gfIsDrsClientLibInitialized || eServiceShutdown) {
        // Either we've been stopped or we were never started.
        *pdwStatus = ERROR_DS_DRA_SHUTDOWN;
        return FALSE;
    }

    //
    // Get the correct binding handle cache flags
    //

    if (ulFlags & FBINDSZDRS_LOCK_HANDLE)
    {
        bhflags|=BHCACHE_FLAGS_LOCK_HANDLE;
    }

    __try {
        if (NULL == *ppContextInfo) {
            // This is our first request for a context handle.

            // Put it on the list first because the free routine expects it to there
	    //    more specifically, if *ppContextInfo is not null,
	    //    then the free routine expects it to be there.
            // Note that if somehow this thread exits without the context free routine
            // being called, this list will contain dangling thread memory 

            EnterCriticalSection(&gcsDrsRpcServerCtxList); 
	    __try {  
		if (eServiceShutdown) {
		    // uh oh, we shouldn't add ourselves now, because on shutdown this
		    // list is used to cancell all rpc threads, and that cancel routine
		    // might have already run.  So we'll have to leave now.  Since we 
		    // won't allocate *ppContextInfo, we'll bail right after leaving
		    // the critsec.
		    *pdwStatus = ERROR_DS_DRA_SHUTDOWN;
		} else {    
		    if (!gfDrsRpcServerCtxListInitialized) {
			InitializeListHead(&gDrsRpcServerCtxList);  
			Assert(0 == gcNumDrsRpcServerCtxListEntries);
			gfDrsRpcServerCtxListInitialized = TRUE;
		    }

		    // Check list sanity
		    Assert((gDrsRpcServerCtxList.Flink->Blink)==&gDrsRpcServerCtxList);
		    Assert((gDrsRpcServerCtxList.Blink->Flink)==&gDrsRpcServerCtxList);
		    
		    *ppContextInfo = THAllocOrgEx(pTHS, sizeof(DRS_CONTEXT_INFO));
		    (*ppContextInfo)->dstimeCreated = DBTime();
		    (*ppContextInfo)->dwThreadId = GetCurrentThreadId();
		    (*ppContextInfo)->eCallType = DRS_CALL_NO_CALL;
		    
		    InsertTailList(&gDrsRpcServerCtxList, &(*ppContextInfo)->ListEntry);
		    ++gcNumDrsRpcServerCtxListEntries;
		}
            }
            __finally {
		// if we allocated a ppContextInfo, we'd better be on the list.
		Assert((*ppContextInfo==NULL) || ((*ppContextInfo)->ListEntry.Flink != NULL));
		Assert((*ppContextInfo==NULL) || ((*ppContextInfo)->ListEntry.Blink != NULL));
		Assert((*ppContextInfo==NULL) || (((*ppContextInfo)->ListEntry.Flink->Blink)==&((*ppContextInfo)->ListEntry)));
		Assert((*ppContextInfo==NULL) || (((*ppContextInfo)->ListEntry.Blink->Flink)==&((*ppContextInfo)->ListEntry)));
		
		LeaveCriticalSection(&gcsDrsRpcServerCtxList);
            } 

	    if (*ppContextInfo==NULL) {
		Assert(*pdwStatus!=0);
		__leave;
	    }

            if ((NULL == &(pTHS->hThread)) || (INVALID_HANDLE_VALUE == pTHS->hThread)) {
                if (!DuplicateHandle(GetCurrentProcess(),
                                     GetCurrentThread(),
                                     GetCurrentProcess(),
                                     &(pTHS->hThread),
                                     0,
                                     FALSE,
                                     DUPLICATE_SAME_ACCESS)) {
                    *pdwStatus = GetLastError();
                    __leave;
                }
            } 
            (*ppContextInfo)->phThread = &(pTHS->hThread);

            // Note that we don't copy the server name; we assume the string
            // we were given is valid for the lifetime of the structure.
            (*ppContextInfo)->pszServerName = szServer;  

            if (!(ulFlags & FBINDSZDRS_NO_CACHED_HANDLES)
                && BHCacheGetDrsContext(pTHS,
                                        pextLocal,
                                        szServer,
                                        pszDomainName,
                                        bhflags,
                                        &(*ppContextInfo)->hDrs,
                                        &(*ppContextInfo)->ext)) {
                // Found a context handle in the cache -- we're done!
                Assert(NULL != (*ppContextInfo)->hDrs);
                (*ppContextInfo)->ulFlags = ulFlags;
                (*ppContextInfo)->fIsHandleFromCache = TRUE;
                (*ppContextInfo)->fIsHandleInCache = TRUE;
                *pdwStatus = ERROR_SUCCESS; 
                fSuccess = TRUE;
                __leave;
            }
        }
        else {
            // We've tried (and failed) to use a context handle already.
            Assert((*ppContextInfo)->pszServerName == szServer);
            Assert(NULL != (*ppContextInfo)->hDrs);
            (*ppContextInfo)->eCallType = DRS_CALL_NO_CALL;
            (*ppContextInfo)->dwBindingTimeoutMins = 0;

            if (*pdwStatus == RPC_S_CALL_CANCELLED) {
                if ((*ppContextInfo)->fIsHandleInCache) {  
                    // void the handle in cache (so nobody else uses it after
                    // all current calls are through)
                    BHCacheVoidServer(szServer);
                } 

                // leave the previous failure intact
                __leave;
            }

            if (!(*ppContextInfo)->fIsHandleFromCache) {
                // Whatever RPC exception occurred did so while using a handle that
                // was not retrieved from the cache.  Fail the call.

                // Leave the previous failure status intact in *pdwStatus
                if (*pdwStatus == ERROR_SUCCESS) {
                    Assert( !"A previous non-success rpc exception should have occurred" );
                    *pdwStatus = RPC_S_NO_MORE_BINDINGS;
                }  
                __leave;
            } 

            // We're going to try to acquire a new context handle, but before we do
            // we need to free the reference to the cached handle we already have. 
            Assert((*ppContextInfo)->fIsHandleInCache);
	    
	    // Reset state.  Do this before we remove it from the cache, just in case
	    // it excepts after we remove it, but before these flags get set.  We'd
	    // rather have it leak memory in the cache (at worst case) then AV from
	    // a double free.
            (*ppContextInfo)->fIsHandleFromCache = FALSE;
            (*ppContextInfo)->fIsHandleInCache   = FALSE;
            
	    BHCacheFreeContext(szServer, &((*ppContextInfo)->hDrs));
            Assert((*ppContextInfo)->hDrs == NULL);
            BHCacheVoidServer(szServer);
        }

        Assert(NULL != *ppContextInfo);
        Assert(NULL == (*ppContextInfo)->hDrs);
        Assert(!(*ppContextInfo)->fIsHandleFromCache);
        Assert(!(*ppContextInfo)->fIsHandleInCache);

        fSuccess = getContextBindingHelper( pTHS,szServer,pszDomainName,
                                            ulFlags,ppContextInfo,pdwStatus);
    } __finally {
        // If an exception is raised, guarantee that context is cleaned up
        // Note that callers of this function must also guarantee that the context
        // is cleaned up in all cases before the thread terminates
        if (AbnormalTermination()) {
            DRSFreeContextInfo(pTHS, ppContextInfo);
        }
    }
    return(fSuccess);
}


BOOL
getContextBindingHelper(
    IN      THSTATE *           pTHS,
    IN      LPWSTR              szServer,
    IN      LPWSTR              pszDomainName,  OPTIONAL
    IN      ULONG               ulFlags,
    IN OUT  DRS_CONTEXT_INFO ** ppContextInfo,
    IN OUT  DWORD *             pdwStatus
    )

/*++

Routine Description:

    Description

Arguments:

    pTHS - thread state
    szServer - server name
    pszDomainName - domain name, optional
    ulFlags - binding flags
    ppContextInfo - context structure, updated
    pdwStatus - status code returned

Return Value:

    None

--*/

{
    ULONG               draErr;
    ULONG               rpcStatus;
    ULONG               i;
    BOOL                fSuccess = FALSE;
    RPC_BINDING_HANDLE  hRpc = NULL;
    DRS_HANDLE          hDrs = NULL;
    DRS_EXTENSIONS *    pextRemote = NULL;
    DWORD               cchServer;
    UUID *              puuidDsa;
    LPWSTR              pszServerPrincName = NULL;
    VOID *              pvAddr;
    ULONG               ul1, ul2;
    BOOL                fUseMutualAuthSpn = TRUE;
    BOOL                fImpersonate = FALSE;
    DRS_EXTENSIONS *    pextLocal = (DRS_EXTENSIONS *) gAnchor.pLocalDRSExtensions;

    puuidDsa = (NULL == gAnchor.pDSADN) ? NULL : &gAnchor.pDSADN->Guid;

getbindingretry:

    if (!gfIsDrsClientLibInitialized || eServiceShutdown) {
        // Either we've been stopped or we were never started.
        *pdwStatus = ERROR_DS_DRA_SHUTDOWN;
        return FALSE;
    }

    draErr = DRSImpersonateInstallClient(&fImpersonate);
    if (ERROR_SUCCESS != draErr) {
        *pdwStatus = draErr;
        return FALSE;
    }

    // Attempt to create a binding handle, and, if successful, use it to acquire
    // a DRS context handle.
    _try {
        rpcStatus = DRSGetRpcBinding(pTHS,
                                     szServer,
                                     pszDomainName,
                                     fUseMutualAuthSpn,
                                     gfUseUniqueAssoc,
                                     &hRpc,
                                     &pszServerPrincName);
        if (RPC_S_OK == rpcStatus) {
            // set the time to cancel this rpc call 
            rpcStatus = RpcBindingSetOption(hRpc, RPC_C_OPT_CALL_TIMEOUT, gulDrsRpcBindTimeoutInMins*60*1000);
            if (RPC_S_OK != rpcStatus) {
                DRA_EXCEPT(rpcStatus,0); 
            }
            (*ppContextInfo)->eCallType = DRS_CALL_BIND;
            (*ppContextInfo)->dwBindingTimeoutMins = gulDrsRpcBindTimeoutInMins;

            if ( FBINDSZDRS_CRYPTO_BIND & ulFlags )
                {
                // Register the callback with RPC which will ultimately
                // set the SESSION_KEY in our thread state on return
                // from _IDL_DRSBind.
                InitRpcSessionEncryption(pTHS, ulFlags, NULL, hRpc);
            }

            __try { 
                draErr = _IDL_DRSBind(hRpc,
                                      puuidDsa,
                                      pextLocal,
                                      &pextRemote,
                                      &hDrs);
                MAP_SECURITY_PACKAGE_ERROR( draErr );

                if ( !draErr && (FBINDSZDRS_CRYPTO_BIND & ulFlags) )
                    {
                    if ( !IS_DRS_EXT_SUPPORTED(pextRemote,
                                               DRS_EXT_CRYPTO_BIND) ) {
                        draErr = DRAERR_NotSupported;
                    } else {
                        // Should have SESSION_KEY in thread state now.
                        Assert(    pTHS->SessionKey.SessionKey
                                   && pTHS->SessionKey.SessionKeyLength);
                    }
                }
            } __except(GetExceptionData(GetExceptionInformation(),
                                        &rpcStatus, &pvAddr, &ul1, &ul2)) {
                  MAP_SECURITY_PACKAGE_ERROR( rpcStatus );
                  // uncaught rpc error, log extended info if possible
                  LogRpcExtendedErrorInfo(pTHS, rpcStatus, szServer, DSID(FILENO, __LINE__));
                  Assert( rpcStatus != RPC_S_OK );
            } // end try/except
            
        // Error code used to be here.

        } // if (RPC_S_OK)

    } __finally {
        if (fImpersonate) {
            RevertToSelf();
        }
    }

    // Service Principal Name cannot be found on the target
    if ( (rpcStatus == ERROR_WRONG_TARGET_NAME) && (fUseMutualAuthSpn) ) {
        BOOL fFallback = 0;
        GetConfigParam(DRA_SPN_FALLBACK, &fFallback, sizeof(BOOL));
        if (fFallback) {
            fUseMutualAuthSpn = FALSE;
            if (NULL != hRpc) {
                RpcBindingFree(&hRpc);
            }
            if (NULL != pszServerPrincName) {
                free(pszServerPrincName);
                pszServerPrincName = NULL;
            }
            goto getbindingretry;
        } else {
            DPRINT3( 0, "Got error %d binding to %ws using spn %ws;\n"
                     "target server does not have or support mutual auth spn;\n"
                     "Check if server and domain specified correctly;\n"
                     "Verify that SPN is present on source or call Chandans;\n"
                     "set registry key to allow falling back to one way authentication.\n",
                     rpcStatus, szServer, pszServerPrincName );
            LogEvent(DS_EVENT_CAT_RPC_CLIENT,
                     DS_EVENT_SEV_ALWAYS,
                     DIRLOG_DRA_SPN_WRONG_TARGET_NAME,
                     szInsertWC(szServer),
                     szInsertWC(pszServerPrincName),
                     NULL );
        }
    }

    if (RPC_S_OK != rpcStatus) {
        // This is a first-leg error
        *pdwStatus = rpcStatus;

        // Failed to acquire new DRS context handle.
        // there is no extended rpc info here, if an
        // expception warranted error occured (which 
        // have eeinfo) it was thrown out of this function
        if (DsaIsInstalling()) {
            // Log install failure.
            LogEvent(DS_EVENT_CAT_SETUP,
                     DS_EVENT_SEV_ALWAYS,
                     DIRLOG_RPC_CONNECTION_FAILED,
                     szInsertWC(szServer),
                     szInsertUL(rpcStatus),
                     szInsertWin32Msg(rpcStatus));
        }
        else if (RPC_S_CALL_CANCELLED == rpcStatus) {
            if (!eServiceShutdown) { 
                // Log non-install RPC cancellation.
                LogEvent8(DS_EVENT_CAT_RPC_CLIENT,
                          DS_EVENT_SEV_ALWAYS,
                          DIRLOG_DRA_DISPATCHER_CANCELED,
                          szInsertHex(GetCurrentThreadId()),
                          szInsertWC(szServer),
                          szInsertUL(gulDrsRpcBindTimeoutInMins),
                          szInsertHex(DSID(FILENO,__LINE__)),  
                          NULL,NULL,NULL,NULL); 
            } else {
                LogEvent(DS_EVENT_CAT_RPC_CLIENT,
                          DS_EVENT_SEV_MINIMAL,
                          DIRLOG_DRA_DISPATCHER_CANCELED_SHUTDOWN,
                          szInsertHex(GetCurrentThreadId()),
                          szInsertWC(szServer),
                          szInsertHex(DSID(FILENO,__LINE__))); 
            }
        }
        else {
            // Log other non-install failure.
            LogEvent(DS_EVENT_CAT_RPC_CLIENT,
                     DS_EVENT_SEV_EXTENSIVE,
                     DIRLOG_DRA_GET_RPC_HANDLE_FAILURE,
                     szInsertWC(szServer),
                     szInsertUL(rpcStatus),
                     szInsertWin32Msg(rpcStatus));
        }
    }
    else if (DRAERR_Success == draErr) {
        // Acquired new DRS context handle.
        Assert(NULL != hDrs);
 
        if (!(ulFlags & FBINDSZDRS_NO_CACHED_HANDLES)
            && (NULL != pszServerPrincName)
            && IS_DOMAIN_QUALIFIED_SPN(pszServerPrincName)) {
            // Cache our context handle.
            
            // Note that we do not cache non-domain-qualified SPNs -- i.e.,
            // those without @domain.com tacked on the end.  This is to protect
            // against having code paths that require DQSPNs (like GC lookup,
            // to avoid infinite recursion) from re-using contexts based on
            // NDQSPNs (which may require a re-bind under the covers using the
            // NDQSPN -- e.g., if the Kerberos ticket has expired).
            //
            // In the ideal world we would cache (and look up) contexts based on
            // server name / domain name / SPN triples, which would guarantee
            // the right behavior yet allow caching of any SPN form.  This would
            // be a performance hit today, though, as we have not yet calculated
            // the SPN when we perform the context cache lookup and deriving the
            // SPN is potentially expensive (requiring a transaction and
            // several database reads).

            i = UlBHCacheHash(szServer);
            EnterCriticalSection(&csBHCache);
            __try {
                VALIDATE_BH_ENTRY(i);
    
                cchServer = 1 + wcslen(szServer);
    
                // Update cache if caching is desired and entry is available.
                if ((NULL == rgBHCache[i].pszServer)
                    && (NULL != (rgBHCache[i].pszServer
                                 = malloc(cchServer * sizeof(WCHAR)))) ) {
                    RPC_STATUS rpcStatusTemp = RPC_S_OK; 
                    // we want to save away the handle in the cache, 
                    // and return only a copy of the handle to the caller.
                    // The handle in cache is not directly used by any
                    // client - they all use copies of the handle.  This
                    // handle still needs to be referenced counted.  If 
                    // any client unbinds any copy or the original, all copies
                    // and the original become invalid handles.  All handles
                    // need to be freed explicitly, except 
                    // the handle which unbinds on the server (calls _IDL_UNBIND).
                    // in this case, only the handle in cache is used to unbind
                    // the server (from BHCacheDecRef when there are no more users).

                    // RpcBindingCopy has been overloaded to use context handles, but
                    // RpcBindingFree hasn't been overloaded to free these copies, 
                    // instead we use RpcSsDestroyClientContext.
                    rgBHCache[i].hDrs = hDrs;
                    hDrs = 0;
                    rpcStatusTemp = RpcBindingCopy(rgBHCache[i].hDrs, &hDrs);
                    // if we aren't able to copy the handle for some reason, it's not fatal
                    // we just won't cash this at all.  We need to remember to free the malloc'ed
                    // memory above if we do.
                    if (rpcStatusTemp!=RPC_S_OK) {
                        hDrs = rgBHCache[i].hDrs;
                        rgBHCache[i].hDrs = 0;
                        free(rgBHCache[i].pszServer);
                        rgBHCache[i].pszServer = NULL;
                        // don't cache.
                    } else {  
                        // cache.
                        rgBHCache[i].cchServer = cchServer;
                        memcpy(rgBHCache[i].pszServer,
                               szServer,
                               rgBHCache[i].cchServer * sizeof(WCHAR));
                        CopyExtensions(pextRemote, &rgBHCache[i].extRemote);
                        CopyExtensions(pextLocal, &rgBHCache[i].extLocal);
                        
                        // put copy of handle in cache.   
                        rgBHCache[i].pszServerPrincName = pszServerPrincName;
                        // One ref to keep the cache entry alive.
                        // One ref for the current caller.
                        rgBHCache[i].cRefs = 2; 
                        rgBHCache[i].fDontUse = FALSE;
                        // Lock the handle if required
                        if (ulFlags & FBINDSZDRS_LOCK_HANDLE)
                            {
                            rgBHCache[i].fLocked = TRUE;
                        }
                        (*ppContextInfo)->fIsHandleInCache = TRUE;  
                    }
                }
            }
            __finally {
                VALIDATE_BH_ENTRY(i);
                LeaveCriticalSection(&csBHCache);
            }
        } 

        // Update caller.
        (*ppContextInfo)->hDrs = hDrs; 
        (*ppContextInfo)->ulFlags = ulFlags;
        CopyExtensions(pextRemote, &(*ppContextInfo)->ext);
        if (NULL != pextRemote) {
            MIDL_user_free(pextRemote);
        }

        fSuccess = TRUE;
        *pdwStatus = ERROR_SUCCESS;
    } else {
        // This is a second-leg error
        // This is a rare occurance since IDL_BIND does not ususally fail
        *pdwStatus = draErr; 
        if (hDrs) { 
            DRSInsertFreeHandleQueue(szServer, hDrs);
            hDrs = NULL;
        }
        if (pextRemote!=NULL) {
            MIDL_user_free(pextRemote);
        } 
        Assert( !fSuccess );
    }

    if ((!(*ppContextInfo)->fIsHandleInCache) && (pszServerPrincName!=NULL)) {
        // free the principle name since we are not caching it
        free(pszServerPrincName);
        pszServerPrincName=NULL;
    }

    if (NULL != hRpc) {
        // Free the RPC binding handle whether we successfully retrieved a
        // context handle or not -- it's no longer needed.
        RpcBindingFree(&hRpc);
    }

    Assert(NULL == hRpc);
    Assert(fSuccess || (NULL == hDrs));
    Assert( (*ppContextInfo)->fIsHandleInCache || (pszServerPrincName==NULL) );

    return fSuccess;
}

void
DRSExpireServerCtxHandles(
    void
    )
/*
 Routine Description:
    Scan the context handle cache and expire any handles that haven't been used
    recently.  This is particularly important for the intersite demand-dial case
    since closing the handle prevents RPC keep alives from being sent, which in
    turn allows the dial-up line to go idle and be automcatically disconnected.
*/
{
    DWORD     i;
    GUID *    pLocalSiteGuid;
    GUID *    pRemoteSiteGuid;
    BOOL      fSameSite;
    DWORD     cMaxTicksSinceLastUse;
    DWORD     cTicksDiff;
    DWORD     cTickNow = GetTickCount();

    pLocalSiteGuid = gAnchor.pSiteDN ? &gAnchor.pSiteDN->Guid : NULL;

    EnterCriticalSection(&csBHCache);
    __try {
        for (i = 0; i < BHCacheSize; i++) {
            VALIDATE_BH_ENTRY(i);

            if ((NULL == rgBHCache[i].pszServer)    // empty
                || rgBHCache[i].fDontUse            // already expired
                || (1 != rgBHCache[i].cRefs)) {     // in use
                continue;
            }

            pRemoteSiteGuid = SITE_GUID_FROM_DRS_EXT(&rgBHCache[i].extRemote);

                    // Note that we err on the side of "not same site" if
                // w    e can't tell for sure.
            fSameSite = (NULL != pLocalSiteGuid)
            && (NULL != pRemoteSiteGuid)
            && (0 == memcmp(pLocalSiteGuid,
                            pRemoteSiteGuid,
                            sizeof(GUID)));

            cMaxTicksSinceLastUse = fSameSite ? gulDrsCtxHandleLifetimeIntrasite
                : gulDrsCtxHandleLifetimeIntersite;
            cMaxTicksSinceLastUse *= 1000; // secs to msecs

            cTicksDiff = cTickNow - rgBHCache[i].cTickLastUsed;

            if ((cMaxTicksSinceLastUse > 0)
                && (cTicksDiff >= cMaxTicksSinceLastUse)) {
                // Stick a fork in it -- it's done.
                DPRINT3(2, "Expiring DRS context handle to %ls after %d min, %d sec.\n",
                        rgBHCache[i].pszServer,
                        cTicksDiff / (60 * 1000),
                        (cTicksDiff / 1000) % 60);
                BHCacheDecRef(&rgBHCache[i]);
                Assert(NULL == rgBHCache[i].pszServer);
            }
        }
    }
    __finally {
        LeaveCriticalSection(&csBHCache);
    }
}

void
DRSExpireContextHandles(
    IN  VOID *  pvArg,
    OUT VOID ** ppvNextArg,
    OUT DWORD * pcSecsUntilNextRun
    )
/*++

Routine Description:

   
Arguments/Return Values:

    Typical task queue function signature.

--*/
{
    __try {
            // call client and server expire
        DRSExpireServerCtxHandles();
        DRSExpireClientCtxHandles();
    }
    __finally{
        *pcSecsUntilNextRun = gulDrsCtxHandleExpiryCheckInterval;
    }
}

void
DRSFreeContextInfo(
    IN      THSTATE           * pTHS,
    IN OUT  DRS_CONTEXT_INFO ** ppContextInfo
    )
/*++

Routine Description:

    Free context handle data returned by a prior call to FBindSzDRS().

Arguments:

    ppContextInfo (IN/OUT) - The context handle data to free.

Return Values:

    None.

--*/
{
    if (NULL == *ppContextInfo) {
        return;
    }

    Assert( (*ppContextInfo)->dwThreadId == GetCurrentThreadId() );

    EnterCriticalSection(&gcsDrsRpcServerCtxList);
    __try {
        // Assert that we are still on the list!
        Assert((*ppContextInfo)->ListEntry.Flink != NULL);
        Assert((*ppContextInfo)->ListEntry.Blink != NULL);
        Assert(((*ppContextInfo)->ListEntry.Flink->Blink)==&((*ppContextInfo)->ListEntry));
        Assert(((*ppContextInfo)->ListEntry.Blink->Flink)==&((*ppContextInfo)->ListEntry));

        RemoveEntryList(&(*ppContextInfo)->ListEntry);
        (*ppContextInfo)->ListEntry.Flink = NULL;
        (*ppContextInfo)->ListEntry.Blink = NULL;

        gcNumDrsRpcServerCtxListEntries--;
    }
    __finally {
        LeaveCriticalSection(&gcsDrsRpcServerCtxList);
    }

    __try {
        if ((*ppContextInfo)->fIsHandleInCache) {
            // Context handle is in the cache; decrement its refcount.
            BHCacheFreeContext((*ppContextInfo)->pszServerName,
                               &(*ppContextInfo)->hDrs);
            Assert((*ppContextInfo)->hDrs == NULL);
        }
        else { 
            // Context handle is not cached; free it outright.

            DRSInsertFreeHandleQueue((*ppContextInfo)->pszServerName,
                                     (*ppContextInfo)->hDrs);
        } 
    } __finally {
        // Under low memory conditions, it is possible for BHCacheFreeContext or
        // DRSInsertFreeHandleQueue to raise exceptions. Higher layers use *ppContextInfo
        // as an indicator to call this routine again.  To prevent this routine from
        // running again on a partially freed structure, we guarantee to mark the
        // structure as released in all conditions.
        memset( *ppContextInfo, 0, sizeof( DRS_CONTEXT_INFO ) );
        THFreeOrg(pTHS, *ppContextInfo);
        *ppContextInfo = NULL;
    }
}

DWORD
DRSFlushKerberosTicketCache()
/*++

Routine Description:

    Flush the kerberos ticket cache of all tickets. 
    Stolen from \ds\security\protocols\kerberos\ssp\ssptest.c

Arguments:

    pTHS -

Return Value:

    win32 error codes

--*/

{
    NTSTATUS Status;
    BOOLEAN WasEnabled;
    STRING Name;
    ULONG Dummy;
    HANDLE LogonHandle = NULL;
    ULONG PackageId;
    PKERB_QUERY_TKT_CACHE_RESPONSE Response;
    ULONG ResponseSize;
    NTSTATUS SubStatus;

    PKERB_PURGE_TKT_CACHE_REQUEST CacheRequest = NULL;
    USHORT Index;

    THSTATE * pTHS=pTHStls;

    //
    // Turn on the TCB privilege
    //

    Status = RtlAdjustPrivilege(SE_TCB_PRIVILEGE, TRUE, FALSE, &WasEnabled);
    if (!NT_SUCCESS(Status))
    {
        Assert(!"RtlAdjustPrivilege Failed!\n");
        return RtlNtStatusToDosError(Status);
    }
    __try {

        RtlInitString(
            &Name,
            "DS Replication"
            );

        Status = LsaRegisterLogonProcess(
            &Name,
            &LogonHandle,
            &Dummy
            );

        if (!NT_SUCCESS(Status))
            {
            Assert(!"Failed to register as a logon process!\n");
            __leave;
        }
        __try {

            RtlInitString(
                &Name,
                MICROSOFT_KERBEROS_NAME_A
                );

            Status = LsaLookupAuthenticationPackage(
                LogonHandle,
                &Name,
                &PackageId
                );

            if (!NT_SUCCESS(Status)) {
                DPRINT2(1,"Failed to lookup package %Z: 0x%x\n",&Name, Status);
                Assert(!"LsaLookupAuthenticationPackage failed!\n");
            } else { 

                CacheRequest = (PKERB_PURGE_TKT_CACHE_REQUEST)THAllocEx(pTHS, sizeof(KERB_PURGE_TKT_CACHE_REQUEST)); 

                CacheRequest->MessageType = KerbPurgeTicketCacheMessage;

                DPRINT(0, "Deleting all Kerberos Tickets!\n");

                Status = LsaCallAuthenticationPackage(
                    LogonHandle,
                    PackageId,
                    CacheRequest,
                    sizeof(KERB_PURGE_TKT_CACHE_REQUEST),
                    &Response,
                    &ResponseSize,
                    &SubStatus
                    );

                Assert(Response==NULL);
                Assert(ResponseSize==0);

                if (!NT_SUCCESS(Status) || !NT_SUCCESS(SubStatus)) {
                    DPRINT2(0,"token failed: 0x%x, 0x %x\n",Status, SubStatus);
                    DPRINT2(0,"token failed: %d, %d\n", RtlNtStatusToDosError(Status), RtlNtStatusToDosError(SubStatus));
                    Status= NT_SUCCESS(Status) ? SubStatus : Status;
                }    
            }
        }
        __finally {
            SubStatus = LsaDeregisterLogonProcess(LogonHandle);
            if (!NT_SUCCESS(SubStatus)) {
                Assert(!"Unable to deregister logon process!\n");
                // don't return this since it's the success of the LsaCallAuthenticaitonPackage that
                // we're really concerned with.
            }
            if (CacheRequest) {
                THFreeEx(pTHS, CacheRequest);
            }
        }
    }
    __finally {
        if (!WasEnabled) {
            SubStatus = RtlAdjustPrivilege(SE_TCB_PRIVILEGE, FALSE, FALSE, &WasEnabled);
            if (!NT_SUCCESS(SubStatus)) {
                Assert(!"RtlAdjustPrivilege Failed!\n");
                // again, don't return this since the success of the LsaCallAuthPackage is 
                // the main concern.
            }
        } 
    }

    return RtlNtStatusToDosError(Status);
}



BOOL
drsIsCallComplete(
    IN  RPC_STATUS          rpcStatus,
    IN  ULONG               ulError,
    IN  DRS_CONTEXT_INFO *  pContextInfo
    )

/*++

Routine Description:

    Determine whether the result of an RPC call merits a retry with a fresh
    binding handle.

Arguments:

    rpcStatus - Status from RPC run-time, where applicable
    ulError - Status returned from server manager routine
    pContextInfo - Client-side context

Return Value:

    BOOL - TRUE - call should NOT be retried
           FALSE - call should be retried

--*/

{
    if (rpcStatus) {
        // RPC call was not completed.  If the last attempt was using a cached
        // handle, try again with a fresh handle, since the failure may have
        // been due to the cached handle no longer being valid.
        return !(pContextInfo->fIsHandleFromCache);
    }
    else if ((ERROR_ACCESS_DENIED == ulError)
             || (ERROR_REVISION_MISMATCH == ulError)) {
        // RPC call completed but the server side denied us access.
        // In this scenario, authentication wasn't successful.  If we
        // used a cached binding handle, try again with a fresh handle.
        
        // [wlees 12 Apr 00] Could also be that server has changed extensions
        // since we last bound.
        return !(pContextInfo->fIsHandleFromCache);
    } 
    else if (ERROR_DS_DRA_ACCESS_DENIED == ulError) {
        BOOL fUseUniqueAssoc = gfUseUniqueAssoc;
        DWORD err = ERROR_SUCCESS;
        // In this scenario, the authentication was successful -- i.e., the
        // IDL_DRSBind call succeeded, ergo the machine account for the local
        // DC (or whatever account was used for authentication) is present on
        // the remote DC.  However, the token derived for this account by the
        // remote DC from the Kerberos ticket does not have sufficient
        // privileges to perfrom the requested operation, possibly because the
        // Kerberos ticket does not yet include the latest group memberships
        // for this account, or this account is having some of it's group memberships
        // filtered in the trust path.
        //
        // If we assume that the authorization failure was based upon some missing
        // information, when the server aquires that information (which puts us in
        // a different group, or gives that group different rights) we need to 
        // update the security context we are using.  In order to update this,
        // we need to purge our kerberos ticket cache to that server, and 
        // use a unique rpc association to bind to the server.  Unforetunately, we
        // can never be sure that the default rpc association EVER has refreshed
        // security information since it depends upon being closed and reopened to 
        // refresh and ANY other tcp rpc calls from this process to the same
        // server process could keep the association open indefinately.
 

        // if we are beginning to use unique associations here, log a message
        if (!gfUseUniqueAssoc) {
            DPRINT(0,"Switching to use unique RPC associations for the life of this process!\n");
            LogEvent(DS_EVENT_CAT_RPC_CLIENT,
                      DS_EVENT_SEV_BASIC,
                      DIRLOG_DRA_BEGIN_UNIQUE_RPC_ASSOC,
                      NULL,NULL,NULL);
        }

        gfUseUniqueAssoc = TRUE;  

        err = DRSFlushKerberosTicketCache(); 

        if (err != ERROR_SUCCESS) {
            // This is a bad place to fail.  Log that we can't flush the
            // cache and return TRUE.  If we can't flush the cache then there is
            // no reason to try again, we will get the same result.  In (default) 8
            // hours the cache will expire and we'll get through then.   

            LogEvent(DS_EVENT_CAT_RPC_CLIENT,
                     DS_EVENT_SEV_MINIMAL,
                     DIRLOG_DRA_KERB_CACHE_PURGE_FAILURE,
                     szInsertWin32Msg(err),
                     szInsertUL(err),NULL);

            return TRUE;
        } 

        return (fUseUniqueAssoc && !(pContextInfo->fIsHandleFromCache));
    }
    else {
        // RPC call completed with success or a non-retriable error.
        return TRUE;
    }
} /* drsIsCallComplete */

BOOL
DRSIsRegisteredAsyncRpcState(
    IN  DRS_ASYNC_RPC_STATE *   pAsyncState
    )
/*++

Routine Description:

    Scans the list of registered async state structures to determine if the
    given async state is present.

Arguments:

    pAsyncState (IN) - Async RPC state to look for.

Return Values:

    TRUE - pAsyncState is registered.
    FALSE - pAsyncState is not registered.

--*/
{
    LIST_ENTRY * pListEntry;
    LIST_ENTRY * pTargetListEntry = &pAsyncState->ListEntry;
    BOOL fFound = FALSE;

    // Note that pAsyncState must not be dereferenced -- assume it can be a
    // random pointer.
    RtlEnterCriticalSection(&gcsDrsAsyncRpcListLock);
    __try {
        for (pListEntry = gDrsAsyncRpcList.Flink;
             pListEntry != &gDrsAsyncRpcList;
             pListEntry = pListEntry->Flink) {
            if (pListEntry == pTargetListEntry) {
                fFound = TRUE;
                break;
            }
        }
    } __finally {
        RtlLeaveCriticalSection(&gcsDrsAsyncRpcListLock);
    }

    return fFound;
}

void
drsZeroAsyncOutParameters(
    IN  DRS_ASYNC_RPC_STATE *   pAsyncState
    )
/*++

Routine Description:

    Zero-fill OUT parameters associated with the given async RPC call.  This
    forces RPC to allocate new buffers for returned data, rather than
    attempting to re-use previous buffers (which we have likely invalidated
    via TH_free_to_mark or never initialized in the first place).

Arguments:

    pAsyncState (IN) - Async RPC call info.

Return Values:

    None.

--*/
{
    switch (pAsyncState->CallType) {
    case DRS_ASYNC_CALL_GET_CHANGES:
        memset(pAsyncState->CallArgs.GetChg.pmsgOut,
               0,
               sizeof(*pAsyncState->CallArgs.GetChg.pmsgOut));
        break;

    default:
        Assert(!"Unknown CallType!");
        DRA_EXCEPT(DRAERR_InternalError, pAsyncState->CallType);
    }
}

void
drsCancelAsyncRpc(
    IN OUT  DRS_ASYNC_RPC_STATE *   pAsyncState
    )
/*++

Routine Description:

    Cancel an async RPC call that's currently in progress.

Arguments:

    pAsyncState (IN/OUT) - Async RPC call to cancel.

Return Values:

    None.

--*/
{
    DWORD waitStatus;
    DWORD err;
    RPC_STATUS rpcStatus = RPC_S_OK;

    Assert(pAsyncState->fIsCallInProgress);

    __try {
        // Tell RPC to abort the call.
        err = RpcAsyncCancelCall(&pAsyncState->RpcState, TRUE);
        Assert(RPC_S_OK == err);

        // We never seed RPC with pre-allocated buffers for out parameters.
        drsZeroAsyncOutParameters(pAsyncState);

        // Wait for aborted call to terminate.
        waitStatus = WaitForSingleObject(pAsyncState->RpcState.u.hEvent,
                                         INFINITE);
        if (WAIT_OBJECT_0 != waitStatus) {
            err = GetLastError();
            LogUnhandledError(waitStatus);
            LogUnhandledError(err);
            DPRINT2(0, "Wait for async RPC completion failed (%d, GLE %d)",
                    waitStatus, err);
            Assert(!"Wait for async RPC completion failed -- contact DsRepl");
        }

        // Call completed.  Retrieve exit code and out parameters.
        // Note that this is required even though we cancelled the call.
	rpcStatus = RpcAsyncCompleteCall(&pAsyncState->RpcState, &err);
	// Yes, we are ignoring the output.  Cancelling a distributed call is tricky business.
	// There is no guarentee that we'll get there in time to stop it from completing 
	// successfully.  What about other errors?  Sucess or failure, there isn't anything
	// more we can do.  Esentially, the call is the server's problem now - we tried.
    } __except (DRSHandleRpcClientException(
                    GetExceptionInformation(),
                    pAsyncState->pContextInfo ?
                    pAsyncState->pContextInfo->pszServerName : L"",
                    pAsyncState->pContextInfo,
                    *rgCallTypeTable[pAsyncState->CallType].pcNumMinutesUntilRpcTimeout,
                    &rpcStatus)) {
        DPRINT1(0, "Exception 0x%x caught in drsCancelAsyncRpc!\n", rpcStatus);
        // Nothing to do. Ignore the exception.
    }

    pAsyncState->fIsCallInProgress = FALSE;
}

void
DRSDestroyAsyncRpcState(
    IN      THSTATE *               pTHS,
    IN OUT  DRS_ASYNC_RPC_STATE *   pAsyncState
    )
/*++

Routine Description:

    Free all the resources associated with a DRS_ASYNC_RPC_STATE structure.

    This function is designed to gracefully handle being called on a empty/freed
    DRS_ASYNC_RPC_STATE structures, so no need to worry too much about e.g.
    calling DRSDestroyAsyncRpcState twice.  (Just make sure you call it at least
    once!)

Arguments:

    pTHS (IN)

    pAsyncState (IN/OUT) - Async RPC state to destroy.

Return Values:

    None.

--*/
{
    RPC_STATUS  rpcStatus;
    SESSION_KEY SessionKeyAtStart;

    // Save session key so we can restore it when we're done.
    PEKSaveSessionKeyForMyThread(pTHS, &SessionKeyAtStart);

    if (pAsyncState->fIsCallInProgress) {
        drsCancelAsyncRpc(pAsyncState);
        Assert(!pAsyncState->fIsCallInProgress);
    }

    if (NULL != pAsyncState->RpcState.u.hEvent) {
        CloseHandle(pAsyncState->RpcState.u.hEvent);
        pAsyncState->RpcState.u.hEvent = NULL;
    }

    if (NULL != pAsyncState->pContextInfo) {
        DRSFreeContextInfo(pTHS, &pAsyncState->pContextInfo);
        Assert(NULL == pAsyncState->pContextInfo);
    }

    if (NULL != pAsyncState->SessionKey.SessionKey) {
        Assert(0 != pAsyncState->SessionKey.SessionKeyLength);
        PEKDestroySessionKeySavedByDiffThread(&pAsyncState->SessionKey);
    }

    RtlEnterCriticalSection(&gcsDrsAsyncRpcListLock);
    __try {
        if (NULL != pAsyncState->ListEntry.Flink) {
            Assert(NULL != pAsyncState->ListEntry.Blink);
            RemoveEntryList(&pAsyncState->ListEntry);
            pAsyncState->ListEntry.Flink = NULL;
            pAsyncState->ListEntry.Blink = NULL;
        }
    } __finally {
        RtlLeaveCriticalSection(&gcsDrsAsyncRpcListLock);
    }

    // Erase the structure.
    memset(pAsyncState, 0, sizeof(*pAsyncState));

    DPRINT1(1, "Destroyed async RPC state %p.\n", pAsyncState);

    // Exit with the same session key we entered with.
    PEKRestoreSessionKeySavedByMyThread(pTHS, &SessionKeyAtStart);
}

void
drsPrepareAsyncRpcState(
    IN  THSTATE *               pTHS,
    IN  LPWSTR                  pszServerName,
    IN  LPWSTR                  pszDomainName   OPTIONAL,
    IN  DRS_CALL_TYPE           CallType,
    OUT DRS_ASYNC_RPC_STATE *   pAsyncState
    )
/*++

Routine Description:

    Initializes async RPC state for a forthcoming async RPC call.

    This routine may be invoked multiple times on a given async state structure
    without an intervening DRSDestroyAsyncRpcState.  Doing so cancels the async
    RPC call in progress (if any) and prepares for a new async RPC call.

    The caller must eventually call DRSDestroyAsyncRpcState to free the
    resources associated with this structure.

Arguments:

    pTHS (IN)

    pszServerName (IN) - Name of the RPC server (e.g., replication source for a
        GetChanges call).  The buffer for this string must remain valid until
        DRSDestroyAsyncRpcState is invoked -- i.e., it is saved by reference,
        not copied.

    pszDomainName (IN, OPTIONAL) - DNS name of the domain to which pszServerName
        belongs.  Should be NULL if pszServerName is a GUID-based DNS name, may
        be NULL or non-NULL for other names.  The buffer for this string must 
        remain valid until DRSDestroyAsyncRpcState is invoked -- i.e., it is
        saved by reference, not copied.

    CallType (IN) - Identifies the forthcoming async RPC call (e.g.,
        GetChanges).

    pAsyncState (OUT) - Async RPC state to prepare.

Return Values:

    None.  Throws exception on error.

--*/
{
    ULONG   ret;
    HANDLE  hEvent;
    BOOL    fResetEvent = FALSE;

    Assert(DRS_ASYNC_CALL_NONE != CallType);

    if (0 == pAsyncState->timeInitialized) {
        // First time initializing this DRS_ASYNC_RPC_STATE.  The structure
        // should be zeroed out, with the possible exception of pContextInfo.
        Assert(0 == pAsyncState->dwCallerTID);
        Assert(0 == pAsyncState->CallType);
        Assert(NULL == pAsyncState->CallArgs.pszServerName);
        Assert(NULL == pAsyncState->CallArgs.pszDomainName);
        Assert(NULL == pAsyncState->ListEntry.Flink);
        Assert(NULL == pAsyncState->ListEntry.Blink);
        Assert(NULL == pAsyncState->RpcState.u.hEvent);
        Assert(!pAsyncState->fIsCallInProgress);

        DPRINT1(1, "Preparing new async RPC state %p.\n", pAsyncState);
    }
    else {
        // Not the first time initializing this DRS_ASYNC_RPC_STATE.
        Assert(GetCurrentThreadId() == pAsyncState->dwCallerTID);
        Assert(CallType == pAsyncState->CallType);
        Assert(pszServerName == pAsyncState->CallArgs.pszServerName);
        Assert(pszDomainName == pAsyncState->CallArgs.pszDomainName);
        Assert(NULL != pAsyncState->ListEntry.Flink);
        Assert(NULL != pAsyncState->ListEntry.Blink);
        Assert(NULL != pAsyncState->RpcState.u.hEvent);
        Assert(!pAsyncState->fIsCallInProgress);

        if (NULL != pAsyncState->SessionKey.SessionKey) {
            Assert(0 != pAsyncState->SessionKey.SessionKeyLength);
            PEKDestroySessionKeySavedByDiffThread(&pAsyncState->SessionKey);
        }

        DPRINT1(1, "Re-preparing existing async RPC state %p.\n", pAsyncState);
    }

    __try {
        hEvent = pAsyncState->RpcState.u.hEvent;
        if (NULL == hEvent) {
            // Create event for RPC to signal when the response has been
            // received.
            hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
            if (NULL == hEvent) {
                ret = GetLastError();
                DRA_EXCEPT(ret, 0);
            }
        }
        else {
            // Already have an event -- make sure it's reset later.
            // Can't reset now, as e.g. cancelling a call in progress may set
            // the event.
            fResetEvent = TRUE;
        }

        // Init RPC portion.
        if (pAsyncState->fIsCallInProgress) {
            drsCancelAsyncRpc(pAsyncState);
            Assert(!pAsyncState->fIsCallInProgress);
        }
        ret = RpcAsyncInitializeHandle(&pAsyncState->RpcState,
                                       sizeof(RPC_ASYNC_STATE));
        if (ret) {
            DRA_EXCEPT(ret, 0);
        }

        if (fResetEvent && !ResetEvent(hEvent)) {
            ret = GetLastError();
            DRA_EXCEPT(ret, 0);
        }

        pAsyncState->RpcState.NotificationType = RpcNotificationTypeEvent;
        pAsyncState->RpcState.u.hEvent = hEvent;
        hEvent = NULL;
    }
    __finally {
        if (AbnormalTermination()) {
            // Encountered exception while trying to initialize -- tear down
            // whatever portion we did manage to init.
            DRSDestroyAsyncRpcState(pTHS, pAsyncState);

            if (NULL != hEvent) {
                CloseHandle(hEvent);
            }
        }
    }

    if (0 == pAsyncState->timeInitialized) {
        // First time initializing this DRS_ASYNC_RPC_STATE.
        pAsyncState->timeInitialized = GetSecondsSince1601();
        pAsyncState->dwCallerTID = GetCurrentThreadId();
        pAsyncState->CallType = CallType;
        pAsyncState->CallArgs.pszServerName = pszServerName;
        pAsyncState->CallArgs.pszDomainName = pszDomainName;

        // Save async state on global linked list.
        RtlEnterCriticalSection(&gcsDrsAsyncRpcListLock);
        InsertTailList(&gDrsAsyncRpcList, &pAsyncState->ListEntry);
        RtlLeaveCriticalSection(&gcsDrsAsyncRpcListLock);
    }

    Assert(!pAsyncState->fIsCallInProgress);
    Assert(NULL != pAsyncState->RpcState.u.hEvent);
    Assert(RpcNotificationTypeEvent == pAsyncState->RpcState.NotificationType);
    Assert(0 == pAsyncState->SessionKey.SessionKeyLength);
    Assert(NULL == pAsyncState->SessionKey.SessionKey);
    Assert(GetCurrentThreadId() == pAsyncState->dwCallerTID);
    Assert(pszServerName == pAsyncState->CallArgs.pszServerName);
    Assert(pszDomainName == pAsyncState->CallArgs.pszDomainName);
    Assert(NULL != pAsyncState->ListEntry.Flink);
    Assert(NULL != pAsyncState->ListEntry.Blink);
    Assert(NULL != pAsyncState->RpcState.u.hEvent);

    // Assert(IsListEmpty(&gDrsAsyncRpcList)) on shutdown?
}

ULONG
drsWaitForAsyncRpc(
    IN      THSTATE *               pTHS,
    IN OUT  DRS_ASYNC_RPC_STATE *   pAsyncState,
    OUT     ULONG *                 pRet
    )
/*++

Routine Description:

    Waits for completion of a prior, outstanding async RPC call.

    The async RPC must have been posted by this same thread -- this is required
    by (RPC's) RpcAsyncCancelCall.

Arguments:

    pTHS (IN)

    pAsyncState (IN/OUT) - Async RPC state associated with oustanding async RPC
        call.

    pRet (OUT) - When this function returns, either there was no RPC error
        (indicating our function was invoked on the remote DSA), or there was
        an RPC error (in which case our function may or may not have executed on
        the remote DSA).  In the former case, this output parameter holds the
        return code of the server-side function we invoked on the remote DSA.
        In the latter case, this output parameter holds the RPC error code.

Return Values:

    0 or RPC error.  If the server-side function on the remote DSA was executed
    and the results returned to the local DSA, the return value is 0.  If there
    was any sort of error in reaching the remote DSA or transferring the data,
    the return value is non-0.

    Note that a return code of 0 *DOES NOT* indicate that the function we
    invoked on the remote DSA returned 0 -- that information is held in *pRet.
    The return code of drsWaitForAsyncRpc solely indicates whether there was
    an RPC communications error.

--*/
{
#if DBG
    static DWORD s_cNumTicksWaitedForReply = 0;
    static DWORD s_cNumReplies = 0;
    DWORD       tickStart;
    DWORD       cNumTicks;
#endif

    RPC_STATUS  rpcStatus;
    DWORD       waitStatus;
    ULONG       ret = 0;
    DWORD       cNumMSecToTimeout;
    HANDLE      rghWaitHandles[2];

    Assert(pAsyncState->fIsCallInProgress);
    Assert(GetCurrentThreadId() == pAsyncState->dwCallerTID);
    Assert(RpcNotificationTypeEvent == pAsyncState->RpcState.NotificationType);
    Assert(NULL != pAsyncState->RpcState.u.hEvent);

    // Sanity check CallType argument / table.
    Assert(DRS_ASYNC_CALL_NONE != pAsyncState->CallType);
    Assert(pAsyncState->CallType < DRS_ASYNC_CALL_MAX);
    Assert(pAsyncState->CallType < ARRAY_SIZE(rgCallTypeTable));
    Assert(pAsyncState->CallType
           == rgCallTypeTable[pAsyncState->CallType].CallType);

    __try {
        // We never seed RPC with pre-allocated buffers for out parameters.
        drsZeroAsyncOutParameters(pAsyncState);

        DPRINT1(1, "Waiting for completion of RPC call with state %p.\n", pAsyncState);
#if DBG
        tickStart = GetTickCount();
#endif
        // Asynchronous call has been initiated.  Wait for it to complete.
        cNumMSecToTimeout
            = *rgCallTypeTable[pAsyncState->CallType].pcNumMinutesUntilRpcTimeout
              * 60 * 1000;

        rghWaitHandles[0] = hServDoneEvent; // shutdown
        rghWaitHandles[1] = pAsyncState->RpcState.u.hEvent; // RPC completed

        waitStatus = WaitForMultipleObjects(ARRAY_SIZE(rghWaitHandles),
                                            rghWaitHandles,
                                            FALSE,
                                            cNumMSecToTimeout);
#if DBG
        cNumTicks = GetTickCount() - tickStart;
        s_cNumTicksWaitedForReply += cNumTicks;
        s_cNumReplies++;

        DPRINT3(1, "Async RPC call %p terminated; waited %d msec (%d msec avg).\n",
                pAsyncState,
                cNumTicks,
                s_cNumTicksWaitedForReply / s_cNumReplies);
#endif

        switch (waitStatus) {
        case WAIT_OBJECT_0:
            // DS is shutting down.
            DPRINT1(0, "Aborting async RPC call to %ls due to DS shutdown.\n",
                    pAsyncState->CallArgs.pszServerName);

            rpcStatus = ERROR_DS_SHUTTING_DOWN;
            break;

        case WAIT_OBJECT_0 + 1:
            // Call completed.  Retrieve exit code and out parameters.
            rpcStatus = RpcAsyncCompleteCall(&pAsyncState->RpcState, &ret);
            pAsyncState->fIsCallInProgress = FALSE;
            break;

        case WAIT_TIMEOUT:
            // RPC call timed out.
            DPRINT2(0, "Async RPC call to %ls timed out after %d minutes!\n",
                    pAsyncState->CallArgs.pszServerName,
                    *rgCallTypeTable[pAsyncState->CallType].pcNumMinutesUntilRpcTimeout);

            LogEvent8(DS_EVENT_CAT_RPC_CLIENT,
                      DS_EVENT_SEV_ALWAYS,
                      DIRLOG_DRA_DISPATCHER_VILLIAN,
                      szInsertWC(pAsyncState->CallArgs.pszServerName),
                      szInsertHex(pAsyncState->dwCallerTID),
                      szInsertDsMsg(rgCallTypeTable[pAsyncState->CallType].dwMsgID),
                      szInsertUL( *rgCallTypeTable[pAsyncState->CallType].pcNumMinutesUntilRpcTimeout),
                      NULL, NULL, NULL, NULL
                      );

            rpcStatus = RPC_S_CALL_CANCELLED;
            break;

        case WAIT_ABANDONED_0:
        case WAIT_ABANDONED_0 + 1:
        default:
            // An unexpected error occurred.
            ret = GetLastError();
            LogUnhandledError(waitStatus);
            LogUnhandledError(ret);
            DPRINT2(0, "Wait for async RPC completion failed (%d, GLE %d)",
                    waitStatus, ret);
            Assert(!"Wait for async RPC completion failed -- contact JeffParh");

            rpcStatus = ERROR_DS_INTERNAL_FAILURE;
            break;
        }

        if (rpcStatus) {
            DRA_EXCEPT(rpcStatus, 0);
        }

        if (ret) {
            // RPC call succeeded, but the server-side function returned an
            // error.
            __leave;
        }

        // Update THSTATE so pek code can match destination's capabilities.
        DraSetRemoteDsaExtensionsOnThreadState(pTHS,
                                               &pAsyncState->pContextInfo->ext);

        // Restore session key associated with this RPC call.
        Assert(0 != pAsyncState->SessionKey.SessionKeyLength);
        Assert(NULL != pAsyncState->SessionKey.SessionKey);
        PEKRestoreSessionKeySavedByDiffThread(pTHS, &pAsyncState->SessionKey);
    }
    __except (DRSHandleRpcClientException(GetExceptionInformation(),
                                          pAsyncState->pContextInfo->pszServerName,
                                          pAsyncState->pContextInfo,
                                          *rgCallTypeTable[pAsyncState->CallType].pcNumMinutesUntilRpcTimeout,
                                          &rpcStatus)) {
        ret = rpcStatus;
    }

    if (pAsyncState->fIsCallInProgress) {
	drsCancelAsyncRpc(pAsyncState);
	Assert(!pAsyncState->fIsCallInProgress);
    }	
     

    MAP_SECURITY_PACKAGE_ERROR(ret);

    *pRet = ret;

    DPRINT2(1, "Async RPC call %p returned status %d.\n", pAsyncState, ret);

    // If an RPC failure occurred, *pRet must indicate an error.
    Assert(!rpcStatus || *pRet);

    return rpcStatus;
}

ULONG
I_DRSReplicaSync(
    THSTATE *   pTHS,
    LPWSTR      pszServerName,
    DSNAME *    pNC,
    LPWSTR      pszFromServerName,
    UUID *      pInvocationId,
    ULONG       ulOptions
    )
{
    ULONG               ret = ERROR_SUCCESS;
    DRS_CONTEXT_INFO *  pContextInfo = NULL;
    BOOL                fCallCompleted = FALSE;
    DRS_MSG_REPSYNC     msgSync;
    MTX_ADDR *          pmtx = NULL;

    memset(&msgSync, 0, sizeof(msgSync));

    msgSync.V1.pNC       = pNC;
    msgSync.V1.ulOptions = ulOptions;

    if (NULL != pszFromServerName) {
        pmtx = MtxAddrFromTransportAddrEx(pTHS, pszFromServerName);
        msgSync.V1.pszDsaSrc = pmtx->mtx_name;
    }

    if (NULL != pInvocationId) {
        msgSync.V1.uuidDsaSrc = *pInvocationId;
    }
    
    while (!fCallCompleted
           && FBindSzDRS(pTHS, pszServerName, NULL, 0, &pContextInfo, &ret)) {
        __try {  
            DRSSetContextCancelTime(DRS_CALL_REPLICA_SYNC, gulDrsRpcReplicationTimeoutInMins, pContextInfo );

            ret = _IDL_DRSReplicaSync(pContextInfo->hDrs, 1, &msgSync); 

            fCallCompleted = drsIsCallComplete(0, ret, pContextInfo);
        } 
        __except (DRSHandleRpcClientException(GetExceptionInformation(),
                                              pszServerName,
                                              pContextInfo,
                                              gulDrsRpcReplicationTimeoutInMins,
                                              &ret)) {
            ;
        }       
    }

    MAP_SECURITY_PACKAGE_ERROR( ret );

    if (NULL != pmtx) {
        THFreeEx(pTHS, pmtx);
    }

    DRSFreeContextInfo(pTHS, &pContextInfo);

    return ret;
}

ULONG
I_DRSGetNCChanges(
                 IN  THSTATE *                     pTHS,
                 IN  LPWSTR                        pszServerName,
                 IN  LPWSTR                        pszServerDnsDomainName,     OPTIONAL
                 IN  DSNAME *                      pNC,
                 IN  DRS_MSG_GETCHGREQ_NATIVE *    pmsgInNative,
                 OUT DRS_MSG_GETCHGREPLY_NATIVE *  pmsgOutNative,
                 OUT PBYTE                         pSchemaInfo,
                 OUT DRS_ASYNC_RPC_STATE *         pAsyncState,                OPTIONAL
                 OUT BOOL *                        pfBindSuccess               OPTIONAL
                 )
/*++

Routine Description:

    Request replication changes from a source server over RPC.

Arguments:

    pTHS (IN)

    pszServerName (IN) - DNS name (GUID-based or other) of the source server.

    pszServerDnsDomainName (IN, OPTIONAL) - DNS name of the domain of the
        source server.

    pmsgInNative (IN) - The replication request.

    pmsgOut (OUT) - Holds the replication response after the source server
        has completed processing the request.

    pSchemaInfo (OUT) - Holds the schema signature of the source server after
        the source server has completed processing the request.

    pAsyncState (OUT, OPTIONAL) - If NULL, the traditional GetNCChanges
        behavior is observed -- i.e., if this function returns successfully,
        pmsgOutV1 holds the source server's response and pSchemaInfo holds the
        source server's schema signature.

        If non-NULL, this function returns immediately after posting the
        replication request to the source server -- i.e., it does not wait for
        the server to process the request and send the response.  Rather, the
        caller can perform other operations in the interim and later come back
        to retrieve the response via I_DRSGetNCChangesComplete, at which point
        pmsgOutV1 and pSchemaInfo will hold the returned data.  (Alternatively
        the caller can cancel via DRSDestroyAsyncRpcState.)  Note that the
        buffers pmsgOutV1, pSchemaInfo, and pAsyncState must remain valid until
        the caller invokes I_DRSGetNCChangesComplete or DRSDestroyAsyncRpcState.

        Note that in the non-NULL case the MIDL_user_allocate calls to allocate
        buffers for the unmarshalled data are not invoked until
        I_DRSGetNCChangesComplete is called -- these MIDL_user_allocates are
        *not* invoked by other threads asynchronously.  This is an important
        detail here, given the DS thread-based allocation model and
        ReplicateNC()'s alternating of heaps for GetChanges responses (i.e.,
        the TH_mark, TH_free_to_mark functionality).

        If non-NULL and this function returns successfully the caller MUST
        later call I_DRSGetNCChangesComplete or DRSDestroyAsyncRpcState.
        (Either will do but both are okay, too, as long as
        DRSDestroyAsyncRpcState is called last.)

     pfBindSuccess (OUT, OPTIONAL) - Allows the caller to know if a successful
        bind was made to the server. If this routine has to retry, the status
        of the last bind is returned

Return Values:

    0 or Win32 error code.

--*/
{
    ULONG                   ret = ERROR_SUCCESS;
    BOOL                    fCallCompleted = FALSE;
    DRS_MSG_GETCHGREPLY *   pmsgOut;
    DRS_MSG_GETCHGREQ       MsgIn;
    ULONG                   BindFlags=0;
    DWORD                   dwInVersion;
    BOOL                    fWaitForCompletion = (NULL == pAsyncState);
    SESSION_KEY             SessionKeyAtStart;
    BOOL                    fConstructedMsgIn = FALSE;
    LPWSTR                  pszDnsDomainName = NULL;
    LPWSTR                  pszUseDnsDomainName = pszServerDnsDomainName;
    DRS_ASYNC_RPC_STATE *   pTempAsyncState = NULL;

    // Allocate async RPC state if it was not supplied by the caller.
    if (NULL == pAsyncState) {
        pTempAsyncState = THAllocEx(pTHS, sizeof(*pTempAsyncState));
        pAsyncState = pTempAsyncState;
    }

    UpToDateVec_Validate(pmsgInNative->pUpToDateVecDest);
    UsnVec_Validate(&pmsgInNative->usnvecFrom);

    // Cast the V1 pointer as a VX pointer.  We abuse the fact that the V1
    // struct is the biggest struct in the VX union to avoid putting the VX on
    // the stack and having to perform the copy (or changing the signature of
    // this function).
    Assert(sizeof(DRS_MSG_GETCHGREPLY_NATIVE) == sizeof(DRS_MSG_GETCHGREPLY));
    pmsgOut = (DRS_MSG_GETCHGREPLY *) pmsgOutNative;

    // We need to prevent multi threaded use of the same handle in order to use
    // the security callback feature of RPC to retrieve the session key.
    BindFlags |= FBINDSZDRS_LOCK_HANDLE;

    if (!fWaitForCompletion) {
        // We're not getting the reply packet on this call, therefore we should
        // return from this call with the session key we were called with.
        PEKSaveSessionKeyForMyThread(pTHS, &SessionKeyAtStart);
    }

    __try {

        DBPOS * pDB = NULL;

        if (pNC == NULL ||
            (fNullUuid(&(pNC->Guid)) &&
             pNC->NameLen == 0)) {
            Assert(!"No one should call with without a completely valid (str and guid) NC");
            ret = ERROR_INVALID_PARAMETER;
            __leave;
        }

        // first, if this is a writable domain NC, use the dns domain name of that
        // nc in the SPN to ensure that the source server can be authenticated as
        // a member of that domain.

        DBOpen2(TRUE, &pDB);
        __try {

            // Note that extended operations send the object name in the "pNC"
            // field, which is not necessarily the name of the NC.  Get the correct
            // name of the NC which is being requested.  We must check the cross refs
            // since it might be the first request for this NC, we won't yet have it in
            // our list of master or replica nc's.

            if (IsMasterForNC(pDB, gAnchor.pDSADN, pNC)) {
                pszDnsDomainName = GetDomainDnsHostnameFromNC(pTHS, pNC); 
                // pszDnsDomainName will be null if pmsgInNative->pNC is not a domain partition
                // (ie Config, Schema, ndnc's) from the GetDomainDnsHostnameFromNC function
                if (pszDnsDomainName) {
                    pszUseDnsDomainName = pszDnsDomainName;
                } else {
                    pszUseDnsDomainName = pszServerDnsDomainName;  
                }
            }
        }
        __finally {
            DBClose(pDB, TRUE);
        }

        // we use the context info block and rpc call return stored in pAsyncState in case of failure
        // if I_DRSGetNCChangesComplete decides to run this call again, FBindSzDRS needs to know both
        // whether this is a second try (ie pContextInfo is not null) and what error caused us to try
        // again.  Immediately outside of this call, reset ret so that this function returns correct
        // values if it errors and cannot retry, or if it fails the retry also.
        while (!fCallCompleted
               && FBindSzDRS(pTHS, pszServerName, pszUseDnsDomainName,
                             BindFlags, &pAsyncState->pContextInfo,
                             &(pAsyncState->RpcCallReturn))) {
            __try {
                if (!fConstructedMsgIn) {
                    // First pass -- construct message.  We can't do this before
                    // we FBindSzDRS() because we need bind-time extensions info
                    // from the source DC to determine which message version to
                    // send.

                    if ((0 == pmsgInNative->cMaxObjects)
                        || (0 == pmsgInNative->cMaxBytes)) {
                        // Use appropriate default packet size based on whether
                        // the source is in the same site or in a different site.
                        if (IS_REMOTE_DSA_IN_SITE(&pAsyncState->pContextInfo->ext,
                                                  gAnchor.pSiteDN)) {
                            // Same site.  (Note that we err on the side of
                            // "same site" if we can't tell for sure.)
                            pmsgInNative->cMaxObjects = gcMaxIntraSiteObjects;
                            pmsgInNative->cMaxBytes   = gcMaxIntraSiteBytes;
                        } else {
                            // Different sites.
                            pmsgInNative->cMaxObjects = gcMaxInterSiteObjects;
                            pmsgInNative->cMaxBytes   = gcMaxInterSiteBytes;
                        }
                    }

                    if (IS_DRS_GETCHGREQ_V8_SUPPORTED(&pAsyncState->pContextInfo->ext)) {
                        // Target DSA is running post whistler review bits
                        dwInVersion = 8;
                    } else if (IS_DRS_GETCHGREQ_V5_SUPPORTED(&pAsyncState->pContextInfo->ext)) {
                        // Target DSA is running post Win2k RTM RC1 bits.
                        dwInVersion = 5;
                    } else {
                        // Target DSA is a pre-Win2k RTM RC2 DC.  We no longer
                        // support those.
                        DRA_EXCEPT(ERROR_REVISION_MISMATCH, 0);
                    }

                    if (!IS_DRS_EXT_SUPPORTED(&pAsyncState->pContextInfo->ext,
                                              DRS_EXT_RESTORE_USN_OPTIMIZATION)) {
                        // Pre-Win2k RC3 DC -- we don't support those.
                        DRA_EXCEPT(ERROR_REVISION_MISMATCH, 0);
                    }

                    draXlateNativeRequestToOutboundRequest(pTHS,
                                                           pmsgInNative,
                                                           NULL,
                                                           NULL,
                                                           dwInVersion,
                                                           &MsgIn);
                    fConstructedMsgIn = TRUE;
                }

                // Inform RPC to issue a call back on the security context used
                // for this call, so that we can copy the session key for
                // stronger encryption of secrets/passwords.
                InitRpcSessionEncryption(pTHS,
                                         BindFlags,
                                         pAsyncState->pContextInfo,
                                         NULL);

                // Construct state for asynchronous RPC call.
                drsPrepareAsyncRpcState(pTHS,
                                        pszServerName,
                                        pszServerDnsDomainName,
                                        DRS_ASYNC_CALL_GET_CHANGES,
                                        pAsyncState);
                pAsyncState->CallArgs.GetChg.pmsgIn      = pmsgInNative;
                pAsyncState->CallArgs.GetChg.pmsgOut     = pmsgOutNative;
                pAsyncState->CallArgs.GetChg.pSchemaInfo = pSchemaInfo;

                DRSSetContextCancelTime(DRS_CALL_GET_NC_CHANGES, gulDrsRpcReplicationTimeoutInMins, pAsyncState->pContextInfo);

                // Initiate the async call.  When control returns to us, RPC
                // has marshalled the input parameters and has enqueued the
                // send (but the response/out parameters have not yet been
                // received).
                _IDL_DRSGetNCChanges(&pAsyncState->RpcState,
                                     pAsyncState->pContextInfo->hDrs,
                                     dwInVersion,
                                     &MsgIn,
                                     &pAsyncState->CallArgs.GetChg.dwOutVersion,
                                     pmsgOut);
                pAsyncState->fIsCallInProgress = TRUE;

                DPRINT3(1, "Posted async GetChg call %p from USN 0x%I64x with flags 0x%x.\n",
                        pAsyncState,
                        pAsyncState->CallArgs.GetChg.pmsgIn->usnvecFrom.usnHighObjUpdate,
                        pAsyncState->CallArgs.GetChg.pmsgIn->ulFlags);

                fCallCompleted = TRUE;
            }
            __except (DRSHandleRpcClientException(GetExceptionInformation(),
                                                  pszServerName,
                                                  pAsyncState->pContextInfo,
                                                  gulDrsRpcReplicationTimeoutInMins,
                                                  &(pAsyncState->RpcCallReturn))) {

                // Exception intercepted.  We may decide to try contacting the
                // source machine again.
                Assert(!fCallCompleted);
            }
        }

        ret = pAsyncState->RpcCallReturn;
        MAP_SECURITY_PACKAGE_ERROR(ret); 

        if (pfBindSuccess) {
            // Was a handle obtained during this call? May or may not be cached.
            *pfBindSuccess = pAsyncState->pContextInfo &&
                (pAsyncState->pContextInfo->hDrs != NULL);
            DPRINT2( 1, "Sync from %ws, bind %d\n", pszServerName, *pfBindSuccess );
        }

        if (!ret && fWaitForCompletion) {
            ret = I_DRSGetNCChangesComplete(pTHS, pNC, pAsyncState, pfBindSuccess);
        }

        if (pszDnsDomainName) {
            THFreeEx(pTHS, pszDnsDomainName);
        }
    }
    __finally { 
        if (AbnormalTermination() || ret) {
            DRSDestroyAsyncRpcState(pTHS, pAsyncState);
        }

        if (!fWaitForCompletion) {
            PEKRestoreSessionKeySavedByMyThread(pTHS, &SessionKeyAtStart);
        }

        if (pTempAsyncState!=NULL) {
            THFreeEx(pTHS,pTempAsyncState);
        }
    }

    return ret;
}

ULONG
I_DRSGetNCChangesComplete(
    IN      THSTATE *               pTHS,
    IN      DSNAME *                pNC,
    IN OUT  DRS_ASYNC_RPC_STATE *   pAsyncState,
    OUT     BOOL *                  pfBindSuccess OPTIONAL
    )
/*++

Routine Description:

    Complete a replication get changes request begun during a previous call
    to I_DRSGetNCChanges.

Arguments:

    pTHS (IN)

    pAsyncState (IN/OUT) - Pointer to async state constructed via a previous
        call to I_DRSGetNCChanges.

Return Values:

    0 or Win32 error code.

--*/
{
    RPC_STATUS                    rpcStatus;
    ULONG                         ret;
    SESSION_KEY                   SessionKeyAtRpcCompletion;
    BOOL                          fSessionKeySaved = FALSE;

    // We abuse the fact that the native struct is the biggest struct in the
    // union to avoid putting the union on the stack and having to perform the
    // copy.
    Assert(sizeof(DRS_MSG_GETCHGREPLY_NATIVE) == sizeof(DRS_MSG_GETCHGREPLY));

    Assert(DRS_ASYNC_CALL_GET_CHANGES == pAsyncState->CallType);
    Assert(NULL != pAsyncState->pContextInfo);

    __try {
        do {
            // use RpcCallReturn in the AsyncState block to hold the error value so that 
            // I_DRSGetNCChanges can know why we failed if we end up trying again below.
            rpcStatus = drsWaitForAsyncRpc(pTHS, pAsyncState, &(pAsyncState->RpcCallReturn));
 
            if (drsIsCallComplete(rpcStatus, pAsyncState->RpcCallReturn, pAsyncState->pContextInfo)) {
                // Success or non-retriable error.
                ret = pAsyncState->RpcCallReturn;
                break;
            }

            // Call failed in a way that dictates we must try again.
            ret = I_DRSGetNCChanges(pTHS,
                                    pAsyncState->CallArgs.pszServerName,
                                    pAsyncState->CallArgs.pszDomainName,
                                    pNC,
                                    pAsyncState->CallArgs.GetChg.pmsgIn,
                                    pAsyncState->CallArgs.GetChg.pmsgOut,
                                    pAsyncState->CallArgs.GetChg.pSchemaInfo,
                                    pAsyncState,
                                    pfBindSuccess);
        } while (!ret);

        if (ret) {
            __leave;
        }

        PEKSaveSessionKeyForMyThread(pTHS, &SessionKeyAtRpcCompletion);
        fSessionKeySaved = TRUE;

        draXlateInboundReplyToNativeReply(pTHS,
                                          pAsyncState->CallArgs.GetChg.dwOutVersion,
                                          (DRS_MSG_GETCHGREPLY *) pAsyncState->CallArgs.GetChg.pmsgOut,
                                          pAsyncState->CallArgs.GetChg.pmsgIn->ulExtendedOp
                                            ? DRA_XLATE_FSMO_REPLY
                                            : 0,
                                          pAsyncState->CallArgs.GetChg.pmsgOut);

        UpToDateVec_Validate(pAsyncState->CallArgs.GetChg.pmsgOut->pUpToDateVecSrc);
        UsnVec_Validate(&pAsyncState->CallArgs.GetChg.pmsgOut->usnvecTo);

        __try {
            if (IS_DRS_SCHEMA_INFO_SUPPORTED(pTHS->pextRemote)) {
                // Schema info piggybacked on the prefix table, Strip it
                StripSchInfoFromPrefixTable(&pAsyncState->CallArgs.GetChg.pmsgOut->PrefixTableSrc,
                                            pAsyncState->CallArgs.GetChg.pSchemaInfo);
            }
        }
        __except (GetDraException((GetExceptionInformation()), &ret)) {
            DPRINT1(1,"I_DRSGetNCChanges: Exception %d while trying to strip schema info\n", ret);
        }
    }
    __finally {
        if (ret && pAsyncState && pAsyncState->CallArgs.GetChg.pmsgIn) {
            LogEvent8(DS_EVENT_CAT_REPLICATION,
                      DS_EVENT_SEV_BASIC,
                      DIRLOG_DRA_GETNCCHANGES_COMPLETE,
                      szInsertWC(pAsyncState->CallArgs.pszServerName),
                      szInsertDN(pAsyncState->CallArgs.GetChg.pmsgIn->pNC),
                      szInsertUSN(pAsyncState->CallArgs.GetChg.pmsgIn->usnvecFrom.usnHighObjUpdate),
                      szInsertUSN(pAsyncState->CallArgs.GetChg.pmsgIn->usnvecFrom.usnHighPropUpdate),
                      szInsertUL(pAsyncState->CallArgs.GetChg.pmsgIn->ulFlags),
                      szInsertUL(pAsyncState->CallArgs.GetChg.pmsgIn->ulExtendedOp),
                      szInsertUL(ret),
                      szInsertWin32Msg(ret) );
        }
        DRSDestroyAsyncRpcState(pTHS, pAsyncState);

        if (fSessionKeySaved) {
            PEKRestoreSessionKeySavedByMyThread(pTHS,
                                                &SessionKeyAtRpcCompletion);
        }
    }

    Assert(ret || (NULL != pTHS->SessionKey.SessionKey));
    Assert(ret || (NULL != pTHS->pextRemote));

    return ret;
}

ULONG
I_DRSUpdateRefsEx(
    THSTATE *   pTHS,
    LPWSTR      pszServerName,
    DSNAME *    pNC,
    LPWSTR      pszDsaDest,
    UUID *      puuidDsaDest,
    ULONG       ulOptions,
    PULONG      pfCallCompleted
    )
{
    ULONG               ret = ERROR_SUCCESS;
    DRS_CONTEXT_INFO *  pContextInfo = NULL;
    BOOL                fCallCompleted = FALSE;
    DRS_MSG_UPDREFS     msgUpdRefs;
    MTX_ADDR *          pmtx = NULL;

    memset(&msgUpdRefs, 0, sizeof(msgUpdRefs));

    msgUpdRefs.V1.pNC        = pNC;
    msgUpdRefs.V1.ulOptions  = ulOptions;

    if (NULL != pszDsaDest) {
        pmtx = MtxAddrFromTransportAddrEx(pTHS, pszDsaDest);
        msgUpdRefs.V1.pszDsaDest = pmtx->mtx_name;
    }

    if (NULL != puuidDsaDest) {
        msgUpdRefs.V1.uuidDsaObjDest = *puuidDsaDest;
    }

    while (!fCallCompleted
           && FBindSzDRS(pTHS, pszServerName, NULL, 0, &pContextInfo, &ret)) {
        __try {
            DRSSetContextCancelTime(DRS_CALL_UPDATE_REFS,gulDrsRpcReplicationTimeoutInMins,pContextInfo );   

            ret = _IDL_DRSUpdateRefs(pContextInfo->hDrs, 1, &msgUpdRefs);

            fCallCompleted = drsIsCallComplete(0, ret, pContextInfo);
        }
        __except (DRSHandleRpcClientException(GetExceptionInformation(),
                                              pszServerName,
                                              pContextInfo,
                                              gulDrsRpcReplicationTimeoutInMins,
                                              &ret)) {
            ;
        }
    }

    MAP_SECURITY_PACKAGE_ERROR( ret );

    DRSFreeContextInfo(pTHS, &pContextInfo);

    if (NULL != pmtx) {
        THFreeEx(pTHS, pmtx);
    }

    if (pfCallCompleted) {
        *pfCallCompleted = fCallCompleted;
    }

    return ret;
}

ULONG
I_DRSReplicaAddEx(
    IN  THSTATE *   pTHS,
    IN  LPWSTR      pszServerName,
    IN  DSNAME *    pNCName,
    IN  DSNAME *    pSourceDsaDN,
    IN  DSNAME *    pTransportDN,
    IN  LPWSTR      pszSourceDsaAddress,
    IN  REPLTIMES * pSyncSchedule,
    IN  ULONG       ulOptions,
    OUT PULONG      pfCallCompleted
    )
{
    ULONG               ret = ERROR_SUCCESS;
    DRS_CONTEXT_INFO *  pContextInfo = NULL;
    BOOL                fCallCompleted = FALSE;
    DRS_MSG_REPADD      msgAdd;
    DWORD               dwMsgVersion;
    MTX_ADDR *          pmtx = NULL;

    Assert(NULL != pszSourceDsaAddress);

    memset(&msgAdd, 0, sizeof(msgAdd));

    pmtx = MtxAddrFromTransportAddrEx(pTHS, pszSourceDsaAddress);

    if ((NULL == pSourceDsaDN) && (NULL == pTransportDN)) {
        dwMsgVersion = 1;

        msgAdd.V1.pNC       = pNCName;
        msgAdd.V1.pszDsaSrc = pmtx->mtx_name;
        msgAdd.V1.ulOptions = ulOptions;

        if (NULL != pSyncSchedule) {
            msgAdd.V1.rtSchedule = *pSyncSchedule;
        }
    }
    else {
        dwMsgVersion = 2;

        msgAdd.V2.pNC                 = pNCName;
        msgAdd.V2.pSourceDsaDN        = pSourceDsaDN;
        msgAdd.V2.pTransportDN        = pTransportDN;
        msgAdd.V2.pszSourceDsaAddress = pmtx->mtx_name;
        msgAdd.V2.ulOptions           = ulOptions;

        if (NULL != pSyncSchedule) {
            msgAdd.V2.rtSchedule = *pSyncSchedule;
        }
    }

    while (!fCallCompleted
           && FBindSzDRS(pTHS, pszServerName, NULL, 0, &pContextInfo, &ret)) {
        if ((2 == dwMsgVersion)
            && !IS_DRS_REPADD_V2_SUPPORTED(&pContextInfo->ext)) {
            ret = DRAERR_NotSupported;
            break;
        }
        __try {
            DRSSetContextCancelTime(DRS_CALL_REPLICA_ADD,gulDrsRpcReplicationTimeoutInMins,pContextInfo );   

            ret =  _IDL_DRSReplicaAdd(pContextInfo->hDrs, dwMsgVersion, &msgAdd);

            fCallCompleted = drsIsCallComplete(0, ret, pContextInfo);
        }
        __except (DRSHandleRpcClientException(GetExceptionInformation(),
                                              pszServerName,
                                              pContextInfo,
                                              gulDrsRpcReplicationTimeoutInMins,
                                              &ret)) {
            ;
        }
    }

    MAP_SECURITY_PACKAGE_ERROR( ret );

    DRSFreeContextInfo(pTHS, &pContextInfo);

    THFreeEx(pTHS, pmtx);

    if (pfCallCompleted) {
        *pfCallCompleted = fCallCompleted;
    }

    return ret;
}


ULONG
I_DRSReplicaDel(
    IN  THSTATE * pTHS,
    IN  LPWSTR    pszServerName,
    IN  DSNAME *  pNCName,
    IN  LPWSTR    pszSourceDSA,
    IN  ULONG     ulOptions
    )
{
    ULONG               ret = ERROR_SUCCESS;
    DRS_CONTEXT_INFO *  pContextInfo = NULL;
    BOOL                fCallCompleted = FALSE;
    DRS_MSG_REPDEL      msgDel;
    MTX_ADDR *          pmtx = NULL;

    memset(&msgDel, 0, sizeof(msgDel));

    msgDel.V1.pNC       = pNCName;
    msgDel.V1.ulOptions = ulOptions;

    if (NULL != pszSourceDSA) {
        pmtx = MtxAddrFromTransportAddrEx(pTHS, pszSourceDSA);
        msgDel.V1.pszDsaSrc = pmtx->mtx_name;
    }

    while (!fCallCompleted
           && FBindSzDRS(pTHS, pszServerName, NULL, 0, &pContextInfo, &ret)) {
        __try {
            DRSSetContextCancelTime(DRS_CALL_REPLICA_DEL, gulDrsRpcReplicationTimeoutInMins,pContextInfo );   

            ret =  _IDL_DRSReplicaDel(pContextInfo->hDrs, 1, &msgDel);

            fCallCompleted = drsIsCallComplete(0, ret, pContextInfo);
        }
        __except (DRSHandleRpcClientException(GetExceptionInformation(),
                                              pszServerName,
                                              pContextInfo,
                                              gulDrsRpcReplicationTimeoutInMins,
                                              &ret)) {
            ;
        }
    }

    MAP_SECURITY_PACKAGE_ERROR( ret );

    DRSFreeContextInfo(pTHS, &pContextInfo);

    if (NULL != pmtx) {
        THFreeEx(pTHS, pmtx);
    }

    return ret;
}


ULONG
I_DRSVerifyNames(
    THSTATE *               pTHS,
    LPWSTR                  pszServerName,
    LPWSTR                  pszDnsDomainName,
    DWORD                   dwInVersion,
    DRS_MSG_VERIFYREQ *     pmsgIn,
    DWORD *                 pdwOutVersion,
    DRS_MSG_VERIFYREPLY *   pmsgOut
    )
{
    ULONG               ret = ERROR_SUCCESS;
    DRS_CONTEXT_INFO *  pContextInfo = NULL;
    BOOL                fCallCompleted = FALSE;

    while (!fCallCompleted
           && FBindSzDRS(pTHS, pszServerName, pszDnsDomainName, 0, &pContextInfo, &ret)) {
        __try {
            DRSSetContextCancelTime(DRS_CALL_VERIFY_NAMES,gulDrsRpcGcLookupTimeoutInMins,pContextInfo );  

            ret = _IDL_DRSVerifyNames(pContextInfo->hDrs,
                                      dwInVersion,
                                      pmsgIn,
                                      pdwOutVersion,
                                      pmsgOut);

            fCallCompleted = drsIsCallComplete(0, ret, pContextInfo);
        }
        __except (DRSHandleRpcClientException(GetExceptionInformation(),
                                              pszServerName,
                                              pContextInfo,
                                              gulDrsRpcGcLookupTimeoutInMins,
                                              &ret)) {
            ;
        }
    }

    MAP_SECURITY_PACKAGE_ERROR( ret );

    DRSFreeContextInfo(pTHS, &pContextInfo);

    return ret;
}


ULONG
I_DRSGetMemberships(
    THSTATE *   pTHS,
    LPWSTR      pszServerName,
    LPWSTR      pszServerDnsDomainName,
    DWORD       dwFlags,
    DSNAME      **ppObjects,
    ULONG       cObjects,
    PDSNAME     pLimitingDomain,
    REVERSE_MEMBERSHIP_OPERATION_TYPE
                OperationType,
    PULONG      pErrCode,
    PULONG      pcDsNames,
    PDSNAME     ** prpDsNames,
    PULONG      *pAttributes,
    PULONG      pcSidHistory,
    PSID        **rgSidHistory
    )
{
    ULONG                   ret = ERROR_SUCCESS;
    DRS_CONTEXT_INFO *      pContextInfo = NULL;
    BOOL                    fCallCompleted = FALSE;
    DRS_MSG_REVMEMB_REQ     msgIn;
    DRS_MSG_REVMEMB_REPLY   msgOut;
    DWORD                   dwOutVersion;

    msgIn.V1.ppDsNames       = ppObjects;
    msgIn.V1.cDsNames        = cObjects;
    msgIn.V1.dwFlags         = 0;
    msgIn.V1.pLimitingDomain = pLimitingDomain;
    msgIn.V1.OperationType   = OperationType;

    RtlZeroMemory(&msgOut, sizeof(msgOut));

    while (!fCallCompleted
           && FBindSzDRS(pTHS, pszServerName, pszServerDnsDomainName, 0, &pContextInfo, &ret)) {
        __try {
            DRSSetContextCancelTime(DRS_CALL_GET_MEMBERSHIPS,gulDrsRpcGcLookupTimeoutInMins,pContextInfo );   

            ret = _IDL_DRSGetMemberships(
                pContextInfo->hDrs,
                1,
                &msgIn,
                &dwOutVersion,
                &msgOut
                );

            *prpDsNames = msgOut.V1.ppDsNames;
            *pcDsNames  = msgOut.V1.cDsNames;

            if (ARGUMENT_PRESENT(pAttributes)) {
                *pAttributes = msgOut.V1.pAttributes;
            }

            if ((ARGUMENT_PRESENT(pcSidHistory))
                && (ARGUMENT_PRESENT(rgSidHistory))) {
                *pcSidHistory = msgOut.V1.cSidHistory;
                *rgSidHistory = msgOut.V1.ppSidHistory;
            }
          
            *pErrCode = msgOut.V1.errCode;

            fCallCompleted = drsIsCallComplete(0, ret, pContextInfo);
        }
        __except (DRSHandleRpcClientException(GetExceptionInformation(),
                                              pszServerName,
                                              pContextInfo,
                                              gulDrsRpcGcLookupTimeoutInMins,
                                              &ret)) {
            ;
        }
    }
    MAP_SECURITY_PACKAGE_ERROR( ret );

    DRSFreeContextInfo(pTHS, &pContextInfo);

    return ret;
}


ULONG
I_DRSInterDomainMove(
    IN  THSTATE *           pTHS,
    IN  LPWSTR              pszServerName,
    IN  DWORD               dwInVersion,
    IN  DRS_MSG_MOVEREQ *   pmsgIn,
    IN  DWORD *             pdwOutVersion,
    OUT DRS_MSG_MOVEREPLY * pmsgOut
    )
{
    ULONG               ret = ERROR_SUCCESS;
    DRS_CONTEXT_INFO *  pContextInfo = NULL;
    BOOL                fCallCompleted = FALSE;
    ULONG               BindFlags;
    extern ULONG        EncryptSecretData(THSTATE *, ENTINF *);

    if (    (NULL == pszServerName)
         || (NULL == pmsgIn)
         || (NULL == pmsgOut)
         || (2 != dwInVersion) ) {
        return(DRAERR_InvalidParameter);
    }

    BindFlags = FBINDSZDRS_LOCK_HANDLE |            // required by encryption
                FBINDSZDRS_NO_CACHED_HANDLES |      // for user impersonation
                FBINDSZDRS_CRYPTO_BIND;             // RPC session encryption

    while (    !fCallCompleted
               && FBindSzDRS(  pTHS,
                               pszServerName,
                               NULL,
                               BindFlags,
                               &pContextInfo, &ret)) {

        // Security error check.
        MAP_SECURITY_PACKAGE_ERROR(ret);
        if ( ret ) {
            break;
        }

        // Versioning check.
        if (    (2 == dwInVersion)
                && !IS_DRS_MOVEREQ_V2_SUPPORTED(&pContextInfo->ext) ) {
            ret = DRAERR_NotSupported;
            break;
        }

        // FBINDSZDRS_CRYPTO_BIND sanity check.
        Assert(    pTHS->SessionKey.SessionKey
                   && pTHS->SessionKey.SessionKeyLength);

        // Update THSTATE so pek code can match destination's capabilities.
        DraSetRemoteDsaExtensionsOnThreadState(pTHS, &pContextInfo->ext);

        // At this point we know that:
        //
        //      1) The destination has the session keys stashed in the
        //         DRS_CLIENT_CONTEXT for this client session and will
        //         presumably use them when decrypting DBIsSecretData()
        //         in the ENTINF we are about to send.
        //      3) We have the corresponding session keys stashed in our
        //         thread state such that we will encrypt DBIsSecretData()
        //         with those keys for any items read while fDRA is set.
        //
        // So now invoke the callback to encrypt those things which need it.
        //
        // N.B. The PEK code (pek.c) figures out for itself which version/level
        // of encryption is supported in common between source and destination
        // based on the extensions.


        if ( ret = EncryptSecretData(pTHS, pmsgIn->V2.pSrcObject) ) {
            break;
        }

        if (IS_DRS_SCHEMA_INFO_SUPPORTED(&pContextInfo->ext)) {
            // need to ship the schema info piggybacked on the prefix table
            // Note: the client doesn't ship the table unless it also
            // send some atts (like partial atts). We send the schema
            // info anyway. Right now it will not be used on the
            // server side.

            if (AddSchInfoToPrefixTable(pTHS, &pmsgIn->V2.PrefixTable)){
                ret = DRAERR_SchemaInfoShip;
                break;
            }
        }

        __try {
            DRSSetContextCancelTime(DRS_CALL_INTER_DOMAIN_MOVE, gulDrsRpcMoveObjectTimeoutInMins,pContextInfo );   

            ret = _IDL_DRSInterDomainMove(pContextInfo->hDrs,
                                          dwInVersion,
                                          pmsgIn,
                                          pdwOutVersion,
                                          pmsgOut);

            fCallCompleted = drsIsCallComplete(0, ret, pContextInfo);
        }
        __except (DRSHandleRpcClientException(GetExceptionInformation(),
                                              pszServerName,
                                              pContextInfo,
                                              gulDrsRpcMoveObjectTimeoutInMins,
                                              &ret)) {
            ;
        }
    }

    MAP_SECURITY_PACKAGE_ERROR( ret );

    DRSFreeContextInfo(pTHS, &pContextInfo);

    PEKClearSessionKeys(pTHS);

    return ret;
}


ULONG
I_DRSGetNT4ChangeLog(
    THSTATE *               pTHS,
    LPWSTR                  pszServerName,
    DWORD                   dwFlags,
    ULONG                   PreferredMaximumLength,
    PVOID *                 ppRestart,
    PULONG                  pcbRestart,
    PVOID *                 ppLog,
    PULONG                  pcbLog,
    NT4_REPLICATION_STATE * ReplicationState,
    NTSTATUS *              ActualNtStatus
    )
{
    ULONG                     ret = ERROR_SUCCESS;
    DRS_CONTEXT_INFO *        pContextInfo = NULL;
    BOOL                      fCallCompleted = FALSE;
    DRS_MSG_NT4_CHGLOG_REQ    msgIn;
    DRS_MSG_NT4_CHGLOG_REPLY  msgOut;
    DWORD                     dwOutVersion;

    msgIn.V1.dwFlags                = dwFlags;
    msgIn.V1.PreferredMaximumLength = PreferredMaximumLength;
    msgIn.V1.cbRestart              = *pcbRestart;
    msgIn.V1.pRestart               = *ppRestart;

    RtlZeroMemory(&msgOut,sizeof(msgOut));

    //
    // For getting the change log, we do not try hard, but try
    // weakly. Remember it is a best effort process, and if we
    // encounter any errors we will end up dropping the idea, of
    // getting the change log and rescheduling it later.
    //

    while (!fCallCompleted
           && FBindSzDRS(pTHS, pszServerName, NULL, 0, &pContextInfo, &ret)) {
        __try {
            DRSSetContextCancelTime(DRS_CALL_GET_NT4_CHANGE_LOG,gulDrsRpcNT4ChangeLogTimeoutInMins,pContextInfo );    

            ret =  _IDL_DRSGetNT4ChangeLog(
                pContextInfo->hDrs,
                1,
                &msgIn,
                &dwOutVersion,
                &msgOut
                );
            Assert(1 == dwOutVersion);

            *ppRestart = msgOut.V1.pRestart;
            *pcbRestart = msgOut.V1.cbRestart;
            *ppLog = msgOut.V1.pLog;
            *pcbLog = msgOut.V1.cbLog;

            *ReplicationState = msgOut.V1.ReplicationState;
            *ActualNtStatus = msgOut.V1.ActualNtStatus;

            fCallCompleted = drsIsCallComplete(0, ret, pContextInfo);
        }
        __except (DRSHandleRpcClientException(GetExceptionInformation(),
                                              pszServerName,
                                              pContextInfo,
                                              gulDrsRpcNT4ChangeLogTimeoutInMins,
                                              &ret)) {
            ;
        }
    }

    MAP_SECURITY_PACKAGE_ERROR( ret );

    DRSFreeContextInfo(pTHS, &pContextInfo);

    return ret;
}


ULONG
I_DRSCrackNames(
    THSTATE *               pTHS,
    LPWSTR                  pszServerName,
    LPWSTR                  pszDnsDomainName,
    DWORD                   dwInVersion,
    DRS_MSG_CRACKREQ *      pmsgIn,
    DWORD *                 pdwOutVersion,
    DRS_MSG_CRACKREPLY *    pmsgOut
    )
{
    ULONG               win32Err = ERROR_SUCCESS;
    DRS_CONTEXT_INFO *  pContextInfo = NULL;
    BOOL                fCallCompleted = FALSE;

    // N.B. - For various reasons, _IDL_DRSCrackNames returns WIN32
    // error codes, not DRAERR* values.  See dsamain\dra\ntdsapi.c.
    // Therefore this routine does too.  The only known internal client
    // at this time is CrackSingleName in dsamain\src\cracknam.c.

    while (!fCallCompleted
           && FBindSzDRS(pTHS, pszServerName, pszDnsDomainName, 0, &pContextInfo, &win32Err)) {
        __try {
            DRSSetContextCancelTime(DRS_CALL_CRACK_NAMES,gulDrsRpcGcLookupTimeoutInMins,pContextInfo );    

            win32Err = _IDL_DRSCrackNames(pContextInfo->hDrs,
                                          dwInVersion,
                                          pmsgIn,
                                          pdwOutVersion,
                                          pmsgOut);
            fCallCompleted = drsIsCallComplete(0, win32Err, pContextInfo);
        }
        __except (DRSHandleRpcClientException(GetExceptionInformation(),
                                              pszServerName,
                                              pContextInfo,
                                              gulDrsRpcGcLookupTimeoutInMins,
                                              &win32Err)) {
            ;
        }
    }

    MAP_SECURITY_PACKAGE_ERROR( win32Err );

    DRSFreeContextInfo(pTHS, &pContextInfo);

    return win32Err;
}

#ifdef INCLUDE_UNIT_TESTS

/*++

    This is a Unit test that excercises the binding handle cache
    and also usage of cached as well as locked handles. Just make
    sure that the machine to which you want to get binding handle is
    running and is reachable. The test will assert on failure. Typically
    on an idle machine the test will succeed. There can be spurious failures
    on a busy machine. Agreed it is primitive that the we have to declare the
    server name statically like below, and it will be a later excercise to
    improve this such that this can be taken as a parameter to BHCacheTest.

--*/
LPWSTR szTestServer = L"murli06.murlitest.nttest.microsoft.com";

VOID BHCacheTest(VOID)
{
    DRS_CONTEXT_INFO *pContextInfo1=NULL;
    DRS_CONTEXT_INFO *pContextInfo2=NULL;

    DWORD            dwStatus=0;
    BOOL             Success;
    DRS_HANDLE       hDrs;

    //
    // Void the cache for this server
    //

    BHCacheVoidServer(szTestServer);

    //
    // First try a simple binding
    //

    Success = FBindSzDRS(
                pTHStls,
                szTestServer,
                NULL,
                0,
                &pContextInfo1,
                &dwStatus
                );

    Assert(Success);
    Assert(0==dwStatus);
    // The handle should be cached.
    Assert(pContextInfo1->fIsHandleInCache);

    //
    // Try a simple binding again. Should get back the
    // same handle.
    //

     Success = FBindSzDRS(
                pTHStls,
                szTestServer,
                NULL,
                0,
                &pContextInfo2,
                &dwStatus
                );

    Assert(Success);
    Assert(0==dwStatus);
    // The handle should be cached.
    Assert(pContextInfo2->fIsHandleInCache);
    // The handle should be the same
    Assert(pContextInfo1->hDrs==pContextInfo2->hDrs);

    //
    // Save the handle. Will be used below
    //

    hDrs = pContextInfo1->hDrs;
    //
    // Free the handles. The handles should be returned to
    // the cache
    //


    DRSFreeContextInfo(pTHStls, &pContextInfo1);
    DRSFreeContextInfo(pTHStls, &pContextInfo2);


    //
    // Try a case of a locked handle. The handle should now
    // come from the cache
    //

    Success = FBindSzDRS(
                pTHStls,
                szTestServer,
                NULL,
                FBINDSZDRS_LOCK_HANDLE,
                &pContextInfo1,
                &dwStatus
                );

    Assert(Success);
    Assert(0==dwStatus);
    // The handle should be cached.
    Assert(pContextInfo1->fIsHandleInCache);
    // Should have the same handle back.
    Assert(hDrs==pContextInfo1->hDrs);

    //
    // Try to obtain one other handle. This handle should
    // not come from the cache ( the one in the cache is locked )
    //

     Success = FBindSzDRS(
                pTHStls,
                szTestServer,
                NULL,
                0,
                &pContextInfo2,
                &dwStatus
                );

    Assert(Success);
    Assert(0==dwStatus);
    // The handle should be cached.
    Assert(!(pContextInfo2->fIsHandleInCache));
    // The handle should be the same
    Assert(hDrs!=pContextInfo2->hDrs);

    //
    // Free Handles
    //

    DRSFreeContextInfo(pTHStls, &pContextInfo2);
    DRSFreeContextInfo(pTHStls, &pContextInfo1);

    //
    // Get one more handle, and this should be
    // from the cache and the same as the first 3
    // handles
    //


    Success = FBindSzDRS(
                pTHStls,
                szTestServer,
                NULL,
                0,
                &pContextInfo1,
                &dwStatus
                );

    Assert(Success);
    Assert(0==dwStatus);
    // The handle should be cached.
    Assert(pContextInfo1->fIsHandleInCache);
    // Should have the same handle back.
    Assert(hDrs==pContextInfo1->hDrs);

    //
    // Free the handle
    //

    DRSFreeContextInfo(pTHStls, &pContextInfo1);

 }


#endif

ULONG
I_DRSAddEntry(
    IN  THSTATE *                   pTHS,
    IN  LPWSTR                      pszServerName,
    IN  DRS_SecBufferDesc *         pClientCreds,   OPTIONAL
    IN  DRS_MSG_ADDENTRYREQ_V2 *    pReq,
    OUT DWORD *                     pdwReplyVer,
    OUT DRS_MSG_ADDENTRYREPLY *     pReply
    )

/*++

  Routine Description:

    Calls the DSA on server pszServerName to add the object described in
    pReq, with the results of the operation being returned in pReply.

  Parameters:

    pszServerName - Name of server where the add should take place

    pClientCreds (OPTIONAL) - Credentials to use to add the object, if other
        than the default credentials (gCredentials)

    pReq - mostly an addarg in a bag

    pReply - add results

  Return Values:

    ERROR_SUCCESS on success

--*/

{
    ULONG               ret = ERROR_SUCCESS;
    DRS_CONTEXT_INFO *  pContextInfo = NULL;
    BOOL                fCallCompleted = FALSE;
    ULONG               ReqVer;
    DRS_MSG_ADDENTRYREQ uReq;
    DRS_MSG_ADDENTRYREPLY uReply;

    if (NULL == pClientCreds) {
        ReqVer = 2;
        uReq.V2 = *pReq;
    } else {
        ReqVer = 3;
        uReq.V3.EntInfList = pReq->EntInfList;
        uReq.V3.pClientCreds = pClientCreds;
    }

    RtlZeroMemory( &uReply, sizeof( uReply ) );

    while (!fCallCompleted && FBindSzDRS(pTHS,
                                         pszServerName,
                                         NULL,
                                         0,
                                         &pContextInfo,
                                         &ret)) {
        Assert(!ret)
            switch (ReqVer) {
            case 2:
                if (!IS_DRS_ADDENTRY_V2_SUPPORTED(&pContextInfo->ext)) {
                    ret = DRAERR_NotSupported;
                }
                break;

            case 3:
                if (!IS_DRS_ADDENTRY_V3_SUPPORTED(&pContextInfo->ext)) {
                    ret = DRAERR_NotSupported;
                }
                break;

            default:
                Assert(!"Logic Error");
                ret = DRAERR_NotSupported;
                break;
            }

        if (ret) {
            break;
        }

        __try {
            DRSSetContextCancelTime(DRS_CALL_ADD_ENTRY,gulDrsRpcReplicationTimeoutInMins,pContextInfo );     

            ret =  _IDL_DRSAddEntry(pContextInfo->hDrs,
                                    ReqVer,
                                    &uReq,
                                    pdwReplyVer,
                                    &uReply);

            fCallCompleted = drsIsCallComplete(0, ret, pContextInfo);
        }
        __except (DRSHandleRpcClientException(GetExceptionInformation(),
                                              pszServerName,
                                              pContextInfo,
                                              gulDrsRpcReplicationTimeoutInMins,
                                              &ret)) {
            ;
        }
    }

    MAP_SECURITY_PACKAGE_ERROR( ret );

    DRSFreeContextInfo(pTHS, &pContextInfo);

    *pReply = uReply;

    return ret;
}

ULONG
I_DRSIsExtSupported(
    IN  THSTATE *   pTHS,
    IN  LPWSTR      pszServerName,
    IN  ULONG       Ext
    )
/*++

  Routine Description:

    This routine determines if pszServerName supports Ext

  Parameters:

    pszServerName - Name of server where the add should take place

    Ext -- the extension of the DRS interface the caller is interested in


  Return Values:

    ERROR_SUCCESS or DRAERR_NotSupported, or network error

--*/

{
    ULONG               ret = DRAERR_Success;
    DRS_CONTEXT_INFO *  pContextInfo = NULL;

    if ( FBindSzDRS(
             pTHS,
             pszServerName,
             NULL, 0,
             &pContextInfo,
             &ret) && !ret ){
        // bind succeeded w/ no errors.
        // see if exts are supported.
        if ( !IS_DRS_EXT_SUPPORTED(&pContextInfo->ext, Ext) ) {
            ret = DRAERR_NotSupported;
        }
    }

    DRSFreeContextInfo(pTHS, &pContextInfo);

    return ret;
}

ULONG
I_DRSGetMemberships2(
    THSTATE                       *pTHS,
    LPWSTR                         pszServerName,
    LPWSTR                         pszDnsDomainName,
    DWORD                          dwInVersion,
    DRS_MSG_GETMEMBERSHIPS2_REQ   *pmsgIn,
    DWORD                         *pdwOutVersion,
    DRS_MSG_GETMEMBERSHIPS2_REPLY *pmsgOut
    )
/*++

Routine Description:

    This routine is a batchable form of I_DRSGetMemberships

Parameters:

    pTHS -- thread state

    pszServerName -- target server

    pszServerDnsDomainDomain -- domain of pszServerName, needed for SPN

    dwMsgInVersion -- version of in blob

    psgIn -- in blob, see drs.idl for details

    pdwMsgOutVersion -- version of returned blob

    psgOut -- returned blob, see drs.idl for details

  Return Values:

    ERROR_SUCCESS on success

--*/
{

    ULONG               ret = ERROR_SUCCESS;
    DRS_CONTEXT_INFO *  pContextInfo = NULL;
    BOOL                fCallCompleted = FALSE;

    while (!fCallCompleted
           && FBindSzDRS(pTHS, pszServerName, pszDnsDomainName, 0, &pContextInfo, &ret)) {
        __try {
            DRSSetContextCancelTime(DRS_CALL_GET_MEMBERSHIPS2,gulDrsRpcGcLookupTimeoutInMins,pContextInfo );       

            ret = _IDL_DRSGetMemberships2(pContextInfo->hDrs,
                                          dwInVersion,
                                          pmsgIn,
                                          pdwOutVersion,
                                          pmsgOut);

            fCallCompleted = drsIsCallComplete(0, ret, pContextInfo);

        }
        __except (DRSHandleRpcClientException(GetExceptionInformation(),
                                              pszServerName,
                                              pContextInfo,
                                              gulDrsRpcGcLookupTimeoutInMins,
                                              &ret)) {
            ;
        }
    }

    MAP_SECURITY_PACKAGE_ERROR( ret );

    DRSFreeContextInfo(pTHS, &pContextInfo);

    return ret;

}



BOOL
I_DRSIsIntraSiteServer(
    IN  THSTATE *   pTHS,
    IN  LPWSTR      pszServerName
    )
/*++

  Routine Description:

    This routine determines if pszServerName is intra-site relative
    to this server (as given by gAnchor).

  Parameters:

    pTHS - Thread context
    pszServerName - Name of server where the add should take place

  Return Values:

    TRUE: Target server is intra-site
    FALSE: Target server is NOT intra-site
    (or some other failure, i.e. default is inter-site)

--*/

{
    ULONG               ret = ERROR_SUCCESS;
    DRS_CONTEXT_INFO *  pContextInfo = NULL;
    BOOL                fIntra = FALSE;

    if ( FBindSzDRS(
             pTHS,
             pszServerName,
             NULL,0,
             &pContextInfo,
             &ret)           &&
         IS_REMOTE_DSA_IN_SITE(&pContextInfo->ext, gAnchor.pSiteDN) )
    {
        fIntra = TRUE;
    }

    DRSFreeContextInfo(pTHS, &pContextInfo);

    return fIntra;
}

ULONG
I_DRSGetObjectExistence(
    IN      THSTATE *                     pTHS,
    IN      LPWSTR                        pszServerName,
    IN      DRS_MSG_EXISTREQ *            pmsgIn,
    OUT     DWORD *                       pdwOutVersion,
    OUT     DRS_MSG_EXISTREPLY *          pmsgOut
    )
/*++

  Routine Description:

    This routine determines if a set of objects on the source server
    match the set of objects on the destination (this) server.  If not,
    the set of object guids is returned.

  Parameters:

    pTHS - Thread context
    pszServerName - Source server (transport address)
    pmsgIn
    pdwOutVersion
    pmsgOut

  Return Values:

    0 or Errors

--*/
{
    ULONG                ret = ERROR_SUCCESS;
    DRS_CONTEXT_INFO *   pContextInfo = NULL;
    BOOL                 fCallCompleted = FALSE;

    while (!fCallCompleted
           && FBindSzDRS(pTHS, pszServerName, NULL, 0, &pContextInfo, &ret)) {
        __try {  
            DRSSetContextCancelTime(DRS_CALL_GET_OBJECT_EXISTENCE, gulDrsRpcObjectExistenceTimeoutInMins,pContextInfo );  

            ret = _IDL_DRSGetObjectExistence(pContextInfo->hDrs, 
                                             1, 
                                             pmsgIn,
                                             pdwOutVersion,
                                             pmsgOut); 
            fCallCompleted = drsIsCallComplete(0, ret, pContextInfo);
        } 
        __except (DRSHandleRpcClientException(GetExceptionInformation(),
                                              pszServerName,
                                              pContextInfo,
                                              gulDrsRpcObjectExistenceTimeoutInMins,
                                              &ret)) {
            ;
        }       
    }

    MAP_SECURITY_PACKAGE_ERROR( ret );

    DRSFreeContextInfo(pTHS, &pContextInfo);

    return ret;

}

ULONG
I_DRSGetReplInfo(
    IN      THSTATE *                     pTHS,
    IN      LPWSTR                        pszServerName,
    IN      DWORD                         dwInVersion,
    IN      DRS_MSG_GETREPLINFO_REQ *     pmsgIn,
    OUT     DWORD *                       pdwOutVersion,
    OUT     DRS_MSG_GETREPLINFO_REPLY *   pmsgOut
    )
/*++

  Routine Description:

    This routine allows access to RepInfo from DC to DC.

  Parameters:

    pTHS - Thread context
    pszServerName - Server to access
    dwInVersion -
    pmsgIn -
    pdwOutVersion -
    pmsgOut -

  Return Values:

    0 or Errors

--*/
{
    ULONG                ret = ERROR_SUCCESS;
    DRS_CONTEXT_INFO *   pContextInfo = NULL;
    BOOL                 fCallCompleted = FALSE;

    while (!fCallCompleted
           && FBindSzDRS(pTHS, pszServerName, NULL, 0, &pContextInfo, &ret)) {
        __try {  
            DRSSetContextCancelTime(DRS_CALL_GET_REPL_INFO,gulDrsRpcGetReplInfoTimeoutInMins,pContextInfo );   

            ret = _IDL_DRSGetReplInfo(pContextInfo->hDrs, 
                                      1, 
                                      pmsgIn,
                                      pdwOutVersion,
                                      pmsgOut); 

            fCallCompleted = drsIsCallComplete(0, ret, pContextInfo);
        } 
        __except (DRSHandleRpcClientException(GetExceptionInformation(),
                                              pszServerName,
                                              pContextInfo,
                                              gulDrsRpcGetReplInfoTimeoutInMins,
                                              &ret)) {
            ;
        }       
    }

    MAP_SECURITY_PACKAGE_ERROR( ret );
    DRSFreeContextInfo(pTHS, &pContextInfo);

    return ret;
}

// Max FindGC attempts to do in I_DRSxxxFindGC functions.
// This will prevent us from going into an infinite loop
// trying to find an appropriate GC when the GC invalidation
// time is too short and GCs start coming back from the 
// invalidated cache before we get a chance to find a working GC.
#define MAX_FIND_GC_ATTEMPTS 10
#define FIND_GC_TIMEOUT 5*60*1000   // in ticks

ULONG
I_DRSCrackNamesFindGC(
    THSTATE *               pTHS,
    LPWSTR                  pszPreferredDestGC,
    LPWSTR                  pszPreferredDestDnsDomainName,
    DWORD                   dwMsgInVersion,
    DRS_MSG_CRACKREQ        *pmsgIn,
    DWORD                   *pdwMsgOutVersion,
    DRS_MSG_CRACKREPLY      *pmsgOut,
    DWORD                   dwFindGCFlags
    )
// FindDC version of I_DRSCrackNames
// pszPreferredDestGC and pszPreferredDestDnsDomainName are optional parameters
// If the preferred GC is not available, or was not specified, then FindGC is
// invoked to find a GC. 
{
    FIND_DC_INFO *pGCInfo = NULL;
    ULONG        err = 0;           
    DWORD        dwStartTick = GetTickCount();
    DWORD        cAttempts = 0;

    do {
        if (pszPreferredDestGC == NULL) {
            // preferred destination GC was not specified, 
            // or it was found to be bad on the last iteration.
            if (err = FindGC(dwFindGCFlags, &pGCInfo) ) {
                // no more GCs left
                break;
            }
            pszPreferredDestGC = pGCInfo->addr;
            pszPreferredDestDnsDomainName = FIND_DC_INFO_DOMAIN_NAME(pGCInfo);
        }

        err = I_DRSCrackNames(
            pTHS,
            pszPreferredDestGC,
            pszPreferredDestDnsDomainName,
            dwMsgInVersion,
            pmsgIn,
            pdwMsgOutVersion,
            pmsgOut);

        Assert((ERROR_SUCCESS != err) || (1 == *pdwMsgOutVersion));

        if (err == ERROR_SUCCESS || (*pdwMsgOutVersion == 1 && err != ERROR_DS_GCVERIFY_ERROR)) {
            // Either we got success, or the GC at least set the out version value.
            // Also, the DC did not say that it is not a GC.
            // In either case, we should not fail over.
            if (pGCInfo) {
                THFreeEx(pTHS, pGCInfo);
            }
            break;
        }

        // Either connection to GC is bad, or this GC isn't
        // playing by the rules and is returning a version
        // I didn't ask for. Or, it said it is not a GC.
        pszPreferredDestGC = NULL;
        pszPreferredDestDnsDomainName = NULL;
        if (pGCInfo) {
            InvalidateGC(pGCInfo, err);
            THFreeEx(pTHS, pGCInfo);
        }

        // Check if we have timed out or made too many FindGC attempts
        // The subtraction takes care of DWORD wrap-around
        if (GetTickCount() - dwStartTick >= FIND_GC_TIMEOUT || ++cAttempts >= MAX_FIND_GC_ATTEMPTS) {
            err = ERROR_TIMEOUT;
            break;
        }
        // OK, we did not time out yet. Let's check another GC
        
        // We are going to repeat the RPC call. We need to clear
        // the pmsgOut structure in case the previous call has left
        // something in it. Otherwise, RPC will think we pre-allocated
        // memory when it unmarshalls the reply and will likely AV.
        // Note: we might leak some THS heap memory that RPC could have
        // possibly allocated, but it's ok since we never free it
        // in normal runs either. And we don't expect to go through
        // this loop too many times anyway.
        memset(pmsgOut, 0, sizeof(DRS_MSG_CRACKREPLY));
    } while (TRUE);

    return err;
}

ULONG
I_DRSVerifyNamesFindGC(
    THSTATE                 *pTHS,
    LPWSTR                  pszPreferredDestGC,
    LPWSTR                  pszPreferredDestDnsDomainName,
    DWORD                   dwMsgInVersion,
    DRS_MSG_VERIFYREQ       *pmsgIn,
    DWORD                   *pdwMsgOutVersion,
    DRS_MSG_VERIFYREPLY     *pmsgOut,
    DWORD                   dwFindGCFlags
    )
// FindGC version of I_DRSVerifyNames
{
    FIND_DC_INFO *pGCInfo = NULL;
    ULONG        err = 0;           
    DWORD        dwStartTick = GetTickCount();
    DWORD        cAttempts = 0;

    do {
        if (pszPreferredDestGC == NULL) {
            // preferred destination GC was not specified, 
            // or it was found to be bad on the last iteration.
            if (err = FindGC(dwFindGCFlags, &pGCInfo) ) {
                // no more GCs left
                break;
            }
            pszPreferredDestGC = pGCInfo->addr;
            pszPreferredDestDnsDomainName = FIND_DC_INFO_DOMAIN_NAME(pGCInfo);
        }

        err = I_DRSVerifyNames(
            pTHS,
            pszPreferredDestGC,
            pszPreferredDestDnsDomainName,
            dwMsgInVersion,
            pmsgIn,
            pdwMsgOutVersion,
            pmsgOut);

        Assert((ERROR_SUCCESS != err) || (1 == *pdwMsgOutVersion));

        if (err == ERROR_SUCCESS || (pmsgOut->V1.error != 0 && pmsgOut->V1.error != ERROR_DS_GC_REQUIRED)) {
            // Either we got success, or the GC set the error value.
            // Also, the DC did not say that it is not a GC.
            // In either case, we should not fail over.
            if (pGCInfo) {
                THFreeEx(pTHS, pGCInfo);
            }
            break;
        }

        // Either connection to GC is bad, or this GC isn't
        // playing by the rules and is returning a version
        // I didn't ask for.
        // Or it thinks it's not a GC.
        pszPreferredDestGC = NULL;
        pszPreferredDestDnsDomainName = NULL;
        if (pGCInfo) {
            InvalidateGC(pGCInfo, err);
            THFreeEx(pTHS, pGCInfo);
        }

        // Check if we have timed out or made too many FindGC attempts
        // The subtraction takes care of DWORD wrap-around
        if (GetTickCount() - dwStartTick >= FIND_GC_TIMEOUT || ++cAttempts >= MAX_FIND_GC_ATTEMPTS) {
            err = ERROR_TIMEOUT;
            break;
        }
        // OK, we did not time out yet. Let's check another GC
        
        // We are going to repeat the RPC call. We need to clear
        // the pmsgOut structure in case the previous call has left
        // something in it. Otherwise, RPC will think we pre-allocated
        // memory when it unmarshalls the reply and will likely AV.
        // Note: we might leak some THS heap memory that RPC could have
        // possibly allocated, but it's ok since we never free it
        // in normal runs either. And we don't expect to go through
        // this loop too many times anyway.
        memset(pmsgOut, 0, sizeof(DRS_MSG_VERIFYREPLY));
    } while (TRUE);

    return err;
}

ULONG
I_DRSGetMembershipsFindGC(
    THSTATE *   pTHS,
    LPWSTR      pszPreferredDestGC,
    LPWSTR      pszPreferredDestDnsDomainName,
    DWORD       dwFlags,
    DSNAME      **ppObjects,
    ULONG       cObjects,
    PDSNAME     pLimitingDomain,
    REVERSE_MEMBERSHIP_OPERATION_TYPE
                OperationType,
    PULONG      pErrCode,
    PULONG      pcDsNames,
    PDSNAME     ** prpDsNames,
    PULONG      *pAttributes,
    PULONG      pcSidHistory,
    PSID        **rgSidHistory,
    DWORD       dwFindGCFlags
    )
// FindGC version of I_DRSGetMemberships
{
    FIND_DC_INFO *pGCInfo = NULL;
    ULONG        err = 0;           
    DWORD        dwStartTick = GetTickCount();
    DWORD        cAttempts = 0;

    do {
        if (pszPreferredDestGC == NULL) {
            // preferred destination dsa was not specified, or it was bad.
            if (err = FindGC(dwFindGCFlags, &pGCInfo) ) {
                // no more GCs left
                break;
            }
            pszPreferredDestGC = pGCInfo->addr;
            pszPreferredDestDnsDomainName = FIND_DC_INFO_DOMAIN_NAME(pGCInfo);
        }
        // reset status
        *pErrCode = STATUS_SUCCESS;

        err = I_DRSGetMemberships(
            pTHS,
            pszPreferredDestGC,
            pszPreferredDestDnsDomainName,
            dwFlags,
            ppObjects,
            cObjects,
            pLimitingDomain,
            OperationType,
            pErrCode,
            pcDsNames,
            prpDsNames,
            pAttributes,
            pcSidHistory,
            rgSidHistory
            );

        if (err == ERROR_SUCCESS || *pErrCode != STATUS_SUCCESS) {
            // Either the call was successful, or GC set the 
            // errCode, which means we got through to the GC.
            // In this case we should not fail over.
            if (pGCInfo) {
                THFreeEx(pTHS, pGCInfo);
            }
            break;
        }

        // Either connection to GC is bad, or this GC isn't
        // playing by the rules and is returning a version
        // I didn't ask for.
        pszPreferredDestGC = NULL;
        pszPreferredDestDnsDomainName = NULL;
        if (pGCInfo) {
            InvalidateGC(pGCInfo, err);
            THFreeEx(pTHS, pGCInfo);
        }

        // Check if we have timed out or made too many FindDC attempts
        // The subtraction takes care of DWORD wrap-around
        if (GetTickCount() - dwStartTick >= FIND_GC_TIMEOUT || ++cAttempts >= MAX_FIND_GC_ATTEMPTS) {
            err = ERROR_TIMEOUT;
            break;
        }
        // OK, we did not time out yet. Let's check another GC
    } while (TRUE);

    return err;
}

ULONG
draGetServerOutgoingCalls(
    IN  THSTATE *                   pTHS,
    OUT DS_REPL_SERVER_OUTGOING_CALLS **  ppCalls
    )
/*++

Routine Description:

    Returns a list of all outstanding server call contexts

Arguments:

    pTHS (IN)

    ppContexts (OUT) - On successful return, holds the client context list.

Return Values:

    Win32 error code.

--*/
{
    DS_REPL_SERVER_OUTGOING_CALLS *   pCalls;
    DS_REPL_SERVER_OUTGOING_CALL  *   pCall;
    DRS_CONTEXT_INFO *        pContextInfo;
    DWORD                       cb;
    DWORD                       iCtx;
    LIST_ENTRY *pListEntry;

    EnterCriticalSection(&gcsDrsRpcServerCtxList);
    __try {
        if (!gfDrsRpcServerCtxListInitialized) {
            InitializeListHead(&gDrsRpcServerCtxList);  
            Assert(0 == gcNumDrsRpcServerCtxListEntries);
            gfDrsRpcServerCtxListInitialized = TRUE;
        }

        cb = offsetof(DS_REPL_SERVER_OUTGOING_CALLS, rgCall)
             + sizeof(DS_REPL_SERVER_OUTGOING_CALL) * gcNumDrsRpcServerCtxListEntries;

        pCalls = THAllocEx(pTHS, cb);

        pListEntry = gDrsRpcServerCtxList.Flink;
        for (iCtx = 0; iCtx < gcNumDrsRpcServerCtxListEntries; iCtx++) {
            pCall = &pCalls->rgCall[iCtx];
            // Check list sanity
            Assert( pListEntry );
            Assert((pListEntry->Flink->Blink)==pListEntry);
            Assert((pListEntry->Blink->Flink)==pListEntry);

            pContextInfo = CONTAINING_RECORD(pListEntry,DRS_CONTEXT_INFO,ListEntry); 

            if (pContextInfo->pszServerName) {
                pCall->pszServerName =
                 THAllocEx( pTHS, (wcslen(pContextInfo->pszServerName) + 1)*sizeof(WCHAR) );
                wcscpy( pCall->pszServerName, pContextInfo->pszServerName );
            }
            pCall->fIsHandleBound = (pContextInfo->hDrs != NULL);
            pCall->fIsHandleFromCache = pContextInfo->fIsHandleFromCache;
            pCall->fIsHandleInCache = pContextInfo->fIsHandleInCache;
            pCall->dwThreadId = pContextInfo->dwThreadId;
            pCall->dwBindingTimeoutMins = pContextInfo->dwBindingTimeoutMins;
            pCall->dstimeCreated = pContextInfo->dstimeCreated;
            pCall->dwCallType = (DWORD) pContextInfo->eCallType;

            pListEntry = pContextInfo->ListEntry.Flink;
        }

        Assert(pListEntry == &gDrsRpcServerCtxList);
        pCalls->cNumCalls = gcNumDrsRpcServerCtxListEntries;
    }
    __finally {
        LeaveCriticalSection(&gcsDrsRpcServerCtxList);
    }

    *ppCalls = pCalls;

    return 0;
}

