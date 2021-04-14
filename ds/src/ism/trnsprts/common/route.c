/*++

Copyright (c) 1997  Microsoft Corporation

Module Name:

    route.c

Abstract:

This module contains the routines to implement the "routing api", namely
    GetTransportServers
    GetSiteConnectivity
    GetSchedule

These routines are independent from the data transfer functions.

These routines are based on configuration data of servers, sites and links.  This may come
from the local system registry (testing) or from ldap.

Author:

    Will Lees (wlees) 15-Dec-1997

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

#include <ntdspch.h>
#include <ntdsa.h>    // INTERSITETRANS_OPT_* flags

#include <dsconfig.h>  // GetConfigParam()
#include <ismapi.h>
#include <debug.h>

#include <winsock.h>

#include <common.h>

#include <dsutil.h> // TickTime routines

#define DEBSUB "ROUTE:"

// Logging headers.
// TODO: better place to put these?
typedef ULONG MessageId;
typedef ULONG ATTRTYP;
#include "dsevent.h"                    /* header Audit\Alert logging */
#include "mdcodes.h"                    /* header for error codes */

#include <fileno.h>
#define  FILENO FILENO_ISMSERV_ROUTE

// Use the generate table template to create a type specific table!
// Site Hash Table

#define SITE_HASH_TABLE_SIZE 5003  // should be prime

#define DWORD_INFINITY          (~ (DWORD) 0)

typedef struct _SITE_INSTANCE {
    TABLE_ENTRY TableEntry;  // must be first
    DWORD Size;
    DWORD Index;
} SITE_INSTANCE, *PSITE_INSTANCE;

typedef PTABLE_INSTANCE PSITE_TABLE;

#define SiteTableCreate() TableCreate( SITE_HASH_TABLE_SIZE, sizeof( SITE_INSTANCE ) )
#define SiteTableFree( table ) TableFree( (PTABLE_INSTANCE) table )
#define SiteTableFindCreate( table, name, create ) \
(PSITE_INSTANCE) TableFindCreateEntry( (PTABLE_INSTANCE) table, name, create )

/* External */

/* Static */

/* Forward */ /* Generated by Emacs 19.34.1 on Tue Oct 27 11:07:11 1998 */

VOID
RouteInvalidateConnectivity(
    PTRANSPORT_INSTANCE pTransport
    );

DWORD
RouteGetConnInternal(
    PTRANSPORT_INSTANCE pTransport,
    LPDWORD pNumberSites,
    PWSTR **ppSiteList,
    PISM_LINK *ppLinkArray,
    DWORD dwRouteFlags,
    DWORD dwReplInterval
    );

DWORD
RouteGetConnectivity(
    PTRANSPORT_INSTANCE pTransport,
    LPDWORD pNumberSites,
    PWSTR **ppSiteList,
    PISM_LINK *ppLinkArray,
    DWORD dwRouteFlags,
    DWORD dwReplInterval
    );

VOID
RouteFreeLinkArray(
    PTRANSPORT_INSTANCE pTransport,
    PISM_LINK pLinkArray
    );

DWORD
RouteGetPathSchedInternal(
    PTRANSPORT_INSTANCE pTransport,
    LPCWSTR FromSiteName,
    LPCWSTR ToSiteName,
    PBYTE *pSchedule,
    DWORD *pLength
    );

DWORD
RouteGetPathSchedule(
    PTRANSPORT_INSTANCE pTransport,
    LPCWSTR FromSiteName,
    LPCWSTR ToSiteName,
    PBYTE *pSchedule,
    DWORD *pLength
    );

void
RouteFreeState(
    PTRANSPORT_INSTANCE pTransport
    );

static DWORD
processSiteLinkBridges(
    PTRANSPORT_INSTANCE pTransport,
    DWORD dwRouteFlags,
    DWORD dwReplInterval,
    PSITE_TABLE SiteTable,
    DWORD NumberSites,
    PISMGRAPH CostArray
    );

static DWORD
readSimpleBridge(
    PTRANSPORT_INSTANCE pTransport,
    DWORD dwRouteFlags,
    DWORD dwReplInterval,
    PWSTR BridgeName,
    PSITE_TABLE SiteTable,
    PISMGRAPH TempArray,
    PISMGRAPH CostArray
    );

static DWORD
walkSiteLinks(
    PTRANSPORT_INSTANCE pTransport,
    PSITE_TABLE SiteTable,
    PISMGRAPH CostArray,
    DWORD dwRouteFlags,
    DWORD dwReplInterval
    );

static DWORD
addSiteLink(
    PTRANSPORT_INSTANCE pTransport,
    PSITE_TABLE SiteTable,
    PISMGRAPH CostArray,
    LPWSTR SiteLinkName,
    DWORD dwRouteFlags,
    DWORD dwReplInterval
    );

static DWORD
addLinkToCostArray(
    PSITE_TABLE SiteTable,
    PISMGRAPH CostArray,
    PWSTR FromSite,
    PWSTR ToSite,
    PISM_LINK pLinkValue,
    PBYTE pSchedule
    );

/* End Forward */


VOID
RouteInvalidateConnectivity(
    PTRANSPORT_INSTANCE pTransport
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
    EnterCriticalSection( &(pTransport->Lock) );
    __try {
        // Invalidate the cache, recalculate later
        DPRINT1( 1, "Invalidating routing cache for %ws\n",
                 pTransport->Name );
        pTransport->RoutingState.fCacheIsValid = FALSE;
    } finally {
        LeaveCriticalSection( &(pTransport->Lock) );
    }
}


DWORD
RouteGetConnInternal(
    PTRANSPORT_INSTANCE pTransport,
    LPDWORD pNumberSites,
    PWSTR **ppSiteList,
    PISM_LINK *ppLinkArray,
    DWORD dwRouteFlags,
    DWORD dwReplInterval
    )

/*++

Routine Description:

Return the list of sites, a site name array, and a connectivity matrix.

A hash table is used to store the site names found, for ease of lookup.

Once all the site names are found, they are assigned an order (index).  This is the order
used to generate the site name list at the end.

The simple link structures are read to determine which sites are connected.

Once the number of sites is known, the cost array is allocated.  The initial single hop costs
are then placed in the array.  Then the ALL COSTS algorithm is run on the array to produce the
shorted paths for all pairs.

Each time this routine is called, it regenerates three pieces of information:
- the list of sites
- the matrix of costs
- the matrix of schedules
The first two are returned and freed.

The transport has associated with it some lingering state.  The matrix of schedules is not
freed at the end of this routine.  It remains, tied to the transport handle, for the benefit
of the GetConnectionSchedule api.

Arguments:

    pTransport - 
    pNumberSites - May be NULL if output is not desired.
    ppSiteList - May be NULL if output is not desired.
    ppLinkArray - May be NULL if output is not desired.

    dwRouteFlags - Zero or more of the following bits:
        ROUTE_IGNORE_SCHEDULES - Schedules on siteLink objects will be ignored.
            (And the "ever-present" schedule is assumed.)
        ROUTE_BRIDGES_REQUIRED - siteLinks must be explicitly bridged with
            siteLinkBridge objects to indicate transitive connections.
            Otherwise, siteLink transitivity is assumed.

    dwReplInterval - default replication interval

Return Value:

    DWORD - 

--*/

{
    DWORD status, i;
    PISMGRAPH CostGraph = NULL;
    PSITE_TABLE SiteTable = NULL;
    PSITE_INSTANCE site;
    DWORD NumberSites;
    LPWSTR *pSiteList = NULL, *pSiteListCopy = NULL;
    PISM_LINK pLinkArray = NULL;
    int nPriority = THREAD_PRIORITY_NORMAL;
    DWORD dwBiasedPriority;

    // Parameter validation: NULL output parameters are okay here if the
    // caller does not want any output. (i.e. RouteQuerySitesByCost)
    if(    (NULL==pNumberSites)
        || (NULL==ppSiteList)
        || (NULL==ppLinkArray) )
    {
        // All output parameters must be NULL
        Assert( NULL==pNumberSites && NULL==ppSiteList && NULL==ppLinkArray );
    }

    // Step 0: Check if cached data still valid
    // Use tick counts in case time gets changed or set backwards
    // Tick counts are in 1ms intervals, wrap every 47 days of uptime
    // Note the degraded performance guarantee. If the notify thead dies for some
    // reason, we will not consider the cache valid and will recalculate.

    if ( (DirIsNotifyThreadActive( pTransport ) ) &&
         (pTransport->RoutingState.CostGraph) &&
         (pTransport->RoutingState.fCacheIsValid) ) {

        NumberSites = pTransport->RoutingState.NumberSites;
        CostGraph = pTransport->RoutingState.CostGraph;
        pSiteList = pTransport->RoutingState.pSiteList;

        goto copy_out;
    }

    // Step 1: Initialize

    // Free previous graph state 
    RouteFreeState( pTransport );

    // initialize site list
    NumberSites = 0;
    status = DirGetSiteList( pTransport->DirectoryConnection,
                             &NumberSites,
                             &pSiteList );
    if (status != ERROR_SUCCESS) {
        // nothing to clean up yet
        return status;
    }

    // There should always be atleast one site
    if (NumberSites == 0) {
        // nothing to clean up yet
        return ERROR_DS_OBJ_NOT_FOUND;
    }

    // Initialize symbol table
    SiteTable = SiteTableCreate();
    if (NULL == SiteTable) {
        status = ERROR_NOT_ENOUGH_MEMORY;
        goto cleanup;
    }

    // Change priority if necessary
    if (!GetConfigParam( ISM_THREAD_PRIORITY, &dwBiasedPriority, sizeof( DWORD ) )) {
        if (dwBiasedPriority > ISM_MAX_THREAD_PRIORITY) {
            dwBiasedPriority = ISM_MAX_THREAD_PRIORITY;
        }
        nPriority = dwBiasedPriority - ISM_THREAD_PRIORITY_BIAS;
        if( ! SetThreadPriority(GetCurrentThread(),nPriority) ) {
            status = GetLastError();
            DPRINT1(0, "Failed to set the thread priority. Err=%d\n", status);
            LogEvent(
                DS_EVENT_CAT_ISM,
                DS_EVENT_SEV_MINIMAL,
                DIRLOG_KCC_SET_PRIORITY_ERROR,
                szInsertWin32Msg(status),
                NULL,
                NULL
                );
        } else {
            DPRINT1(1, "ISM thread priority is now %d\n", nPriority );
        }
    }

    // Step 2: Insert each site into the hash table for ease of lookup
    for( i = 0; i < NumberSites; i++ ) {
        site = SiteTableFindCreate( SiteTable, pSiteList[i], TRUE /* create */ );
        if (site == NULL) {
            status = ERROR_NOT_ENOUGH_MEMORY;
            goto cleanup;
        }
        // Assign index to site according to site list order
        site->Index = i;
    }

    // Step 3: allocate cost matrix
    // SCALING BUG 87827:
    // 1000 SITES = 1000 * 1000 * 12 BYTES = approx 12 MB
    CostGraph = GraphCreate( NumberSites, TRUE /* initialize */ );
    if (CostGraph == NULL) {
        status = ERROR_NOT_ENOUGH_MEMORY;  // bug fix 151725
        goto cleanup;
    }

    // Step 4: add pure links without bridging

    status = walkSiteLinks( pTransport,
                            SiteTable,
                            CostGraph,
                            dwRouteFlags,
                            dwReplInterval );
    if (status != ERROR_SUCCESS) {
        goto cleanup;
    }

    if (dwRouteFlags & ROUTE_BRIDGES_REQUIRED) {
        // Step 5: walk site link bridges for explicit transitivity

        status = processSiteLinkBridges( pTransport,
                                         dwRouteFlags,
                                         dwReplInterval,
                                         SiteTable,
                                         NumberSites,
                                         CostGraph );
        if (status != ERROR_SUCCESS) {
            goto cleanup;
        }
    }
    else {
        // Step 5: Transitivity is assumed, so compute the full transitive
        // closure.
        status = GraphAllCosts( CostGraph, (dwRouteFlags & ROUTE_IGNORE_SCHEDULES) );
        if (status != ERROR_SUCCESS) {
            DPRINT1( 0, "GraphAllCosts failed, error %d\n", status );
            goto cleanup;
        }
    }

    Assert( pTransport->RoutingState.NumberSites == 0 );
    Assert( pTransport->RoutingState.pSiteList == NULL );
    Assert( pTransport->RoutingState.CostGraph == NULL );
    Assert( pTransport->RoutingState.SiteSymbolTable == NULL );

    // Cache data: SiteList, CostGraph and SiteTable remain allocated
    pTransport->RoutingState.fCacheIsValid = TRUE;
    pTransport->RoutingState.NumberSites = NumberSites;
    pTransport->RoutingState.pSiteList = pSiteList;
    pTransport->RoutingState.CostGraph = CostGraph;
    pTransport->RoutingState.SiteSymbolTable = SiteTable;

copy_out:
    // Step 6: Copy out user arguments
    // The resource release code here is a little tricky. If we are successful, all the
    // data blocks will have been handed out, and don't need any individual cleanup.
    // If we fail in here, then we want to clear the cache and allow individual cleanup
    // of the data blocks.

    Assert( NumberSites != 0 );
    Assert( pSiteList != NULL );
    Assert( CostGraph != NULL );

    Assert( pTransport->RoutingState.NumberSites != 0 );
    Assert( pTransport->RoutingState.pSiteList != NULL );
    Assert( pTransport->RoutingState.CostGraph != NULL );
    Assert( pTransport->RoutingState.SiteSymbolTable != NULL );
    Assert( pSiteListCopy == NULL );
    Assert( pLinkArray == NULL );

    // Assume success
    status = ERROR_SUCCESS;

    // If the caller does not want any output, don't create any...
    if( NULL==pNumberSites || NULL==ppSiteList || NULL==ppLinkArray ) {
        // ... but don't clean up the transport's internal state either
        pSiteList = NULL;
        CostGraph = NULL;
        SiteTable = NULL;
        goto cleanup;
    }

    DirCopySiteList( NumberSites, pSiteList, &pSiteListCopy );
    if (pSiteListCopy == NULL) {
        status = ERROR_NOT_ENOUGH_MEMORY;
        // We are in the strange position of having enough memory to build and
        // cache a routing state, but not enough to copy it out.
        // keep going
    }

    if (status == ERROR_SUCCESS) {
        // This call copies the array portion out of the graph.
        GraphReferenceMatrix( CostGraph, &pLinkArray );
        if (pLinkArray == NULL) {
            status = ERROR_NOT_ENOUGH_MEMORY;
            // keep going
        }
    }

    if (status == ERROR_SUCCESS) {
        *pNumberSites = NumberSites;
        *ppSiteList = pSiteListCopy;
        *ppLinkArray = pLinkArray;

        pSiteListCopy = NULL;  // don't clean this up
        pLinkArray = NULL;  // don't clean this up

    } else {
        RouteFreeState( pTransport );
        // pSiteList, CostGraph and SiteSymbolTable cleaned up now
        // pSiteListCopy and pLinkArray will be cleaned up below

        Assert( pTransport->RoutingState.NumberSites == 0 );
        Assert( pTransport->RoutingState.pSiteList == NULL );
        Assert( pTransport->RoutingState.CostGraph == NULL );
        Assert( pTransport->RoutingState.SiteSymbolTable == NULL );
    }

    pSiteList = NULL; // don't clean this up
    CostGraph = NULL; // don't clean this up
    SiteTable = NULL; // don't clean this up

cleanup:               

    if (nPriority != THREAD_PRIORITY_NORMAL) {
        if( ! SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_NORMAL) ) {
            status = GetLastError();
            DPRINT1(0, "Failed to set the thread priority. Err=%d\n", status);
        }
    }

    // Clean up site list
    if (pSiteList) {
        DirFreeSiteList( NumberSites, pSiteList );
    }
    if (pSiteListCopy) {
        DirFreeSiteList( NumberSites, pSiteListCopy );
    }

    // free site table
    if (SiteTable != NULL) {
        SiteTableFree( SiteTable );
    }

    // free matrix
    if (pLinkArray != NULL) {
        GraphDereferenceMatrix( CostGraph, pLinkArray );
    }

    // free cost array
    if (CostGraph != NULL) {
        GraphFree( CostGraph );
    }

    return status;
} /* RouteGetConnectivity */


DWORD
RouteGetConnectivity(
    PTRANSPORT_INSTANCE pTransport,
    LPDWORD pNumberSites,
    PWSTR **ppSiteList,
    PISM_LINK *ppLinkArray,
    DWORD dwRouteFlags,
    DWORD dwReplInterval
    )

/*++

Routine Description:

Wrapper for RouteGetConnInternal.
Take a lock around the connectivity function so that no other routine may
be reading or writing the routing state variables.

Arguments:

    pTransport - 
    pNumberSites - 
    ppSiteList - 
    ppLinkArray - 
    dwRouteFlags - 
    dwReplInterval - 

Return Value:

    DWORD - 

--*/

{
    DWORD status;

    EnterCriticalSection( &(pTransport->Lock) );
    __try {

        status = RouteGetConnInternal(
            pTransport,
            pNumberSites,
            ppSiteList,
            ppLinkArray,
            dwRouteFlags,
            dwReplInterval
            );
    } finally {
        LeaveCriticalSection( &(pTransport->Lock) );
    }
    return status;
} /* RouteGetConnectivity */


VOID
RouteFreeLinkArray(
    PTRANSPORT_INSTANCE pTransport,
    PISM_LINK pLinkArray
    )

/*++

Routine Description:

    Description

Arguments:

    pTransport - 
    pLinkArray - 

Return Value:

    None

--*/

{
    GraphDereferenceMatrix( pTransport->RoutingState.CostGraph,
                            pLinkArray );
} /* RouteFreeLinkArray */


DWORD
RouteGetPathSchedInternal(
    PTRANSPORT_INSTANCE pTransport,
    LPCWSTR FromSiteName,
    LPCWSTR ToSiteName,
    PBYTE *pSchedule,
    DWORD *pLength
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
    DWORD status;
    PSITE_INSTANCE toSite, fromSite;

    // Note that there are no cache lifetime requirements on the schedule
    // data.
    // TODO: investigate returning schedules with connectivity data

    // Must have called GetConnectivity first
    if (pTransport->RoutingState.CostGraph == NULL) {
        DPRINT( 0, "Must call GetConnectivity first on this transport\n" );
        return ERROR_NOT_READY;
    }

    fromSite = SiteTableFindCreate( pTransport->RoutingState.SiteSymbolTable,
                                    FromSiteName,
                                    FALSE /* don't create */ );
    if (fromSite == NULL) {
        DPRINT1( 0, "GetPathSched: %ws, (from) site not found\n", FromSiteName );
        return ERROR_NO_SUCH_SITE;
    }

    toSite = SiteTableFindCreate( pTransport->RoutingState.SiteSymbolTable,
                                  ToSiteName,
                                  FALSE /* don't create */ );
    if (toSite == NULL) {
        DPRINT1( 0, "GetPathSched: %ws, (to) site not found\n", ToSiteName );
        return ERROR_NO_SUCH_SITE;
    }

    status = GraphGetPathSchedule( pTransport->RoutingState.CostGraph,
                                   fromSite->Index,
                                   toSite->Index,
                                   pSchedule,
                                   pLength );

    return status;
}


DWORD
RouteGetPathSchedule(
    PTRANSPORT_INSTANCE pTransport,
    LPCWSTR FromSiteName,
    LPCWSTR ToSiteName,
    PBYTE *pSchedule,
    DWORD *pLength
    )

/*++

Routine Description:

Wrapper for RouteGetPathSchedInternal.
Take the transport lock so only one thread may be accessing the routing
state at a given time.

Arguments:

    pTransport - 
    FromSiteName - 
    ToSiteName - 
    pSchedule - 
    pLength - 

Return Value:

    DWORD - 

--*/

{
    DWORD status;

    EnterCriticalSection( &(pTransport->Lock) );
    __try {
        status = RouteGetPathSchedInternal(
            pTransport,
            FromSiteName,
            ToSiteName,
            pSchedule,
            pLength
            );
    } finally {
        LeaveCriticalSection( &(pTransport->Lock) );
    }
    return status;
} /* RouteGetPathSchedule */


void
RouteFreeState(
    PTRANSPORT_INSTANCE pTransport
    )

/*++

Routine Description:

    Description

Arguments:

    pTransport - 

Return Value:

    None

--*/

{
    PROUTING_STATE prs = &(pTransport->RoutingState);
    
    // This routine assumes that the caller will hold the transport
    // lock, expect during rundown

    if (prs->pSiteList) {
        DirFreeSiteList( prs->NumberSites, prs->pSiteList );
    }
    if (prs->CostGraph) {
        GraphFree( prs->CostGraph );
    }
    if (prs->SiteSymbolTable) {
        SiteTableFree( prs->SiteSymbolTable );
    }

    ZeroMemory( prs, sizeof( ROUTING_STATE ) );
} /* RouteFreeState */


DWORD
RouteQuerySitesByCost(
    PTRANSPORT_INSTANCE         pTransport,
    LPCWSTR                     pszFromSite,
    DWORD                       cToSites,
    LPCWSTR*                    rgszToSites,
    DWORD                       dwFlags,
    ISM_SITE_COST_INFO_ARRAY**  prgSiteInfo
    )
/*++

Routine Description:

    Determine the individual costs between the From site and the To sites.

Arguments:

    pTransport (IN) - Pointer to the structure containing all information
                      for this transport.

    pszFromSite (IN) - The distinguished name of the From site.

    rgszToSites (IN) - An array containing the distinguished names of the To sites.

    cToSites (IN) - The number of entries in the rgszToSites array.

    dwFlags (IN) - Unused.

    prgSiteInfo (IN) - On successful return, holds a pointer to a structure
        containing the costs between the From site and the To sites.
        This array should be freed using RouteFreeSiteCostInfo().
    
Return Values:

    NO_ERROR - Success.
    ERROR_* - Failure.

--*/
{
    PISM_SITE_COST_INFO rgCostInfo=NULL;
    PWSTR               *pSiteList=NULL;
    PISM_LINK           pLinkArray=NULL;
    PSITE_TABLE         SiteTable=NULL;
    PSITE_INSTANCE      site;
    DWORD               NumberSites=0, i;
    DWORD               iFromSite, iToSite;
    DWORD               status;

    // Validate parameters
    Assert( NULL!=pTransport );
    Assert( NULL!=pszFromSite );
    Assert( NULL!=rgszToSites );
    Assert( NULL!=prgSiteInfo );

    // Clear results
    *prgSiteInfo = NULL;

    EnterCriticalSection( &(pTransport->Lock) );

    __try {

        // Call RouteGetConnInternal to check the cached matrix and generate a
        // new one if necessary. It does not return any results to us.
        status = RouteGetConnInternal( pTransport, NULL, NULL, NULL,
            pTransport->Options, pTransport->ReplInterval );
        
        // The results of the RouteGetConnInternal call are obtained by
        // directly peeking in the transport object.
        NumberSites = pTransport->RoutingState.NumberSites;
        SiteTable = pTransport->RoutingState.SiteSymbolTable;
        GraphPeekMatrix( pTransport->RoutingState.CostGraph, &pLinkArray );
        
        Assert( NULL!=pLinkArray );
        Assert( NULL!=SiteTable );
        
        // Find the From site in the hash table
        site = SiteTableFindCreate( SiteTable, pszFromSite, FALSE /* don't create */ );
        if( site==NULL ) {
            status = ERROR_DS_OBJ_NOT_FOUND;
            __leave;
        }
        iFromSite = site->Index;

        // Allocate the structure which contains the results
        *prgSiteInfo = NEW_TYPE_ZERO( ISM_SITE_COST_INFO_ARRAY );
        if( NULL==*prgSiteInfo ) {
            status = ERROR_NOT_ENOUGH_MEMORY;
            __leave;
        }
        (*prgSiteInfo)->cToSites = cToSites;

        // Allocate the array containing the results
        rgCostInfo = NEW_TYPE_ARRAY_ZERO( cToSites, ISM_SITE_COST_INFO );
        if( NULL==rgCostInfo ) {
            status = ERROR_NOT_ENOUGH_MEMORY;
            __leave;
        }
        (*prgSiteInfo)->rgCostInfo = rgCostInfo;

        // Copy the costs into the results
        for( i=0; i<cToSites; i++ ) {

            // Find the To site in the hash table
            site = SiteTableFindCreate( SiteTable, rgszToSites[i], FALSE /* don't create */ );
            if( site==NULL ) {
                rgCostInfo[i].dwErrorCode = ERROR_DS_OBJ_NOT_FOUND;
                rgCostInfo[i].dwCost = DWORD_INFINITY;
                continue;   // Skip to next To site
            }

            iToSite = site->Index;
            rgCostInfo[i].dwErrorCode = ERROR_SUCCESS;
            rgCostInfo[i].dwCost = pLinkArray[ iFromSite*NumberSites + iToSite ].ulCost;

        }

        status = ERROR_SUCCESS;

    } finally {

        // If the call failed for some reason, free the results structure
        if( ERROR_SUCCESS!=status ) {
            if( NULL!=rgCostInfo ) {
                FREE_TYPE( rgCostInfo );
                rgCostInfo = NULL;
            }
            if( NULL!=*prgSiteInfo ) {
                FREE_TYPE( *prgSiteInfo );
                *prgSiteInfo = NULL;
            }
        }

        LeaveCriticalSection( &(pTransport->Lock) );
    }

    return status;
}


VOID
RouteFreeSiteCostInfo(
    IN ISM_SITE_COST_INFO_ARRAY*   prgSiteInfo
    )
/*++

Routine Description:

    Frees the structure returned by RouteQuerySitesByCost().

Arguments:

    prgSiteInfo (IN) - Structure to free.

Return Values:

    None.

--*/
{
    if( NULL!=prgSiteInfo ) {
        if( NULL!=prgSiteInfo->rgCostInfo ) {
            FREE_TYPE( prgSiteInfo->rgCostInfo );
        }
        FREE_TYPE( prgSiteInfo );
    }
}


static DWORD
processSiteLinkBridges(
    PTRANSPORT_INSTANCE pTransport,
    DWORD dwRouteFlags,
    DWORD dwReplInterval,
    PSITE_TABLE SiteTable,
    DWORD NumberSites,
    PISMGRAPH CostArray
    )

/*++

Routine Description:

This routine enumerates the simple link structures in the registry.  It calls an action
routine for each.

The temp array is used to calculate an intermediate matrix for each bridged network
They are merged into the final cost array

Arguments:

    pTransport - 
    dwRouteFlags - route behavior options
    dwReplInterval - default replication interval
    NumberSites - 
    SiteTable - 
    CostArray - 

Return Value:

    DWORD - 

--*/

{
    DWORD status, index, length;
    WCHAR bridgeName[MAX_REG_COMPONENT];
    PISMGRAPH TempArray = NULL;
    PVOID context = NULL;

    Assert( dwRouteFlags & ROUTE_BRIDGES_REQUIRED );

    // Allocate a temp array now for intermediate results.  It is init'd later
    TempArray = GraphCreate( NumberSites, FALSE /* initialize */ );
    if (TempArray == NULL) {
        DPRINT1( 0, "failed to allocate temp matrix for %d sites\n", NumberSites );
        status = ERROR_NOT_ENOUGH_MEMORY;
        goto cleanup;
    }

    while (1) {
        status = DirIterateSiteLinkBridges( pTransport,
                                            pTransport->DirectoryConnection,
                                            &context,
                                            bridgeName );
        if (status == ERROR_NO_MORE_ITEMS) {
            // context is already cleaned up
            status = ERROR_SUCCESS;
            break;
        } else if (status != ERROR_SUCCESS) {
            break;
        }

        status = readSimpleBridge( pTransport,
                                   dwRouteFlags,
                                   dwReplInterval,
                                   bridgeName,
                                   SiteTable,
                                   TempArray,
                                   CostArray
                                   );
        if (status != ERROR_SUCCESS) {
            DPRINT2( 0, "read of bridge key %ws, error = %d\n", bridgeName, status );
            LogEvent8( 
                DS_EVENT_CAT_ISM,
                DS_EVENT_SEV_ALWAYS,
                DIRLOG_ISM_OBJECT_FAILURE,
                szInsertWC( bridgeName ),
                szInsertWin32Msg( status ),
                szInsertWin32ErrCode( status ),
                NULL, NULL, NULL, NULL, NULL
                );
            // keep going
        }
    }

    // status is set according to success or failure of iteration

cleanup:

    if (context != NULL) {
        DirTerminateIteration( &context );
    }

    if (TempArray != NULL) {
        GraphFree( TempArray );
    }

    return status;
} /* processSiteLinkBridges */


static DWORD
readSimpleBridge(
    PTRANSPORT_INSTANCE pTransport,
    DWORD dwRouteFlags,
    DWORD dwReplInterval,
    PWSTR BridgeName,
    PSITE_TABLE SiteTable,
    PISMGRAPH TempArray,
    PISMGRAPH CostArray
    )

/*++

Routine Description:

This routine is called by processSiteLinkBridges.  It handles the work on a single bridge entry.

Arguments:

    pTransport - 
    dwRouteFlags - route behavior options
    dwReplInterval - default replication interval
    BridgeName - 
    NumberSites - 
    SiteTable - 
    TempArray - 
    CostArray - 

Return Value:

    DWORD - 

--*/

{
    DWORD status, type, length, i;
    PWSTR siteLinkList = NULL, linkName;

    DPRINT1( 3, "readSimpleBridge, bridge = %ws\n", BridgeName );

    status = GraphInit( TempArray );
    if( ERROR_SUCCESS!=status ) {
        return status;
    }

    status = DirReadSiteLinkBridge( pTransport,
                                    pTransport->DirectoryConnection,
                                    BridgeName,
                                    &siteLinkList );
    if (status != ERROR_SUCCESS) {
        return status;
    }

    // No siteLinks listed, all done
    if (siteLinkList == NULL) {
        status = ERROR_SUCCESS;
        goto cleanup;
    }

    // Walk SiteLink list, populating array

    for( linkName = siteLinkList; *linkName != L'\0'; linkName += wcslen( linkName ) + 1 ) {

        status = addSiteLink( pTransport,
                              SiteTable,
                              TempArray,
                              linkName,
                              dwRouteFlags,
                              dwReplInterval );
        if (status != ERROR_SUCCESS) {
            DPRINT1( 0, "Action routine failed, error %d\n", status );
            goto cleanup;
        }

    }

    // All sites in a site link that is in a bridge are transitive
    // Perform all pairs, shortest path

    status = GraphAllCosts( TempArray, (dwRouteFlags & ROUTE_IGNORE_SCHEDULES) );
    if (status != ERROR_SUCCESS) {
        DPRINT1( 0, "GraphAllCosts failed, error %d\n", status );
        goto cleanup;
    }

    // Merge results into master matrix
    status = GraphMerge( CostArray, TempArray );
    if (status != ERROR_SUCCESS) {
        DPRINT1( 0, "GraphMerge failed, error %d\n", status );
        goto cleanup;
    }

    status = ERROR_SUCCESS;

cleanup:

    if (siteLinkList != NULL) {
        DirFreeMultiszString( siteLinkList );
    }

    return status;
} /* readSimpleBridge */


static DWORD
walkSiteLinks(
    PTRANSPORT_INSTANCE pTransport,
    PSITE_TABLE SiteTable,
    PISMGRAPH CostArray,
    DWORD dwRouteFlags,
    DWORD dwReplInterval
    )

/*++

Routine Description:

This routine enumerates the simple link structures in the registry.  It calls an action
routine for each.

Arguments:

    pTransport - 
    SiteTable - 
    CostArray - 

    dwRouteFlags - If the ROUTE_IGNORE_SCHEDULES bit is set, schedules on
        siteLink objects will be ignored.  (And the "ever-present" schedule
        is assumed.)

    dwReplInterval - default replication interval

Return Value:

    DWORD - 

--*/

{
    DWORD status;
    PVOID context = NULL;
    WCHAR siteLinkName[MAX_REG_COMPONENT];

    while (1) {
        status = DirIterateSiteLinks( pTransport,
                                      pTransport->DirectoryConnection,
                                      &context,
                                      siteLinkName );
        if (status == ERROR_NO_MORE_ITEMS) {
            // context is already cleaned up
            status = ERROR_SUCCESS;
            break;
        } else if (status != ERROR_SUCCESS) {
            goto cleanup;
        }

        status = addSiteLink( pTransport,
                              SiteTable,
                              CostArray,
                              siteLinkName,
                              dwRouteFlags,
                              dwReplInterval );
        if (status != ERROR_SUCCESS) {
            LogEvent8( 
                DS_EVENT_CAT_ISM,
                DS_EVENT_SEV_ALWAYS,
                DIRLOG_ISM_OBJECT_FAILURE,
                szInsertWC( siteLinkName ),
                szInsertWin32Msg( status ),
                szInsertWin32ErrCode( status ),
                NULL, NULL, NULL, NULL, NULL
                );
            // keep going
        }
    }

    // status is set at this point according to success or failure

cleanup:

    if (context != NULL) {
        DirTerminateIteration( &context );
    }

    return status;

} /* walkSiteLinks */


static DWORD
addSiteLink(
    PTRANSPORT_INSTANCE pTransport,
    PSITE_TABLE SiteTable,
    PISMGRAPH CostArray,
    LPWSTR SiteLinkName,
    DWORD dwRouteFlags,
    DWORD dwReplInterval
    )

/*++

Routine Description:

    Description

Arguments:

    pTransport - 
    SiteTable - 
    CostArray - 
    SiteLinkName - 

    dwRouteFlags - If the ROUTE_IGNORE_SCHEDULES bit is set, schedules on
        siteLink objects will be ignored.  (And the "ever-present" schedule
        is assumed.)

    dwReplInterval - default replication interval

Return Value:

    DWORD - 

--*/

{
    DWORD status;
    LPWSTR siteList = NULL, inner, outer;
    PBYTE pSchedule = NULL;
    PBYTE *ppSchedule;
    ISM_LINK link;

    //DPRINT1( 3, "addSiteLink, link name = %ws\n", SiteLinkName );

    ppSchedule = (dwRouteFlags & ROUTE_IGNORE_SCHEDULES)
                    ? NULL
                    : &pSchedule;

    status = DirReadSiteLink( pTransport,
                              pTransport->DirectoryConnection,
                              SiteLinkName,
                              &siteList,
                              &link,
                              ppSchedule );
    if (status != ERROR_SUCCESS) {
        // nothing to clean up
        return status;
    }

    // No sites listed
    if (siteList == NULL) {
        status = ERROR_SUCCESS;
        goto cleanup;
    }

    // If interval not specified, use the transport default

    if (link.ulReplicationInterval == 0) {
        link.ulReplicationInterval = dwReplInterval;
    }

    // process site link

    // SiteList can be 2 or more sites
    // Generate pairs, eliminate duplicates (order is not significant):
    // (a,b,c) => (a,b), (a,c), (b,c)

    outer = siteList;
    while (*outer != L'\0') {
        DWORD outerLength = wcslen( outer) + 1;

        for( inner = outer + outerLength; *inner != L'\0'; inner += wcslen( inner ) + 1 ) {

            // Filter out cyles to self, just in case
            if (_wcsicmp( outer, inner ) == 0) {
                continue;
            }

            // The action routine handles unidirectional (directed) links.  We convert here from
            // the bidirectional (undirected) notation by calling the action routine twice, once
            // for each direction.
            
            // Add the link in the forward direction

            status = addLinkToCostArray( SiteTable, CostArray, 
                                         outer, inner, &link, pSchedule );
            if (status != ERROR_SUCCESS) {
                DPRINT1( 0, "addLinkToCostArray1 failed, error %d\n", status );
                goto cleanup;
            }

            // Add the link in the backward direction
            status = addLinkToCostArray( SiteTable, CostArray,
                                         inner, outer, &link, pSchedule );
            if (status != ERROR_SUCCESS) {
                DPRINT1( 0, "addLinkToCostArray2 failed, error %d\n", status );
                goto cleanup;
            }
        }

        outer += outerLength;
    }

    status = ERROR_SUCCESS;

cleanup:

    if (siteList) {
        DirFreeMultiszString( siteList );
    }

    if (pSchedule) {
        DirFreeSchedule( pSchedule );
    }

    return status;
} /* addSiteLink */


static DWORD
addLinkToCostArray(
    PSITE_TABLE SiteTable,
    PISMGRAPH CostArray,
    PWSTR FromSite,
    PWSTR ToSite,
    PISM_LINK pLinkValue,
    PBYTE pSchedule
    )

/*++

Routine Description:

This is an action routine for the WalkSiteLinks function.

This routine stores the hop costs in the cost array.

Arguments:

    SiteTable - 
    CostArray - 
    FromSite - 
    ToSite - 
    Cost - 
    pSchedule - 

Return Value:

    DWORD - 

--*/

{
    PSITE_INSTANCE site1, site2;
    LPDWORD element;
    DWORD status;

    DPRINT5( 3, "Adding simple link %ws --(%d,%d,%p)--> %ws\n",
             FromSite,
             pLinkValue->ulCost, pLinkValue->ulReplicationInterval,
             pSchedule, ToSite );

    site1 = SiteTableFindCreate( SiteTable,
                                 FromSite,
                                 FALSE /* don't create */ );
    if (site1 == NULL) {
        // Must exist
        DPRINT1( 0, "Site %ws is not valid\n", FromSite );
        return ERROR_NO_SUCH_SITE;
    }
    site2 = SiteTableFindCreate( SiteTable,
                                 ToSite,
                                 FALSE /* don't create */ );
    if (site2 == NULL) {
        // Must exist
        DPRINT1( 0, "Site %ws is not valid\n", ToSite );
        return ERROR_NO_SUCH_SITE;
    }

    // Put cost in table ONLY if it is better than previous cost
    status = GraphAddEdgeIfBetter( CostArray,
                                   site1->Index, site2->Index,
                                   pLinkValue, pSchedule );

    return status;
} /* addLinkToCostArray */

/* end route.c */
