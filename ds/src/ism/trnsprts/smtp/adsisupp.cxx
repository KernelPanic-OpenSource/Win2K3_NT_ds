/*++

Copyright (c) 1998  Microsoft Corporation

Module Name:

    adsisupp.cxx

Abstract:

    ADSI support routines

    This package contains general ADSI helper routines, as well as routines to
    query and manipulate data in the metabase for particular services.

    ADSI is a general COM interface to any kind of hierarchical data store.
    Many components in the system register themselves as providers to the asdi
    namespace, and thus become managable through it.  We are managing the
    Smtpservice in the way.

    You can view the structure of the namespace using adsvw.

    The structure of the smtp service is:

    IIS:/LocalHost/SMTPSVC/
        1/ (class IIsSmtpServer)
            .
            .
            Domain (class Container)
                <your domain here> (Class IIsSmtpDomain)

        Info/ (class IIsSmtpInfo)

    There are many interesting properties on the server and domain objects that
    may prove useful in the future.

    The routines here include:
        Determine if a given Smtp mail routing domain is present
        Create a smtp mail routing domain

Author:

    Will Lees (wlees) 07-Oct-1998

Environment:

Notes:

Revision History:

--*/

#define INC_OLE2

#include <ntdspchx.h>

#include <ismapi.h>
#include <debug.h>

// Logging headers.
// TODO: better place to put these?
typedef ULONG MessageId;
typedef ULONG ATTRTYP;
#include "dsevent.h"                    /* header Audit\Alert logging */
#include "mdcodes.h"                    /* header for error codes */

#include <fileno.h>
#define  FILENO FILENO_ISMSERV_ADSISUPP

#include "common.h"
#include "ismsmtp.h"
#include "support.hxx"

#define DEBSUB "ADSISUPP:"

#include <activeds.h>

/* External */

/* Static */
static LPWSTR EmptyClassList[] = { NULL };

#define ADS_DOMAIN_PATH L"IIS://LocalHost/SMTPSVC/1/Domain"
#define ADS_DOMAIN_PATH_LENGTH (ARRAY_SIZE(DROP_DIRECTORY))

/* Forward */ /* Generated by Emacs 19.34.1 on Mon Apr 26 09:46:36 1999 */

HRESULT
AddSmtpDomainIfNeeded(
    LPWSTR DomainName,
    BSTR bstrDropDirectory
    );

HRESULT __cdecl
domainCallback(
    PVOID pObjectOpaque,
    PVOID Context1,
    PVOID Context2
    );

HRESULT
addSmtpDomain(
    LPWSTR AdsPath,
    LPWSTR DomainName,
    BSTR bstrDropDirectory
    );

HRESULT
ModifySmtpDomainIfNeeded(
    LPWSTR AdsContainerPath,
    LPWSTR DomainName,
    BSTR bstrDropDirectory
    );

HRESULT
EnumObject(
    LPWSTR pszADsPath,
    LPWSTR * lppClassNames,
    DWORD dwClassNames,
    ENUM_CALLBACK_FN *pCallback,
    PVOID Context1,
    PVOID Context2
    );

HRESULT
getSmtpServerProperties(
    LPWSTR AdsPath,
    BSTR *pbstrDropDirectory
    );

HRESULT
getPropBstr(
    IADs *pObject,
    LPWSTR PropertyName,
    BSTR *pbstrValue
    );

HRESULT
putPropInteger(
    IADs *pObject,
    LPWSTR PropertyName,
    DWORD Value
    );

HRESULT
putPropBstr(
    IADs *pObject,
    LPWSTR PropertyName,
    BSTR bstrValue
    );

/* End Forward */


HRESULT
CheckSmtpDomainContainerPresent(
    void
    )

/*++

Routine Description:

Check whether the Smtp Domain Container can be opened

Arguments:

    void - 

Return Value:

    HRESULT - 

--*/

{
    HRESULT hr;
    IADs *pObject = NULL;
    IADsContainer * pADsContainer =  NULL;

    // Get the domain container
    hr = ADsGetObject(
        ADS_DOMAIN_PATH,
        IID_IADsContainer,
        (void **)&pADsContainer
        );
    if (FAILED(hr)) {
        goto cleanup;
    }

    // Just checking

cleanup:
    if (pADsContainer) {
        pADsContainer->Release();
    }

    return hr;
} /* CheckSmtpDomainContainerPresent */


HRESULT
removeSmtpDomainHelper(
    LPWSTR AdsPath,
    LPWSTR DomainName
    )

/*++

Routine Description:

    Remove the given smtp domain

Arguments:

    AdsPath - 
    DomainName - 

Return Value:

    HRESULT - 

--*/

{
    HRESULT hr;
    IADsContainer * pADsContainer =  NULL;
    IADs * pADsObject =  NULL;

    // Get the domain container
    hr = ADsGetObject(
        AdsPath,
        IID_IADsContainer,
        (void **)&pADsContainer
        );
    if (FAILED(hr)) {
        DPRINT2(0,"ADsGetObject(%ws) failed with error 0x%x\n",
                AdsPath, hr );
        LogEvent8(DS_EVENT_CAT_ISM,
                  DS_EVENT_SEV_ALWAYS,
                  DIRLOG_ISM_ADS_GET_OBJECT_FAILURE,
                  szInsertWC(AdsPath),
                  szInsertWin32Msg( hr ),
                  szInsertHResultCode( hr ),
                  NULL, NULL, NULL, NULL, NULL );
        goto cleanup;
    }

    hr = pADsContainer->Delete( L"IIsSmtpDomain", DomainName );
    if (FAILED(hr)) {
        DPRINT2(0,"ADsContainer->Delete(%ws) failed with error 0x%x\n",
                DomainName, hr );
        LogEvent8(DS_EVENT_CAT_ISM,
                  DS_EVENT_SEV_ALWAYS,
                  DIRLOG_ISM_ADS_DELETE_OBJECT_FAILURE,
                  szInsertWC(DomainName),
                  szInsertWin32Msg( hr ),
                  szInsertHResultCode( hr ),
                  NULL, NULL, NULL, NULL, NULL );
        goto cleanup;
    }

    hr = pADsContainer->QueryInterface(IID_IADs, (VOID **) &pADsObject) ;
    if (FAILED(hr)) {
        DPRINT1( 0, "QueryInterface failed, error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }

    // Commit the deletion to the store
    hr = pADsObject->SetInfo();
    if (FAILED(hr)) {
        DPRINT2(0,"ADsObject->SetInfo(%ws) failed with error 0x%x\n",
                DomainName, hr );
        LogEvent8(DS_EVENT_CAT_ISM,
                  DS_EVENT_SEV_ALWAYS,
                  DIRLOG_ISM_ADS_SET_INFO_FAILURE,
                  szInsertWC(DomainName),
                  szInsertWin32Msg( hr ),
                  szInsertHResultCode( hr ),
                  NULL, NULL, NULL, NULL, NULL );
        goto cleanup;
    }

cleanup:
    if (pADsContainer) {
        pADsContainer->Release();
    }
    if (pADsObject) {
        pADsObject->Release();
    }

    return hr;

} /* removeSmtpDomainHelp */


HRESULT
RemoveSmtpDomain(
    LPWSTR DomainName
    )

/*++

Routine Description:

    Remove the given smtp domain

Arguments:

    DomainName - 

Return Value:

    HRESULT - 

--*/

{
    return removeSmtpDomainHelper( ADS_DOMAIN_PATH, DomainName );
}


HRESULT
AddSmtpDomainIfNeeded(
    LPWSTR DomainName,
    BSTR bstrDropDirectory
    )

/*++

Routine Description:

Check for the given Smtp Domain in the Smtp Service metabase, and create one
if not present.

Note, this routine uses the first virtual Smtp Server.  In the future we may
need a way to specify the preferred virtual server

Arguments:

    DomainName - usually a dns name of this computer, the RHS of a mail addr

Return Value:

    HRESULT - 

--*/

{
    HRESULT hr;
    LPWSTR classList[] = { L"IIsSmtpDomain" };
    BOOL fDomainFound = FALSE;

    // Note: we search only the first Virtual Smtp Server

    // Enumerate the domain container, looking for IIsSmtpDomain objects

    hr = EnumObject( ADS_DOMAIN_PATH,
                     classList,
                     1,
                     domainCallback,
                     DomainName,
                     &fDomainFound
                     );
    if (FAILED(hr)) {
        DPRINT2( 0, "EnumObject(%ws) failed, error 0x%x\n",
                 ADS_DOMAIN_PATH, hr );
        // Error already logged
        goto cleanup;
    }

    // If it was found, we need to check that it is correct
    if (fDomainFound) {
        DPRINT1( 1, "SMTP Domain %ws is already present.\n", DomainName );
        hr = ModifySmtpDomainIfNeeded( ADS_DOMAIN_PATH, DomainName, bstrDropDirectory );
        if (FAILED(hr)) {
            DPRINT1( 0, "Failed to modify Smtp Domain, error 0x%x\n", hr );
            // Error already logged
        }
        goto cleanup;
    }

    // Add the new domain
    hr = addSmtpDomain( ADS_DOMAIN_PATH, DomainName, bstrDropDirectory );
    if (FAILED(hr)) {
        DPRINT1( 0, "Failed to add Smtp Domain, error 0x%x\n", hr );
        // Error already logged
        goto cleanup;
    }

    hr = S_OK;
cleanup:
   return hr;
} /* AddSmtpDomainIfNeeded */


HRESULT __cdecl
domainCallback(
    PVOID pObjectOpaque,
    PVOID Context1,
    PVOID Context2
    )

/*++

Routine Description:

This is a call back routine for the EnumObject function.
This routine is expected to be called back for SmtpDomain objects.  It
compares the passed in objects with the given domain name, and sets the
out parameter boolean true when they match

Arguments:

    pObjectOpaque - a pointer to a IADS * object for a smtp domain
    Context1 - lpwstr domain name
    Context2 - lpbool found, set true if a match occurs

Return Value:

    HRESULT __cdecl - 

--*/

{
    HRESULT hr;
    BSTR bstrName = NULL;
    IADs *pObject = (IADs *) pObjectOpaque;
    LPWSTR DomainName = (LPWSTR) Context1;
    LPBOOL pfDomainFound = (LPBOOL) Context2;

    // Get the object name
    hr = pObject->get_Name(&bstrName) ;
    if (FAILED(hr)) {
        DPRINT1(0,"get_Name failed with error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }

    // Does it match?
    if (_wcsicmp(DomainName,bstrName) == 0) {
        *pfDomainFound = TRUE;
        hr = S_FALSE; // terminate enumeration with success
    }

cleanup:

    if (bstrName) {
        SysFreeString(bstrName);
    }

    return hr;
} /* domainCallback */


HRESULT
addSmtpDomain(
    LPWSTR AdsPath,
    LPWSTR DomainName,
    BSTR bstrDropDirectory
    )

/*++

Routine Description:

Add a Smtp Domain to the given domain container.
The domain is added as type "1".
We default everything else for now.

Arguments:

    AdsPath - 
    DomainName - 
    bstrDropDirectory - 

Return Value:

    HRESULT - 

--*/

{
    HRESULT hr;
    IADsContainer * pADsContainer =  NULL;
    BSTR bstrClass = NULL, bstrName = NULL;
    IDispatch *pDispatch = NULL;
    IADs *pObject = NULL;

    // Get the domain container
    hr = ADsGetObject(
        AdsPath,
        IID_IADsContainer,
        (void **)&pADsContainer
        );
    if (FAILED(hr)) {
        DPRINT2(0,"ADsGetObject(%ws) failed with error 0x%x\n",
                AdsPath, hr );
        LogEvent8(DS_EVENT_CAT_ISM,
                  DS_EVENT_SEV_ALWAYS,
                  DIRLOG_ISM_ADS_GET_OBJECT_FAILURE,
                  szInsertWC(AdsPath),
                  szInsertWin32Msg( hr ),
                  szInsertHResultCode( hr ),
                  NULL, NULL, NULL, NULL, NULL );
        goto cleanup;
    }

    // build a class string
    bstrClass = SysAllocString( L"IIsSmtpDomain" );
    if (bstrClass == NULL) {
        hr = E_OUTOFMEMORY;
        DPRINT1(0,"SysAllocString failed with error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }

    // build a new object name string
    bstrName = SysAllocString( DomainName );
    if (bstrName == NULL) {
        hr = E_OUTOFMEMORY;
        DPRINT1(0,"SysAllocString failed with error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }

    // Invoke the create method on the container object to create the new
    // object of default class, in this case, IIsSmtpDomain.

    hr = pADsContainer->Create(
        bstrClass,
        bstrName,
        &pDispatch
        );
    if (FAILED(hr)) {
        DPRINT1(0,"ADsContainer::Crate failed with error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }
    
    // Get the newly created object
    hr = pDispatch->QueryInterface(
        IID_IADs,
        (VOID **) &pObject
        );
    if (FAILED(hr)) {
        DPRINT1(0,"QueryInterface failed with error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }

    // Write properties here if needed
    // 0x1 is the value Mike Swafford (mikeswa) said to put here.
    hr = putPropInteger( pObject, L"RouteAction", 1 );
    if (FAILED(hr)) {
        DPRINT1(0,"putPropInteger failed with error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }

    hr = putPropBstr( pObject, L"RouteActionString", bstrDropDirectory );
    if (FAILED(hr)) {
        DPRINT1(0,"putPropBstr failed with error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }

    // Write the object to persistent store
    hr = pObject->SetInfo();
    if (FAILED(hr)) {
        DPRINT1(0,"SetInfo failed with error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }

    DPRINT3( 1, "Added SMTP Domain %ws (drop dir %ws) to %ws\n",
             DomainName, bstrDropDirectory, AdsPath );
    LogEvent8(DS_EVENT_CAT_ISM,
              DS_EVENT_SEV_ALWAYS,
              DIRLOG_ISM_SMTP_DOMAIN_ADD,
              szInsertWC(AdsPath),
              szInsertWC(DomainName),
              szInsertWC(bstrDropDirectory),
              NULL, NULL, NULL, NULL, NULL);

cleanup:
    if (pADsContainer) {
        pADsContainer->Release();
    }
    if (pDispatch) {
        pDispatch->Release();
    }
    if (pObject) {
        pObject->Release();
    }

    if (bstrClass) {
        SysFreeString( bstrClass );
    }
    if (bstrName) {
        SysFreeString( bstrName );
    }

    return hr;
} /* addSmtpDomain */


HRESULT
ModifySmtpDomainIfNeeded(
    LPWSTR AdsContainerPath,
    LPWSTR DomainName,
    BSTR bstrDropDirectory
    )

/*++

Routine Description:

Make sure the drop directory is correct on the existing smtp domain

Arguments:

    AdsPath - 
    DomainName - 
    bstrDropDirectory - 

Return Value:

    HRESULT - 

--*/

{
    HRESULT hr;
    IADs *pObject = NULL;
    LPWSTR pwzDomainObjectPath = NULL;
    BSTR bstrOldDropDirectory = NULL;

    pwzDomainObjectPath = NEW_TYPE_ARRAY(
        (wcslen( AdsContainerPath ) + 1 + wcslen( DomainName ) + 1), WCHAR );
    if (pwzDomainObjectPath == NULL) {
        hr = E_OUTOFMEMORY;
        DPRINT1(0,"Can't allocate domain path, error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }
    wcscpy( pwzDomainObjectPath, AdsContainerPath );
    wcscat( pwzDomainObjectPath, L"/" );
    wcscat( pwzDomainObjectPath, DomainName );

    // Get the domain container
    hr = ADsGetObject(
        pwzDomainObjectPath,
        IID_IADs,
        (void **)&pObject
        );
    if (FAILED(hr)) {
        DPRINT2(0,"ADsGetObject(%ws) failed with error 0x%x\n",
                pwzDomainObjectPath, hr );
        LogEvent8(DS_EVENT_CAT_ISM,
                  DS_EVENT_SEV_ALWAYS,
                  DIRLOG_ISM_ADS_GET_OBJECT_FAILURE,
                  szInsertWC(pwzDomainObjectPath),
                  szInsertWin32Msg( hr ),
                  szInsertHResultCode( hr ),
                  NULL, NULL, NULL, NULL, NULL );
        goto cleanup;
    }

    hr = getPropBstr( pObject, L"RouteActionString", &bstrOldDropDirectory );
    if (FAILED(hr)) {
        DPRINT1(0,"getPropBstr failed with error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }

    // See if the value needs to be changed
    if (_wcsicmp( bstrOldDropDirectory, bstrDropDirectory ) == 0) {
        hr = S_OK;
        goto cleanup;
    }

    // Write properties here if needed
    // 0x1 is the value Mike Swafford (mikeswa) said to put here.
    hr = putPropInteger( pObject, L"RouteAction", 1 );
    if (FAILED(hr)) {
        DPRINT1(0,"putPropInteger failed with error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }

    hr = putPropBstr( pObject, L"RouteActionString", bstrDropDirectory );
    if (FAILED(hr)) {
        DPRINT1(0,"putPropBstr failed with error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }

    // Write the object to persistent store
    hr = pObject->SetInfo();
    if (FAILED(hr)) {
        DPRINT1(0,"SetInfo failed with error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }

    DPRINT2( 1, "Modified object %ws with drop dir %ws\n",
             pwzDomainObjectPath, bstrDropDirectory );
    LogEvent8(DS_EVENT_CAT_ISM,
              DS_EVENT_SEV_ALWAYS,
              DIRLOG_ISM_SMTP_DOMAIN_ADD,
              szInsertWC(AdsContainerPath),
              szInsertWC(DomainName),
              szInsertWC(bstrDropDirectory),
              NULL, NULL, NULL, NULL, NULL);

cleanup:
    if (pwzDomainObjectPath != NULL) {
        FREE_TYPE( pwzDomainObjectPath );
    }
    if (pObject) {
        pObject->Release();
    }
    if (bstrOldDropDirectory) {
        SysFreeString( bstrOldDropDirectory );
    }


    return hr;
} /* addSmtpDomain */


HRESULT
EnumObject(
    LPWSTR pszADsPath,
    LPWSTR * lppClassNames,
    DWORD dwClassNames,
    ENUM_CALLBACK_FN *pCallback,
    PVOID Context1,
    PVOID Context2
    )

/*++

Routine Description:

Enumerates the contents of a container object.
Calls a callback on each object found.
Filters objects returned if requested.

This routine is taken from
mssdk\samples\netds\adsi\cpp\adscmd\enum.cxx
Enhanced for error handling, manual filtering and a callback routine.

Arguments:

    pszADsPath - string path of container in ads namespace
    lppClassNames - array of strings of classes to request
    dwClassNames - number of strings in array
    pCallback - callback function to call on each object found
    Context1 - callback argument 1
    Context2 - callback argument 2

Return Value:

    HRESULT - 

--*/

{
#define MAX_ADS_ENUM      16     // number of entries to read each time
    ULONG cElementFetched = 0L;
    IEnumVARIANT * pEnumVariant = NULL;
    VARIANT VarFilter, VariantArray[MAX_ADS_ENUM];

    HRESULT hr;
    IADsContainer * pADsContainer =  NULL;
    DWORD i = 0;
    BOOL  fContinue = TRUE, fLoopContinue;
    BOOL fFilterPutSuccessful = FALSE;
    BOOL fManualFilterNeeded = FALSE;

   VariantInit(&VarFilter);

   // Get the container object
   hr = ADsGetObject(
       pszADsPath,
       IID_IADsContainer,
       (void **)&pADsContainer
       );
    if (FAILED(hr)) {
        DPRINT2( 0, "AdsGetObject(%ws) failed, error 0x%x\n",
                 pszADsPath, hr );
        LogEvent8(DS_EVENT_CAT_ISM,
                  DS_EVENT_SEV_ALWAYS,
                  DIRLOG_ISM_ADS_GET_OBJECT_FAILURE,
                  szInsertWC(pszADsPath),
                  szInsertWin32Msg( hr ),
                  szInsertHResultCode( hr ),
                  NULL, NULL, NULL, NULL, NULL )
        goto cleanup ;
    }

    // Build and supply the filter array
    // Not all classes support this, apparently
    if (dwClassNames) {
        hr = ADsBuildVarArrayStr(
            lppClassNames,
            dwClassNames,
            &VarFilter
            );
        if (FAILED(hr)) {
            DPRINT1( 0, "ADsBuildVarArrayStr failed, error 0x%x\n", hr );
            LogUnhandledError( hr );
            goto cleanup ;
        }

        hr = pADsContainer->put_Filter(VarFilter);
        if (hr != E_NOTIMPL) {
            if (FAILED(hr)) {
                DPRINT1( 0, "put_Filter failed, error 0x%x\n", hr );
                LogUnhandledError( hr );
                goto cleanup ;
            }
            fFilterPutSuccessful = TRUE;
        } else {
            fManualFilterNeeded = TRUE;
        }
    }

    // Build an enumerator for the container
    hr = ADsBuildEnumerator(
            pADsContainer,
            &pEnumVariant
            );
    if (FAILED(hr)) {
        DPRINT1( 0, "ADsBuildEnumerator failed, error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup ;
    }

    // Loop while more to do
    // Note the structure of the loop: we always exit through the top.
    // The loop_cleanup section must be executed to free resources.

    while (fContinue) {

        // Zero the array of variants
        memset(VariantArray, 0, sizeof(VARIANT)*MAX_ADS_ENUM);

        // Get the next batch. Note that cElemFetched may be >0 even though
        // the routine returns S_FALSE.

        hr = ADsEnumerateNext(
                    pEnumVariant,
                    MAX_ADS_ENUM,
                    VariantArray,
                    &cElementFetched
                    );

        if (hr == S_FALSE) {
            fContinue = FALSE;
            // keep going, but this is the last time
        } else if (FAILED(hr)) {
            DPRINT1( 0, "ADsEnumerateNext failed, error 0x%x\n", hr );
            LogUnhandledError( hr );
            goto cleanup ;
        }

        // Loop through the elements found
        fLoopContinue = TRUE;
        for (i = 0; ((i < cElementFetched) && (fLoopContinue)); i++ ) {

            IDispatch *pDispatch = NULL;
            IADs *pObject = NULL ;
            BSTR bstrClass = NULL;

            // Get the pointer to the object returned

            pDispatch = VariantArray[i].pdispVal;

            hr = pDispatch->QueryInterface(IID_IADs,
                                           (VOID **) &pObject) ;
            if (FAILED(hr)) {
                DPRINT1( 0, "QueryInterface failed, error 0x%x\n", hr );
                LogUnhandledError( hr );
                fLoopContinue = FALSE;
                goto loop_cleanup ;
            }

            // Get its class name

            hr = pObject->get_Class(&bstrClass);
            if (FAILED(hr)) {
                DPRINT1( 0, "get_Class failed, error 0x%x\n", hr );
                LogUnhandledError( hr );
                fLoopContinue = FALSE;
                goto loop_cleanup ;
            }

            // If manual filtering, check class against list
            if (fManualFilterNeeded) {
                DWORD j;
                BOOL found;
                for( j = 0, found=FALSE; (j < dwClassNames) && (!found); j++ ) {
                    found = (wcscmp( bstrClass, lppClassNames[j] ) == 0);
                }
                if (!found) {
                    goto loop_cleanup;
                }
            }

            // Call the callback on this object
            hr = (*pCallback)( pObject, Context1, Context2 );
            if (FAILED(hr)) {
                DPRINT1( 0, "enumObject callback failed, error 0x%x\n", hr );
                fLoopContinue = FALSE;
            } else if (hr == S_FALSE) {
                fLoopContinue = FALSE;
                // Enumeration terminated prematurely with success
            }

        loop_cleanup:
            if (bstrClass) {
                SysFreeString(bstrClass);
            }

            if (pObject) {
                pObject->Release();
            }
            pDispatch->Release();

        } // For loop through elements found

        // Cleanup up premature loop termination
        if (!fLoopContinue) {
            for (; (i < cElementFetched); i++ ) {
                IDispatch *pDispatch = NULL;

                pDispatch = VariantArray[i].pdispVal;
                pDispatch->Release();
            }
            // Abort containing loop
            fContinue = FALSE;
        }

    } // while (fContinue)

    // hr set above and passed through

cleanup:

    if (pEnumVariant) {
        pEnumVariant->Release();
    }

    if (!fFilterPutSuccessful) {
        VariantClear(&VarFilter);
    }
    // else will be cleared as part of container release

    if (pADsContainer) {
        pADsContainer->Release();
    }

    return(hr);
} /* EnumObject */


HRESULT
getSmtpServerProperties(
    LPWSTR AdsPath,
    BSTR *pbstrDropDirectory
    )

/*++

Routine Description:

Get properties from the given smtp server

Arguments:

    AdsPath - 
    pbstrDropDirectory - pointer to bstr to get drop directory, caller frees

Return Value:

    HRESULT - 

--*/

{
    HRESULT hr;
    IADs *pObject = NULL ;

    // Get smtp server object
    hr = ADsGetObject(
        AdsPath,
        IID_IADs,
        (void **)&pObject
        );
    if (FAILED(hr)) {
        DPRINT2(0,"ADsGetObject(%ws) failed with error 0x%x\n",
                AdsPath, hr );
        LogEvent8(DS_EVENT_CAT_ISM,
                  DS_EVENT_SEV_ALWAYS,
                  DIRLOG_ISM_ADS_GET_OBJECT_FAILURE,
                  szInsertWC(AdsPath),
                  szInsertWin32Msg( hr ),
                  szInsertHResultCode( hr ),
                  NULL, NULL, NULL, NULL, NULL );
        goto cleanup;
    }

    hr = getPropBstr( pObject, L"DropDirectory", pbstrDropDirectory );
    if (FAILED(hr)) {
        DPRINT1(0,"getPropBstr failed with error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }

    // Retrieve other properties here as needed

    hr = S_OK;

cleanup:

    if (pObject) {
        pObject->Release();
    }

    return hr;
} /* getSmtpServerProperties */


HRESULT
getPropBstr(
    IADs *pObject,
    LPWSTR PropertyName,
    BSTR *pbstrValue
    )

/*++

Routine Description:

Get a bstr-valued property from an object. Caller must free.

Arguments:

    pObject - 
    PropertyName - 
    pbstrValue - pointer to allocated bstr; caller must sysfree()

Return Value:

    HRESULT - 

--*/

{
    HRESULT hr;
    BSTR bstrProp = NULL;
    VARIANT varValue;

    // Allocate the property
    bstrProp = SysAllocString( PropertyName );
    if (bstrProp == NULL) {
        hr = E_OUTOFMEMORY;
        DPRINT1(0,"SysAllocString failed with error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }

    // Get the property
    hr = pObject->Get( bstrProp, &varValue );
    if (FAILED(hr)) {
        DPRINT1(0,"Get failed with error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }

    // We are expecting a bstr

    if (varValue.vt != VT_BSTR) {
        hr = E_INVALIDARG;
        DPRINT1( 0, "Variant has unexpected type %d\n", varValue.vt );
        LogUnhandledError( hr );
        goto cleanup;
    }

// Steal the bstr out of the variant, and don't free the variant
    *pbstrValue = varValue.bstrVal;

    hr = S_OK;

cleanup:
    if (bstrProp) {
        SysFreeString(bstrProp);
    }

    return hr;
} /* getPropBstr */


HRESULT
putPropInteger(
    IADs *pObject,
    LPWSTR PropertyName,
    DWORD Value
    )

/*++

Routine Description:

This is a helper routine to create an integer attribute on an object.
The attribute to be set is specified at run-time.

Note, it is assumed caller will call pObject->SetInfo to flush the changes

Arguments:

    pObject - iads object pointer
    PropertyName - string name of property to be written
    Value - integer

Return Value:

    HRESULT - 

--*/

{
    HRESULT hr;
    BSTR bstrProp = NULL;
    VARIANT varValue;

    VariantInit( &varValue );

    bstrProp = SysAllocString( PropertyName );
    if (bstrProp == NULL) {
        hr = E_OUTOFMEMORY;
        DPRINT1(0,"SysAllocString failed with error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }

    varValue.vt = VT_I4;
    varValue.lVal = Value;

    hr = pObject->Put( bstrProp, varValue );
    if (FAILED(hr)) {
        DPRINT1(0, "Put failed with error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }

cleanup:
    if (bstrProp) {
        SysFreeString(bstrProp);
    }

    // No need to free variant

    return hr;

} /* putPropInteger */


HRESULT
putPropBstr(
    IADs *pObject,
    LPWSTR PropertyName,
    BSTR bstrValue
    )

/*++

Routine Description:

This is a helper routine to create an bstr attribute on an object.
The attribute to be set is specified at run-time.

Note, it is assumed caller will call pObject->SetInfo to flush the changes

Arguments:

    pObject - iads object pointer
    PropertyName - string name of property to be written
    Value - bstr

Return Value:

    HRESULT - 

--*/

{
    HRESULT hr;
    BSTR bstrProp = NULL;
    VARIANT varValue;

    VariantInit( &varValue );

    bstrProp = SysAllocString( PropertyName );
    if (bstrProp == NULL) {
        hr = E_OUTOFMEMORY;
        DPRINT1(0,"SysAllocString failed with error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }

    varValue.vt = VT_BSTR;
    varValue.bstrVal = bstrValue;

    hr = pObject->Put( bstrProp, varValue );
    if (FAILED(hr)) {
        DPRINT1(0, "Put failed with error 0x%x\n", hr );
        LogUnhandledError( hr );
        goto cleanup;
    }

cleanup:
    if (bstrProp) {
        SysFreeString(bstrProp);
    }

    // Caller will free bstr
    // No need to free variant

    return hr;

} /* putPropBstr */

/* end adsisupp.cxx */