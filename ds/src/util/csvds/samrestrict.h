#ifndef _SAM_H
#define _SAM_H

/*++

Copyright (c) 1996 Microsoft Corporation.
All rights reserved.

MODULE NAME:

    samrestrict.h

ABSTRACT:
    
    Ldap display names of SAM read-only attrbiutes.

DETAILS:

    We have the problem that if we export out SAM objects,
    we can't put them back in with all the attributes we 
    get because some of them are SAM read only.
    This header contains the read onlys listed by
    their ldapDisplayNames, for each Sam object type.  
    
    
CREATED:

    07/14/97    Roman Yelensky (t-romany)

REVISION HISTORY:

--*/

/*
 * The server and domain are here only for completeness, its not actually
 * relevant and is not expected and won't work for import/export operations.
 */


//
// CLASS_SAM_SERVER, SampServerObjectType (ldapdisplayname: samServer) 
//
PWSTR g_rgszServerSAM[] = {
     L"revision",  //SAMP_FIXED_SERVER_REVISION_LEVEL, ATT_REVISION
     L"objectSid", //not in mappings.c, but still required!, ATT_OBJECT_SID
     NULL
};


//
// CLASS_SAM_DOMAIN, SampDomainObjectType (ldapdisplayname: domain) 
//
PWSTR g_rgszDomainSAM[] = {
    L"objectSid",                // SAMP_DOMAIN_SID, ATT_OBJECT_SID
    L"domainReplica",            // SAMP_DOMAIN_REPLICA, ATT_DOMAIN_REPLICA
    L"creationTime",             // SAMP_FIXED_DOMAIN_CREATION_TIME, 
                                // ATT_CREATION_TIME
    L"modifiedCount",            // SAMP_FIXED_DOMAIN_MODIFIED_COUNT,
                                // ATT_MODIFIED_COUNT
    L"modifiedCountAtLastProm",  // SAMP_FIXED_DOMAIN_MODCOUNT_LAST_PROMOTION, 
                                // ATT_MODIFIED_COUNT_AT_LAST_PROM
    L"nextRid",                  // SAMP_FIXED_DOMAIN_NEXT_RID, ATT_NEXT_RID
    L"serverState",              // SAMP_FIXED_DOMAIN_SERVER_STATE, 
                                // ATT_SERVER_STATE
    L"sAMAccountType",           // SAMP_DOMAIN_ACCOUNT_TYPE, 
                                // ATT_SAM_ACCOUNT_TYPE
    L"uASCompat",               // SAMP_FIXED_DOMAIN_UAS_COMPAT_REQUIRED,
                                //  ATT_UAS_COMPAT
    NULL
};


//
// CLASS_GROUP, SampGroupObjectType (ldapdisplayname: group) 
//
PWSTR g_rgszGroupSAM[] = {
    L"rid",                 // SAMP_FIXED_GROUP_RID, ATT_RID
    L"sAMAccountType",      // SAMP_GROUP_ACCOUNT_TYPE, ATT_SAM_ACCOUNT_TYPE
    L"objectSid",           // not in mappings.c, but still required!, 
                           // ATT_OBJECT_SID
    L"memberOf",            // SAMP_USER_GROUPS, ATT_MEMBER
    L"isCriticalSystemObject", // SAMP_FIXED_GROUP_IS_CRITICAL,
                               //  ATT_IS_CRITICAL_SYSTEM_OBJECT
    NULL
};


//
// CLASS_LOCALGROUP, SampAliasObjectType (ldapdisplayname: localGroup) 
//
PWSTR g_rgszLocalGroupSAM[] = {
    L"rid",                // SAMP_FIXED_ALIAS_RID, ATT_RID
    L"sAMAccountType",     // SAMP_ALIAS_ACCOUNT_TYPE, ATT_SAM_ACCOUNT_TYPE
    L"objectSid",          // not in mappings.c, but still required!, ATT_OBJECT_SID
    L"isCriticalSystemObject", // SAMP_FIXED_GROUP_IS_CRITICAL,
                               //  ATT_IS_CRITICAL_SYSTEM_OBJECT
    NULL
};


//
// CLASS_USER, SampUserObjectType (ldapdisplayname: user)
//
PWSTR g_rgszUserSAM[] = {
    L"memberOf",                // SAMP_USER_GROUPS, ATT_MEMBER
    L"dBCSPwd",                 // SAMP_USER_DBCS_PWD, ATT_DBCS_PWD
    L"ntPwdHistory",            // SAMP_USER_NT_PWD_HISTORY, ATT_NT_PWD_HISTORY
    L"lmPwdHistory",            // SAMP_USER_LM_PWD_HISTORY, ATT_LM_PWD_HISTORY
    L"lastLogon",               // SAMP_FIXED_USER_LAST_LOGON, ATT_LAST_LOGON
    L"lastLogoff",              // SAMP_FIXED_USER_LAST_LOGOFF, ATT_LAST_LOGOFF
    L"badPasswordTime",         // SAMP_FIXED_USER_LAST_BAD_PASSWORD_TIME, 
                               // ATT_BAD_PASSWORD_TIME
    L"rid",                     // SAMP_FIXED_USER_USERID, ATT_RID
    L"badPwdCount",             // SAMP_FIXED_USER_BAD_PWD_COUNT, 
                               // ATT_BAD_PWD_COUNT
    L"logonCount",              // SAMP_FIXED_USER_LOGON_COUNT, ATT_LOGON_COUNT
    L"sAMAccountType",          // SAMP_USER_ACCOUNT_TYPE, ATT_SAM_ACCOUNT_TYPE
    L"supplementalCredentials", // SAMP_FIXED_USER_SUPPLEMENTAL_CREDENTIALS,
                               // ATT_SUPPLEMENTAL_CREDENTIALS
    L"objectSid",               // not in mappings.c, but still required!, 
                               // ATT_OBJECT_SID
    L"pwdLastSet",
    L"isCriticalSystemObject",  // SAMP_FIXED_USER_IS_CRITICAL,
                                //  ATT_IS_CRITICAL_SYSTEM_OBJECT
    L"lastLogonTimestamp",      // SAMP_FIXED_USER_LAST_LOGON_TIMESTAMP,
                                //  ATT_LAST_LOGON_TIMESTAMP
    NULL
};


PWSTR g_rgszSamObjects[] = {
    L"samServer",
    L"domain",
    L"group",
    L"localGroup",
    L"user",
    NULL
};
    
PRTL_GENERIC_TABLE pSamObjects=NULL;
PRTL_GENERIC_TABLE pServerAttrs=NULL;
PRTL_GENERIC_TABLE pDomainAttrs=NULL;
PRTL_GENERIC_TABLE pGroupAttrs=NULL;
PRTL_GENERIC_TABLE pLocalGroupAttrs=NULL;
PRTL_GENERIC_TABLE pUserAttrs=NULL;

/*Although not restricted to SAM type stuff 
 (but currently only used for such), 
 the two tables below list any objects that we must do special 
 outputting for and their associated special actions. 
 This will we setup into tables in the table creation
 function and used in LL_ldap_parse()*/

PWSTR g_rgszSpecialClass[] = {
    L"domain",
    L"group",
    L"localGroup",
    NULL
};


int g_rgAction[] = {
    S_MEM,
    S_MEM,
    S_MEM,
    0
};
#endif
