//+-------------------------------------------------------------------------
//
// Microsoft Windows
//
// Copyright (C) Microsoft Corporation, 1996 - 2001
//
// File:       scinfo.cpp
//
// Abstract:
//
// This application is used to provide a snapshot of the Calais (Smart Card
// Resource Manager) service's status, and to display certificates on smart
// cards via the common WinNT UI.
//
// SCInfo -- describes the RM status and displays each available sc cert(s)
//
// The following options are always enabled:
//	Readername	-- for just one reader
//	-sig		-- display signature key certs only
//	-ex		-- display exchange key certs only
//	-nocert		-- don't look for certs to display
//	-key		-- verify keyset public key matches cert public key
//
// Author: Amanda Matlosz (AMatlosz) 07/14/1998
//
// Environment: Win32 Console App
//
// Notes: For use in NT5 public key rollout testing
//
// Need to include the following libs:
//	winscard.lib
//
//--------------------------------------------------------------------------

#include <pch.cpp>

#pragma hdrstop

#include <winscard.h>
#include <winsvc.h>
#include <cryptui.h>

#include "cscsp.h"

#define __dwFILE__	__dwFILE_CERTUTIL_SCINFO_CPP__


#define wszSCARDSERVICE	L"SCardSvr"


//+-------------------------------------------------------------------------
// IsSCardSvrRunning checks the registry and queries the service for status
//--------------------------------------------------------------------------

HRESULT
IsSCardSvrRunning()
{
    HRESULT hr;
    WCHAR const *pwszError = NULL;
    UINT idmsg = IDS_SMARTCARD_NOTRUNNING; // "The Microsoft Smart Card Resource Manager is not running."

    HANDLE hSCardSvrStarted = NULL;
    HMODULE hDll = GetModuleHandle(L"winscard.dll");

    typedef HANDLE (WINAPI FNSCARDACCESSSTARTEDEVENT)(VOID);
    FNSCARDACCESSSTARTEDEVENT *pfnSCardAccessStartedEvent;

    pfnSCardAccessStartedEvent = (FNSCARDACCESSSTARTEDEVENT *)
			    GetProcAddress(hDll, "SCardAccessStartedEvent");

    if (NULL == pfnSCardAccessStartedEvent)
    {
	hr = myHLastError();
	pwszError = L"GetProcAddress";
	_JumpErrorStr(hr, error, "GetProcAddress", L"SCardAccessStartedEvent");
    }
    hSCardSvrStarted = (*pfnSCardAccessStartedEvent)();
    if (NULL == hSCardSvrStarted)
    {
	hr = myHLastError();
	if (S_OK == hr)
	{
	    hr = E_HANDLE;
	}
	pwszError = L"SCardAccessStartedEvent";
	_JumpError(hr, error, "SCardAccessStartedEvent");
    }
    hr = WaitForSingleObject(hSCardSvrStarted, 1000);
    if (WAIT_OBJECT_0 != hr)
    {
	hr = myHError(hr);
	pwszError = L"WaitForSingleObject";
	_JumpError(hr, error, "WaitForSingleObject");
    }
    idmsg = IDS_SMARTCARD_RUNNING; // "The Microsoft Smart Card Resource Manager is running."
    hr = S_OK;

error:

    // Display status

    wprintf(myLoadResourceString(idmsg));
    wprintf(wszNewLine);
    if (S_OK != hr)
    {
	// IDS_SERVICEPAUSED; // "Service is paused."
	// IDS_SERVICESTOPPED; // "Service is stopped."
	cuPrintErrorAndString(
		pwszError,
		IDS_SERVICEUNKNOWNSTATE, // "Service is in an unknown state."
		hr,
		NULL);
    }
    return(hr);
}


VOID
FreeReaderList(
    IN SCARDCONTEXT hSCard,
    IN WCHAR *pwszzReaderNameAlloc,
    IN SCARD_READERSTATE *prgReaderState)
{
    if (NULL != hSCard)
    {
	if (NULL != pwszzReaderNameAlloc)
	{
	    SCardFreeMemory(hSCard, pwszzReaderNameAlloc);
	}
        SCardReleaseContext(hSCard);
    }
    if (NULL != prgReaderState)
    {
	LocalFree(prgReaderState);
    }
}


//+-------------------------------------------------------------------------
// BuildReaderList tries to set *phSCard and get a list of currently available
// smart card readers.
//--------------------------------------------------------------------------

HRESULT
BuildReaderList(
    OPTIONAL IN WCHAR const *pwszReaderName,
    OUT SCARDCONTEXT *phSCard,
    OUT WCHAR **ppwszzReaderNameAlloc,
    OUT SCARD_READERSTATE **pprgReaderState,
    OUT DWORD *pcReaders)
{
    HRESULT hr;
    DWORD i;
    DWORD dwAutoAllocate;
    SCARDCONTEXT hSCard = NULL;
    WCHAR *pwszzReaderNameAlloc = NULL;
    SCARD_READERSTATE *prgReaderState = NULL;
    DWORD cReaders;

    *phSCard = NULL;
    *ppwszzReaderNameAlloc = NULL;
    *pcReaders = 0;
    *pprgReaderState = NULL;

    wprintf(myLoadResourceString(IDS_SCREADER_STATUS_COLON));
    wprintf(wszNewLine);

    // Acquire global SCARDCONTEXT from resource manager if possible

    hr = SCardEstablishContext(SCARD_SCOPE_USER, NULL, NULL, &hSCard);
    if (S_OK != hr)
    {
	cuPrintAPIError(L"SCardEstablishContext", hr);
	wprintf(myLoadResourceString(IDS_SC_USER_SCOPE));
	wprintf(wszNewLine);
	wprintf(myLoadResourceString(IDS_SC_NO_LIST));
	wprintf(wszNewLine);
	_JumpError(hr, error, "SCardEstablishContext");
    }

    // Build a readerstatus array from either a list of readers; or use the one
    // the user specified

    cReaders = 1;
    if (NULL == pwszReaderName)
    {
	dwAutoAllocate = SCARD_AUTOALLOCATE;
	hr = SCardListReaders(
			    hSCard,
			    SCARD_DEFAULT_READERS,
			    (WCHAR *) &pwszzReaderNameAlloc,
			    &dwAutoAllocate);
	if (S_OK != hr)
	{
	    cuPrintAPIError(L"SCardListReaders", hr);
	    wprintf(myLoadResourceString(IDS_SC_LIST_FAILED));
	    wprintf(wszNewLine);

	    if (SCARD_E_NO_READERS_AVAILABLE == hr)
	    {
		wprintf(myLoadResourceString(IDS_SC_LIST_FAILED_NO_READERS));
	    }
	    else
	    {
		wprintf(myLoadResourceString(IDS_SC_LIST_FAILED_GENERIC));
	    }
	    wprintf(wszNewLine);
	    _JumpError(hr, error, "SCardListReaders");
	}

	// Build a readerstatus array...

	cReaders = 0;
	if (NULL != pwszzReaderNameAlloc)
	{
	    for (
		pwszReaderName = pwszzReaderNameAlloc;
		L'\0' != *pwszReaderName;
		pwszReaderName += wcslen(pwszReaderName) + 1)
	    {
		cReaders++;
	    }
	}
	pwszReaderName = pwszzReaderNameAlloc;
    }
    prgReaderState = (SCARD_READERSTATE *) LocalAlloc(
				LMEM_FIXED | LMEM_ZEROINIT,
				cReaders * sizeof(**pprgReaderState));
    if (NULL == prgReaderState)
    {
	hr = E_OUTOFMEMORY;
	_JumpError(hr, error, "LocalAlloc");
    }

    wprintf(myLoadResourceString(IDS_SC_READERS_COLON));
    wprintf(L" %u\n", cReaders);
    for (i = 0; i < cReaders; i++)
    {
	wprintf(L"  %u: %ws\n", i, pwszReaderName);
	prgReaderState[i].szReader = const_cast<WCHAR *>(pwszReaderName);
	prgReaderState[i].dwCurrentState = SCARD_STATE_UNAWARE;
	pwszReaderName += wcslen(pwszReaderName) + 1;
    }

    // ...And get the reader status from the resource manager

    hr = SCardGetStatusChange(
			hSCard,
			INFINITE,	// hardly
			prgReaderState,
			cReaders);
    if (S_OK != hr)
    {
	cuPrintAPIError(L"SCardGetStatusChange", hr);
	_JumpError(hr, error, "SCardGetStatusChange");
    }
    *phSCard = hSCard;
    hSCard = NULL;

    *ppwszzReaderNameAlloc = pwszzReaderNameAlloc;
    pwszzReaderNameAlloc = NULL;

    *pprgReaderState = prgReaderState;
    prgReaderState = NULL;

    *pcReaders = cReaders;
    hr = S_OK;

error:
    FreeReaderList(hSCard, pwszzReaderNameAlloc, prgReaderState);
    return(hr);
}


//+-------------------------------------------------------------------------
// DisplayReaderList displays the status for a list of readers.
//--------------------------------------------------------------------------

HRESULT
DisplayReaderList(
    IN SCARDCONTEXT hSCard,
    IN SCARD_READERSTATE const *prgReaderState,
    IN DWORD cReaders)
{
    HRESULT hr;
    DWORD i;
    DWORD dwAutoAllocate;
    UINT idsMsg;

    // Display all reader information

    for (i = 0; i < cReaders; i++)
    {
	DWORD dwState;
	WCHAR const *pwszSep;
	static WCHAR const s_wszSep[] = L" | ";

	//--- reader: readerName

	wprintf(myLoadResourceString(IDS_SC_MINUS_READER_COLON));
	wprintf(L" %ws\n", prgReaderState[i].szReader);

	//--- status: /bits/

	wprintf(myLoadResourceString(IDS_SC_MINUS_STATUS_COLON));
	dwState = prgReaderState[i].dwEventState;

	pwszSep = L" ";
	if (SCARD_STATE_UNKNOWN & dwState)
	{
	    wprintf(L"%wsSCARD_STATE_UNKNOWN", pwszSep);
	    pwszSep = s_wszSep;
	}
	if (SCARD_STATE_UNAVAILABLE & dwState)
	{
	    wprintf(L"%wsSCARD_STATE_UNAVAILABLE", pwszSep);
	    pwszSep = s_wszSep;
	}
	if (SCARD_STATE_EMPTY & dwState)
	{
	    wprintf(L"%wsSCARD_STATE_EMPTY", pwszSep);
	    pwszSep = s_wszSep;
	}
	if (SCARD_STATE_PRESENT & dwState)
	{
	    wprintf(L"%wsSCARD_STATE_PRESENT", pwszSep);
	    pwszSep = s_wszSep;
	}
	if (SCARD_STATE_EXCLUSIVE & dwState)
	{
	    wprintf(L"%wsSCARD_STATE_EXCLUSIVE", pwszSep);
	    pwszSep = s_wszSep;
	}
	if (SCARD_STATE_INUSE & dwState)
	{
	    wprintf(L"%wsSCARD_STATE_INUSE", pwszSep);
	    pwszSep = s_wszSep;
	}
	if (SCARD_STATE_MUTE & dwState)
	{
	    wprintf(L"%wsSCARD_STATE_MUTE", pwszSep);
	    pwszSep = s_wszSep;
	}
	if (SCARD_STATE_UNPOWERED & dwState)
	{
	    wprintf(L"%wsSCARD_STATE_UNPOWERED", pwszSep);
	}
	wprintf(wszNewLine);

	//--- status: what scstatus would say

	// NO CARD

	if (SCARD_STATE_EMPTY & dwState)	// SC_STATUS_NO_CARD
	{
	    idsMsg = IDS_SC_STATUS_NO_CARD;
	}

	// CARD in reader: SHARED, EXCLUSIVE, FREE, UNKNOWN ?

	else
	if (SCARD_STATE_PRESENT & dwState)
	{
	    if (SCARD_STATE_MUTE & dwState)	// SC_STATUS_UNKNOWN
	    {
		idsMsg = IDS_SC_STATUS_UNKNOWN;
	    }
	    else
	    if (SCARD_STATE_INUSE & dwState)
	    {
		if (dwState & SCARD_STATE_EXCLUSIVE & dwState)
		{
		    // SC_STATUS_EXCLUSIVE

		    idsMsg = IDS_SC_STATUS_BUSY;
		}
		else				// SC_STATUS_SHARED
		{
		    idsMsg = IDS_SC_STATUS_SHARED;
		}
	    }
	    else				// SC_SATATUS_AVAILABLE
	    {
		idsMsg = IDS_SC_STATUS_AVAILABLE;
	    }
	}
	// READER ERROR: at this point, something's gone wrong
	else	// SCARD_STATE_UNAVAILABLE & dwState -- SC_STATUS_ERROR
	{

	    idsMsg = IDS_SC_STATUS_NO_RESPONSE;
	}
	wprintf(myLoadResourceString(IDS_SC_MINUS_STATUS_COLON));
	wprintf(L" ");
	wprintf(myLoadResourceString(idsMsg));
	wprintf(wszNewLine);

	// card name(s):

	wprintf(myLoadResourceString(IDS_SC_MINUS_CARD_COLON));
	if (0 < prgReaderState[i].cbAtr)
	{
	    WCHAR *pwszCardName = NULL;

	    // Get the name of the card

	    dwAutoAllocate = SCARD_AUTOALLOCATE;
	    hr = SCardListCards(
			    hSCard,
			    prgReaderState[i].rgbAtr,
			    NULL,
			    0,
			    (WCHAR *) &pwszCardName,
			    &dwAutoAllocate);
	    if (S_OK != hr || NULL == pwszCardName)
	    {
		wprintf(L" ");
		wprintf(myLoadResourceString(IDS_SC_UNKNOWN_CARD));
	    }
	    else
	    {
		WCHAR const *pwszName;

		pwszSep = L"";
		for (
		    pwszName = pwszCardName;
		    L'\0' != *pwszName;
		    pwszName += wcslen(pwszName) + 1)
		{
		    wprintf(L"%ws %ws", pwszSep, pwszName);
		    pwszSep = L",";
		}
	    }
	    if (NULL != pwszCardName)
	    {
		SCardFreeMemory(hSCard, pwszCardName);
	    }
	}
	wprintf(wszNewLine);
    }
    hr = S_OK;

//error:
    return(hr);
}


HRESULT
myCryptGetProvParamToUnicode(
    IN HCRYPTPROV hProv,
    IN DWORD dwParam,
    OUT WCHAR **ppwszOut,
    IN DWORD dwFlags)
{
    HRESULT hr;
    char *psz = NULL;
    DWORD cb;

    *ppwszOut = NULL;
    if (!CryptGetProvParam(hProv, dwParam, NULL, &cb, 0))
    {
	hr = myHLastError();
	_JumpError(hr, error, "CryptGetProvParam");
    }

    psz = (char *) LocalAlloc(LMEM_FIXED, cb);
    if (NULL == psz)
    {
	hr = E_OUTOFMEMORY;
	_JumpError(hr, error, "LocalAlloc");
    }
    if (!CryptGetProvParam(hProv, dwParam, (BYTE *) psz, &cb, 0))
    {
	hr = myHLastError();
	_JumpError(hr, error, "CryptGetProvParam");
    }

    if (!myConvertSzToWsz(ppwszOut, psz, -1))
    {
	hr = E_OUTOFMEMORY;
	_JumpError(hr, error, "myConvertSzToWsz");
    }
    hr = S_OK;

error:
    if (NULL != psz)
    {
        LocalFree(psz);
    }
    return(hr);
}


//+-------------------------------------------------------------------------
// GetCertContext -- called by DisplayCerts
//--------------------------------------------------------------------------

HRESULT
GetCertContext(
    IN HCRYPTPROV hProv,
    IN HCRYPTKEY hKey,
    IN DWORD dwKeySpec,
    IN WCHAR const *pwszKeyType,
    OUT CERT_CONTEXT const **ppCert)
{
    HRESULT hr;
    CERT_CONTEXT const *pCert = NULL;
    CERT_PUBLIC_KEY_INFO *pKey = NULL;
    CRYPT_KEY_PROV_INFO KeyProvInfo;
    WCHAR *pwszContainerName = NULL;
    WCHAR *pwszProvName = NULL;
    BYTE *pbCert = NULL;
    DWORD cbCert;
    DWORD cbKey;

    *ppCert = NULL;

    // Get the cert from this key

    if (!CryptGetKeyParam(hKey, KP_CERTIFICATE, NULL, &cbCert, 0))
    {
        hr = myHLastError();
        if (HRESULT_FROM_WIN32(ERROR_MORE_DATA) != hr)
        {
            _JumpError(hr, error, "CryptGetKeyParam");
        }
    }
    pbCert = (BYTE *) LocalAlloc(LMEM_FIXED, cbCert);
    if (NULL == pbCert)
    {
        hr = E_OUTOFMEMORY;
	_JumpError(hr, error, "LocalAlloc");
    }
    if (!CryptGetKeyParam(hKey, KP_CERTIFICATE, pbCert, &cbCert, 0))
    {
        hr = myHLastError();
	_JumpError(hr, error, "CryptGetKeyParam");
    }

    // Convert the certificate into a Cert Context.

    pCert = CertCreateCertificateContext(
				    X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
				    pbCert,
				    cbCert);
    if (NULL == pCert)
    {
        hr = myHLastError();
        _JumpError(hr, error, "CertCreateCertificateContext");
    }

    // Perform public key check

    wprintf(wszNewLine);
    wprintf(myLoadResourceString(IDS_FORMAT_SC_TESTING_MATCH), pwszKeyType);
    wprintf(wszNewLine);

    if (!CryptExportPublicKeyInfo(
		    hProv,
		    dwKeySpec,
		    X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
		    NULL,
		    &cbKey))		// in, out
    {
	hr = myHLastError();
	cuPrintAPIError(L"CryptExportPublicKeyInfo", hr);
	_JumpError(hr, error, "CryptExportPublicKeyInfo");
    }
    if (0 == cbKey)
    {
	hr = SCARD_E_UNEXPECTED;	// huh?
	wprintf(
	    myLoadResourceString(IDS_SC_SIZE_ZERO),
	    L"CryptExportPublicKeyInfo");
	wprintf(wszNewLine);
	_JumpError(hr, error, "zero info size");
    }

    pKey = (CERT_PUBLIC_KEY_INFO *) LocalAlloc(LMEM_FIXED, cbKey);
    if (NULL == pKey)
    {
	hr = E_OUTOFMEMORY;
	_JumpError(hr, error, "LocalAlloc");
    }

    if (!CryptExportPublicKeyInfo(
			    hProv,
			    dwKeySpec,
			    X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
			    pKey,
			    &cbKey))
    {
	hr = myHLastError();
	cuPrintAPIError(L"CryptExportPublicKeyInfo", hr);
	_JumpError(hr, error, "CryptExportPublicKeyInfo");
    }

    if (!CertComparePublicKeyInfo(
	  X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
	  pKey,			// from the private keyset
	  &pCert->pCertInfo->SubjectPublicKeyInfo))	// cert public key
    {
	wprintf(wszNewLine);
	wprintf(myLoadResourceString(IDS_SC_KEYPROVINFO_KEY));
	wprintf(wszNewLine);
	cuDumpPublicKey(pKey);

	wprintf(wszNewLine);
	wprintf(myLoadResourceString(IDS_SC_CERT_KEY));
	wprintf(wszNewLine);
	cuDumpPublicKey(&pCert->pCertInfo->SubjectPublicKeyInfo);

	// by design, CertComparePublicKeyInfo doesn't set last error!

	hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
	_JumpError(hr, error, "CertComparePublicKeyInfo");
    }
    wprintf(myLoadResourceString(IDS_SC_KEY_MATCHES));
    wprintf(wszNewLine);

    // Associate cryptprovider w/ the private key property of this cert
    // ... need the container name

    hr = myCryptGetProvParamToUnicode(
			hProv,
			PP_CONTAINER,
			&pwszContainerName,
			0);
    _JumpIfError(hr, error, "myCryptGetProvParamToUnicode");

    //  ... need the provider name

    hr = myCryptGetProvParamToUnicode(hProv, PP_NAME, &pwszProvName, 0);
    _JumpIfError(hr, error, "myCryptGetProvParamToUnicode");

    // Set the cert context properties to reflect the prov info

    KeyProvInfo.pwszContainerName = pwszContainerName;
    KeyProvInfo.pwszProvName = pwszProvName;
    KeyProvInfo.dwProvType = PROV_RSA_FULL;
    KeyProvInfo.dwFlags = CERT_SET_KEY_CONTEXT_PROP_ID;
    KeyProvInfo.cProvParam = 0;
    KeyProvInfo.rgProvParam = NULL;
    KeyProvInfo.dwKeySpec = dwKeySpec;

    if (!CertSetCertificateContextProperty(
				    pCert,
				    CERT_KEY_PROV_INFO_PROP_ID,
				    0,
				    (VOID *) &KeyProvInfo))
    {
        hr = myHLastError();

	// the cert's been incorrectly created -- scrap it.

	_JumpError(hr, error, "CertSetCertificateContextProperty");
    }
    hr = cuDumpCertKeyProviderInfo(g_wszPad2, pCert, NULL, NULL);
    _PrintIfError(hr, "cuDumpCertKeyProviderInfo");

    if (!g_fCryptSilent)
    {
	if (AT_SIGNATURE == dwKeySpec)
	{
	    hr = myValidateKeyForSigning(
				    hProv,
				    &pCert->pCertInfo->SubjectPublicKeyInfo,
				    CALG_SHA1);
	}
	else
	{
	    hr = myValidateKeyForEncrypting(
				    hProv,
				    &pCert->pCertInfo->SubjectPublicKeyInfo,
				    CALG_RC4);
	}
	if (S_OK != hr)
	{
	    if (SCARD_W_CANCELLED_BY_USER != hr)
	    {
		wprintf(myLoadResourceString(IDS_ERR_PRIVATEKEY_MISMATCH)); // "ERROR: Certificate public key does NOT match private key"
		wprintf(wszNewLine);
		//_JumpError(hr, error, "myValidateKeyForEncrypting");
		_PrintError(hr, "myValidateKeyForEncrypting");
	    }
	}
	else
	{
	    wprintf(myLoadResourceString(IDS_PRIVATEKEY_VERIFIES));
	    wprintf(wszNewLine);
	}
    }
    *ppCert = pCert;
    pCert = NULL;
    hr = S_OK;

error:
    if (NULL != pbCert)
    {
	LocalFree(pbCert);
    }
    if (NULL != pKey)
    {
	LocalFree(pKey);
    }
    if (NULL != pwszContainerName)
    {
        LocalFree(pwszContainerName);
    }
    if (NULL != pwszProvName)
    {
        LocalFree(pwszProvName);
    }
    if (NULL != pCert)
    {
	CertFreeCertificateContext(pCert);
    }
    return(hr);
}


//+-------------------------------------------------------------------------
// DisplayChainInfo -- This code verifies that the SC cert is valid.
// Uses identical code to KDC cert chaining engine.
//
// Author: Todds
//--------------------------------------------------------------------------

DWORD
DisplayChainInfo(
    IN CERT_CONTEXT const *pCert)
{
    HRESULT hr;
    CERT_CHAIN_PARA ChainParameters;
    char *pszSCUsage = szOID_KP_SMARTCARD_LOGON;
    CERT_CHAIN_CONTEXT const *pChainContext = NULL;
    DWORD VerifyState;

    ZeroMemory(&ChainParameters, sizeof(ChainParameters));
    ChainParameters.cbSize = sizeof(ChainParameters);
    ChainParameters.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
    ChainParameters.RequestedUsage.Usage.cUsageIdentifier = 1;
    ChainParameters.RequestedUsage.Usage.rgpszUsageIdentifier = &pszSCUsage;

    if (!CertGetCertificateChain(
                          HCCE_LOCAL_MACHINE,
                          pCert,
                          NULL,			// evaluate at current time
                          NULL,			// no additional stores
                          &ChainParameters,
                          CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT,
                          NULL,			// reserved
                          &pChainContext))
    {
        hr = myHLastError();
	cuPrintAPIError(L"CertGetCertificateChain", hr);
	_JumpError(hr, error, "CertGetCertificateChain");
    }
    if (CERT_TRUST_NO_ERROR != pChainContext->TrustStatus.dwErrorStatus)
    {
	wprintf(
	    L"CertGetCertificateChain(dwErrorStatus) = 0x%x\n",
	    pChainContext->TrustStatus.dwErrorStatus);
	wprintf(myLoadResourceString(IDS_SC_BAD_CHAIN));
	wprintf(wszNewLine);
    }
    else
    {
	wprintf(myLoadResourceString(IDS_SC_GOOD_CHAIN));
	wprintf(wszNewLine);
    }

    hr = cuVerifyCertContext(
			pCert,			// pCert
			NULL,			// hStoreCA
			1,			// cApplicationPolicies
			&pszSCUsage,		// apszApplicationPolicies
			0,			// cIssuancePolicies
			NULL,			// apszIssuancePolicies
			TRUE,			// fNTAuth
			&VerifyState);
    _JumpIfError(hr, error, "cuVerifyCertContext");

error:
    if (NULL != pChainContext)
    {
        CertFreeCertificateChain(pChainContext);
    }
    return(hr);
}


HRESULT
DisplayReaderCertAndKey(
    IN SCARD_READERSTATE const *pReaderState,
    IN HCRYPTPROV hProv,
    IN DWORD dwKeySpec,
    IN WCHAR const *pwszKeyType,
    IN WCHAR const *pwszCardName,
    IN WCHAR const *pwszCSPName)
{
    HRESULT hr;
    HCRYPTKEY hKey = NULL;
    CERT_CONTEXT const *pCert = NULL;
    DWORD cwc;
    WCHAR *pwszTitle = NULL;
    CRYPTUI_VIEWCERTIFICATE_STRUCT CertViewInfo;

    // Get the key

    if (!CryptGetUserKey(hProv, dwKeySpec, &hKey))
    {
	hr = myHLastError();
	cuPrintAPIError(L"CryptGetUserKey", hr);
	if (NTE_NO_KEY == hr)
	{
	    wprintf(
		myLoadResourceString(IDS_FORMAT_SC_NO_KEY_COLON),
		pwszKeyType);
	}
	else
	{
	    wprintf(
		myLoadResourceString(IDS_FORMAT_SC_CANNOT_OPEN_KEY_COLON),
		pwszKeyType);
	}
	wprintf(L" %ws\n", pReaderState->szReader);
	_JumpError2(hr, error, "CryptGetUserKey", NTE_NO_KEY);
    }

    // Get the cert for this key

    hr = GetCertContext(hProv, hKey, dwKeySpec, pwszKeyType, &pCert);
    if (S_OK != hr)
    {
	wprintf(
	    myLoadResourceString(IDS_FORMAT_SC_NO_CERT_COLON),
	    pwszKeyType);
	wprintf(L" %ws\n", pReaderState->szReader);
	_JumpError(hr, error, "GetCertContext");
    }

    // Attempt to build a certificate chain

    wprintf(wszNewLine);
    wprintf(myLoadResourceString(IDS_SC_VALIDATING_CHAIN));
    wprintf(wszNewLine);

    DisplayChainInfo(pCert);

    // call common UI to display Cert Context
    // (from cryptui.h (cryptui.dll))

    if (!g_fCryptSilent)
    {
	cwc = wcslen(pReaderState->szReader) +
		2 +
		wcslen(pwszKeyType);
	pwszTitle = (WCHAR *) LocalAlloc(
				    LMEM_FIXED,
				    (cwc + 1) * sizeof(WCHAR));
	if (NULL == pwszTitle)
	{
	    hr = E_OUTOFMEMORY;
	    _JumpError(hr, error, "LocalAlloc");
	}

	swprintf(
	    pwszTitle,
	    L"%ws: %ws",
	    pReaderState->szReader,
	    pwszKeyType);

	ZeroMemory(&CertViewInfo, sizeof(CertViewInfo));
	CertViewInfo.dwSize = sizeof(CertViewInfo);
	//CertViewInfo.hwndParent = NULL;
	CertViewInfo.szTitle = pwszTitle;
	CertViewInfo.dwFlags = CRYPTUI_DISABLE_EDITPROPERTIES |
				    CRYPTUI_DISABLE_ADDTOSTORE | 
				    CRYPTUI_ENABLE_REVOCATION_CHECKING;
	CertViewInfo.pCertContext = pCert;

	CryptUIDlgViewCertificate(&CertViewInfo, NULL);
	CertFreeCertificateContext(pCert);
    }
    wprintf(
	myLoadResourceString(IDS_FORMAT_SC_CERT_DISPLAYED_COLON),
	pwszKeyType);
    wprintf(L" %ws\n", pReaderState->szReader);
    hr = S_OK;

error:
    if (NULL != pwszTitle)
    {
	LocalFree(pwszTitle);
    }
    if (NULL != hKey)
    {
	CryptDestroyKey(hKey);
    }
    return(hr);
}


HRESULT
DisplayReaderCert(
    IN SCARDCONTEXT hSCard,
    IN SCARD_READERSTATE const *pReaderState)
{
    HRESULT hr;
    HRESULT hr2;
    DWORD dwAutoAllocate;
    WCHAR wszFQCN[256];
    HCRYPTPROV hProv = NULL;
    WCHAR *pwszCardName = NULL;
    WCHAR *pwszCSPName = NULL;

    if (0 >= pReaderState->cbAtr)
    {
	hr = S_OK;
	goto error;	// no point to do any more work on this reader
    }

    // Inform user of current test

    wprintf(L"\n=======================================================\n");
    wprintf(myLoadResourceString(IDS_SC_ANALYZING_CARD_COLON));
    wprintf(L" %ws\n", pReaderState->szReader);

    // Get the name of the card

    dwAutoAllocate = SCARD_AUTOALLOCATE;
    hr = SCardListCards(
		    hSCard,
		    pReaderState->rgbAtr,
		    NULL,			// rgguidInterfaces 
		    0,				// cguidInterfaceCount 
		    (WCHAR *) &pwszCardName,	// mszCards 
		    &dwAutoAllocate);		// pcchCards 
    _JumpIfError(hr, error, "SCardListCards");

    dwAutoAllocate = SCARD_AUTOALLOCATE;
    hr = SCardGetCardTypeProviderName(
				hSCard,
				pwszCardName,
				SCARD_PROVIDER_CSP,
				(WCHAR *) &pwszCSPName,
				&dwAutoAllocate);
    if (S_OK != hr)
    {
	cuPrintAPIError(L"SCardGetCardTypeProviderName", hr);
	wprintf(
	    myLoadResourceString(IDS_FORMAT_SC_CANNOT_GET_CSP),
	    pwszCardName);
	_JumpError(hr, error, "SCardGetCardTypeProviderName");
    }

    // Prepare FullyQualifiedContainerName for CryptAcquireContext call

    swprintf(wszFQCN, L"\\\\.\\%ws\\", pReaderState->szReader);

    if (!CryptAcquireContext(
			&hProv,
			wszFQCN,	// default container via reader
			pwszCSPName,
			PROV_RSA_FULL,
			g_fCryptSilent? CRYPT_SILENT : 0))
    {
	hr = myHLastError();
	wprintf(L"%ws:\n", pReaderState->szReader);
	wprintf(L"%ws:\n", pwszCSPName);
	cuPrintAPIError(L"CryptAcquireContext", hr);
	_JumpError(hr, error, "SCardGetCardTypeProviderName");
    }

    // Enumerate the keys user specified and display the certs...

    hr = DisplayReaderCertAndKey(
			pReaderState,
			hProv,
			AT_SIGNATURE,
			L"AT_SIGNATURE",
			pwszCardName,
			pwszCSPName);
    _PrintIfError2(hr, "DisplayReaderCertAndKey", NTE_NO_KEY);

    hr2 = DisplayReaderCertAndKey(
			pReaderState,
			hProv,
			AT_KEYEXCHANGE,
			L"AT_KEYEXCHANGE",
			pwszCardName,
			pwszCSPName);
    _PrintIfError2(hr2, "DisplayReaderCertAndKey", NTE_NO_KEY);
    if (S_OK == hr)
    {
	hr = hr2;
	hr2 = S_OK;
    }

    // ignore NTE_NO_KEY if the other key type exists or has a different error:

    if (NTE_NO_KEY == hr)
    {
	hr = hr2;
	_PrintIfError2(hr, "DisplayReaderCertAndKey", NTE_NO_KEY);
    }

error:
    if (NULL != pwszCSPName)
    {
	SCardFreeMemory(hSCard, pwszCSPName);
    }
    if (NULL != pwszCardName)
    {
	SCardFreeMemory(hSCard, pwszCardName);
    }
    if (NULL != hProv)
    {
	CryptReleaseContext(hProv, 0);
    }
    return(hr);
}


//+-------------------------------------------------------------------------
// DisplayCerts
//--------------------------------------------------------------------------

HRESULT
DisplayCerts(
    IN SCARDCONTEXT hSCard,
    IN SCARD_READERSTATE const *prgReaderState,
    IN DWORD cReaders)
{
    HRESULT hr;
    HRESULT hr2;
    DWORD i;

    // For each reader that has a card, load the CSP and display the cert

    hr = S_OK;
    for (i = 0; i < cReaders; i++)
    {
	hr2 = DisplayReaderCert(hSCard, &prgReaderState[i]);
	_PrintIfError(hr2, "DisplayReaderCert");
	if (S_OK == hr)
	{
	    hr = hr2;
	}
    }
    return(hr);
}


//+-------------------------------------------------------------------------
// verbSCInfo -- This is the main entry point for the smart card test program.
// Nice and simple, borrowed from DBarlow
//
// Author: Doug Barlow (dbarlow) 11/10/1997
//
// Revisions:
// 	AMatlosz 2/26/98
//--------------------------------------------------------------------------

HRESULT
verbSCInfo(
    IN WCHAR const *pwszOption,
    OPTIONAL IN WCHAR const *pwszReaderName,
    IN WCHAR const *pwszArg2,
    IN WCHAR const *pwszArg3,
    IN WCHAR const *pwszArg4)
{
    HRESULT hr;
    HRESULT hr2;
    SCARDCONTEXT hSCard = NULL;
    WCHAR *pwszzReaderNameAlloc = NULL;
    SCARD_READERSTATE *prgReaderState = NULL;
    DWORD cReaders;
    
    hr = IsSCardSvrRunning();
    _JumpIfError(hr, error, "IsSCardSvrRunning");

    hr = BuildReaderList(
		    pwszReaderName,
		    &hSCard,
		    &pwszzReaderNameAlloc,
		    &prgReaderState,
		    &cReaders);
    _PrintIfError(hr, "BuildReaderList");

    hr2 = DisplayReaderList(hSCard, prgReaderState, cReaders);
    _PrintIfError(hr2, "DisplayReaderList");
    if (S_OK == hr)
    {
	hr = hr2;
    }

    hr2 = DisplayCerts(hSCard, prgReaderState, cReaders);
    _PrintIfError(hr2, "DisplayCerts");
    if (S_OK == hr)
    {
	hr = hr2;
    }
    wprintf(wszNewLine);
    wprintf(myLoadResourceString(IDS_DONE));
    wprintf(wszNewLine);

error:
    FreeReaderList(hSCard, pwszzReaderNameAlloc, prgReaderState);
    return(hr);
}
