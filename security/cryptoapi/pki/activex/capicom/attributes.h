/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  Microsoft Windows

  Copyright (C) Microsoft Corporation, 1995 - 1999.

  File:    Attributes.h

  Content: Declaration of CAttributes.

  History: 11-15-99    dsie     created

------------------------------------------------------------------------------*/

#ifndef __ATTRIBUTES_H_
#define __ATTRIBUTES_H_

#include "Resource.h"
#include "Lock.h"
#include "Debug.h"
#include "CopyItem.h"
#include "Attribute.h"

//
// typdefs to make life easier.
//
typedef std::map<CComBSTR, CComPtr<IAttribute> > AttributeMap;
typedef CComEnumOnSTL<IEnumVARIANT, &IID_IEnumVARIANT, VARIANT, _CopyMapItem<IAttribute>, AttributeMap> AttributeEnum;
typedef ICollectionOnSTLImpl<IAttributes, AttributeMap, VARIANT, _CopyMapItem<IAttribute>, AttributeEnum> IAttributesCollection;


////////////////////////////////////////////////////////////////////////////////
//
// Exported functions.
//

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  Function : CreateAttributesObject

  Synopsis : Create and initialize an IAttributes collection object.

  Parameter: CRYPT_ATTRIBUTES * pAttrbibutes - Pointer to attributes to be 
                                               added to the collection object.
  
             IAttributes ** ppIAttributes - Pointer to pointer to IAttributes 
                                            to receive the interface pointer.
             
  Remark   : 

------------------------------------------------------------------------------*/

HRESULT CreateAttributesObject (CRYPT_ATTRIBUTES * pAttributes,
                                IAttributes     ** ppIAttributes);


////////////////////////////////////////////////////////////////////////////////
//
// CAttributes
//

class ATL_NO_VTABLE CAttributes : 
    public CComObjectRootEx<CComMultiThreadModel>,
    public CComCoClass<CAttributes, &CLSID_Attributes>,
    public ICAPICOMError<CAttributes, &IID_IAttributes>,
    public IDispatchImpl<IAttributesCollection, &IID_IAttributes, &LIBID_CAPICOM,
                         CAPICOM_MAJOR_VERSION, CAPICOM_MINOR_VERSION>
{
public:
    CAttributes()
    {
    }

DECLARE_NO_REGISTRY()

DECLARE_GET_CONTROLLING_UNKNOWN()

DECLARE_PROTECT_FINAL_CONSTRUCT()

BEGIN_COM_MAP(CAttributes)
    COM_INTERFACE_ENTRY(IAttributes)
    COM_INTERFACE_ENTRY(IDispatch)
    COM_INTERFACE_ENTRY(ISupportErrorInfo)
END_COM_MAP()

BEGIN_CATEGORY_MAP(CAttributes)
END_CATEGORY_MAP()

    HRESULT FinalConstruct()
    {
        HRESULT hr;

        if (FAILED(hr = m_Lock.Initialized()))
        {
            DebugTrace("Error [%#x]: Critical section could not be created for Attributes object.\n", hr);
            return hr;
        }

        m_dwNextIndex = 0;

        return S_OK;
    }

//
// IAttributes
//
public:
    //
    // These are the only ones that we need to implemented, others will be
    // handled by ATL ICollectionOnSTLImpl.
    //
    STDMETHOD(Clear)
        (void);

    STDMETHOD(Remove)
        (/*[in]*/ long Index);

    STDMETHOD(Add)
        (/*[in]*/ IAttribute * pVal);

    //
    // None COM functions.
    //
    STDMETHOD(Init)
        (CRYPT_ATTRIBUTES * pAttributes);

private:
    CLock   m_Lock;
    DWORD   m_dwNextIndex;
};
#endif //__ATTRIBUTES_H_
