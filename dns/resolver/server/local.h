/*++

Copyright (c) 2000-2001  Microsoft Corporation

Module Name:

    local.h

Abstract:

    DNS Resolver.

    DNS Resolver service local include file.

Author:

    Jim Gilroy  (jamesg)        March 2000

Revision History:

--*/


#ifndef _LOCAL_INCLUDED_
#define _LOCAL_INCLUDED_


#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <ntseapi.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef _WINSOCK2API_
#include <winsock2.h>
#endif
#include <sockreg.h>
#include <windns.h>
#include <netevent.h>       // DNS events

//#define  DNSAPI_INTERNAL
#define  DNS_NEW_DEFS 1     // QFE builds use new definitions

#include "dnsrslvr.h"
#include <dnslibp.h>
#include "registry.h"
#include <dnsapip.h>

#define  ENABLE_DEBUG_LOGGING 1
#include "logit.h"
#include "dnsmsg.h"
#include "heapdbg.h"


//
//  Resolver debug flags
//

#define DNS_DBG_CACHE       DNS_DBG_QUERY


//
//  Cache definitions
//

#define NO_LOCK                 0
#define LOCKED                  1

#define ONE_YEAR_IN_SECONDS     60*60*24*365


//
//  Bypassing cache completely check
//

#define COMBINED_BYPASS_CACHE_FLAG  (DNS_QUERY_BYPASS_CACHE | DNS_QUERY_NO_HOSTS_FILE)

#define SKIP_CACHE_LOOKUP(Flags)    \
        ( ((Flags) & COMBINED_BYPASS_CACHE_FLAG) == COMBINED_BYPASS_CACHE_FLAG )


//
//  Cache defintion
//
//  Should be private to cache but is currently
//  exposed in enum routines
//

typedef struct _CacheEntry
{
    struct _CacheEntry *    pNext;
    PWSTR                   pName;
    DWORD                   Reserved;
    DWORD                   MaxCount;
    PDNS_RECORD             Records[ 1 ];
}
CACHE_ENTRY, *PCACHE_ENTRY;

        

#if 1
//
//  Config
//
//  Saved NT5 registry stuff.
//  In typical fashsion this was packed away in a few different places.
//  Keeping around until all of this is properly put to bed.
//
//
//  Registry value routine prototypes
//
#define DNS_DEFAULT_HASH_TABLE_SIZE                211      // A prime number
#define DNS_DEFAULT_NEGATIVE_SOA_CACHE_TIME        10       // 10 seconds
#define DNS_DEFAULT_NET_FAILURE_CACHE_TIME         30       // 30 seconds
#define DNS_DEFAULT_ADAPTER_TIMEOUT_CACHE_TIME     2*60     // 2 minutes
#define DNS_DEFAULT_MESSAGE_POPUP_LIMIT            0        // Don't allow!
#define DNS_DEFAULT_MAX_SOA_TTL_LIMIT              5*60     // 5 minutes
#define DNS_DEFAULT_RESET_SERVER_PRIORITIES_TIME   5*60     // 5 minutes
#endif


//
//  Event tags
//  Make recognizable DWORD tags for in memory log.
//

#define FOUR_CHARACTER_CONSTANT(a,b,c,d) \
        ((((DWORD)d) << 24) + (((DWORD)c) << 16) + (((DWORD)b) << 8) + ((DWORD)a))

#define RES_EVENT_INITNET_START         FOUR_CHARACTER_CONSTANT('N','e','t','+')
#define RES_EVENT_INITNET_END           FOUR_CHARACTER_CONSTANT('N','e','t','-')
#define RES_EVENT_REGISTER_SCH          FOUR_CHARACTER_CONSTANT('S','c','h','+')
#define RES_EVENT_CREATE_EVENT          FOUR_CHARACTER_CONSTANT('E','v','t','+')
#define RES_EVENT_START_RPC             FOUR_CHARACTER_CONSTANT('R','p','c','+')
#define RES_EVENT_STOP_RPC              FOUR_CHARACTER_CONSTANT('R','p','c','-')
#define RES_EVENT_STATUS                FOUR_CHARACTER_CONSTANT('S','t','a','t')
#define RES_EVENT_UPDATE_STATE          FOUR_CHARACTER_CONSTANT('U','p','d',' ')
#define RES_EVENT_UPDATE_STATUS         FOUR_CHARACTER_CONSTANT('U','p','d','-')
#define RES_EVENT_INITCRIT_START        FOUR_CHARACTER_CONSTANT('I','C','S','+')
#define RES_EVENT_INITCRIT_END          FOUR_CHARACTER_CONSTANT('I','C','S','-')
#define RES_EVENT_DELCRIT_START         FOUR_CHARACTER_CONSTANT('D','C','S','+')
#define RES_EVENT_DELCRIT_END           FOUR_CHARACTER_CONSTANT('D','C','S','-')
#define RES_EVENT_STARTED               FOUR_CHARACTER_CONSTANT('S','t','a','r')
#define RES_EVENT_STOPPING              FOUR_CHARACTER_CONSTANT('S','t','o','p')
#define RES_EVENT_SHUTDOWN              FOUR_CHARACTER_CONSTANT('S','h','u','t')

#define RES_EVENT_SERVICE_CONTROL       FOUR_CHARACTER_CONSTANT('S','v','c',' ')
#define RES_EVENT_INIT_CACHE            FOUR_CHARACTER_CONSTANT('C','c','h','+')
#define RES_EVENT_FLUSH_CACHE           FOUR_CHARACTER_CONSTANT('F','l','s','h')
#define RES_EVENT_PNP_START             FOUR_CHARACTER_CONSTANT('P','n','P','+')
#define RES_EVENT_PNP_END               FOUR_CHARACTER_CONSTANT('P','n','P','-')


//
//  Service
//

extern  HANDLE      g_hStopEvent;
extern  BOOL        g_StopFlag;
extern  BOOL        g_WakeFlag;
extern  BOOL        g_GarbageCollectFlag;

extern  BOOL        g_LogTraceInfo;

//
//  Config (config.c) 
//

extern  DWORD       g_MaxSOACacheEntryTtlLimit;
extern  DWORD       g_NegativeSOACacheTime;
extern  DWORD       g_MessagePopupLimit;
extern  DWORD       g_NetFailureCacheTime;

//
//  Config info (config.c)
//

extern  PDNS_NETINFO    g_NetworkInfo;
extern  DWORD           g_TimeOfLastPnPUpdate;

//
//  Cache (cache.c)
//

extern  PCACHE_ENTRY *  g_HashTable;
extern  DWORD           g_HashTableSize;
extern  DWORD           g_EntryCount;
extern  DWORD           g_RecordSetCount;


//
//  Network failure caching
//

extern  DWORD       g_NetFailureTime;
extern  DNS_STATUS  g_NetFailureStatus;
extern  DWORD       g_TimedOutAdapterTime;
extern  DWORD       g_ResetServerPrioritiesTime;
extern  BOOL        g_fTimedOutAdapter;
extern  DNS_STATUS  g_PreviousNetFailureStatus;
extern  DWORD       g_MessagePopupStrikes;
extern  DWORD       g_NumberOfMessagePopups;


//
//  Locking
//

extern  CRITICAL_SECTION        CacheCS;
extern  CRITICAL_SECTION        NetworkFailureCS;
extern  CRITICAL_SECTION        NetinfoCS;
extern  TIMED_LOCK              NetinfoBuildLock;


#define LOCK_CACHE()            Cache_Lock( 0 )
#define LOCK_CACHE_NO_START()   Cache_Lock( 1 )
#define UNLOCK_CACHE()          Cache_Unlock()

#define LOCK_NET_FAILURE()      EnterCriticalSection( &NetworkFailureCS )
#define UNLOCK_NET_FAILURE()    LeaveCriticalSection( &NetworkFailureCS )


//
//  Cache flush levels
//
//  Note, these aren't bit flags, just made them that way for
//  easy reading.
//

#define FLUSH_LEVEL_NORMAL      (0)
#define FLUSH_LEVEL_INVALID     (1)
#define FLUSH_LEVEL_WIRE        (2)
#define FLUSH_LEVEL_STRONG      (4)
#define FLUSH_LEVEL_CLEANUP     (8)

#define FLUSH_LEVEL_GARBAGE     (FLUSH_LEVEL_WIRE)



//
//  Resolver RPC access control
//

#define RESOLVER_ACCESS_READ        0x00000001
#define RESOLVER_ACCESS_ENUM        0x00000002
#define RESOLVER_ACCESS_QUERY       0x00000010
#define RESOLVER_ACCESS_FLUSH       0x00000020
#define RESOLVER_ACCESS_REGISTER    0x00000100

//
//  Generic mapping for resolver
//
//  note:  not using generic bits for access control,
//         but still must provide mapping
//

#define RESOLVER_GENERIC_READ       ((STANDARD_RIGHTS_READ)     | \
                                    (RESOLVER_ACCESS_READ)      | \
                                    (RESOLVER_ACCESS_QUERY)     | \
                                    (RESOLVER_ACCESS_ENUM))

#define RESOLVER_GENERIC_EXECUTE    RESOLVER_GENERIC_READ

#define RESOLVER_GENERIC_WRITE      ((RESOLVER_GENERIC_READ)    | \
                                    (RESOLVER_ACCESS_FLUSH))

#define RESOLVER_GENERIC_ALL        ((RESOLVER_GENERIC_WRITE)   | \
                                    (RESOLVER_ACCESS_REGISTER))


//
//  Cache memory
//
//  Note, heap global doesn't need exposure if functionalize
//

extern  HANDLE g_CacheHeap;

#define CACHE_HEAP_ALLOC_ZERO(size) \
        HeapAlloc( g_CacheHeap, HEAP_ZERO_MEMORY, (size) )

#define CACHE_HEAP_ALLOC(size) \
        HeapAlloc( g_CacheHeap, 0, (size) )

#define CACHE_HEAP_FREE(p) \
        HeapFree( g_CacheHeap, 0, (p) )


//
//  Record and RPC memory:
//
//  Note:  most records are created by dnsapi heap -- from
//  query or hosts file routines.  However, we do create
//  name error caching records ourselves using dnslib routines.
//
//  This means -- until we either
//      - extend query or dnslib record creation interfaces to
//        include heap parameter
//      - explicitly free and recreate
//      - tag records (dnsapi\not) somehow (flags field)
//  that
//  dnsapi and dnslib heaps MUST be the same.
//  With dnsapi now potentially having it's own heap, this means
//  dnslib should use dnsapi heap.
//
//  So we'll put off using the debug heap for dnslib.
//

//
//  Resolver allocators
//

PVOID
Res_Alloc(
    IN      DWORD           Length,
    IN      DWORD           Tag,
    IN      PSTR            pszFile,
    IN      DWORD           LineNo
    );

PVOID
Res_AllocZero(
    IN      DWORD           Length,
    IN      DWORD           Tag,
    IN      PSTR            pszFile,
    IN      DWORD           LineNo
    );

VOID
Res_Free(
    IN OUT  PVOID           pMemory,
    IN      DWORD           Tag
    );

#define RESHEAP_TAG_GENERAL     0
#define RESHEAP_TAG_RECORD      1
#define RESHEAP_TAG_RPC         2
#define RESHEAP_TAG_MCAST       3


//
//  General memory
//

#define GENERAL_HEAP_ALLOC(Size)    \
        Res_Alloc(                  \
            Size,                   \
            RESHEAP_TAG_GENERAL,    \
            __FILE__,               \
            __LINE__ )

#define GENERAL_HEAP_ALLOC_ZERO(Size)   \
        Res_AllocZero(              \
            Size,                   \
            RESHEAP_TAG_GENERAL,    \
            __FILE__,               \
            __LINE__ )

#define GENERAL_HEAP_FREE(pMem)     \
        Res_Free(                   \
            pMem,                   \
            RESHEAP_TAG_GENERAL )


//
//  RPC allocs
//

#define RPC_HEAP_ALLOC(Size)        \
        Res_Alloc(                  \
            Size,                   \
            RESHEAP_TAG_RPC,        \
            __FILE__,               \
            __LINE__ )

#define RPC_HEAP_ALLOC_ZERO(Size)   \
        Res_AllocZero(              \
            Size,                   \
            RESHEAP_TAG_RPC,        \
            __FILE__,               \
            __LINE__ )

#define RPC_HEAP_FREE(pMem)         \
        Res_Free(                   \
            pMem,                   \
            RESHEAP_TAG_RPC )


//
//  Record heap routines
//

#define RECORD_HEAP_ALLOC(Size)     \
        Res_Alloc(                  \
            Size,                   \
            RESHEAP_TAG_RECORD,     \
            __FILE__,               \
            __LINE__ )

#define RECORD_HEAP_ALLOC_ZERO(Size)    \
        Res_AllocZero(              \
            Size,                   \
            RESHEAP_TAG_RECORD,     \
            __FILE__,               \
            __LINE__ )

#define RECORD_HEAP_FREE(pMem)      \
        Res_Free(                   \
            pMem,                   \
            RESHEAP_TAG_RECORD )

//
//  Mcast heap routines
//

#define MCAST_HEAP_ALLOC(Size)      \
        Res_Alloc(                  \
            Size,                   \
            RESHEAP_TAG_MCAST,      \
            __FILE__,               \
            __LINE__ )

#define MCAST_HEAP_ALLOC_ZERO(Size)    \
        Res_AllocZero(              \
            Size,                   \
            RESHEAP_TAG_MCAST,      \
            __FILE__,               \
            __LINE__ )

#define MCAST_HEAP_FREE(pMem)       \
        Res_Free(                   \
            pMem,                   \
            RESHEAP_TAG_MCAST )


//
//  Cache routines (ncache.c)
//

DNS_STATUS
Cache_Lock(
    IN      BOOL            fNoStart
    );

VOID
Cache_Unlock(
    VOID
    );

DNS_STATUS
Cache_Initialize(
    VOID
    );

DNS_STATUS
Cache_Shutdown(
    VOID
    );

DNS_STATUS
Cache_Flush(
    VOID
    );

VOID
Cache_FlushRecords(
    IN      PWSTR           pName,
    IN      DWORD           FlushLevel,
    IN      WORD            Type
    );

BOOL
Cache_IsRecordTtlValid(
    IN      PDNS_RECORD     pRecord
    );

//
//  Cache operations routines (ncache.c)
//

BOOL
Cache_ReadResults(
    OUT     PDNS_RESULTS    pResults,
    IN      PWSTR           pwsName,
    IN      WORD            wType
    );

VOID
Cache_PrepareRecordList(
    IN OUT  PDNS_RECORD     pRecordList
    );

VOID
Cache_RestoreRecordListForRpc(
    IN OUT  PDNS_RECORD     pRecordList
    );

VOID
Cache_RecordList(
    IN OUT  PDNS_RECORD     pRecordList
    );

VOID
Cache_RecordSetAtomic(
    IN      PWSTR           pwsName,
    IN      WORD            wType,
    IN      PDNS_RECORD     pRecordSet
    );

VOID
Cache_GarbageCollect(
    IN      DWORD           Flag
    );

DNS_STATUS
Cache_QueryResponse(
    IN OUT  PQUERY_BLOB     pBlob
    );

BOOL
Cache_GetRecordsForRpc(
    OUT     PDNS_RECORD *   ppRecordList,
    OUT     PDNS_STATUS     pStatus,
    IN      PWSTR           pwsName,
    IN      WORD            wType,
    IN      DWORD           Flags
    );

VOID
Cache_DeleteMatchingRecords(
    IN      PDNS_RECORD     pRecords
    );

//
//  Host file routines (notify.c)
//

VOID
InitCacheWithHostFile(
    VOID
    );


//
//  Notification (notify.c)
//

VOID
ThreadShutdownWait(
    IN      HANDLE          hThread
    );

HANDLE
CreateHostsFileChangeHandle(
    VOID
    );

VOID
NotifyThread(
    VOID
    );

VOID
StartNotify(
    VOID
    );

VOID
ShutdownNotify(
    VOID
    );


//
//  Config -- Network info (config.c)
//

VOID
UpdateNetworkInfo(
    IN OUT  PDNS_NETINFO    pNetworkInfo
    );

PDNS_NETINFO         
GrabNetworkInfo(
    VOID
    );

VOID
ZeroNetworkConfigGlobals(
    VOID
    );

VOID
CleanupNetworkInfo(
    VOID
    );

VOID
ReadRegistryConfig(
    VOID
    );

VOID
HandleConfigChange(
    IN      PSTR            pszReason,
    IN      BOOL            fCache_Flush
    );

#if 0
//
//  Currently ignoring all bogus net failure stuff
//
BOOL
IsKnownNetFailure(
    VOID
    );

VOID
SetKnownNetFailure(
    IN      DNS_STATUS      Status
    );

#endif

#define IsKnownNetFailure()     (FALSE)


//
//  Net config (still remote.c)
//

#define THREE_MINUTES_FROM_SYSTEM_BOOT  180
#define MAX_DNS_NOTIFICATION_LIST_SIZE  1000
#define PNP_REFRESH_UPDATE_WINDOW       60

BOOL
IsTimeToResetServerPriorities(
    VOID
    );


//
//  Service notification (notesrv.c)
//

VOID
SendServiceNotifications(
    VOID
    );

VOID
CleanupServiceNotification(
    VOID
    );

//
//  In memory logging (memlog.c)
//

VOID
LogEventInMemory(
    IN      DWORD           Checkpoint,
    IN      DWORD           Data
    );

//
//  Event logging (dnsrslvr.c)
//

VOID
ResolverLogEvent (
    IN      DWORD           MessageId,
    IN      WORD            EventType,
    IN      DWORD           StringCount,
    IN      PWSTR *         StringArray,
    IN      DWORD           ErrorCode
    );

//
//  IP list and notification (ip.c)
//

DNS_STATUS
IpNotifyThread(
    IN      LPVOID  pvDummy
    );

VOID
ZeroInitIpListGlobals(
    VOID
    );

DNS_STATUS
InitIpListAndNotification(
    VOID
    );

VOID
ShutdownIpListAndNotify(
    VOID
    );


//
//  Resolver log (logit.c)
//
//  Special type routines for resolver logging.
//  General log open\print routines defined in logit.h.
//

VOID
PrintNetworkInfoToLog(
    IN      PDNS_NETINFO    pNetworkInfo
    );


//
//  RPC server and access checking (rpc.c)
//

DNS_STATUS
Rpc_Initialize(
    VOID
    );

VOID
Rpc_Shutdown(
    VOID
    );

BOOL
Rpc_AccessCheck(
    IN      DWORD           DesiredAccess
    );


//
//  Multicast (mcast.c)
//

DNS_STATUS
Mcast_Startup(
    VOID
    );

VOID
Mcast_SignalShutdown(
    VOID
    );

VOID
Mcast_ShutdownWait(
    VOID
    );

#endif // _LOCAL_INCLUDED_


