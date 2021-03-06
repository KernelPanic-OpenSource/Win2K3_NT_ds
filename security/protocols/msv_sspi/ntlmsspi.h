/*++

Copyright (c) 1993 Microsoft Corporation

Module Name:

    ntlmsspi.h

Abstract:

    Header file describing the interface to code common to the
    NT Lanman Security Support Provider (NtLmSsp) Service and the DLL.

Author:

    Cliff Van Dyke (CliffV) 17-Sep-1993

Revision History:
    ChandanS  03-Aug-1996  Stolen from net\svcdlls\ntlmssp\common\ntlmsspi.h

--*/

#ifndef _NTLMSSPI_INCLUDED_
#define _NTLMSSPI_INCLUDED_

//
// init.c will #include this file with NTLMCOMN_ALLOCATE defined.
// That will cause each of these variables to be allocated.
//
#ifdef NTLMSSPI_ALLOCATE
#define EXTERN
#else
#define EXTERN extern
#endif


////////////////////////////////////////////////////////////////////////
//
// Global Definitions
//
////////////////////////////////////////////////////////////////////////

//
// Description of a credential.
//

#define SSP_CREDENTIAL_TAG_ACTIVE  (ULONG)('AdrC')
#define SSP_CREDENTIAL_TAG_DELETE  (ULONG)('DdrC')

#define SSP_CREDENTIAL_FLAG_WAS_NETWORK_SERVICE 0x1

typedef struct _SSP_CREDENTIAL {

    //
    // Global list of all Credentials.
    //  (Serialized by SspCredentialCritSect)
    //

    LIST_ENTRY Next;

    //
    // Used to prevent this Credential from being deleted prematurely.
    //  (Serialized by SspCredentialCritSect)
    //

    ULONG References;

    //
    // Flag of how credential may be used.
    //
    // SECPKG_CRED_* flags
    //

    ULONG CredentialUseFlags;

    //
    // Logon ID of the client
    //

    LUID LogonId;

    //
    // Process Id of client
    //

    ULONG ClientProcessID;

    //
    // Tag indicating credential is valid for fast reference.
    //

    ULONG CredentialTag;

    //
    // Impersonation level of caller at time of AcquireCredentialsHandle
    //

    SECURITY_IMPERSONATION_LEVEL ImpersonationLevel;

    //
    // Default credentials on client context, on server context UserName
    // holds a full user name (domain\user) and the other two should be
    // NULL.
    //

    UNICODE_STRING DomainName;
    UNICODE_STRING UserName;
    UNICODE_STRING Password;

    //
    // This flag should be set when the credential is unlinked
    // from the list.
    //

    BOOLEAN Unlinked;

    //
    // This flag is set when the credential was granted to a
    // kernel mode caller
    //

    BOOLEAN KernelClient;

    //
    // ntlm specific credential usage flags
    //

    ULONG MutableCredFlags;

} SSP_CREDENTIAL, *PSSP_CREDENTIAL;

typedef enum {
    IdleState,
    NegotiateSentState,    // Outbound context only
    ChallengeSentState,    // Inbound context only
    AuthenticateSentState, // Outbound context only
    AuthenticatedState,    // Inbound context only
    PassedToServiceState   // Outbound context only
} SSP_CONTEXT_STATE, *PSSP_CONTEXT_STATE;

typedef struct _NTLM_VER_INFO {
    ULONG64 Major : 8;
    ULONG64 Minor : 8;
    ULONG64 Build : 16;
    ULONG64 Reserved : 24;
    ULONG64 Revision : 8;
} NTLM_VER_INFO, *PNTLM_VER_INFO;

//
// Description of a Context
//

#define SSP_CONTEXT_TAG_ACTIVE  (ULONG64)('AxtC')
#define SSP_CONTEXT_TAG_DELETE  (ULONG64)('DxtC')

typedef struct _SSP_CONTEXT {

    //
    // Tag indicating context is valid.
    //

    ULONG64 ContextTag;


    //
    // Timeout the context after awhile.
    //
    ULONG TickStart;

    LARGE_INTEGER StartTime;
    ULONG Interval;

    //
    // Used to prevent this Context from being deleted prematurely.
    //  (Serialized by SspContextCritSect)
    //

    ULONG References;



    //
    // Maintain the Negotiated protocol
    //

    ULONG NegotiateFlags;

    //
    // Maintain the context requirements
    //

    ULONG ContextFlags;

    //
    // State of the context
    //

    SSP_CONTEXT_STATE State;

    //
    // Token Handle of authenticated user
    //  Only valid when in AuthenticatedState.
    //

    HANDLE TokenHandle;

    //
    // Referenced pointer to the credential used to create this
    // context.
    //

    PSSP_CREDENTIAL Credential;

    //
    // The challenge passed to the client.
    //  Only valid when in ChallengeSentState.
    //

    UCHAR Challenge[MSV1_0_CHALLENGE_LENGTH];

    //
    // The session key calculated by the LSA
    //

    UCHAR SessionKey[MSV1_0_USER_SESSION_KEY_LENGTH];

    //
    // Default credentials.
    //

    UNICODE_STRING DomainName;
    UNICODE_STRING UserName;
    UNICODE_STRING Password;

    //
    // optional marshalled targetinfo for credential manager.
    //

    PCREDENTIAL_TARGET_INFORMATIONW TargetInfo;

    //
    // marshalled target info for DFS/RDR.
    //

    PBYTE       pbMarshalledTargetInfo;
    ULONG       cbMarshalledTargetInfo;

    //
    // context handle referenced to validate loopback operations.
    //

    ULONG_PTR ServerContextHandle;

    //
    // Process Id of client
    //

    ULONG ClientProcessID;
    NTSTATUS LastStatus;

    BOOLEAN Server;         // client or server ? (can be implied by other fields...)

    BOOLEAN DownLevel;      // downlevel RDR/SRV ?

    //
    // This flag is set when the context was granted to a
    // kernel mode caller
    //

    BOOLEAN KernelClient;

    //
    // version control
    //

    union {
        NTLM_VER_INFO ClientVersion; // stored in server context
        NTLM_VER_INFO ServerVersion; // stored in client context
    };

    CHAR ContextMagicNumber[MSV1_0_USER_SESSION_KEY_LENGTH];

} SSP_CONTEXT, *PSSP_CONTEXT;

//
// Maximum lifetime of a context
//

#if DBG
#define NTLMSSP_MAX_LIFETIME (2*60*60*1000)    // 2 hours
#else
// used to be 2 minutes, changed to 5 minutes to allow negotiation in
// wide-area networks which can have long retry timeouts
#define NTLMSSP_MAX_LIFETIME (5*60*1000)    // 5 minutes
#endif // DBG



typedef struct _SSP_PROCESSOPTIONS {

    //
    // Global list of all process options.
    //  (Serialized by NtLmGlobalProcessOptionsLock
    //

    LIST_ENTRY Next;

    //
    // Process Id of client
    //

    ULONG ClientProcessID;

    //
    // options bitmask.
    //

    ULONG ProcessOptions;

} SSP_PROCESSOPTIONS, *PSSP_PROCESSOPTIONS;



////////////////////////////////////////////////////////////////////////
//
// Procedure Forwards
//
////////////////////////////////////////////////////////////////////////


//
// Procedure forwards from credhand.cxx
//

NTSTATUS
SspCredentialInitialize(
    VOID
    );

VOID
SspCredentialTerminate(
    VOID
    );

NTSTATUS
SspCredentialReferenceCredential(
    IN ULONG_PTR CredentialHandle,
    IN BOOLEAN DereferenceCredential,
    OUT PSSP_CREDENTIAL * UserCredential
    );

VOID
SspCredentialDereferenceCredential(
    PSSP_CREDENTIAL Credential
    );

NTSTATUS
SspCredentialGetPassword(
    IN PSSP_CREDENTIAL Credential,
    OUT PUNICODE_STRING Password
    );

//
// Procedure forwards from context.cxx
//

NTSTATUS
SspContextInitialize(
    VOID
    );

VOID
SspContextTerminate(
    VOID
    );

//
// from ctxtcli.cxx
//

NTSTATUS
CredpParseUserName(
    IN OUT LPWSTR ParseName,
    OUT LPWSTR* pUserName,
    OUT LPWSTR* pDomainName
    );

NTSTATUS
CopyCredManCredentials(
    IN PLUID LogonId,
    IN CREDENTIAL_TARGET_INFORMATIONW* pTargetInfo,
    IN OUT PSSP_CONTEXT Context,
    IN BOOLEAN fShareLevel,
    IN BOOLEAN bAllowOwfPassword,
    OUT BOOLEAN* pbIsOwfPassword
    );

NTSTATUS
CredpExtractMarshalledTargetInfo(
    IN  PUNICODE_STRING TargetServerName,
    OUT CREDENTIAL_TARGET_INFORMATIONW **pTargetInfo
    );

NTSTATUS
CredpProcessUserNameCredential(
    IN  PUNICODE_STRING MarshalledUserName,
    OUT PUNICODE_STRING UserName,
    OUT PUNICODE_STRING DomainName,
    OUT PUNICODE_STRING Password
    );

//
// random number generator.
//

NTSTATUS
SspGenerateRandomBits(
    VOID        *pRandomData,
    ULONG       cRandomData
    );

//
// Procedure forwards from ntlm.cxx
//
VOID
NtLmCheckLmCompatibility(
    );

VOID
NtLmQueryMappedDomains(
    VOID
    );

VOID
NtLmFreeMappedDomains(
    VOID
    );


VOID
NTAPI
NtLmQueryDynamicGlobals(
    PVOID pvContext,
    BOOLEAN TimedOut
    );

ULONG
NtLmCheckProcessOption(
    IN  ULONG OptionRequest
    );

BOOLEAN
NtLmSetProcessOption(
    IN  ULONG OptionRequest,
    IN  BOOLEAN DisableOption
    );


//
// Procedure forwards from rng.cxx
//

VOID
NtLmCleanupRNG(VOID);

BOOL
NtLmInitializeRNG(VOID);


/*++

Brief description of the challenge/response algorithms for LM, NTLM, and NTLM3

  The basic outline is the same for all versions, just the OWF, RESP, and SESSKEY
  funcs are different:

1. Compute a "response key" (Kr) from the user's name (U), domain (UD) and password (P):

    Kr = OWF(U, UD, P)

2. Compute a response using the response key, server challenge (NS),
    client challenge (NC), timestamp (T), version (V), highest version
    client understands (HV), and the server's principal name (S)

    R = RESP(Kr, NS, NC, T, V, HV, S)

3. Compute a session key from Kr, U, UD

    Kx = SESSKEY(Kr, R, U, UD)


The are the OWF, RESP, and SESSKEY funcs for NTLM3

    OWF(U, UD, P) = MD5(MD4(P), U, UD)
    RESP(Kr, NS, NC, T, V, HV, S) = (V, HV, R, T, NC, HMAC(Kr, (NS, V, HV, T, NC, S)), S)
    SESSKEY(Ku, R, U, UD) = HMAC(Kr, R)

--*/



PMSV1_0_AV_PAIR
MsvpAvlInit(
    IN void * pAvList
    );

PMSV1_0_AV_PAIR
MsvpAvlGet(
    IN PMSV1_0_AV_PAIR pAvList,             // first pair of AV pair list
    IN MSV1_0_AVID AvId,                    // AV pair to find
    IN LONG cAvList                         // size of AV list
    );

ULONG
MsvpAvlLen(
    IN PMSV1_0_AV_PAIR pAvList,            // first pair of AV pair list
    IN LONG cAvList                        // max size of AV list
    );

PMSV1_0_AV_PAIR
MsvpAvlAdd(
    IN PMSV1_0_AV_PAIR pAvList,             // first pair of AV pair list
    IN MSV1_0_AVID AvId,                    // AV pair to add
    IN PUNICODE_STRING pString,             // value of pair
    IN LONG cAvList                         // max size of AV list
    );


ULONG
MsvpAvlSize(
    IN ULONG iPairs,            // number of AV pairs response will include
    IN ULONG iPairsLen          // total size of values for the pairs
    );

NTSTATUS
MsvpAvlToString(
    IN      PUNICODE_STRING AvlString,
    IN      MSV1_0_AVID AvId,
    IN OUT  LPWSTR *szAvlString
    );

NTSTATUS
MsvpAvlToFlag(
    IN      PUNICODE_STRING AvlString,
    IN      MSV1_0_AVID AvId,
    IN OUT  ULONG *ulAvlFlag
    );


VOID
MsvpCalculateNtlm2Challenge (
    IN UCHAR ChallengeToClient[MSV1_0_CHALLENGE_LENGTH],
    IN UCHAR ChallengeFromClient[MSV1_0_CHALLENGE_LENGTH],
    OUT UCHAR Challenge[MSV1_0_CHALLENGE_LENGTH]
    );

VOID
MsvpCalculateNtlm2SessionKeys (
    IN PUSER_SESSION_KEY NtUserSessionKey,
    IN UCHAR ChallengeToClient[MSV1_0_CHALLENGE_LENGTH],
    IN UCHAR ChallengeFromClient[MSV1_0_CHALLENGE_LENGTH],
    OUT PUSER_SESSION_KEY LocalUserSessionKey,
    OUT PLM_SESSION_KEY LocalLmSessionKey
    );


//
// calculate NTLM3 response from credentials and server name
// called with pNtlm3Response filled in with version, client challenge, timestamp
//

VOID
MsvpNtlm3Response (
    IN PNT_OWF_PASSWORD pNtOwfPassword,
    IN PUNICODE_STRING pUserName,
    IN PUNICODE_STRING pLogonDomainName,
    IN ULONG ServerNameLength,
    IN UCHAR ChallengeToClient[MSV1_0_CHALLENGE_LENGTH],
    IN PMSV1_0_NTLM3_RESPONSE pNtlm3Response,
    OUT UCHAR Response[MSV1_0_NTLM3_RESPONSE_LENGTH],
    OUT PUSER_SESSION_KEY UserSessionKey,
    OUT PLM_SESSION_KEY LmSessionKey
    );

typedef struct {
        UCHAR Response[MSV1_0_NTLM3_RESPONSE_LENGTH];
        UCHAR ChallengeFromClient[MSV1_0_CHALLENGE_LENGTH];
} MSV1_0_LM3_RESPONSE, *PMSV1_0_LM3_RESPONSE;

//
// calculate LM3 response from credentials
//

VOID
MsvpLm3Response (
    IN PNT_OWF_PASSWORD pNtOwfPassword,
    IN PUNICODE_STRING pUserName,
    IN PUNICODE_STRING pLogonDomainName,
    IN UCHAR ChallengeToClient[MSV1_0_CHALLENGE_LENGTH],
    IN PMSV1_0_LM3_RESPONSE pLm3Response,
    OUT UCHAR Response[MSV1_0_NTLM3_RESPONSE_LENGTH],
    OUT PUSER_SESSION_KEY UserSessionKey,
    OUT PLM_SESSION_KEY LmSessionKey
    );


NTSTATUS
MsvpLm20GetNtlm3ChallengeResponse (
    IN PNT_OWF_PASSWORD pNtOwfPassword,
    IN PUNICODE_STRING pUserName,
    IN PUNICODE_STRING pLogonDomainName,
    IN PUNICODE_STRING pServerName,
    IN UCHAR ChallengeToClient[MSV1_0_CHALLENGE_LENGTH],
    OUT PMSV1_0_NTLM3_RESPONSE pNtlm3Response,
    OUT PMSV1_0_LM3_RESPONSE pLm3Response,
    OUT PUSER_SESSION_KEY UserSessionKey,
    OUT PLM_SESSION_KEY LmSessionKey
    );


#endif // ifndef _NTLMSSPI_INCLUDED_
