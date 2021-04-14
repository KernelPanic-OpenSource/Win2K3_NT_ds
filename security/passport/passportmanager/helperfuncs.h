/**********************************************************************/
/**                       Microsoft Passport                         **/
/**                Copyright(c) Microsoft Corporation, 1999 - 2001   **/
/**********************************************************************/

/*
    HelperFuncs.h
      defines helper functions for passport manager object


    FILE HISTORY:

*/
    
// HelperFuncs.h : Useful functions

#ifndef __HELPERFUNCS_H_
#define __HELPERFUNCS_H_

#include <httpfilt.h>
#include <httpext.h>
#include "nsconst.h"
#include "passport.h"
#include "smartcls.h"

typedef enum { PM_LOGOTYPE_SIGNIN, PM_LOGOTYPE_SIGNOUT } PM_LOGOTYPE;

#define  SECURELEVEL_USE_HTTPS(n)   (n >= k_iSeclevelSecureChannel)

BSTR
FormatNormalLogoTag(
    LPCWSTR pszLoginServerURL,
    ULONG   ulSiteId,
    LPCWSTR pszReturnURL,
    ULONG   ulTimeWindow,
    BOOL    bForceLogin,
    ULONG   ulCurrentCryptVersion,
    time_t  tCurrentTime,
    LPCWSTR pszCoBrand,
    LPCWSTR pszImageURL,
    LPCWSTR pszNameSpace,
    int     nKPP,
    PM_LOGOTYPE nLogoType,
    USHORT  lang,
    ULONG   ulSecureLevel,
    CRegistryConfig* pCRC,
    BOOL    fRedirToSelf,
    BOOL    bCreateTPF
    
    );

BSTR
FormatUpdateLogoTag(
    LPCWSTR pszLoginServerURL,
    ULONG   ulSiteId,
    LPCWSTR pszReturnURL,
    ULONG   ulTimeWindows,
    BOOL    bForceLogin,
    ULONG   ulCurrentKeyVersion,
    time_t  tCurrentTime,
    LPCWSTR pszCoBrand,
    int     nKPP,
    LPCWSTR pszUpdateServerURL,
    BOOL    bSecure,
    LPCWSTR pszProfileUpdate,
    PM_LOGOTYPE nLogoType,
    ULONG   ulSecureLevel,
    CRegistryConfig* pCRC,
    BOOL  bCreateTPF
    );

BSTR
FormatAuthURL(
    LPCWSTR pszLoginServerURL,
    ULONG   ulSiteId,
    LPCWSTR pszReturnURL,
    ULONG   ulTimeWindow,
    BOOL    bForceLogin,
    ULONG   ulCurrentKeyVersion,
    time_t  tCurrentTime,
    LPCWSTR pszCoBrand,
    LPCWSTR pszNameSpace,
    int     nKPP,
    USHORT  lang,
    ULONG   ulSecureLevel,
    CRegistryConfig* pCRC,
    BOOL    fRedirToSelf,
    BOOL    bCreateTPF
    );

BOOL
GetQueryData(
    LPCSTR   pszQueryString,
    BSTR*   pbstrTicket,
    BSTR*   pbstrProfile,
    BSTR*   pbstrFlags
    );

BOOL
GetCookie(
    LPCSTR   pszCookieHeader,
    LPCSTR   pszCookieName,
    BSTR*   pbstrCookieVal
    );

BOOL
BuildCookieHeaders(
    LPCSTR  pszTicket,
    LPCSTR  pszProfile,
    LPCSTR  pszConsent,
    LPCSTR  pszSecure,
    LPCSTR  pszTicketDomain,
    LPCSTR  pszTicketPath,
    LPCSTR  pszConsentDomain,
    LPCSTR  pszConsentPath,
    LPCSTR  pszSecuredomain,
    LPCSTR  pszSecurePath,
    BOOL    bSave,
    LPSTR   pszBuf,
    LPDWORD pdwBufLen,
    bool    bHTTPOnly
    );

HRESULT
DecryptTicketAndProfile(
    BSTR                bstrTicket,
    BSTR                bstrProfile,
    BOOL                bCheckConsent,
    BSTR                bstrConsent,
    CRegistryConfig*    pRegistryConfig,
    IPassportTicket*    piTicket,
    IPassportProfile*   piProfile
    );

HRESULT
DoSecureCheck(
    BSTR                bstrSecure,
    CRegistryConfig*    pRegistryConfig,
    IPassportTicket*    piTicket
    );

HRESULT
GetSiteNamePFC(
    PHTTP_FILTER_CONTEXT    pfc,
    LPSTR                   szBuf,
    LPDWORD                 lpdwBufLen
    );

HRESULT
GetSiteNameECB(
    EXTENSION_CONTROL_BLOCK*    pECB,
    LPSTR                       szBuf,
    LPDWORD                     lpdwBufLen
    );

LPSTR
GetServerVariableECB(
    EXTENSION_CONTROL_BLOCK*    pECB,
    LPSTR                       pszHeader
    );

LPSTR
GetServerVariablePFC(
    PHTTP_FILTER_CONTEXT    pPFC,
    LPSTR                   pszHeader
    );

int GetRawHeaders(LPCSTR headers, LPCSTR* names, LPCSTR* values, DWORD* dwSizes, DWORD namescount);
LPCSTR GetRawQueryString(LPCSTR headers, DWORD* dwSize);


LONG
FromHex(
    LPCWSTR     pszHexString
    );

//  max sizes for URLs (without qs) and with
//  these could be a bit opportunistic
#define MAX_URL_LENGTH      2048
#define MAX_QS_LENGTH       2048
#define PP_MAX_ATTRIBUTE_LENGTH MAX_URL_LENGTH
#define PPSITE_CHALLENGE   L"msppchlg=1"
#define PPSITE_CHALLENGE_A  "msppchlg=1"
#define PPLOGIN_PARAM      L"mspplogin="
#define PPLOGIN_PARAM_A     "mspplogin="

//  identification string for the auth method
#define PASSPORT_PROT14_A    "Passport1.4"
#define PASSPORT_PROT14     L"Passport1.4"

#define C_PPAUTH_INFO_HEADER  "Authentication-Info"
#define W_PPAUTH_INFO_HEADER L"Authentication-Info"

// cookie names for Tweener
#define C_PPCOOKIE_NAMES     "tname=MSPAuth,tname=MSPProf,tname=MSPConsent,tname=MSPSecAuth"
#define W_PPCOOKIE_NAMES    L"tname=MSPAuth,tname=MSPProf,tname=MSPConsent,tname=MSPSecAuth"
#define C_AUTH_INFO_HEADER_PASSPORT C_PPAUTH_INFO_HEADER ": " PASSPORT_PROT14_A " " C_PPCOOKIE_NAMES "\r\n"


PWSTR
FormatAuthURLParameters(
    LPCWSTR pszLoginServerURL,
    ULONG   ulSiteId,
    LPCWSTR pszReturnURL,
    ULONG   ulTimeWindow,
    BOOL    bForceLogin,
    ULONG   ulCurrentKeyVersion,
    time_t  tCurrentTime,
    LPCWSTR pszCoBrand,
    LPCWSTR pszNameSpace,
    int     nKPP,
    PWSTR   pszBufStart,
    ULONG   cBufLen,
    USHORT  lang,
    ULONG   ulSecureLevel,
    CRegistryConfig* pCRC,
    BOOL    fRedirectToSelf,
    BOOL    bCreateTPF
    );

HRESULT SignQueryString(
    CRegistryConfig* pCRC,
    ULONG   ulCurrentKeyVersion,
    LPWSTR  pszBufStart,
    LPWSTR& pszCurrent,
    LPCWSTR pszBufEnd,
    BOOL    bCreateTPF
    );

HRESULT PartnerHash(
    CRegistryConfig* pCRC,
    ULONG   ulCurrentKeyVersion,
    LPCWSTR tobeSigned,
    ULONG   nChars,
    BSTR*   pbstrHash);


#endif
