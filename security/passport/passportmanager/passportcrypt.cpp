/**********************************************************************/
/**                       Microsoft Passport                         **/
/**                Copyright(c) Microsoft Corporation, 1999 - 2001   **/
/**********************************************************************/

/*
    PassportCrypt.cpp


    FILE HISTORY:

*/


// PassportCrypt.cpp : Implementation of CCrypt
#include "stdafx.h"
#include "Passport.h"
#include "PassportCrypt.h"
#include <time.h>

/* moved into passport.idl -- so consumer of the COM API can see it
// max blocks + 10 should be multiples of 3 for simplicity
#define ENC_MAX_SIZE  2045
// I don't trust the compiler... (((2045+10)*4)/3)+9 = 2749 * sizeof(wchar)
#define DEC_MAX_SIZE  5498
*/

//===========================================================================
//
// CCrypt 
//
CCrypt::CCrypt() : m_crypt(NULL), m_szSiteName(NULL), m_szHostName(NULL)
{
    m_pUnkMarshaler = NULL;
    m_keyVersion = 0;

    CRegistryConfig* crc = g_config->checkoutRegistryConfig();
    if( crc )
    {
        m_keyVersion = crc->getCurrentCryptVersion();
        crc->getCrypt(m_keyVersion, &m_validUntil);
        crc->Release();
    }
}


/////////////////////////////////////////////////////////////////////////////
// CCrypt

//===========================================================================
//
// InterfaceSupportsErrorInfo 
//
STDMETHODIMP CCrypt::InterfaceSupportsErrorInfo(REFIID riid)
{
  static const IID* arr[] = 
  {
    &IID_IPassportCrypt,
  };
  for (int i=0;i<sizeof(arr)/sizeof(arr[0]);i++)
    {
      if (InlineIsEqualGUID(*arr[i],riid))
	return S_OK;
    }
  return S_FALSE;
}


//===========================================================================
//
// OnStartPage 
//
STDMETHODIMP CCrypt::OnStartPage(IUnknown* piUnk) 
{
    BOOL                    bHasPort;
    DWORD                   dwServerNameLen;
    HRESULT                 hr = S_OK;
    BOOL                    bVariantInited = FALSE;

    // param needs to cleanup
    IRequestPtr             piRequest ;
    IRequestDictionaryPtr   piServerVariables ;
    _variant_t              vtItemName;
    _variant_t              vtServerName;
    _variant_t              vtServerPort;
    _variant_t              vtHTTPS;
//    WCHAR                   szServerName = NULL; 
//    CHAR*                   szServerName_A = NULL; 
    CRegistryConfig*        crc =  NULL;


    if(!piUnk)
    {
        hr = E_POINTER;
        goto exit;
    }

    if (!g_config->isValid()) // Guarantees config is non-null
    {
        AtlReportError(CLSID_Manager, PP_E_NOT_CONFIGUREDSTR,
                       IID_IPassportManager, PP_E_NOT_CONFIGURED);
        hr = PP_E_NOT_CONFIGURED;
    }

    try
    {

        // Get Request Object Pointer
        piRequest = ((IScriptingContextPtr)piUnk)->Request;

        //
        //  Use the request object to get the server name being requested
        //  so we can get the correct registry config.  But only do this
        //  if we have some configured sites.
        //

        if(g_config->HasSites())
        {
            piRequest->get_ServerVariables(&piServerVariables);

            vtItemName.vt = VT_BSTR;
            vtItemName.bstrVal = SysAllocString(L"SERVER_NAME");
            if (NULL == vtItemName.bstrVal)
            {
                hr = E_OUTOFMEMORY;
                goto exit;
            }

            piServerVariables->get_Item(vtItemName, &vtServerName);
            if(vtServerName.vt != VT_BSTR)
                VariantChangeType(&vtServerName, &vtServerName, 0, VT_BSTR);

            VariantClear(&vtItemName);

            vtItemName.vt = VT_BSTR;
            vtItemName.bstrVal = SysAllocString(L"SERVER_PORT");
            if (NULL == vtItemName.bstrVal)
            {
                hr = E_OUTOFMEMORY;
                goto exit;
            }

            piServerVariables->get_Item(vtItemName, &vtServerPort);
            if(vtServerPort.vt != VT_BSTR)
                VariantChangeType(&vtServerPort, &vtServerPort, 0, VT_BSTR);

            VariantClear(&vtItemName);
            vtItemName.vt = VT_BSTR;
            vtItemName.bstrVal = SysAllocString(L"HTTPS");
            if (NULL == vtItemName.bstrVal)
            {
                hr = E_OUTOFMEMORY;
                goto exit;
            }

            hr = piServerVariables->get_Item(vtItemName, &vtHTTPS);
            if(vtHTTPS.vt != VT_BSTR)
                VariantChangeType(&vtHTTPS, &vtHTTPS, 0, VT_BSTR);

            //  If not default port, append ":port" to server name.
            bHasPort = (lstrcmpiW(L"off", vtHTTPS.bstrVal) == 0 && 
                        lstrcmpW(L"80", vtServerPort.bstrVal) != 0) || 
                        (lstrcmpiW(L"on", vtHTTPS.bstrVal) == 0 && 
                        lstrcmpW(L"443", vtServerPort.bstrVal) != 0); 
            dwServerNameLen = bHasPort ?   
                        lstrlenW(vtServerName.bstrVal) + lstrlenW(vtServerPort.bstrVal) + 2 :
                        lstrlenW(vtServerName.bstrVal) + 1;

            m_szHostName = new CHAR[dwServerNameLen];
            if( !m_szHostName )
            {
                hr = E_OUTOFMEMORY;
                goto exit;
            }

            WideCharToMultiByte(CP_ACP, 0, vtServerName.bstrVal, -1,
                    m_szHostName, dwServerNameLen,
                    NULL,
                    NULL);

            if(bHasPort)
            {
                USES_CONVERSION;
                lstrcatA(m_szHostName, ":");
                lstrcatA(m_szHostName, W2A(vtServerPort.bstrVal));
            }

            crc = g_config->checkoutRegistryConfig(m_szHostName);
        }
        else
        {
            crc = g_config->checkoutRegistryConfig();
        }

        m_keyVersion = 0;
        if (crc)
        {
            m_keyVersion = crc->getCurrentCryptVersion();
            crc->getCrypt(m_keyVersion,&m_validUntil);
        }
    }
    catch(...)
    {
        hr = S_OK;
    }

exit:

    if( crc )
        crc->Release();

    return hr;
}

//===========================================================================
//
// Encrypt 
//
STDMETHODIMP CCrypt::Encrypt(BSTR rawData, BSTR *pEncrypted)
{
    if (!rawData)
        return E_INVALIDARG;

    if (SysStringLen(rawData) > ENC_MAX_SIZE)
    {
        AtlReportError(CLSID_Crypt, L"Passport.Crypt: Data too large", 
                       IID_IPassportCrypt, E_FAIL);
        return E_FAIL;
    }

    if (m_crypt)
    {
        if (!m_crypt->Encrypt(m_keyVersion, (LPSTR)rawData, SysStringByteLen(rawData), pEncrypted))
        {
            AtlReportError(CLSID_Crypt, 
                           L"Encryption failed", IID_IPassportCrypt, E_FAIL);
            return E_FAIL;
        }
    }
    else
    {
        CRegistryConfig* crc = ObtainCRC();
        if (!crc)
        {
            AtlReportError(CLSID_Crypt, 
                           L"Passport misconfigured", IID_IPassportCrypt, E_FAIL);
            return E_FAIL;
        }
        CCoCrypt *cr = crc->getCrypt(m_keyVersion,&m_validUntil);
        if (!cr)
        {
            AtlReportError(CLSID_Crypt, 
                           L"No such key version", IID_IPassportCrypt, E_FAIL);
            crc->Release();
            return E_FAIL;
        }
        if (!cr->Encrypt(m_keyVersion,(LPSTR)rawData,SysStringByteLen(rawData),pEncrypted))
        {
             AtlReportError(CLSID_Crypt, 
                            L"Encryption failed", IID_IPassportCrypt, E_FAIL);
             crc->Release();
             return E_FAIL;
        }
        crc->Release();
    }
    return S_OK;
}

//===========================================================================
//
// Decrypt
//
STDMETHODIMP CCrypt::Decrypt(BSTR rawData, BSTR *pUnencrypted)
{
    if (rawData == NULL)
    {
      *pUnencrypted = NULL;
      return S_OK;
    }

    if (SysStringLen(rawData) > DEC_MAX_SIZE)
    {
      AtlReportError(CLSID_Crypt, L"Passport.Crypt: Data too large",
             IID_IPassportCrypt, E_FAIL);
      return E_FAIL;
    }

    if (m_crypt) // Just do our job, no questions
    {
        if (m_crypt->Decrypt(rawData, SysStringByteLen(rawData), pUnencrypted))
        {
            return S_OK;
        }

        if(g_pAlert)
            g_pAlert->report(PassportAlertInterface::WARNING_TYPE, PM_FAILED_DECRYPT);

        *pUnencrypted = NULL;
        return S_OK;
    }

    // First find the key version
    int kv = CCoCrypt::getKeyVersion(rawData);
    time_t vU, now;

    CRegistryConfig* crc = ObtainCRC();
    if (!crc)
    {
        AtlReportError(CLSID_Crypt,
               L"Passport misconfigured", IID_IPassportCrypt, PP_E_NOT_CONFIGURED);
        return PP_E_NOT_CONFIGURED;
    }
    CCoCrypt *cr = crc->getCrypt(kv, &vU);

    time(&now);

    if ((vU != 0 && now > vU) || cr == NULL)
    {
        *pUnencrypted = NULL;
        if(g_pAlert)
            g_pAlert->report(PassportAlertInterface::WARNING_TYPE, PM_FAILED_DECRYPT);
        crc->Release();
        return S_OK;
    }

    if (cr->Decrypt(rawData, SysStringByteLen(rawData), pUnencrypted))
    {
      crc->Release();
      return S_OK;
    }
  
    if(g_pAlert)
        g_pAlert->report(PassportAlertInterface::WARNING_TYPE, PM_FAILED_DECRYPT);
  
    crc->Release();
    *pUnencrypted = NULL;
    return S_OK;
}

//===========================================================================
//
// get_keyVersion 
//
STDMETHODIMP CCrypt::get_keyVersion(int *pVal)
{
  *pVal = m_keyVersion;
  return S_OK;
}

//===========================================================================
//
// put_keyVersion 
//
STDMETHODIMP CCrypt::put_keyVersion(int newVal)
{
  m_keyVersion = newVal;
  if (m_crypt)
    {
      delete m_crypt;
      m_crypt = NULL;
    }
  return S_OK;
}

//===========================================================================
//
// vget_IsValid 
//
STDMETHODIMP CCrypt::get_IsValid(VARIANT_BOOL *pVal)
{
// fix 6695	PassportCrypt.IsValid is inconsistent to end users.
// *pVal = (m_crypt != NULL) ? VARIANT_TRUE : VARIANT_FALSE;

  *pVal = (g_config->isValid()) ? VARIANT_TRUE : VARIANT_FALSE;
  return S_OK;
}

//===========================================================================
//
// put_keyMaterial 
//
STDMETHODIMP CCrypt::put_keyMaterial(BSTR newVal)
{
    CCoCrypt *pcccTemp = new CCoCrypt();

    if (pcccTemp == NULL)
    {
        return E_OUTOFMEMORY;
    }

    if (m_crypt)
    {
        delete m_crypt;
    }

    m_crypt = pcccTemp;

    m_crypt->setKeyMaterial(newVal);
    return S_OK;
}


//===========================================================================
//
// Compress 
//
STDMETHODIMP CCrypt::Compress(
    BSTR    bstrIn,
    BSTR*   pbstrCompressed
    )
{
    HRESULT hr;
    UINT    nInLen;

    //
    //  Check inputs.
    //

    if(bstrIn == NULL ||
       pbstrCompressed == NULL)
    {
        hr = E_POINTER;
        goto Cleanup;
    }

    //
    //  nInLen does not include the terminating NULL.
    //

    nInLen = SysStringLen(bstrIn);

    //
    //  Always want to allocate an even number of bytes
    //  so that the corresponding decompress does not
    //  lose characters.
    //

    if(nInLen & 0x1)
        nInLen++;

    //
    //  Allocate a BSTR of the correct length.
    //

    *pbstrCompressed = SysAllocStringByteLen(NULL, nInLen);
    if(*pbstrCompressed == NULL)
    {
        hr = E_OUTOFMEMORY;
        goto Cleanup;
    }

    //
    //  We allocated a total of nInLen + 2 bytes.  Zero it out.
    //

    memset(*pbstrCompressed, 0, nInLen + sizeof(OLECHAR));

    //
    //  Convert to multibyte.
    //

    if (0 == WideCharToMultiByte(CP_ACP, 0, bstrIn, nInLen, (LPSTR)*pbstrCompressed, 
                        nInLen + 1, // this is how many bytes were allocated by
                                                        // SysAllocStringByteLen
                        NULL, NULL))
    {
        hr = E_FAIL;
    }
    else
    {
        hr = S_OK;
    }

Cleanup:
    
    return hr;
}


//===========================================================================
//
// Decompress 
//
STDMETHODIMP CCrypt::Decompress(
    BSTR    bstrIn,
    BSTR*   pbstrDecompressed
    )
{
    HRESULT hr;
    CHAR    *pch;
    UINT    nInLen;

    if(bstrIn == NULL ||
       pbstrDecompressed == NULL)
    {
        hr = E_POINTER;
        goto Cleanup;
    }

    //
    //  nInLen is number of mbc's, and does not include the terminating NULL.
    //

    nInLen = SysStringByteLen(bstrIn);

    //
    //	13386: return NULL if 0 length
    //
    if (nInLen == 0)
    {
        *pbstrDecompressed = NULL;
        hr = S_OK;
        goto Cleanup;
    }
	
    pch = (CHAR*)bstrIn;
    if ('\0' == pch[nInLen - 1])
    {
        nInLen--;
    }

    //
    //  Allocate a BSTR of the correct length.
    //

    *pbstrDecompressed = SysAllocStringLen(NULL, nInLen);
    if(*pbstrDecompressed == NULL)
    {
        hr = E_OUTOFMEMORY;
        goto Cleanup;
    }

    //
    //  We allocated a total of (nOutLen+1) * sizeof(OLECHAR) bytes, since
    //  SysAllocStringLen allocs an extra character.  Zero it all out.
    //

    memset(*pbstrDecompressed, 0, (nInLen + 1) * sizeof(OLECHAR));

    //
    //  Convert to wide.
    //

    if (0 == MultiByteToWideChar(CP_ACP, 0, (LPCSTR)bstrIn, -1, *pbstrDecompressed, nInLen + 1))
    {
        hr = E_FAIL;
    }
    else
    {
        hr = S_OK;
    }
Cleanup:
    
    return hr;
}


//===========================================================================
//
// put_site 
//
STDMETHODIMP
CCrypt::put_site(
    BSTR    bstrSiteName
    )
{
    HRESULT             hr;
    int                 nLen;
    LPSTR               szNewSiteName;
    CRegistryConfig*    crc;

    if(!bstrSiteName)
    {
        if(m_szSiteName)
            delete [] m_szSiteName;
        m_szSiteName = NULL;
    }
    else
    {
        nLen = SysStringLen(bstrSiteName) + 1;
        szNewSiteName = new CHAR[nLen];
        if(!szNewSiteName)
        {
            hr = E_OUTOFMEMORY;
            goto Cleanup;
        }

        WideCharToMultiByte(CP_ACP, 0, bstrSiteName, -1,
                            szNewSiteName, nLen,
                            NULL,
                            NULL);


        Cleanup();

        m_szSiteName = szNewSiteName;
    }

    crc = ObtainCRC();
    if (!crc)
    {
        m_keyVersion = 0;
    }
    else
    {
        m_keyVersion = crc->getCurrentCryptVersion();
        crc->getCrypt(m_keyVersion,&m_validUntil);
        crc->Release();
    }

    hr = S_OK;

Cleanup:

    return hr;
}

//===========================================================================
//
// put_host 
//
STDMETHODIMP
CCrypt::put_host(
    BSTR    bstrHostName
    )
{
    HRESULT             hr;
    int                 nLen;
    LPSTR               szNewHostName;
    CRegistryConfig*    crc;

    if(!bstrHostName)
    {
        if(m_szHostName)
            delete [] m_szHostName;
        m_szHostName = NULL;
    }
    else
    {

        nLen = SysStringLen(bstrHostName) + 1;
        szNewHostName = new CHAR[nLen];
        if(!szNewHostName)
        {
            hr = E_OUTOFMEMORY;
            goto Cleanup;
        }

        WideCharToMultiByte(CP_ACP, 0, bstrHostName, -1,
                            szNewHostName, nLen,
                            NULL,
                            NULL);

        Cleanup();
        m_szHostName = szNewHostName;

        crc = ObtainCRC();
        if (!crc)
        {
            m_keyVersion = 0;
        }
        else
        {
            m_keyVersion = crc->getCurrentCryptVersion();
            crc->getCrypt(m_keyVersion,&m_validUntil);
            crc->Release();
        }
    }
    hr = S_OK;

Cleanup:

    return hr;
}

//===========================================================================
//
// Cleanup 
//
void CCrypt::Cleanup()
{
    if( m_szSiteName )
    {
        delete [] m_szSiteName;
        m_szSiteName = NULL;
    }

    if( m_szHostName )
    {
        delete [] m_szHostName;
        m_szHostName = NULL;
    }
}
    

//===========================================================================
//
// ObtainCRC 
//
CRegistryConfig* CCrypt::ObtainCRC()
{
    CRegistryConfig* crc = NULL;

    if( m_szHostName && m_szSiteName )
    {
        // we are in bad state now
        Cleanup();
        goto exit;
    } 
    
    if( m_szHostName )
        crc = g_config->checkoutRegistryConfig(m_szHostName);

    if( m_szSiteName )
        crc = g_config->checkoutRegistryConfigBySite(m_szSiteName);

    // if we still can't get crc at this moment, try the default one
    if( !crc )
        crc = g_config->checkoutRegistryConfig();

exit:
    return crc;
}

/////////////////////////////////////////////////////////////////////////////
// IPassportService implementation

//===========================================================================
//
// Initialize 
//
STDMETHODIMP CCrypt::Initialize(BSTR configfile, IServiceProvider* p)
{
    HRESULT hr;

    // Initialize.
    if (!g_config->isValid())
    {
        AtlReportError(CLSID_Crypt, PP_E_NOT_CONFIGUREDSTR,
	                    IID_IPassportService, PP_E_NOT_CONFIGURED);
        hr = PP_E_NOT_CONFIGURED;
        goto Cleanup;
    }

    hr = S_OK;

Cleanup:

    return hr;
}


//===========================================================================
//
// Shutdown 
//
STDMETHODIMP CCrypt::Shutdown()
{
    return S_OK;
}


//===========================================================================
//
// ReloadState 
//
STDMETHODIMP CCrypt::ReloadState(IServiceProvider*)
{
    return S_OK;
}


//===========================================================================
//
// CommitState 
//
STDMETHODIMP CCrypt::CommitState(IServiceProvider*)
{
    return S_OK;
}


//===========================================================================
//
// DumpState 
//
STDMETHODIMP CCrypt::DumpState(BSTR* pbstrState)
{
	ATLASSERT( *pbstrState != NULL && 
               "CCrypt:DumpState - "
               "Are you sure you want to hand me a non-null BSTR?" );

	HRESULT hr = S_OK;

	return hr;
}

