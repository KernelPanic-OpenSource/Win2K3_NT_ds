//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//
//  Copyright (C) Microsoft Corporation, 1994 - 1999
//
//  File:       mappings.c
//
//--------------------------------------------------------------------------
/*++

Abstract:

    This file contains the Mappings from SAM Objects to DS Objects. It also
    contains routines to support SAM loopback, and DS notifications for SAM
    and  LSA notifications.


Author:

    Murlis 16-May-1996

Environment:

    User Mode - Win32

Revision History:

    MurliS      16-May-1996
        Created

    ChrisMay    14-Jun-96
        Added missing attributes and miscellaneous clean up, changed
        NextRid from LARGE_INTEGER to INTEGER to match the schema, re-
        moved superfluous attributes.

    ChrisMay    26-Jun-96
        Added work around so that SAMP_USER_FULL_NAME doesn't to the admin
        display name. Remapped SAMP_USER_GROUPS from zero to ATT_EXTENSION_ATTRIBUTE_2.

    DaveStr     11-Jul-96
        Added more attribute and class mapping information for SAM loopback.

    ColinBr     18-Jul-96
        Added 3 new mappings for membership relation SAM attributes. If
        a SAM object doesn't use these attributes(SAMP_USER_GROUPS,
        SAMP_ALIAS_MEMBERS, and SAMP_GROUP_MEMBERS), then they are mapped
        to a benign field (ATT_USER_GROUPS).

    Murlis      12-Jul-97
        Fixed loopback for single access check. Updated Comments.

--*/

#include <NTDSpch.h>
#pragma  hdrstop

#include <mappings.h>
#include <objids.h>
#include <direrr.h>
#include <mdlocal.h>
#include <mdcodes.h>
#include <dsatools.h>
#include <dsevent.h>
#include <dsexcept.h>
#include <debug.h>
#include <samwrite.h>
#include <anchor.h>
#include <sdprop.h>
#include <drautil.h>

#include <fileno.h>
#define  FILENO FILENO_MAPPINGS

#include <ntsam.h>
#include <samrpc.h>
#include <crypt.h>
#include <ntlsa.h>
#include <samisrv.h>
#include <samsrvp.h>
#include <nlrepl.h>
#include <windns.h>             /* DnsNameCompare_W */
#include "dstaskq.h"            /* task queue stuff */
#include <drameta.h>


#define NO_SAM_CHECKS 0

#define DEBSUB "MAPPINGS:"


//------------------------------------------------------------------------------
//
//                         GLOBALS defined and referenced in this file.
// Global flag to disable additional SAM loopback.
BOOL gfDoSamChecks = FALSE;
//
// Global LSA dll handle used for notification
//
HANDLE LsaDllHandle = NULL;
pfLsaIDsNotifiedObjectChange pfLsaNotify = NULL;

//-------------------------------------------------------------------------------


//--------------------------------------------------------------------------------
//++
//++  IMPORTANT NOTE REGARDING ATTRIBUTE MAPPINGS:
//++
//++   The Following SAM identifiers must map to the same attribute
//++   identifier in the DS. This is to facilitate Name and Rid based
//++   lookups, which often do not know the ObjectType in advance.
//++   These are
//++
//++    SAMP_UNKNOWN_OBJECTCLASS   -- Must map to the ATT_OBJECT_CLASS attribute
//++
//++
//++    SAMP_UNKNOWN_OBJECTRID  |
//++    SAMP_FIXED_GROUP_RID    |
//++    SAMP_FIXED_ALIAS_RID    |  -- Must Map to ATT_RID(or equiv)
//++    SAMP_FIXED_USERID       |     attribute. Note the we store the
//++                            |     Sid and not store the Rid in
//++                            |     the DS. To simplify switching
//++                            |     between the two implementations,
//++                            |     ATT_RID is always defined as an
//++                            |     attribute in the DS. Though it may
//++                            |     or may not be in an allowed attri-
//++                            |     bute in the schema. SAM always translates
//++                            |     ATT_RID to ATT_OBJECT_SID before making
//++                            |     the Directory Call
//++
//++    SAMP_UNKNOWN_OBJECTNAME |
//++    SAMP_ALIAS_NAME         |  -- Must Map to Unicode String Name Attribute
//++    SAMP_GROUP_NAME         |     (ATT_ADMIN_DISPLAY_NAME for now )
//++    SAMP_USER_ACCOUNT_NAME  |
//++
//++
//++
//++
//++    SAMP_UNKNOWN_OBJECTSID  |  -- Maps to SID Attribute
//++    SAMP_DOMAIN_SID         |     ATT_OBJECT_SID
//++                            |
//++
//--------------------------------------------------------------------------------



//-------------------------------------------------------------------------------
//++ On a per SAM object basis define SAM attribute to DS attribute mappings.
//++ The SAM constants are defined in mappings.h. Each mapping table entry consists
//++ of
//++    1. the SAM and corresponding DS attribute
//++    2. an identifier for the syntax of the attribute
//++    3. a constant describing whether loopback should allow non-SAM
//++       modification, SAM based modification or no modification.
//++    4. The types of operations that are allowed for the attribute.       
//++    5. A SAM access mask that identifies any SAM checks that need to be done
//++       on the domain object.
//++    6. A SAM access mask that identifies any SAM checks that need to be done
//++       on the account object.
//++    7. A boolean indicating whether an audit is generated upon modification.
//++    8. A SAM auditing mask indicating what types of audits are associated
//++       with the attribute. 
//++
//++    Fields #5 and #6 are used to perform SAM access checks over and above what
//++    the DS checks. The comments at the start of loopback.c give an overview of the
//++    loopback access check mechanism.
//++
//++ attribute to DS attribute mapping are done by the following mapping
//++ tables in the Ds interace wrapper.
//--------------------------------------------------------------------------------
// Define the mapping of server attributes.
SAMP_ATTRIBUTE_MAPPING ServerAttributeMappingTable[] =
{
    // Variable-Length Attributes

    // Security Descriptor on Server Object. Checks who can connect to this SAM
    // server

    { SAMP_SERVER_SECURITY_DESCRIPTOR,
      ATT_NT_SECURITY_DESCRIPTOR,
      OctetString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },

    // Fixed-Length Attributes  [ Refer to _SAMP_V1_0A_FIXED_LENGTH_SERVER ]

    // Revision Level of SAM database
    { SAMP_FIXED_SERVER_REVISION_LEVEL,
      ATT_REVISION,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

      // Revision Level of SAM database
    { SAMP_FIXED_SERVER_USER_PASSWORD,
      ATT_USER_PASSWORD,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS }
};

ULONG cServerAttributeMappingTable =
    sizeof(ServerAttributeMappingTable) /
        sizeof(SAMP_ATTRIBUTE_MAPPING);

// Define the mapping of domain attributes.

SAMP_ATTRIBUTE_MAPPING DomainAttributeMappingTable[] =
{
    // Variable-Length Attributes

    // Security Descriptor on Domain Object
    { SAMP_DOMAIN_SECURITY_DESCRIPTOR,
      ATT_NT_SECURITY_DESCRIPTOR,
      OctetString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },

    // Domain Sid identifying the domain, long
    { SAMP_DOMAIN_SID,
      ATT_OBJECT_SID,
      OctetString,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },

    // OEM information, UNICODE string attribute not really used,
    // by SAM, but other SAM clients may use it. Present for backwards
    // compatibility
    { SAMP_DOMAIN_OEM_INFORMATION,
      ATT_OEM_INFORMATION,
      UnicodeString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // Unicode String Attribute, gives the list of NT4 Replication Domain
    // Controllers.
    { SAMP_DOMAIN_REPLICA,
      ATT_DOMAIN_REPLICA,
      UnicodeString,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

      // Fixed-Length Attributes  [ Refer to _SAMP_V1_0A_FIXED_LENGTH_DOMAIN ]


    // Domain Creation Time maintained by SAM. Changing this will cause
    // Net Logon to Full Sync
    { SAMP_FIXED_DOMAIN_CREATION_TIME,
      ATT_CREATION_TIME,
      LargeInteger,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    // Net Logon Change Log Serial Number
    { SAMP_FIXED_DOMAIN_MODIFIED_COUNT,
      ATT_MODIFIED_COUNT,
      LargeInteger,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    // Domain Policy Attribute, Used to enforce the maximum Password
    // Age.
    { SAMP_FIXED_DOMAIN_MAX_PASSWORD_AGE,
      ATT_MAX_PWD_AGE,
      LargeInteger,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // Domain Policy Attribute, Used to enforce time interval betweeen
    // password changes
    { SAMP_FIXED_DOMAIN_MIN_PASSWORD_AGE,
      ATT_MIN_PWD_AGE,
      LargeInteger,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // Used in Computing the Kickoff time in SamIGetAccountRestrictions
    //
    { SAMP_FIXED_DOMAIN_FORCE_LOGOFF,
      ATT_FORCE_LOGOFF,
      LargeInteger,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // Used By Account Lockout System. Time for which the account is
    // locked out
    { SAMP_FIXED_DOMAIN_LOCKOUT_DURATION,
      ATT_LOCKOUT_DURATION,
      LargeInteger,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // The "Observation window time", after a bad password attempt during which
    // the server increments bad password attempts, so that it may lockout the
    // account if the lockout threshold is reached
    { SAMP_FIXED_DOMAIN_LOCKOUT_OBSERVATION_WINDOW,
      ATT_LOCK_OUT_OBSERVATION_WINDOW,
      LargeInteger,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // The Net Logon Change Log serial number at last promotion
    { SAMP_FIXED_DOMAIN_MODCOUNT_LAST_PROMOTION,
      ATT_MODIFIED_COUNT_AT_LAST_PROM,
      LargeInteger,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    // The Next Rid Field used by the mixed mode allocator
    { SAMP_FIXED_DOMAIN_NEXT_RID,
      ATT_NEXT_RID,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    // Password Properties. Part of Domain Policy. A bit field to
    // indicate complexity / storage restrictions
    { SAMP_FIXED_DOMAIN_PWD_PROPERTIES,
      ATT_PWD_PROPERTIES,
      Integer,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // Min Password Length. Part of Domain Policy
    { SAMP_FIXED_DOMAIN_MIN_PASSWORD_LENGTH,
      ATT_MIN_PWD_LENGTH,
      Integer,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // Password History length -- Part of Domain Policy
    { SAMP_FIXED_DOMAIN_PASSWORD_HISTORY_LENGTH,
      ATT_PWD_HISTORY_LENGTH,
      Integer,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // Used by Account Lockout. The Number of bad password attempts in
    // the Lockout observation window, that will cause the account to
    // be locked out. Part of Domain policy
    { SAMP_FIXED_DOMAIN_LOCKOUT_THRESHOLD,
      ATT_LOCKOUT_THRESHOLD,
      Integer,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // Enum indicating server enabled or disabled
    { SAMP_FIXED_DOMAIN_SERVER_STATE,
      ATT_SERVER_STATE,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },


    // Specifies Compatibility mode with LanMan 2.0 Servers
    { SAMP_FIXED_DOMAIN_UAS_COMPAT_REQUIRED,
      ATT_UAS_COMPAT,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },


    //
    // The Sam Account Type denotes the type of Sam Object
    //

    { SAMP_DOMAIN_ACCOUNT_TYPE,
      ATT_SAM_ACCOUNT_TYPE,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_DOMAIN_MIXED_MODE,
      ATT_NT_MIXED_DOMAIN,
      Integer,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES }, 

    { SAMP_DOMAIN_MACHINE_ACCOUNT_QUOTA,
      ATT_MS_DS_MACHINE_ACCOUNT_QUOTA,
      Integer,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },
   
    { SAMP_DOMAIN_BEHAVIOR_VERSION,
      ATT_MS_DS_BEHAVIOR_VERSION,
      Integer,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    { SAMP_DOMAIN_LASTLOGON_TIMESTAMP_SYNC_INTERVAL,
      ATT_MS_DS_LOGON_TIME_SYNC_INTERVAL,
      Integer,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },

    { SAMP_FIXED_DOMAIN_USER_PASSWORD,
      ATT_USER_PASSWORD,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS }

};

ULONG cDomainAttributeMappingTable =
    sizeof(DomainAttributeMappingTable) /
        sizeof(SAMP_ATTRIBUTE_MAPPING);

// Define the mapping of group attributes.

SAMP_ATTRIBUTE_MAPPING GroupAttributeMappingTable[] =
{
    // Variable-Length Attributes

    // EXISTING ATTRIBUTE(TOP): security Descriptor
    { SAMP_GROUP_SECURITY_DESCRIPTOR,
      ATT_NT_SECURITY_DESCRIPTOR,
      OctetString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },

    // EXISTING ATTRIBUTE (TOP)
    { SAMP_GROUP_NAME,
      ATT_SAM_ACCOUNT_NAME,
      UnicodeString,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_DEDICATED_AUDIT |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // EXISTING ATTRIBUTE (TOP)
    { SAMP_GROUP_ADMIN_COMMENT,
      ATT_DESCRIPTION,
      UnicodeString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },

    { SAMP_GROUP_MEMBERS,
      ATT_MEMBER,
      Dsname,
      SamWriteRequired,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES |
      SAMP_AUDIT_TYPE_DEDICATED_AUDIT },

    // Membership lists are manipulated using Arrays of RIDS in
    // in SAM. So some mappings from DS names to RIDS must exisit
    // when asking for Membership lists. Personal favourite: Ds interface
    // layer automatically maps a Dsname syntax stuff to a RID.

    // Fixed-Length Attributes [ Refer to SAMP_V1_0A_FIXED_LENGTH_GROUP ]


    // Rid, can some higher object have this ( like a SAM account object
    { SAMP_FIXED_GROUP_RID,
      ATT_RID,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },  

    { SAMP_FIXED_GROUP_OBJECTCLASS,
      ATT_OBJECT_CLASS,
      Integer,
      // Technically speaking, one can not write the object class attribute.
      // But this is insured by the core DS code, so we mark it as writable
      // here so that Samp*LoopbackRequired() don't reject legitimate add
      // and modify attempts.
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES }, 

    { SAMP_GROUP_SID_HISTORY,
      ATT_SID_HISTORY,
      Integer,
      SamWriteRequired,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_DEDICATED_AUDIT |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    { SAMP_GROUP_ACCOUNT_TYPE,
      ATT_SAM_ACCOUNT_TYPE,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_FIXED_GROUP_TYPE,
      ATT_GROUP_TYPE,
      Integer,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_DEDICATED_AUDIT |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    { SAMP_FIXED_GROUP_IS_CRITICAL,
      ATT_IS_CRITICAL_SYSTEM_OBJECT,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_FIXED_GROUP_MEMBER_OF,
      ATT_IS_MEMBER_OF_DL,
      Dsname,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_FIXED_GROUP_SID,
      ATT_OBJECT_SID,
      OctetString,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_GROUP_NON_MEMBERS,
      ATT_MS_DS_NON_MEMBERS,
      Dsname,
      SamWriteRequired,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },

    { SAMP_FIXED_GROUP_USER_PASSWORD,
      ATT_USER_PASSWORD,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS }
};

ULONG cGroupAttributeMappingTable =
    sizeof(GroupAttributeMappingTable) /
        sizeof(SAMP_ATTRIBUTE_MAPPING);

// Define the mapping of alias attributes.

SAMP_ATTRIBUTE_MAPPING AliasAttributeMappingTable[] =
{
    // Variable-Length Attributes

    // ?
    { SAMP_ALIAS_SECURITY_DESCRIPTOR,
      ATT_NT_SECURITY_DESCRIPTOR,
      OctetString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },

    // ?
    { SAMP_ALIAS_NAME,
      ATT_SAM_ACCOUNT_NAME,
      UnicodeString,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_DEDICATED_AUDIT | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // ?
    { SAMP_ALIAS_ADMIN_COMMENT,
      ATT_DESCRIPTION,
      UnicodeString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },

    // ?
    { SAMP_ALIAS_MEMBERS,
      ATT_MEMBER,
      Dsname,
      SamWriteRequired,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES |
      SAMP_AUDIT_TYPE_DEDICATED_AUDIT },

    // Fixed-Length Attributes  [ Refer to SAMP_V1_FIXED_LENGTH_ALIAS ]

    // RId of Alias
    { SAMP_FIXED_ALIAS_RID,
      ATT_RID,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_FIXED_ALIAS_OBJECTCLASS,
      ATT_OBJECT_CLASS,
      Integer,
      // Technically speaking, one can not write the object class attribute.
      // But this is insured by the core DS code, so we mark it as writable
      // here so that Samp*LoopbackRequired() don't reject legitimate add
      // and modify attempts.
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },

    { SAMP_ALIAS_SID_HISTORY,
      ATT_SID_HISTORY,
      Integer,
      SamWriteRequired,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_DEDICATED_AUDIT |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    { SAMP_ALIAS_ACCOUNT_TYPE,
      ATT_SAM_ACCOUNT_TYPE,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_FIXED_ALIAS_TYPE,
      ATT_GROUP_TYPE,
      Integer,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_DEDICATED_AUDIT |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    { SAMP_FIXED_ALIAS_SID,
      ATT_OBJECT_SID,
      OctetString,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_ALIAS_NON_MEMBERS,
      ATT_MS_DS_NON_MEMBERS,
      Dsname,
      SamWriteRequired,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },
    
    { SAMP_ALIAS_LDAP_QUERY,
      ATT_MS_DS_AZ_LDAP_QUERY,
      UnicodeString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },

    { SAMP_FIXED_ALIAS_IS_CRITICAL,
      ATT_IS_CRITICAL_SYSTEM_OBJECT,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS },

    { SAMP_FIXED_ALIAS_USER_PASSWORD,
      ATT_USER_PASSWORD,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_DEDICATED_AUDIT }
  
};

ULONG cAliasAttributeMappingTable =
    sizeof(AliasAttributeMappingTable) /
        sizeof(SAMP_ATTRIBUTE_MAPPING);

// Define the mapping of user attributes.

SAMP_ATTRIBUTE_MAPPING UserAttributeMappingTable[] =
{
    // Variable-Length Attributes

    // EXISTING ATTRIBUTE
    { SAMP_USER_SECURITY_DESCRIPTOR,
      ATT_NT_SECURITY_DESCRIPTOR,
      OctetString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },

    // EXISTING ATTRIBUTE (TOP) ( for now ) limit of 256 chars--needs a fix
    { SAMP_USER_ACCOUNT_NAME,
      ATT_SAM_ACCOUNT_NAME,
      UnicodeString,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_DEDICATED_AUDIT |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // BUG: Mapping SAMP_USER_FULL_NAME to ATT_ADMIN_DISPLAY_NAME is broken.
    // The temporary work around is to re-map it to ATT_USER_FULL_NAME, which
    // in turn maps to one of the available extended attributes (131495), a
    // mail-recipient extended attribute. This way, the user account name and
    // the full name attributes will not overwrite each other.

    { SAMP_USER_FULL_NAME,
      ATT_DISPLAY_NAME,
      UnicodeString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // EXISITING ATTRIBUTE(TOP): Admin comment already defined in DS
    // schema in top object
    { SAMP_USER_ADMIN_COMMENT,
      ATT_DESCRIPTION,
      UnicodeString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },
      
    // NEW: User Coment
    { SAMP_USER_USER_COMMENT,
      ATT_USER_COMMENT,
      UnicodeString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },

    // NEW: User Parameters. Don't have any clue
    { SAMP_USER_PARAMETERS,
      ATT_USER_PARAMETERS,
      UnicodeString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // NEW
    { SAMP_USER_HOME_DIRECTORY,
      ATT_HOME_DIRECTORY,
      UnicodeString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // NEW
    { SAMP_USER_HOME_DIRECTORY_DRIVE,
      ATT_HOME_DRIVE,
      UnicodeString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // NEW Script path
    { SAMP_USER_SCRIPT_PATH,
      ATT_SCRIPT_PATH,
      UnicodeString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // NEW Profile Path
    { SAMP_USER_PROFILE_PATH,
      ATT_PROFILE_PATH,
      UnicodeString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // NEW ?? Multivalued
    { SAMP_USER_WORKSTATIONS,
      ATT_USER_WORKSTATIONS,
      UnicodeString,
      SamWriteRequired,
      SamAllowReplaceAndRemove,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // NEW Attribute routines will need to map this to binary blob etc
    { SAMP_USER_LOGON_HOURS,
      ATT_LOGON_HOURS,
      OctetString,
      SamWriteRequired,
      SamAllowReplaceAndRemove,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // This lists which groups the user is a member of. Needs further work.
    { SAMP_USER_GROUPS,
      ATT_IS_MEMBER_OF_DL,
      Dsname,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    // NEW contains the DBCS password of the user
    { SAMP_USER_DBCS_PWD,
      ATT_DBCS_PWD,
      OctetString,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      USER_CHANGE_PASSWORD,
      SAMP_AUDIT_TYPE_OBJ_ACCESS  |
      SAMP_AUDIT_TYPE_DEDICATED_AUDIT },

    // NEW contains Unicode password. All passwords are binary as
    // they should be encrypted ( or hashed )

    { SAMP_USER_UNICODE_PWD,
      ATT_UNICODE_PWD,
      OctetString,
      SamWriteRequired,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_DEDICATED_AUDIT },

    // NEW, Multivalued giving last x passwords stored, in order to enforce
    // new passwords
    { SAMP_USER_NT_PWD_HISTORY,
      ATT_NT_PWD_HISTORY,
      OctetString,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    // NEW Same as above for LAN Man passwords -- Why concept of NT
    // and LAN man passwords
    { SAMP_USER_LM_PWD_HISTORY,
      ATT_LM_PWD_HISTORY,
      OctetString,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },


    // Fixed-Length Attributes [ Refer to SAMP_V1_0A_FIXED_LENGTH_USER ]



    // Last logon time
    { SAMP_FIXED_USER_LAST_LOGON,
      ATT_LAST_LOGON,
      LargeInteger,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    // Last Logoff time
    { SAMP_FIXED_USER_LAST_LOGOFF,
      ATT_LAST_LOGOFF,
      LargeInteger,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    // password last set time
    { SAMP_FIXED_USER_PWD_LAST_SET,
      ATT_PWD_LAST_SET,
      LargeInteger,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,  
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // Account expiry time
    { SAMP_FIXED_USER_ACCOUNT_EXPIRES,
      ATT_ACCOUNT_EXPIRES,
      LargeInteger,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // Last bad password time
    { SAMP_FIXED_USER_LAST_BAD_PASSWORD_TIME,
      ATT_BAD_PASSWORD_TIME,
      LargeInteger,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    // Don't know what this is. This is field in the fixed blob for the
    // user. Maybe Rid but need to explore.
    { SAMP_FIXED_USER_USERID,
      ATT_RID,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    // Rid of group ???
    { SAMP_FIXED_USER_PRIMARY_GROUP_ID,
      ATT_PRIMARY_GROUP_ID,
      Integer,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // ?????
    { SAMP_FIXED_USER_ACCOUNT_CONTROL,
      ATT_USER_ACCOUNT_CONTROL,
      Integer,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_DEDICATED_AUDIT |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // Country code of user
    { SAMP_FIXED_USER_COUNTRY_CODE,
      ATT_COUNTRY_CODE,
      Integer,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },

    // Code page of user
    { SAMP_FIXED_USER_CODEPAGE,
      ATT_CODE_PAGE,
      Integer,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },

    // bad password count, reset upon successful logon ?
    { SAMP_FIXED_USER_BAD_PWD_COUNT,
      ATT_BAD_PWD_COUNT,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    // ?
    { SAMP_FIXED_USER_LOGON_COUNT,
      ATT_LOGON_COUNT,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_FIXED_USER_OBJECTCLASS,
      ATT_OBJECT_CLASS,
      Integer,
      // Technically speaking, one can not write the object class attribute.
      // But this is insured by the core DS code, so we mark it as writable
      // here so that Samp*LoopbackRequired() don't reject legitimate add
      // and modify attempts.
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },

    { SAMP_USER_ACCOUNT_TYPE,
      ATT_SAM_ACCOUNT_TYPE,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_FIXED_USER_LOCAL_POLICY_FLAGS,
      ATT_LOCAL_POLICY_FLAGS,
      Integer,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },

    { SAMP_FIXED_USER_SUPPLEMENTAL_CREDENTIALS,
      ATT_SUPPLEMENTAL_CREDENTIALS,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_USER_SID_HISTORY,
      ATT_SID_HISTORY,
      Integer,
      SamWriteRequired,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,       
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_DEDICATED_AUDIT | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    { SAMP_FIXED_USER_LOCKOUT_TIME,
      ATT_LOCKOUT_TIME,
      Integer,
      SamWriteRequired,
      SamAllowReplaceOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,               
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_FIXED_USER_IS_CRITICAL,
      ATT_IS_CRITICAL_SYSTEM_OBJECT,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_FIXED_USER_UPN,
      ATT_USER_PRINCIPAL_NAME,
      UnicodeString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,         
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES }, 

    { SAMP_USER_CREATOR_SID,
      ATT_MS_DS_CREATOR_SID,
      Integer,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },

    { SAMP_FIXED_USER_SID,
      ATT_OBJECT_SID,
      OctetString,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_FIXED_USER_SITE_AFFINITY,
      ATT_MS_DS_SITE_AFFINITY,
      OctetString,
      SamWriteRequired,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_FIXED_USER_LAST_LOGON_TIMESTAMP,
      ATT_LAST_LOGON_TIMESTAMP,
      LargeInteger,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_FIXED_USER_CACHED_MEMBERSHIP,
      ATT_MS_DS_CACHED_MEMBERSHIP,
      OctetString,
      SamWriteRequired,
      SamAllowDeleteOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_FIXED_USER_CACHED_MEMBERSHIP_TIME_STAMP,
      ATT_MS_DS_CACHED_MEMBERSHIP_TIME_STAMP,
      LargeInteger,
      SamWriteRequired,
      SamAllowDeleteOnly,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_FIXED_USER_ACCOUNT_CONTROL_COMPUTED,
      ATT_MS_DS_USER_ACCOUNT_CONTROL_COMPUTED,
      Integer,
      SamReadOnly,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_USER_PASSWORD,
      ATT_USER_PASSWORD,
      UnicodeString,
      SamWriteRequired,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_DEDICATED_AUDIT },

    {
      SAMP_USER_A2D2LIST,
      ATT_MS_DS_ALLOWED_TO_DELEGATE_TO,
      UnicodeString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    {
      SAMP_USER_SPN,
      ATT_SERVICE_PRINCIPAL_NAME,
      UnicodeString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    {
      SAMP_USER_KVNO,
      ATT_MS_DS_KEYVERSIONNUMBER,
      Integer,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES },
    
    {
      SAMP_USER_DNS_HOST_NAME,
      ATT_DNS_HOST_NAME,
      UnicodeString,
      NonSamWriteAllowed,
      SamAllowAll,
      NO_SAM_CHECKS,
      NO_SAM_CHECKS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

};

ULONG cUserAttributeMappingTable =
    sizeof(UserAttributeMappingTable) /
        sizeof(SAMP_ATTRIBUTE_MAPPING);

//
//  The Unknown Attribute Mapping table is used when the SAM object
//  class is before hand not known, but must locate an object with
//  a given name or rid and find out its class. See important note a
//  above.
//

SAMP_ATTRIBUTE_MAPPING UnknownAttributeMappingTable[] =
{
    // Object Class
    { SAMP_UNKNOWN_OBJECTCLASS,
      ATT_OBJECT_CLASS,
      Integer,
      // Technically speaking, one can not write the object class attribute.
      // But this is insured by the core DS code, so we mark it as writable
      // here so that Samp*LoopbackRequired() don't reject legitimate add
      // and modify attempts.
      NonSamWriteAllowed,
      SamAllowAll,
      DOMAIN_ALL_ACCESS,
      DOMAIN_ALL_ACCESS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    // Rid of Object
    { SAMP_UNKNOWN_OBJECTRID,
      ATT_RID,
      Integer,
      SamReadOnly,
      SamAllowAll,
      DOMAIN_ALL_ACCESS,
      DOMAIN_ALL_ACCESS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    // Name of Object
    { SAMP_UNKNOWN_OBJECTNAME,
      ATT_SAM_ACCOUNT_NAME,
      UnicodeString,
      SamReadOnly,
      SamAllowAll,
      DOMAIN_ALL_ACCESS,
      DOMAIN_ALL_ACCESS,        
      SAMP_AUDIT_TYPE_OBJ_ACCESS | 
      SAMP_AUDIT_TYPE_DEDICATED_AUDIT |
      SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES },

    // SID of Object
    { SAMP_UNKNOWN_OBJECTSID,
      ATT_OBJECT_SID,
      OctetString,
      SamReadOnly,
      SamAllowAll,
      DOMAIN_ALL_ACCESS,
      DOMAIN_ALL_ACCESS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_UNKNOWN_COMMON_NAME,
      ATT_COMMON_NAME,
      UnicodeString,
      NonSamWriteAllowed,
      SamAllowAll,
      DOMAIN_ALL_ACCESS,
      DOMAIN_ALL_ACCESS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_UNKNOWN_ACCOUNT_TYPE,
      ATT_SAM_ACCOUNT_TYPE,
      Integer,
      SamReadOnly,
      SamAllowAll,
      DOMAIN_ALL_ACCESS,
      DOMAIN_ALL_ACCESS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS },

    { SAMP_UNKNOWN_GROUP_TYPE,
      ATT_GROUP_TYPE,
      Integer,
      SamWriteRequired,
      SamAllowAll,
      DOMAIN_ALL_ACCESS,
      DOMAIN_ALL_ACCESS,
      SAMP_AUDIT_TYPE_OBJ_ACCESS |
      SAMP_AUDIT_TYPE_DEDICATED_AUDIT }
};

// ++
// ++  Define Sam Object-to-DS class mappings
// ++

//
// Do not change order in this table without
// updating SampSamClassReferenced and logic
// in loopback.c -- special case
// for builtin domain hard codes it. Opening
// objects through loopback hardcodes the fact
// that local groups are the next entry after
// groups
//

#define CLASS_MAIL_RECIPIENT  196654

SAMP_CLASS_MAPPING ClassMappingTable[] =
{
    { CLASS_SAM_SERVER,
      SampServerObjectType,
      NON_SAM_CREATE_ALLOWED,
      &cServerAttributeMappingTable,
      ServerAttributeMappingTable,
      DOMAIN_ALL_ACCESS,                // domainAddRightsRequired
      DOMAIN_ALL_ACCESS,                // domainRemoveRightsRequired
      NO_SAM_CHECKS,                    // objectAddRightsRequired
      NO_SAM_CHECKS },                  // objectRemoveRightsRequired

    { CLASS_SAM_DOMAIN,
      SampDomainObjectType,
      NON_SAM_CREATE_ALLOWED,
      &cDomainAttributeMappingTable,
      DomainAttributeMappingTable,
      NO_SAM_CHECKS,                // domainAddRightsRequired
      NO_SAM_CHECKS,                // domainRemoveRightsRequired
      NO_SAM_CHECKS,                // objectAddRightsRequired
      NO_SAM_CHECKS },              // objectRemoveRightsRequired

    { CLASS_GROUP,
      SampGroupObjectType,
      SAM_CREATE_ONLY,
      &cGroupAttributeMappingTable,
      GroupAttributeMappingTable,
      NO_SAM_CHECKS,                    // domainAddRightsRequired
      NO_SAM_CHECKS,                    // domainRemoveRightsRequired
      NO_SAM_CHECKS,                    // objectAddRightsRequired
      NO_SAM_CHECKS },                  // objectRemoveRightsRequired

    { CLASS_GROUP,
      SampAliasObjectType,
      SAM_CREATE_ONLY,
      &cAliasAttributeMappingTable,
      AliasAttributeMappingTable,
      NO_SAM_CHECKS,                      // domainAddRightsRequired
      NO_SAM_CHECKS,                      // domainRemoveRightsRequired
      NO_SAM_CHECKS,                      // objectAddRightsRequired
      NO_SAM_CHECKS },                    // objectRemoveRightsRequired

    { CLASS_USER,
      SampUserObjectType,
      SAM_CREATE_ONLY,
      &cUserAttributeMappingTable,
      UserAttributeMappingTable,
      NO_SAM_CHECKS,                      // domainAddRightsRequired
      NO_SAM_CHECKS,                      // domainRemoveRightsRequired
      NO_SAM_CHECKS,                      // objectAddRightsRequired
      NO_SAM_CHECKS }                     // objectRemoveRightsRequired
};

ULONG cClassMappingTable =
    sizeof(ClassMappingTable) /
        sizeof(ClassMappingTable[0]);



ULONG
SampGetDsAttrIdByName(
    UNICODE_STRING AttributeIdentifier
    )

/*++

Routine Description:

Arguments:

Return Values:

--*/

{

    UCHAR   *name = NULL;
    ULONG   nameLen;
    ULONG   DsAttrId = (ULONG) DS_ATTRIBUTE_UNKNOWN;
    THSTATE *pTHS = pTHStls;
    ATTCACHE *pAC = NULL;


    if (0 == AttributeIdentifier.Length || NULL == AttributeIdentifier.Buffer)
    {
        goto Error;
    }

    name = MIDL_user_allocate( AttributeIdentifier.Length + sizeof(UCHAR) );

    if (name == NULL)
    {
        goto Error;
    }

    RtlZeroMemory(name, AttributeIdentifier.Length + sizeof(UCHAR));

    nameLen = WideCharToMultiByte(
                             CP_UTF8,
                             0,
                             (LPCWSTR) AttributeIdentifier.Buffer,
                             AttributeIdentifier.Length / sizeof(WCHAR),
                             name,
                             AttributeIdentifier.Length,
                             NULL,
                             NULL
                             );

    if (nameLen == 0)
    {
        goto Error;
    }

    pAC = SCGetAttByName(pTHS, nameLen, name);

    if (pAC == NULL)
    {
        goto Error;
    }

    DsAttrId = pAC->id;

Error:

    if (name != NULL)
    {
        MIDL_user_free(name);
    }

    return DsAttrId;
}


ULONG
SampGetSamAttrIdByName(
    SAMP_OBJECT_TYPE ObjectType,
    UNICODE_STRING AttributeIdentifier
    )

/*++

Routine Description:

Arguments:

Return Values:

--*/

{

    UCHAR   *name = NULL;
    ULONG   nameLen;
    ULONG   DsAttrId = (ULONG) DS_ATTRIBUTE_UNKNOWN;
    ULONG   SamAttrId = (ULONG) SAM_ATTRIBUTE_UNKNOWN;
    THSTATE *pTHS = pTHStls;
    ATTCACHE *pAC = NULL;


    if (0 == AttributeIdentifier.Length ||
        NULL == AttributeIdentifier.Buffer)
    {
        goto Error;
    }
    name = MIDL_user_allocate( AttributeIdentifier.Length + sizeof(UCHAR) );

    if (name == NULL)
    {
        goto Error;
    }

    RtlZeroMemory(name, AttributeIdentifier.Length + sizeof(UCHAR));

    nameLen = WideCharToMultiByte(
                             CP_UTF8,
                             0,
                             (LPCWSTR) AttributeIdentifier.Buffer,
                             AttributeIdentifier.Length / sizeof(WCHAR),
                             name,
                             AttributeIdentifier.Length,
                             NULL,
                             NULL
                             );

    if (nameLen == 0)
    {
        goto Error;
    }

    pAC = SCGetAttByName(pTHS, nameLen, name);

    if (pAC == NULL)
    {
        goto Error;
    }

    DsAttrId = pAC->id;

    if (DS_ATTRIBUTE_UNKNOWN == DsAttrId)
    {
        goto Error;
    }

    SamAttrId = SampSamAttrFromDsAttr(ObjectType, DsAttrId);

Error:

    if (name != NULL)
    {
        MIDL_user_free(name);
    }

    return SamAttrId;
}


//++
//++  Mapping functions
//++
ULONG
SampDsAttrFromSamAttr(SAMP_OBJECT_TYPE ObjectType, ULONG SamAttributeType)
/*++

Routine Description:

    Get a DS attribute from a SAM attribute

Arguments:

    ObjectType -- specifies the SAM object type
    SamAttributeType Specifies the SAM atribute

Return Values:

    Ds Attribute if one exisits
    DS_ATTRUBUTE_UNKNOWN other wise. Will assert if cannot map

--*/

{
    ULONG Index;
    ULONG DsAttributeId = (ULONG) DS_ATTRIBUTE_UNKNOWN;
    SAMP_ATTRIBUTE_MAPPING * MappingTable;
    ULONG MappingTableSize;

    // Determine the Mapping table to use
    switch(ObjectType)
    {
    case SampServerObjectType:
        MappingTable = ServerAttributeMappingTable;
        MappingTableSize = sizeof(ServerAttributeMappingTable)/sizeof(ServerAttributeMappingTable[0]);
        break;
    case SampDomainObjectType:
        MappingTable = DomainAttributeMappingTable;
        MappingTableSize = sizeof(DomainAttributeMappingTable)/sizeof(DomainAttributeMappingTable[0]);
        break;
    case SampGroupObjectType:
        MappingTable = GroupAttributeMappingTable;
        MappingTableSize = sizeof(GroupAttributeMappingTable)/sizeof(GroupAttributeMappingTable[0]);
        break;
    case SampAliasObjectType:
        MappingTable = AliasAttributeMappingTable;
        MappingTableSize = sizeof(AliasAttributeMappingTable)/sizeof(AliasAttributeMappingTable[0]);
        break;
    case SampUserObjectType:
        MappingTable = UserAttributeMappingTable;
        MappingTableSize = sizeof(UserAttributeMappingTable)/sizeof(UserAttributeMappingTable[0]);
        break;
    case SampUnknownObjectType:
        MappingTable = UnknownAttributeMappingTable;
        MappingTableSize = sizeof(UnknownAttributeMappingTable)/sizeof(UnknownAttributeMappingTable[0]);
        break;
    default:
        goto Error;
    }

    // Walk through the Mapping table
    for (Index=0; Index<MappingTableSize; Index++ )
    {
        if (MappingTable[Index].SamAttributeType == SamAttributeType)
        {
            DsAttributeId = MappingTable[Index].DsAttributeId;
            goto Found;
        }
    }

    // Assert as we did not find match
    Assert(FALSE);

Found:
    return DsAttributeId;

Error:
    // Assert as we did not find table match
    Assert(FALSE);
    goto Found;
}


ULONG
SampSamAttrFromDsAttr(SAMP_OBJECT_TYPE ObjectType, ULONG DsAttributeId)
/*++
Routine Description:

    Get a SAM attribute from a DS attribute

Arguments:

    ObjectType -- specifies the SAM object type
    DSAttributeId Specifies the DS atribute

Return Values:

    SAM Attribute if one exisits
    SAM_ATTRUBUTE_UNKNOWN other wise. Will assert if cannot map
--*/

{
    ULONG Index;
    ULONG SamAttributeType = (ULONG) SAM_ATTRIBUTE_UNKNOWN;
    SAMP_ATTRIBUTE_MAPPING * MappingTable;
    ULONG MappingTableSize;

    // Determine the Mapping table to use
    switch(ObjectType)
    {
    case SampServerObjectType:
        MappingTable = ServerAttributeMappingTable;
        MappingTableSize = sizeof(ServerAttributeMappingTable)/sizeof(ServerAttributeMappingTable[0]);
        break;
    case SampDomainObjectType:
        MappingTable = DomainAttributeMappingTable;
        MappingTableSize = sizeof(DomainAttributeMappingTable)/sizeof(DomainAttributeMappingTable[0]);
        break;
    case SampGroupObjectType:
        MappingTable = GroupAttributeMappingTable;
        MappingTableSize = sizeof(GroupAttributeMappingTable)/sizeof(GroupAttributeMappingTable[0]);
        break;
    case SampAliasObjectType:
        MappingTable = AliasAttributeMappingTable;
        MappingTableSize = sizeof(AliasAttributeMappingTable)/sizeof(AliasAttributeMappingTable[0]);
        break;
    case SampUserObjectType:
        MappingTable = UserAttributeMappingTable;
        MappingTableSize = sizeof(UserAttributeMappingTable)/sizeof(UserAttributeMappingTable[0]);
        break;
    case SampUnknownObjectType:
        MappingTable = UnknownAttributeMappingTable;
        MappingTableSize = sizeof(UnknownAttributeMappingTable)/sizeof(UnknownAttributeMappingTable[0]);
        break;
    default:
        goto Error;
    }

    // Walk through the Mapping table
    for (Index=0; Index<MappingTableSize; Index++ )
    {
        if (MappingTable[Index].DsAttributeId == DsAttributeId)
        {
            SamAttributeType = MappingTable[Index].SamAttributeType;
            goto Found;
        }
    }

    // Assert as we did not find match
    Assert(FALSE);

Found:
    return SamAttributeType;

Error:
    // Assert as we did not find table match
    Assert(FALSE);
    goto Found;
}

ULONG
SampDsClassFromSamObjectType(ULONG SamObjectType)
/*++

Routine Description:

    Get a DS class from a SAM object type

Arguments:
    ObjectType -- specifies the SAM object type

Return Values:

    Ds class if one exisits
    DS_CLASS_UNKNOWN other wise. Will assert if cannot map

 --*/

{
    ULONG Index;
    ULONG DsClass = (ULONG) DS_CLASS_UNKNOWN;

    for (Index=0; Index<sizeof(ClassMappingTable)/sizeof(ClassMappingTable[0]); Index++ )
    {
        if ((ULONG) ClassMappingTable[Index].SamObjectType == SamObjectType)
        {
            DsClass = ClassMappingTable[Index].DsClassId;
            goto Found;
        }
    }

    // Assert as we did not find match
    Assert(FALSE);

Found:
    return DsClass;
}




ULONG
SampSamObjectTypeFromDsClass(ULONG  DsClass)
/*++

Routine Description:

    Get a SAM object type  from a DS class

Arguments:

    DsClass  -- Specifies the DS class

Return Values:
    SAM object type if one exisits
    SampUnknownObjectType other wise. Will assert if cannot map

  BUG:

  This routines makes the assumption that there is a 1:1 mapping between SAM objects and
  DS classes. This is not true for Group / Alias Objects. So something needs to be done
  about this later.

--*/

{
    int Index;
    ULONG SamObjectType = SampUnknownObjectType;

    for (Index=0; Index<sizeof(ClassMappingTable)/sizeof(ClassMappingTable[0]); Index++ )
    {
        if (ClassMappingTable[Index].DsClassId == DsClass)
        {
            SamObjectType = ClassMappingTable[Index].SamObjectType;
            goto Found;
        }
    }

    // Assert as we did not find match
    Assert(FALSE);

Found:
    return SamObjectType;
}

/////////////////////////////////////////////////////////////////////////////
//                                                                         //
// SAM Transactioning Routines                                             //
//                                                                         //
/////////////////////////////////////////////////////////////////////////////

// The default DSA operation is that each Dir* call is individually
// transacted.  This is insufficient when SAM is calling the DS for
// two reasons.
//
// 1)   A single Samr* operation (eg: SamrSetInformationUser) may result
//      in multiple Dir* calls.  All these calls together should constitute
//      a single transaction.  This is achieved by giving SAM control over
//      the transaction.  Transactions are started as late as possible by
//      calling SampMaybeTransactionBegin in newsam2\server\dslayer.c just
//      before making the Dir* call.  The "maybe" aspect of the call is
//      that a new transaction is started only if one doesn't exist.  SAM
//      explicitly calls SampMaybeTransactionEnd(fAbort = FALSE) in its
//      normal commit path and SampMaybeTransactionEnd(fAbort = TRUE) when
//      it frees the global read/write lock.  This latter is a no-op if the
//      transaction has already been terminated.
//
// 2)   A single Dir* call which references both SAM and !SAM attributes
//      may result in multiple Samr* calls to handle the SAM attributes.
//      All these calls together should constitute a single transaction.
//      In this case we want the transactioning of the original Dir* call
//      to be the real thing and SAM to not perform any transactioning at
//      all.  This is achieved via logic in SampMaybeBeginTransaction and
//      the THSTATE.fSamDoCommit flag.  If a THSTATE already exists, then
//      SampMaybeBeginTransaction is a no-op - i.e. an existing transaction
//      is already open.  If THSTATE.fSamDoCommit is FALSE, then
//      SampMaybeEndTransaction is a no-op as well.  Thus multiple Samr*
//      calls can be made within the transaction context of an existing
//      Dir* call.

NTSTATUS
SampMaybeBeginDsTransaction(
    SAMP_DS_TRANSACTION_CONTROL ReadOrWrite
    )

/*++

Routine Description:

    Conditionally initializes a thread state and begins a new transaction.
    This routine is called by SAM just before performing a DS operation.
    If the thread began life in the DS and is looping back through SAM,
    then this method is a no-op as a thread state and open transaction
    already exist. In case only a thread state exists and no open transaction
    exists SampMaybeBeginTransaction, will use that thread state and open a
    transaction on that thread state. In no circumstance will this routine
    return a success and not have an open transaction. Upon Failure the caller
    is assured that everything is cleaned up and the thread state is NULL and
    there is no open transaction.

Arguments:

    ReadOrWrite - flag indicating the type of transaction.

Return Values:

    STATUS_NO_MEMORY or STATUS_UNSUCCESSFUL if unsuccessful
    STATUS_SUCCESS otherwise

--*/
{
    NTSTATUS    Status = STATUS_SUCCESS;
    USHORT      transType;
    THSTATE     *pTHS = NULL;
    ULONG  dwException, ulErrorCode, dsid;
    PVOID dwEA;
    DWORD err = 0;

    Assert((TransactionRead == ReadOrWrite) ||
                    (TransactionWrite == ReadOrWrite));

    __try {

        //
        // Create a thread state; routine is no op if thread state
        // already exists.
        //
        err = THCreate(CALLERTYPE_SAM);
        if ( err )
        {
            Status = STATUS_NO_MEMORY;
            goto Error;
        }
        pTHS=pTHStls;
        Assert(NULL != pTHS);

        //
        // Begin Transaction if Required
        //

        if (NULL==pTHS->pDB)
        {
            // Thread State Exists, but Database pointer is NULL,
            // so no transaction exists. Proceed on opening
            // a new transaction
            //

            // Indicate to DS who the caller is.
            pTHS->fSAM = TRUE;
    

            //
            // Murlis 10/10/96. SAM must call into the DS
            // with fDSA flag set as DS must not perform
            // access checks for calls initiated by SAM
            //
            pTHS->fDSA = TRUE;

            // Indicate whether SAM should commit DS or not.

            pTHS->fSamDoCommit = TRUE;

            // Open database and start read or write transaction.

            transType = ((TransactionWrite == ReadOrWrite)
                         ? SYNC_WRITE
                         : SYNC_READ_ONLY);

            if ( 0 != SyncTransSet(transType) )
            {
                Status = STATUS_UNSUCCESSFUL;
            }
        } // End of NULL==pTHS->pDB
    } // End of Try
    __except(GetExceptionData(GetExceptionInformation(), &dwException,
                              &dwEA, &ulErrorCode, &dsid))
    {
            HandleDirExceptions(dwException, ulErrorCode, dsid);
            Status = STATUS_UNSUCCESSFUL;
    }

Error:

    //
    // If we did not succeed and if we created a thread state , then free it.
    //

    if (!NT_SUCCESS(Status))
    {
        if (NULL!=pTHS)
        {
            //
            // The only way we have a thread state, but not a open DB is
            // when SyncTransSet failed.
            //

            Assert(pTHS->pDB==NULL);
            THDestroy();
        }
    }

    return(Status);
}

NTSTATUS
SampMaybeEndDsTransaction(
    SAMP_DS_TRANSACTION_CONTROL CommitOrAbort
    )

/*++

Routine Description

    Conditionally commits the DS transaction and cleans up the thread state.
    This routine is called by SAM and performs the commit and cleanup iff
    this thread state corresponds to a single Samr* call which originated
    in SAM.  If the call originated in the DS, then the routine is a no-op
    thereby allowing multiple Samr* calls to be treated as a single transaction.

Arguments:

    CommitOrAbort - flag indicating whether to commit or abort the transaction.
                    Valid values are
                         TransactionCommit --- Commits a transaction
                         TransactionAbort  --- Aborts a transaction
                         TransactionCommitAndKeepThreadState -- Commit transaction
                                        and keep the thread state, so that further
                                        processing can continue. SampMaybeBeginDsTransaction
                                        can be used to start another DS transaction on this
                                        thread state.

                        TransactionAbortAndKeepThreadState --
                        This aborts the current transaction and keeps the
                        thread state

Return Values:

    STATUS_SUCCESSFUL on success.
    STATUS_UNSUCCESSFUL on error.

--*/
{
    THSTATE  *pTHS=pTHStls;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOL     fAbort;
    ULONG    dwException, ulErrorCode, dsid;
    PVOID    dwEA;
    int      iErr;
    BOOL     fFreeThreadState = FALSE;

    Assert((TransactionCommit == CommitOrAbort) ||
            (TransactionCommitAndKeepThreadState == CommitOrAbort) ||
             (TransactionAbortAndKeepThreadState == CommitOrAbort) ||
                    (TransactionAbort == CommitOrAbort));

    // The only case in which we don't have a thread state is when an earlier
    // commit removed it and we're being called from the cleanup path in SAM.
    // In this case, SAM may not ask for a commit.

    __try {
        fAbort = ((TransactionAbort == CommitOrAbort)||
                    (TransactionAbortAndKeepThreadState == CommitOrAbort));

        if ( (NULL != pTHS)&&(pTHS->fSamDoCommit)&&(NULL!=pTHS->pDB)) {


            //
            // This is a case where a thread state exists, an open data
            // base exists ( which this routine interprets as an open
            // transaction ) and fSamDoCommit is set. This means that the
            // the transaction is to be committed or aborted and the thread
            // state freed.
            //

            fFreeThreadState = TRUE;

            // In the original DS usage, one passed an existing error
            // code as the second argument to CleanReturn and it would
            // be returned as CleanReturn's status.  I.e. CleanReturn
            // merely re-affirmed that your return from a Dir* call was
            // not clean.  It throws an exception if the actual commit
            // fails.  We have no error code from SAM - all we have is
            // the abort/commit flag.  So pass in a 0 and assert that
            // CleanReturn returns 0 in case it ever changes behaviour.

            // Nuke the GC verification Cache
            pTHS->GCVerifyCache = NULL;

            iErr = CleanReturn(pTHS, 0, fAbort);
            Assert(0 == iErr);
            
        } else if ((NULL!=pTHS)&&(NULL==pTHS->pDB)&&(pTHS->fSamDoCommit)) {
            
            //
            // This is the case where only a thread state exists. This happens
            // in cleanup paths, where a transaction was not begun. In this case
            // free the thread state
            //

            fFreeThreadState = TRUE;
        }
    }
    __except(GetExceptionData(GetExceptionInformation(), &dwException,
                              &dwEA, &ulErrorCode, &dsid)) {
        HandleDirExceptions(dwException, ulErrorCode, dsid);
        Status = STATUS_UNSUCCESSFUL;
    }

    // Perform THDestroy here because CleanReturn can throw an
    // exception and we need to insure that the thread state is cleaned
    // up on both the success and exception handled cases.

    if (   (fFreeThreadState)
        && (CommitOrAbort!=TransactionCommitAndKeepThreadState)
        && (CommitOrAbort!=TransactionAbortAndKeepThreadState))
    {
        THDestroy();
    }

    return(Status);
}

BOOL
SampExistsDsTransaction(
    void
    )

/*++

Routine Description

    Helper to determine whether a SAM transaction is in effect.

Arguments:

    None

Return Values:

    TRUE if transaction is in effect, FALSE otherwise.

--*/

{
    THSTATE *pTHS;
    // There are code paths (WKSTA and Server) where SAM will
    // call SampIsWriteLockHeldByDs() which in turn calls this routine
    // when the DSA is not initialized.  In that case dwTSindex is
    // uninitialized and we should not reference it.

    return((dwTSindex != (DWORD)-1) && (NULL != (pTHS = pTHStls)) &&
           (NULL!=pTHS->pDB) && (pTHS->transactionlevel>0));
}

VOID
SampSplitNT4SID(
    IN NT4SID       *pAccountSid,
    IN OUT NT4SID   *pDomainSid,
    OUT ULONG       *pRid
    )

/*++

Routine Description:

    DaveStr - 7/17/96 - Copied from newsam2\server\utility.c to avoid new
    exports from samsrv.dll and converted to be NT4SID based - i.e. SID
    sizes are all sizeof(NT4SID) - therefore no allocations required.

    This function splits a sid into its domain sid and rid.

Arguments:

    pAccountSid - Specifies the Sid to be split.  The Sid is assumed to be
        syntactically valid.  Sids with zero subauthorities cannot be split.

    pDomainSid - Pointer to output NT4SID representing the domain SID.

    pRid - Pointer to ULONG to hold the RID on output.

Return Value:

    None.

--*/

{
    UCHAR       AccountSubAuthorityCount;
    ULONG       AccountSidLength;

    //
    // Calculate the size of the domain sid
    //

    AccountSubAuthorityCount = *RtlSubAuthorityCountSid(pAccountSid);


    Assert(AccountSubAuthorityCount >= 1);

    AccountSidLength = RtlLengthSid(pAccountSid);

    if (AccountSidLength > MAX_NT4_SID_SIZE) {
        // This is not a valid NT4 sid. DB must be corrupt?
        Assert(!"Invalid SID");
        // We don't want to continue, or we are going to
        // overrun the pDomainSid buffer. Except now.
        DsaExcept(DSA_DB_EXCEPTION, ERROR_INVALID_SID, 0);
    }

    //
    // Copy the Account sid into the Domain sid
    //

    RtlMoveMemory(pDomainSid, pAccountSid, AccountSidLength);

    //
    // Decrement the domain sid sub-authority count
    //

    (*RtlSubAuthorityCountSid(pDomainSid))--;

    //
    // Copy the rid out of the account sid
    //

    *pRid = *RtlSubAuthoritySid(pAccountSid, AccountSubAuthorityCount-1);
}

NTSTATUS
SampDsCtrlOpUpdateUserSupCreds(
    SAMP_SUPPLEMENTAL_CREDS SuppCreds
    )
/*++

Routine Description:

    Will update the Supplment creds

Arguments:

    SuppCreds - Supp creds struct contains unicode password and DSNAME of user.

Return Value:

    STATUS_SUCESS

--*/

{
    DWORD err = 0;
    NTSTATUS NtStatus = STATUS_SUCCESS;
    ATTRBLOCK attrBlockIn, attrBlockOut;
    ULONG i;
    THSTATE *pTHS=NULL;
    BOOL fCommit = FALSE;

    // The extra attributes required by the attribute ATT_USER_PASSWORD when 
    // a modify is noticed
    ULONG samUserPasswordRequiredAttrs[] =
    {
        ATT_SAM_ACCOUNT_NAME,
        ATT_OBJECT_SID, 
        ATT_USER_PRINCIPAL_NAME, 
        ATT_USER_ACCOUNT_CONTROL,
        ATT_SUPPLEMENTAL_CREDENTIALS
    };

    #define NELEMENTS(x) (sizeof(x)/sizeof((x)[0]))

    RtlZeroMemory(&attrBlockIn, sizeof(attrBlockIn));
    RtlZeroMemory(&attrBlockOut, sizeof(attrBlockOut));

    NtStatus = SampMaybeBeginDsTransaction(TransactionWrite);
    if (!NT_SUCCESS(NtStatus)) {
        goto Exit;
    }

    pTHS=pTHStls;

    try {
    
        err = DBFindDSName(pTHS->pDB,
                           SuppCreds.pUserName);
    
        if (err) {
            err = pTHS->errCode;
            _leave;
        }

        // obtain requested input parameters
        attrBlockIn.pAttr = THAllocEx(pTHS, 
                                      NELEMENTS(samUserPasswordRequiredAttrs) * sizeof(ATTR));
        attrBlockIn.attrCount = NELEMENTS(samUserPasswordRequiredAttrs);
        for (i = 0; i < NELEMENTS(samUserPasswordRequiredAttrs); i++) {
    
            ATTCACHE *pAC = SCGetAttById(pTHS, samUserPasswordRequiredAttrs[i]);
            ATTR  *pAttr = NULL;
            ULONG  attrCount = 0;
            Assert(NULL != pAC);
    
            err = DBGetMultipleAtts(pTHS->pDB,
                                    1,
                                    &pAC,
                                    NULL, // no range
                                    NULL,
                                    &attrCount,
                                    &pAttr,
                                    DBGETMULTIPLEATTS_fEXTERNAL,
                                    0);
            if (err) {
                err = pTHS->errCode;
                _leave;
            }
            if (0 == attrCount) {
                // This is handled
                attrBlockIn.pAttr[i].attrTyp = samUserPasswordRequiredAttrs[i];
                attrBlockIn.pAttr[i].AttrVal.valCount = 0;
            } else {
                attrBlockIn.pAttr[i] = *pAttr;
            }
        }
    
        NtStatus = SamIHandleObjectUpdate(eSamObjectUpdateOpCreateSupCreds,
                                          SuppCreds.UpdateInfo,        
                                          &attrBlockIn,
                                          &attrBlockOut);
    
        if ( !NT_SUCCESS(NtStatus) ) {

            err = pTHS->errCode;
            _leave;
        }
    
        // apply the attributes sent back
        for (i = 0; i < attrBlockOut.attrCount; i++) {
    
            // update the object
            ATTCACHE *pAC = SCGetAttById(pTHS, attrBlockOut.pAttr[i].attrTyp);
            Assert(NULL != pAC);
    
            err = DBReplaceAtt_AC(pTHS->pDB,
                                  pAC,
                                  &attrBlockOut.pAttr[i].AttrVal,
                                  NULL);
    
            if (err) {
                SetSvcErrorEx(SV_PROBLEM_BUSY,
                              DIRERR_DATABASE_ERROR,
                              err);
                err = pTHS->errCode;
                _leave;
            }
        }

        err = DBRepl(pTHS->pDB, FALSE, 0, NULL, META_STANDARD_PROCESSING);
        if (err) {
            _leave;
        }

        fCommit = TRUE;

    }
    __finally {

        NTSTATUS NtStatus2;

        if (!fCommit) {
            // only resource errors are expected here
            NtStatus = STATUS_INSUFFICIENT_RESOURCES;
            DBCancelRec(pTHS->pDB);
        }

        NtStatus2 = SampMaybeEndDsTransaction(fCommit?
                                              TransactionCommitAndKeepThreadState:
                                              TransactionAbortAndKeepThreadState
                                              );
        if (NT_SUCCESS(NtStatus)) {
            NtStatus = NtStatus2;
        }
    }

    Exit:

    return NtStatus;

}


NTSTATUS
SampDsCtrlOpFillGuidAndSid(
    IN OUT DSNAME *DSName
    )
/*++

Routine Description:

    This routine improves a DSName by attempting to find the object or 
    corresponding phantom and intialize the Guid and Sid.  A thread state
    must exist and a transaction must be open.
        
    This routine exists to give callers from SAM access to DB API semantics
    when initializing DSNAMEs with Guids and Sids.  Dir Apis do not find 
    Sids on phantoms but DB APIs do.
    
    This routine changes database currency and exceptions can be thrown 
    from DB/Jet.

Arguments:

    DSName - The DSNAME to be improved.
    
Return Value:

    STATUS_SUCCESS -- If the routine successfully filled the Guid, the Sid, 
                      or both.
                      
    STATUS_UNSUCCESSFUL -- A called routine failed.  A service error is logged.

--*/
{
    THSTATE *pTHS=pTHStls;
    DWORD dwErr;
    COMMRES CommRes;
    
    //
    // Attempt to improve the DSNAME by resolving either the Guid and/or Sid.
    //    
        
    //
    // Changes currency to position on the object
    //
    dwErr = DBFindDSName(pTHS->pDB, DSName);
    
    if ((0 == dwErr) || (DIRERR_NOT_AN_OBJECT == dwErr))
    {
        //
        // Either the object or a corresponding phantom was found
        //
        dwErr = DBFillGuidAndSid(pTHS->pDB, DSName);
    }   
    
    //
    // Initialize COMMRES to map the error to an NTSTATUS
    //
    RtlZeroMemory(&CommRes, sizeof(COMMRES));
    CommRes.aliasDeref = FALSE;
    CommRes.errCode = pTHS->errCode;
    CommRes.pErrInfo = pTHS->pErrInfo;        
    
    return DirErrorToNtStatus(dwErr, &CommRes);
    
}   

                         
SAMP_AUDIT_NOTIFICATION*
SampAuditFindNotificationToUpdate(
    IN PSID Sid
    )
/*++

Routine Description:

    This routine attempts to find an existing audit notification for the 
    object associated with Sid.
    
    An audit notification is considered a match if the Sid is equal and
    the existing notification is not for a delete.  Deletion notifications
    are not merged with modifications as they preclude the need to audit
    the modification.  
    
Arguments:

    Sid - Object Sid to match.
    
Return Value:

    Pointer to the matching audit notification if found.
    
    Otherwise, NULL.

--*/
{
    THSTATE *pTHS = pTHStls; 
    SAMP_AUDIT_NOTIFICATION *NotificationList = pTHS->pSamAuditNotificationHead;
    SAMP_AUDIT_NOTIFICATION *NotificationToUpdate = NULL;

    //
    // Search the notification list for an audit entry for this object.
    //      
    for (; NotificationList; NotificationList = NotificationList->Next)
    {
        if (RtlEqualSid(Sid, NotificationList->Sid)) {
            
            if (SecurityDbDelete == NotificationList->DeltaType) {
                //
                // This entry has a matching Sid but is a delete which obviates
                // the need for any information about modifications.
                //
                goto Cleanup;
            
            } else { 
                //
                // This entry has a matching Sid and isn't a delete,
                // we'll update this entry.
                //
                NotificationToUpdate = NotificationList;
                break;
            }         
        }             
    } 
    
Cleanup:

    return NotificationToUpdate;

}
 

VOID
SampAuditInitNotification(
    IN ULONG iClass,
    IN DS_DB_OBJECT_TYPE ObjectType,
    IN SECURITY_DB_DELTA_TYPE DeltaType,
    IN PSID Sid,
    IN PUNICODE_STRING AccountName,
    IN ULONG AccountControl,          
    IN ULONG GroupType,
    IN PPRIVILEGE_SET Privileges,
    IN ULONG AuditType,
    IN OUT SAMP_AUDIT_NOTIFICATION **AuditNotification
    )
/*++

    Routine Description:

       This routine encapsulates the allocation and generic initialization
       of a new audit notification.  Type specific initialization is left
       to the caller.
       
       This routine will throw exceptions if memory allocations fail.
       
    Parameters:
    
       iClass - Object class index into SAM object mapping table.
        
       ObjectType - The type of object associated with this audit.
       
       DeltaType - The type of change being made.
       
       Sid - The object sid of the object being audited.
       
       AccountName - The SAM account name of the object being audited.
       
       AccountControl - The account control, if any, of the object being
                        audited.  This value is ignored until the object 
                        is a user/computer.
                        
       GroupType - The type of group, if the object is a group.  This value
                   is ignored if the object is not a group.
                   
       Privileges - Privilege set
        
       AuditType - This value should have only one bit set indicating the type
                   of audit notification to create.
       
       AuditNotification - Points to a pointer to an audit notification.  If
                           this points to NULL then the notification will 
                           be allocated. 
               
    Return Value

       None.                                                  

--*/
{
    THSTATE *pTHS = pTHStls;
    ULONG cbSid = 0;
    NTSTATUS NtStatus = STATUS_SUCCESS;
    
    //
    // Verify we're not passing ourselves invalid inputs
    //
    Assert(NULL != AuditNotification);
    Assert(NULL != Sid);
    
    //
    //
    // Allocate and initialize a new audit notification
    //
    if (NULL == *AuditNotification) {
        *AuditNotification = THAllocEx(pTHS, sizeof(SAMP_AUDIT_NOTIFICATION));    
        
        cbSid = RtlLengthSid(Sid);
        
        (*AuditNotification)->Sid = THAllocEx(pTHS, cbSid);  
                    
        NtStatus = RtlCopySid(cbSid, (*AuditNotification)->Sid, Sid);
        Assert(STATUS_SUCCESS == NtStatus);
    }                                                    
    
    //
    // We rely on THAllocEx to zero memory here providing expected defaults
    // like LsapAuditSamAttrNoValue for each LSAP_SAM_AUDIT_ATTR_DELTA_TYPE
    //
    (*AuditNotification)->iClass = iClass;
    (*AuditNotification)->ObjectType = ObjectType;
    (*AuditNotification)->DeltaType = DeltaType;
    (*AuditNotification)->AccountName = AccountName;
    (*AuditNotification)->AccountControl = AccountControl;
    (*AuditNotification)->GroupType = GroupType;
    
    //
    // Preserve any privileges already present
    //
    if (NULL == (*AuditNotification)->Privileges) {
        (*AuditNotification)->Privileges = Privileges;
    }
    
    (*AuditNotification)->AuditType |= AuditType;
    
    return;

}


VOID
SampAuditQueueNotification(
    IN SAMP_AUDIT_NOTIFICATION *AuditNotification
    )
/*++

    Routine Description:

       This routine queue an audit notification to the THSTATE.
       
    Parameters:
    
       AuditNotification - Fully initialized audit notification to queue.
                      
    Return Value

       None.                                                  

--*/  
{   
    THSTATE *pTHS = pTHStls;
       
    if (pTHS->pSamAuditNotificationTail)
    {
        pTHS->pSamAuditNotificationTail->Next = AuditNotification;
    }

    pTHS->pSamAuditNotificationTail = AuditNotification;

    if (NULL==pTHS->pSamAuditNotificationHead)
    {
        pTHS->pSamAuditNotificationHead = AuditNotification;
    }
    
}  
          

VOID
SampAuditValidateNotificationList(
    VOID
    )
/*++

Routine Description:

    This routine validates the current thread's audit notification queue.
    
    This routine should only be called in debug builds.
    
Arguments:

    None.
    
Return Value:

    None.

--*/
{
    THSTATE *pTHS = pTHStls; 
    SAMP_AUDIT_NOTIFICATION *NotificationList;
    SAMP_AUDIT_NOTIFICATION *Notification;;

    //
    // Scan the notification list performing validations
    //
    for (NotificationList = pTHS->pSamAuditNotificationHead; 
         NotificationList; 
         NotificationList = NotificationList->Next) {
        
        Assert(NULL != NotificationList->Sid);
        
        //
        // Compare the current notification's Sid with all other notification's
        // Sids to ensure there is never more than one notification per
        // object per transaction.
        //
        for (Notification = pTHS->pSamAuditNotificationHead;
             Notification;
             Notification = Notification->Next) {
            
            if (NotificationList == Notification) {
                continue;
            }
            
            Assert(!RtlEqualSid(NotificationList->Sid, Notification->Sid));
        }
    }
    
    return;
    
}


NTSTATUS
SampDsCtrlOpUpdateAuditNotification(
    IN OUT PSAMP_UPDATE_AUDIT_NOTIFICATION Update
    )
/*++

Routine Description:

    This routine searches the current thread's audit notification queue
    for an appropriate entry to update.  If an appropriate entry is not
    found, one is created.  
    
    The field that is updated is determined by the value of Update->UpdateType.

    There is no output returned aside from the error status.              
              
Arguments:

    Update - The audit notification update information structure.
    
Return Value:

    STATUS_SUCCESS -- If the routine successfully updates/creates the audit 
                      notification.
                      
    STATUS_UNSUCCESSFUL -- A called routine failed.

--*/
{
    NTSTATUS NtStatus = STATUS_SUCCESS;
    THSTATE *pTHS = pTHStls; 
    SAMP_AUDIT_NOTIFICATION *NotificationList = pTHS->pSamAuditNotificationHead;
    SAMP_AUDIT_NOTIFICATION *NotificationToUpdate = NULL;
    PLSAP_AUDIT_USER_ATTR_VALUES UserInfo = NULL;
    
#if DBG
    SampAuditValidateNotificationList();
#endif
    
    NotificationToUpdate = SampAuditFindNotificationToUpdate(Update->Sid);
                   
    //
    // Create a new template notification entry if a suitable one wasn't found.
    // This notification's initialization will be completed later by
    // SampAuditAddNotifications
    //
    if (NULL == NotificationToUpdate) {
        
        SampAuditInitNotification(
            0,
            0,
            0,
            Update->Sid,
            NULL,
            0,          
            0,
            NULL,
            SAMP_AUDIT_TYPE_PRE_CREATED,
            &NotificationToUpdate
            );  
                               
        SampAuditQueueNotification(NotificationToUpdate);
    }
    
    Assert(NotificationToUpdate);
        
    //
    // We have a valid notification, lets store the requested state information.
    //
    switch (Update->UpdateType) {
        
        case SampAuditUpdateTypePrivileges:
            //
            // Do not overwrite previous state, we always keep the original.
            //
            if (NULL == NotificationToUpdate->Privileges) {
                
                NotificationToUpdate->Privileges = 
                    THAllocEx(
                        pTHS,
                        sizeof(PRIVILEGE_SET)
                        );
                
                RtlCopyMemory(
                    NotificationToUpdate->Privileges,
                    Update->UpdateData.Privileges,
                    sizeof(*(Update->UpdateData.Privileges))
                    );
            }        
            
            break;
        
        case SampAuditUpdateTypeUserAccountControl:          
            //
            // Create the attribute info structure if necessary
            //
            if (NULL == NotificationToUpdate->TypeSpecificInfo) {
                
                NotificationToUpdate->TypeSpecificInfo = 
                    THAllocEx(
                        pTHS,
                        sizeof(LSAP_AUDIT_USER_ATTR_VALUES)
                        );
            }
            
            UserInfo = NotificationToUpdate->TypeSpecificInfo;
            
            //
            // Do not overwrite previous state, we always keep the original.
            //
            if (NULL == UserInfo->PrevUserAccountControl) {
                
                UserInfo->PrevUserAccountControl =
                    THAllocEx(
                        pTHS,
                        sizeof(ULONG)
                        );  
                
                *(UserInfo->PrevUserAccountControl) = 
                    Update->UpdateData.IntegerData;
            }
            
            break;
        
        default:
            Assert(FALSE && "Undefined audit notification update type");
            break;
    }
    
#if DBG
    SampAuditValidateNotificationList();
#endif

    return NtStatus;
    
}   


NTSTATUS        
SampDsControl(
    IN PSAMP_DS_CTRL_OP RequestedOp,
    OUT PVOID *Result
    )
/*++

Routine Description:

    This routine is a generic in process interface for SAM to call into the DS.
    
    The requested operation indicated in RequestedOp will be performed and any
    result will be returned through Result.  The RequestedOp->OpData depends
    on RequestedOp->OpType as does the type of Result.
    
    See SAMP_DS_CTRL_OP for operation specific usage.
    
    A thread state should exist and a transaction must be open when 
    calling this routine.
    
    If Result is allocated, the allocation is performed with THAlloc.
        
Arguments:

    RequestedOp - Points to a SAMP_DS_CTRL_OP and determines what operation
                  will be performed as well as the type of Result.

    Result - Points the the address of the result.
    
Return Value:

    STATUS_SUCCESS -- The requested operation succeeded and Result is valid.
    
    STATUS_INSUFFICIENT_RESOURCES -- A resource contraint prevented success.
    
    Operation specific errors returned from called routines.
    
--*/
{
    NTSTATUS NtStatus = STATUS_SUCCESS;
    ULONG dwException, ulErrorCode, dsid;
    PVOID dwEA;
    
    //
    // Validate inputs
    //
    Assert(NULL != RequestedOp);
    Assert(NULL != Result);    
    
    //
    // Verify callers are honoring the THS and open transaction requirement.
    //
    Assert(SampExistsDsTransaction());
    
    //
    // Initialize operation result
    //
    *Result = NULL;
    
    __try {
        //
        // Call operation specific worker routine
        //
        switch (RequestedOp->OpType) {
            
            case SampDsCtrlOpTypeFillGuidAndSid:
                
                NtStatus = SampDsCtrlOpFillGuidAndSid(
                               RequestedOp->OpBody.FillGuidAndSid.DSName
                               );
                
                if (NT_SUCCESS(NtStatus)) {
                    *Result = RequestedOp->OpBody.FillGuidAndSid.DSName;
                }
                
                break;

            case SampDsCtrlOpTypeClearPwdForSupplementalCreds:

                NtStatus = SampDsCtrlOpUpdateUserSupCreds(
                               RequestedOp->OpBody.UpdateSupCreds
                               );

                break;  
                
            case SampDsCtrlOpTypeUpdateAuditNotification:
                
                NtStatus = SampDsCtrlOpUpdateAuditNotification(
                               &RequestedOp->OpBody.UpdateAuditNotification
                               );
                
                break;

            default:
                //
                // Was a new operation control type added but not handled here?
                //
                Assert(FALSE && "Undefined DS operation control type");
                break;
                
        }
    }
    __except(GetExceptionData(GetExceptionInformation(), &dwException,
                              &dwEA, &ulErrorCode, &dsid))
    {
        HandleDirExceptions(dwException, ulErrorCode, dsid);
        NtStatus = STATUS_UNSUCCESSFUL;
    }
    
    return NtStatus;
    
}


BOOL
SampSamClassReferenced(
    CLASSCACHE  *pClassCache,
    ULONG       *piClass
    )

/*++

Routine Description:

    Determines whether the CLASSCACHE entry provided refers to a
    class SAM manages.

Arguments:

    pClassCache - pointer to a valid CLASSCACHE entry.

    piClass - ULONG pointer which is filled on output to reflect the
        index of the SAM class in ClassMappingTable if the class indeed
        is a SAM class.

Return Value:

    TRUE if arguments reflect a SAM class, FALSE otherwise.

--*/

{
    ULONG objClass;
    ULONG AuxClass;
    ULONG samClass;

    //
    // Special case out the Builtin Domain. The object of class Builtin
    // domain actually maps to DomainObjectType in SAM. However the mapping
    // table defined in this file maps the SAM DomainObjectType to 
    // Domain DNS which is the object class of the (account) domain object.
    // Therefore special case the builtin domain case.
    //

    if (CLASS_BUILTIN_DOMAIN==pClassCache->ClassId)
    {
        *piClass = 1;
        return (TRUE);
    }

    //
    // Walk through the class table
    //
    //

    for ( samClass = 0; samClass < cClassMappingTable; samClass++ )
    {
        if ( ClassMappingTable[samClass].DsClassId == pClassCache->ClassId )
        {
            *piClass = samClass;

            return(TRUE);
        }

        // Iterate over all classes in the class inheritance chain.

        for ( objClass = 0; objClass < pClassCache->SubClassCount; objClass++ )
        {
            if ( pClassCache->pSubClassOf[objClass] ==
                        ClassMappingTable[samClass].DsClassId )
            {
                *piClass = samClass;

                return(TRUE);
            }
        }

       // Iterate over all the auxillary classes in the class inheritance

       for ( AuxClass = 0; AuxClass < pClassCache->AuxClassCount; AuxClass++ )
       {
           if ( pClassCache->pAuxClass[AuxClass] ==
                       ClassMappingTable[samClass].DsClassId )
           {
               *piClass = samClass;

               return(TRUE);
           }
       }



    }

    return(FALSE);
}

BOOL
SampSamAttributeModified(
    ULONG       iClass,
    MODIFYARG   *pModifyArg
    )

/*++

Routine Description:

    Determines if any of the MODIFYARGs refer to attributes which are
    Sam Related.

Arguments:

    iClass - index of the SAM class in ClassMappingTable.

    pModifyArg - pointer to MODIFYARG representing attributes being modified.


Return Value:

    TRUE if a SAM attribute is referenced on success
    FALSE otherwise

--*/

{
    ATTRMODLIST             *pAttrMod;
    ULONG                   objAttr;
    ULONG                   samAttr;
    ULONG                   cAttrMapTable;
    SAMP_ATTRIBUTE_MAPPING  *rAttrMapTable;



    cAttrMapTable = *ClassMappingTable[iClass].pcSamAttributeMap;
    rAttrMapTable = ClassMappingTable[iClass].rSamAttributeMap;
    pAttrMod = &pModifyArg->FirstMod;

    // Iterate over attributes in MODIFYARG.

    for ( objAttr = 0; objAttr < pModifyArg->count; objAttr++ )
    {
        // Iterate over this SAM class' mapped attributes.

        for ( samAttr = 0; samAttr < cAttrMapTable; samAttr++ )
        {
            if ( pAttrMod->AttrInf.attrTyp ==
                                rAttrMapTable[samAttr].DsAttributeId )
            {
                // A Sam Attribute has been referenced
                return TRUE;
            }
        }

        pAttrMod = pAttrMod->pNextMod;
    }

    return FALSE;
}

ATTRTYP
SamNonReplicatedAttrs[]=
{
    ATT_LAST_LOGON,
    ATT_LAST_LOGOFF,
    ATT_BAD_PWD_COUNT,
    ATT_LOGON_COUNT,
    ATT_MODIFIED_COUNT,
    ATT_BAD_PASSWORD_TIME
};

BOOL
SampSamReplicatedAttributeModified(
    ULONG       iClass,
    MODIFYARG   *pModifyArg
    )

/*++

Routine Description:

    Determines if any of the MODIFYARGs refer to attributes which are
    Sam Related and also are to be replicated. Currently the only attribute
    that this routine includes in this list are the Logon statistics
    attribute ( LAST_LOGON, LAST_LOGOFF, BAD_PWD_COUNT, LOGON_COUNT )

Arguments:

    iClass - index of the SAM class in ClassMappingTable.

    pModifyArg - pointer to MODIFYARG representing attributes being modified.


Return Value:

    TRUE if a SAM attribute is referenced on success
    FALSE otherwise

--*/

{
    ATTRMODLIST             *pAttrMod;
    ULONG                   objAttr;
    ULONG                   samAttr;
    ULONG                   cAttrMapTable;
    SAMP_ATTRIBUTE_MAPPING  *rAttrMapTable;



    cAttrMapTable = *ClassMappingTable[iClass].pcSamAttributeMap;
    rAttrMapTable = ClassMappingTable[iClass].rSamAttributeMap;
    pAttrMod = &pModifyArg->FirstMod;

    // Iterate over attributes in MODIFYARG.

    for ( objAttr = 0; objAttr < pModifyArg->count; objAttr++ )
    {
        // Iterate over this SAM class' mapped attributes.

        for ( samAttr = 0; samAttr < cAttrMapTable; samAttr++ )
        {
            if ( pAttrMod->AttrInf.attrTyp ==
                                rAttrMapTable[samAttr].DsAttributeId )
            {
                //
                // A Sam Attribute has been referenced
                // Check to see if it is replicated
                //

                BOOLEAN NonReplicatedAttribute = FALSE;
                ULONG i;

                for (i=0;i<ARRAY_COUNT(SamNonReplicatedAttrs);i++)
                {
                    if (pAttrMod->AttrInf.attrTyp == SamNonReplicatedAttrs[i])
                    {
                        NonReplicatedAttribute = TRUE;
                    }
                }

                //
                // If the attribute is not replicated then return TRUE
                //

                if (!NonReplicatedAttribute)
                {
                    return TRUE;
                }
            }
        }

        pAttrMod = pAttrMod->pNextMod;
    }

    return FALSE;
}

ULONG
SampAddLoopbackRequired(
    ULONG       iClass,
    ADDARG      *pAddArg,
    BOOL        *pfLoopbackRequired,
    BOOL        *pfUserPasswordSupport
    )

/*++

Routine Description:

    Determines if any of the ADDARGS refer to attributes which are
    SamWriteRequired.  Returns an error if ADDARGS reference
    attributes which are SamReadOnly.

Arguments:

    iClass - index of the SAM class in ClassMappingTable.

    pAddArg - pointer to ADDARG representing attributes being added.

    pfLoopbackRequired - pointer to BOOL which is set to TRUE iff no errors
        and SamWriteRequired attributes are in the ADDARG.

Return Value:

    0 on success, error code otherwise.  Sets pTHStls->errCode on error.

--*/

{
    ATTR                    *rAttr;
    ULONG                   objAttr;
    ULONG                   samAttr;
    ULONG                   cAttrMapTable;
    SAMP_ATTRIBUTE_MAPPING  *rAttrMapTable;

    *pfUserPasswordSupport = gfUserPasswordSupport;

    // Special case the Non Domain NC creation case

    if (SampDomainObjectType==ClassMappingTable[iClass].SamObjectType)
    {
        CROSS_REF * pCR;
        COMMARG     Commarg;

        //
        // Find the cross ref for the domain in which the object is purported
        // to reside
        //

        pCR = FindBestCrossRef(pAddArg->pObject,&Commarg);

        if  ((NULL!=pCR) &&
            (NameMatched(pCR->pNC, pAddArg->pObject)) &&
            (!(pCR->flags & FLAG_CR_NTDS_DOMAIN ))) 
        {
            //
            // Creating a non domain NC
            //

            *pfLoopbackRequired = FALSE;
            return(0);

        }

        //
        // Fall through the default creation of domains path, any failure
        // in FindBestCrossRef (e.g no matching cross ref )
        // will also make the code fallback into the default loopback check
        // which will prevent the creation of a domain
        //

        THClearErrors();
    }

    // By definition, an add of a SAM object must be looped back.

    *pfLoopbackRequired = TRUE;

    cAttrMapTable = *ClassMappingTable[iClass].pcSamAttributeMap;
    rAttrMapTable = ClassMappingTable[iClass].rSamAttributeMap;
    rAttr = pAddArg->AttrBlock.pAttr;

    // Iterate over attributes in ADDARG.

    for ( objAttr = 0; objAttr < pAddArg->AttrBlock.attrCount; objAttr++ )
    {
        // Iterate over this SAM class' mapped attributes.

        for ( samAttr = 0; samAttr < cAttrMapTable; samAttr++ )
        {
            if ( rAttr[objAttr].attrTyp ==
                                rAttrMapTable[samAttr].DsAttributeId )
            {
                switch ( rAttrMapTable[samAttr].writeRule )
                {
                case SamWriteRequired:

                    //
                    // We only treat ATT_USER_PASSWORD as a loopback arg
                    // if the heuristic gfUserPasswordSupport is true
                    //

                    if ( !((rAttr[objAttr].attrTyp == ATT_USER_PASSWORD) &&
                        !*pfUserPasswordSupport) ) {

                        *pfLoopbackRequired = TRUE;
                    
                    } 
                    // Don't return immediately since we want to process
                    // the rest of the ADDARG looking for SamReadOnly
                    // cases since that requires us to return an error.

                    break;

                case SamReadOnly:


                    SetSvcError(
                            SV_PROBLEM_WILL_NOT_PERFORM,
                            DIRERR_ATTRIBUTE_OWNED_BY_SAM);

                    return(pTHStls->errCode);

                case NonSamWriteAllowed:

                    break;

                default:

                    LogUnhandledError(STATUS_UNSUCCESSFUL);
                    Assert(!"Missing SAMP_WRITE_RULES case");
                }
            }
        }
    }

    return(0);
}

ULONG
SampModifyLoopbackRequired(
    ULONG       iClass,
    MODIFYARG   *pModifyArg,
    BOOL        *pfLoopbackRequired,
    BOOL        *pfUserPasswordSupport
    )

/*++

Routine Description:

    Determines if any of the MODIFYARGs refer to attributes which are
    SamWriteRequired.  Returns an error if MODIFYARGs reference
    attributes which are SamReadOnly.

Arguments:

    iClass - index of the SAM class in ClassMappingTable.

    pModifyArg - pointer to MODIFYARG representing attributes being modified.

    pfLoopbackRequired - pointer to BOOL which is set to TRUE iff no errors
        and SamWriteRequired attributes are in the MODIFYARGs.

Return Value:

    0 on success, error code otherwise.  Sets pTHStls->errCode on error.

--*/

{
    ATTRMODLIST             *pAttrMod;
    ULONG                   objAttr;
    ULONG                   samAttr;
    ULONG                   cAttrMapTable;
    SAMP_ATTRIBUTE_MAPPING  *rAttrMapTable;

    *pfLoopbackRequired = FALSE;
    *pfUserPasswordSupport = gfUserPasswordSupport;

    cAttrMapTable = *ClassMappingTable[iClass].pcSamAttributeMap;
    rAttrMapTable = ClassMappingTable[iClass].rSamAttributeMap;
    pAttrMod = &pModifyArg->FirstMod;

    // Iterate over attributes in MODIFYARG.

    for ( objAttr = 0; objAttr < pModifyArg->count; objAttr++ )
    {
        // Iterate over this SAM class' mapped attributes.

        for ( samAttr = 0; samAttr < cAttrMapTable; samAttr++ )
        {
            if ( pAttrMod->AttrInf.attrTyp ==
                                rAttrMapTable[samAttr].DsAttributeId )
            {
                switch ( rAttrMapTable[samAttr].writeRule )
                {
                case SamWriteRequired:

                    //
                    // We only treat ATT_USER_PASSWORD as a loopback arg
                    // if the heuristic gfUserPasswordSupport is true
                    //

                    if ( !((pAttrMod->AttrInf.attrTyp == ATT_USER_PASSWORD) &&
                        !*pfUserPasswordSupport) ) {

                        *pfLoopbackRequired = TRUE;

                    }

                    // Don't return immediately since we want to process
                    // the rest of the MODIFYARG looking for SamReadOnly
                    // cases since that requires us to return an error.

                    break;

                case SamReadOnly:

                    SetSvcError(
                            SV_PROBLEM_WILL_NOT_PERFORM,
                            DIRERR_ATTRIBUTE_OWNED_BY_SAM);

                    return(pTHStls->errCode);

                case NonSamWriteAllowed:

                    break;

                default:

                    LogUnhandledError(STATUS_UNSUCCESSFUL);
                    Assert(!"Missing SAMP_WRITE_RULES case");
                }
            }
        }

        pAttrMod = pAttrMod->pNextMod;
    }

    return(0);
}

VOID
SampBuildAddCallMap(
    ADDARG              *pArg,
    ULONG               iClass,
    ULONG               *pcCallMap,
    SAMP_CALL_MAPPING   **prCallMap,
    ACCESS_MASK         *pDomainModifyRightsRequired,
    ACCESS_MASK         *pObjectModifyRightsRequired,
    BOOL                fUserPasswordSupport
    )

/*++

Routine Description:

    Converts an ADDARG to a SAMP_CALL_MAPPING array.

Arguments:

    pArg - pointer to ADDARG to convert.

    iClass - index of the SAM class in ClassMappingTable.

    pcCallMap - pointer to ULONG which holds size of call mapping on return.

    prCallMap - pointer to call mapping array which is allocated and filled
        on return.

Return Value:

    None.

--*/

{
    THSTATE                *pTHS=pTHStls;
    ULONG                   i;
    ULONG                   cCallMap;
    SAMP_CALL_MAPPING       *rCallMap;
    ULONG                   cAttributeMap;
    SAMP_ATTRIBUTE_MAPPING  *rAttributeMap;
    ULONG                   iMappedAttr;

    *pDomainModifyRightsRequired = 0;
    *pObjectModifyRightsRequired = 0;

    // Allocate return data.

    cCallMap = pArg->AttrBlock.attrCount;
    rCallMap = (SAMP_CALL_MAPPING *) THAllocEx(pTHS,
                            (cCallMap) * sizeof(SAMP_CALL_MAPPING));

    // Fill in the return data.

    cAttributeMap = *ClassMappingTable[iClass].pcSamAttributeMap;
    rAttributeMap = ClassMappingTable[iClass].rSamAttributeMap;

    for ( i = 0; i < cCallMap; i++ )
    {
        rCallMap[i].fSamWriteRequired = FALSE;
        rCallMap[i].choice = AT_CHOICE_ADD_ATT;
        rCallMap[i].attr = pArg->AttrBlock.pAttr[i];

        // Determine if this is a SAM attribute.
        // Iterate over each attribute mapped by this SAM class.

        for ( iMappedAttr = 0; iMappedAttr < cAttributeMap; iMappedAttr++ )
        {
            if (   (pArg->AttrBlock.pAttr[i].attrTyp ==
                                    rAttributeMap[iMappedAttr].DsAttributeId)
                && (rAttributeMap[iMappedAttr].writeRule != NonSamWriteAllowed)
                // Test for special cross domain move case so as to allow
                // IDL_DRSRemoteAdd to write the SID history.
                && !(    (ATT_SID_HISTORY == pArg->AttrBlock.pAttr[i].attrTyp)
                      && (pTHS->fCrossDomainMove) ) )
            {

                //
                // We only treat ATT_USER_PASSWORD as a loopback arg
                // if the heuristic gfUserPasswordSupport is true
                //

                if ( !((pArg->AttrBlock.pAttr[i].attrTyp == ATT_USER_PASSWORD) &&
                    !fUserPasswordSupport) ) {
                
                    // This is a mapped attribute.
    
                    rCallMap[i].fSamWriteRequired = TRUE;
                    rCallMap[i].iAttr = iMappedAttr;
    
                    // Add in any new access rights required.
    
                    *pDomainModifyRightsRequired |=
                            rAttributeMap[iMappedAttr].domainModifyRightsRequired;
                    *pObjectModifyRightsRequired |=
                            rAttributeMap[iMappedAttr].objectModifyRightsRequired;
    
                    // By the time the loopback code makes Samr calls
                    // to write the mapped attribute, the object will
                    // already have been added via SamrCreate<type>InDomain.
                    // SAM writes all mapped properties on creation to insure
                    // that they have a legal default value.  Thus we tag
                    // the choice as AT_CHOICE_REPLACE_ATT since that is
                    // the corresponding legal operation to perform on an
                    // existing value.  Except in the case of group membership
                    // where the desired operator is AT_CHOICE_ADD_VALUES as
                    // per SampWriteGroupMembers in samwrite.c.
    
                    if ( ATT_MEMBER == pArg->AttrBlock.pAttr[i].attrTyp )
                    {
                        rCallMap[i].choice = AT_CHOICE_ADD_VALUES;
                    }
                    else
                    {
                        rCallMap[i].choice = AT_CHOICE_REPLACE_ATT;
                    }

                }

                break;
            }
        }
    }

    // Assign return values.

    *pcCallMap = cCallMap;
    *prCallMap = rCallMap;
}

VOID
SampBuildModifyCallMap(
    MODIFYARG           *pArg,
    ULONG               iClass,
    ULONG               *pcCallMap,
    SAMP_CALL_MAPPING   **prCallMap,
    ACCESS_MASK         *pDomainModifyRightsRequired,
    ACCESS_MASK         *pObjectModifyRightsRequired,
    BOOL                fUserPasswordSupport
    )

/*++

Routine Description:

    Converts a MODIFYARG to a SAMP_CALL_MAPPING array.

Arguments:

    pArg - pointer to MODIFYARG to convert.

    iClass - index of the SAM class in ClassMappingTable.

    pcCallMap - pointer to ULONG which holds size of call mapping on return.

    prCallMap - pointer to call mapping array which is allocated and filled
        on return.

Return Value:

    None.

--*/

{
    THSTATE                *pTHS=pTHStls;
    ULONG                   i;
    ULONG                   cCallMap;
    SAMP_CALL_MAPPING       *rCallMap;
    ULONG                   cAttributeMap;
    SAMP_ATTRIBUTE_MAPPING  *rAttributeMap;
    ULONG                   iMappedAttr;
    ATTRMODLIST             *pAttrMod;

    *pDomainModifyRightsRequired = 0;
    *pObjectModifyRightsRequired = 0;

    // Allocate return data.

    cCallMap = pArg->count;
    rCallMap = (SAMP_CALL_MAPPING *) THAllocEx(pTHS,
                            (cCallMap) * sizeof(SAMP_CALL_MAPPING));

    // Fill in the return data.

    cAttributeMap = *ClassMappingTable[iClass].pcSamAttributeMap;
    rAttributeMap = ClassMappingTable[iClass].rSamAttributeMap;

    pAttrMod = &pArg->FirstMod;

    for ( i = 0; i < cCallMap; i++ )
    {
        rCallMap[i].fSamWriteRequired = FALSE;
        rCallMap[i].choice = pAttrMod->choice;
        rCallMap[i].attr = pAttrMod->AttrInf;

        // Determine if this is a SAM attribute.
        // Iterate over each attribute mapped by this SAM class.

        for ( iMappedAttr = 0; iMappedAttr < cAttributeMap; iMappedAttr++ )
        {
            if (( pAttrMod->AttrInf.attrTyp ==
                        rAttributeMap[iMappedAttr].DsAttributeId ) &&
                (NonSamWriteAllowed!=rAttributeMap[iMappedAttr].writeRule))
            {

                //
                // We only treat ATT_USER_PASSWORD as a loopback arg
                // if the heuristic gfUserPasswordSupport is true
                //

                if ( !((pAttrMod->AttrInf.attrTyp == ATT_USER_PASSWORD) &&
                    !fUserPasswordSupport) ) {

                    // This is a mapped attribute.
    
                    rCallMap[i].fSamWriteRequired = TRUE;
                    rCallMap[i].iAttr = iMappedAttr;
    
                    // Add in any new access rights required.
    
                    *pDomainModifyRightsRequired |=
                            rAttributeMap[iMappedAttr].domainModifyRightsRequired;
                    *pObjectModifyRightsRequired |=
                            rAttributeMap[iMappedAttr].objectModifyRightsRequired;

                }

                break;
            }
        }

        pAttrMod = pAttrMod->pNextMod;
    }

    // Assign return values.

    *pcCallMap = cCallMap;
    *prCallMap = rCallMap;
}

BOOL
SampExistsDsLoopback(
    DSNAME  **ppLoopbackName OPTIONAL
    )

/*++

Routine Description:

    Determines if this thread in SAM is part of a loopback operation,
    and if so, returns the DN of the looped back object.

Arguments:

    ppLoopbackName - pointer to pointer to DSNAME which represents the
        looped back object DN.

Return Value:

    TRUE if loopback case, false otherwise.

--*/

{
    THSTATE *pTHS=pTHStls;
    SAMP_LOOPBACK_ARG *pSamLoopback;

    if ( SampExistsDsTransaction() &&
         (NULL != pTHS->pSamLoopback) )
    {
        pSamLoopback = (SAMP_LOOPBACK_ARG *) pTHS->pSamLoopback;

        Assert(NULL != pSamLoopback->pObject);

        if ( ARGUMENT_PRESENT( ppLoopbackName ) )
        {
            *ppLoopbackName = pSamLoopback->pObject;
        }

        return(TRUE);
    }

    return(FALSE);
}

VOID
SampMapSamLoopbackError(
    NTSTATUS status
    )

/*++

Routine Description:

    Calls looped back through SAM may return an error.  This error
    may have originated in SAM or in the DS.  In the latter (DS) case,
    pTHStls->errCode is already set and there is nothing more to do.
    In the SAM case, we need to generate a DS error.

Arguments:

    status - an NTSTATUS returned by SAM.

Return Value:

    None

--*/

{
    THSTATE *pTHS=pTHStls;

    if ( NT_SUCCESS(status) )
    {
        // No SAM error.  We still should clear pTHStls->errCode
        // because the DS may have returned a missing attribute
        // error for a group membership operation (for example)
        // which set pTHStls->errCode but which SAM treated as
        // a successful empty membership.

        pTHS->errCode = 0;
    }
    else if ( 0 != pTHS->errCode )
    {
        // Error originated in DS - nothing to do as pTHStls->errCode
        // is already set.

        NULL;
    }
    else
    {
        // Error originated SAM - map it as best we can.

        switch ( status )
        {
        case STATUS_ACCESS_DENIED:
        case STATUS_INVALID_DOMAIN_ROLE:
        case STATUS_ACCOUNT_RESTRICTION:
        case STATUS_INVALID_WORKSTATION:
        case STATUS_INVALID_LOGON_HOURS:
        case STATUS_PRIVILEGE_NOT_HELD:

            SetSecError(
                SE_PROBLEM_INSUFF_ACCESS_RIGHTS,
                RtlNtStatusToDosError(status));
            break;

        case STATUS_ALIAS_EXISTS:
        case STATUS_GROUP_EXISTS:
        case STATUS_USER_EXISTS:
        case STATUS_MEMBER_IN_GROUP:
        case STATUS_MEMBER_IN_ALIAS:

            SetUpdError(
                UP_PROBLEM_ENTRY_EXISTS,
                RtlNtStatusToDosError(status));
            break;

        case STATUS_BUFFER_OVERFLOW:
        case STATUS_BUFFER_TOO_SMALL:
        case STATUS_INSUFFICIENT_RESOURCES:
        case STATUS_NO_MEMORY:

            SetSysError(
                ENOMEM,
                RtlNtStatusToDosError(status));
            break;

        case STATUS_DISK_FULL:
            SetSysError(
                ENOSPC,
                RtlNtStatusToDosError(status));
            break;

        case STATUS_DS_BUSY:

            SetSvcError(SV_PROBLEM_BUSY,RtlNtStatusToDosError(status));
            break;

        case STATUS_DS_UNAVAILABLE:

            SetSvcError(SV_PROBLEM_UNAVAILABLE,RtlNtStatusToDosError(status));
            break;

        case STATUS_DS_ADMIN_LIMIT_EXCEEDED:

            SetSvcError(SV_PROBLEM_ADMIN_LIMIT_EXCEEDED,DS_ERR_ADMIN_LIMIT_EXCEEDED);
            break;

        case STATUS_NO_SUCH_ALIAS:
        case STATUS_NO_SUCH_DOMAIN:
        case STATUS_NO_SUCH_GROUP:
        case STATUS_NO_SUCH_MEMBER:
        case STATUS_NO_SUCH_USER:
        case STATUS_OBJECT_NAME_NOT_FOUND:

            // Could pass pTHS->pSamLoopback->pObject as 2nd parameter
            // but no guarantee that this is the name which failed.  I.e.
            // failing name could have been a member being added/removed.
            // So use NULL since SetNamError() is coded to accept it.

            SetNamError(
                NA_PROBLEM_NO_OBJECT,
                NULL,
                RtlNtStatusToDosError(status));
            break;

        case STATUS_OBJECT_NAME_INVALID:
             SetNamError(
                NA_PROBLEM_BAD_NAME,
                NULL,
                RtlNtStatusToDosError(status));
            break;


        case STATUS_DS_OBJ_CLASS_VIOLATION:
             SetUpdError(
                 UP_PROBLEM_OBJ_CLASS_VIOLATION,
                 RtlNtStatusToDosError(status));
             break;
        case STATUS_DS_CANT_ON_NON_LEAF:
             SetUpdError(
                 UP_PROBLEM_CANT_ON_NON_LEAF,
                 RtlNtStatusToDosError(status));

             break;
        case STATUS_DS_CANT_MOD_OBJ_CLASS:
             SetUpdError(
                 UP_PROBLEM_CANT_MOD_OBJ_CLASS,
                 RtlNtStatusToDosError(status));
             break;

        case STATUS_INVALID_DOMAIN_STATE:
        case STATUS_INVALID_SERVER_STATE:
        case STATUS_CANNOT_IMPERSONATE:

            SetSysError(
                EBUSY,
                RtlNtStatusToDosError(status));
            break;

        case STATUS_BAD_DESCRIPTOR_FORMAT:
        case STATUS_DATA_ERROR:
        case STATUS_ILL_FORMED_PASSWORD:
        case STATUS_INVALID_ACCOUNT_NAME:
        case STATUS_INVALID_HANDLE:
        case STATUS_INVALID_ID_AUTHORITY:
        case STATUS_INVALID_INFO_CLASS:
        case STATUS_INVALID_OWNER:
        case STATUS_INVALID_PARAMETER:
        case STATUS_INVALID_SID:
        case STATUS_NO_MORE_ENTRIES:
        case STATUS_OBJECT_TYPE_MISMATCH:
        case STATUS_SPECIAL_ACCOUNT:
        case STATUS_INVALID_MEMBER:

            SetSysError(
                EINVAL,
                RtlNtStatusToDosError(status));
            break;

        case STATUS_UNSUCCESSFUL:
        case STATUS_NOT_IMPLEMENTED:
        case STATUS_INTERNAL_ERROR:
        default:

            SetSvcError(
                SV_PROBLEM_WILL_NOT_PERFORM,
                RtlNtStatusToDosError(status));
            break;
        }
    }
}

ULONG
SampDeriveMostBasicDsClass(
    ULONG   DerivedClass
    )

/*++

Routine Description:

    Returns the most basic DS class which SAM knows about that
    the presented class is derived from.

Arguments:

    DerivedClass - A CLASS_* value which may or may not be derived
        from a more basic class SAM knows how to handle.

Return Value:

    A valid CLASS_* value.  If no derivation is discovered, this
        is the same as the DerivedClass input value.


--*/

{
    THSTATE     *pTHS=pTHStls;
    CLASSCACHE  *pCC = NULL;
    ULONG       iClass;

    if ( !(pCC = SCGetClassById(pTHS, DerivedClass)) ||
         !SampSamClassReferenced(pCC, &iClass) )
    {
        // We should always be able to look up the class since
        // SAM read it from the DS to begin with.  And it should
        // be a class SAM knows about since SAM is handling the
        // operation.  I.e. If the call originated in SAM it
        // it should know what it is dealing with.  If the call
        // originated in the DS we should only be looping things
        // back which derive from a basic SAM class.
        //
        // No longer true because addsid.c reads the object specified
        // by the user. It can be anything. addsid.c calls this function
        // to decide if the object is or is not an acceptable basic class.
        // So, don't assert. Return the DerivedClass and addsid.c will
        // error off. All of the other callers of this function will
        // assert on their own when they notice DerivedClass is not a
        // most basic SAM class.
        // Assert(!"Should not happen");
        return(DerivedClass);
    }

    return(ClassMappingTable[iClass].DsClassId);
}

VOID
SampSetDsa(
    BOOLEAN DsaFlag
   )
/*++
  Routine Description

     This routine sets/resets the fDSA flag in pTHStls based on the
     passed in DsaFlag. pTHStls->fDSA is used to indicate to the DS
     that this is the DS itself that is doing the operation and therefore
     proceed with the operation without any access checks. This is
     used by in process clients like to SAM to perform certain privileged
     operations.

  Parameters:
     DsaFlag -- pTHS->fDSA is set to this.

  Return Values


    None
--*/
{
    THSTATE *pTHS=pTHStls;

    if (NULL!=pTHS)
        pTHS->fDSA = DsaFlag;
}


VOID
SampSetLsa(
    BOOLEAN LsaFlag
   )
/*++
  Routine Description

     This routine sets/resets the fLSA flag in pTHStls based on the
     passed in DsaFlag. pTHStls->fLSA is used to indicate to the DS
     that this is the LSA itself that is doing the operation and therefore
     proceed with the operation without any access checks. This is
     used by in process clients like to the LSA to perform certain privileged
     operations.

  Parameters:
     LsaFlag -- pTHSTls->fLsa is set to this.

  Return Values


    None
--*/
{
    THSTATE *pTHS=pTHStls;

    if (NULL!=pTHS)
        pTHS->fLsa = LsaFlag;
}

DWORD
SampCheckForDomainMods(
   IN   THSTATE   *pTHS,                    
   IN   DSNAME    *pObject,
   IN   ULONG      cModAtts,
   IN   ATTRTYP   *pModAtts,
   OUT  BOOL      *fIsMixedModeChange,
   OUT  BOOL      *fRoleChange,
   OUT  DOMAIN_SERVER_ROLE *NewRole
   )
/*++

  Routine Description

    This routine determines the new role (BDC or PDC) for the local DC, 
    if the fsmo attribute on the domain object changed.

  Parameters:
                            
    pObject -- the object being modified
    
    cModAttrs -- the count of (replicatable) attributes that were changed                                        
    
    pModAtts  -- the (replicatable) attributes that were changed.

    fIsMixedModeChange -- did the mixed mode change
        
    fRoleChange -- if a role change occured
                                    
    NewRole   -- the new role of the server

  Return Values

    0 on success, !0 on fatal error                  
    
--*/
{
    ULONG   i;
    DWORD   err = 0;
    DSNAME *pRoleOwner = NULL;
    DWORD  ntMixedDomain = 1;
    ULONG   len = 0;

    *fIsMixedModeChange = FALSE;
    *fRoleChange = FALSE;
    *NewRole = DomainServerRoleBackup;

    if ( DsaIsInstalling() ) {
        return 0;
    }

    if (NameMatched(pObject,gAnchor.pDomainDN)) {
        for (i = 0; i < cModAtts; i++) {
            if (pModAtts[i] == ATT_FSMO_ROLE_OWNER) {
                // Read the new value
                err = DBGetAttVal(pTHS->pDB, 
                                  1, 
                                  ATT_FSMO_ROLE_OWNER,
                                  0, 
                                  0, 
                                  &len, 
                                  (UCHAR **) &pRoleOwner);
                if (err) {
                    SetSvcError(SV_PROBLEM_DIR_ERROR,err);
                    goto exit;
                } else {
                    *fRoleChange = TRUE;
                    if (NameMatched(gAnchor.pDSADN, pRoleOwner) ) {
                        *NewRole = DomainServerRolePrimary;
                    } else {
                        *NewRole = DomainServerRoleBackup;
                    }
                    THFreeEx(pTHS, pRoleOwner);
                }
            } else if (pModAtts[i] == ATT_NT_MIXED_DOMAIN) {


                err = DBGetSingleValue(pTHS->pDB,
                                       ATT_NT_MIXED_DOMAIN,
                                       &ntMixedDomain,
                                       sizeof(ntMixedDomain),
                                       NULL);
                if (err) {
                    SetSvcError(SV_PROBLEM_DIR_ERROR,err);
                    goto exit;
                }
                if (ntMixedDomain == 0) {
                    *fIsMixedModeChange = TRUE;
                }

            }
        }
    }

exit:

    return err;
}

VOID
SampAuditFailedAbortTransaction( 
    IN ULONG DsaException,
    IN DWORD Error,
    IN ULONG Location
    )
/*++

    Routine Description:

       This routine throws DsaException to cause a transaction to fail and
       roll back when a required audit can not be written for some reason.
        
       This wrapper for DsaException exists to encapsulate the audit failure
       behavior for SAM in DS mode.  Future plans currently include an LSA
       exported handler for failed audits that will honor crash on audit
       failure configurations for the auditing component.  This routine will
       serve as a single source for update such a mechanism or change be
       required.

    Parameters:

       DsaException - The exception to throw.
       
       Error - The error code associated with the exception.            
            
       Location - This ULONG is encoded file and line information for
                  DsaException.  Typically, DSID(FILENO, __LINE__).
                      
    Return Value

       None.                                                    

--*/
{
    RaiseDsaExcept(DsaException, 
                   Error, 
                   0, 
                   Location, 
                   DS_EVENT_SEV_MINIMAL
                   );   
    
}    


//
// Data types of attributes audited 
//
typedef enum _AUDIT_ATTR_DATATYPE {
    
    AuditDatatypeString = 0,
    AuditDatatypeMultivaluedString,
    AuditDatatypeUlong,
    AuditDatatypeLargeInteger,
    AuditDatatypeDeltaTime,
    AuditDatatypeFileTime,
    AuditDatatypeBitfield,
    AuditDatatypeSid,
    AuditDatatypeSidList,
    AuditDatatypeLogonHours,
    AuditDatatypeSecret,
    AuditDatatypeUndefined
    
} AUDIT_ATTR_DATATYPE;


//
// A table for each object type maps the ATTRTYP to the correct offset in
// the object specific structure
//    
                   

//
// Audit attribute information lookup table entry
//
typedef struct _SAMP_AUDIT_ATTR_INFO {
    
    ATTRTYP AttrId;
    AUDIT_ATTR_DATATYPE AttributeDataType;
    ULONG AttrFieldOffset;
        
} SAMP_AUDIT_ATTR_INFO;


//
// Table of attribute offset information for SAMP_AUDIT_GROUP_ATTR_VALUES
//
SAMP_AUDIT_ATTR_INFO SampAuditUserAttributeInfo[] = {
    
    { ATT_SAM_ACCOUNT_NAME,
      AuditDatatypeString,
      LSAP_FIELD_PTR(LSAP_AUDIT_USER_ATTR_VALUES, SamAccountName) },
    
    { ATT_DISPLAY_NAME,
      AuditDatatypeString,
      LSAP_FIELD_PTR(LSAP_AUDIT_USER_ATTR_VALUES, DisplayName) },
      
    { ATT_USER_PRINCIPAL_NAME,
      AuditDatatypeString,
      LSAP_FIELD_PTR(LSAP_AUDIT_USER_ATTR_VALUES, UserPrincipalName) },
    
    { ATT_HOME_DIRECTORY,
      AuditDatatypeString,
      LSAP_FIELD_PTR(LSAP_AUDIT_USER_ATTR_VALUES, HomeDirectory) },

    { ATT_HOME_DRIVE,
      AuditDatatypeString,
      LSAP_FIELD_PTR(LSAP_AUDIT_USER_ATTR_VALUES, HomeDrive) },

    { ATT_SCRIPT_PATH,
      AuditDatatypeString,
      LSAP_FIELD_PTR(LSAP_AUDIT_USER_ATTR_VALUES, ScriptPath) },

    { ATT_PROFILE_PATH,
      AuditDatatypeString,
      LSAP_FIELD_PTR(LSAP_AUDIT_USER_ATTR_VALUES, ProfilePath) },

    { ATT_USER_WORKSTATIONS,
      AuditDatatypeString,
      LSAP_FIELD_PTR(LSAP_AUDIT_USER_ATTR_VALUES, UserWorkStations) },

    { ATT_PWD_LAST_SET,
      AuditDatatypeFileTime,
      LSAP_FIELD_PTR(LSAP_AUDIT_USER_ATTR_VALUES, PasswordLastSet) },

    { ATT_ACCOUNT_EXPIRES,
      AuditDatatypeFileTime,
      LSAP_FIELD_PTR(LSAP_AUDIT_USER_ATTR_VALUES, AccountExpires) },

    { ATT_PRIMARY_GROUP_ID,
      AuditDatatypeUlong,
      LSAP_FIELD_PTR(LSAP_AUDIT_USER_ATTR_VALUES, PrimaryGroupId) },
    
    { ATT_MS_DS_ALLOWED_TO_DELEGATE_TO,
      AuditDatatypeMultivaluedString,
      LSAP_FIELD_PTR(LSAP_AUDIT_USER_ATTR_VALUES, AllowedToDelegateTo) },

    { ATT_USER_ACCOUNT_CONTROL,
      AuditDatatypeUlong,
      LSAP_FIELD_PTR(LSAP_AUDIT_USER_ATTR_VALUES, UserAccountControl) },
    
    { ATT_USER_PARAMETERS,
      AuditDatatypeSecret,                         
      LSAP_FIELD_PTR(LSAP_AUDIT_USER_ATTR_VALUES, UserParameters) },
    
    { ATT_SID_HISTORY,
      AuditDatatypeSidList,
      LSAP_FIELD_PTR(LSAP_AUDIT_USER_ATTR_VALUES, SidHistory) },

    { ATT_LOGON_HOURS,
      AuditDatatypeLogonHours,
      LSAP_FIELD_PTR(LSAP_AUDIT_USER_ATTR_VALUES, LogonHours) },
     
    { ATT_DNS_HOST_NAME,
      AuditDatatypeString,
      LSAP_FIELD_PTR(LSAP_AUDIT_USER_ATTR_VALUES, DnsHostName) },
    
    { ATT_SERVICE_PRINCIPAL_NAME,
      AuditDatatypeMultivaluedString,
      LSAP_FIELD_PTR(LSAP_AUDIT_USER_ATTR_VALUES, ServicePrincipalNames) }
    
};

ULONG cSampAuditUserAttributeInfo =
    sizeof(SampAuditUserAttributeInfo) /
        sizeof(SAMP_AUDIT_ATTR_INFO);


//
// Table of attribute offset information for SAMP_AUDIT_GROUP_ATTR_VALUES
//
SAMP_AUDIT_ATTR_INFO SampAuditGroupAttributeInfo[] = {
    
    { ATT_SAM_ACCOUNT_NAME,
      AuditDatatypeString,
      LSAP_FIELD_PTR(LSAP_AUDIT_GROUP_ATTR_VALUES, SamAccountName) },
    
    { ATT_SID_HISTORY,
      AuditDatatypeSidList,
      LSAP_FIELD_PTR(LSAP_AUDIT_GROUP_ATTR_VALUES, SidHistory) }
      
};     

ULONG cSampAuditGroupAttributeInfo =
    sizeof(SampAuditGroupAttributeInfo) /
        sizeof(SAMP_AUDIT_ATTR_INFO);
                        

//
// Table of attribute offset information for SAMP_AUDIT_DOMAIN_ATTR_VALUES
//
SAMP_AUDIT_ATTR_INFO SampAuditDomainAttributeInfo[] = {
    
    { ATT_MIN_PWD_AGE,
      AuditDatatypeDeltaTime,
      LSAP_FIELD_PTR(LSAP_AUDIT_DOMAIN_ATTR_VALUES, MinPasswordAge) },
    
    { ATT_MAX_PWD_AGE,
      AuditDatatypeDeltaTime,
      LSAP_FIELD_PTR(LSAP_AUDIT_DOMAIN_ATTR_VALUES, MaxPasswordAge) },
      
    { ATT_FORCE_LOGOFF,
      AuditDatatypeDeltaTime,
      LSAP_FIELD_PTR(LSAP_AUDIT_DOMAIN_ATTR_VALUES, ForceLogoff) },
    
    { ATT_LOCKOUT_THRESHOLD,
      AuditDatatypeUlong,
      LSAP_FIELD_PTR(LSAP_AUDIT_DOMAIN_ATTR_VALUES, LockoutThreshold) },
    
    { ATT_LOCK_OUT_OBSERVATION_WINDOW,
      AuditDatatypeDeltaTime,
      LSAP_FIELD_PTR(LSAP_AUDIT_DOMAIN_ATTR_VALUES, LockoutObservationWindow) },
      
    { ATT_LOCKOUT_DURATION,
      AuditDatatypeDeltaTime,
      LSAP_FIELD_PTR(LSAP_AUDIT_DOMAIN_ATTR_VALUES, LockoutDuration) },
    
    { ATT_PWD_PROPERTIES,
      AuditDatatypeUlong,
      LSAP_FIELD_PTR(LSAP_AUDIT_DOMAIN_ATTR_VALUES, PasswordProperties) },
    
    { ATT_MIN_PWD_LENGTH,
      AuditDatatypeUlong,
      LSAP_FIELD_PTR(LSAP_AUDIT_DOMAIN_ATTR_VALUES, MinPasswordLength) },
      
    { ATT_PWD_HISTORY_LENGTH,
      AuditDatatypeUlong,
      LSAP_FIELD_PTR(LSAP_AUDIT_DOMAIN_ATTR_VALUES, PasswordHistoryLength) },
    
    { ATT_MS_DS_MACHINE_ACCOUNT_QUOTA,
      AuditDatatypeUlong,
      LSAP_FIELD_PTR(LSAP_AUDIT_DOMAIN_ATTR_VALUES, MachineAccountQuota) },
    
    { ATT_NT_MIXED_DOMAIN,
      AuditDatatypeUlong,
      LSAP_FIELD_PTR(LSAP_AUDIT_DOMAIN_ATTR_VALUES, MixedDomainMode) },
      
    { ATT_MS_DS_BEHAVIOR_VERSION,
      AuditDatatypeUlong,
      LSAP_FIELD_PTR(LSAP_AUDIT_DOMAIN_ATTR_VALUES, DomainBehaviorVersion) },
    
    { ATT_OEM_INFORMATION,
      AuditDatatypeString,
      LSAP_FIELD_PTR(LSAP_AUDIT_DOMAIN_ATTR_VALUES, OemInformation) }

};

ULONG cSampAuditDomainAttributeInfo =
    sizeof(SampAuditDomainAttributeInfo) /
        sizeof(SAMP_AUDIT_ATTR_INFO);


//
// Used by qsort to sort ATTCACHE array for efficient search
//
extern
int __cdecl
CmpACByAttType(
    const void * keyval, 
    const void * datum
    );


int __cdecl
CmpByAttType(
    IN const void * keyval, 
    IN const void * datum
    )
/*++

    Routine Description:

        A simple function to be used with qsort to sort ATTRTYP. 
        
    Parameters:
    
       keyval - ATTRTYPE left
       
       datum - ATTRTYP right
        
    Return Value

       < 0 - Less than
       0   - equal
       > 0 - Greater than                                                     

--*/

{
    ATTRTYP *ppAttrTypKey = (ATTRTYP*)keyval;
    ATTRTYP *ppAttrTypDatum = (ATTRTYP*)datum;

    return (*ppAttrTypKey - *ppAttrTypDatum);
} 
        

VOID
SampAuditGetChangeInfo(
    IN SAMP_OBJECT_TYPE ObjectType,
    IN ULONG cModAtts,
    IN ATTRTYP *pModAtts,
    IN OUT PVOID *NewValueInfo, OPTIONAL
    IN ULONG cAttributeMappingTable,
    IN SAMP_ATTRIBUTE_MAPPING *pAttributeMappingTable
    )
/*++

    Routine Description:

       This routine gathers new value information for all audited attributes
       that were modified as part of the transaction.
       
       1) Examine transaction metadata and determine which attribute changes 
       need to be audited.  
       
       2) The new values are read from the database.
        
       3) An ObjectType specific structure is allocated and then referenced
       through a generic pointer.
       
       4) Table lookups determine an offset into the structure for storing
       a pointer to the attribute value and the type of change made.
       
       NewValueInfo points the newly allocated structure upon return.  The
       memory for this structure as well as the attribute values come from
       the thread's heap and will be be freed then the THSTATE is cleaned up.
       
       This routine can throw exceptions, THAllocEx is used.

    Parameters:
    
       ObjectType - The type of the SAM object

       cModAtts - Count of attributes in the metadata vector
        
       pModAtts - The metadata vector attribute types
        
       NewValueInfo - PVOID pointing to an object speficic structure that
                      holds the new values for all modified attributes.  If 
                      this points to a non-NULL value then the pointer is
                      assumed valid and the existing NewValueInfo structure
                      will be updated.
                             
       cAttributeMappingTable - Count of items in pAttributeMappingTable
        
       pAttributeMappingTable - SAM attribute mapping table for the object
                                type being modified in this transaction.
        
    Return Value

       Nothing.
       
       Exception is throw on failure.                                                     

--*/
{    
    DWORD dwError = ERROR_SUCCESS;
    THSTATE *pTHS = pTHStls;
    ATTRVAL       *pAV = NULL;
    ATTRVALBLOCK  *pAValBlock = NULL;
    ULONG NextModIndex = 0;
    ULONG i, j, k;
    ULONG CurrentReturnedAttr = 0;
    ULONG cAttrInfo = 0;
    BOOL AuditedAttributesWereChanged = FALSE;
    BOOL AttributeFound = FALSE;
    SAMP_AUDIT_ATTR_INFO *AttrInfo = NULL;
    PLSA_ADT_STRING_LIST StringList = NULL;
    UINT_PTR Value = 0;
    PUINT_PTR ValueOffset = NULL;
    PLSA_ADT_SID_LIST SidList = NULL;
    USHORT Length = 0;
    PUNICODE_STRING String = NULL;
    BOOLEAN fAttrNoValue = FALSE;
    ULONG AttrCount = 0;
    ATTR *Attrs = NULL;
    ATTRTYP *AttrTypes = NULL;
    ATTCACHE **AttCache = NULL;
    ULONG AttrCountReturned = 0;
    NTSTATUS NtStatus = STATUS_SUCCESS;
    
    //
    // Allocate memory for the request block, it's based on all attributes
    // so there may be some extra space but we don't need to make multiple
    // passes through the array to save a small amount of memory that will
    // be freed shortly.  We do not free this memory in this routine
    // as it is referenced by the notification and will be needed for the audit.
    // It will be freed when the thread state is cleaned up.
    //     
    AttrTypes = (ATTRTYP*)THAllocEx(pTHS, sizeof(ATTRTYP) * cModAtts );
    
    //
    // Initialize the attribute request block with metadata attribute type
    // info - only including modified attributes that we audit.
    //
    for(i = 0; i < cModAtts; i++)
    { 
        //
        // Determine if the current attribute is audited
        //   
        for (j = 0; j < cAttributeMappingTable; j++) {
                
            if (pModAtts[i] == pAttributeMappingTable[j].DsAttributeId &&
                ((SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES) & 
                 pAttributeMappingTable[j].AuditTypeMask)) {
                
                AuditedAttributesWereChanged = TRUE;
                AttrTypes[AttrCount++] = pModAtts[i]; 
            }
        }
    } 
    
    //
    // Ensure the list of audited attributes is sorted as well as the
    // list of returned values.  This way we can walk the list and notice
    // any "holes" or values not returned from the search and set
    // LsapAuditSamAttrNoValue appropriately.
    //
    qsort(AttrTypes, AttrCount, sizeof(AttrTypes[0]), CmpByAttType); 
               
    AttCache = (ATTCACHE**)THAllocEx(pTHS, AttrCount * sizeof(ATTCACHE*));
    
    //
    // Populate the ATTCACHE list from the ATTRTYP list
    //
    for (i = 0; i < AttrCount; i++) {
        
        AttCache[i] = SCGetAttById(pTHS, AttrTypes[i]);
    }
    
    qsort(AttCache, AttrCount, sizeof(AttCache[0]), CmpACByAttType);
    
    //
    // Do the DB read
    //
    // We do not free the result block memory in this routine as it is 
    // referenced by the notification and will be needed for the audit.
    // It will be freed when the thread state is cleaned up.
    // 
    dwError = DBGetMultipleAtts(
                  pTHS->pDB,
                  AttrCount,
                  AttCache,
                  NULL,       
                  NULL,
                  &AttrCountReturned,
                  &Attrs,
                  DBGETMULTIPLEATTS_fEXTERNAL,
                  0
                  );    
    
    //
    // A failure to read is likely a resource constraint as our database  
    // view is consistent with the metadata vector supplied.
    //     
    if (dwError)
    {   
        //
        // We must except because we can not audit completely.  This will role
        // back the transaction.
        //
        SampAuditFailedAbortTransaction(
                DSA_DB_EXCEPTION,
                dwError,
                DSID(FILENO, __LINE__)
                ); 
    }
    THClearErrors();
   
    //
    // Based on the type of object we need to create and initialize the correct
    // structure with all of the values changed as part of this transaction.
    // If NewValueInfo is non-NULL it is assumed a valid structure which is
    // simply updated preserving anything that isn't overwritten explicitly.
    //
    switch (ObjectType) {
        
        case SampUserObjectType:
            
            if (NULL == *NewValueInfo) {
                
                *NewValueInfo = THAllocEx(
                                pTHS,
                                sizeof(LSAP_AUDIT_USER_ATTR_VALUES)
                                );
            }   
            
            AttrInfo = SampAuditUserAttributeInfo;
            cAttrInfo = cSampAuditUserAttributeInfo;
            
            break;
            
        case SampGroupObjectType:
        case SampAliasObjectType: 
            
            if (NULL == *NewValueInfo) {

                *NewValueInfo = THAllocEx(
                                    pTHS,
                                    sizeof(LSAP_AUDIT_GROUP_ATTR_VALUES)
                                    );
            }
            
            AttrInfo = SampAuditGroupAttributeInfo;
            cAttrInfo = cSampAuditGroupAttributeInfo;
                                                           
            break;
            
        case SampDomainObjectType: 

            if (NULL == *NewValueInfo) {
                
                *NewValueInfo = THAllocEx(
                                    pTHS,
                                    sizeof(LSAP_AUDIT_DOMAIN_ATTR_VALUES)
                                    );
            }
            
            AttrInfo = SampAuditDomainAttributeInfo;
            cAttrInfo = cSampAuditDomainAttributeInfo;

            break;
            
        default:
            //
            // If the object type is unknown in producation we had best 
            // quietly fail the audit.
            //
            Assert(FALSE && 
                   "Attempted to collect audit values for unsupported object");
            goto Cleanup;
    }                                  
                                                                                 
    //
    // Process every modified attribute
    //
    for (i = 0; i < AttrCount; i++) {
        
        AttributeFound = FALSE;
        
        //
        // Lookup the attribute's offset into it's value structure.
        //
        // Note: Because the search vector and result vector are sorted this
        // lookup could be made more efficient by defining the table in 
        // sorted order by ATTRTYP and always resuming the search from the
        // next index from where the previous attribute was found.
        //
        for (j = 0; j < cAttrInfo; j++) {
            
            if (AttrTypes[i] == AttrInfo[j].AttrId) {
                
                AttributeFound = TRUE; 
                fAttrNoValue = FALSE;                         
                Value = 0;
                
                //
                // Compute a pointer to the relevant field in the structure
                //
                ValueOffset = (PUINT_PTR)((UINT_PTR)(*NewValueInfo) +
                                (AttrInfo[j].AttrFieldOffset * 
                                 sizeof(UINT_PTR)));             
                
                //
                // Attributes not returned from the search are implicitly 
                // deleted
                //
                if (CurrentReturnedAttr >= AttrCountReturned ||
                    Attrs[CurrentReturnedAttr].attrTyp != AttrTypes[i]) {
                    
                    fAttrNoValue = TRUE;   
                
                } else { 
                    
                    pAValBlock = &Attrs[CurrentReturnedAttr].AttrVal;
                    
                    //
                    // This attribute was returned, index the next returned
                    // attribute if there are any more.
                    //
                    if (CurrentReturnedAttr <= AttrCountReturned) {
                        CurrentReturnedAttr++;
                    }
                    
                    //
                    // Collect the value depending on its type
                    //
                    switch (AttrInfo[j].AttributeDataType) {
                        
                    //
                    // Simple single valued entries
                    //
                    case AuditDatatypeUlong:
                    case AuditDatatypeLargeInteger:
                    case AuditDatatypeDeltaTime:
                    case AuditDatatypeFileTime:
                    case AuditDatatypeSid:
                    case AuditDatatypeLogonHours: 
                        
                        Assert(pAValBlock->valCount == 1);
                              
                        //
                        // If this is the UAC we need to convert the 
                        // LM flags to SAM UAC bits to match the
                        // representation in PrevAccountControl
                        //
                        if (ATT_USER_ACCOUNT_CONTROL == AttrInfo[j].AttrId) {
                            
                            ULONG *UserAccountControl = THAllocEx(
                                                            pTHS,
                                                            sizeof(ULONG)
                                                            );

                            //
                            // Ignore the status, the flags came from the DB
                            // and therefore have already been validated.  
                            //
                            SampFlagsToAccountControl(
                                *((PULONG)(pAValBlock->pAVal[0].pVal)),           
                                UserAccountControl
                                );
                            
                            Value = (UINT_PTR)UserAccountControl;
                            
                        } else {
                            
                            Value = (UINT_PTR)pAValBlock->pAVal[0].pVal;    
                        }   
                        
                        break;
                        
                    //
                    // Secret data we only indicate a change, no value.
                    //
                    case AuditDatatypeSecret:
                            
                            Assert(pAValBlock->valCount == 1);
                            
                            break;
                        
                    //
                    // Single valued string requires UNICODE_STRING construct
                    //     
                    case AuditDatatypeString:               
                            
                        Assert(pAValBlock->valCount == 1);
                                
                        //
                        // Allocate the UNICODE_STRING
                        //
                        String = (PUNICODE_STRING)THAllocEx(
                                      pTHS,
                                      sizeof(UNICODE_STRING)
                                      );
                        
                        //
                        // Truncate the buffer to prevent overflow
                        //
                        if (pAValBlock->pAVal[0].valLen > MAXUSHORT) {
                            
                            Length = MAXUSHORT;    
                        } else {
                            
                            Length = (USHORT)pAValBlock->pAVal[0].valLen;     
                        }
                        
                        String->Length = Length;
                        String->MaximumLength = Length;
                        String->Buffer = (PWSTR)pAValBlock->pAVal[0].pVal;
                                        
                        Value = (UINT_PTR)String;
                        
                        break;
                        
                    //
                    // Multivalued string 
                    //
                    case AuditDatatypeMultivaluedString:
                            
                        Assert(pAValBlock->valCount >= 1);
                        
                        //
                        // Allocate and initialize the STRING_LIST
                        //
                        StringList = (PLSA_ADT_STRING_LIST)THAllocEx(
                                         pTHS,
                                         sizeof(LSA_ADT_STRING_LIST)
                                         );
                                        
                        StringList->cStrings = pAValBlock->valCount;
                        
                        //
                        // Allocate enough memory to store each string as a 
                        // UNICODE_STRING
                        //
                        StringList->Strings = (PLSA_ADT_STRING_LIST_ENTRY)THAllocEx(
                                                  pTHS,
                                                  StringList->cStrings * sizeof(LSA_ADT_STRING_LIST_ENTRY)
                                                  );
                        
                        //
                        // Initialize every UNICODE_STRING
                        //
                        for (k = 0; k < StringList->cStrings; k++) {
                            
                            //
                            // Truncate the buffer to prevent overflow
                            //
                            if (pAValBlock->pAVal[k].valLen > MAXUSHORT) {
                                
                                Length = MAXUSHORT;    
                            } else {
                                
                                Length = (USHORT)pAValBlock->pAVal[k].valLen;     
                            }

                            StringList->Strings[k].Flags = 0;
                            StringList->Strings[k].String.Length = Length;
                            StringList->Strings[k].String.MaximumLength = Length;
                            StringList->Strings[k].String.Buffer = (PWSTR)pAValBlock->pAVal[k].pVal;
                        }
                        
                        Value = (UINT_PTR)StringList;
                        
                        break;
                        
                    //
                    // Sid list
                    //
                    case AuditDatatypeSidList:
                            
                        Assert(pAValBlock->valCount >= 1);
                        
                        SidList = (PLSA_ADT_SID_LIST)THAllocEx(
                                      pTHS,
                                      sizeof(LSA_ADT_SID_LIST)
                                      );
                        
                        SidList->cSids = pAValBlock->valCount;
                        
                        //
                        // Allocate enough memory to store each PLSA_ADT_SID_LIST_ENTRY
                        //
                        SidList->Sids = (PLSA_ADT_SID_LIST_ENTRY)THAllocEx(
                                            pTHS,
                                            SidList->cSids * sizeof(LSA_ADT_SID_LIST_ENTRY)
                                            );
                        
                        //
                        // Initialize every PLSA_ADT_SID_LIST_ENTRY
                        //
                        for (k = 0; k < SidList->cSids; k++) {
                            
                            SidList->Sids[k].Flags = 0;
                            SidList->Sids[k].Sid = (PSID)pAValBlock->pAVal[k].pVal;
                        }   
                         
                        Value = (UINT_PTR)SidList;
                        
                        break;
                        
                    //
                    // Report and/or handle entropy
                    //
                    default:
                                
                        //
                        // This could happen only if an attribute category is
                        // added to the AttrInfo table but this switch isn't 
                        // updated to implement the type category.
                        //
                        Assert(FALSE && "Unknown attribute category!");
                    }                
                }
                                    
                //
                // Copy the pointer to the value into this field.
                //
                *ValueOffset = Value;  
                
                //
                // Set the value information flag, by default these are all
                // LsapAuditSamAttrUnchanged
                //
                switch (ObjectType) {
                    
                    case SampUserObjectType: 
                        //
                        // Secrets are tagged so they are never displayed.
                        //
                        if (AuditDatatypeSecret == AttrInfo[j].AttributeDataType) {
                            ((PLSAP_AUDIT_USER_ATTR_VALUES)
                                *NewValueInfo)->AttrDeltaType[AttrInfo[j].AttrFieldOffset] 
                                    = LsapAuditSamAttrSecret;
                                
                        } else {
                            ((PLSAP_AUDIT_USER_ATTR_VALUES)
                                *NewValueInfo)->AttrDeltaType[AttrInfo[j].AttrFieldOffset] =
                                    fAttrNoValue ? LsapAuditSamAttrNoValue :
                                                   LsapAuditSamAttrNewValue;        
                        }
                        
                        break;
                        
                    case SampGroupObjectType:
                    case SampAliasObjectType:
                        ((PLSAP_AUDIT_GROUP_ATTR_VALUES)
                         *NewValueInfo)->AttrDeltaType[AttrInfo[j].AttrFieldOffset] =
                            fAttrNoValue ? LsapAuditSamAttrNoValue :
                                           LsapAuditSamAttrNewValue;
                        break;
                        
                    case SampDomainObjectType:
                        ((PLSAP_AUDIT_DOMAIN_ATTR_VALUES)
                         *NewValueInfo)->AttrDeltaType[AttrInfo[j].AttrFieldOffset] =
                            fAttrNoValue ? LsapAuditSamAttrNoValue :
                                           LsapAuditSamAttrNewValue;
                        break;
                        
                    default:
                        //
                        // This will not happen as we've already bailed if the
                        // object type was unkown.
                        //
                        Assert(FALSE && "Unknown object type");
                        break;
                }     
            }
            
            //
            // Shortcircuit the search once we've processed the attribute
            //
            if (AttributeFound) {
                break;
            }
        }   
    } 
    
Cleanup:

    return;
    
}   

    
BOOLEAN
SampAuditDetermineRequiredAudits(
    IN SECURITY_DB_DELTA_TYPE DeltaType,
    IN ULONG cModAtts,
    IN ATTRTYP *pModAtts,
    IN ULONG cSamAttributeMap,
    IN SAMP_ATTRIBUTE_MAPPING *rSamAttributeMap,
    OUT ULONG *AuditTypeMask
    )
/*++

    This routine determines if any audits are required for the change defined
    by the DeltaType and meta data vector.  
    
    Bits will be set in AuditTypeMask for each required audit type.
    
    Parameters

        DeltaType - The type of database change.
        
        cModAtts - Count of metadata vector attributes.
        
        pModAtts - Attributes of the metadata vector.
        
        cSamAttributeMap - Count of entries in the mapping table.
        
        rSamAttributeMap - SAM attribute mapping table.
        
        AuditTypeMask - A bit field indicating required audit types.
        
    Return Values

        TRUE - One or more audits are required and specified in the the
               audit mask.
        
        FALSE - An audit is not required.  The audit mask is 
                SAMP_AUDIT_TYPE_NONE.
        
--*/
{   
    ULONG i, j;
    
    *AuditTypeMask = SAMP_AUDIT_TYPE_NONE; 
        
    //
    // Determine what audits to generate based intially on DeltaType
    // If other audit types are introduced/moved from loopback auditing
    // we may want to categorize this determination differently.
    //
    switch (DeltaType) {
        
        case SecurityDbNew:
        case SecurityDbChange:  
            //
            // For create and change operations the metadata is interesting to
            // determine what audits need to written.
            //
            for(i = 0; i < cModAtts; i++)
            {
                //
                // Lookup each attribute in the mapping table
                //
                for (j = 0; j < cSamAttributeMap; j++) {  
                    //
                    // For each audited attribute record the type of audit required
                    // by setting the appropriate bit in the audit type mask and
                    // incrementing the count.  Do not record the same audit type
                    // more than once, and ignore SACL based audits which are
                    // processed elsewhere.
                    //
                    if (pModAtts[i] == rSamAttributeMap[j].DsAttributeId) {
                        
                        *AuditTypeMask |= rSamAttributeMap[j].AuditTypeMask;
                    }
                }
            }    
            
            break;
            
        case SecurityDbDelete:
            
            *AuditTypeMask |= SAMP_AUDIT_TYPE_OBJ_DELETED;
            
            break;
            
            
        default:
            
            //
            // No other audit type supported at this time
            //  
            break;
    }  
    
    //
    // Object access audits are SACL based and not processed by this mechanism.
    // We should remove this bit if it was set above.
    //
    *AuditTypeMask &= (~SAMP_AUDIT_TYPE_OBJ_ACCESS);
    
    //
    // If any bits set then audits are required.  
    //
    return !(SAMP_AUDIT_TYPE_NONE == *AuditTypeMask);
    
}


NTSTATUS
SampAuditAddNotifications(
    IN DSNAME *Object,
    IN ULONG iClass,
    IN ULONG LsaClass,
    IN SECURITY_DB_DELTA_TYPE DeltaType,
    IN ULONG cModAtts,
    IN ATTRTYP *pModAtts
    )
/*++

    Routine Description:

       This routine adds a notification for SAM to generate an audit.
        
       If auditing is not enabled a notification is not added and this
       routine returns immediately.
       
       The audit queue may have more than one notification present during
       a transaction.  Likewise, there are rules for merging incoming 
       notifications with those already queued.  This is necessary because 
       of how loopback can make multiple calls during a creation operation 
       (an initial add of the object and subsequent modifications).  By 
       merging audit notifications from secondary change operations with 
       any previously queued create notifications the result is a single 
       properly ordered audit notification for the object's creation with 
       any and all the new value information. 
       
       SampProcessAuditNotifications is invoked when the transaction completes 
       and it processes all audit notifications by dispatching calls to the 
       auditing interface.
          
       This routine can throw exceptions and is designed to do so if 
       an audit notification should be generated but not succeed.  
       This will force a rollback of the transaction.
       
       Note: This routine assumes that the DS does not support nested 
       transactions for SAM object types.  For this reason the audit 
       notification list is kept on the THSTATE not the DBPOS and the merge
       algorithm doesn't consider the possibility that more than one object
       can be modified in the same (top level) transaction.  That is, all
       operations are considered to occur in the same top level transaction.
       
       See samaudit.c file header for more on the SAM auditing model.
       

    Parameters:
    
       Object    - DSNAME of the object associated with the audit.
       
       iClass    - Object class index into SAM object mapping table.
       
       LsaClass  - Lsa's notion of the object class type
       
       DeltaType - The type of change being made.
                                                     
       cModAtts  - Count of attributes in the metadata vector.
        
       pModAtts  - The metadata vector attribute types.
                      
    Return Value

       STATUS_SUCCESS - NewValueInfoString is valid.
       
       DSA_MEM_EXCEPTION - Thrown if resource constraints prevent a 
                           required notification.                                                     .

--*/

{
    NTSTATUS Status = ERROR_SUCCESS;
    DWORD Err;
    THSTATE *pTHS = pTHStls;
    PVOID NewValueInfo = NULL;
    SAMP_AUDIT_NOTIFICATION *AuditNotification = NULL;
    SAMP_OBJECT_TYPE SamObjectType;
    ULONG cSamAttributeMap;
    SAMP_ATTRIBUTE_MAPPING *rSamAttributeMap;
    ULONG  GroupType = 0;
    PULONG pGroupType = &GroupType;
    ULONG  cbGroupType = 0;
    PSID   Sid = NULL;
    SAMP_AUDIT_NOTIFICATION *pElement;
    PUNICODE_STRING AccountName = NULL;
    ULONG AccountControl = 0;    
    ULONG AuditTypeMask = SAMP_AUDIT_TYPE_NONE;
    SECURITY_DB_DELTA_TYPE EffectiveDeltaType = DeltaType;
    BOOLEAN fIsAuditWithValues = FALSE;
    BOOLEAN fIsNewOrChangeAudit = FALSE;
    BOOLEAN fIsPreCreatedAudit = FALSE;
    BOOLEAN fIsChangeOp = FALSE;
    BOOLEAN fMerging = FALSE;
    
    //
    // Short circuit if auditing isn't enabled or this is a replicated change.
    //
    if (LsaClass != 0 || 
        pTHS->fDRA    || 
        !SampIsAuditingEnabled(0, STATUS_SUCCESS)) {
   
        goto Cleanup;
    }
    
    //
    // Do not audit during DC installation
    //
    if (DsaIsInstalling()) {
        goto Cleanup;
    }
    
    //
    // Collect information from the Class Mapping Table
    //
    SamObjectType = 
        ClassMappingTable[iClass].SamObjectType;
    
    cSamAttributeMap = 
        *ClassMappingTable[iClass].pcSamAttributeMap;
    
    rSamAttributeMap =
        ClassMappingTable[iClass].rSamAttributeMap;
    
    //
    // Server objects have no audits processed by this mechanism at this time.
    //
    if (SampServerObjectType == SamObjectType) {
        
        goto Cleanup;
    } 
                  
    //
    // We don't audit changes to tombstoned objects.
    //
    if (DeltaType != SecurityDbDelete && DBIsObjDeleted(pTHS->pDB)) {
        
        goto Cleanup;
    }
    
    //
    // At this point we must be dealing with a security principal
    //
    
    //
    // The DS Name must have a Sid.  The exception is if this is a new object 
    // creation, in which case, we'll read the Sid from the database.
    // No SAM object audit can be processed without a Sid.
    // 
    if (0 == Object->SidLen) {
        
        ULONG cbSid = 0;

        //
        // New object creations can legally not have a Sid in the DSName yet.
        //
        if (DeltaType == SecurityDbNew) {                   
            //
            // No nesting trx support, if present, an add should always be first. 
            //
            Assert(pTHS->pSamAuditNotificationHead == NULL);
        }
        
        //
        // Do the DB read
        //
        // We do not free the result block memory in this routine as it is 
        // referenced by the notification and will be needed for the audit.
        // It will be freed when the thread state is cleaned up.
        //   
        Err = DBGetAttVal(
                  pTHS->pDB,
                  1,
                  ATT_OBJECT_SID,
                  DBGETATTVAL_fREALLOC,
                  0,
                  &cbSid,
                  (PUCHAR*)&Sid
                  );            
                    
        if (0 != Err) { 
            //
            // If the Sid is not present we may be allowing non-security
            // principal cases into the audit notification logic.
            //
            Assert(DB_ERR_NO_VALUE != Err && 
                   "Security principals must have a Sid");    
            
            //                                                                          
            // We must except because we can not audit properly without the Sid.  
            // This will role back the transaction.
            //
            SampAuditFailedAbortTransaction(
                DSA_DB_EXCEPTION,
                Err,
                DSID(FILENO, __LINE__)
                );                               
        }         
        
    } else {
        
        //
        // Make a working copy of the Sid.  We will modify this Sid when the
        // notification is processed but it doesn't belong to us, it belongs
        // to the owner of the AddArg/ModifyArg associated with the change.
        // Additionally, we can't allocate the memory when we process the audit
        // because if that fails we have no way to roll back the transaction.
        // The other option would be to use static stack storage in the 
        // processing routine.
        //
        Sid = THAllocEx(pTHS, RtlLengthSid((PSID)&Object->Sid));
        
        RtlCopySid(RtlLengthSid((PSID)&Object->Sid), Sid, (PSID)&Object->Sid);
    }
    Assert(Sid != NULL && RtlValidSid(Sid));
    
    //
    // Determine what audit notifications we need to queue or update
    //
    if (!SampAuditDetermineRequiredAudits(
             DeltaType,
             cModAtts,
             pModAtts,
             cSamAttributeMap,
             rSamAttributeMap,
             &AuditTypeMask
             )
        ) {
        //
        // If there are none, our work is through here.
        //
        goto Cleanup;    
    }
    
    //
    // OK, there is no reason we should not queue the audit, lets now
    // obtain all necessary information to perform the audit.
    // 
    
    //
    // All object types but domain require AccountName
    //
    if (SampDomainObjectType != SamObjectType) {
            
        WCHAR *AccountNameBuffer;
        ULONG cbAccountName;

        Err = DBGetAttVal(pTHS->pDB,
                          1,
                          ATT_SAM_ACCOUNT_NAME,
                          DBGETATTVAL_fREALLOC,
                          0,
                          &cbAccountName,
                          (PUCHAR *)&AccountNameBuffer
                          );
        
        if (Err == 0) {
            
            AccountName = THAllocEx(pTHS, sizeof(UNICODE_STRING));
            
            //
            // Memory Allocs in DS take Exceptions. And will cause a rollback.
            // So don't bother checking return from THAllocEx
            //           
            AccountName->Length = (USHORT)cbAccountName;
            AccountName->MaximumLength = (USHORT)cbAccountName;
            AccountName->Buffer = AccountNameBuffer;
            
        } else { 
            //
            // Non domain security principals should always have a 
            // Sam account name.  If not we may be allowing non-security
            // principal cases into the audit notification logic.
            //
            Assert(DB_ERR_NO_VALUE != Err && 
                   "Non-domain security principals must have a Sam account name");
              
            //                                                                          
            // We must except because we can not audit properly without the  
            // account name.  This will role back the transaction.
            //
            SampAuditFailedAbortTransaction(
                DSA_DB_EXCEPTION,
                Err,
                DSID(FILENO, __LINE__)
                ); 
        }
    }
    
    //
    // Obtain required information to perform audits on groups.
    //   
    if (SampGroupObjectType == SamObjectType) {   
        //
        // All group audits require the group type
        //
        Err = DBGetAttVal(
                  pTHS->pDB,
                  1,
                  ATT_GROUP_TYPE,
                  DBGETATTVAL_fCONSTANT,
                  sizeof(ULONG),
                  &cbGroupType,
                  (PUCHAR *)&pGroupType
                  );

        if (0 == Err) {
            
            Assert(sizeof(ULONG)==cbGroupType);

            //
            // If this is a resource group then increment iClass by one
            // so that we point into the alias object mapping table rather
            // than the group object mapping table
            //   
            if (GroupType & GROUP_TYPE_RESOURCE_BEHAVOIR) {
                iClass++;
            }
            
        } else {
            //
            // A group will always have a group type.
            //
            Assert(DB_ERR_NO_VALUE != Err && 
                   "Group security principals must have a group type");
                       
            //                                                                          
            // We must except because we can not audit properly without the  
            // group type.  This will role back the transaction.
            //
            SampAuditFailedAbortTransaction(
                DSA_DB_EXCEPTION,
                Err,
                DSID(FILENO, __LINE__)
                );         
        }
    }
    
    //
    // User / Computer objects require the account control
    //
    if (SampUserObjectType == SamObjectType) {
        
        BOOLEAN fReadAccountControl = FALSE;

        Err = DBGetSingleValue(
                  pTHS->pDB, 
                  ATT_USER_ACCOUNT_CONTROL,  
                  &AccountControl,
                  sizeof(AccountControl), 
                  NULL
                  );
                        
        if (0 == Err) {                
            
            Status = SampFlagsToAccountControl(
                         AccountControl,
                         &AccountControl 
                         );
            
            Assert(NT_SUCCESS(Status));
            
            if (NT_SUCCESS(Status)) {
                fReadAccountControl = TRUE;
            }
        }
        
        if (!fReadAccountControl) {
            //
            // A user/group will always have a user account control.
            //
            Assert(DB_ERR_NO_VALUE != Err && 
                   "User/Computers must have an user account control");

            //                                                                          
            // We must except because we can not audit properly without the  
            // account control.  This will role back the transaction.
            //  
            SampAuditFailedAbortTransaction(
                DSA_DB_EXCEPTION,
                Err,
                DSID(FILENO, __LINE__)
                );    
        }   
    }

#if DBG
    SampAuditValidateNotificationList();
#endif
    
    //
    // All required items that are common to all audit notification types
    // have been collected.  Now process each based on type.
    //
    
    //
    // Create and change audits that might require new value information
    //
    if ((SecurityDbNew == DeltaType || SecurityDbChange == DeltaType) &&
        ((AuditTypeMask & SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_NO_VALUES) ||
         (AuditTypeMask & SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES))) {
         
        AuditNotification = SampAuditFindNotificationToUpdate(Sid);
          
        //
        // If we found a match prepare the notification for merging.
        //
        if (NULL != AuditNotification) {
                
            Assert(RtlEqualSid(Sid, AuditNotification->Sid));
        
            if ((SAMP_AUDIT_TYPE_PRE_CREATED & 
                 AuditNotification->AuditType) != 0) {
                //
                // Clear the pre-created bit as we will shortly
                // initialize this notification.
                //    
                AuditNotification->AuditType &= (~SAMP_AUDIT_TYPE_PRE_CREATED);
            }
            
            //
            // This flag is set if a previous audit notification is found 
            // 
            //
            fMerging = TRUE;
                    
            //
            // Ensure merging a SecurityDbChange with a previous
            // notification of type SecurityDbNew doesn't clobber
            // the change type.
            //
            if (SecurityDbNew == AuditNotification->DeltaType) {
                EffectiveDeltaType = SecurityDbNew;
            }
        }
    
        //
        // If this routine fails we'll rollback the transaction via exception.
        //
        SampAuditInitNotification(
            iClass,
            SamObjectType,
            EffectiveDeltaType,
            Sid,
            AccountName,
            AccountControl,          
            GroupType,
            NULL,
            AuditTypeMask,
            &AuditNotification
            );    
        
        //
        // We've either merged information with an existing notification or
        // created and intialized a new notification by now.
        //
        Assert(AuditNotification);
        
        if (AuditTypeMask & SAMP_AUDIT_TYPE_OBJ_CREATE_OR_CHANGE_WITH_VALUES) {
            //
            // Collect any new value information for the audit  
            //
            switch (SamObjectType) {  
                // 
                // Only these object types supported
                //
                case SampDomainObjectType:
                case SampUserObjectType:
                case SampGroupObjectType:
                case SampAliasObjectType:
                    
                    SampAuditGetChangeInfo(
                        SamObjectType,           
                        cModAtts,
                        pModAtts,
                        &AuditNotification->TypeSpecificInfo,
                        cSamAttributeMap,
                        rSamAttributeMap
                        );  
                    
                    //                                                                          
                    // The above routine will either succeed or throw an
                    // exception to roll back the transaction.
                    //
                    
                    break;
                    
                default:         
                    //
                    // No value information required for this object type
                    //
                    Assert(FALSE && "Invalid object type");
                    break; 
            }
        }
            
        if (!fMerging) {
            SampAuditQueueNotification(AuditNotification);  
        }
        
        AuditNotification = NULL;
    }
    
    //
    // Object deletion audits
    //
    if (SecurityDbDelete == DeltaType &&
        (AuditTypeMask & SAMP_AUDIT_TYPE_OBJ_DELETED)) {
        
        //
        // If this routine fails we'll rollback the transaction via exception.
        //
        SampAuditInitNotification(
            iClass,
            SamObjectType,
            DeltaType,
            Sid,
            AccountName,
            AccountControl,          
            GroupType,
            NULL,
            SAMP_AUDIT_TYPE_OBJ_DELETED,
            &AuditNotification
            );
        
        Assert(AuditNotification);
        
        SampAuditQueueNotification(AuditNotification); 
    }
    
#if DBG
    SampAuditValidateNotificationList();
#endif
        
Cleanup:    
    
    return Status;

}


ULONG
SampAddNetlogonAndLsaNotification(
    DSNAME      * Object,
    ULONG         iClass,
    ULONG         LsaClass,
    SECURITY_DB_DELTA_TYPE  DeltaType,
    BOOL          MixedModeChange,
    BOOL          RoleTransfer,
    DOMAIN_SERVER_ROLE NewRole,
    BOOL          UserAccountControlChanged
    )
/*++

    Routine Description:

       Given the object Name and the class of the object, this routine
       tries to find out if a notification is required and if so will 
       add a notification structure to pTHStls to communicate interesting
       changes to Netlogon and Lsa.

    Parameters:

        Object       -- DS Name of the Object
        iClass       -- Indicates the Object Class
        LsaClass     -- Lsa's notion of the object class type
        DeltaType    -- Indicates the Type of Change
        MixedModeChange -- Indicates the mixed mode state is changing
        RoleTransfer -- Indicates that the Role is changing
        NewRole      -- The New Server Role
        
    Return Value

        pTHS->errCode: 0 succeed
                       Non Zero, service error, DbGetAttVal failed.

--*/
{
    DWORD Err;
    HANDLE Token;
    //
    // This buffer holds both the TOKEN_USER and TOKEN_STATISTICS structures
    BYTE Buffer [ sizeof( NT4SID ) + sizeof( TOKEN_STATISTICS ) ];
    ULONG Size;
    NAMING_CONTEXT *CurrentNamingContext;
    COMMARG CommArg;
    ATTRBLOCK *pObjAttrBlock=NULL;
    BOOLEAN AddLsaNotification = FALSE;
    THSTATE   *pTHS = pTHStls;

    ULONG  GroupType = 0;
    PULONG pGroupType = &GroupType;
    ULONG  cbGroupType = 0;
    

    if (LsaClass == 0)
    {
        BOOL fNotifySam = TRUE;
        PSID  pSid = NULL;
        ULONG cbSid = 0;
        DWORD  dwError;

        //
        // SAM notifications ......
        //

        //
        // Don't bother if we are installing
        //

        if (DsaIsInstalling())
        {
            return(0);
        }

        //
        // Don't bother if we are not running in lsa
        //

        if (!gfRunningInsideLsa)
        {
            return(0);
        }

        //
        // If it is the SAM server object, then no notification is required,
        // just return
        //

        if (SampServerObjectType==ClassMappingTable[iClass].SamObjectType)
        {
            return(0);
        }

        //
        // Ignore domains without a SID. These are NDNC's
        //
        if ( (SampDomainObjectType==ClassMappingTable[iClass].SamObjectType)
          && (0 == Object->SidLen)) {

            return(0);
        }

        //
        // Obtain the SID of the object
        //

        if (0!=Object->SidLen)
        {
            pSid = &Object->Sid;
            cbSid = Object->SidLen;
        }
        else
        {
            
            dwError = DBGetAttVal(
                        pTHS->pDB,
                        1,
                        ATT_OBJECT_SID,
                        DBGETATTVAL_fREALLOC,
                        0,
                        &cbSid,
                        (PUCHAR *)&pSid
                        );

            if (0!=dwError)
            {
                //
                // Its a security principal , better have a SID
                //
                SetSvcErrorEx(SV_PROBLEM_DIR_ERROR,
                              ERROR_DS_MISSING_REQUIRED_ATT,
                              dwError);

               return pTHS->errCode;
            }

            Assert(cbSid <=sizeof(NT4SID));
        }

        //
        // This is a SAM class. Apply a sequence of tests to figure out whether
        // we need to notify SAM.
        //
        if ((!RoleTransfer ) &&(!SampNetLogonNotificationRequired(
                                        pSid,
                                        ClassMappingTable[iClass].SamObjectType)))
        {
            //
            // if Sam says no and it is not a role transfer bail
            //

            fNotifySam = FALSE;
        }



        //
        // If the change is not a delete, then check if the object is deleted
        // Avoid useless notifications on Tombstones.
        //

        if ((fNotifySam) && (DeltaType != SecurityDbDelete)
                && (DBIsObjDeleted(pTHS->pDB)))
        {
             fNotifySam=FALSE;
        }

        //
        // Groups require security enabled-ness for notifications.
        //

        if ((fNotifySam) && (SampGroupObjectType==ClassMappingTable[iClass].SamObjectType))
        {

            //
            // if the previous test succeeded and it is a group object
            // then check if it is a security enabled group
            //

            dwError = DBGetAttVal(pTHS->pDB,
                                    1,
                                    ATT_GROUP_TYPE,
                                    DBGETATTVAL_fCONSTANT,
                                    sizeof(ULONG),
                                    &cbGroupType,
                                    (PUCHAR *)&pGroupType
                                    );

            if (0==dwError)
            {
                // 
                // no error 
                // 
                Assert(sizeof(ULONG)==cbGroupType);

                //
                // If this is a resource group then increment iClass by one
                // so that we point into the alias object mapping table rather
                // than the group object mapping table
                //

                if (GroupType & GROUP_TYPE_RESOURCE_BEHAVOIR)
                {
                    iClass++;
                }
                fNotifySam = TRUE;
            }
            else
            {
                //
                // handle error. In most cases, DBGetAttVal failed  
                // because of resource shortage. so use 
                // error_not_enough_memory as the error. 
                // 

                SetSvcError(SV_PROBLEM_BUSY, 
                            ERROR_NOT_ENOUGH_MEMORY
                            );

                return pTHS->errCode;
            }
        }


        if (fNotifySam)
        {
            //
            // Notification is Required
            //

            SAMP_NOTIFICATION_INFORMATION * pNewSamNotification = NULL;
            
            //
            // Compose a New Notification Structure
            //

            pNewSamNotification = THAllocEx(pTHS, sizeof(SAMP_NOTIFICATION_INFORMATION));
            
            //
            // Memory Allocs in DS take Exceptions. And will cause a rollback.
            // So don't bother checking return from THAllocEx
            //   
            
            RtlCopyMemory(&pNewSamNotification->Sid, pSid, cbSid);
            pNewSamNotification->DeltaType = DeltaType;
            pNewSamNotification->iClass = iClass;
            pNewSamNotification->ObjectType = DsDbObjectSam;
            pNewSamNotification->RoleTransfer = RoleTransfer;
            pNewSamNotification->NewRole      = NewRole;
            pNewSamNotification->MixedModeChange = MixedModeChange;
            pNewSamNotification->GroupType = GroupType;
            pNewSamNotification->UserAccountControlChange = UserAccountControlChanged;
            
            if (SampDomainObjectType!=ClassMappingTable[iClass].SamObjectType)
            {
                //
                // If its not a domain object that has changed, we will supply the
                // SAM account Name. Even though Netlogon can live without the account
                // Name there are a lot of 3d party notification packages, synchronizing
                // the SAM database with security systems in other foreign operating systems.
                // It is quite possible that they may rely on the account Name, hence we
                // should try to supply this.
                //

                DWORD dwError=0;
                WCHAR *AccountNameBuffer;
                ULONG cbAccountName;

                dwError = DBGetAttVal(pTHS->pDB,
                                        1,
                                        ATT_SAM_ACCOUNT_NAME,
                                        DBGETATTVAL_fREALLOC,
                                        0,
                                        &cbAccountName,
                                        (PUCHAR *)&AccountNameBuffer
                                        );
                if (dwError==0)
                {
                    PUNICODE_STRING AccountName;

                    AccountName = THAllocEx(pTHS, sizeof(UNICODE_STRING));
                    
                    //
                    // Memory Allocs in DS take Exceptions. And will cause a rollback.
                    // So don't bother checking return from THAllocEx
                    //                 

                    AccountName->Length = (USHORT) cbAccountName;
                    AccountName->MaximumLength = (USHORT) cbAccountName;
                    AccountName->Buffer = AccountNameBuffer;

                    //
                    // Set the account name in the notification structure
                    //

                    pNewSamNotification->AccountName = AccountName;
                }
                else
                {
                    //
                    // handle error. In most cases, DBGetAttVal failed  
                    // because of resource shortage. so use 
                    // error_not_enough_memory as the error. 
                    // 
                    
                    SetSvcError(SV_PROBLEM_BUSY, 
                                ERROR_NOT_ENOUGH_MEMORY
                                );

                    return pTHS->errCode;
                }
            }

            //
            // For User Accounts, CliffV states that we also need the User account Control
            // Therefore Grab it from the database. Soldier on if we could not read the
            // property from the database
            //

            if (SampUserObjectType==ClassMappingTable[iClass].SamObjectType)
            {
                ULONG AccountControl = 0;

                if(0==DBGetSingleValue(pTHS->pDB, ATT_USER_ACCOUNT_CONTROL, &AccountControl,
                                sizeof(AccountControl), NULL))
                {
                     NTSTATUS NtStatus = SampFlagsToAccountControl(
                                                               AccountControl,
                                                              &AccountControl );
                     Assert( NT_SUCCESS( NtStatus ) );
                     if ( NT_SUCCESS( NtStatus ) )
                     {
                         pNewSamNotification->AccountControl = AccountControl;
                     }
                }
                else
                {
                    // 
                    // handle error. In most cases, DBGetSingleValue failed  
                    // because of resource shortage. so use 
                    // error_not_enough_memory as the error. 
                    // 
                    
                    SetSvcError(SV_PROBLEM_BUSY, 
                                ERROR_NOT_ENOUGH_MEMORY
                                );

                    return pTHS->errCode;
                }
            }
            
            //
            // Add this notification to the End of the List. I do not think it is advisable
            // to add it in the front, because this will reverse the order of notifications
            // to netlogon, which may cause problems. Pointer to both the Head as well as the
            // tail of the list is kept which allows easy addition to the end.
            //

            if (pTHS->pSamNotificationTail)
            {
                pTHS->pSamNotificationTail->Next = pNewSamNotification;
            }

            pTHS->pSamNotificationTail = pNewSamNotification;

            if (NULL==pTHS->pSamNotificationHead)
            {
                pTHS->pSamNotificationHead = pNewSamNotification;
            }
        }

    } else if ( LsaClass  ) {

        //
        // Do Lsa notification
        //



        if (Object->NameLen>0)
        {
            //
            // Make sure that we have a name within the current authoritative naming context
            //
            InitCommarg( &CommArg );

            /* ...and override some of them */
            CommArg.Svccntl.DerefAliasFlag          = DA_NEVER;
            CommArg.Svccntl.localScope              = TRUE;
            CommArg.Svccntl.SecurityDescriptorFlags = 0;
            CommArg.Svccntl.dontUseCopy             = FALSE;
            CommArg.ulSizeLimit                     = 0x20000;


            Err = DSNameToBlockName(pTHS,
                                    Object,
                                    &pObjAttrBlock,
                                    DN2BN_LOWER_CASE);
            if (Err) {
                //
                // set Error, so that the error code will be returned
                // when we finish
                // 
                SetNamError(NA_PROBLEM_BAD_NAME,
                            Object,
                            DIRERR_BAD_NAME_SYNTAX);

            } else {

                CurrentNamingContext = FindNamingContext(pObjAttrBlock, &CommArg);

                if ( CurrentNamingContext ) {

                    if( NameMatched( CurrentNamingContext, gAnchor.pDomainDN ) ||
                        ( gAnchor.pConfigDN != NULL && NameMatched( CurrentNamingContext, gAnchor.pConfigDN ) ) ) {

                        AddLsaNotification = TRUE;
                    }
                }
            }
        }
        else if (Object->SidLen>0)
        {

            //
            // The object has a SID but no name. This should happen only for user objects
            // currently. User objects have an account SID
            //

            Assert(CLASS_USER==LsaClass);

            //
            // Decrement the subauthority count to get the domain sid
            //

            (*RtlSubAuthorityCountSid(&Object->Sid))--;

            //
            // If the SID now matches the domain sid add to the notification
            //

            if (RtlEqualSid(&gAnchor.pDomainDN->Sid,&Object->Sid))
            {
                AddLsaNotification = TRUE;
            }

            //
            // Increment the subauthority to get back the account SID
            //

            (*RtlSubAuthorityCountSid(&Object->Sid))++;

        }




        if( AddLsaNotification ) {

            SAMP_NOTIFICATION_INFORMATION * pNewSamNotification = NULL;

            //
            // Compose a New Notification Structure
            //

            pNewSamNotification = THAllocEx(pTHS, sizeof(SAMP_NOTIFICATION_INFORMATION));

            //
            // Memory Allocs in DS take Exceptions. And will cause a rollback.
            // So don't bother checking return from THAllocEx
            //

            pNewSamNotification->Sid = Object->Sid;
            pNewSamNotification->DeltaType = DeltaType;
            pNewSamNotification->iClass = LsaClass;
            pNewSamNotification->ObjectType = DsDbObjectLsa;

            pNewSamNotification->Object = THAllocEx(pTHS,  Object->structLen );



            // Memory Allocs in DS take Exceptions. And will cause a rollback.
            // So don't bother checking return from THAllocEx
            //
            RtlCopyMemory( pNewSamNotification->Object,
                           Object,
                           DSNameSizeFromLen( Object-> NameLen ) );

            //
            // Now, if possible, get the sid of the current user if its an object we need to audit...
            //
            switch( LsaClass ) {

            case CLASS_TRUSTED_DOMAIN:
            case CLASS_SECRET:
            case CLASS_DOMAIN_POLICY:
                if ( !pTHS->fDRA ) {

                    Err = ImpersonateAnyClient();

                    if ( Err == 0 ) {

                        //
                        // Open the client token
                        //
                        if (!OpenThreadToken( GetCurrentThread(),
                                              TOKEN_QUERY,
                                              TRUE,
                                              &Token)) {

                            Err = GetLastError();

                        } else {

                            Size = sizeof( Buffer );
                            if ( !GetTokenInformation( Token,
                                                       TokenUser,
                                                       Buffer,
                                                       Size,
                                                       &Size ) ) {

                                Err = GetLastError();

                                Assert( Err != ERROR_INSUFFICIENT_BUFFER );

                            } else {

                                Assert( Size <= sizeof ( Buffer ) );

                                RtlCopyMemory( ( PBYTE )&pNewSamNotification->UserSid,
                                               ( ( PTOKEN_USER )Buffer)->User.Sid,
                                               RtlLengthSid( ( ( PTOKEN_USER )Buffer)->User.Sid ) );

                                Size = sizeof( Buffer );
                                if ( !GetTokenInformation( Token,
                                                           TokenStatistics,
                                                           Buffer,
                                                           Size,
                                                           &Size ) ) {

                                    Err = GetLastError();

                                    Assert( Err != ERROR_INSUFFICIENT_BUFFER );

                                } else {

                                    Assert( Size <= sizeof ( Buffer ) );

                                    pNewSamNotification->UserAuthenticationId =
                                                            ( ( PTOKEN_STATISTICS )Buffer )->AuthenticationId;

                                }
                            }

                            CloseHandle( Token );
                        }


                        UnImpersonateAnyClient();
                    }
                }
                break;

            default:
                break;

            }

            //
            // Add this notification to the End of the List. I do not think it is advisable
            // to add it in the front, because this will reverse the order of notifications
            // to netlogon, which may cause problems. Pointer to both the Head as well as the
            // tail of the list is kept which allows easy addition to the end.
            //

            if (pTHS->pSamNotificationTail)
            {
                pTHS->pSamNotificationTail->Next = pNewSamNotification;
            }

            pTHS->pSamNotificationTail = pNewSamNotification;

            if (NULL==pTHS->pSamNotificationHead)
            {
                pTHS->pSamNotificationHead = pNewSamNotification;
            }


        }
    }


    return pTHS->errCode;

}


ULONG
SampQueueNotifications(                
    DSNAME      * Object,
    ULONG         iClass,
    ULONG         LsaClass,
    SECURITY_DB_DELTA_TYPE  DeltaType,
    BOOL          MixedModeChange,
    BOOL          RoleTransfer,
    DOMAIN_SERVER_ROLE NewRole,
    ULONG         cModAtts,
    ATTRTYP      *pModAtts
    )
/*++

    Routine Description:

       Given the object Name and the class of the object and the notifiction type, this routine
       tries to find out if a notification is required and if so, will add a notification structure
       to pTHStls

    Parameters:

        Object       -- DS Name of the Object
        iClass       -- Indicates the Object Class
        LsaClass     -- Lsa's notion of the object class type
        DeltaType    -- Indicates the Type of Change
        MixedModeChange -- Indicates the mixed mode state is changing
        RoleTransfer -- Indicates that the Role is changing
        NewRole      -- The New Server Role
    Return Value

        pTHS->errCode: 0 succeed
                       Non Zero, service error, DbGetAttVal failed.

--*/
{
    THSTATE   * pTHS = pTHStls;
    BOOL      UserAccountControlChanged = FALSE;
    ULONG     i=0;

    //
    // If we're not part of LSA, don't bother
    //
    if (!gfRunningInsideLsa)
        return 0; 

    //
    // Ignore domains without a SID. These are NDNC's
    //
    if ( (LsaClass == 0)
      && (SampDomainObjectType==ClassMappingTable[iClass].SamObjectType)
      && (0 == Object->SidLen)) {

        return(0);
    }

    //
    // Queue a notification for generating an audit on transasction commit
    //
    SampAuditAddNotifications(
        Object,             
        iClass,             
        LsaClass,         
        DeltaType,
        cModAtts,
        pModAtts
        ); 
    

    //
    // Check if user account control changed as part of this transaction
    //

    for (i=0;i<cModAtts;i++)
    {
        if (ATT_USER_ACCOUNT_CONTROL==pModAtts[i])
        {
            UserAccountControlChanged = TRUE;
            break;
        }
    }
    
    //
    // Queue a notification for the Netlogon replicator and Lsa if needed.
    // The error code is ignored, pTHS->errCode will be set.
    //
    SampAddNetlogonAndLsaNotification(
        Object,             
        iClass,             
        LsaClass,           
        DeltaType,
        MixedModeChange,    
        RoleTransfer,       
        NewRole,
        UserAccountControlChanged
        );

    return pTHS->errCode;
    
}


BOOLEAN IsEqualNotificationNode(
            IN SAMP_NOTIFICATION_INFORMATION *LeadingNode,
            IN SAMP_NOTIFICATION_INFORMATION *TrailingNode
            )
/*++

    Given 2 notification nodes, this routine checks to see
    if they represent changes to the same object

    Parameters

        LeadingNode, TailingNode -- two notification nodes in the notification
        list. LeadingNode is the Node that is ahead in the notification list, and
        TrailingNode is the one that is behind in the notification list.

    Return Values

        TRUE -- Yes they are equal
        FALSE -- No they are not
--*/
{
    //
    // Compare if the 2 nodes are the same. Never compare the same for
    // role transfer or mixed mode change as we do not want to collapse
    // multiple of these into a single notification --> every one of these
    // are different. Further perform the optimization that an Add followed by a Modify
    // is simply equal to an Add ie If Leading node had an add operation, and tailing node
    // had a modify operation, then tailing node need not be used for the notification.
    //
    if ((LeadingNode->iClass == TrailingNode->iClass)
        && ( LeadingNode->ObjectType == TrailingNode->ObjectType)
        && (( LeadingNode->DeltaType == TrailingNode->DeltaType) || 
            ((LeadingNode->DeltaType == SecurityDbNew ) 
            &&  (TrailingNode->DeltaType ==SecurityDbChange)))
        && ( RtlEqualSid(&LeadingNode->Sid,&TrailingNode->Sid))
        && ( LeadingNode->UserAuthenticationId.LowPart==TrailingNode->UserAuthenticationId.LowPart)
        && ( LeadingNode->UserAuthenticationId.HighPart==TrailingNode->UserAuthenticationId.HighPart)
        &&  (!LeadingNode->RoleTransfer ) && (!TrailingNode->RoleTransfer)
        && (!LeadingNode->MixedModeChange) && (!TrailingNode->MixedModeChange)
        && (!LeadingNode->ObjectType==DsDbObjectLsa) 
        && (!TrailingNode->ObjectType==DsDbObjectLsa))
    {
        return(TRUE);
    }

    return(FALSE);
}


VOID
SampProcessAuditNotifications(
    SAMP_AUDIT_NOTIFICATION *NotificationList
    )
/*++

    This Routine Walks through the Linked list of notification structures, 
    writing the any audit events queued.

    Parameters

       NotificationList    List of Notifications that must be given to SAM / LSA

    Return Values

        None -- Void Function

--*/
{   
    THSTATE *pTHS = pTHStls;
    
    for (; NotificationList; NotificationList = NotificationList->Next)
    {
        //
        // Pre-created notifications can be queued but never fully initialized.
        // This happens when the sum of all changes amounts to no delta to
        // the object and the DS optimizes out the writes.  Such orphaned
        // notifications are discarded here.
        //
        if (NotificationList->AuditType & SAMP_AUDIT_TYPE_PRE_CREATED) {
            continue;
        }
        
        SampNotifyAuditChange(
            NotificationList->Sid,
            NotificationList->DeltaType,
            ClassMappingTable[NotificationList->iClass].SamObjectType,
            NotificationList->AccountName,
            NotificationList->AccountControl,
            NotificationList->GroupType,
            pTHS->CallerType,
            NotificationList->Privileges,
            NotificationList->AuditType,
            NotificationList->TypeSpecificInfo
            );
    }
}


VOID
SampProcessReplicatedInChanges(
    SAMP_NOTIFICATION_INFORMATION * NotificationList
    )
/*++

    This Routine Walks through the Linked list of notification structures, issuing a
    Notification to SAM / Netlogon.


    Parameters

       NotificationList    List of Notifications that must be given to SAM / LSA

    Return Values

        None -- Void Function

--*/
{
    THSTATE *pTHS = pTHStls;
    SAMP_NOTIFICATION_INFORMATION * OriginalList = NULL;
    

    for (OriginalList=NotificationList;NotificationList;NotificationList=NotificationList->Next)
    {
        SAMP_NOTIFICATION_INFORMATION * TmpList = NULL;
        BOOLEAN                         fNotifiedBefore = FALSE;

        //
        // Make a pass over the list to see if the item has been notified  
        //

        for (TmpList = OriginalList;((TmpList!=NULL) && (TmpList!=NotificationList));TmpList = TmpList->Next)
        {
            if (IsEqualNotificationNode(TmpList,NotificationList))
            {
                fNotifiedBefore = TRUE;
                break;
            }
        }

        //
        // If notified before then skip to next item
        //

        if (fNotifiedBefore)
        {
            continue;
        }

        if ( NotificationList->ObjectType == DsDbObjectSam ) {
                    
            SampNotifyReplicatedInChange(
                (PSID) &(NotificationList->Sid),
                pTHS->fSamWriteLockHeld?TRUE:FALSE,
                NotificationList->DeltaType,
                ClassMappingTable[NotificationList->iClass].SamObjectType,
                NotificationList->AccountName,
                NotificationList->AccountControl,
                NotificationList->GroupType,
                pTHS->CallerType,
                NotificationList->MixedModeChange,
                NotificationList->UserAccountControlChange
                );
            
            //
            // If the Role is changing , then give a role transfer notification
            //          
            
            if (NotificationList->RoleTransfer)
            {
                THSTATE * pTHSSaved;

                //
                // Always THSave and Restore before SamINotifyRoleChange.
                // This is because LSA calls are involved, which might
                // potentially write into the database.
                //

                pTHSSaved = THSave();

                SamINotifyRoleChange(
                    &(NotificationList->Sid),
                    NotificationList->NewRole
                    );

                THRestore(pTHSSaved);

            }
            
        } else {

            if ( pfLsaNotify ) {

                ( *pfLsaNotify )( NotificationList->iClass,
                                  NotificationList->Object,
                                  NotificationList->DeltaType,
                                  &NotificationList->UserSid,
                                  NotificationList->UserAuthenticationId,
                                  (BOOLEAN) pTHS->fDRA, // Used to distinguish between
                                             // a replicated in and originating
                                             // change
                                  (BOOLEAN) (pTHS->CallerType == CALLERTYPE_LSA)  // Used to distinguish between
                                             // LSA and DS/LDAP-originating changes
                                  );
            }
        }
    }
}

VOID
SampNotifyLsaOfXrefChange(
    IN DSNAME * pObject
    )
/*++

  Routine Description
  
   Calls the LSA change notification routine to notify the LSA of a cross
   ref change. This is called everytime the core updates its own cross ref
   list. Cross ref changes are important for LSA/Security to maintain the
   correct enterprise tree picture and also for knowing about new domains
   as they come and leave

  Parameters

    pObject - DSNAME of the Xref Object

  Return Values 

   None 
--*/
{
   LUID id={0,0};

   if (pfLsaNotify)
   {
       ( *pfLsaNotify )(
            (ULONG) CLASS_CROSS_REF,
            pObject,
            SecurityDbChange,
            NULL,
            id,
            (BOOLEAN) TRUE,
            (BOOLEAN) FALSE
            );
   }
}

   
   
NTSTATUS
SampGetClassAttribute(
    IN ULONG    ClassId,
    IN ULONG    AttributeId,
    IN OUT PULONG  attLen,
    OUT PVOID   pattVal
    )
/*++

    This routine gets the requested property of the class schema object, for the Class
    specified in ClassId.


    Parameters:

        ClassId  The ClassId of the class that we are interseted in
        AttributeId The attribute of the class schema object that we want
        attLen       The length of the attribute value is present in here .
                     Caller allocates the buffer in pAttVal and passes its length
                     in attLen. If the buffer required is less than the buffer supplied
                     then the data is returned in pattVal. Else  the required size is
                     returned in attLen.
        pattVal      The value of the attribute is returned in here.


    Secuirty Descriptors returned by this routine are always in a format that can be used
    by the RTL routines.

    Return Values

        STATUS_SUCCESS
        STATUS_NOT_FOUND
        STATUS_BUFFER_TOO_SMALL
--*/
{
    THSTATE     *pTHS=pTHStls;
    NTSTATUS    NtStatus = STATUS_SUCCESS;
    CLASSCACHE  *pClassCache;

    Assert(NULL!=pTHS);


    if (pClassCache = SCGetClassById(pTHS, ClassId))
    {
        //
        // Found the Class cache.
        //

        switch(AttributeId)
        {
        case ATT_SCHEMA_ID_GUID:
                if (*attLen >= sizeof(GUID))
                {
                    RtlCopyMemory(pattVal,&(pClassCache->propGuid),sizeof(GUID));
                    *attLen = sizeof(GUID);
                }
                else
                {
                    *attLen = sizeof(GUID);
                    NtStatus = STATUS_BUFFER_TOO_SMALL;
                }
                break;
                case ATT_DEFAULT_SECURITY_DESCRIPTOR:
                    if (*attLen >= pClassCache->SDLen)
                {
                    UCHAR   * NtSecurityDescriptorStart;
                    ULONG     NtSecurityDescriptorLength;

                    Assert(pClassCache->SDLen>0);

                    //
                    // Security descriptors in the class cache have a DWORD prepended to them,
                    // So take care of that in order to return a SD usable by NT RTL routines.
                    //

                    NtSecurityDescriptorStart = (PUCHAR)pClassCache->pSD;
                    NtSecurityDescriptorLength = pClassCache->SDLen;
                    RtlCopyMemory(pattVal,
                                  NtSecurityDescriptorStart,
                                  NtSecurityDescriptorLength);
                    *attLen = pClassCache->SDLen;
                }
                else
                {
                    *attLen = pClassCache->SDLen;
                    NtStatus = STATUS_BUFFER_TOO_SMALL;
                }
                break;
        default:
                        //
                        // We do not as yet have support for these other properties
                        //
                        Assert(FALSE);
            NtStatus = STATUS_NOT_FOUND;
            break;
        }


    }
    else
    {
        NtStatus = STATUS_UNSUCCESSFUL;
    }

    return NtStatus;
}

VOID
SampGetLoopbackObjectClassId(
    PULONG ClassId
    )
/*++

    For a Loopback Add Call, gets the object class of the object to be
    added.

    ClassId -- The Object Class is returned in this Parameter

--*/
{
    SAMP_LOOPBACK_ARG  *pSamLoopback;
    ULONG               i;
    DSNAME              *LoopbackObject;

    Assert(SampExistsDsTransaction());
    Assert(SampExistsDsLoopback(&LoopbackObject));

    pSamLoopback = pTHStls->pSamLoopback;

    Assert(pSamLoopback->type==LoopbackAdd);

    *ClassId = pSamLoopback->MostSpecificClass;

}
//
// SAM attributes that SAM prefers to be unique
//
// Attributes in this table cannot be modified by out-of-process callers.
// Note that this only applies to classes that are not known to SAM.
//
// ATT_SAM_ACCOUNT_NAME - Sam needs to enforce uniqueness
// ATT_OBJECT_SID - ditto
// ATT_IS_CRITICAL_SYSTEM_OBJECT - Controls what objects get replicated at
// dcpromo-time. Only internal callers can set this.
// Encrypted attributes such as SUPPLEMENTAL_CREDENTIALS, UNICODE_PWD etc
// can be written only by the system. Else a denial of service attack can
// take place because a client can give us potentially very large data to
// encrypt
//
ULONG   SamUniqueAttributes[] =
{
        ATT_USER_PASSWORD,
        ATT_SAM_ACCOUNT_NAME,
        ATT_OBJECT_SID,
        ATT_IS_CRITICAL_SYSTEM_OBJECT,
        ATT_SAM_ACCOUNT_TYPE,
        ATT_SUPPLEMENTAL_CREDENTIALS,
        ATT_UNICODE_PWD,
        ATT_NT_PWD_HISTORY,
        ATT_LM_PWD_HISTORY

};

BOOLEAN
SampSamUniqueAttributeAdded(
        ADDARG * pAddarg
        )
/*++

        This Routine Checks, wether Attributes like SAM_ACCOUNT_NAME, OBJECT_SID
        etc which should be unique are present in the given addarg

        Parameters

                pAddarg -- Add arg to be checked

    Return Values

                TRUE   -- If Unique Attribute is referenced
                FALSE  -- If Not
--*/
{
        ULONG i,j;

        for (i=0;i<pAddarg->AttrBlock.attrCount;i++)
        {
                for (j=0;j<ARRAY_COUNT(SamUniqueAttributes);j++)
                {
                        if (pAddarg->AttrBlock.pAttr[i].attrTyp == SamUniqueAttributes[j])
                                return TRUE;
                }
        }

        return FALSE;
}

BOOLEAN
SampSamUniqueAttributeModified(
        MODIFYARG * pModifyArg
        )
/*++

        This Routine Checks, wether Attributes like SAM_ACCOUNT_NAME, OBJECT_SID
        etc which should be unique are present in the given modifyarg

        Parameters

                pModifyArg -- Modify arg to be checked

    Return Values

                TRUE   -- If Unique Attribute is referenced
                FALSE  -- If Not
--*/
{
    ULONG j;
    ATTRMODLIST *Mod;

    for (Mod=&(pModifyArg->FirstMod); Mod!=NULL;Mod=Mod->pNextMod)
    {
        for (j=0;j<ARRAY_COUNT(SamUniqueAttributes);j++)
        {
            if (Mod->AttrInf.attrTyp == SamUniqueAttributes[j])
                return TRUE;
        }
    }

    return FALSE;
}

VOID
SampGetEnterpriseSidList(
   IN   PULONG pcSids,
   IN OPTIONAL PSID * rgSids
   )
/*++

    Routine Description

        This Routine Walks through the anchor data structure and
        obtains the Sids of all the domains in the enterprise
        Caller allocates the memory for the array of pointers in
        rgSids. The Pointer's to the Sids are in DS Memory space.
        The delayed memory free model is used, to provide safety
        if the gAnchor structure is modified. The caller should not
        use the pointers returned in rgSids for a long time, but
        should rather use it immediately and then forget the list.

    Parameters

        pcSids -- Count  of Sids
        rgSids --  Pointer to a Buffer that holds an array of pointers to Sids.


    Return Values

        STATUS_SUCCESS
--*/
{
    NTSTATUS    NtStatus = STATUS_SUCCESS;
    CROSS_REF_LIST * pCRL;


    *pcSids=0;
    for (pCRL=gAnchor.pCRL;pCRL!=NULL;pCRL=pCRL->pNextCR)
    {
        if (pCRL->CR.pNC->SidLen>0)
        {
            if (ARGUMENT_PRESENT(rgSids))
            {
                rgSids[*pcSids] = &(pCRL->CR.pNC->Sid);
            }
            (*pcSids)++;
        }
    }


}

NTSTATUS
MatchCrossRefBySid(
   IN PSID           SidToMatch,
   OUT PDSNAME       XrefDsName OPTIONAL,
   IN OUT PULONG     XrefNameLen
   )
/*++

    Routine Description

       This routine walks the gAnchor matching the SID specified to 
       that of any domain in that we may know about in the Xref list


    Parameters

       SidToMatch  The SID to match
       XrefDsName  The DSNAME of the Xref that matched 
       XrefNameLen The length of the Xref DSNAME that matched

    Return Values
       STATUS_SUCCESS
       STATUS_BUFFER_TOO_SMALL

--*/
{
    NTSTATUS    NtStatus = STATUS_OBJECT_NAME_NOT_FOUND;
    CROSS_REF_LIST * pCRL;


    for (pCRL=gAnchor.pCRL;pCRL!=NULL;pCRL=pCRL->pNextCR)
    {
        if ((pCRL->CR.pNC->SidLen>0) 
            && (RtlEqualSid(SidToMatch,(PSID)&pCRL->CR.pNC->Sid)))
        {
            ULONG LengthRequired = pCRL->CR.pObj->structLen;

       
            NtStatus = STATUS_SUCCESS;    

            if (ARGUMENT_PRESENT(XrefDsName))
            {
                if (*XrefNameLen>=LengthRequired)
                {
                    RtlCopyMemory(XrefDsName,pCRL->CR.pObj,LengthRequired);
                }
                else
                {
                    NtStatus = STATUS_BUFFER_TOO_SMALL;
                }
            }
            *XrefNameLen = LengthRequired;
            break;

        
         }
     }


     return(NtStatus);

}


   
   

VOID
SampSetSam(
    IN BOOLEAN fSAM
    )
/*++
   This Routine is used to Indicate "SAM" in
   thread state. This is called by SAM, when it
   has to create a thread state but not begin a
   transaction

--*/
{
    THSTATE *pTHS=pTHStls;

    Assert(NULL!=pTHS);
    pTHS->fSAM = fSAM;
    pTHS->fSamDoCommit = fSAM;
}




VOID
SampSignalStart(
        VOID
        )
/*++
    This Routine is used by SAM to signal to the core that it is finished
    initializing and that taks which can conflict with SAM initialization may
    now be started.
--*/
{
    SetEvent(hevSDPropagatorStart);
}







NTSTATUS
InitializeLsaNotificationCallback(
    VOID
    )
/*++

        This routine loads the lsasrv.dll and initializes a global function pointer used
    to do Lsa object change notificaiton

        Parameters

                VOID

    Return Values

                STATUS_SUCCESS -- Success
        STATUS_DLL_NOT_FOUND -- The lsasrv.dll could not be loaded
        STATUS_PROCEDURE_NOT_FOUND -- The notification producre entry point not found in
                                      the lsasrv.dll
--*/
{
    NTSTATUS Status = STATUS_SUCCESS;

    //
    // Load the Lsa
    LsaDllHandle = LoadLibraryA( "lsasrv" );

    if ( LsaDllHandle == NULL ) {

        Status = STATUS_DLL_NOT_FOUND;

    } else {

        pfLsaNotify = ( pfLsaIDsNotifiedObjectChange )
                            GetProcAddress( LsaDllHandle, "LsaIDsNotifiedObjectChange" );

        if ( !pfLsaNotify ) {

            Status = STATUS_PROCEDURE_NOT_FOUND;
        }
    }


    return( Status );
}


NTSTATUS
UnInitializeLsaNotificationCallback(
    VOID
    )
/*++

        This routine unloads the lsasrv.dll

        Parameters

                VOID

    Return Values

                STATUS_SUCCESS -- Success
--*/
{
    NTSTATUS Status = STATUS_SUCCESS;

    //
    // NULL out the function pointer, and unload the dll
    //
    pfLsaNotify = NULL;
    if (NULL!=LsaDllHandle)
        FreeLibrary(LsaDllHandle);
    LsaDllHandle = NULL;

    return( Status );
}



BOOL
SampIsClassIdLsaClassId(
    THSTATE *pTHS,
    IN ULONG Class,
    IN ULONG cModAtts,
    IN ATTRTYP *pModAtts,
    OUT PULONG LsaClass
    )
/*++

        This routine determines if the specified object class is one that corresponds
    to an object Lsa cares about

        Parameters

                Class -- The class id of the object in question.
        LsaClass -- 0 if not an Lsa object, non-zero otherwise.

    Return Values

                FALSE -- Not an LSA object
        TRUE -- Lsa object
--*/
{
    BOOL Return = FALSE;
    unsigned i;

    *LsaClass = 0;

    switch ( Class) {
    case CLASS_CROSS_REF:
    case CLASS_TRUSTED_DOMAIN:
    case CLASS_SECRET:

        if( !pTHS->fLsa && !pTHS->fSAM) {
           *LsaClass = Class;
            Return = TRUE;
        }
        break;
    }

    return( Return );
}


BOOL
SampIsClassIdAllowedByLsa(
    THSTATE  *pTHS,
    IN ULONG Class
    )
/*++
Routine Description:

    This routine determine if the specific object Class is OK for manipulating 
    according to LSA

Arguments:

    Class - object class ID

Return Values:

    TRUE - This object is OK for manipulating through LDAP
    
    FALSE - This object can't be manipulated by LDAP directly. So only 
            in-process client (such as LSA) can add/del/modify. 

--*/
{
    BOOL Result = TRUE;

    if (pTHS->fDSA || pTHS->fDRA || pTHS->fLsa ||
        (pTHS->CallerType == CALLERTYPE_LSA) )
    {
        return TRUE; 
    }

    //
    // Support for manipulating Trusted Doamin and Secret Object other than LSA,
    // or one of the trusted callers above should be disabled. Because LSA assumes 
    // that it is the only code that modifies TDO and secret objects.
    // 
    switch (Class) {
    case CLASS_TRUSTED_DOMAIN:
    case CLASS_SECRET:
        Result = FALSE;
        break;
    default:
        ;
    }

    return (Result);
}




NTSTATUS
SampGetServerRoleFromFSMO(
    DOMAIN_SERVER_ROLE * ServerRole)
/*++

    This routine looks at the FSMO for PDCness and
    determines the server Role


    Parameters

        ServerRole -- The server role is returned in here


    Return Values

        STATUS_SUCCESS
        STATUS_UNSUCCESSFUL

--*/
{
    NTSTATUS NtStatus = STATUS_SUCCESS;
    ULONG    err=0;
    THSTATE  *pTHS;
    DWORD    dwException;
    PVOID    dwEA;
    ULONG    ulErrorCode,
             dsid;
    ULONG    len;
    DSNAME   *pOwner;


    //
    // Should not have an open transaction while
    // calling this routine
    //

    if (SampExistsDsTransaction())
    {
        return STATUS_INVALID_PARAMETER;
    }

    __try
    {

        NtStatus = SampMaybeBeginDsTransaction(
                        TransactionRead);

        if (!NT_SUCCESS(NtStatus))
        {
            __leave;
        }

        pTHS = pTHStls;

        //
        // Position on the domain object
        //

        err = DBFindDSName(pTHS->pDB, gAnchor.pDomainDN);

        if (0!=err)
        {
            NtStatus = STATUS_UNSUCCESSFUL;
            __leave;
        }

        //
        // Get the FSMO role owner property
        //

        err = DBGetAttVal(pTHS->pDB,
                          1,
                          ATT_FSMO_ROLE_OWNER,
                          0,
                          0,
                          &len,
                          (UCHAR **)&pOwner);
        if (err)
        {
             NtStatus = STATUS_UNSUCCESSFUL;
                __leave;
        }

        //
        // We have everything we want
        //

        NtStatus = STATUS_SUCCESS;

        //
        // Is it own NTDS Dsa Object
        //

        if (NameMatched(pOwner,gAnchor.pDSADN)) {
                /* This DSA is  the role owner */

            *ServerRole = DomainServerRolePrimary;

        }
        else
        {
            *ServerRole = DomainServerRoleBackup;
        }
    }
     __except(GetExceptionData(GetExceptionInformation(), &dwException,
                              &dwEA, &ulErrorCode, &dsid))
    {
            HandleDirExceptions(dwException, ulErrorCode, dsid);
            NtStatus = STATUS_UNSUCCESSFUL;
    }


    //
    // End any open transactions
    //

    SampMaybeEndDsTransaction(TransactionCommit);

    return NtStatus;
}

NTSTATUS
SampComputeGroupType(
    ULONG  ObjectClass,
    ULONG  GroupType,
    NT4_GROUP_TYPE *pNT4GroupType,
    NT5_GROUP_TYPE *pNT5GroupType,
    BOOLEAN        *pSecurityEnabled
   )
/*++
    Routine Description

        Given the object class ATTR and Group Type, this routine computes the
        correct NT4 and NT5 group types. The object class parameter is currently
        not required but making this routine take the object class parameter
        allows easy transitioniong to a scheme whereby, the NT5 group types are
        determined by object type rather than group type.

    Parameters:

        ObjectClass      -- Specifies the object class.
        GroupType        -- The group type property
        pNT4GroupType    -- The NT4 Group type is returned in here
        pNT5GroupType    -- The NT5 Group type is returned in here
        pSecurityEnabled -- boolean indicates wether group is security enabled

    Return Values

        STATUS_SUCCESS
--*/
{
    Assert(SampGroupObjectType==SampSamObjectTypeFromDsClass(
                                            SampDeriveMostBasicDsClass(ObjectClass) ) );


    *pNT4GroupType = NT4GlobalGroup;

    if (GroupType & GROUP_TYPE_SECURITY_ENABLED)
        *pSecurityEnabled = TRUE;
    else
        *pSecurityEnabled = FALSE;

    if (GroupType & GROUP_TYPE_RESOURCE_GROUP)
    {
        *pNT5GroupType = NT5ResourceGroup;
        *pNT4GroupType = NT4LocalGroup;
    }
    else if (GroupType & GROUP_TYPE_ACCOUNT_GROUP)
    {
        *pNT5GroupType = NT5AccountGroup;
    }
    else if (GroupType & GROUP_TYPE_UNIVERSAL_GROUP)
    {
        *pNT5GroupType = NT5UniversalGroup;
    }
    else if (GroupType & GROUP_TYPE_APP_BASIC_GROUP)
    {
        *pNT5GroupType = NT5AppBasicGroup;
        *pNT4GroupType = NT4LocalGroup;
    }
    else if (GroupType & GROUP_TYPE_APP_QUERY_GROUP)
    {
        *pNT5GroupType = NT5AppQueryGroup;
        *pNT4GroupType = NT4LocalGroup;
    }

    return STATUS_SUCCESS;
}

BOOLEAN
SampAddLoopbackTask(
    IN PVOID TaskInfo
    )
/*++

Routine Description:

    This routine is called by SAM to add an entry in the LoopbackTaskInfo so
    that when the transaction is finally committed, SAM will be notified
    of the commit and then can take action.  For example, notify
    external packages about password changes.

    The element is placed in the outermost transaction info so it can be
    rolled back properly.

Arguments:

    TaskInfo - an opaque blob for SAM

Return Value:

    TRUE if the entry is put in the list; FALSE otherwise

--*/
{
    THSTATE *pTHS = pTHStls;
    PLOOPBACKTASKINFO pItem = NULL;
    NESTED_TRANSACTIONAL_DATA *pNTD = NULL;

    // We should have a thread state
    Assert( VALID_THSTATE(pTHS) );

    // We are in loopback, we should have a transaction
    Assert( SampExistsDsTransaction() );

    // We should a parameter, too
    Assert( TaskInfo );

    if ( !pTHS
      || !SampExistsDsTransaction()
      || !TaskInfo )
    {
        return FALSE;
    }

    pNTD = pTHS->JetCache.dataPtr;

    //
    // There should always be at least one entry
    //
    Assert( pNTD );

    //
    // Prepare the element
    //
    pItem = THAlloc( sizeof(LOOPBACKTASKINFO) );
    if ( !pItem )
    {
        return FALSE;
    }
    memset( pItem, 0, sizeof(LOOPBACKTASKINFO) );

    pItem->Next = NULL;
    pItem->TaskInfo = TaskInfo;

    //
    // Put the element in the list
    //
    if ( pNTD->pLoopbackTaskInfo )
    {
        pItem->Next = pNTD->pLoopbackTaskInfo;
    }
    pNTD->pLoopbackTaskInfo = pItem;

    return TRUE;

}

VOID
SampProcessLoopbackTasks(
    VOID
    )
/*++

Routine Description:

    This routine is called once a transaction has been end (either Commit or
    aborted ) and the SAM lock has been released.  It calls into SAM with 
    whatever items SAM put into the threadstate during the transaction.

    We will let SAM make the decision (whether do this item or ignore)
     based on fCommit field in each Loopback task item. 


Arguments:

    None.

Return Value:

    None.

--*/
{
    THSTATE *pTHS = pTHStls;
    PLOOPBACKTASKINFO Item, Temp;

    // We should have a thread state
    Assert( VALID_THSTATE(pTHS) );

    Item = pTHS->SamTaskList;

    while ( Item )
    {
        SampProcessSingleLoopbackTask( Item->TaskInfo );

        Temp = Item;
        Item = Item->Next;
        THFreeEx(pTHS, Temp );

    }

    pTHS->SamTaskList = NULL;

    return;
}



VOID
AbortLoopbackTasks(
    PLOOPBACKTASKINFO List
    )
/*++

Routine Description:

    This routine is called once a transaction has aborted.


Arguments:

    List - pointer points to the SAM Lookback Tasks.

Return Value:

    None.

--*/
{
    PLOOPBACKTASKINFO Item;

    Item = List;

    while ( Item )
    {
        SampAbortSingleLoopbackTask( Item->TaskInfo );
        Item = Item->Next;
    }

    return;
}

BOOL
LoopbackTaskPreProcessTransactionalData(
        BOOL fCommit
        )
/*++

Routine Description:

Arguments:

    fCommit: Whether the routine is committing or not

Return Value:

    True/False for Success/Fail.

--*/
{

    THSTATE          *pTHS = pTHStls;

    Assert( VALID_THSTATE(pTHS) );

    Assert( pTHS->transactionlevel > 0 );
    Assert( pTHS->JetCache.dataPtr );

    if( !pTHS->JetCache.dataPtr->pLoopbackTaskInfo )
    {
        // No data to process
        NOTHING;
    }
    else if ( !fCommit )
    {
        //
        // Aborted transaction, abort the SAM Loopback tasks
        // by marking fCommit (in easy task item structure)
        // field to FALSE.
        //
        AbortLoopbackTasks( pTHS->JetCache.dataPtr->pLoopbackTaskInfo );
    }
    // ELSE we will actually deal with this in the post process phase.


    return TRUE;

}

void
LoopbackTaskPostProcessTransactionalData(
    IN THSTATE *pTHS,
    IN BOOL fCommit,
    IN BOOL fCommitted
    )
/*++

Routine Description:

Arguments:

    pTHS: The threadstate

    fCommit: Whether the routine is committing or not

    fCommitted:

Return Value:

    None.

--*/
{

    LOOPBACKTASKINFO *pItem = NULL;
    LOOPBACKTASKINFO *pTemp = NULL;

    Assert( VALID_THSTATE(pTHS) );

    //
    // Parameter sanity check
    //
    if ( fCommitted )
    {
        // fCommitted should only be set when fCommit is true
        Assert( fCommit );
    }

    if ( !pTHS->JetCache.dataPtr->pLoopbackTaskInfo )
    {
        // nothing to do.
        NOTHING;
    }
    else 
    {
        if ( !fCommitted )
        {
            //
            // The commit did not succeed; abort the SAM Loopback tasks
            // by marking fCommit (in easy task item structure)
            // field to FALSE.
            // 
            AbortLoopbackTasks( pTHS->JetCache.dataPtr->pLoopbackTaskInfo );
        }

        // 
        // regardless whether commit success or not, as long as 
        // pTHS->JetCache.dataPtr->pLoopbackTaskInfo is not NULL, 
        // we will do the following. Since we have already marked 
        // commit or not in each Task Item field, we will let
        // SAM decide whether the do each every task item or not 
        // when we finally let SAM process the Task Info. 
        // 

        if ( 0 == pTHS->transactionlevel )
        {
            //
            // This was the final commit - put the changes on the thread
            // state
            //
            Assert( NULL == pTHS->SamTaskList );
            pTHS->SamTaskList = pTHS->JetCache.dataPtr->pLoopbackTaskInfo;
            pTHS->JetCache.dataPtr->pLoopbackTaskInfo = NULL;
        }
        else
        {
            //
            // Put the changes on the parent transaction
            //
            NESTED_TRANSACTIONAL_DATA *pOuter = NULL;

            pOuter = pTHS->JetCache.dataPtr->pOuter;

            Assert( pOuter );

            //
            // Add the pending list to the beginning of the parent's
            // list
            //

            //
            // First, the end of the Pending task list
            //
            pItem = pTHS->JetCache.dataPtr->pLoopbackTaskInfo;
            Assert( pItem );
            while ( pItem->Next )
            {
                pItem = pItem->Next;
            }

            pItem->Next = pOuter->pLoopbackTaskInfo;
            pOuter->pLoopbackTaskInfo = pTHS->JetCache.dataPtr->pLoopbackTaskInfo;

        }
    }

    //
    // One way on another the Pending list has been taken care of
    //
    pTHS->JetCache.dataPtr->pLoopbackTaskInfo = NULL;

}


BOOLEAN
SampDoesDomainExist(
    IN PDSNAME pDN
    )
//
// This routine (exported from ntdsa.dll) determines if a particular domain
// (pDN) exists by walking through the cross ref list.
//
{
    BOOLEAN  fExists = FALSE;

    CROSS_REF_LIST *      pCRL;

    // quick parameter check
    Assert( pDN );
    if ( !pDN )
    {
        return FALSE;
    }

    // we are not really updating gAnchor but we don't it
    // to change either
    EnterCriticalSection( &gAnchor.CSUpdate );
    _try
    {
        for ( pCRL = gAnchor.pCRL; NULL != pCRL; pCRL = pCRL->pNextCR )
        {
            if ( NameMatched( (DSNAME*)pCRL->CR.pNC, pDN ) )
            {
                fExists = TRUE;
                break;
            }
        }

    }
    _finally
    {
        LeaveCriticalSection( &gAnchor.CSUpdate );
    }


    return fExists;
}

extern
NTSTATUS
MatchCrossRefByNetbiosName(
   IN LPWSTR         NetbiosName,
   OUT PDSNAME       XrefDsName OPTIONAL,
   IN OUT PULONG     XrefNameLen
   )
/*++

    Routine Description

       This routine walks the gAnchor matching the Netbios domain
       name specified to that of any domain in that we may know 
       about in the Xref list

    Parameters

       NetbiosName  The Netbios name to match
       XrefDsName   The DSNAME of the Xref that matched 
       XrefNameLen  The length of the Xref DSNAME that matched

    Return Values
       STATUS_SUCCESS
       STATUS_BUFFER_TOO_SMALL

--*/
{
    NTSTATUS    NtStatus = STATUS_OBJECT_NAME_NOT_FOUND;
    ULONG       crFlags = (FLAG_CR_NTDS_NC | FLAG_CR_NTDS_DOMAIN);
    CROSS_REF_LIST * pCRL;
    UNICODE_STRING XrefNetbiosDomainName;
    UNICODE_STRING NetbiosDomainName;

    for (pCRL=gAnchor.pCRL;pCRL!=NULL;pCRL=pCRL->pNextCR)
    {
        if ( pCRL->CR.NetbiosName 
            && ((pCRL->CR.flags & crFlags) == crFlags))
        {
            RtlInitUnicodeString(&XrefNetbiosDomainName, 
                                 pCRL->CR.NetbiosName);
            
            RtlInitUnicodeString(&NetbiosDomainName, 
                                 NetbiosName);

            if ( RtlEqualDomainName(&XrefNetbiosDomainName,
                                    &NetbiosDomainName ))
            {            
                ULONG LengthRequired = pCRL->CR.pObj->structLen;

                NtStatus = STATUS_SUCCESS;    

                if (ARGUMENT_PRESENT(XrefDsName))
                {
                    if (*XrefNameLen>=LengthRequired)
                    {
                        RtlCopyMemory(XrefDsName,pCRL->CR.pObj,LengthRequired);
                    }
                    else
                    {
                        NtStatus = STATUS_BUFFER_TOO_SMALL;
                    }
                }
                *XrefNameLen = LengthRequired;
                break;
            }
         }
     }

     return(NtStatus);

}

extern
NTSTATUS
MatchDomainDnByNetbiosName(
   IN LPWSTR         NetbiosName,
   OUT PDSNAME       DomainDsName OPTIONAL,
   IN OUT PULONG     DomainDsNameLen
   )
/*++

    Routine Description

       This routine walks the gAnchor matching the Netbios domain
       name specified to that of any domain in that we may know 
       about in the Xref list

    Parameters

       NetbiosName     The Netbios domain name to match
       DomainDsName    The DSNAME of the Domain that matched 
       DomainDsNameLen The length of the Domain DSNAME that matched

    Return Values
       STATUS_SUCCESS
       STATUS_BUFFER_TOO_SMALL

--*/
{
    NTSTATUS    NtStatus = STATUS_OBJECT_NAME_NOT_FOUND;
    ULONG       crFlags = (FLAG_CR_NTDS_NC | FLAG_CR_NTDS_DOMAIN);
    CROSS_REF_LIST * pCRL;
    UNICODE_STRING XrefNetbiosDomainName;
    UNICODE_STRING NetbiosDomainName;

    for (pCRL=gAnchor.pCRL;pCRL!=NULL;pCRL=pCRL->pNextCR)
    {
        if ( pCRL->CR.NetbiosName 
            && ((pCRL->CR.flags & crFlags) == crFlags))            
       {
            RtlInitUnicodeString(&XrefNetbiosDomainName,
                                 pCRL->CR.NetbiosName);

            RtlInitUnicodeString(&NetbiosDomainName,
                                 NetbiosName);

            if ( RtlEqualDomainName(&XrefNetbiosDomainName,
                                    &NetbiosDomainName ))
            {        
                ULONG LengthRequired = pCRL->CR.pNC->structLen;

                NtStatus = STATUS_SUCCESS;    

                if (ARGUMENT_PRESENT(DomainDsName))
                {
                    if (*DomainDsNameLen>=LengthRequired)
                    {
                        RtlCopyMemory(DomainDsName,pCRL->CR.pNC,LengthRequired);
                    }
                    else
                    {
                        NtStatus = STATUS_BUFFER_TOO_SMALL;
                    }
                }
                *DomainDsNameLen = LengthRequired;
                break;
            }
         }
     }

     return(NtStatus);

}

extern
NTSTATUS
MatchDomainDnByDnsName(
   IN LPWSTR         DnsName,
   OUT PDSNAME       DomainDsName OPTIONAL,
   IN OUT PULONG     DomainDsNameLen
   )
/*++

    Routine Description

       This routine walks the gAnchor matching the Dns domain name
       specified to that of any domain in that we may know about 
       in the Xref list

    Parameters

       DnsName         The Dns domain name to match
       DomainDsName    The DSNAME of the Domain that matched 
       DomainDsNameLen The length of the Domain DSNAME that matched

    Return Values
       STATUS_SUCCESS
       STATUS_BUFFER_TOO_SMALL

--*/
{
    NTSTATUS    NtStatus = STATUS_OBJECT_NAME_NOT_FOUND;
    ULONG       crFlags = (FLAG_CR_NTDS_NC | FLAG_CR_NTDS_DOMAIN);
    CROSS_REF_LIST * pCRL;
    
    for (pCRL=gAnchor.pCRL;pCRL!=NULL;pCRL=pCRL->pNextCR)
    {
        if (    pCRL->CR.DnsName 
            && ((pCRL->CR.flags & crFlags) == crFlags)
            && DnsNameCompare_W(pCRL->CR.DnsName, DnsName ))
        {                   
            ULONG LengthRequired = pCRL->CR.pNC->structLen;

            NtStatus = STATUS_SUCCESS;    

            if (ARGUMENT_PRESENT(DomainDsName))
            {
                if (*DomainDsNameLen>=LengthRequired)
                {
                    RtlCopyMemory(DomainDsName,pCRL->CR.pNC,LengthRequired);
                }
                else
                {
                    NtStatus = STATUS_BUFFER_TOO_SMALL;
                }
            }
            *DomainDsNameLen = LengthRequired;
            break;            
         }
     }

     return(NtStatus);

}

extern
NTSTATUS
FindNetbiosDomainName(
   IN DSNAME*        DomainDsName,
   OUT LPWSTR        NetbiosName OPTIONAL,
   IN OUT PULONG     NetbiosNameLen
   )
{

    NTSTATUS    NtStatus = STATUS_OBJECT_NAME_NOT_FOUND;
    CROSS_REF_LIST * pCRL;


    for (pCRL=gAnchor.pCRL;pCRL!=NULL;pCRL=pCRL->pNextCR)
    {
        if ( NameMatched( (DSNAME*)pCRL->CR.pNC, DomainDsName ) )
        {
            if ( pCRL->CR.NetbiosName ) {
                
                ULONG LengthRequired = (wcslen( pCRL->CR.NetbiosName ) + 1) * sizeof(WCHAR);
           
                NtStatus = STATUS_SUCCESS;    
        
                if (ARGUMENT_PRESENT(NetbiosName))
                {
                    if (*NetbiosNameLen>=LengthRequired)
                    {
                        wcscpy( NetbiosName, pCRL->CR.NetbiosName );
                    }
                    else
                    {
                        NtStatus = STATUS_BUFFER_TOO_SMALL;
                    }
                }
                *NetbiosNameLen = LengthRequired;

            } else {

                NtStatus = STATUS_DS_NO_ATTRIBUTE_OR_VALUE;
            }

            break;

        }
    }


    return(NtStatus);
}



