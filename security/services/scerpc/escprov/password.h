// password.h: interface for the CPasswordPolicy class.
//
// Copyright (c)1997-1999 Microsoft Corporation
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_PASSWORD_H__BD7570F7_9F0E_4C6B_B525_E078691B6D0E__INCLUDED_)
#define AFX_PASSWORD_H__BD7570F7_9F0E_4C6B_B525_E078691B6D0E__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#include "GenericClass.h"

/*

Class description
    
    Naming: 

        CPasswordPolicy stands for Security Options.
    
    Base class: 

        CGenericClass, because it is a class representing a WMI  
        object - its WMI class name is Sce_PasswordPolicy
    
    Purpose of class:
    
        (1) Implement Sce_PasswordPolicy WMI class.
    
    Design:
         
        (1) Almost trivial other than implementing necessary method as a concrete class
    
    Use:

        (1) Almost never used directly. Alway through the common interface defined by
            CGenericClass.
    
    
*/

class CPasswordPolicy : public CGenericClass
{
public:
        CPasswordPolicy (
                        ISceKeyChain *pKeyChain, 
                        IWbemServices *pNamespace, 
                        IWbemContext *pCtx = NULL
                        );

        virtual ~CPasswordPolicy();

        virtual HRESULT PutInst (
                                IWbemClassObject *pInst, 
                                IWbemObjectSink *pHandler, 
                                IWbemContext *pCtx
                                );

        virtual HRESULT CreateObject (
                                     IWbemObjectSink *pHandler, 
                                     ACTIONTYPE atAction
                                     );

private:

        HRESULT ConstructInstance (
                                  IWbemObjectSink *pHandler, 
                                  CSceStore* pSceStore, 
                                  LPWSTR wszLogStorePath, 
                                  ACTIONTYPE atAction
                                  );

        HRESULT DeleteInstance (
                                IWbemObjectSink *pHandler,
                                CSceStore* pSceStore
                               );

        HRESULT SaveSettingsToStore (
                                    CSceStore* pSceStore, 
                                    DWORD dwMinAge, 
                                    DWORD dwMaxAge, 
                                    DWORD dwMinLen, 
                                    DWORD dwHistory, 
                                    DWORD dwComplexity, 
                                    DWORD dwClear, 
                                    DWORD dwForce, 
                                    DWORD dwLSAPol, 
                                    DWORD dwAdmin, 
                                    DWORD dwGuest
                                    );

};

#endif // !defined(AFX_PASSWORD_H__BD7570F7_9F0E_4C6B_B525_E078691B6D0E__INCLUDED_)
