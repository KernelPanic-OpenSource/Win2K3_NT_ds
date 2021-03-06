/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  Microsoft Windows, Copyright (C) Microsoft Corporation, 2000

  File:    EKU.cpp

  Content: Implementation of CEKU.

  History: 11-15-99    dsie     created

------------------------------------------------------------------------------*/

#include "StdAfx.h"
#include "CAPICOM.h"
#include "EKU.h"

////////////////////////////////////////////////////////////////////////////////
//
// Exported functions.
//

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  Function : CreateEKUObject

  Synopsis : Create an IEKU object and initialize the object with data
             from the specified OID.

  Parameter: LPTSTR * pszOID - Pointer to EKU OID string.
  
             IEKU ** ppIEKU - Pointer to pointer IEKU object.
             
  Remark   : 

------------------------------------------------------------------------------*/

HRESULT CreateEKUObject (LPSTR pszOID, IEKU ** ppIEKU)
{
    HRESULT            hr    = S_OK;
    CComObject<CEKU> * pCEKU = NULL;
    CAPICOM_EKU        EkuName;
    CComBSTR           bstrValue;

    DebugTrace("Entering CreateEKUObject().\n");

    //
    // Sanity check.
    //
    ATLASSERT(ppIEKU);

    try
    {
        //
        // Create the object. Note that the ref count will still be 0 
        // after the object is created.
        //
        if (FAILED(hr = CComObject<CEKU>::CreateInstance(&pCEKU)))
        {
            DebugTrace("Error [%#x]: CComObject<CEKU>::CreateInstance() failed.\n", hr);
            goto ErrorExit;
        }

        //
        // Determine EKU enum name.
        //
        if (NULL == pszOID)
        {
            EkuName = CAPICOM_EKU_OTHER;
        }
        else if (0 == ::strcmp(CAPICOM_OID_SERVER_AUTH, pszOID))
        {
            EkuName = CAPICOM_EKU_SERVER_AUTH;
        }
        else if (0 == ::strcmp(CAPICOM_OID_CLIENT_AUTH, pszOID))
        {
            EkuName = CAPICOM_EKU_CLIENT_AUTH;
        }
        else if (0 == ::strcmp(CAPICOM_OID_CODE_SIGNING, pszOID))
        {
            EkuName = CAPICOM_EKU_CODE_SIGNING;
        }
        else if (0 == ::strcmp(CAPICOM_OID_EMAIL_PROTECTION, pszOID))
        {
            EkuName = CAPICOM_EKU_EMAIL_PROTECTION;
        }
        else if (0 == ::strcmp(CAPICOM_OID_SMART_CARD_LOGON, pszOID))
        {
            EkuName = CAPICOM_EKU_SMARTCARD_LOGON;
        }
        else if (0 == ::strcmp(CAPICOM_OID_ENCRYPTING_FILE_SYSTEM, pszOID))
        {
            EkuName = CAPICOM_EKU_ENCRYPTING_FILE_SYSTEM;
        }
        else
        {
            EkuName = CAPICOM_EKU_OTHER;
        }

        //
        // Initialize object.
        //
        if (FAILED(hr = pCEKU->Init(EkuName, pszOID)))
        {
            DebugTrace("Error [%#x]: pCEKU->Init() failed.\n", hr);
            goto ErrorExit;
        }

        //
        // Return interface pointer to caller.
        //
        if (FAILED(hr = pCEKU->QueryInterface(ppIEKU)))
        {
            DebugTrace("Error [%#x]: pCEKU->QueryInterface() failed.\n", hr);
            goto ErrorExit;
        }
    }

    catch(...)
    {
        hr = E_POINTER;

        DebugTrace("Exception: invalid parameter.\n");
        goto ErrorExit;
    }

CommonExit:

    DebugTrace("Leaving CreateEKUObject().\n");

    return hr;

ErrorExit:
    //
    // Sanity check.
    //
    ATLASSERT(FAILED(hr));

    if (pCEKU)
    {
        delete pCEKU;
    }

    goto CommonExit;
}


////////////////////////////////////////////////////////////////////////////////
//
// CEKU
//

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  Function : CEKU::get_Name

  Synopsis : Return the enum name of the EKU.

  Parameter: CAPICOM_EKU * pVal - Pointer to CAPICOM_EKU to receive result.

  Remark   :

------------------------------------------------------------------------------*/

STDMETHODIMP CEKU::get_Name (CAPICOM_EKU * pVal)
{
    HRESULT hr = S_OK;

    DebugTrace("Entering CEKU::get_Name().\n");

    try
    {
        //
        // Lock access to this object.
        //
        m_Lock.Lock();

        //
        // Check parameters.
        //
        if (NULL == pVal)
        {
            hr = E_INVALIDARG;

            DebugTrace("Error [%#x]: Parameter pVal is NULL.\n", hr);
            goto ErrorExit;
        }

        //
        // Return result.
        //
          *pVal = m_Name;
    }

    catch(...)
    {
        hr = E_POINTER;

        DebugTrace("Exception: invalid parameter.\n");
        goto ErrorExit;
    }

UnlockExit:
    //
    // Unlock access to this object.
    //
    m_Lock.Unlock();

    DebugTrace("Leaving CEKU::get_Name().\n");

    return hr;

ErrorExit:
    //
    // Sanity check.
    //
    ATLASSERT(FAILED(hr));

    ReportError(hr);

    goto UnlockExit;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  Function : CEKU::put_Name

  Synopsis : Set EKU enum name.

  Parameter: CAPICOM_EKU newVal - EKU enum name.
  
  Remark   : The corresponding EKU value will be set for all except EKU_OTHER,
             in which case the user must make another explicit call to 
             put_Value to set it.

------------------------------------------------------------------------------*/

STDMETHODIMP CEKU::put_Name (CAPICOM_EKU newVal)
{
    HRESULT hr = S_OK;

    DebugTrace("Entering CEKU::put_Name().\n");

    //
    // Lock access to this object.
    //
    m_Lock.Lock();

    //
    // Reset value based on EKU name.
    //
    switch (newVal)
    {
        case CAPICOM_EKU_OTHER:
        {
            m_bstrOID.Empty();
            break;
        }

        case CAPICOM_EKU_SERVER_AUTH:
        {
            if (!(m_bstrOID = CAPICOM_OID_SERVER_AUTH))
            {
                hr = E_OUTOFMEMORY;

                DebugTrace("Error [%#x]: m_bstrOID = CAPICOM_OID_SERVER_AUTH failed.\n", hr);
                goto ErrorExit;
            }
            break;
        }

        case CAPICOM_EKU_CLIENT_AUTH:
        {
            if (!(m_bstrOID = CAPICOM_OID_CLIENT_AUTH))
            {
                hr = E_OUTOFMEMORY;

                DebugTrace("Error [%#x]: m_bstrOID = CAPICOM_OID_CLIENT_AUTH failed.\n", hr);
                goto ErrorExit;
            }
            break;
        }

        case CAPICOM_EKU_CODE_SIGNING:
        {
            if (!(m_bstrOID = CAPICOM_OID_CODE_SIGNING))
            {
                hr = E_OUTOFMEMORY;

                DebugTrace("Error [%#x]: m_bstrOID = CAPICOM_OID_CODE_SIGNING failed.\n", hr);
                goto ErrorExit;
            }
            break;
        }

        case CAPICOM_EKU_EMAIL_PROTECTION:
        {
            if (!(m_bstrOID = CAPICOM_OID_EMAIL_PROTECTION))
            {
                hr = E_OUTOFMEMORY;

                DebugTrace("Error [%#x]: m_bstrOID = CAPICOM_OID_EMAIL_PROTECTION failed.\n", hr);
                goto ErrorExit;
            }
            break;
        }

        case CAPICOM_EKU_SMARTCARD_LOGON:
        {
            if (!(m_bstrOID = CAPICOM_OID_SMART_CARD_LOGON))
            {
                hr = E_OUTOFMEMORY;

                DebugTrace("Error [%#x]: m_bstrOID = CAPICOM_OID_SMART_CARD_LOGON failed.\n", hr);
                goto ErrorExit;
            }
            break;
        }

        case CAPICOM_EKU_ENCRYPTING_FILE_SYSTEM:
        {
            if (!(m_bstrOID = CAPICOM_OID_ENCRYPTING_FILE_SYSTEM))
            {
                hr = E_OUTOFMEMORY;

                DebugTrace("Error [%#x]: m_bstrOID = CAPICOM_OID_ENCRYPTING_FILE_SYSTEM failed.\n", hr);
                goto ErrorExit;
            }
            break;
        }

        default:
        {
            hr = E_INVALIDARG;

            DebugTrace("Error [%#x]: Unknown EKU name (%#x).\n", hr, newVal);
            goto ErrorExit;
        }
    }

    //
    // Store name.
    //
    m_Name = newVal;

UnlockExit:
    //
    // Unlock access to this object.
    //
    m_Lock.Unlock();

    DebugTrace("Leaving CEKU::put_Name().\n");

    return hr;

ErrorExit:
    //
    // Sanity check.
    //
    ATLASSERT(FAILED(hr));

    ReportError(hr);

    goto UnlockExit;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  Function : CEKU::get_OID

  Synopsis : Return the actual OID string of the EKU.

  Parameter: BSTR * pVal - Pointer to BSTR to receive value.

  Remark   :

------------------------------------------------------------------------------*/

STDMETHODIMP CEKU::get_OID (BSTR * pVal)
{
    HRESULT hr = S_OK;

    DebugTrace("Entering CEKU::get_OID().\n");

    try
    {
        //
        // Lock access to this object.
        //
        m_Lock.Lock();

        //
        // Check parameters.
        //
        if (NULL == pVal)
        {
            hr = E_INVALIDARG;

            DebugTrace("Error [%#x]: Parameter pVal is NULL.\n", hr);
            goto ErrorExit;
        }

        //
        // Return result.
        //
        if (FAILED(hr = m_bstrOID.CopyTo(pVal)))
        {
            DebugTrace("Error [%#x]: m_bstrOID.CopyTo() failed.\n", hr);
            goto ErrorExit;
        }
    }

    catch(...)
    {
        hr = E_POINTER;

        DebugTrace("Exception: invalid parameter.\n");
        goto ErrorExit;
    }

UnlockExit:
    //
    // Unlock access to this object.
    //
    m_Lock.Unlock();

    DebugTrace("Leaving CEKU::get_OID().\n");

    return hr;

ErrorExit:
    //
    // Sanity check.
    //
    ATLASSERT(FAILED(hr));

    ReportError(hr);

    goto UnlockExit;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  Function : CEKU::put_OID

  Synopsis : Set EKU actual OID string value.

  Parameter: BSTR newVal - EKU OID string.

  Remark   :

------------------------------------------------------------------------------*/

STDMETHODIMP CEKU::put_OID (BSTR newVal)
{
    HRESULT hr = S_OK;

    DebugTrace("Entering CEKU::put_OID().\n");

    try
    {
        //
        // Lock access to this object.
        //
        m_Lock.Lock();

        //
        // Make sure Name property is CAPICOM_EKU_OTHER.
        //
        if (CAPICOM_EKU_OTHER != m_Name)
        {
            hr = CAPICOM_E_EKU_INVALID_OID;

            DebugTrace("Error [%#x]: attemp to set EKU OID, when EKU name is not CAPICOM_EKU_OTHER.\n", hr);
            goto ErrorExit;
        }

        //
        // Store value.
        //
        if (NULL == newVal)
        {
            m_bstrOID.Empty();
        }
        else if (!(m_bstrOID = newVal))
        {
            hr = E_OUTOFMEMORY;

            DebugTrace("Error [%#x]: m_bstrOID = newVal failed.\n", hr);
            goto ErrorExit;
        }
    }

    catch(...)
    {
        hr = E_POINTER;

        DebugTrace("Exception: invalid parameter.\n");
        goto ErrorExit;
    }

UnlockExit:
    //
    // Unlock access to this object.
    //
    m_Lock.Unlock();

    DebugTrace("Leaving CEKU::put_OID().\n");

    return hr;

ErrorExit:
    //
    // Sanity check.
    //
    ATLASSERT(FAILED(hr));

    ReportError(hr);

    goto UnlockExit;
}


////////////////////////////////////////////////////////////////////////////////
//
// Private methods.
//

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  Function : CEKU::Init

  Synopsis : Initialize the object.

  Parameter: CAPICOM_EKU EkuName - Enum name of EKU.

             LPSTR lpszOID - EKU OID string.

  Remark   : This method is not part of the COM interface (it is a normal C++
             member function). We need it to initialize the object created 
             internally by us with CERT_CONTEXT.

             Since it is only a normal C++ member function, this function can
             only be called from a C++ class pointer, not an interface pointer.
             
------------------------------------------------------------------------------*/

STDMETHODIMP CEKU::Init (CAPICOM_EKU EkuName, 
                         LPSTR       lpszOID)
{
    HRESULT hr = S_OK;

    DebugTrace("Entering CEKU::Init().\n");

    //
    // Explicitly empty the BSTR to work around ATL's bug where it calls 
    // SysAllocStringLen() with -1 when the right-hand value is NULL, and 
    // caused OLEAUT32.DLL to assert in checked build. 
    //
    // Note: ATL fixed this problem in VC 7.
    //
    if (NULL == lpszOID)
    {
        m_bstrOID.Empty();
    }
    else if (!(m_bstrOID = lpszOID))
    {
        hr = E_OUTOFMEMORY;

        DebugTrace("Error [%#x]: m_bstrOID = lpszOID failed.\n", hr);
        goto ErrorExit;
    }

    //
    // Init private members.
    //
    m_Name = EkuName;

CommonExit:

    DebugTrace("Leaving CEKU::Init().\n");

    return hr;

ErrorExit:
    //
    // Sanity check.
    //
    ATLASSERT(FAILED(hr));

    goto CommonExit;
}
