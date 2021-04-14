/*++

Copyright (c) 1996-2000 Microsoft Corporation.
All rights reserved.

MODULE NAME:

    kccmain.cxx

ABSTRACT:

    The KCC serves as an automated administrator.  It performs directory
    management tasks both periodically and based upon notifications.

    It is designed to live in-process with NTDSA.DLL (i.e., inside
    the LSA process).

DETAILS:

    All KCC tasks are performed indirectly by the task queue, and thus are
    processed one at a time.  Periodic tasks are registered at KCC
    initialization and reschedule themselves when they complete.
    Notifications similarly cause tasks to be added to the task queue
    (which do not automatically reschedule themselves).

CREATED:

    01/13/97    Jeff Parham (jeffparh)

REVISION HISTORY:

--*/

#include <NTDSpchx.h>
#pragma  hdrstop

#include <limits.h>
#include <dsconfig.h>

#include "kcc.hxx"
#include "kcctask.hxx"
#include "kccconn.hxx"
#include "kcctopl.hxx"
#include "kccsite.hxx"
#include "kccstale.hxx"
#include "kcctools.hxx"

#define  FILENO FILENO_KCC_KCCMAIN



//
// KCC global variables
//

KCC_STATE   geKccState                  = KCC_STOPPED;
HANDLE      ghKccShutdownEvent          = NULL;
BOOL        gfRunningUnderAltID         = FALSE;


// A bitmask specifying all valid flags which may be passed
// in a DRS_MSG_KCC_EXECUTE message.
#define ALL_DS_KCC_FLAGS_MASK           (  DS_KCC_FLAG_ASYNC_OP \
                                         | DS_KCC_FLAG_DAMPED   \
                                        )

// Time after boot of first replication topology update and the interval between
// subsequent executions of this task.
#define KCC_DEFAULT_UPDATE_TOPL_DELAY   (5 * MINS_IN_SECS)          // Seconds.
#define KCC_MIN_UPDATE_TOPL_DELAY       (20)                        // Seconds.
#define KCC_MAX_UPDATE_TOPL_DELAY       (ULONG_MAX)                 // Seconds.

#define KCC_DEFAULT_UPDATE_TOPL_PERIOD  (15 * MINS_IN_SECS)         // Seconds.
#define KCC_MIN_UPDATE_TOPL_PERIOD      (20)                        // Seconds.
#define KCC_MAX_UPDATE_TOPL_PERIOD      (ULONG_MAX)                 // Seconds.

DWORD gcSecsUntilFirstTopologyUpdate  = 0;
DWORD gcSecsBetweenTopologyUpdates    = 0;


// These global values are the thresholds by which servers necessary for the
// ring topology and servers needed for gc topology are measured against when
// determing if they are valid source servers.
#define KCC_DEFAULT_CRIT_FAILOVER_TRIES     (0)
#define KCC_MIN_CRIT_FAILOVER_TRIES         (0)
#define KCC_MAX_CRIT_FAILOVER_TRIES         (ULONG_MAX)

#define KCC_DEFAULT_CRIT_FAILOVER_TIME      (2 * HOURS_IN_SECS)     // Seconds.
#define KCC_MIN_CRIT_FAILOVER_TIME          (0)                     // Seconds.
#define KCC_MAX_CRIT_FAILOVER_TIME          (ULONG_MAX)             // Seconds.

#define KCC_DEFAULT_NONCRIT_FAILOVER_TRIES  (1)
#define KCC_MIN_NONCRIT_FAILOVER_TRIES      (0)
#define KCC_MAX_NONCRIT_FAILOVER_TRIES      (ULONG_MAX)

#define KCC_DEFAULT_NONCRIT_FAILOVER_TIME   (12 * HOURS_IN_SECS)    // Seconds.
#define KCC_MIN_NONCRIT_FAILOVER_TIME       (0)                     // Seconds.
#define KCC_MAX_NONCRIT_FAILOVER_TIME       (ULONG_MAX)             // Seconds.

#define KCC_DEFAULT_INTERSITE_FAILOVER_TRIES  (1)
#define KCC_MIN_INTERSITE_FAILOVER_TRIES      (0)
#define KCC_MAX_INTERSITE_FAILOVER_TRIES      (ULONG_MAX)

#define KCC_DEFAULT_INTERSITE_FAILOVER_TIME   (2 * HOURS_IN_SECS)   // Seconds.
#define KCC_MIN_INTERSITE_FAILOVER_TIME       (0)                   // Seconds.
#define KCC_MAX_INTERSITE_FAILOVER_TIME       (ULONG_MAX)           // Seconds.

#define KCC_DEFAULT_CONNECTION_PROBATION_TIME (8 * HOURS_IN_SECS)   // Seconds.
#define KCC_MIN_CONNECTION_PROBATION_TIME     (0)                   // Seconds.
#define KCC_MAX_CONNECTION_PROBATION_TIME     (ULONG_MAX)           // Seconds.

#define KCC_DEFAULT_CONNECTION_RETENTION_TIME (7 * 24 * HOURS_IN_SECS)   // Seconds.
#define KCC_MIN_CONNECTION_RETENTION_TIME     (0)                   // Seconds.
#define KCC_MAX_CONNECTION_RETENTION_TIME     (ULONG_MAX)           // Seconds.

#define KCC_DEFAULT_CONN_REPEAT_DEL_TOLERANCE (3)   // Occurrences
#define KCC_MIN_CONN_REPEAT_DEL_TOLERANCE     (1)                   // Occurrences.
#define KCC_MAX_CONN_REPEAT_DEL_TOLERANCE     (ULONG_MAX)           // Occurrences.

DWORD gcCriticalLinkFailuresAllowed     = 0;
DWORD gcSecsUntilCriticalLinkFailure    = 0;
DWORD gcNonCriticalLinkFailuresAllowed  = 0;
DWORD gcSecsUntilNonCriticalLinkFailure = 0;
DWORD gcIntersiteLinkFailuresAllowed    = 0;
DWORD gcSecsUntilIntersiteLinkFailure   = 0;
DWORD gcConnectionProbationSecs         = 0;
DWORD gcConnectionRetentionSecs         = 0;
DWORD gcConnectionRepeatedDeletionTolerance = 0;

// Do we allow asynchronous replication (e.g., over SMTP) of writeable domain
// NC info?  We currently inhibit this, ostensibly to reduce our test matrix.

#define KCC_DEFAULT_ALLOW_MBR_BETWEEN_DCS_OF_SAME_DOMAIN    (FALSE)
#define KCC_MIN_ALLOW_MBR_BETWEEN_DCS_OF_SAME_DOMAIN        (0)
#define KCC_MAX_ALLOW_MBR_BETWEEN_DCS_OF_SAME_DOMAIN        (ULONG_MAX)

BOOL gfAllowMbrBetweenDCsOfSameDomain = FALSE;


// What priority does the topology generation thread run at? The thread priorities
// are values in the range (-2,..,2), but the registry can only store DWORDs, so
// we bias the stored priority values with KCC_THREAD_PRIORITY_BIAS.
#define KCC_DEFAULT_THREAD_PRIORITY 2
#define KCC_MIN_THREAD_PRIORITY     0
#define KCC_MAX_THREAD_PRIORITY     4
DWORD gdwKccThreadPriority;


// How long should a repsTo fail before it is considered stale.
#define KCC_DEFAULT_REPSTO_FAILURE_TIME (24 * HOURS_IN_SECS)   // Seconds.
#define KCC_MIN_REPSTO_FAILURE_TIME     (1)                   // Seconds.
#define KCC_MAX_REPSTO_FAILURE_TIME     (ULONG_MAX)           // Seconds.
DWORD gcSecsUntilRepsToFailure;

// How many seconds into the future do we look for duplicate
// entries in the task queue.
#define KCC_DEFAULT_TASK_DAMPENING_TIME       (20)                  // Seconds.
#define KCC_MIN_TASK_DAMPENING_TIME           (0)                   // Seconds.
#define KCC_MAX_TASK_DAMPENING_TIME           (ULONG_MAX)           // Seconds.
DWORD gcTaskDampeningSecs = KCC_DEFAULT_TASK_DAMPENING_TIME;


// Registry key and event to track changes to our registry parameters.
HKEY    ghkParameters = NULL;
HANDLE  ghevParametersChange = NULL;
        

// Default Intra-site schedule.  This determines the polling interval that
// destinations use to ask for changes from their sources.
// Since notifications are the perferred mechanism to disseminating changes,
// make the interval relatively infrequent.
//
// Note:- If you change this, please make sure to change the
//        g_defaultSchedDBData[] defined in dsamain\boot\addobj.cxx
//        which defines the intrasite schedule in global NT schedule format.

const DWORD rgdwDefaultIntrasiteSchedule[] = {
    sizeof(SCHEDULE) + SCHEDULE_DATA_ENTRIES,       // Size (in bytes)
    0,                                              // Bandwidth
    1,                                              // NumberOfSchedules
    SCHEDULE_INTERVAL,                              // Schedules[0].Type
    sizeof(SCHEDULE),                               // Schedules[0].Offset
    0x01010101, 0x01010101, 0x01010101, 0x01010101, // Schedule 0 data
    0x01010101, 0x01010101, 0x01010101, 0x01010101, //   (once an hour)
    0x01010101, 0x01010101, 0x01010101, 0x01010101, // 4 DWORDs * 4 bytes/DWORD
    0x01010101, 0x01010101, 0x01010101, 0x01010101, //   * 10.5 rows
                                                    //   = 168 bytes
    0x01010101, 0x01010101, 0x01010101, 0x01010101, //   = SCHEDULE_DATA_ENTRIES
    0x01010101, 0x01010101, 0x01010101, 0x01010101,
    0x01010101, 0x01010101, 0x01010101, 0x01010101,
    0x01010101, 0x01010101, 0x01010101, 0x01010101,
    
    0x01010101, 0x01010101, 0x01010101, 0x01010101,
    0x01010101, 0x01010101, 0x01010101, 0x01010101,
    0x01010101, 0x01010101
};
const SCHEDULE * gpDefaultIntrasiteSchedule = (SCHEDULE *) rgdwDefaultIntrasiteSchedule;

// The intrasite schedule object is first created when we read the local
// NTDS Site Settings object, which always precedes its use. 
TOPL_SCHEDULE gpIntrasiteSchedule = NULL;
BOOLEAN       gfIntrasiteSchedInited = FALSE;


KCC_TASK_UPDATE_REPL_TOPOLOGY   gtaskUpdateReplTopology;
KCC_CONNECTION_FAILURE_CACHE    gConnectionFailureCache;
KCC_LINK_FAILURE_CACHE          gLinkFailureCache;
KCC_DS_CACHE *                  gpDSCache = NULL;
KCC_CONNECTION_DELETION_CACHE   gConnectionDeletionCache;

//
// For efficiency sake, don't start refreshing until we have at least
// seven servers, since we typically won't have any optimizing edges
// to refresh.
//
BOOL  gfLastServerCountSet = FALSE;
ULONG gLastServerCount = 0;

#ifdef ANALYZE_STATE_SERVER
BOOL                            gfDumpStaleServerCaches = FALSE;
BOOL                            gfDumpConnectionReason  = FALSE;
#endif

// Event logging config (as exported from ntdsa.dll).
DS_EVENT_CONFIG * gpDsEventConfig = NULL;

void KccLoadParameters();


DWORD
KccInitialize()
{
    DWORD         dwWin32Status = ERROR_SUCCESS;
    SPAREFN_INFO  rgSpareInfo[1];
    DWORD         cSpares = sizeof(rgSpareInfo) / sizeof(rgSpareInfo[0]);
    DWORD         winError;

    if ( KCC_STOPPED != geKccState )
    {
        Assert( !"Attempt to reinitialize KCC while it's running!" );
        dwWin32Status = ERROR_DS_INTERNAL_FAILURE;
    }
    else
    {
        // initialize KCC state
        ghKccShutdownEvent = NULL;

        // Initialize logging (as exported from ntdsa.dll).
        gpDsEventConfig = DsGetEventConfig();

        // Open parameters reg key.
        winError = RegOpenKey(HKEY_LOCAL_MACHINE,
                              DSA_CONFIG_SECTION,
                              &ghkParameters);
        if (0 != winError) {
            LogUnhandledError(winError);
        }
            
        // Create an event to signal changes in the parameters reg key.
        ghevParametersChange = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (NULL == ghevParametersChange) {
            winError = GetLastError();
            LogUnhandledError(winError);
        }
        
        // Watch for changes in the parameters reg key.
        rgSpareInfo[0].hevSpare = ghevParametersChange;
        rgSpareInfo[0].pfSpare  = KccLoadParameters;

        // Get our current config parameters from the registry.
        KccLoadParameters();

        ghKccShutdownEvent = CreateEvent(
                                NULL,   // no security descriptor
                                TRUE,   // is manual-reset
                                FALSE,  // !is initially signalled
                                NULL    // no name
                                );

        if ( NULL == ghKccShutdownEvent )
        {
            dwWin32Status = GetLastError();
            LogUnhandledErrorAnonymous( dwWin32Status );
        }
        else
        {
            if (!InitTaskScheduler(cSpares, rgSpareInfo, TRUE)) {
                dwWin32Status = ERROR_NOT_ENOUGH_MEMORY;

                LogUnhandledErrorAnonymous( dwWin32Status );
            }
            else
            {
                // initialize global ConnectionFailureCache
                if ( !gConnectionFailureCache.Init() ) {
                    // don't bail out
                    DPRINT( 0, "gConnectionFailureCache.Init failed\n" );
                }
                
                // initialize global ConnectionFailureCache
                if ( !gLinkFailureCache.Init() ) {
                    // don't bail out
                    DPRINT( 0, "gLinkFailureCache.Init failed\n" );
                }
                    
                // register periodic tasks
                if ( !gtaskUpdateReplTopology.Init() ) {
                    dwWin32Status = ERROR_NOT_ENOUGH_MEMORY;
                    LogUnhandledErrorAnonymous( dwWin32Status );
                }
            }
        }

        if ( dwWin32Status == ERROR_SUCCESS )
        {
            LogEvent(
                DS_EVENT_CAT_KCC,
                DS_EVENT_SEV_EXTENSIVE,
                DIRLOG_CHK_INIT_SUCCESS,
                0,
                0,
                0
                );

            geKccState = KCC_STARTED;
        }
        else
        {
            LogEvent(
                DS_EVENT_CAT_KCC,
                DS_EVENT_SEV_ALWAYS,
                DIRLOG_CHK_INIT_FAILURE,
                szInsertWin32ErrCode( dwWin32Status ),
                szInsertWin32Msg( dwWin32Status ),
                0
                );

            // initialization failed; shut down
            Assert( !"KCC could not be initialized!" );
            KccUnInitializeTrigger( );
            KccUnInitializeWait( INFINITE );
        }
    }

    return dwWin32Status;
}


void
KccUnInitializeTrigger( )
{
    if (NULL != ghKccShutdownEvent)
    {
        // KCC was started -- trigger shutdown.
        geKccState = KCC_STOPPING;
        
        // signal logging change monitor to shut down
        SetEvent( ghKccShutdownEvent );
        ShutdownTaskSchedulerTrigger( );
    }
    else
    {
        // KCC was never started.  Don't log events, as eventing (specifically
        // ntdskcc!gpDsEventConfig) has not been initialized.
        Assert(KCC_STOPPED == geKccState);
    }
}


DWORD
KccUnInitializeWait(
    DWORD   dwMaxWaitInMsec
    )
{
    DWORD       dwWin32Status = ERROR_SUCCESS;
    DWORD       waitStatus;

    if (NULL != ghKccShutdownEvent)
    {
        // KCC was started -- wait for shutdown, if it hasn't completed yet.
        if (KCC_STOPPED != geKccState)
        {
    
            if ( !ShutdownTaskSchedulerWait( dwMaxWaitInMsec ) )
            {
                dwWin32Status = ERROR_INVALID_FUNCTION;
            }
            else
            {
                CloseHandle( ghKccShutdownEvent );
    
                ghKccShutdownEvent = NULL;
                geKccState         = KCC_STOPPED;
            }
        }
    
        if ( dwWin32Status == ERROR_SUCCESS )
        {
            LogEvent(
                DS_EVENT_CAT_KCC,
                DS_EVENT_SEV_EXTENSIVE,
                DIRLOG_CHK_STOP_SUCCESS,
                0,
                0,
                0
                );
        }
        else
        {
            LogEvent(
                DS_EVENT_CAT_KCC,
                DS_EVENT_SEV_ALWAYS,
                DIRLOG_CHK_STOP_FAILURE,
                szInsertWin32ErrCode( dwWin32Status ),
                szInsertWin32Msg( dwWin32Status ),
                0
                );
    
            Assert( !"KCC could not be stopped!" );
        }
    }
    else
    {
        // KCC was never started.  Don't log events, as eventing (specifically
        // ntdskcc!gpDsEventConfig) has not been initialized.
        Assert(KCC_STOPPED == geKccState);
    }

    return dwWin32Status;
}


#ifdef INCLUDE_UNIT_TESTS

void
KccBOTest()
{
    // WARNING: DELIBERATE BUFFER OVERFLOW TO TEST /GS SUPPORT
    char pStackBuffer[10], *pHeapBuffer;
    DWORD err, dwBufferSize;
    
    err = GetConfigParamAllocA( KCC_BO_TEST, (PVOID*) &pHeapBuffer, &dwBufferSize );
    if( !err && pHeapBuffer ) {
        DPRINT1( 0, "BO Test: %s\n", pHeapBuffer );
        memcpy( pStackBuffer, pHeapBuffer, dwBufferSize );
        free( pHeapBuffer );
        Beep( 440, 1000 );
    } else {
        Beep( 880, 250 );
    }
}

#endif


void
KccLoadParameters()
/*++

Routine Description:

    Refreshes intenal globals derived from our registry config.

Arguments:

    None.

Return Values:

    None.

--*/
{
    const struct {
        LPSTR   pszValueName;
        DWORD   dwDefaultValue;
        DWORD   dwMinValue;
        DWORD   dwMaxValue;
        DWORD   dwMultiplier;
        DWORD * pdwMultipliedValue;
    } rgValues[] =  {   { KCC_UPDATE_TOPL_DELAY,
                          KCC_DEFAULT_UPDATE_TOPL_DELAY,
                          KCC_MIN_UPDATE_TOPL_DELAY,
                          KCC_MAX_UPDATE_TOPL_DELAY,
                          SECS_IN_SECS,
                          &gcSecsUntilFirstTopologyUpdate },
                        
                        { KCC_UPDATE_TOPL_PERIOD,
                          KCC_DEFAULT_UPDATE_TOPL_PERIOD,
                          KCC_MIN_UPDATE_TOPL_PERIOD,
                          KCC_MAX_UPDATE_TOPL_PERIOD,
                          SECS_IN_SECS,
                          &gcSecsBetweenTopologyUpdates },
                        
                        { KCC_CRIT_FAILOVER_TRIES,
                          KCC_DEFAULT_CRIT_FAILOVER_TRIES,
                          KCC_MIN_CRIT_FAILOVER_TRIES,
                          KCC_MAX_CRIT_FAILOVER_TRIES,
                          1,
                          &gcCriticalLinkFailuresAllowed },

                        { KCC_CRIT_FAILOVER_TIME,
                          KCC_DEFAULT_CRIT_FAILOVER_TIME,
                          KCC_MIN_CRIT_FAILOVER_TIME,
                          KCC_MAX_CRIT_FAILOVER_TIME,
                          SECS_IN_SECS,
                          &gcSecsUntilCriticalLinkFailure },

                        { KCC_NONCRIT_FAILOVER_TRIES,
                          KCC_DEFAULT_NONCRIT_FAILOVER_TRIES,
                          KCC_MIN_NONCRIT_FAILOVER_TRIES,
                          KCC_MAX_NONCRIT_FAILOVER_TRIES,
                          1,
                          &gcNonCriticalLinkFailuresAllowed },

                        { KCC_NONCRIT_FAILOVER_TIME,
                          KCC_DEFAULT_NONCRIT_FAILOVER_TIME,
                          KCC_MIN_NONCRIT_FAILOVER_TIME,
                          KCC_MAX_NONCRIT_FAILOVER_TIME,
                          SECS_IN_SECS,
                          &gcSecsUntilNonCriticalLinkFailure },

                        { KCC_INTERSITE_FAILOVER_TRIES,
                          KCC_DEFAULT_INTERSITE_FAILOVER_TRIES,
                          KCC_MIN_INTERSITE_FAILOVER_TRIES,
                          KCC_MAX_INTERSITE_FAILOVER_TRIES,
                          1,
                          &gcIntersiteLinkFailuresAllowed },

                        { KCC_INTERSITE_FAILOVER_TIME,
                          KCC_DEFAULT_INTERSITE_FAILOVER_TIME,
                          KCC_MIN_INTERSITE_FAILOVER_TIME,
                          KCC_MAX_INTERSITE_FAILOVER_TIME,
                          SECS_IN_SECS,
                          &gcSecsUntilIntersiteLinkFailure },

#ifdef ALLOW_MBR_BETWEEN_DCS_OF_SAME_DOMAIN_IF_REGKEY_SET
                        { KCC_ALLOW_MBR_BETWEEN_DCS_OF_SAME_DOMAIN,
                          KCC_DEFAULT_ALLOW_MBR_BETWEEN_DCS_OF_SAME_DOMAIN,
                          KCC_MIN_ALLOW_MBR_BETWEEN_DCS_OF_SAME_DOMAIN,
                          KCC_MAX_ALLOW_MBR_BETWEEN_DCS_OF_SAME_DOMAIN,
                          1,
                          (DWORD *) &gfAllowMbrBetweenDCsOfSameDomain },
#endif

                        { KCC_THREAD_PRIORITY,
                          KCC_DEFAULT_THREAD_PRIORITY,
                          KCC_MIN_THREAD_PRIORITY,
                          KCC_MAX_THREAD_PRIORITY,
                          1,
                          &gdwKccThreadPriority },
                          
                        { KCC_CONNECTION_PROBATION_TIME,
                          KCC_DEFAULT_CONNECTION_PROBATION_TIME,
                          KCC_MIN_CONNECTION_PROBATION_TIME,
                          KCC_MAX_CONNECTION_PROBATION_TIME,
                          SECS_IN_SECS,
                          &gcConnectionProbationSecs },

                        { KCC_CONNECTION_RETENTION_TIME,
                          KCC_DEFAULT_CONNECTION_RETENTION_TIME,
                          KCC_MIN_CONNECTION_RETENTION_TIME,
                          KCC_MAX_CONNECTION_RETENTION_TIME,
                          SECS_IN_SECS,
                          &gcConnectionRetentionSecs },

                        { KCC_CONN_REPEAT_DEL_TOLERANCE,
                          KCC_DEFAULT_CONN_REPEAT_DEL_TOLERANCE,
                          KCC_MIN_CONN_REPEAT_DEL_TOLERANCE,
                          KCC_MAX_CONN_REPEAT_DEL_TOLERANCE,
                          1,
                          &gcConnectionRepeatedDeletionTolerance },

                        { KCC_REPSTO_FAILURE_TIME,
                          KCC_DEFAULT_REPSTO_FAILURE_TIME,
                          KCC_MIN_REPSTO_FAILURE_TIME,
                          KCC_MAX_REPSTO_FAILURE_TIME,
                          SECS_IN_SECS,
                          &gcSecsUntilRepsToFailure },

                        { KCC_TASK_DAMPENING_TIME,
                          KCC_DEFAULT_TASK_DAMPENING_TIME,
                          KCC_MIN_TASK_DAMPENING_TIME,
                          KCC_MAX_TASK_DAMPENING_TIME,
                          SECS_IN_SECS,
                          &gcTaskDampeningSecs },
                    };
    const DWORD cValues = sizeof(rgValues) / sizeof(rgValues[0]);

    DWORD dwValue;
    DWORD err;
    
    Assert(NULL != ghkParameters);
    Assert(NULL != ghevParametersChange);

    err = RegNotifyChangeKeyValue(ghkParameters,
                                  TRUE,
                                  REG_NOTIFY_CHANGE_LAST_SET,
                                  ghevParametersChange,
                                  TRUE);
    Assert(0 == err);
    
    for (DWORD i = 0; i < cValues; i++) {
        if (GetConfigParam(rgValues[i].pszValueName, &dwValue, sizeof(dwValue))) {
            dwValue = rgValues[i].dwDefaultValue;
            Assert(dwValue >= rgValues[i].dwMinValue);
            Assert(dwValue <= rgValues[i].dwMaxValue);
        }
        else if (dwValue < rgValues[i].dwMinValue) {
            LogEvent(DS_EVENT_CAT_KCC,
                     DS_EVENT_SEV_ALWAYS,
                     DIRLOG_CHK_CONFIG_PARAM_TOO_LOW,
                     szInsertWC(rgValues[i].pszValueName),
                     szInsertUL(dwValue),
                     szInsertUL(rgValues[i].dwMinValue));
            dwValue = rgValues[i].dwMinValue;
        }
        else if (dwValue > rgValues[i].dwMaxValue) {
            LogEvent(DS_EVENT_CAT_KCC,
                     DS_EVENT_SEV_ALWAYS,
                     DIRLOG_CHK_CONFIG_PARAM_TOO_HIGH,
                     szInsertWC(rgValues[i].pszValueName),
                     szInsertUL(dwValue),
                     szInsertUL(rgValues[i].dwMaxValue));
            dwValue = rgValues[i].dwMaxValue;
        }
        
        *(rgValues[i].pdwMultipliedValue) = dwValue * rgValues[i].dwMultiplier;
    }

    #ifdef INCLUDE_UNIT_TESTS
        // WARNING: Buffer overflow to test /GS support
        KccBOTest();
    #endif
}


DWORD
KccExecuteTask(
    IN  DWORD                   dwInVersion,
    IN  DRS_MSG_KCC_EXECUTE *   pMsgIn
    )
{
    DWORD dwFlags;

    // Check parameters
    Assert( NULL!=pMsgIn );
    if(    (1 != dwInVersion)
        || (NULL==pMsgIn)
        || (DS_KCC_TASKID_UPDATE_TOPOLOGY != pMsgIn->V1.dwTaskID) )
    {
        return ERROR_INVALID_PARAMETER;
    }

    // Kcc cannot be executed until the task queue is up and running
    if ( !gfIsTqRunning ) {
        return ERROR_DS_NOT_INSTALLED;
    }

    // Clear any unexpected flags
    dwFlags = pMsgIn->V1.dwFlags & ALL_DS_KCC_FLAGS_MASK;
    
    return gtaskUpdateReplTopology.Trigger(dwFlags, gcTaskDampeningSecs);
}


DWORD
KccGetFailureCache(
    IN  DWORD                         InfoType,
    OUT DS_REPL_KCC_DSA_FAILURESW **  ppFailures
    )
/*++

Routine Description:

    Returns the contents of the connection or link failure cache.

Arguments:

    InfoType (IN) - Identifies the cache to return -- either
        DS_REPL_INFO_KCC_DSA_CONNECT_FAILURES or
        DS_REPL_INFO_KCC_DSA_LINK_FAILURES.
    
    ppFailures (OUT) - On successful return, holds the contents of the cache.
    
Return Values:

    Win32 error code.

--*/
{
    DWORD                   winError;
    KCC_CACHE_LINKED_LIST * pFailureCache;

    // Check parameters
    if( NULL==ppFailures ) {
        return ERROR_INVALID_PARAMETER;
    }

    if (DS_REPL_INFO_KCC_DSA_CONNECT_FAILURES == InfoType) {
        winError = gConnectionFailureCache.Extract(ppFailures);
    }
    else if (DS_REPL_INFO_KCC_DSA_LINK_FAILURES == InfoType) {
        winError = gLinkFailureCache.Extract(ppFailures);
    }
    else {
        winError = ERROR_INVALID_PARAMETER;
    }
    
    return winError;
}


// Override global new and delete to use the thread-heap
// and raise an exception on failure

void *
__cdecl
operator new(
    size_t  cb
    )
{
    void * pv;

    pv = THAlloc( cb );
    if ( NULL == pv )
    {
        DPRINT1( 0, "Failed to allocate %d bytes\n", cb );
        KCC_MEM_EXCEPT( cb );
    }

    return pv;
}


void
__cdecl
operator delete(
    void *   pv
    )
{
    THFree( pv );
}
