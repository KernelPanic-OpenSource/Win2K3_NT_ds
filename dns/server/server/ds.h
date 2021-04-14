/*++

Copyright(c) 1995-1999 Microsoft Corporation

Module Name:

    ds.h

Abstract:

    Domain Name System (DNS) Server

    Definitions for symbols and globals related to DS.C 

Author:

    Jeff Westhead, September 1999

Revision History:

--*/


#ifndef _DS_H_INCLUDED
#define _DS_H_INCLUDED


#ifdef LDAP_UNICODE
#define  LDAP_TEXT(str)           L ## str
#else
#define  LDAP_TEXT(str)           str
#endif


//
//  Open specifying NULL for server name.
//  LDAP will connect this to local DS if available.
//
//  Note: can not use loopback address due to reduced security.
//

#define LOCAL_SERVER                NULL
#define LOCAL_SERVER_W              NULL


//
//  DN path is essentially:
//      - DN for DS root, which is essentially limited to 255 of DNS name, plus
//      overhead;  overhead may substaintial since labels individual broken out in DN
//      meaning each has "dc=" overhead (absolute worst case 127 labels x 4 bytes per)
//      - MicrosoftDns and System containers
//      - zone DNS path (255 limit + dc=)
//      - node DNS name (255 limit + dc=)
//

#define MAX_DN_PATH                 1280


//
// Default time limit on LDAP operations
//

#define DNS_LDAP_TIME_LIMIT_S       180        // seconds
#define DNS_LDAP_TIME_LIMIT_MS      180000     // milliseconds


extern PLDAP    pServerLdap;


//
// DS attribute definitions
//

extern LDAP_TIMEVAL    g_LdapTimeout;

//
//
//

typedef struct _DsAttrPair
{
    PWSTR           szAttrType;
    BOOL            fMultiValued;
    union
    {
        PWSTR       pszAttrVal;     //  single-valued
        PWSTR *     ppszAttrVals;   //  multi-valued (allocated array of allocated strings)
    };
}
DSATTRPAIR, *PDSATTRPAIR;

extern DSATTRPAIR DSEAttributes[];

#define I_DSE_CURRENTTIME    0
#define I_DSE_DSSERVICENAME  1
#define I_DSE_DEF_NC         2
#define I_DSE_SCHEMA_NC      3      //  must be before NamingContexts
#define I_DSE_CONFIG_NC      4      //  must be before NamingContexts
#define I_DSE_ROOTDMN_NC     5
#define I_DSE_HIGHEST_USN    6
#define I_DSE_DNSHOSTNAME    7
#define I_DSE_SERVERNAME     8
#define I_DSE_NAMINGCONTEXTS 9      //  must be after SchemaNC and ConfigNC
#define I_DSE_NULL           10

//
//  Attribute list table. This is not really a necessary construct.
//  could just use constants and trust compiler to optimize dups.
//

extern PWSTR    DsTypeAttributeTable[];

#define I_DSATTR_DC             0
#define I_DSATTR_DNSRECORD      1
#define I_DSATTR_DNSPROPERTY    2
#define I_DSATTR_OBJECTGUID     3
#define I_DSATTR_SD             4
#define I_DSATTR_WHENCREATED    5
#define I_DSATTR_WHENCHANGED    6
#define I_DSATTR_USNCREATED     7
#define I_DSATTR_USNCHANGED     8
#define I_DSATTR_OBJECTCLASS    9
#define I_DSATTR_NULL           10

#define DSATTR_DC               ( DsTypeAttributeTable[ I_DSATTR_DC ] )
#define DSATTR_DNSRECORD        ( DsTypeAttributeTable[ I_DSATTR_DNSRECORD ] )
#define DSATTR_DNSPROPERTY      ( DsTypeAttributeTable[ I_DSATTR_DNSPROPERTY ] )
#define DSATTR_SD               ( DsTypeAttributeTable[ I_DSATTR_SD ])
#define DSATTR_USNCHANGED       ( DsTypeAttributeTable[ I_DSATTR_USNCHANGED ] )
#define DSATTR_WHENCHANGED      ( DsTypeAttributeTable[ I_DSATTR_WHENCHANGED ] )
#define DSATTR_OBJECTCLASS      ( DsTypeAttributeTable[ I_DSATTR_OBJECTCLASS ] )
#define DSATTR_ENABLED          ( L"Enabled" )
#define DSATTR_DISPLAYNAME      ( L"displayName" )
#define DSATTR_BEHAVIORVERSION  ( L"msDS-Behavior-Version" )

#define DNSDS_TOMBSTONE_TYPE    ( DNS_TYPE_ZERO )
#define DNS_TYPE_TOMBSTONE      ( DNS_TYPE_ZERO )


//
//  Forest/domain/DSA behavior version constants.
//

//
//  Use this macro to set behavior version globals with "Forest", "Domain",
//  or "Dsa" as _LEVEL_. The actual value will be boosted up to the forced
//  value if it is lower.
//

#define SetDsBehaviorVersion( _LEVEL_, _VALUE_ )                        \
    g_ulDs##_LEVEL_##Version =                                          \
        ( SrvCfg_dwForce##_LEVEL_##BehaviorVersion !=                   \
            DNS_INVALID_BEHAVIOR_VERSION &&                             \
        SrvCfg_dwForce##_LEVEL_##BehaviorVersion > _VALUE_ ) ?          \
            SrvCfg_dwForce##_LEVEL_##BehaviorVersion :                  \
            _VALUE_;

#define IS_WHISTLER_FOREST()    \
    ( g_ulDsForestVersion >= DS_BEHAVIOR_WIN2003 )

#define IS_WHISTLER_DOMAIN()    \
    ( g_ulDsDomainVersion >= DS_BEHAVIOR_WIN2003 )


//
//  Active Directory version globals
//

extern ULONG        g_ulDsForestVersion;
extern ULONG        g_ulDsDomainVersion;
extern ULONG        g_ulDsDsaVersion;

#define DNS_INVALID_BEHAVIOR_VERSION    (-1)

extern ULONG        g_ulDownlevelDCsInDomain;   //  count of DCs that are
extern ULONG        g_ulDownlevelDCsInForest;   //      less than Whistler

#define DNS_INVALID_COUNT               (-1)


//
//  Misc globals
//

extern  WCHAR    g_szWildCardFilter[];
extern  WCHAR    g_szDnsZoneFilter[];
extern  PWCHAR   g_pszRelativeDnsSysPath;
extern  PWCHAR   g_pszRelativeDnsFolderPath;


//
//  Lazy writing control
//

extern LDAPControl      LazyCommitControl;
extern DWORD            LazyCommitDataValue;

//
//  No-referrals control
//

extern LDAPControl      NoDsSvrReferralControl;

//
//  SD control info
//

extern LDAPControl     SecurityDescriptorControl_DGO;
extern LDAPControl     SecurityDescriptorControl_D;

//
//  Search blob
//

typedef struct _DnsDsEnum
{
    PLDAPSearch     pSearchBlock;           // ldap search result on zone
    PLDAPMessage    pResultMessage;         // current page of message
    PLDAPMessage    pNodeMessage;           // message for current node
    PZONE_INFO      pZone;
    LONGLONG        SearchTime;
    LONGLONG        TombstoneExpireTime;
    DNS_STATUS      LastError;
    DWORD           dwSearchFlag;
    DWORD           dwLookupFlag;
    DWORD           dwHighestVersion;
    DWORD           dwTotalNodes;
    DWORD           dwTotalTombstones;
    DWORD           dwTotalRecords;
#if 0
    DWORD           dwHighUsnLength;
    CHAR            szHighUsn[ MAX_USN_LENGTH ];    // largest USN in enum
#endif
    CHAR            szStartUsn[ MAX_USN_LENGTH ];   // USN at search start

    //  node record data

    PLDAP_BERVAL *  ppBerval;           // the values in the array
    PDB_RECORD      pRecords;
    DWORD           dwRecordCount;
    DWORD           dwNodeVersion;
    DWORD           dwTombstoneVersion;
    BOOL            bAuthenticatedUserSD;
}
DS_SEARCH, *PDS_SEARCH;


#define DNSDS_SEARCH_LOAD       (0)
#define DNSDS_SEARCH_UPDATES    (1)
#define DNSDS_SEARCH_DELETE     (2)
#define DNSDS_SEARCH_TOMBSTONES (3)

//
//  Time LDAP searches
//

#define DS_SEARCH_START( searchTime ) \
        ( searchTime = GetTickCount() )

#define DS_SEARCH_STOP( searchTime ) \
        STAT_ADD( DsStats.LdapSearchTime, (GetTickCount() - searchTime) )


//
//  Function prototypes
//

PWCHAR
Ds_GenerateBaseDnsDn(
    IN      BOOL    fIncludeMicrosoftDnsFolder
    );

VOID
Ds_InitializeSearchBlob(
    IN      PDS_SEARCH      pSearchBlob
    );

VOID
Ds_CleanupSearchBlob(
    IN      PDS_SEARCH      pSearchBlob
    );

DNS_STATUS
Ds_GetNextMessageInSearch(
    IN OUT  PDS_SEARCH      pSearchBlob
    );

PWSTR
DS_CreateZoneDsName(
    IN      PZONE_INFO      pZone
    );

DNS_STATUS
Ds_SetZoneDp(
    IN      PZONE_INFO          pZone,
    IN      PDNS_DP_INFO        pDpInfo,
    IN      BOOL                fUseTempDsName
    );

DNS_STATUS
Ds_CreateZoneFromDs(
    IN      PLDAPMessage    pZoneMessage,
    IN      PDNS_DP_INFO    pDpInfo,
    OUT     PZONE_INFO *    ppZone,         OPTIONAL
    OUT     PZONE_INFO *    ppExistingZone  OPTIONAL
    );

DNS_STATUS
Ds_StartDsZoneSearch(
    IN OUT  PDS_SEARCH      pSearchBlob,
    IN      PZONE_INFO      pZone,
    IN      DWORD           dwSearchFlag
    );

DNS_STATUS
Ds_ReadZoneProperties(
    IN OUT  PZONE_INFO      pZone,
    IN      PLDAPMessage    pZoneMessage        OPTIONAL
    );

DNS_STATUS
Ds_LdapUnbind(
    IN OUT  PLDAP *         ppLdap
    );

DNS_STATUS
Ds_ReadServerObjectSD(
    PLDAP                   pldap,
    PSECURITY_DESCRIPTOR *  ppSd
    );

DNS_STATUS
Ds_LoadRootDseAttributes(
    IN      PLDAP           pLdap
    );
    
    
#endif  //  _DS_H_INCLUDED

//
//  end of ds.h
//
