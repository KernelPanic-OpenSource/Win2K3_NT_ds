//+--------------------------------------------------------------------------
//
// Microsoft Windows
// Copyright (C) Microsoft Corporation, 1996 - 1999
//
// File:        crypt.cpp
//
// Contents:    Cert Server wrapper routines
//
//---------------------------------------------------------------------------

#include <pch.cpp>

#pragma hdrstop

#include <stdlib.h>
#include <winldap.h>

#include "csdisp.h"
#include "cscsp.h"
#include "csprop.h"
#include "csber.h"
#include "csldap.h"

#define __dwFILE__	__dwFILE_CERTLIB_CRYPT_CPP__


HRESULT
myGenerateKeys(
    IN WCHAR const *pwszContainer,
    OPTIONAL IN WCHAR const *pwszProvName,
    IN DWORD dwFlags,
    IN BOOL fMachineKeySet,
    IN DWORD dwKeySpec,
    IN DWORD dwProvType,
    IN DWORD dwKeySize,
    OUT HCRYPTPROV *phProv)
{
    HRESULT hr;
    HCRYPTKEY hKey = NULL;

    *phProv = NULL;

    if (fMachineKeySet)
    {
	dwFlags |= CRYPT_MACHINE_KEYSET;
    }

    // see if the container already exists

    if (CryptAcquireContext(
			phProv,
			pwszContainer,
			pwszProvName,
			dwProvType,
			dwFlags))
    {
	if (NULL != *phProv)
	{
	    CryptReleaseContext(*phProv, 0);
	    *phProv = NULL;
	}

        // container exists -- remove old keys and generate new ones.

        if (!CryptAcquireContext(
			    phProv,
			    pwszContainer,
			    pwszProvName,
			    dwProvType,
			    CRYPT_DELETEKEYSET | dwFlags))
        {
            hr = myHLastError();
            _JumpError(hr, error, "CryptAcquireContextEx");
        }
    }

    // create new container

    if (!CryptAcquireContext(
                        phProv,
                        pwszContainer,
			pwszProvName,
                        dwProvType,
                        CRYPT_NEWKEYSET | dwFlags)) // force new container
    {
        hr = myHLastError();
	_JumpError(hr, error, "CryptAcquireContextEx");
    }

    // create keys

    if (!CryptGenKey(*phProv, dwKeySpec, dwKeySize << 16, &hKey))
    {
        hr = myHLastError();
        _JumpError(hr, error, "CryptGenKey");
    }
    hr = S_OK;

error:
    if (NULL != hKey)
    {
        CryptDestroyKey(hKey);
    }
    return(hr);
}


HRESULT
myCryptExportPrivateKey(
    IN HCRYPTKEY hKey,
    OUT BYTE **ppbKey,
    OUT DWORD *pcbKey)
{
    HRESULT hr;
    BYTE *pbKey = NULL;

    *ppbKey = NULL;

    // export the key set to a CAPI blob

    if (!CryptExportKey(hKey, NULL, PRIVATEKEYBLOB, 0, NULL, pcbKey))
    {
	hr = myHLastError();
	_JumpError2(hr, error, "CryptExportKey", NTE_BAD_KEY_STATE);
    }

    pbKey = (BYTE *) LocalAlloc(LMEM_FIXED, *pcbKey);
    if (NULL == pbKey)
    {
	hr = E_OUTOFMEMORY;
	_JumpError(hr, error, "LocalAlloc");
    }

    if (!CryptExportKey(hKey, NULL, PRIVATEKEYBLOB, 0, pbKey, pcbKey))
    {
	hr = myHLastError();
	_JumpError(hr, error, "CryptExportKey");
    }

    *ppbKey = pbKey;
    pbKey = NULL;
    hr = S_OK;

error:
    if (NULL != pbKey)
    {
	LocalFree(pbKey);
    }
    return(hr);
}


HRESULT
myVerifyPublicKeyFromHProv(
    IN HCRYPTPROV hProv,
    IN DWORD dwKeySpec,
    IN OPTIONAL CERT_CONTEXT const *pCert,
    IN BOOL fV1Cert,
    IN OPTIONAL CERT_PUBLIC_KEY_INFO const *pPublicKeyInfo,
    OPTIONAL OUT BOOL *pfMatchingKey)
{
    HRESULT hr;
    DWORD cb;
    CERT_PUBLIC_KEY_INFO *pPublicKeyInfoExported = NULL;

    if (NULL != pfMatchingKey)
    {
	*pfMatchingKey = FALSE;
    }
    if (!myCryptExportPublicKeyInfo(
				hProv,
				dwKeySpec,
				CERTLIB_USE_LOCALALLOC,
				&pPublicKeyInfoExported,
				&cb))
    {
	hr = myHLastError();
	_JumpError(hr, error, "myCryptExportPublicKeyInfo");
    }

    if (NULL == pPublicKeyInfo)
    {
	if (NULL == pCert)
	{
	    hr = E_POINTER;
	    _JumpError(hr, error, "No cert & no SubjectPublicKeyInfo");
	}
	pPublicKeyInfo = &pCert->pCertInfo->SubjectPublicKeyInfo;
    }
    if (!myCertComparePublicKeyInfo(
			    X509_ASN_ENCODING,
			    fV1Cert,
			    pPublicKeyInfoExported,
			    pPublicKeyInfo))
    {
	// by design, (my)CertComparePublicKeyInfo doesn't set last error!

	hr = NTE_BAD_KEY;
	_JumpError(hr, error, "myCertComparePublicKeyInfo");
    }
    if (NULL != pfMatchingKey)
    {
	*pfMatchingKey = TRUE;
    }
    hr = S_OK;

error:
    if (NULL != pPublicKeyInfoExported)
    {
	LocalFree(pPublicKeyInfoExported);
    }
    return(hr);
}


HRESULT
myVerifyPublicKey(
    IN OPTIONAL CERT_CONTEXT const *pCert,
    IN BOOL fV1Cert,
    IN OPTIONAL CRYPT_KEY_PROV_INFO const *pKeyProvInfo,
    IN OPTIONAL CERT_PUBLIC_KEY_INFO const *pPublicKeyInfo,
    OPTIONAL OUT BOOL *pfMatchingKey)
{
    HRESULT hr;
    HCRYPTPROV hProv = NULL;
    DWORD dwKeySpec;

    if (NULL != pfMatchingKey)
    {
	*pfMatchingKey = FALSE;
    }
    if (NULL == pCert ||
	!CryptAcquireCertificatePrivateKey(
				    pCert,
				    0,		// dwFlags
				    NULL,	// pvReserved
				    &hProv,
				    &dwKeySpec,
				    NULL))	// pfCallerFreeProv
    {
        if (NULL != pKeyProvInfo)
        {
            // ok, try passed kpi

            if (!myCertSrvCryptAcquireContext(
		&hProv,
		pKeyProvInfo->pwszContainerName,
		pKeyProvInfo->pwszProvName,
		pKeyProvInfo->dwProvType,
		~CRYPT_MACHINE_KEYSET & pKeyProvInfo->dwFlags,
		(CRYPT_MACHINE_KEYSET & pKeyProvInfo->dwFlags)? TRUE : FALSE))
	    {
                hr = myHLastError();
                _JumpErrorStr(
			hr,
			error,
			"myCertSrvCryptAcquireContextEx",
			pKeyProvInfo->pwszContainerName);
            }
            dwKeySpec = pKeyProvInfo->dwKeySpec;
        }
        else if (NULL != pCert)
	{
            hr = myHLastError();
            _JumpError(hr, error, "CryptAcquireCertificatePrivateKey");
	}
	else
        {
	    hr = E_POINTER;
	    _JumpError(hr, error, "No cert & no KeyProvInfo");
        }
    }
    hr = myVerifyPublicKeyFromHProv(
			    hProv,
			    dwKeySpec,
			    pCert,
			    fV1Cert,
			    pPublicKeyInfo,
			    pfMatchingKey);
    _JumpIfError(hr, error, "myVerifyPublicKeyFromHProv");

error:
    if (NULL != hProv)
    {
	CryptReleaseContext(hProv, 0);
    }
    return(hr);
}


DWORD
myASNGetDataIndex(
    IN BYTE bBERTag,
    IN DWORD iStart,
    IN BYTE const *pb,
    IN DWORD cb,
    OUT DWORD *pdwLen)
{
    DWORD iData = MAXDWORD;

    pb += iStart;
    if (iStart + 2 <= cb && bBERTag == pb[0])
    {
	DWORD cbLen = 0;

	if (0x7f >= pb[1])
	{
	    *pdwLen = pb[1];
	    cbLen = 1;
	}
	else if (iStart + 3 <= cb && 0x81 == pb[1])
	{
	    *pdwLen = pb[2];
	    cbLen = 2;
	}
	else if (iStart + 4 <= cb && 0x82 == pb[1])
	{
	    *pdwLen = (pb[2] << 8) | pb[3];
	    cbLen = 3;
	}
	if (0 != cbLen &&
	    iStart + cbLen + *pdwLen <= cb)
	{
	    iData = iStart + 1 + cbLen;
	}
    }
    return(iData);
}


DWORD
myASNStoreLength(
    IN DWORD dwLen,
    IN OUT BYTE *pb,
    IN DWORD cb)
{
    DWORD cbLen = MAXDWORD;

    if (1 <= cb && 0x7f >= dwLen)
    {
	pb[0] = (BYTE) dwLen;
	cbLen = 1;
    }
    else if (2 <= cb && 0xff >= dwLen)
    {
	pb[0] = 0x81;
	pb[1] = (BYTE) dwLen;
	cbLen = 2;
    }
    else if (3 <= cb && 0xffff >= dwLen)
    {
	pb[0] = 0x82;
	pb[1] = (BYTE) (dwLen >> 8);
	pb[2] = (BYTE) dwLen;
	cbLen = 3;
    }
    return(cbLen);
}


// If this is a valid public key with a missing leading zero byte before the
// sign bit set in the first public key byte, add a zero byte and increment
// the lengths.  This canonicalizes the old incorrect V1 public key encoding
// used in EPF files.

HRESULT
myCanonicalizePublicKey(
    IN BYTE const *pbKeyIn,
    IN DWORD cbKeyIn,
    OUT BYTE **ppbKeyOut,
    OUT DWORD *pcbKeyOut)
{
    HRESULT hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    DWORD iDataSequence;
    DWORD dwLenSequence;
    DWORD iDataModulus;
    DWORD dwLenModulus;
    BYTE *pbKeyOut = NULL;
    DWORD cbKeyOut;
    BYTE *pb;
    DWORD cb;
    DWORD cbLen;

    *ppbKeyOut = NULL;

    iDataSequence = myASNGetDataIndex(
			    BER_SEQUENCE,
			    0,
			    pbKeyIn,
			    cbKeyIn,
			    &dwLenSequence);
    if (MAXDWORD == iDataSequence)
    {
	_JumpError(hr, error, "iDataSequence");
    }
    iDataModulus = myASNGetDataIndex(
			    BER_INTEGER,
			    iDataSequence,
			    pbKeyIn,
			    cbKeyIn,
			    &dwLenModulus);
    if (MAXDWORD == iDataModulus)
    {
	_JumpError(hr, error, "iDataModulus");
    }
    if (iDataSequence + dwLenSequence == cbKeyIn - 1 &&
	1 == pbKeyIn[cbKeyIn - 1])
    {
	cbKeyIn--;
    }
    if (0 == (0x80 & pbKeyIn[iDataModulus]))
    {
	_JumpError2(hr, error, "key sign", hr);
    }
    cbKeyOut = cbKeyIn + 1;
    pbKeyOut = (BYTE *) LocalAlloc(LMEM_FIXED, cbKeyOut);
    if (NULL == pbKeyOut)
    {
	hr = E_OUTOFMEMORY;
	_JumpError(hr, error, "LocalAlloc");
    }

    pb = pbKeyOut;
    cb = cbKeyOut;

    // Set the sequence tag and new length:

    *pb++ = BER_SEQUENCE;
    cb--;
    cbLen = myASNStoreLength(dwLenSequence + 1, pb, cb);
    if (MAXDWORD == cbLen)
    {
	_JumpError(hr, error, "new dwLenSequence");
    }
    pb += cbLen;
    cb -= cbLen;

    // Set the modulus tag and new length:

    *pb++ = BER_INTEGER;
    cb--;
    cbLen = myASNStoreLength(dwLenModulus + 1, pb, cb);
    if (MAXDWORD == cbLen)
    {
	_JumpError(hr, error, "new dwLenModulus");
    }
    pb += cbLen;
    cb -= cbLen;

    *pb++ = 0;
    cb--;

    if (cb != cbKeyIn - (iDataModulus))
    {
	// crossed an encoding length boundary -- shouldn't happen!
	// new and old sequence and modulus lengths expected:
	//   0x10a <== 0x109, 0x101 <== 0x100
	//   0x89  <== 0x88,  0x81  <== 0x80
	//   0x48  <== 0x47,  0x41  <== 0x40

	_JumpError(hr, error, "new key length");
    }
    CopyMemory(pb, &pbKeyIn[iDataModulus], cb);

    *pcbKeyOut = cbKeyOut;
    *ppbKeyOut = pbKeyOut;
    pbKeyOut = NULL;
    hr = S_OK;

error:
    if (NULL != pbKeyOut)
    {
	LocalFree(pbKeyOut);
    }
    return(hr);
}


// If this is a valid public key with a proper leading zero byte before the
// sign bit set in the next public key byte, remove the zero byte and decrement
// the lengths.  This conforms to the old incorrect V1 public key encoding
// used in EPF files.

HRESULT
mySqueezePublicKey(
    IN BYTE const *pbKeyIn,
    IN DWORD cbKeyIn,
    OUT BYTE **ppbKeyOut,
    OUT DWORD *pcbKeyOut)
{
    HRESULT hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    DWORD iDataSequence;
    DWORD dwLenSequence;
    DWORD iDataModulus;
    DWORD dwLenModulus;
    BYTE *pbKeyOut = NULL;
    DWORD cbKeyOut;
    BYTE *pb;
    DWORD cb;
    DWORD cbLen;

    *ppbKeyOut = NULL;

    iDataSequence = myASNGetDataIndex(
			    BER_SEQUENCE,
			    0,
			    pbKeyIn,
			    cbKeyIn,
			    &dwLenSequence);
    if (MAXDWORD == iDataSequence)
    {
	_JumpError(hr, error, "iDataSequence");
    }
    iDataModulus = myASNGetDataIndex(
			    BER_INTEGER,
			    iDataSequence,
			    pbKeyIn,
			    cbKeyIn,
			    &dwLenModulus);
    if (MAXDWORD == iDataModulus)
    {
	_JumpError(hr, error, "iDataModulus");
    }
    if (0 != pbKeyIn[iDataModulus] ||
	0 == (0x80 & pbKeyIn[iDataModulus + 1]))
    {
	_JumpError(hr, error, "key sign");
    }
    cbKeyOut = cbKeyIn - 1;
    pbKeyOut = (BYTE *) LocalAlloc(LMEM_FIXED, cbKeyOut);
    if (NULL == pbKeyOut)
    {
	hr = E_OUTOFMEMORY;
	_JumpError(hr, error, "LocalAlloc");
    }

    pb = pbKeyOut;
    cb = cbKeyOut;

    // Set the sequence tag and new length:

    *pb++ = BER_SEQUENCE;
    cb--;
    cbLen = myASNStoreLength(dwLenSequence - 1, pb, cb);
    if (MAXDWORD == cbLen)
    {
	_JumpError(hr, error, "new dwLenSequence");
    }
    pb += cbLen;
    cb -= cbLen;

    // Set the modulus tag and new length:

    *pb++ = BER_INTEGER;
    cb--;
    cbLen = myASNStoreLength(dwLenModulus - 1, pb, cb);
    if (MAXDWORD == cbLen)
    {
	_JumpError(hr, error, "new dwLenModulus");
    }
    pb += cbLen;
    cb -= cbLen;

    if (cb != cbKeyIn - (iDataModulus + 1))
    {
	// crossed an encoding length boundary -- shouldn't happen!
	// new and old sequence and modulus lengths expected:
	//   0x10a ==> 0x109, 0x101 ==> 0x100
	//   0x89  ==> 0x88,  0x81  ==> 0x80
	//   0x48  ==> 0x47,  0x41  ==> 0x40

	_JumpError(hr, error, "new key length");
    }
    CopyMemory(pb, &pbKeyIn[iDataModulus + 1], cb);

    *pcbKeyOut = cbKeyOut;
    *ppbKeyOut = pbKeyOut;
    pbKeyOut = NULL;
    hr = S_OK;

error:
    if (NULL != pbKeyOut)
    {
	LocalFree(pbKeyOut);
    }
    return(hr);
}


// by design, (my)CertComparePublicKeyInfo doesn't set last error!

BOOL
myCertComparePublicKeyInfo(
    IN DWORD dwCertEncodingType,
    IN BOOL fV1Cert,
    IN CERT_PUBLIC_KEY_INFO const *pPubKey1,
    IN CERT_PUBLIC_KEY_INFO const *pPubKey2)
{
    BOOL fMatch = FALSE;
    BYTE *pbKeyNew = NULL;

    if (CertComparePublicKeyInfo(
			dwCertEncodingType,
			const_cast<CERT_PUBLIC_KEY_INFO *>(pPubKey1),
			const_cast<CERT_PUBLIC_KEY_INFO *>(pPubKey2)))
    {
	fMatch = TRUE;
	goto error;
    }

#if 0
    wprintf(L"pPubKey1:\n");
    DumpHex(
	DH_NOADDRESS | DH_NOTABPREFIX | 8,
	pPubKey1->PublicKey.pbData,
	pPubKey1->PublicKey.cbData);

    wprintf(L"pPubKey2:\n");
    DumpHex(
	DH_NOADDRESS | DH_NOTABPREFIX | 8,
	pPubKey2->PublicKey.pbData,
	pPubKey2->PublicKey.cbData);
#endif

    // If this is a V1 X509 cert with the sign bit set in the first public key
    // byte -- without a leading zero pad byte, and there's a wasted byte at
    // the end of the public key, squeeze out the leading zero byte from the
    // correctly encoded key, and compare the result.

    if (fV1Cert &&
	(pPubKey1->PublicKey.cbData == pPubKey2->PublicKey.cbData ||
	 pPubKey1->PublicKey.cbData == pPubKey2->PublicKey.cbData + 1))
    {
	HRESULT hr;
	CERT_PUBLIC_KEY_INFO PubKey1;
	CERT_PUBLIC_KEY_INFO PubKey2;

	PubKey1 = *pPubKey1;
	PubKey2 = *pPubKey2;

	hr = mySqueezePublicKey(
			PubKey1.PublicKey.pbData,
			PubKey1.PublicKey.cbData,
			&pbKeyNew,
			&PubKey1.PublicKey.cbData);
	_JumpIfError(hr, error, "mySqueezePublicKey");

	//PubKey1.PublicKey.cbData--;
	PubKey1.PublicKey.pbData = pbKeyNew;
	if (2 < PubKey2.PublicKey.cbData &&
	    PubKey2.PublicKey.cbData == 1 + PubKey1.PublicKey.cbData &&
	    1 == PubKey2.PublicKey.pbData[PubKey2.PublicKey.cbData - 1] &&
	    1 == PubKey2.PublicKey.pbData[PubKey2.PublicKey.cbData - 2])
	{
	    PubKey2.PublicKey.cbData--;
	}
#if 0
	wprintf(L"PubKey1:\n");
	DumpHex(
	    DH_NOADDRESS | DH_NOTABPREFIX | 8,
	    PubKey1.PublicKey.pbData,
	    PubKey1.PublicKey.cbData);

	wprintf(L"PubKey2:\n");
	DumpHex(
	    DH_NOADDRESS | DH_NOTABPREFIX | 8,
	    PubKey2.PublicKey.pbData,
	    PubKey2.PublicKey.cbData);
#endif
	if (CertComparePublicKeyInfo(
			    dwCertEncodingType,
			    &PubKey1,
			    &PubKey2))
	{
	    fMatch = TRUE;
	}
    }

error:
    if (NULL != pbKeyNew)
    {
	LocalFree(pbKeyNew);
    }
    return(fMatch);
}


BOOL
myCryptSignMessage(
    IN CRYPT_SIGN_MESSAGE_PARA const *pcsmp,
    IN BYTE const *pbToBeSigned,
    IN DWORD cbToBeSigned,
    IN CERTLIB_ALLOCATOR allocType,
    OUT BYTE **ppbSignedBlob,
    OUT DWORD *pcbSignedBlob)
{
    BOOL b;

    *ppbSignedBlob = NULL;
    *pcbSignedBlob = 0;
    for (;;)
    {
	b = CryptSignMessage(
			const_cast<CRYPT_SIGN_MESSAGE_PARA *>(pcsmp),
			TRUE,			// fDetachedSignature
			1,			// cToBeSigned
			&pbToBeSigned,		// rgpbToBeSigned
			&cbToBeSigned,		// rgcbToBeSigned
			*ppbSignedBlob,
			pcbSignedBlob);
	if (b && 0 == *pcbSignedBlob)
	{
	    SetLastError((DWORD) HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
	    b = FALSE;
	}
	if (!b)
	{
	    if (NULL != *ppbSignedBlob)
	    {
		myFree(*ppbSignedBlob, allocType);
		*ppbSignedBlob = NULL;
	    }
	    break;
	}
	if (NULL != *ppbSignedBlob)
	{
	    break;
	}
	*ppbSignedBlob = (BYTE *) myAlloc(*pcbSignedBlob, allocType);
	if (NULL == *ppbSignedBlob)
	{
	    b = FALSE;
	    break;
	}
    }
    return(b);
}


BOOL
myEncodeCert(
    IN DWORD dwEncodingType,
    IN CERT_SIGNED_CONTENT_INFO const *pInfo,
    IN CERTLIB_ALLOCATOR allocType,
    OUT BYTE **ppbEncoded,
    OUT DWORD *pcbEncoded)
{
    return(myEncodeObject(
		    dwEncodingType,
		    X509_CERT,
		    pInfo,
		    0,
		    allocType,
		    ppbEncoded,
		    pcbEncoded));
}


BOOL
myEncodeName(
    IN DWORD dwEncodingType,
    IN CERT_NAME_INFO const *pInfo,
    IN DWORD dwFlags,
    IN CERTLIB_ALLOCATOR allocType,
    OUT BYTE **ppbEncoded,
    OUT DWORD *pcbEncoded)
{
    return(myEncodeObject(
		    dwEncodingType,
		    X509_UNICODE_NAME,
		    pInfo,
		    dwFlags,
		    allocType,
		    ppbEncoded,
		    pcbEncoded));
}


BOOL
myEncodeKeyAttributes(
    IN DWORD dwEncodingType,
    IN CERT_KEY_ATTRIBUTES_INFO const *pInfo,
    IN CERTLIB_ALLOCATOR allocType,
    OUT BYTE **ppbEncoded,
    OUT DWORD *pcbEncoded)
{
    return(myEncodeObject(
		    dwEncodingType,
		    X509_KEY_ATTRIBUTES,
		    pInfo,
		    0,
		    allocType,
		    ppbEncoded,
		    pcbEncoded));
}


BOOL
myEncodeKeyUsage(
    IN DWORD dwEncodingType,
    IN CRYPT_BIT_BLOB const *pInfo,
    IN CERTLIB_ALLOCATOR allocType,
    OUT BYTE **ppbEncoded,
    OUT DWORD *pcbEncoded)
{
    return(myEncodeObject(
		    dwEncodingType,
		    X509_KEY_USAGE,
		    pInfo,
		    0,
		    allocType,
		    ppbEncoded,
		    pcbEncoded));
}


BOOL
myEncodeKeyAuthority2(
    IN DWORD dwEncodingType,
    IN CERT_AUTHORITY_KEY_ID2_INFO const *pInfo,
    IN CERTLIB_ALLOCATOR allocType,
    OUT BYTE **ppbEncoded,
    OUT DWORD *pcbEncoded)
{
    return(myEncodeObject(
		    dwEncodingType,
		    X509_AUTHORITY_KEY_ID2,
		    pInfo,
		    0,
		    allocType,
		    ppbEncoded,
		    pcbEncoded));
}


BOOL
myEncodeToBeSigned(
    DWORD dwEncodingType,
    CERT_INFO const *pInfo,
    IN CERTLIB_ALLOCATOR allocType,
    BYTE **ppbEncoded,
    DWORD *pcbEncoded)
{
    return(myEncodeObject(
		    dwEncodingType,
		    X509_CERT_TO_BE_SIGNED,
		    pInfo,
		    0,
		    allocType,
		    ppbEncoded,
		    pcbEncoded));
}

BOOL
myDecodeName(
    IN DWORD dwEncodingType,
    IN LPCSTR lpszStructType,
    IN BYTE const *pbEncoded,
    IN DWORD cbEncoded,
    IN CERTLIB_ALLOCATOR allocType,
    OUT CERT_NAME_INFO **ppNameInfo,
    OUT DWORD *pcbNameInfo)
{
    return(myDecodeObject(
		    dwEncodingType,
		    lpszStructType,
		    pbEncoded,
		    cbEncoded,
		    allocType,
		    (VOID **) ppNameInfo,
		    pcbNameInfo));
}


BOOL
myDecodeKeyAuthority(
    IN DWORD dwEncodingType,
    IN BYTE const *pbEncoded,
    IN DWORD cbEncoded,
    IN CERTLIB_ALLOCATOR allocType,
    OUT CERT_AUTHORITY_KEY_ID_INFO const **ppInfo,
    OUT DWORD *pcbInfo)
{
    return(myDecodeObject(
		    dwEncodingType,
		    X509_AUTHORITY_KEY_ID,
		    pbEncoded,
		    cbEncoded,
		    allocType,
		    (VOID **) ppInfo,
		    pcbInfo));
}


BOOL
myDecodeKeyAuthority2(
    IN DWORD dwEncodingType,
    IN BYTE const *pbEncoded,
    IN DWORD cbEncoded,
    IN CERTLIB_ALLOCATOR allocType,
    OUT CERT_AUTHORITY_KEY_ID2_INFO const **ppInfo,
    OUT DWORD *pcbInfo)
{
    return(myDecodeObject(
		    dwEncodingType,
		    X509_AUTHORITY_KEY_ID2,
		    pbEncoded,
		    cbEncoded,
		    allocType,
		    (VOID **) ppInfo,
		    pcbInfo));
}


BOOL
myDecodeExtensions(
    IN DWORD dwEncodingType,
    IN BYTE const *pbEncoded,
    IN DWORD cbEncoded,
    IN CERTLIB_ALLOCATOR allocType,
    OUT CERT_EXTENSIONS **ppInfo,
    OUT DWORD *pcbInfo)
{
    return(myDecodeObject(
		    dwEncodingType,
		    X509_NAME,
		    pbEncoded,
		    cbEncoded,
		    allocType,
		    (VOID **) ppInfo,
		    pcbInfo));
}


BOOL
myDecodeKeyGenRequest(
    IN BYTE const *pbRequest,
    IN DWORD cbRequest,
    IN CERTLIB_ALLOCATOR allocType,
    OUT CERT_KEYGEN_REQUEST_INFO **ppKeyGenRequest,
    OUT DWORD *pcbKeyGenRequest)
{
    return(myDecodeObject(
		    X509_ASN_ENCODING,
		    X509_KEYGEN_REQUEST_TO_BE_SIGNED,
		    pbRequest,
		    cbRequest,
		    allocType,
		    (VOID **) ppKeyGenRequest,
		    pcbKeyGenRequest));
}


HRESULT
myDecodeCSPProviderAttribute(
    IN BYTE const *pbCSPEncoded,
    IN DWORD cbCSPEncoded,
    OUT CRYPT_CSP_PROVIDER **ppccp)
{
    HRESULT hr;
    CRYPT_SEQUENCE_OF_ANY *pCSPProviderSeq = NULL;
    DWORD cb;
    CRYPT_CSP_PROVIDER *pccp;
    DWORD dwKeySpec;
    CERT_NAME_VALUE *pName = NULL;
    CRYPT_BIT_BLOB *pBlob = NULL;
    BYTE *pb;

    *ppccp = NULL;

    if (!myDecodeObject(
		    X509_ASN_ENCODING,
		    X509_SEQUENCE_OF_ANY,
		    pbCSPEncoded,
		    cbCSPEncoded,
		    CERTLIB_USE_LOCALALLOC,
		    (VOID **) &pCSPProviderSeq,
		    &cb))
    {
	hr = myHLastError();
	_JumpError(hr, error, "myDecodeObject");
    }
    if (3 > pCSPProviderSeq->cValue)
    {
	hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
	_JumpError(hr, error, "Sequence count < 3");
    }

    dwKeySpec = 0;
    if (NULL != pCSPProviderSeq->rgValue[0].pbData &&
	0 != pCSPProviderSeq->rgValue[0].cbData)
    {
	cb = sizeof(dwKeySpec);
	if (!CryptDecodeObject(
			X509_ASN_ENCODING,
			X509_INTEGER,
			pCSPProviderSeq->rgValue[0].pbData,
			pCSPProviderSeq->rgValue[0].cbData,
			0,
			&dwKeySpec,
			&cb))
	{
	    hr = myHLastError();
	    _JumpError(hr, error, "CryptDecodeObject");
	}
    }
    if (NULL != pCSPProviderSeq->rgValue[1].pbData &&
	0 != pCSPProviderSeq->rgValue[1].cbData)
    {
	if (!myDecodeObject(
			X509_ASN_ENCODING,
			X509_UNICODE_ANY_STRING,
			pCSPProviderSeq->rgValue[1].pbData,
			pCSPProviderSeq->rgValue[1].cbData,
			CERTLIB_USE_LOCALALLOC,
			(VOID **) &pName,
			&cb))
	{
	    hr = myHLastError();
	    _JumpError(hr, error, "myDecodeObject");
	}
    }
    if (NULL != pCSPProviderSeq->rgValue[2].pbData &&
	0 != pCSPProviderSeq->rgValue[2].cbData)
    {
	if (!myDecodeObject(
			X509_ASN_ENCODING,
			X509_BITS,
			pCSPProviderSeq->rgValue[2].pbData,
			pCSPProviderSeq->rgValue[2].cbData,
			CERTLIB_USE_LOCALALLOC,
			(VOID **) &pBlob,
			&cb))
	{
	    hr = myHLastError();
	    _JumpError(hr, error, "myDecodeObject");
	}
    }
    cb = sizeof(*pccp);
    if (NULL != pName && NULL != pName->Value.pbData)
    {
	cb += POINTERROUND((wcslen((WCHAR const *) pName->Value.pbData) + 1) *
							    sizeof(WCHAR));
    }
    if (NULL != pBlob && NULL != pBlob->pbData)
    {
	cb += POINTERROUND(pBlob->cbData);
    }
    pccp = (CRYPT_CSP_PROVIDER *) LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, cb);
    if (NULL == pccp)
    {
	hr = E_OUTOFMEMORY;
	_JumpError(hr, error, "LocalAlloc");
    }
    *ppccp = pccp;
    pb = (BYTE *) (pccp + 1);
    pccp->dwKeySpec = dwKeySpec;
    if (NULL != pName->Value.pbData)
    {
	pccp->pwszProviderName = (WCHAR *) pb;
	wcscpy(pccp->pwszProviderName, (WCHAR const *) pName->Value.pbData);
	pb += POINTERROUND((wcslen((WCHAR const *) pName->Value.pbData) + 1) *
							    sizeof(WCHAR));
    }
    if (NULL != pBlob && NULL != pBlob->pbData)
    {
	pccp->Signature.pbData = pb;
	pccp->Signature.cbData = pBlob->cbData;
	pccp->Signature.cUnusedBits = pBlob->cUnusedBits;
	CopyMemory(pccp->Signature.pbData, pBlob->pbData, pBlob->cbData);
    }
    hr = S_OK;

error:
    if (NULL != pCSPProviderSeq)
    {
	LocalFree(pCSPProviderSeq);
    }
    if (NULL != pName)
    {
	LocalFree(pName);
    }
    if (NULL != pBlob)
    {
	LocalFree(pBlob);
    }
    return(hr);
}


BOOL
myCertGetCertificateContextProperty(
    IN CERT_CONTEXT const *pCertContext,
    IN DWORD dwPropId,
    IN CERTLIB_ALLOCATOR allocType,
    OUT VOID **ppvData,
    OUT DWORD *pcbData)
{
    BOOL b;

    *ppvData = NULL;
    *pcbData = 0;
    for (;;)
    {
	b = CertGetCertificateContextProperty(
					 pCertContext,
					 dwPropId,
					 *ppvData,
					 pcbData);
	if (b && 0 == *pcbData)
	{
	    SetLastError((DWORD) HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
	    b = FALSE;
	}
	if (!b)
	{
	    if (NULL != *ppvData)
	    {
		myFree(*ppvData, allocType);
		*ppvData = NULL;
	    }
	    break;
	}
	if (NULL != *ppvData)
	{
	    break;
	}
	*ppvData = (VOID *) myAlloc(*pcbData, allocType);
	if (NULL == *ppvData)
	{
	    b = FALSE;
	    break;
	}
    }
    return(b);
}


HRESULT
myCertGetKeyProviderInfo(
    IN CERT_CONTEXT const *pCert,
    OUT CRYPT_KEY_PROV_INFO **ppkpi)
{
    HRESULT hr;
    DWORD cb;

    *ppkpi = NULL;

    if (!CertGetCertificateContextProperty(
					pCert,
					CERT_KEY_PROV_INFO_PROP_ID,
					NULL,
					&cb))
    {
	hr = myHLastError();
	_JumpError2(
		hr,
		error,
		"CertGetCertificateContextProperty",
		CRYPT_E_NOT_FOUND);
    }

    *ppkpi = (CRYPT_KEY_PROV_INFO *) LocalAlloc(LMEM_FIXED, cb);
    if (NULL == *ppkpi)
    {
	hr = E_OUTOFMEMORY;
	_JumpError(hr, error, "LocalAlloc");
    }

    if (!CertGetCertificateContextProperty(
					pCert,
					CERT_KEY_PROV_INFO_PROP_ID,
					*ppkpi,
					&cb))
    {
	hr = myHLastError();
	_JumpError(hr, error, "CertGetCertificateContextProperty");
    }
    hr = S_OK;

error:
    return(hr);
}


HRESULT
myCryptExportKey(
    IN HCRYPTKEY hKey,
    IN HCRYPTKEY hKeyExp,
    IN DWORD dwBlobType,
    IN DWORD dwFlags,
    OUT BYTE **ppbKey,
    OUT DWORD *pcbKey)
{
    HRESULT hr;

    if (!CryptExportKey(hKey, hKeyExp, dwBlobType, dwFlags, NULL, pcbKey))
    {
	hr = myHLastError();
	_JumpError(hr, error, "CryptExportKey");
    }

    *ppbKey = (BYTE *) LocalAlloc(LMEM_FIXED, *pcbKey);
    if (NULL == *ppbKey)
    {
	hr = E_OUTOFMEMORY;
	_JumpError(hr, error, "LocalAlloc");
    }

    if (!CryptExportKey(hKey, hKeyExp, dwBlobType, dwFlags, *ppbKey, pcbKey))
    {
	hr = myHLastError();
	LocalFree(*ppbKey);
	*ppbKey = NULL;
	_JumpError(hr, error, "CryptExportKey");
    }
    hr = S_OK;

error:
    return(hr);
}


HRESULT
myCryptEncrypt(
    IN HCRYPTKEY hKey,
    IN BYTE const *pbIn,
    IN DWORD cbIn,
    OUT BYTE **ppbEncrypted,
    OUT DWORD *pcbEncrypted)
{
    HRESULT hr;
    BYTE *pbEncrypted = NULL;
    DWORD cbEncrypted;
    DWORD cbAlloc;
    BOOL fRetried = FALSE;

    cbAlloc = cbIn + 64;	// may be enough to prevent overflow
    for (;;)
    {
	cbEncrypted = cbIn;
	pbEncrypted = (BYTE *) LocalAlloc(LMEM_FIXED, cbAlloc);
	if (NULL == pbEncrypted)
	{
	   hr = E_OUTOFMEMORY;
	   _JumpError(hr, error, "LocalAlloc");
	}
	CopyMemory(pbEncrypted, pbIn, cbIn);

	if (!CryptEncrypt(
		    hKey,
		    NULL,		// hHash
		    TRUE,		// Final
		    0,			// dwFlags
		    pbEncrypted,	// pbData
		    &cbEncrypted,	// pdwDataLen
		    cbAlloc))		// dwBufLen
	{
	    hr = myHLastError();
	    if (!fRetried && HRESULT_FROM_WIN32(ERROR_MORE_DATA) == hr)
	    {
                SecureZeroMemory(pbEncrypted, cbAlloc); // only zero size we alloced last time around
		LocalFree(pbEncrypted);
		pbEncrypted = NULL;
		DBGPRINT((
		    DBG_SS_CERTLIB,
		    "CryptEncrypt(MORE DATA): %u -> %u\n",
		    cbAlloc,
		    cbEncrypted));
		cbAlloc = cbEncrypted;
		fRetried = TRUE;
		continue;
	    }
	    _JumpError(hr, error, "CryptEncrypt");
	}
	break;
    }
    *ppbEncrypted = pbEncrypted;
    *pcbEncrypted = cbEncrypted;
    pbEncrypted = NULL;
    hr = S_OK;

error:
    if (NULL != pbEncrypted)
    {
	LocalFree(pbEncrypted);
    }
    return(hr);
}


HRESULT
myCryptDecrypt(
    IN HCRYPTKEY hKey,
    IN BYTE const *pbIn,
    IN DWORD cbIn,
    OUT BYTE **ppbDecrypted,
    OUT DWORD *pcbDecrypted)
{
    HRESULT hr;
    BYTE *pbDecrypted = NULL;
    DWORD cbDecrypted;

    // init
    *ppbDecrypted = NULL;
    *pcbDecrypted = 0;

    cbDecrypted = cbIn;
    pbDecrypted = (BYTE *) LocalAlloc(LMEM_FIXED, cbIn);
    if (NULL == pbDecrypted)
    {
        hr = E_OUTOFMEMORY;
        _JumpError(hr, error, "LocalAlloc");
    }
    CopyMemory(pbDecrypted, pbIn, cbIn);

    if (!CryptDecrypt(
		hKey,
		NULL,			// hHash
		TRUE,			// Final
		0,			// dwFlags
		pbDecrypted,		// pbData
		&cbDecrypted))		// pdwDataLen
    {
	hr = myHLastError();
	_JumpError(hr, error, "CryptDecrypt");
    }
    *ppbDecrypted = pbDecrypted;
    *pcbDecrypted = cbDecrypted;
    pbDecrypted = NULL;
    hr = S_OK;

error:
    if (NULL != pbDecrypted)
    {
	LocalFree(pbDecrypted);
    }
    return(hr);
}


HRESULT
myCryptEncryptMessage(
    IN ALG_ID algId,
    IN DWORD cCertRecipient,
    IN CERT_CONTEXT const **rgCertRecipient,
    IN BYTE const *pbIn,
    IN DWORD cbIn,
    IN OPTIONAL HCRYPTPROV hCryptProv,
    OUT BYTE **ppbEncrypted,
    OUT DWORD *pcbEncrypted)
{
    HRESULT hr;
    CRYPT_ENCRYPT_MESSAGE_PARA cemp;
    CRYPT_OID_INFO const *pOidInfo;

    // Convert to an Object Id

    pOidInfo = CryptFindOIDInfo(
			CRYPT_OID_INFO_ALGID_KEY,
			&algId,
			CRYPT_ENCRYPT_ALG_OID_GROUP_ID);
    if (NULL == pOidInfo)
    {
        // function is not doc'd to set GetLastError()

        hr = CRYPT_E_NOT_FOUND;
	DBGPRINT((DBG_SS_ERROR, "algId = %x\n", algId));
        _JumpError(hr, error, "CryptFindOIDInfo");
    }

    // Encrypt the data with the public key

    ZeroMemory(&cemp, sizeof(cemp));
    cemp.cbSize = sizeof(cemp);
    cemp.dwMsgEncodingType = PKCS_7_ASN_ENCODING | CRYPT_ASN_ENCODING;
    cemp.ContentEncryptionAlgorithm.pszObjId = const_cast<char *>(pOidInfo->pszOID);
    cemp.hCryptProv = hCryptProv;

    *pcbEncrypted = 0;
    for (;;)
    {
	if (!CryptEncryptMessage(
			    &cemp,
			    cCertRecipient,	// cRecipientCert
			    rgCertRecipient,	// rgpRecipientCert IN
			    pbIn,		// pbToBeEncrypted
			    cbIn,		// cbToBeEncrypted
			    *ppbEncrypted,	// pbEncryptedBlob
			    pcbEncrypted))	// pcbEncryptedBlob
	{
	    hr = myHLastError();
	    if (NULL != *ppbEncrypted)
	    {
		LocalFree(*ppbEncrypted);
		*ppbEncrypted = NULL;
	    }
	    _JumpError(hr, error, "CryptEncryptMessage");
	}
	if (NULL != *ppbEncrypted)
	{
	    break;
	}
	*ppbEncrypted = (BYTE *) LocalAlloc(LMEM_FIXED, *pcbEncrypted);
	if (NULL == *ppbEncrypted)
	{
	    hr = E_OUTOFMEMORY;
	    _JumpError(hr, error, "LocalAlloc");
	}
    }
    hr = S_OK;

error:
    CSASSERT(S_OK == hr || FAILED(hr));
    return(hr);
}


HRESULT
myCryptDecryptMessage(
    IN HCERTSTORE hStore,
    IN BYTE const *pbEncrypted,
    IN DWORD cbEncrypted,
    IN CERTLIB_ALLOCATOR allocType,
    OUT BYTE **ppbDecrypted,
    OUT DWORD *pcbDecrypted)
{
    HRESULT hr;
    CRYPT_DECRYPT_MESSAGE_PARA cdmp;

    ZeroMemory(&cdmp, sizeof(cdmp));
    cdmp.cbSize = sizeof(cdmp);
    cdmp.dwMsgAndCertEncodingType = PKCS_7_ASN_ENCODING | X509_ASN_ENCODING;
    cdmp.cCertStore = 1;
    cdmp.rghCertStore = &hStore;

    *ppbDecrypted = NULL;
    *pcbDecrypted = 0;
    for (;;)
    {
	if (!CryptDecryptMessage(
			    &cdmp,
			    pbEncrypted,
			    cbEncrypted,
			    *ppbDecrypted,
			    pcbDecrypted,
			    NULL))	// ppXchgCert
	{
	    hr = myHLastError();
	    if (NULL != *ppbDecrypted)
	    {
		myFree(*ppbDecrypted, allocType);
		*ppbDecrypted = NULL;
	    }
	    _JumpError(hr, error, "CryptDecryptMessage");
	}
	if (NULL != *ppbDecrypted)
	{
	    break;
	}
	*ppbDecrypted = (BYTE *) myAlloc(*pcbDecrypted, allocType);
	if (NULL == *ppbDecrypted)
	{
	    hr = E_OUTOFMEMORY;
	    _JumpError(hr, error, "myAlloc");
	}
    }
    hr = S_OK;

error:
    return(hr);
}


HRESULT
myGetInnerPKCS10(
    IN HCRYPTMSG hMsg,
    IN char const *pszInnerContentObjId,
    OUT CERT_REQUEST_INFO **ppRequest)
{
    HRESULT hr;
    BYTE *pbContent = NULL;
    DWORD cbContent;
    CMC_DATA_INFO *pcmcData = NULL;
    DWORD cbcmcData;
    CMC_TAGGED_CERT_REQUEST const *pTaggedCertRequest;
    DWORD cbRequest;

    *ppRequest = NULL;

    if (0 != strcmp(pszInnerContentObjId, szOID_CT_PKI_DATA))
    {
	hr = CRYPT_E_INVALID_MSG_TYPE;
	_JumpError(hr, error, "Not a CMC request");
    }

    // Get the request content, then search for the PKCS10's public key.

    hr = myCryptMsgGetParam(
		    hMsg,
		    CMSG_CONTENT_PARAM,
		    0,
                    CERTLIB_USE_LOCALALLOC,
		    (VOID **) &pbContent,
		    &cbContent);
    _JumpIfError(hr, error, "myCryptMsgGetParam");

    if (!myDecodeObject(
		    X509_ASN_ENCODING,
		    CMC_DATA,
		    pbContent,
		    cbContent,
		    CERTLIB_USE_LOCALALLOC,
		    (VOID **) &pcmcData,
		    &cbcmcData))
    {
	hr = myHLastError();
	_JumpError(hr, error, "myDecodeObject");
    }

    if (1 != pcmcData->cTaggedRequest ||
	CMC_TAGGED_CERT_REQUEST_CHOICE !=
	    pcmcData->rgTaggedRequest[0].dwTaggedRequestChoice)
    {
	hr = CRYPT_E_INVALID_MSG_TYPE;
	_JumpError(hr, error, "Must be 1 PKCS10");
    }
    pTaggedCertRequest = pcmcData->rgTaggedRequest[0].pTaggedCertRequest;

    if (!myDecodeObject(
		    X509_ASN_ENCODING,
		    X509_CERT_REQUEST_TO_BE_SIGNED,
		    pTaggedCertRequest->SignedCertRequest.pbData,
		    pTaggedCertRequest->SignedCertRequest.cbData,
		    CERTLIB_USE_LOCALALLOC,
		    (VOID **) ppRequest,
		    &cbRequest))
    {
	hr = myHLastError();
	_JumpError(hr, error, "myDecodeObject");
    }
    hr = S_OK;

error:
    if (NULL != pbContent)
    {
	LocalFree(pbContent);
    }
    if (NULL != pcmcData)
    {
	LocalFree(pcmcData);
    }
    return(hr);
}


HRESULT
myPKCSEncodeString(
    IN WCHAR const *pwsz,
    OUT BYTE **ppbOut,
    OUT DWORD *pcbOut)
{
    HRESULT hr = S_OK;
    CERT_NAME_VALUE cnv;

    // encode the string as an IA5 string

    cnv.dwValueType = CERT_RDN_IA5_STRING;
    cnv.Value.pbData = (BYTE *) pwsz;
    cnv.Value.cbData = 0;	// Use L'\0' termination for the length

    if (!myEncodeObject(
		    X509_ASN_ENCODING,
		    X509_UNICODE_NAME_VALUE,
		    &cnv,
		    0,
		    CERTLIB_USE_LOCALALLOC,
		    ppbOut,
		    pcbOut))
    {
	hr = myHLastError();
	_JumpError(hr, error, "myEncodeObject");
    }
error:
    return(hr);
}


HRESULT
myPKCSDecodeString(
    IN BYTE const *pbIn,
    IN DWORD cbIn,
    OUT WCHAR **ppwszOut)
{
    HRESULT hr = S_OK;
    CERT_NAME_VALUE *pcnv = NULL;
    DWORD cbOut;

    *ppwszOut = NULL;

    // decode the string from an IA5 string

    if (!myDecodeObject(
		    X509_ASN_ENCODING,
		    X509_UNICODE_NAME_VALUE,
		    pbIn,
		    cbIn,
		    CERTLIB_USE_LOCALALLOC,
		    (VOID **) &pcnv,
		    &cbOut))
    {
	hr = myHLastError();
	_JumpError(hr, error, "myDecodeObject");
    }
    if (CERT_RDN_IA5_STRING != pcnv->dwValueType)
    {
	hr = E_INVALIDARG;
	_JumpError(hr, error, "Not an IA5 string");
    }
    cbOut = (wcslen((WCHAR const *) pcnv->Value.pbData) + 1) * sizeof(WCHAR);
    *ppwszOut = (WCHAR *) LocalAlloc(LMEM_FIXED, cbOut);
    if (NULL == *ppwszOut)
    {
	hr = E_OUTOFMEMORY;
	_JumpError(hr, error, "LocalAlloc");
    }
    CopyMemory(*ppwszOut, pcnv->Value.pbData, cbOut);

error:
    if (NULL != pcnv)
    {
	LocalFree(pcnv);
    }
    return(hr);
}


HRESULT
myPKCSEncodeLong(
    IN LONG Value,
    OUT BYTE **ppbOut,
    OUT DWORD *pcbOut)
{
    HRESULT hr = S_OK;

    // encode the long value

    if (!myEncodeObject(
		    X509_ASN_ENCODING,
		    X509_INTEGER,
		    &Value,
		    0,
		    CERTLIB_USE_LOCALALLOC,
		    ppbOut,
		    pcbOut))
    {
	hr = myHLastError();
	_JumpError(hr, error, "myEncodeObject");
    }
error:
    return(hr);
}


HRESULT
myPKCSDecodeLong(
    IN BYTE const *pbIn,
    IN DWORD cbIn,
    OUT LONG **ppValue)
{
    HRESULT hr = S_OK;
    DWORD cbOut;

    // encode the long value

    if (!myDecodeObject(
		    X509_ASN_ENCODING,
		    X509_INTEGER,
		    pbIn,
		    cbIn,
		    CERTLIB_USE_LOCALALLOC,
		    (VOID **) ppValue,
		    &cbOut))
    {
	hr = myHLastError();
	_JumpError(hr, error, "myDecodeObject");
    }
    CSASSERT(sizeof(**ppValue) == cbOut);
error:
    return(hr);
}


HRESULT
myPKCSEncodeDate(
    IN FILETIME const *pft,
    OUT BYTE **ppbOut,
    OUT DWORD *pcbOut)
{
    HRESULT hr = S_OK;

    // encode the time value

    if (!myEncodeObject(
		    X509_ASN_ENCODING,
		    X509_CHOICE_OF_TIME,
		    pft,
		    0,
		    CERTLIB_USE_LOCALALLOC,
		    ppbOut,
		    pcbOut))
    {
	hr = myHLastError();
	_JumpError(hr, error, "myEncodeObject");
    }
error:
    return(hr);
}


HRESULT
myPKCSDecodeDate(
    IN BYTE const *pbIn,
    IN DWORD cbIn,
    OUT FILETIME **ppftOut)
{
    HRESULT hr = S_OK;
    DWORD cbOut;

    // encode the time value

    if (!myDecodeObject(
		    X509_ASN_ENCODING,
		    X509_CHOICE_OF_TIME,
		    pbIn,
		    cbIn,
		    CERTLIB_USE_LOCALALLOC,
		    (VOID **) ppftOut,
		    &cbOut))
    {
	hr = myHLastError();
	_JumpError(hr, error, "myDecodeObject");
    }
    CSASSERT(sizeof(**ppftOut) == cbOut);
error:
    return(hr);
}


HRESULT
myEncodeExtension(
    IN DWORD Flags,
    IN BYTE const *pbIn,
    IN DWORD cbIn,
    OUT BYTE **ppbOut,
    OUT DWORD *pcbOut)
{
    HRESULT hr = E_INVALIDARG;

    // everyone assumes pbIn != NULL

    if (NULL == pbIn || 0 == cbIn)
    {
	_JumpError(hr, error, "NULL param");
    }

    switch (PROPTYPE_MASK & Flags)
    {
	case PROPTYPE_STRING:
	    if (0 == (PROPMARSHAL_LOCALSTRING & Flags) &&
		sizeof(WCHAR) <= cbIn)
	    {
		cbIn -= sizeof(WCHAR);
	    }
	    if (wcslen((WCHAR const *) pbIn) * sizeof(WCHAR) != cbIn)
	    {
		_JumpError(hr, error, "bad string len");
	    }
	    hr = myPKCSEncodeString((WCHAR const *) pbIn, ppbOut, pcbOut);
	    _JumpIfError(hr, error, "myPKCSEncodeString");

	    break;

	case PROPTYPE_LONG:
	    CSASSERT(sizeof(DWORD) == cbIn);
	    hr = myPKCSEncodeLong(*(DWORD const *) pbIn, ppbOut, pcbOut);
	    _JumpIfError(hr, error, "myPKCSEncodeLong");

	    break;

	case PROPTYPE_DATE:
	    CSASSERT(sizeof(FILETIME) == cbIn);
	    hr = myPKCSEncodeDate((FILETIME const *) pbIn, ppbOut, pcbOut);
	    _JumpIfError(hr, error, "myPKCSEncodeDate");

	    break;

	default:
	    _JumpError(hr, error, "variant type/value");
    }
    hr = S_OK;

error:
    return(hr);
}


HRESULT
myDecodeExtension(
    IN DWORD Flags,
    IN BYTE const *pbIn,
    IN DWORD cbIn,
    OUT BYTE **ppbOut,
    OUT DWORD *pcbOut)
{
    HRESULT hr;

    switch (PROPTYPE_MASK & Flags)
    {
	case PROPTYPE_STRING:
	    hr = myPKCSDecodeString(pbIn, cbIn, (WCHAR **) ppbOut);
	    _JumpIfError(hr, error, "myPKCSDecodeString");

	    *pcbOut = wcslen((WCHAR const *) *ppbOut) * sizeof(WCHAR);
	    break;

	case PROPTYPE_LONG:
	    hr = myPKCSDecodeLong(pbIn, cbIn, (LONG **) ppbOut);
	    _JumpIfError(hr, error, "myPKCSDecodeLong");

	    *pcbOut = sizeof(LONG);
	    break;

	case PROPTYPE_DATE:
	    hr = myPKCSDecodeDate(pbIn, cbIn, (FILETIME **) ppbOut);
	    _JumpIfError(hr, error, "myPKCSDecodeDate");

	    *pcbOut = sizeof(FILETIME);
	    break;

	default:
	    hr = E_INVALIDARG;
	    _JumpError(hr, error, "Flags: Invalid type");
    }

error:
    return(hr);
}


// szOID_ENROLLMENT_NAME_VALUE_PAIR

BOOL
myDecodeNameValuePair(
    IN DWORD dwEncodingType,
    IN BYTE const *pbEncoded,
    IN DWORD cbEncoded,
    IN CERTLIB_ALLOCATOR allocType,
    OUT CRYPT_ENROLLMENT_NAME_VALUE_PAIR **ppInfo,
    OUT DWORD *pcbInfo)
{

    return(myDecodeObject(
		    dwEncodingType,
		    szOID_ENROLLMENT_NAME_VALUE_PAIR,
		    pbEncoded,
		    cbEncoded,
		    allocType,
		    (VOID **) ppInfo,
		    pcbInfo));
}


//+-------------------------------------------------------------------------
// myVerifyObjIdA - verify the passed pszObjId is valid as per X.208
//
// Encode and Decode the Object Id and make sure it survives the round trip.
// The first number must be 0, 1 or 2.
// Enforce all characters are digits and dots.
// Enforce that no dot starts or ends the Object Id, and disallow double dots.
// Enforce there is at least one dot separator.
// If the first number is 0 or 1, the second number must be between 0 & 39.
// If the first number is 2, the second number can be any value.
//--------------------------------------------------------------------------

HRESULT
myVerifyObjIdA(
    IN CHAR const *pszObjId)
{
    HRESULT hr;
    BYTE *pbEncoded = NULL;
    DWORD cbEncoded;
    CRYPT_ATTRIBUTE ainfo;
    CRYPT_ATTRIBUTE *painfo = NULL;
    DWORD cbainfo;
    char const *psz;
    int i;
    BOOL fNoisy = FALSE;

    hr = E_INVALIDARG;
    for (psz = pszObjId; '\0' != *psz; psz++)
    {
	// must be a digit or a dot separator

	if (!isdigit(*psz))
	{
	    if ('.' != *psz)
	    {
		_JumpError2(hr, error, "bad ObjId: bad char", hr);
	    }

	    // can't have dot at start, double dots or dot at end

	    if (psz == pszObjId || '.' == psz[1] || '\0' == psz[1])
	    {
		_JumpError2(hr, error, "bad ObjId: dot location", hr);
	    }
	}
    }
    psz = strchr(pszObjId, '.');
    if (NULL == psz)
    {
	_JumpError2(hr, error, "bad ObjId: must have at least one dot", hr);
    }
    i = atoi(pszObjId);
    switch (i)
    {
	case 0:
	case 1:
	    i = atoi(++psz);
	    if (0 > i || 39 < i)
	    {
		_JumpError(hr, error, "bad ObjId: 0. or 1. must be followed by 0..39");
	    }
	    break;

	case 2:
	    break;

	default:
	    fNoisy = TRUE;
	    _JumpError(hr, error, "bad ObjId: must start with 0, 1 or 2");
    }

    fNoisy = TRUE;
    ainfo.pszObjId = const_cast<char *>(pszObjId);
    ainfo.cValue = 0;
    ainfo.rgValue = NULL;

    if (!myEncodeObject(
		    X509_ASN_ENCODING,
		    PKCS_ATTRIBUTE,
		    &ainfo,
		    0,
		    CERTLIB_USE_LOCALALLOC,
		    &pbEncoded,
		    &cbEncoded))
    {
	hr = myHLastError();
	_JumpError(hr, error, "myEncodeObject");
    }

    if (!myDecodeObject(
		    X509_ASN_ENCODING,
		    PKCS_ATTRIBUTE,
		    pbEncoded,
		    cbEncoded,
		    CERTLIB_USE_LOCALALLOC,
		    (VOID **) &painfo,
		    &cbainfo))
    {
	hr = myHLastError();
	_JumpError(hr, error, "myDecodeObject");
    }

    if (0 != strcmp(pszObjId, painfo->pszObjId))
    {
	hr = E_INVALIDARG;
	_JumpError(hr, error, "bad ObjId: decode mismatch");
    }
    hr = S_OK;

error:
    if (S_OK != hr)
    {
	DBGPRINT((
	    fNoisy? DBG_SS_CERTLIB : DBG_SS_CERTLIBI,
	    "myVerifyObjIdA(%hs): %x\n",
	    pszObjId,
	    hr));
    }
    if (NULL != pbEncoded)
    {
    	LocalFree(pbEncoded);
    }
    if (NULL != painfo)
    {
    	LocalFree(painfo);
    }
    return(hr);
}


HRESULT
myVerifyObjId(
    IN WCHAR const *pwszObjId)
{
    HRESULT hr;
    CHAR *pszObjId = NULL;

    if (!ConvertWszToSz(&pszObjId, pwszObjId, -1))
    {
	hr = E_OUTOFMEMORY;
	_JumpError(hr, error, "ConvertWszToSz");
    }
    hr = myVerifyObjIdA(pszObjId);
    _JumpIfErrorStr2(hr, error, "myVerifyObjIdA", pwszObjId, E_INVALIDARG);

error:
    if (NULL != pszObjId)
    {
    	LocalFree(pszObjId);
    }
    return(hr);
}


// The returned pszObjId is a constant that must not be freed.  CryptFindOIDInfo
// has a static internal database that is valid until crypt32.dll is unloaded.

#define GON_GROUP	0x00000001
#define GON_GENERIC	0x00000002

typedef struct _OIDNAME
{
    char const *pszObjId;
    WCHAR const *pwszDisplayName;
} OIDNAME;

#if DBG
#define wszCERTLIB	L"(certlib)"
#else
#define wszCERTLIB	L""
#endif

OIDNAME s_aOIDName[] = {
    { szOID_CT_PKI_DATA,		L"CMC Data" wszCERTLIB, },
    { szOID_CT_PKI_RESPONSE,		L"CMC Response" wszCERTLIB, },
    { szOID_CMC,			L"Unsigned CMC Request" wszCERTLIB, },
    { szOID_CMC_TRANSACTION_ID,		L"Transaction Id" wszCERTLIB, },
    { szOID_CMC_SENDER_NONCE,		L"Sender Nonce" wszCERTLIB, },
    { szOID_CMC_RECIPIENT_NONCE,	L"Recipient Nonce" wszCERTLIB, },
    { szOID_CMC_REG_INFO,		L"Reg Info" wszCERTLIB, },
    { szOID_CMC_GET_CERT,		L"Get Certificate" wszCERTLIB, },
    { szOID_CMC_GET_CRL,		L"Get CRL" wszCERTLIB, },
    { szOID_CMC_REVOKE_REQUEST,		L"Revoke Request" wszCERTLIB, },
    { szOID_CMC_QUERY_PENDING,		L"Query Pending" wszCERTLIB, },
    { szOID_CMC_ID_CONFIRM_CERT_ACCEPTANCE, L"Confirm Certificate Acceptance" wszCERTLIB, },
    { szOID_CMC_STATUS_INFO,		L"Unsigned CMC Response" wszCERTLIB, },
    { szOID_CMC_ADD_EXTENSIONS,		L"CMC Extensions" wszCERTLIB, },
    { szOID_CMC_ADD_ATTRIBUTES,		L"CMC Attributes" wszCERTLIB, },
    { szOID_VERISIGN_ONSITE_JURISDICTION_HASH, L"Jurisdiction Hash" wszCERTLIB, },
    { szOID_PKCS_7_DATA,		L"PKCS 7 Data" wszCERTLIB, },
    { szOID_ARCHIVED_KEY_ATTR,		L"Archived Key" wszCERTLIB, },
    { szOID_CTL,			L"Certifcate Trust List" wszCERTLIB, },
    { szOID_ARCHIVED_KEY_CERT_HASH,	L"Archived Key Certificate Hash" wszCERTLIB, },
    { szOID_ROOT_LIST_SIGNER,		L"Root List Signer" wszCERTLIB, },
    { szOID_PRIVATEKEY_USAGE_PERIOD,	L"Private Key Usage Period" wszCERTLIB, },
    { szOID_REQUEST_CLIENT_INFO,	L"Client Information" wszCERTLIB, },
    { szOID_NTDS_REPLICATION,	        L"DS object Guid" wszCERTLIB, },
    { szOID_CERTSRV_CROSSCA_VERSION,	L"Cross CA Version" wszCERTLIB, },
};


WCHAR const *
myGetOIDNameA(
    IN char const *pszObjId)
{
    CRYPT_OID_INFO const *pInfo = NULL;
    WCHAR const *pwszName = L"";
    DWORD Flags = GON_GROUP | GON_GENERIC;

    if ('+' == *pszObjId)
    {
	Flags = GON_GROUP;	// Group lookup only
	pszObjId++;
    }
    else
    if ('-' == *pszObjId)
    {
	Flags = GON_GENERIC;	// Generic lookup only
	pszObjId++;
    }

    // First try looking up the ObjectId as an Extension or Attribute, because
    // we get a better Display Name, especially for Subject RDNs: CN, L, etc.
    // If that fails, look it up without restricting the group.

    if (GON_GROUP & Flags)
    {
	pInfo = CryptFindOIDInfo(
			    CRYPT_OID_INFO_OID_KEY,
			    (VOID *) pszObjId,
			    CRYPT_EXT_OR_ATTR_OID_GROUP_ID);
    }
    if ((GON_GENERIC & Flags) &&
	(NULL == pInfo ||
	 NULL == pInfo->pwszName ||
	 L'\0' == pInfo->pwszName[0]))
    {
	pInfo = CryptFindOIDInfo(CRYPT_OID_INFO_OID_KEY, (VOID *) pszObjId, 0);
    }
    if (NULL != pInfo && NULL != pInfo->pwszName && L'\0' != pInfo->pwszName[0])
    {
	pwszName = pInfo->pwszName;
    }
    else
    {
	OIDNAME const *pOIDName;

	for (pOIDName = s_aOIDName;
	     pOIDName < &s_aOIDName[ARRAYSIZE(s_aOIDName)];
	     pOIDName++)
	{
	    if (0 == strcmp(pOIDName->pszObjId, pszObjId))
	    {
		pwszName = pOIDName->pwszDisplayName;
		break;
	    }
	}
    }
    return(pwszName);
}


WCHAR const *
myGetOIDName(
    IN WCHAR const *pwszObjId)
{
    char *pszObjId = NULL;
    WCHAR const *pwszName = L"";

    if (!ConvertWszToSz(&pszObjId, pwszObjId, -1))
    {
	_JumpError(E_OUTOFMEMORY, error, "ConvertWszToSz");
    }
    pwszName = myGetOIDNameA(pszObjId);

error:
    if (NULL != pszObjId)
    {
	LocalFree(pszObjId);
    }
    return(pwszName);
}


typedef struct _DUMPFLAGS
{
    DWORD Mask;
    DWORD Value;
    WCHAR const *pwszDescription;
} DUMPFLAGS;


#define _DFBIT(def)		{ (def), (def), L#def }
#define _DFBIT2(mask, def)	{ (mask), (def), L#def }


DUMPFLAGS g_adfErrorStatus[] =
{
    _DFBIT(CERT_TRUST_IS_NOT_TIME_VALID),
    _DFBIT(CERT_TRUST_IS_NOT_TIME_NESTED),
    _DFBIT(CERT_TRUST_IS_REVOKED),
    _DFBIT(CERT_TRUST_IS_NOT_SIGNATURE_VALID),
    _DFBIT(CERT_TRUST_IS_NOT_VALID_FOR_USAGE),
    _DFBIT(CERT_TRUST_IS_UNTRUSTED_ROOT),
    _DFBIT(CERT_TRUST_REVOCATION_STATUS_UNKNOWN),
    _DFBIT(CERT_TRUST_IS_CYCLIC),

    _DFBIT(CERT_TRUST_INVALID_EXTENSION),
    _DFBIT(CERT_TRUST_INVALID_POLICY_CONSTRAINTS),
    _DFBIT(CERT_TRUST_INVALID_BASIC_CONSTRAINTS),
    _DFBIT(CERT_TRUST_INVALID_NAME_CONSTRAINTS),
    _DFBIT(CERT_TRUST_HAS_NOT_SUPPORTED_NAME_CONSTRAINT),
    _DFBIT(CERT_TRUST_HAS_NOT_DEFINED_NAME_CONSTRAINT),
    _DFBIT(CERT_TRUST_HAS_NOT_PERMITTED_NAME_CONSTRAINT),
    _DFBIT(CERT_TRUST_HAS_EXCLUDED_NAME_CONSTRAINT),

    _DFBIT(CERT_TRUST_IS_OFFLINE_REVOCATION),
    _DFBIT(CERT_TRUST_NO_ISSUANCE_CHAIN_POLICY),

    _DFBIT(CERT_TRUST_IS_PARTIAL_CHAIN),
    _DFBIT(CERT_TRUST_CTL_IS_NOT_TIME_VALID),
    _DFBIT(CERT_TRUST_CTL_IS_NOT_SIGNATURE_VALID),
    _DFBIT(CERT_TRUST_CTL_IS_NOT_VALID_FOR_USAGE),

    { 0, 0, NULL }
};

DUMPFLAGS g_adfInfoStatus[] =
{
    _DFBIT(CERT_TRUST_HAS_EXACT_MATCH_ISSUER),
    _DFBIT(CERT_TRUST_HAS_KEY_MATCH_ISSUER),
    _DFBIT(CERT_TRUST_HAS_NAME_MATCH_ISSUER),
    _DFBIT(CERT_TRUST_IS_SELF_SIGNED),

    _DFBIT(CERT_TRUST_HAS_PREFERRED_ISSUER),
    _DFBIT(CERT_TRUST_HAS_ISSUANCE_CHAIN_POLICY),
    _DFBIT(CERT_TRUST_HAS_VALID_NAME_CONSTRAINTS),

    _DFBIT(CERT_TRUST_IS_COMPLEX_CHAIN),
    { 0, 0, NULL }
};

DUMPFLAGS g_adfChainFlags[] =
{
    _DFBIT(CERT_CHAIN_CACHE_END_CERT),
    _DFBIT(CERT_CHAIN_THREAD_STORE_SYNC),
    _DFBIT(CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL),
    _DFBIT(CERT_CHAIN_USE_LOCAL_MACHINE_STORE),
    _DFBIT(CERT_CHAIN_ENABLE_CACHE_AUTO_UPDATE),
    _DFBIT(CERT_CHAIN_ENABLE_SHARE_STORE),
    _DFBIT(CERT_CHAIN_DISABLE_PASS1_QUALITY_FILTERING),
    _DFBIT(CERT_CHAIN_RETURN_LOWER_QUALITY_CONTEXTS),
    _DFBIT(CERT_CHAIN_DISABLE_AUTH_ROOT_AUTO_UPDATE),
    _DFBIT(CERT_CHAIN_TIMESTAMP_TIME),
    _DFBIT(CERT_CHAIN_REVOCATION_ACCUMULATIVE_TIMEOUT),
    _DFBIT(CERT_CHAIN_REVOCATION_CHECK_END_CERT),
    _DFBIT(CERT_CHAIN_REVOCATION_CHECK_CHAIN),
    _DFBIT(CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT),
    _DFBIT(CERT_CHAIN_REVOCATION_CHECK_CACHE_ONLY),
    { 0, 0, NULL }
};

DUMPFLAGS g_adfVerifyFlags[] =
{
    _DFBIT(CA_VERIFY_FLAGS_ALLOW_UNTRUSTED_ROOT),
    _DFBIT(CA_VERIFY_FLAGS_IGNORE_OFFLINE),
    _DFBIT(CA_VERIFY_FLAGS_NO_REVOCATION),
    _DFBIT(CA_VERIFY_FLAGS_FULL_CHAIN_REVOCATION),
    _DFBIT(CA_VERIFY_FLAGS_NT_AUTH),
    _DFBIT(CA_VERIFY_FLAGS_IGNORE_INVALID_POLICIES),
    _DFBIT(CA_VERIFY_FLAGS_IGNORE_NOREVCHECK),
    _DFBIT(CA_VERIFY_FLAGS_DUMP_CHAIN),
    _DFBIT(CA_VERIFY_FLAGS_SAVE_CHAIN),
    { 0, 0, NULL }
};


VOID
DumpFlags(
    IN DWORD Flags,
    IN WCHAR const *pwsz,
    IN DUMPFLAGS const *pdf)
{
    for ( ; NULL != pdf->pwszDescription; pdf++)
    {
	if ((Flags & pdf->Mask) == pdf->Value)
	{
	    CONSOLEPRINT3((
		    MAXDWORD,
		    "%ws = %ws (0x%x)\n",
		    pwsz,
		    pdf->pwszDescription,
		    pdf->Value));
	}
    }
}


VOID
DumpUsage(
    IN WCHAR const *pwsz,
    OPTIONAL IN CERT_ENHKEY_USAGE const *pUsage)
{
    DWORD i;

    if (NULL != pUsage)
    {
	for (i = 0; i < pUsage->cUsageIdentifier; i++)
	{
	    CONSOLEPRINT4((
		    MAXDWORD,
		    "%ws[%u] = %hs %ws\n",
		    pwsz,
		    i,
		    pUsage->rgpszUsageIdentifier[i],
		    myGetOIDNameA(pUsage->rgpszUsageIdentifier[i])));
	}
    }
}


HRESULT
WriteBlob(
    IN FILE *pf,
    IN BYTE const *pb,
    IN DWORD cb,
    IN DWORD Flags)
{
    HRESULT hr;
    char *pszBase64 = NULL;

    hr = myCryptBinaryToStringA(
		    pb,
		    cb,
		    Flags | CRYPT_STRING_NOCR,
		    &pszBase64);
    _JumpIfError(hr, error, "myCryptBinaryToStringA");

    fputs(pszBase64, pf);
    fflush(pf);
    if (ferror(pf))
    {
	hr = HRESULT_FROM_WIN32(ERROR_DISK_FULL);
	_JumpError(hr, error, "fputs");
    }
    hr = S_OK;

error:
    if (NULL != pszBase64)
    {
	LocalFree(pszBase64);
    }
    return(hr);
}


DWORD
myCRLNumber(
    IN CRL_CONTEXT const *pCRL)
{
    HRESULT hr;
    CERT_EXTENSION const *pExt;
    DWORD CRLNumber = 0;
    DWORD dw;
    DWORD cb;

    pExt = CertFindExtension(
			szOID_CRL_NUMBER,
			pCRL->pCrlInfo->cExtension,
			pCRL->pCrlInfo->rgExtension);
    if (NULL == pExt)
    {
	// This API doesn't set LastError
	hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
	_JumpError(hr, error, "CertFindExtension(CRLNumber)");
    }
    cb = sizeof(dw);
    dw = 0;
    if (!CryptDecodeObject(
			X509_ASN_ENCODING,
			X509_INTEGER,
			pExt->Value.pbData,
			pExt->Value.cbData,
			0,
			&dw,
			&cb))
    {
	hr = myHLastError();
	_JumpError(hr, error, "CryptDecodeObject");
    }
    CRLNumber = dw;

error:
    return(CRLNumber);
}


VOID
WriteChain(
    IN CERT_SIMPLE_CHAIN const *pChain,
    IN DWORD SaveIndex,
    IN DWORD ChainIndex)
{
    HRESULT hr;
#define szCHAINFORMAT	"Chain%d_%d.txt"
    char szPath[MAX_PATH + ARRAYSIZE(szCHAINFORMAT) + 2 * cwcDWORDSPRINTF];
    DWORD i;
    FILE *pf = NULL;
    DWORD cch;

    cch = GetEnvironmentVariableA("temp", szPath, MAX_PATH);
    if (0 == cch || MAX_PATH <= cch)
    {
	strcpy(szPath, "\\");
    }
    i = strlen(szPath);
    if (0 == i || '\\' != szPath[i - 1])
    {
	szPath[i++] = '\\';
    }
    sprintf(&szPath[i], szCHAINFORMAT, SaveIndex, ChainIndex);
    pf = fopen(szPath, "w");
    if (NULL == pf)
    {
	hr = errno;
	_JumpError(hr, error, "fopen");
    }
    for (i = 0; i < pChain->cElement; i++)
    {
	CERT_CHAIN_ELEMENT const *pElement = pChain->rgpElement[i];
	CERT_REVOCATION_INFO *pRevocationInfo;

	if (0 < i)
	{
	    fputs("\n", pf);
	}
	fprintf(pf, "Certificate %d:\n", i);

	hr = WriteBlob(
		    pf,
		    pElement->pCertContext->pbCertEncoded,
		    pElement->pCertContext->cbCertEncoded,
		    CRYPT_STRING_BASE64HEADER);
	_JumpIfError(hr, error, "WriteBlob");

	pRevocationInfo = pElement->pRevocationInfo;

	if (NULL != pRevocationInfo &&
	    CCSIZEOF_STRUCT(CERT_REVOCATION_INFO, pCrlInfo) <=
		pRevocationInfo->cbSize &&
	    NULL != pRevocationInfo->pCrlInfo)
	{
	    CERT_REVOCATION_CRL_INFO *pCrlInfo;

	    pCrlInfo = pRevocationInfo->pCrlInfo;
	    if (NULL != pCrlInfo)
	    {
		if (NULL != pCrlInfo->pBaseCrlContext)
		{
		    fprintf(
			pf,
			"\nCRL %u:\n",
			myCRLNumber(pCrlInfo->pBaseCrlContext));
		    hr = WriteBlob(
				pf,
				pCrlInfo->pBaseCrlContext->pbCrlEncoded,
				pCrlInfo->pBaseCrlContext->cbCrlEncoded,
				CRYPT_STRING_BASE64X509CRLHEADER);
		    _JumpIfError(hr, error, "WriteBlob");
		}
		if (NULL != pCrlInfo->pDeltaCrlContext)
		{
		    fprintf(
			pf,
			"\nDelta CRL %u:\n",
			myCRLNumber(pCrlInfo->pDeltaCrlContext));
		    hr = WriteBlob(
				pf,
				pCrlInfo->pDeltaCrlContext->pbCrlEncoded,
				pCrlInfo->pDeltaCrlContext->cbCrlEncoded,
				CRYPT_STRING_BASE64X509CRLHEADER);
		    _JumpIfError(hr, error, "WriteBlob");
		}
	    }
	}
    }

error:
    if (NULL != pf)
    {
	fclose(pf);
    }
}


HRESULT
DumpChainOpenHash(
    OUT HCRYPTPROV *phProv,
    OUT HCRYPTHASH *phHash)
{
    HRESULT hr;

    *phProv = NULL;
    *phHash = NULL;
    if (!CryptAcquireContext(
		    phProv,
		    NULL,		// container
		    MS_DEF_PROV,
		    PROV_RSA_FULL,
		    CRYPT_VERIFYCONTEXT))
    {
	*phProv = NULL;
	hr = myHLastError();
	_JumpError(hr, error, "CryptAcquireContext");
    }

    if (!CryptCreateHash(*phProv, CALG_SHA1, 0, 0, phHash))
    {
	*phHash = NULL;
	hr = myHLastError();
	_JumpError(hr, error, "CryptCreateHash");
    }
    hr = S_OK;

error:
    if (S_OK != hr && NULL != *phProv)
    {
	if (!CryptReleaseContext(*phProv, 0))
	{
	    HRESULT hr2 = myHLastError();

	    _PrintError(hr, "CryptReleaseContext");
	    if (hr == S_OK)
	    {
		hr = hr2;
	    }
	}
	*phProv = NULL;
    }
    return(hr);
}


VOID
DumpChainName(
    IN char const *pszType,
    IN CERT_NAME_BLOB const *pName)
{
    HRESULT hr;
    WCHAR *pwsz;

    pwsz = NULL;
    hr = myCertNameToStr(
		X509_ASN_ENCODING,
		pName,
		CERT_X500_NAME_STR | CERT_NAME_STR_REVERSE_FLAG,
		&pwsz);
    _PrintIfError(hr, "myCertNameToStr");
    if (NULL != pwsz)
    {
	CONSOLEPRINT2((MAXDWORD, "%hs: %ws\n", pszType, pwsz));
	LocalFree(pwsz);
    }
}


HRESULT
DumpChainSerial(
    IN char const *pszType,
    IN CRYPT_INTEGER_BLOB const *pSerial)
{
    HRESULT hr;
    BSTR strSerial = NULL;

    hr = MultiByteIntegerToBstr(
			    FALSE,
			    pSerial->cbData,
			    pSerial->pbData,
			    &strSerial);
    _JumpIfError(hr, error, "MultiByteIntegerToBstr");

    CONSOLEPRINT2((MAXDWORD, "%hs: %ws\n", pszType, strSerial));

error:
    if (NULL != strSerial)
    {
	SysFreeString(strSerial);
    }
    return(hr);
}


HRESULT
DumpChainHash(
    IN BYTE const *pbHash,
    IN DWORD cbHash)
{
    HRESULT hr;
    WCHAR wszHash[CBMAX_CRYPT_HASH_LEN * 3];
    DWORD cbwszHash;

    cbwszHash = sizeof(wszHash);
    hr = MultiByteIntegerToWszBuf(
		       TRUE,	// byte multiple
		       cbHash,
		       pbHash,
		       &cbwszHash,
		       wszHash);
    _JumpIfError(hr, error, "MultiByteIntegerToWszBuf");

    CONSOLEPRINT1((MAXDWORD, "  %ws\n", wszHash));

    hr = S_OK;

error:
    return(hr);
}


HRESULT
DumpChainAddHash(
    IN OPTIONAL HCRYPTHASH hHash,
    IN BYTE const *pb,
    IN DWORD cb)
{
    HRESULT hr;

    if (NULL != hHash)
    {
	if (!CryptHashData(hHash, pb, cb, 0))
	{
	    hr = myHLastError();
	    _JumpError(hr, error, "CryptHashData");
	}
    }
    hr = S_OK;

error:
    return(hr);
}


HRESULT
DumpChainHashResult(
    IN OPTIONAL HCRYPTHASH hHash,
    IN char const *pszType)
{
    HRESULT hr;
    HCRYPTHASH hHashT = NULL;
    BYTE abHash[CBMAX_CRYPT_HASH_LEN];
    DWORD cbHash;

    if (NULL != hHash)
    {
	if (!CryptDuplicateHash(
			hHash,
			NULL,		// pdwReserved
			0,		// dwFlags
			&hHashT))
	{
	    hr = myHLastError();
	    _JumpError(hr, error, "CryptDuplicateHash");
	}
	if (!CryptGetHashParam(hHashT, HP_HASHVAL, abHash, &cbHash, 0))
	{
	    hr = myHLastError();
	    _JumpError(hr, error, "CryptGetHashParam");
	}
	CONSOLEPRINT2((MAXDWORD, "%hs:\n", pszType));
	hr = DumpChainHash(abHash, cbHash);
	_JumpIfError(hr, error, "DumpChainHash");
    }
    hr = S_OK;

error:
    if (NULL != hHashT)
    {
	CryptDestroyHash(hHashT);
    }
    return(hr);
}


VOID
DumpChainTemplate(
    IN CERT_INFO const *pCertInfo)
{
    HRESULT hr;
    CERT_NAME_VALUE *pbName = NULL;
    CERT_TEMPLATE_EXT *pTemplate = NULL;
    CERT_EXTENSION *pExt;
    DWORD cb;

    // display v1 template extension first

    pExt = CertFindExtension(
		    szOID_ENROLL_CERTTYPE_EXTENSION,
		    pCertInfo->cExtension,
		    pCertInfo->rgExtension);
    if (NULL != pExt)
    {
	if (!myDecodeObject(
			X509_ASN_ENCODING,
			X509_UNICODE_ANY_STRING,
			pExt->Value.pbData,
			pExt->Value.cbData,
			CERTLIB_USE_LOCALALLOC,
			(VOID **) &pbName,
			&cb))
	{
	    hr = myHLastError();
	    _PrintError(hr, "myDecodeObject");
	}
	else
	{
	    CONSOLEPRINT1((MAXDWORD, "  Template: %ws\n", pbName->Value.pbData));
	}
    }
    pExt = CertFindExtension(
		    szOID_CERTIFICATE_TEMPLATE,
		    pCertInfo->cExtension,
		    pCertInfo->rgExtension);

    if (NULL != pExt)
    {
        if (!myDecodeObject(
                        X509_ASN_ENCODING,
                        X509_CERTIFICATE_TEMPLATE,
                        pExt->Value.pbData,
                        pExt->Value.cbData,
                        CERTLIB_USE_LOCALALLOC,
                        (VOID **) &pTemplate,
                        &cb))
        {
            hr = myHLastError();
            _PrintError(hr, "myDecodeObject");
        }
	else
	{
	    WCHAR const *pwsz;

	    pwsz = myGetOIDNameA(pTemplate->pszObjId); // Static: do not free!
	    if (NULL != pwsz && L'\0' != *pwsz)
	    {
		CONSOLEPRINT1((MAXDWORD, "  Template: %ws\n", pwsz));
	    }
	    else
	    {
		CONSOLEPRINT1((MAXDWORD, "  Template: %hs\n", pTemplate->pszObjId));
	    }
	}
    }

//error:
    LOCAL_FREE(pTemplate);
    LOCAL_FREE(pbName);
}


HRESULT
DumpChainCert(
    IN CERT_CONTEXT const *pcc)
{
    HRESULT hr;
    BYTE abHash[CBMAX_CRYPT_HASH_LEN];
    DWORD cbHash;

    DumpChainName("  Issuer", &pcc->pCertInfo->Issuer);
    DumpChainName("  Subject", &pcc->pCertInfo->Subject);
    DumpChainSerial("  Serial", &pcc->pCertInfo->SerialNumber);
    DumpChainTemplate(pcc->pCertInfo);
    if (!CertGetCertificateContextProperty(
				    pcc,
				    CERT_SHA1_HASH_PROP_ID,
				    abHash,
				    &cbHash))
    {
	hr = myHLastError();
	_JumpError(hr, error, "CertGetCertificateContextProperty");
    }
    hr = DumpChainHash(abHash, cbHash);
    _JumpIfError(hr, error, "DumpChainHash");

error:
    return(hr);
}


HRESULT
DumpChainCRL(
    IN BOOL fDelta,
    OPTIONAL IN CRL_CONTEXT const *pCRL,
    IN OPTIONAL HCRYPTHASH hHash)
{
    HRESULT hr;
    BYTE abHash[CBMAX_CRYPT_HASH_LEN];
    DWORD cbHash;

    if (NULL != pCRL)
    {
	CONSOLEPRINT2((
		MAXDWORD,
		"    %hsCRL %u:\n",
		fDelta? "Delta " : "",
		myCRLNumber(pCRL)));

	DumpChainName("    Issuer", &pCRL->pCrlInfo->Issuer);
	if (!CertGetCRLContextProperty(
				    pCRL,
				    CERT_SHA1_HASH_PROP_ID,
				    abHash,
				    &cbHash))
	{
	    hr = myHLastError();
	    _PrintError(hr, "CertGetCRLContextProperty");
	}
	else
	{
	    CONSOLEPRINT0((MAXDWORD, "  "));
	    hr = DumpChainHash(abHash, cbHash);
	    _PrintIfError(hr, "DumpChainHash");
	}
	hr = DumpChainAddHash(hHash, pCRL->pbCrlEncoded, pCRL->cbCrlEncoded);
	_JumpIfError(hr, error, "DumpChainAddHash");
    }
    hr = S_OK;

error:
    return(hr);
}


VOID
DumpChainSeconds(
    IN WCHAR const *pwszField,
    IN DWORD dwSeconds)
{
    HRESULT hr;
    LLFILETIME llftPeriod;
    WCHAR *pwszTimePeriod;

    llftPeriod.ft.dwHighDateTime = 0;
    llftPeriod.ft.dwLowDateTime = dwSeconds;
    llftPeriod.ll *= CVT_BASE;
    llftPeriod.ll = -llftPeriod.ll;

    hr = myFileTimePeriodToWszTimePeriod(&llftPeriod.ft, TRUE, &pwszTimePeriod);
    if (S_OK != hr)
    {
	_PrintError(hr, "myFileTimePeriodToWszTimePeriod");
	CONSOLEPRINT2((MAXDWORD, "%ws: %us\n", pwszField, dwSeconds));
    }
    else
    {
	CONSOLEPRINT2((MAXDWORD, "%ws: %ws\n", pwszField, pwszTimePeriod));
	LocalFree(pwszTimePeriod);
    }
}


VOID
myDumpChain(
    IN HRESULT hrVerify,
    IN DWORD dwFlags,
    IN CERT_CONTEXT const *pCert,
    OPTIONAL IN FNSIMPLECHAINELEMENTCALLBACK *pfnCallback,
    OPTIONAL IN WCHAR const *pwszMissingIssuer,
    IN CERT_CHAIN_CONTEXT const *pChainContext)
{
    HRESULT hr;
    DWORD i;
    DWORD j;
    static BOOL s_fEnvChecked = FALSE;
    static BOOL s_fDumpEnabled = FALSE;
    static DWORD s_SaveCount = 0;
    static DWORD s_SaveIndex;
    BOOL fDump;
    BOOL fSave;
    HCRYPTPROV hProv = NULL;
    HCRYPTHASH hHash = NULL;

    if (!s_fEnvChecked)
    {
	WCHAR wszBuf[20];
	DWORD cwc;

	cwc = GetEnvironmentVariable(
				L"CertSrv_Chain",
				wszBuf,
				ARRAYSIZE(wszBuf));
	if (0 < cwc && ARRAYSIZE(wszBuf) > cwc)
	{
	    s_fDumpEnabled = TRUE;
	    s_SaveCount = _wtoi(wszBuf);
	    s_SaveIndex = s_SaveCount;
	}
	s_fEnvChecked = TRUE;
    }
    fSave = 0 != s_SaveCount || (CA_VERIFY_FLAGS_SAVE_CHAIN & dwFlags);
    fDump = s_fDumpEnabled ||
		S_OK != hrVerify ||
		(CA_VERIFY_FLAGS_DUMP_CHAIN & dwFlags);
#if DBG_CERTSRV
    if (DbgIsSSActive(DBG_SS_CERTLIBI))
    {
	fDump = TRUE;
    }
#endif
    if (!fSave && !fDump)
    {
	return;
    }
    if (0 != s_SaveCount)
    {
	if (++s_SaveIndex >= s_SaveCount)
	{
	    s_SaveIndex = 0;
	}
    }
    if (fDump)
    {
	CONSOLEPRINT0((MAXDWORD, "-------- CERT_CHAIN_CONTEXT --------\n"));
	DumpFlags(
		pChainContext->TrustStatus.dwInfoStatus,
		L"ChainContext.dwInfoStatus",
		g_adfInfoStatus);
	DumpFlags(
		pChainContext->TrustStatus.dwErrorStatus,
		L"ChainContext.dwErrorStatus",
		g_adfErrorStatus);
	if (CCSIZEOF_STRUCT(CERT_CHAIN_CONTEXT, dwRevocationFreshnessTime) <=
			pChainContext->cbSize &&
	    pChainContext->fHasRevocationFreshnessTime)
	{
	    DumpChainSeconds(
		L"ChainContext.dwRevocationFreshnessTime",
		pChainContext->dwRevocationFreshnessTime);
	}
    }
    for (i = 0; i < pChainContext->cChain; i++)
    {
	if (fSave)
	{
	    WriteChain(pChainContext->rgpChain[i], s_SaveIndex, i);
	}
	if (fDump)
	{
	    DumpFlags(
		    pChainContext->rgpChain[i]->TrustStatus.dwInfoStatus,
		    L"\nSimpleChain.dwInfoStatus",
		    g_adfInfoStatus);
	    DumpFlags(
		    pChainContext->rgpChain[i]->TrustStatus.dwErrorStatus,
		    L"SimpleChain.dwErrorStatus",
		    g_adfErrorStatus);
	    if (CCSIZEOF_STRUCT(CERT_SIMPLE_CHAIN, dwRevocationFreshnessTime) <=
			    pChainContext->rgpChain[i]->cbSize &&
		pChainContext->rgpChain[i]->fHasRevocationFreshnessTime)
	    {
		DumpChainSeconds(
		    L"SimpleChain.dwRevocationFreshnessTime",
		    pChainContext->rgpChain[i]->dwRevocationFreshnessTime);
	    }
	}
	if (NULL != hHash)
	{
	    CryptDestroyHash(hHash);
	    hHash = NULL;
	}
	if (NULL != hProv)
	{
	    CryptReleaseContext(hProv, 0);
	    hProv = NULL;
	}
	hr = DumpChainOpenHash(&hProv, &hHash);
	_PrintIfError(hr, "DumpChainOpenHash");

	for (j = 0; j < pChainContext->rgpChain[i]->cElement; j++)
	{
	    CERT_CHAIN_ELEMENT const *pElement;

	    pElement = pChainContext->rgpChain[i]->rgpElement[j];

	    if (fDump ||
		S_OK != hrVerify ||
		0 != pElement->TrustStatus.dwErrorStatus)
	    {
		CERT_REVOCATION_INFO *pRevocationInfo;

		CONSOLEPRINT4((
		    MAXDWORD,
		    "\nCertContext[%u][%u]: dwInfoStatus=%x dwErrorStatus=%x\n",
		    i,
		    j,
		    pElement->TrustStatus.dwInfoStatus,
		    pElement->TrustStatus.dwErrorStatus));

		hr = DumpChainCert(pElement->pCertContext);
		_PrintIfError(hr, "DumpChainCert");

		DumpFlags(
			pElement->TrustStatus.dwInfoStatus,
			L"  Element.dwInfoStatus",
			g_adfInfoStatus);
		DumpFlags(
			pElement->TrustStatus.dwErrorStatus,
			L"  Element.dwErrorStatus",
			g_adfErrorStatus);

		if (NULL != pfnCallback)
		{
		    (*pfnCallback)(dwFlags, j, pChainContext->rgpChain[i]);
		}
		pRevocationInfo = pElement->pRevocationInfo;

		if (NULL != pRevocationInfo &&
		    CCSIZEOF_STRUCT(CERT_REVOCATION_INFO, pCrlInfo) <=
			pRevocationInfo->cbSize &&
		    NULL != pRevocationInfo->pCrlInfo)
		{
		    hr = DumpChainCRL(
			    FALSE,
			    pRevocationInfo->pCrlInfo->pBaseCrlContext,
			    hHash);
		    _PrintIfError(hr, "DumpChainCRL");

		    hr = DumpChainCRL(
			    TRUE,
			    pRevocationInfo->pCrlInfo->pDeltaCrlContext,
			    hHash);
		    _PrintIfError(hr, "DumpChainCRL");
		}

		if (CCSIZEOF_STRUCT(CERT_CHAIN_ELEMENT, pIssuanceUsage) <=
		    pElement->cbSize)
		{
		    DumpUsage(L"  Issuance", pElement->pIssuanceUsage);
		}
		if (CCSIZEOF_STRUCT(CERT_CHAIN_ELEMENT, pApplicationUsage) <=
		    pElement->cbSize)
		{
		    DumpUsage(L"  Application", pElement->pApplicationUsage);
		}
		if (CCSIZEOF_STRUCT(CERT_CHAIN_ELEMENT, pwszExtendedErrorInfo) <=
		    pElement->cbSize &&
		    NULL != pElement->pwszExtendedErrorInfo)
		{
		    CONSOLEPRINT1((
			    MAXDWORD,
			    "  %ws",
			    pElement->pwszExtendedErrorInfo));
		}
		if (1 + j == pChainContext->rgpChain[i]->cElement)
		{
		    DumpChainHashResult(hHash, "\nExclude leaf cert");
		}

		hr = DumpChainAddHash(
			    hHash,
			    pElement->pCertContext->pbCertEncoded,
			    pElement->pCertContext->cbCertEncoded);
		_PrintIfError(hr, "DumpChainAddHash");

		if (1 + j == pChainContext->rgpChain[i]->cElement)
		{
		    hr = DumpChainHashResult(hHash, "Full chain");
		    _PrintIfError(hr, "DumpChainAddHash");
		}
	    }
	}
    }
    if (fDump)
    {
	if (S_OK != hrVerify)
	{
	    WCHAR const *pwszErr = myGetErrorMessageText(hrVerify, TRUE);

	    if (NULL != pwszMissingIssuer)
	    {
		CONSOLEPRINT1((
			MAXDWORD,
			"Missing Issuer: %ws\n",
			pwszMissingIssuer));
	    }
	    DumpChainCert(pCert);
	    if (NULL != pwszErr)
	    {
		CONSOLEPRINT1((MAXDWORD, "%ws\n", pwszErr));
		LocalFree(const_cast<WCHAR *>(pwszErr));
	    }
	}
	CONSOLEPRINT0((MAXDWORD, "------------------------------------\n"));
    }
    if (NULL != hHash)
    {
	CryptDestroyHash(hHash);
    }
    if (NULL != hProv)
    {
	CryptReleaseContext(hProv, 0);
    }
}


#pragma warning(push)
#pragma warning(disable: 4706)	// assignment within conditional expression: while (*pwsz++ = *psz++)
HRESULT
SavePolicies(
    OPTIONAL IN CERT_ENHKEY_USAGE const *pUsage,
    OUT WCHAR **ppwszzPolicies)
{
    HRESULT hr;
    DWORD i;
    DWORD cwc;
    char const *psz;
    WCHAR *pwsz;

    // pUsage == NULL means the cert is good for *all* policies.
    // Do nothing here, which returns *ppwszzPolicies == NULL.
    //
    // pUsage->cUsageIdentifier == 0 means the cert is good for *no* policies.
    // Return a double L'\0' terminated string containing no policy OIDs.

    if (NULL != pUsage)
    {
	BOOL fEmpty = 0 == pUsage->cUsageIdentifier ||
			NULL == pUsage->rgpszUsageIdentifier;

	cwc = 1;
	if (fEmpty)
	{
	    cwc++;
	}
	else
	{
	    for (i = 0; i < pUsage->cUsageIdentifier; i++)
	    {
		cwc += strlen(pUsage->rgpszUsageIdentifier[i]) + 1;
	    }
	}
	pwsz = (WCHAR *) LocalAlloc(LMEM_FIXED, cwc * sizeof(WCHAR));
	if (NULL == pwsz)
	{
	    hr = E_OUTOFMEMORY;
	    _JumpError(hr, error, "LocalAlloc");
	}
	*ppwszzPolicies = pwsz;
	if (fEmpty)
	{
	    *pwsz++ = L'\0';
	}
	else
	{
	    for (i = 0; i < pUsage->cUsageIdentifier; i++)
	    {
		psz = pUsage->rgpszUsageIdentifier[i];
		while (*pwsz++ = *psz++)
		    ;
	    }
	}
	*pwsz++ = L'\0';
	CSASSERT(pwsz == &(*ppwszzPolicies)[cwc]);
    }
    hr = S_OK;

error:
    return(hr);
}
#pragma warning(pop)


HRESULT
myVerifyCertContextEx(
    IN CERT_CONTEXT const *pCert,
    IN DWORD dwFlags,
    IN DWORD dwmsTimeout,
    IN DWORD cUsageOids,
    OPTIONAL IN CHAR const * const *apszUsageOids,
    IN DWORD cIssuanceOids,
    OPTIONAL IN CHAR const * const *apszIssuanceOids,
    OPTIONAL IN HCERTCHAINENGINE hChainEngine,
    OPTIONAL IN FILETIME const *pft,
    OPTIONAL IN HCERTSTORE hAdditionalStore,
    OPTIONAL IN FNSIMPLECHAINELEMENTCALLBACK *pfnCallback,
    OPTIONAL OUT WCHAR **ppwszMissingIssuer,
    OPTIONAL OUT WCHAR **ppwszzIssuancePolicies,
    OPTIONAL OUT WCHAR **ppwszzApplicationPolicies,
    OPTIONAL OUT WCHAR **ppwszExtendedErrorInfo,
    OPTIONAL OUT CERT_TRUST_STATUS *pTrustStatus)
{
    HRESULT hr;
    DWORD ChainFlags;
    CERT_CHAIN_PARA ChainParams;
    CERT_CHAIN_POLICY_PARA ChainPolicy;
    CERT_CHAIN_POLICY_STATUS PolicyStatus;
    CERT_CHAIN_CONTEXT const *pChainContext = NULL;
    LPCSTR pszChainPolicyFlags;
    WCHAR *pwszMissingIssuer = NULL;
    CERT_CHAIN_ELEMENT const *pElement;
    WCHAR const *pwsz;

    if (NULL != ppwszMissingIssuer)
    {
	*ppwszMissingIssuer = NULL;
    }
    if (NULL != ppwszzIssuancePolicies)
    {
	*ppwszzIssuancePolicies = NULL;
    }
    if (NULL != ppwszzApplicationPolicies)
    {
	*ppwszzApplicationPolicies = NULL;
    }
    if (NULL != ppwszExtendedErrorInfo)
    {
	*ppwszExtendedErrorInfo = NULL;
    }
    if (NULL != pTrustStatus)
    {
	ZeroMemory(pTrustStatus, sizeof(*pTrustStatus));
    }
    ZeroMemory(&ChainParams, sizeof(ChainParams));
    ChainParams.cbSize = sizeof(ChainParams);
    ChainParams.dwUrlRetrievalTimeout = dwmsTimeout;

    if (0 != cUsageOids && NULL != apszUsageOids)
    {
	ChainParams.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
	ChainParams.RequestedUsage.Usage.cUsageIdentifier = cUsageOids;
	ChainParams.RequestedUsage.Usage.rgpszUsageIdentifier = const_cast<char **>(apszUsageOids);
    }

    if (0 != cIssuanceOids && NULL != apszIssuanceOids)
    {
	ChainParams.RequestedIssuancePolicy.dwType = USAGE_MATCH_TYPE_AND;
	ChainParams.RequestedIssuancePolicy.Usage.cUsageIdentifier = cIssuanceOids;
	ChainParams.RequestedIssuancePolicy.Usage.rgpszUsageIdentifier = const_cast<char **>(apszIssuanceOids);
    }

    ChainFlags = 0;
    if (0 == (CA_VERIFY_FLAGS_NO_REVOCATION & dwFlags))
    {
	if (CA_VERIFY_FLAGS_FULL_CHAIN_REVOCATION & dwFlags)
	{
	    ChainFlags = CERT_CHAIN_REVOCATION_CHECK_CHAIN;
	}
	else
	{
	    ChainFlags = CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
	}
    }
    if (CA_VERIFY_FLAGS_DUMP_CHAIN & dwFlags)
    {
	DumpFlags(dwFlags, L"dwFlags", g_adfVerifyFlags);
	if (0 != cIssuanceOids && NULL != apszIssuanceOids)
	{
	    DumpUsage(L"Issuance", &ChainParams.RequestedIssuancePolicy.Usage);
	}
	if (0 != cUsageOids && NULL != apszUsageOids)
	{
	    DumpUsage(L"Application", &ChainParams.RequestedUsage.Usage);
	}
	DumpFlags(ChainFlags, L"ChainFlags", g_adfChainFlags);
	pwsz = NULL;
	if (HCCE_LOCAL_MACHINE == hChainEngine)
	{
	    pwsz = L"HCCE_LOCAL_MACHINE";
	}
	else if (HCCE_CURRENT_USER == hChainEngine)
	{
	    pwsz = L"HCCE_CURRENT_USER";
	}
	if (NULL != pwsz)
	{
	    CONSOLEPRINT1((MAXDWORD, "%ws\n", pwsz));
	}
    }

    // use NTAuth policy if (usage oids being added) & (caller asks us to check(EntCA))

    pwsz = NULL;
    if (0 != cUsageOids &&
	NULL != apszUsageOids &&
	(CA_VERIFY_FLAGS_NT_AUTH & dwFlags))
    {
	pszChainPolicyFlags = CERT_CHAIN_POLICY_NT_AUTH;
	pwsz = L"CERT_CHAIN_POLICY_NT_AUTH";
    }
    else
    {
	pszChainPolicyFlags = CERT_CHAIN_POLICY_BASE;
	pwsz = L"CERT_CHAIN_POLICY_BASE";
    }
    if (NULL != pwsz && (CA_VERIFY_FLAGS_DUMP_CHAIN & dwFlags))
    {
	CONSOLEPRINT1((MAXDWORD, "%ws\n", pwsz));
    }

    // Get the chain and verify the cert:

    DBGPRINT((DBG_SS_CERTLIBI, "Calling CertGetCertificateChain...\n"));
    if (!CertGetCertificateChain(
			    hChainEngine,	// hChainEngine
			    pCert,		// pCertContext
			    const_cast<FILETIME *>(pft), // pTime
			    hAdditionalStore,	// hAdditionalStore
			    &ChainParams,	// pChainPara
			    ChainFlags,		// dwFlags
			    NULL,		// pvReserved
			    &pChainContext))	// ppChainContext
    {
        hr = myHLastError();
	_JumpError(hr, error, "CertGetCertificateChain");
    }
    DBGPRINT((DBG_SS_CERTLIBI, "CertGetCertificateChain done\n"));

    ZeroMemory(&ChainPolicy, sizeof(ChainPolicy));
    ChainPolicy.cbSize = sizeof(ChainPolicy);
    ChainPolicy.dwFlags = CERT_CHAIN_POLICY_IGNORE_NOT_TIME_NESTED_FLAG;
    //ChainPolicy.pvExtraPolicyPara = NULL;

    ZeroMemory(&PolicyStatus, sizeof(PolicyStatus));
    PolicyStatus.cbSize = sizeof(PolicyStatus);
    //PolicyStatus.dwError = 0;
    PolicyStatus.lChainIndex = -1;
    PolicyStatus.lElementIndex = -1;
    //PolicyStatus.pvExtraPolicyStatus = NULL;

    if (!CertVerifyCertificateChainPolicy(
                                    pszChainPolicyFlags,
				    pChainContext,
				    &ChainPolicy,
				    &PolicyStatus))
    {
        hr = myHLastError();
	_JumpError(hr, error, "CertVerifyCertificateChainPolicy");
    }

    hr = myHError(PolicyStatus.dwError);
    if ((CA_VERIFY_FLAGS_IGNORE_OFFLINE |
	 CA_VERIFY_FLAGS_IGNORE_NOREVCHECK |
	 CA_VERIFY_FLAGS_NO_REVOCATION) & dwFlags)
    {
	if (CRYPT_E_NO_REVOCATION_CHECK == hr ||
	    (CRYPT_E_REVOCATION_OFFLINE == hr &&
	     ((CA_VERIFY_FLAGS_IGNORE_OFFLINE |
	       CA_VERIFY_FLAGS_IGNORE_NOREVCHECK) & dwFlags)))
        {
            hr = S_OK;
        }
    }
    if (CA_VERIFY_FLAGS_ALLOW_UNTRUSTED_ROOT & dwFlags)
    {
        if (CERT_E_UNTRUSTEDROOT == hr)
        {
            hr = S_OK;
        }
    }
    if (CA_VERIFY_FLAGS_IGNORE_INVALID_POLICIES & dwFlags)
    {
	if (CERT_E_INVALID_POLICY == hr)
        {
            hr = S_OK;
        }
    }
    if (S_OK != hr &&
	0 < pChainContext->cChain &&
	0 < pChainContext->rgpChain[0]->cElement)
    {
	pElement = pChainContext->rgpChain[0]->rgpElement[
		    pChainContext->rgpChain[0]->cElement - 1];

	if (0 == (CERT_TRUST_IS_SELF_SIGNED & pElement->TrustStatus.dwInfoStatus))
	{
	    HRESULT hr2;

	    hr2 = myCertNameToStr(
			    X509_ASN_ENCODING,
			    &pElement->pCertContext->pCertInfo->Issuer,
			    CERT_X500_NAME_STR | CERT_NAME_STR_REVERSE_FLAG,
			    &pwszMissingIssuer);
	    _PrintIfError(hr2, "myCertNameToStr");
	}
    }
    if (NULL != pTrustStatus)
    {
	*pTrustStatus = pChainContext->TrustStatus;
    }
    myDumpChain(
	    hr,
	    dwFlags,
	    pCert,
	    pfnCallback,
	    pwszMissingIssuer,
	    pChainContext);

    if (NULL != ppwszMissingIssuer)
    {
	*ppwszMissingIssuer = pwszMissingIssuer;
	pwszMissingIssuer = NULL;
    }
    if (NULL != ppwszExtendedErrorInfo)
    {
	CERT_CHAIN_ELEMENT **ppElement;
	CERT_CHAIN_ELEMENT **ppElementEnd;

	ppElement = &pChainContext->rgpChain[0]->rgpElement[0];
	ppElementEnd = &ppElement[pChainContext->rgpChain[0]->cElement];
	while (ppElement < ppElementEnd)
	{
	    if (CCSIZEOF_STRUCT(CERT_CHAIN_ELEMENT, pwszExtendedErrorInfo) <=
		(*ppElement)->cbSize &&
		NULL != (*ppElement)->pwszExtendedErrorInfo)
	    {
		HRESULT hr2;

		hr2 = myDupString(
			    (*ppElement)->pwszExtendedErrorInfo,
			    ppwszExtendedErrorInfo);
		_PrintIfError(hr2, "myDupString");

		break;
	    }
	    ppElement++;
	}
    }
    _JumpIfError(hr, error, "PolicyStatus.dwError");

    pElement = pChainContext->rgpChain[0]->rgpElement[0];

    if (NULL != ppwszzIssuancePolicies &&
	CCSIZEOF_STRUCT(CERT_CHAIN_ELEMENT, pIssuanceUsage) <= pElement->cbSize)
    {
	hr = SavePolicies(
		    pElement->pIssuanceUsage,
		    ppwszzIssuancePolicies);
	_JumpIfError(hr, error, "SavePolicies");
    }
    if (NULL != ppwszzApplicationPolicies &&
	CCSIZEOF_STRUCT(CERT_CHAIN_ELEMENT, pApplicationUsage) <= pElement->cbSize)
    {
	hr = SavePolicies(
		    pElement->pApplicationUsage,
		    ppwszzApplicationPolicies);
	_JumpIfError(hr, error, "SavePolicies");
    }

error:
    if (S_OK != hr)
    {
	if (NULL != ppwszzIssuancePolicies && NULL != *ppwszzIssuancePolicies)
	{
	    LocalFree(*ppwszzIssuancePolicies);
	    *ppwszzIssuancePolicies = NULL;
	}
	if (NULL != ppwszzApplicationPolicies && NULL != *ppwszzApplicationPolicies)
	{
	    LocalFree(*ppwszzApplicationPolicies);
	    *ppwszzApplicationPolicies = NULL;
	}
    }
    if (NULL != pwszMissingIssuer)
    {
        LocalFree(pwszMissingIssuer);
    }
    if (NULL != pChainContext)
    {
        CertFreeCertificateChain(pChainContext);
    }
    return(hr);
}


HRESULT
myVerifyCertContext(
    IN CERT_CONTEXT const *pCert,
    IN DWORD dwFlags,
    IN DWORD cUsageOids,
    OPTIONAL IN CHAR const * const *apszUsageOids,
    OPTIONAL IN HCERTCHAINENGINE hChainEngine,
    OPTIONAL IN HCERTSTORE hAdditionalStore,
    OPTIONAL OUT WCHAR **ppwszMissingIssuer)
{
    HRESULT hr;

    hr = myVerifyCertContextEx(
			pCert,
			dwFlags,
			0,		// dwmsTimeout
			cUsageOids,
			apszUsageOids,
			0,		// cIssuanceOids
			NULL,		// apszIssuanceOids
			hChainEngine,
			NULL,		// pft
			hAdditionalStore,
			NULL,		// pfnCallback
			ppwszMissingIssuer,
			NULL,		// ppwszzIssuancePolicies
			NULL,		// ppwszzApplicationPolicies
			NULL,		// ppwszExtendedErrorInfo
			NULL);		// pTrustStatus
    _JumpIfError2(hr, error, "myVerifyCertContextEx", hr);

error:
    return(hr);
}


HRESULT
myIsFirstSigner(
    IN CERT_NAME_BLOB const *pNameBlob,
    OUT BOOL *pfFirst)
{
    HRESULT hr;
    CERT_NAME_INFO *pNameInfo = NULL;
    DWORD cbNameInfo;
    DWORD i;

    *pfFirst = FALSE;

    if (!myDecodeName(
		X509_ASN_ENCODING,
		X509_UNICODE_NAME,
		pNameBlob->pbData,
		pNameBlob->cbData,
		CERTLIB_USE_LOCALALLOC,
		&pNameInfo,
		&cbNameInfo))
    {

	hr = myHLastError();
	_JumpError(hr, error, "myDecodeName");
    }
    for (i = 0; i < pNameInfo->cRDN; i++)
    {
	CERT_RDN const *prdn;
	DWORD j;

	prdn = &pNameInfo->rgRDN[i];

	for (j = 0; j < prdn->cRDNAttr; j++)
	{
	    if (0 == strcmp(
			prdn->rgRDNAttr[j].pszObjId,
			szOID_RDN_DUMMY_SIGNER))
	    {
		*pfFirst = TRUE;
		i = pNameInfo->cRDN;	// terminate outer loop
		break;
	    }
	}
    }
    hr = S_OK;

error:
    if (NULL != pNameInfo)
    {
	LocalFree(pNameInfo);
    }
    return(hr);
}


HCERTSTORE
myPFXImportCertStore(
    IN CRYPT_DATA_BLOB *ppfx,
    OPTIONAL IN WCHAR const *pwszPassword,
    IN DWORD dwFlags)
{
    HCERTSTORE hStore;
    HRESULT hr;

    if (NULL == pwszPassword)
    {
	pwszPassword = L"";	// Try empty password first, then NULL
    }

    for (;;)
    {
	hStore = PFXImportCertStore(ppfx, pwszPassword, dwFlags);
	if (NULL == hStore)
	{
	    hr = myHLastError();
	    if (HRESULT_FROM_WIN32(ERROR_INVALID_PASSWORD) != hr ||
		NULL == pwszPassword ||
		L'\0' != *pwszPassword)
	    {
		_JumpError2(
			hr,
			error,
			"PFXImportCertStore",
			HRESULT_FROM_WIN32(ERROR_INVALID_PASSWORD));
	    }
	    pwszPassword = NULL;	// empty password failed; try NULL
	    continue;
	}
	break;
    }


error:
    return(hStore);
}


// No longer support versions before IE3.02 - Auth2 update, advisory only

HRESULT
CertCheck7f(
    IN CERT_CONTEXT const *pcc)
{
    HRESULT hr;
    DWORD State;
    DWORD Index1;
    DWORD Index2;
    DWORD cwcField;
    WCHAR wszField[128];
    DWORD cwcObjectId;
    WCHAR wszObjectId[128];
    WCHAR const *pwszObjectIdDescription = NULL;

    cwcField = sizeof(wszField)/sizeof(wszField[0]);
    cwcObjectId = sizeof(wszObjectId)/sizeof(wszObjectId[0]);
    hr = myCheck7f(
		pcc->pbCertEncoded,
		pcc->cbCertEncoded,
		FALSE,
		&State,
		&Index1,
		&Index2,
		&cwcField,
		wszField,
		&cwcObjectId,
		wszObjectId,
		&pwszObjectIdDescription);	// Static: do not free!
    _JumpIfError(hr, error, "myCheck7f");

    if (CHECK7F_NONE != State)
    {
	hr = CERTSRV_E_ENCODING_LENGTH;

#if DBG_CERTSRV
	WCHAR wszIndex[5 + 2 * cwcDWORDSPRINTF];

	wszIndex[0] = L'\0';
	if (0 != Index1)
	{
	    wsprintf(
		wszIndex,
		0 != Index2? L"[%u,%u]" : L"[%u]",
		Index1 - 1,
		Index2 - 1);
	}
	DBGPRINT((
	    DBG_SS_CERTLIB,
	    "CertCheck7f: %ws%ws%ws%ws%ws%ws%ws, hr=%x\n",
	    wszField,
	    wszIndex,
	    0 != cwcObjectId? L" ObjectId=" : L"",
	    0 != cwcObjectId? wszObjectId : L"",
	    NULL != pwszObjectIdDescription? L" " wszLPAREN : L"",
	    NULL != pwszObjectIdDescription? pwszObjectIdDescription : L"",
	    NULL != pwszObjectIdDescription? wszRPAREN : L"",
	    hr));
#endif // DBG_CERTSRV
    }
error:
    return(hr);
}


HRESULT
myAddCertToStore(
    IN HCERTSTORE                          hStore,
    IN CERT_CONTEXT const                 *pCertContext,
    OPTIONAL IN CRYPT_KEY_PROV_INFO const *pkpi,
    OPTIONAL OUT CERT_CONTEXT const      **ppCert)
{
    HRESULT hr;
    CERT_CONTEXT const *pcc = NULL;

    if (NULL != ppCert)
    {
	*ppCert = NULL;
    }

    // for root cert, if it shows related private key, it will
    // pfx import failure for other applications

    // Add as encoded blob to avoid all properties, key prov info, etc.

    if (!CertAddEncodedCertificateToStore(
		    hStore,
		    X509_ASN_ENCODING,
		    pCertContext->pbCertEncoded,
		    pCertContext->cbCertEncoded,
		    NULL != pkpi?
			CERT_STORE_ADD_REPLACE_EXISTING :
			CERT_STORE_ADD_USE_EXISTING,
		    &pcc))			// ppCertContext
    {
        hr = myHLastError();
        _JumpError(hr, error, "CertAddEncodedCertificateToStore");
    }
    if (NULL != pkpi)
    {
	if (!CertSetCertificateContextProperty(
					    pcc,
					    CERT_KEY_PROV_INFO_PROP_ID,
					    0,
					    pkpi))
	{
	    hr = myHLastError();
	    _JumpError(hr, error, "CertSetCertificateContextProperty");
	}
    }
    if (NULL != ppCert)
    {
	*ppCert = pcc;
	pcc = NULL;
    }
    hr = S_OK;

error:
    if (NULL != pcc)
    {
	CertFreeCertificateContext(pcc);
    }
    return(hr);
}


HRESULT
mySaveChainAndKeys(
    IN CERT_SIMPLE_CHAIN const *pSimpleChain,
    IN WCHAR const *pwszStore,
    IN DWORD dwStoreFlags,
    IN CRYPT_KEY_PROV_INFO const *pkpi,
    OPTIONAL IN CERT_CONTEXT const **ppCert)
{
    HRESULT hr;
    HCERTSTORE hRootStore = NULL;
    HCERTSTORE hCAStore = NULL;
    HCERTSTORE hMyStore = NULL;
    DWORD i;

    if (NULL != ppCert)
    {
	*ppCert = NULL;
    }
    hRootStore = CertOpenStore(
			CERT_STORE_PROV_SYSTEM_REGISTRY_W,
			X509_ASN_ENCODING,
			NULL,		// hProv
			dwStoreFlags,
			wszROOT_CERTSTORE);
    if (NULL == hRootStore)
    {
        hr = myHLastError();
        _JumpErrorStr(hr, error, "CertOpenStore", wszROOT_CERTSTORE);
    }

    hCAStore = CertOpenStore(
			CERT_STORE_PROV_SYSTEM_REGISTRY_W,
			X509_ASN_ENCODING,
			NULL,		// hProv
			dwStoreFlags,
			wszCA_CERTSTORE);
    if (NULL == hCAStore)
    {
        hr = myHLastError();
        _JumpErrorStr(hr, error, "CertOpenStore", wszCA_CERTSTORE);
    }

    hMyStore = CertOpenStore(
			CERT_STORE_PROV_SYSTEM_REGISTRY_W,
			X509_ASN_ENCODING,
			NULL,		// hProv
			dwStoreFlags,
			pwszStore);
    if (NULL == hMyStore)
    {
        hr = myHLastError();
        _JumpErrorStr(hr, error, "CertOpenStore", pwszStore);
    }

    for (i = 0; i < pSimpleChain->cElement; i++)
    {
	CERT_CONTEXT const *pcc = pSimpleChain->rgpElement[i]->pCertContext;
	HCERTSTORE hStore;

//	CertCheck7f(pcc);

        // if leaf CA cert, add to MY store

        if (0 == i)
        {
            CERT_CONTEXT const *pccFound = CertFindCertificateInStore(
						    hMyStore,
						    X509_ASN_ENCODING,
						    0,
						    CERT_FIND_EXISTING,
						    pcc,
						    NULL);

            if (NULL == pccFound)
            {
		hr = myAddCertToStore(hMyStore, pcc, pkpi, ppCert);
		_JumpIfError(hr, error, "myAddCertToStore");
            }
            else
            {
                if (NULL != ppCert)
                {
                    *ppCert = pccFound;
                }
                else
                {
                    CertFreeCertificateContext(pccFound);
                }
            }
        }

        // if root cert, add to ROOT store (without key); else add to CA store

	hStore = hCAStore;

	if (CERT_TRUST_IS_SELF_SIGNED &
	    pSimpleChain->rgpElement[i]->TrustStatus.dwInfoStatus)
	{
	    hStore = hRootStore;
	}
	hr = myAddCertToStore(hStore, pcc, NULL, NULL);
	_JumpIfError(hr, error, "myAddCertToStore");
    }
    hr = S_OK;

error:
    if (NULL != hRootStore)
    {
        CertCloseStore(hRootStore, CERT_CLOSE_STORE_CHECK_FLAG);
    }
    if (NULL != hCAStore)
    {
        CertCloseStore(hCAStore, CERT_CLOSE_STORE_CHECK_FLAG);
    }
    if (NULL != hMyStore)
    {
        CertCloseStore(hMyStore, CERT_CLOSE_STORE_CHECK_FLAG);
    }
    return(hr);
}


HRESULT
myGetNameIdExtension(
    IN DWORD cExtension,
    IN CERT_EXTENSION const *rgExtension,
    OUT DWORD *pdwNameId)
{
    HRESULT hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    CERT_EXTENSION const *pExt;
    DWORD NameId;
    DWORD cb;

    *pdwNameId = MAXDWORD;
    pExt = CertFindExtension(
			szOID_CERTSRV_CA_VERSION,
			cExtension,
			const_cast<CERT_EXTENSION *>(rgExtension));
    if (NULL == pExt)
    {
	// This API doesn't set LastError
	_JumpError(hr, error, "CertFindExtension(CA Version)");
    }
    cb = sizeof(NameId);
    NameId = 0;
    if (!CryptDecodeObject(
			X509_ASN_ENCODING,
			X509_INTEGER,
			pExt->Value.pbData,
			pExt->Value.cbData,
			0,
			&NameId,
			&cb))
    {
	hr = myHLastError();
	_JumpError(hr, error, "CryptDecodeObject");
    }
    *pdwNameId = NameId;
    hr = S_OK;

error:
    return(hr);
}


HRESULT
myGetNameId(
    IN CERT_CONTEXT const *pCACert,
    OUT DWORD *pdwNameId)
{
    HRESULT hr;

    hr = myGetNameIdExtension(
		    pCACert->pCertInfo->cExtension,
		    pCACert->pCertInfo->rgExtension,
		    pdwNameId);
    _JumpIfError2(hr, error, "myGetNameIdExtension", hr);

error:
    return(hr);
}


HRESULT
myGetCRLNameId(
    IN CRL_CONTEXT const *pCRL,
    OUT DWORD *pdwNameId)
{
    HRESULT hr;

    hr = myGetNameIdExtension(
		    pCRL->pCrlInfo->cExtension,
		    pCRL->pCrlInfo->rgExtension,
		    pdwNameId);
    _JumpIfError2(hr, error, "myGetNameIdExtension", hr);

error:
    return(hr);
}


HRESULT
myGetCertSubjectField(
    IN CERT_CONTEXT const *pCert,
    IN LPCSTR pcszFieldOID,
    OUT WCHAR **ppwszField)
{
    HRESULT hr;
    CERT_NAME_INFO *pCertNameInfo = NULL;
    DWORD cbCertNameInfo;
    WCHAR const *pwszName;

    if (!myDecodeName(
		    X509_ASN_ENCODING,
                    X509_UNICODE_NAME,
                    pCert->pCertInfo->Subject.pbData,
                    pCert->pCertInfo->Subject.cbData,
                    CERTLIB_USE_LOCALALLOC,
                    &pCertNameInfo,
                    &cbCertNameInfo))
    {
        hr = myHLastError();
	_JumpError(hr, error, "myDecodeName");
    }
    hr = myGetCertNameProperty(
			CERT_V1 == pCert->pCertInfo->dwVersion,
			pCertNameInfo,
			pcszFieldOID,
			&pwszName);
    _JumpIfError(hr, error, "myGetCertNameProperty");

    *ppwszField = (WCHAR *) LocalAlloc(
				    LMEM_FIXED,
				    (wcslen(pwszName) + 1) * sizeof(WCHAR));
    if (NULL == *ppwszField)
    {
	hr = E_OUTOFMEMORY;
	_JumpError(hr, error, "LocalAlloc");
    }
    wcscpy(*ppwszField, pwszName);
    hr = S_OK;

error:
    if (NULL != pCertNameInfo)
    {
	LocalFree(pCertNameInfo);
    }
    return(hr);
}

HRESULT
myGetCertSubjectCommonName(
    IN CERT_CONTEXT const *pCert,
    OUT WCHAR **ppwszCommonName)
{
    return myGetCertSubjectField(
        pCert,
        szOID_COMMON_NAME,
        ppwszCommonName);
}

HRESULT
myCertGetNameString(
    IN CERT_CONTEXT const *pcc,
    IN DWORD dwType,
    OUT WCHAR **ppwszSimpleName)
{
    HRESULT hr;
    WCHAR *pwsz = NULL;

    *ppwszSimpleName = NULL;
    if (CERT_NAME_SIMPLE_DISPLAY_TYPE == dwType &&
	CERT_V1 == pcc->pCertInfo->dwVersion)
    {
	hr = myGetCertSubjectCommonName(pcc, &pwsz);
	_PrintIfError(hr, "myGetCertSubjectCommonName");
    }
    if (NULL == pwsz)
    {
	DWORD cwc = 0;

	for (;;)
	{
	    cwc = CertGetNameString(
			    pcc,
			    CERT_NAME_SIMPLE_DISPLAY_TYPE,
			    0,			// dwFlags
			    NULL,		// pvTypePara
			    pwsz,
			    cwc);
	    if (1 >= cwc)
	    {
		hr = HRESULT_FROM_WIN32(ERROR_OBJECT_NOT_FOUND);
		_JumpError(hr, error, "CertGetNameString");
	    }
	    if (NULL != pwsz)
	    {
		break;
	    }
	    pwsz = (WCHAR *) LocalAlloc(LMEM_FIXED, cwc * sizeof(WCHAR));
	    if (NULL == pwsz)
	    {
		hr = E_OUTOFMEMORY;
		_JumpError(hr, error, "LocalAlloc");
	    }
	}
    }
    *ppwszSimpleName = pwsz;
    pwsz = NULL;
    hr = S_OK;

error:
    if (NULL != pwsz)
    {
	LocalFree(pwsz);
    }
    return(hr);
}


HRESULT
myCertStrToName(
    IN DWORD dwCertEncodingType,
    IN LPCWSTR pszX500,
    IN DWORD dwStrType,
    IN OPTIONAL void *pvReserved,
    OUT BYTE **ppbEncoded,
    OUT DWORD *pcbEncoded,
    OUT OPTIONAL LPCWSTR *ppszError)
{
    HRESULT hr;

    *ppbEncoded = NULL;
    *pcbEncoded = 0;

    for (;;)
    {
	if (!CertStrToName(
		    dwCertEncodingType,
		    pszX500,
		    dwStrType,
		    pvReserved,
		    *ppbEncoded,
		    pcbEncoded,
		    ppszError))
	{
	    hr = myHLastError();
	    if (NULL != *ppbEncoded)
	    {
		LocalFree(*ppbEncoded);
		*ppbEncoded = NULL;
	    }
	    _JumpError(hr, error, "CertStrToName");
	}
	if (NULL != *ppbEncoded)
	{
	    break;
	}
	*ppbEncoded = (BYTE *) LocalAlloc(LMEM_FIXED, *pcbEncoded);
	if (NULL == *ppbEncoded)
	{
	    hr = E_OUTOFMEMORY;
	    _JumpError(hr, error, "LocalAlloc");
	}
    }
    hr = S_OK;

error:
    return(hr);
}


HRESULT
myCertNameToStr(
    IN DWORD dwCertEncodingType,
    IN CERT_NAME_BLOB const *pName,
    IN DWORD dwStrType,
    OUT WCHAR **ppwszName)
{
    HRESULT hr;
    DWORD cwc = 0;
    WCHAR *pwszName = NULL;

    for (;;)
    {
	cwc = CertNameToStr(
			dwCertEncodingType,
			const_cast<CERT_NAME_BLOB *>(pName),
			dwStrType,
			pwszName,
			cwc);
	if (1 > cwc)
	{
	    hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
	    _JumpError(hr, error, "CertNameToStr");
	}
	if (NULL != pwszName)
	{
	    break;
	}
	pwszName = (WCHAR *) LocalAlloc(
				    LMEM_FIXED,
				    (cwc + 1) * sizeof(WCHAR));
	if (NULL == pwszName)
	{
	    hr = E_OUTOFMEMORY;
	    _JumpError(hr, error, "LocalAlloc");
	}
    }
    *ppwszName = pwszName;
    pwszName = NULL;
    hr = S_OK;

error:
    if (NULL != pwszName)
    {
	LocalFree(pwszName);
    }
    return(hr);
}


HRESULT
myVerifyKRACertContext(
    IN CERT_CONTEXT const *pCert,
    IN DWORD dwFlags)
{
    HRESULT hr;
    WCHAR *pwszzAppPolicies = NULL;
    WCHAR *pwszCrt;
    DWORD dwKeyUsage;
    DWORD cb = sizeof(dwKeyUsage);

    hr = myVerifyCertContextEx(
			pCert,
			dwFlags,
			0,			// dwmsTimeout
			0,                      // cUsageOids
			NULL,                   // apszUsageOids
			0,			// cIssuanceOids
			NULL,			// apszIssuanceOids
			HCCE_LOCAL_MACHINE,     // hChainEngine
			NULL,			// pft
			NULL,                   // hAdditionalStore
			NULL,			// pfnCallback
			NULL,                   // ppwszMissingIssuer
			NULL,			// ppwszzIssuancePolicies
			&pwszzAppPolicies,
			NULL,			// ppwszExtendedErrorInfo
			NULL);			// pTrustStatus
    _JumpIfError(hr, error, "myVerifyCertContextEx");


    if (!CertGetIntendedKeyUsage(
            X509_ASN_ENCODING,
            pCert->pCertInfo,
            (BYTE*)&dwKeyUsage,
            cb))
    {
        hr = myHLastError();
        _JumpError(hr, error, "CertGetIntendedKeyUsage");
    }

    if(!(dwKeyUsage & CERT_KEY_ENCIPHERMENT_KEY_USAGE))
    {
        hr = CERT_E_WRONG_USAGE;
        _JumpError(hr, error, "Key usage not containing key encipherment");
    }

    hr = CERT_E_WRONG_USAGE;
    for (pwszCrt = pwszzAppPolicies;
         pwszCrt && L'\0' != *pwszCrt;
         pwszCrt += wcslen(pwszCrt) + 1)
    {
        if(0==wcscmp(TEXT(szOID_KP_KEY_RECOVERY_AGENT), pwszCrt))
        {
            hr = S_OK;
            break;
        }
    }
    _JumpIfError(hr, error, "myVerifyKRACertContext");

error:
    if(pwszzAppPolicies)
    {
        LocalFree(pwszzAppPolicies);
    }
    return(hr);
}


HRESULT
myIsDeltaCRL(
    IN CRL_CONTEXT const *pCRL,
    OUT BOOL *pfIsDeltaCRL)
{
    HRESULT hr;
    CERT_EXTENSION *pExt;

    *pfIsDeltaCRL = FALSE;
    pExt = CertFindExtension(
		    szOID_DELTA_CRL_INDICATOR,
		    pCRL->pCrlInfo->cExtension,
		    pCRL->pCrlInfo->rgExtension);
    if (NULL != pExt)
    {
	*pfIsDeltaCRL = TRUE;
    }
    hr = S_OK;

//error:
    return(hr);
}


typedef BOOL (WINAPI fnCryptRetrieveObjectByUrlW) (
    IN LPCWSTR pszUrl,
    IN LPCSTR pszObjectOid,
    IN DWORD dwRetrievalFlags,
    IN DWORD dwTimeout,
    OUT LPVOID* ppvObject,
    IN HCRYPTASYNC hAsyncRetrieve,
    IN OPTIONAL PCRYPT_CREDENTIALS pCredentials,
    IN OPTIONAL LPVOID pvVerify,
    IN OPTIONAL PCRYPT_RETRIEVE_AUX_INFO pAuxInfo
    );

HCERTSTORE
myUrlCertOpenStore(
    IN DWORD dwFlags,
    IN WCHAR const *pwszURL)
{
    HRESULT hr;
    HMODULE hModule = NULL;
    fnCryptRetrieveObjectByUrlW *pfn = NULL;
    HCERTSTORE hStore = NULL;

    hModule = LoadLibrary(TEXT("cryptnet.dll"));
    if (NULL == hModule)
    {
        hr = myHLastError();
        _JumpError(hr, error, "LoadLibrary(cryptnet.dll)");
    }

    pfn = (fnCryptRetrieveObjectByUrlW *) GetProcAddress(
            hModule,
            "CryptRetrieveObjectByUrlW");
    if (NULL == pfn)
    {
        hr = myHLastError();
        _JumpError(hr, error, "CryptRetrieveObjectByUrl");
    }

    if (!(*pfn)(
            pwszURL,
            CONTEXT_OID_CAPI2_ANY,
            dwFlags,
            csecLDAPTIMEOUT * 1000,	// ms
            (VOID **) &hStore,
            NULL,
            NULL,
            NULL,
            NULL))
    {
	hr = myHLastError();
	_JumpErrorStr(hr, error, "CryptRetrieveObjectByUrl", pwszURL);
    }
    hr = S_OK;

error:
    if (NULL != hModule)
    {
        FreeLibrary(hModule);
    }
    if (NULL == hStore)
    {
	SetLastError(hr);
    }
    return(hStore);
}


HRESULT
mySetEnablePrivateKeyUsageCount(
    IN HCRYPTPROV hProv,
    IN BOOL fEnabled)
{
    HRESULT hr;
    DWORD dwEnableKeyUsageCount = fEnabled? 1 : 0;

    if (!CryptSetProvParam(
		    hProv,
		    PP_CRYPT_COUNT_KEY_USE,
		    (BYTE *) &dwEnableKeyUsageCount,
		    0))
    {
        hr = myHLastError();
        _JumpErrorStr(hr, error, "CryptSetProvParam", L"PP_CRYPT_COUNT_KEY_USE");
    }
    hr = S_OK;

error:
    return(hr);
}


HRESULT
myGetSigningKeyUsageCount(
    IN HCRYPTPROV hProv,
    OUT BOOL *pfSupported,
    OUT BOOL *pfEnabled,
    OPTIONAL OUT ULARGE_INTEGER *puliCount)
{
    HRESULT hr;
    HCRYPTKEY hKey = NULL;
    DWORD cb;
    DWORD dwEnableKeyUsageCount;
    ULARGE_INTEGER uliCount;
    BOOL fPropSupported;

    *pfSupported = FALSE;
    *pfEnabled = FALSE;
    if (NULL != puliCount)
    {
	puliCount->QuadPart = 0;
    }
#define CSP_DBGPRINT
#ifdef CSP_DBGPRINT
    uliCount.QuadPart = 0;	// zero only for the debug print
#endif

    // An old CSP supports setting PP_CRYPT_COUNT_KEY_USE, but not fetching!
    // The new CSP supports setting and fetching PP_CRYPT_COUNT_KEY_USE,
    // but always returns with dwEnableKeyUsageCount set to zero on a freshly
    // acquired hProv (the flag is not persistent, and is only used to control
    // key use counting for newly created keys).
    // Always fetch the actual count, if the CSP supports the feature.
    // To distinguish between supported but not enabled & enabled but not yet
    // used (count is zero), fetching the actual count should fail when not
    // enabled, leaving *pfEnabled set to FALSE.

    cb = sizeof(dwEnableKeyUsageCount);
    fPropSupported = CryptGetProvParam(
				hProv,
				PP_CRYPT_COUNT_KEY_USE,
				(BYTE *) &dwEnableKeyUsageCount,
				&cb,
				0);
    if (!fPropSupported)
    {
        hr = myHLastError();
        _PrintErrorStr2(
		    hr,
		    "CryptGetProvParam",
		    L"PP_CRYPT_COUNT_KEY_USE",
		    NTE_BAD_TYPE);
    }
    else
    {
	*pfSupported = TRUE;
	if (!CryptGetUserKey(hProv, AT_SIGNATURE, &hKey))
	{
	    hr = myHLastError();
	    _JumpError(hr, error, "CryptGetUserKey");
	}
	cb = sizeof(uliCount);
	if (!CryptGetKeyParam(
			hKey,
			KP_GET_USE_COUNT,
			(BYTE *) &uliCount,
			&cb,
			0))
	{
	    hr = myHLastError();
	    _PrintErrorStr2(
			hr,
			"CryptGetKeyParam",
			L"KP_GET_USE_COUNT",
			NTE_BAD_TYPE);
	}
	else
	{
	    *pfEnabled = TRUE;
	    if (NULL != puliCount)
	    {
		*puliCount = uliCount;
	    }
	}
    }
    hr = S_OK;

error:
#ifdef CSP_DBGPRINT
    DBGPRINT((
	DBG_SS_CERTLIB,
	"myGetSigningKeyUsageCount:%hs hr=%x Supported=%u Enabled=%u Count=%I64u\n",
	fPropSupported? "NEW" : "OLD",
	hr,
	*pfSupported,
	*pfEnabled,
	NULL != puliCount? puliCount->QuadPart : uliCount.QuadPart));
#endif
    if (NULL != hKey)
    {
	CryptDestroyKey(hKey);
    }
    return(hr);
}

HRESULT
myCertGetEnhancedKeyUsage(
    IN CERT_CONTEXT const *pcc,
    IN DWORD dwFlags,
    OUT CERT_ENHKEY_USAGE **ppUsage)
{
    HRESULT hr;
    CERT_ENHKEY_USAGE *pUsage = NULL;
    DWORD cb;

    *ppUsage = NULL;

    for (;;)
    {
	if (!CertGetEnhancedKeyUsage(pcc, dwFlags, pUsage, &cb))
	{
	    hr = myHLastError();
	    _JumpError2(hr, error, "CertGetEnhancedKeyUsage", CRYPT_E_NOT_FOUND);
	}
	if (NULL != pUsage)
	{
	    *ppUsage = pUsage;
	    pUsage = NULL;
	    break;
	}
	pUsage = (CERT_ENHKEY_USAGE *) LocalAlloc(LMEM_FIXED, cb);
	if (NULL == pUsage)
	{
	    hr = E_OUTOFMEMORY;
	    _JumpError(hr, error, "LocalAlloc");
	}
    }
    hr = S_OK;

error:
    if (NULL != pUsage)
    {
	LocalFree(pUsage);
    }
    return(hr);
}

// if app policies extension was empty, myConvertAppPoliciesToEKU returns S_OK and NULL pbEKU
HRESULT
myConvertAppPoliciesToEKU(
    IN BYTE * pbAppPolicies,
    IN DWORD cbAppPolicies,
    OUT BYTE **ppbEKU,
    OUT DWORD *pcbEKU)
{
    HRESULT hr = S_OK;
    CERT_POLICIES_INFO *pcpsi = NULL;
    DWORD cb, i;
    CERT_ENHKEY_USAGE ceu;

    ZeroMemory(&ceu, sizeof(ceu));
    *ppbEKU = NULL;
    *pcbEKU = 0;

    if (!myDecodeObject(
            X509_ASN_ENCODING,
            X509_CERT_POLICIES,
            pbAppPolicies,
            cbAppPolicies,
            CERTLIB_USE_LOCALALLOC,
            (VOID **) &pcpsi,
            &cb))
    {
        hr = myHLastError();
        _JumpError(hr, error, "myDecodeObject");
    }

    if(0 < pcpsi->cPolicyInfo)
    {
        ceu.rgpszUsageIdentifier = (char **) LocalAlloc(
            LMEM_FIXED,
            pcpsi->cPolicyInfo * sizeof(ceu.rgpszUsageIdentifier[0]));
        if (NULL == ceu.rgpszUsageIdentifier)
        {
            hr = E_OUTOFMEMORY;
            _JumpError(hr, error, "Policy:myLocalAlloc");
        }

        for (i = 0; i < pcpsi->cPolicyInfo; i++)
        {
            ceu.rgpszUsageIdentifier[i] = pcpsi->rgPolicyInfo[i].pszPolicyIdentifier;
        }

        ceu.cUsageIdentifier = pcpsi->cPolicyInfo;

        if (!myEncodeObject(
                X509_ASN_ENCODING,
                X509_ENHANCED_KEY_USAGE,
                &ceu,
                0,
                CERTLIB_USE_LOCALALLOC,
                ppbEKU,
                pcbEKU))
        {
            hr = myHLastError();
            _JumpError(hr, error, "Policy:myEncodeObject");
        }
    }

    hr = S_OK;

error:
    if (NULL != ceu.rgpszUsageIdentifier)
    {
        LocalFree(ceu.rgpszUsageIdentifier);
    }
    return hr;
}
