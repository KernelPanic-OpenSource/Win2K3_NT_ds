//+---------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1995.
//
//  File:       cred.h
//
//  Contents:
//
//  Classes:
//
//  Functions:
//
//  History:    09-23-97   jbanes   LSA integration stuff.
//
//----------------------------------------------------------------------------

#ifndef __CRED_H__
#define __CRED_H__

#define PCT_CRED_MAGIC  *(DWORD *)"CtcP"

typedef struct _CRED_THUMBPRINT
{
    DWORD LowPart;
    DWORD HighPart;
} CRED_THUMBPRINT, *PCRED_THUMBPRINT;

typedef struct _SPCredential
{
    PCCERT_CONTEXT      pCert;
    CRED_THUMBPRINT     CertThumbprint;

    HCRYPTPROV          hProv;
    HCRYPTPROV          hEphem512Prov;
    HCRYPTPROV          hEphem1024Prov;
    HCRYPTPROV          hRemoteProv;

    PROV_ENUMALGS_EX *  pCapiAlgs;      // Algs supported by hProv (server only)
    DWORD               cCapiAlgs;
    DWORD               fAppRemoteProv; // Does application own hRemoteProv?

    DWORD               dwCertFlags;    // Is this a server SGC cert?

    DWORD               dwKeySpec;
    ExchSpec            dwExchSpec;

    PPUBLICKEY          pPublicKey;

    PBYTE               pbSsl3SerializedChain;
    DWORD               cbSsl3SerializedChain;

    HCRYPTKEY           hTek;               // Ephemeral DH

    LIST_ENTRY          ListEntry;
} SPCredential, *PSPCredential;
          

typedef struct _SPCredentialGroup {
    DWORD               Magic;
    DWORD               grbitProtocol;
    DWORD               grbitEnabledProtocols;
    DWORD               dwFlags;
    DWORD               dwMinStrength;
    DWORD               dwMaxStrength;
    DWORD               cSupportedAlgs;
    ALG_ID *            palgSupportedAlgs;
    DWORD               dwSessionLifespan;
    ULONG               ProcessId; 

    // server-side only
    LONG                cMappers;
    HMAPPER **          pahMappers;
    HCERTSTORE          hApplicationRoots;  // Specified by application.
    HCERTSTORE          hUserRoots;         // Current user ROOT - monitored for changes
    PBYTE               pbTrustedIssuers;
    DWORD               cbTrustedIssuers;

    CRED_THUMBPRINT     CredThumbprint;     // Used when purging server cache entries.
    LONG                RefCount;

    DWORD               CredCount;
    LIST_ENTRY          CredList;
    RTL_RESOURCE        csCredListLock;

    LIST_ENTRY          GlobalCredList;
} SPCredentialGroup, * PSPCredentialGroup;


typedef struct _LSA_SCHANNEL_SUB_CRED
{
    PCCERT_CONTEXT      pCert;
    LPWSTR              pszPin;
    HCRYPTPROV          hRemoteProv;
    PVOID               pPrivateKey;
    DWORD               cbPrivateKey;
    LPSTR               pszPassword;
} LSA_SCHANNEL_SUB_CRED, *PLSA_SCHANNEL_SUB_CRED;

typedef struct _LSA_SCHANNEL_CRED
{
    DWORD           dwVersion;
    DWORD           cSubCreds;
    PLSA_SCHANNEL_SUB_CRED paSubCred;
    HCERTSTORE      hRootStore;

    DWORD           cMappers;
    struct _HMAPPER **aphMappers;

    DWORD           cSupportedAlgs;
    ALG_ID *        palgSupportedAlgs;

    DWORD           grbitEnabledProtocols;
    DWORD           dwMinimumCipherStrength;
    DWORD           dwMaximumCipherStrength;
    DWORD           dwSessionLifespan;
    DWORD           dwFlags;
    DWORD           reserved;
} LSA_SCHANNEL_CRED, *PLSA_SCHANNEL_CRED;


#define LockCredentialExclusive(p)  RtlAcquireResourceExclusive(&(p)->csCredListLock, TRUE)
#define LockCredentialShared(p)     RtlAcquireResourceShared(&(p)->csCredListLock, TRUE)
#define UnlockCredential(p)         RtlReleaseResource(&(p)->csCredListLock)

BOOL
SslInitCredentialManager(VOID);

BOOL
SslFreeCredentialManager(VOID);

BOOL
SslCheckForGPEvent(void);

BOOL
IsValidThumbprint(
    PCRED_THUMBPRINT Thumbprint);

BOOL
IsSameThumbprint(
    PCRED_THUMBPRINT Thumbprint1,
    PCRED_THUMBPRINT Thumbprint2);

void
GenerateCertThumbprint(
    PCCERT_CONTEXT pCertContext,
    PCRED_THUMBPRINT Thumbprint);

NTSTATUS
GenerateRandomThumbprint(
    PCRED_THUMBPRINT Thumbprint);

BOOL
DoesCredThumbprintMatch(
    PSPCredentialGroup pCredGroup,
    PCRED_THUMBPRINT pThumbprint);

void
ComputeCredExpiry(
    PSPCredentialGroup pCredGroup,
    PTimeStamp ptsExpiry);

SP_STATUS
SPCreateCred(
    DWORD           dwProtocol,
    PLSA_SCHANNEL_SUB_CRED pSubCred,
    PSPCredential   pCurrentCred,
    BOOL *          pfEventLogged);

SP_STATUS
SPCreateCredential(
   PSPCredentialGroup *ppCred,
   DWORD grbitProtocol,
   PLSA_SCHANNEL_CRED pSchannelCred);

SP_STATUS
AddCredentialToGroup(
    PSPCredentialGroup  pCredGroup, 
    PSPCredential       pCred);

SP_STATUS
IsCredentialInGroup(
    PSPCredentialGroup  pCredGroup, 
    PCCERT_CONTEXT      pCertContext,
    PBOOL               pfInGroup);

SECURITY_STATUS
UpdateCredentialFormat(
    PSCH_CRED pSchCred,         // in
    PLSA_SCHANNEL_CRED pCred);  // out

void
GlobalCheckForCertificateRenewal(void);

void
CheckForCredentialRenewal(
    PSPCredentialGroup pCredGroup);

BOOL
CheckForCertificateRenewal(
    DWORD dwProtocol,
    PCCERT_CONTEXT pCertContext,
    PCCERT_CONTEXT *ppNewCertificate);

NTSTATUS
FindDefaultMachineCred(
    PSPCredentialGroup *ppCred,
    DWORD dwProtocol);

BOOL
SPReferenceCredential(
    PSPCredentialGroup  pCred);

BOOL
SPDereferenceCredential(
    PSPCredentialGroup  pCred,
    BOOL fFreeRemoteHandle);

void
SPDeleteCred(
    PSPCredential pCred,
    BOOL fFreeRemoteHandle);

BOOL 
SPDeleteCredential(
    PSPCredentialGroup pCred,
    BOOL fFreeRemoteHandle);

// flag bit definitions
#define CRED_FLAG_NO_SYSTEM_MAPPER              0x00000004  // client cert mapping
#define CRED_FLAG_NO_SERVERNAME_CHECK           0x00000008  // server cert validation
#define CRED_FLAG_MANUAL_CRED_VALIDATION        0x00000010  // server cert validation
#define CRED_FLAG_NO_DEFAULT_CREDS              0x00000020  // client certificate selection
#define CRED_FLAG_UPDATE_ISSUER_LIST            0x00000040  // new settings have been downloaded from GPO
#define CRED_FLAG_DELETED                       0x00000080  // credential has been deleted by application.

#define CRED_FLAG_REVCHECK_END_CERT             0x00000100 
#define CRED_FLAG_REVCHECK_CHAIN                0x00000200 
#define CRED_FLAG_REVCHECK_CHAIN_EXCLUDE_ROOT   0x00000400 
#define CRED_FLAG_IGNORE_NO_REVOCATION_CHECK    0x00000800
#define CRED_FLAG_IGNORE_REVOCATION_OFFLINE     0x00001000

#define CRED_FLAG_CHECK_FOR_RENEWAL             0x00002000
#define CRED_FLAG_DISABLE_RECONNECTS            0x00004000  
#endif
