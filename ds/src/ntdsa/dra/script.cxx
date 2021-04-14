//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//
//  Copyright (C) Microsoft Corporation, 1998 - 2000
//
//  File:       script.cxx
//
//--------------------------------------------------------------------------

/*++

Abstract:

Server-side RPC entrypoints for the ExecuteScript function

Author:

    MariosZ

Environment:

Notes:

Revision History:

--*/

#include <NTDSpchx.h>
#pragma hdrstop


// Core DSA headers.
extern "C" {
#include <ntdsa.h>
#include <drs.h>
#include <scache.h>                     // schema cache
#include <attids.h>
#include <dbglobal.h>                   // The header for the directory database
#include <mdglobal.h>                   // MD global definition header
#include <mdlocal.h>                    // MD local definition header
#undef new
#undef delete

#include <objids.h>
#include <anchor.h>
#include <dsatools.h>                   // needed for output allocation
#include <Wincrypt.h>                   // encryption / hashing routines
#include <filtypes.h>
#include <dominfo.h>

#define SECURITY_WIN32
#include <sspi.h>
#include <ldap.h>
#include <windns.h>
#include <sddl.h>

#include <lmserver.h>   // needed for lmjoin.h
#include <lmjoin.h>     // needed for netsetup.h
#include <netsetup.h>   // needed for NetpSetDnsComputerNameAsRequired

// Logging headers.
#include "dsevent.h"                    /* header Audit\Alert logging */
#include "mdcodes.h"                    /* header for error codes */

#include "log.h"

// Assorted DSA headers.
#include "dsexcept.h"
#include "permit.h"

#include <base64.h>

#include   "debug.h"                    /* standard debugging header */
#define DEBSUB     "SCRIPT:"           /* define the subsystem for debugging */

#include <fileno.h>
#define  FILENO FILENO_NTDSCRIPT

extern ATTCACHE *getAttByNameW(IN THSTATE *pTHS, IN LPWSTR pszAttributeName);
extern int LocalCompare(THSTATE *pTHS, COMPAREARG *pCompareArg, COMPARERES *pCompareRes);


}

#include <NTDScript.h>
#include <NTDScriptUtil.h>
#include <NTDSContent.h>
#include <NTDScriptExec.h>

#include <SAXErrorHandlerImpl.h>


#define INTRUDER_DELAY (10 * 1000)

// guid used to sign the script stored in the database.
// this is used in addition to the user specified Key.
// {0916C8E3-3431-4586-AF77-44BD3B16F961}
static const GUID guidDomainRename = 
{ 0x916c8e3, 0x3431, 0x4586, { 0xaf, 0x77, 0x44, 0xbd, 0x3b, 0x16, 0xf9, 0x61 } };


extern "C" {
LONG  glRunningExecuteScriptOperations = 0;
ULONG gulScriptLoggerLogLevel = 1;
}
//
//  ScriptParseErrorGen
//
//  Description:
//
//      Generates an error related to parsing / executing of the NTDSA script
//
//  Arguments:
//
//      dsid - the DSID of the error
//      dwErr - the Error Code
//      data  - additional data needed for the error
//
//  Return Value:
//      the Error code
//      
DWORD ScriptErrorGen (PCHAR szError, DWORD dsid, DWORD dwErr, DWORD data, WCHAR *pMsg)
{
    THSTATE *pTHS = pTHStls;

    Assert (pTHS);

    ScriptLogPrint ( (DSLOG_ERROR, "%S(0x%x) DSID (0x%x) Data(0x%x)\r\n", szError, dwErr, dsid, data) );

    // save dsid, error in thstate

    LIST_OF_ERRORS *pErrList = (LIST_OF_ERRORS *)THAllocNoEx (pTHS, sizeof (LIST_OF_ERRORS));

    if (pErrList) {
        pErrList->data = data;
        pErrList->dsid = dsid;
        pErrList->dwErr = dwErr;
        pErrList->pMessage = pMsg;
        pErrList->pPrevError = pTHS->pErrList;

        pTHS->pErrList = pErrList;
    }

    if (dwErr) {
        return dwErr;
    }

    return ERROR_DS_NTDSCRIPT_PROCESS_ERROR;
}

DWORD ScriptProcessErrorFromTHSTATEError() {
    THSTATE* pTHS = pTHStls;
    LPSTR szErrStr;
    
    // Use a handy function to create an error string from THSTATE error info
    szErrStr = THGetErrorString();

    if (szErrStr) {
        ScriptLogPrint ( (DSLOG_ERROR, "Script process error(0x%x) %S\r\n", pTHS->errCode, szErrStr) );
        THFreeEx(pTHS, szErrStr);
    }
    else {
        // something wrong -- we could not create an error string, just log the errCode
        ScriptLogPrint ( (DSLOG_ERROR, "Script process error(0x%x)\r\n", pTHS->errCode) );
    }
    return pTHS->errCode;
}

//
//  ScriptAlloc
//
//  Description:
//
//      Allocated memory used for script parsing. This functions isolates the 
//      memory allocation needs of the script parsing code from the DS
//
//  Arguments:
//
//      size  - memory size needed
//
//  Return Value:
//      the allocated memory buffer on success.
//      NULL on failure
//
void *ScriptAlloc (size_t size)
{
    void * pMem;

    pMem = THAlloc(size);

    return(pMem);
}

//
//  ScriptFree
//
//  Description:
//
//      Free memory used for script parsing. This functions isolates the 
//      memory allocation needs of the script parsing code from the DS
//
//  Arguments:
//
//      ptr - the memory to free
//
//  Return Value:
//     None
//
void ScriptFree (void *ptr)
{
    THSTATE *pTHS = pTHStls;

    Assert (pTHS);

    THFreeEx(pTHS, ptr);
}

//
//  ScriptStringToDSFilter
//
//  Description:
//
//      Converts a WCHAR string that contains an LDAP filter into 
//      a DS FILTER datastructure.
//
//  Arguments:
//
//      search_filter - the LDAP like filter to convert
//      ppFilter - the output filter
//
//  Return Value:
//     0 on success
//     1 on failure
//
DWORD ScriptStringToDSFilter (WCHAR *search_filter, FILTER **ppFilter)
{
    THSTATE *pTHS = pTHStls;
    DWORD cLen = wcslen (search_filter);
    FILTER *pFilter = NULL;
    CLASSCACHE *pCC = NULL;
    
    *ppFilter = NULL;

    if (wcsncmp (L"COUNT_DOMAINS_FILTER", search_filter, cLen) == 0) {

        pFilter = (FILTER *)THAllocEx (pTHS, sizeof (FILTER));

        pFilter->choice = FILTER_CHOICE_ITEM;
        pFilter->FilterTypes.Item.choice = FI_CHOICE_BIT_AND;
        pFilter->FilterTypes.Item.FilTypes.ava.type = ATT_SYSTEM_FLAGS;
        pFilter->FilterTypes.Item.FilTypes.ava.Value.valLen = sizeof (DWORD);
        pFilter->FilterTypes.Item.FilTypes.ava.Value.pVal = (UCHAR *)THAllocEx (pTHS, sizeof (DWORD));
        *(DWORD *)(pFilter->FilterTypes.Item.FilTypes.ava.Value.pVal) = FLAG_CR_NTDS_DOMAIN;

        *ppFilter = pFilter;
    }
    else if (wcsncmp (L"COUNT_TRUSTS_FILTER", search_filter, cLen) == 0) {

        pCC = SCGetClassById(pTHS, CLASS_TRUSTED_DOMAIN);
        if (!pCC) {
            Assert (FALSE);
            return 1;
        }

        pFilter = (FILTER *)THAllocEx (pTHS, sizeof (FILTER));

        pFilter->choice = FILTER_CHOICE_ITEM;
        pFilter->FilterTypes.Item.choice = FI_CHOICE_EQUALITY;
        pFilter->FilterTypes.Item.FilTypes.ava.type = ATT_OBJECT_CATEGORY;
        pFilter->FilterTypes.Item.FilTypes.ava.Value.valLen = pCC->pDefaultObjCategory->structLen;
        pFilter->FilterTypes.Item.FilTypes.ava.Value.pVal = (UCHAR *) pCC->pDefaultObjCategory;

        *ppFilter = pFilter;
    }
    else {
        ScriptLogPrint ( (DSLOG_ERROR, "Cannot parse filter: %ws\r\n", search_filter) );

        Assert (!"Not Supported Filter");
        return 1;
    }

    return 0;
}

#define SCRIPT_VALUE_TRUE  L"TRUE"
#define SCRIPT_VALUE_TRUE_LEN ( sizeof(SCRIPT_VALUE_TRUE)/sizeof(WCHAR) -1 )
#define SCRIPT_VALUE_FALSE  L"FALSE"
#define SCRIPT_VALUE_FALSE_LEN ( sizeof(SCRIPT_VALUE_FALSE)/sizeof(WCHAR) -1 )

//
//  ConvertScriptValueToDirAttrVal
//
//  Description:
//
//     Convert a string to the corresponding AttrVal structure
//     This function is similar to a corresponding function in the LDAP head
//
//  Arguments:
//
//      pAC - the attribute used
//      pVal - the string value
//      pAttrVal - where to store the resulted AttrVal
//
//  Return Value:
//
//     0 on success
//     Win32 error on failure
// 
//
DWORD ConvertScriptValueToDirAttrVal (THSTATE *pTHS, 
                                      ATTCACHE *pAC, 
                                      WCHAR    *pVal, 
                                      ATTRVAL  *pAttrVal)
{
    DWORD cLen = 0;
    DWORD dwErr = 0;


    if (!pVal) {
        pAttrVal->valLen = 0;
        pAttrVal->pVal = NULL;
        return 0;
    }
    else {
        cLen = wcslen (pVal);
        pAttrVal->valLen = cLen * sizeof (WCHAR);
        pAttrVal->pVal = (PUCHAR)pVal;
    }

    // Based on the att, turn the string we were given into a value.
    switch (pAC->OMsyntax) {
    case OM_S_BOOLEAN:
        {
            int val=0;
            // Only two values are allowed.  Anything else is not-understood.
            // Case matters.
            if((cLen == SCRIPT_VALUE_TRUE_LEN) &&
               (wcsncmp(SCRIPT_VALUE_TRUE, pVal, cLen)== 0)) {
                    val = 1;
            }
            else if((cLen == SCRIPT_VALUE_FALSE_LEN) &&
               (wcsncmp(SCRIPT_VALUE_FALSE, pVal, cLen)== 0)) {
                    val = 2;
            }

            if(!val) {   
                dwErr = ERROR_DS_INVALID_ATTRIBUTE_SYNTAX;
            }
            else {
                pAttrVal->valLen = sizeof( BOOL );
                pAttrVal->pVal = ( UCHAR * ) THAllocEx(pTHS, sizeof(BOOL));
                *( BOOL * )pAttrVal->pVal = (val==1);
            }
        }

        break;

    case OM_S_ENUMERATION:
    case OM_S_INTEGER:
        {
            SYNTAX_INTEGER *pInt, sign=1;
            ATTCACHE *pACLink;
            unsigned i;

            pInt = ( SYNTAX_INTEGER  *) THAllocEx(pTHS, sizeof(SYNTAX_INTEGER));
            *pInt = 0;
            i=0;
            if(pVal[i] == L'-') {
                sign = -1;
                i++;
            }
            if(i==cLen) {
                // No length or just a '-'
                return ERROR_DS_INVALID_ATTRIBUTE_SYNTAX;
            } else {
                for(;i<cLen;i++) {
                    // Parse the string one character at a time to detect any
                    // non-allowed characters.
                    if((pVal[i] < L'0') || (pVal[i] > L'9')) {
                        dwErr = ERROR_DS_INVALID_ATTRIBUTE_SYNTAX;
                        break;
                    }
                    *pInt = (*pInt * 10) + pVal[i] - L'0';
                }
            }
                
            if (dwErr == 0) {
                *pInt *= sign;
            } else if (pAC->id != ATT_LINK_ID) {
                return dwErr;
            } else {
                // AutoLinkId
                Assert (FALSE);
                return ERROR_DS_INVALID_ATTRIBUTE_SYNTAX;
            }

            // Ok, got the value, set it up.
            pAttrVal->valLen = sizeof( SYNTAX_INTEGER );
            pAttrVal->pVal = ( UCHAR * ) pInt;
        }
        break;

    case OM_S_OBJECT:
        switch(pAC->syntax) {
        case SYNTAX_DISTNAME_TYPE:
            
            if ( !(dwErr = ScriptNameToDSName (pVal, cLen, (DSNAME **) &pAttrVal->pVal)) ) {
                pAttrVal->valLen=((DSNAME*)pAttrVal->pVal)->structLen;
            }
            break;


        case SYNTAX_DISTNAME_STRING_TYPE:
        case SYNTAX_DISTNAME_BINARY_TYPE:
        case SYNTAX_ADDRESS_TYPE:
        case SYNTAX_OCTET_STRING_TYPE:
            DPRINT1 (0, "Not Implemented Syntax Conversion: %d\n", pAC->syntax);
            Assert (!"Not Implemented Syntax Conversion");
            
            // fall through
        default:
            dwErr = ERROR_DS_INVALID_ATTRIBUTE_SYNTAX;
            break;
        }
        break;


    case OM_S_IA5_STRING:
    case OM_S_NUMERIC_STRING:
    case OM_S_TELETEX_STRING:
    case OM_S_PRINTABLE_STRING:
        {
            AssertionValue assertionVal;
            extern _enum1 CheckStringSyntax(int syntax, AssertionValue *pLDAP_val);

            // Convert Unicode to Ascii
            assertionVal.value = (UCHAR *)String8FromUnicodeString(TRUE, 
                                                          CP_UTF8, 
                                                          pVal, 
                                                          -1, 
                                                          (LPLONG)&assertionVal.length, 
                                                          NULL);

            

            if(!pAC->bExtendedChars) {
                if (CheckStringSyntax(pAC->OMsyntax, &assertionVal)) {
                    THFreeEx (pTHS, assertionVal.value);
                    dwErr = ERROR_DS_INVALID_ATTRIBUTE_SYNTAX;
                    return dwErr;
                }
            }
            pAttrVal->valLen = assertionVal.length;
            pAttrVal->pVal = assertionVal.value;
        }
        break;

    case OM_S_OCTET_STRING:

        if (SYNTAX_SID_TYPE == pAC->syntax) {
            PSID   pSid;
            
            //
            // Check to see if this is the userfriendly string representation.
            //
            if (cLen >= 2 && !_wcsnicmp(pVal, L"S-", 2)
                && ConvertStringSidToSidW(pVal, &pSid)) {

                __try {
                    // Now copy the converted SID into THAlloc'ed memory
                    pAttrVal->valLen = RtlLengthSid(pSid);
                    pAttrVal->pVal = (PUCHAR)THAllocEx(pTHS, pAttrVal->valLen);
                    CopyMemory(pAttrVal->pVal, pSid, pAttrVal->valLen);
                }
                __finally {
                    LocalFree(pSid);
                }

            }
            break;
        }
        //
        // deliberate fall through if this is not a SID.
        //

    case OM_S_GENERAL_STRING:
    case OM_S_GRAPHIC_STRING:
    case OM_S_OBJECT_DESCRIPTOR_STRING:
    case OM_S_VIDEOTEX_STRING:

        // Strings is strings, just use them.
        // Convert Unicode to Ascii

        pAttrVal->pVal = (UCHAR *)String8FromUnicodeString(TRUE, 
                                                  CP_UTF8, 
                                                  pVal, 
                                                  -1, 
                                                  (LPLONG)&pAttrVal->valLen, 
                                                  NULL);
        break;

    case OM_S_UNICODE_STRING:
        pAttrVal->valLen = cLen * sizeof(WCHAR);
        pAttrVal->pVal = (UCHAR *)THAllocEx(pTHS, pAttrVal->valLen + sizeof(WCHAR));
        memcpy (pAttrVal->pVal, pVal, pAttrVal->valLen);
        break;

    case OM_S_I8:
        {
            SYNTAX_I8 *pInt;
            LONG sign=1;
            unsigned i;

            pInt = ( SYNTAX_I8  *) THAllocEx(pTHS, sizeof(SYNTAX_I8));
            pInt->QuadPart = 0;
            i=0;
            if(pVal[i] == L'-') {
                sign = -1;
                i++;
            }

            if(i==cLen) {
                // No length or just a '-'
                return ERROR_DS_INVALID_ATTRIBUTE_SYNTAX;
            }

            for(;i<cLen;i++) {
                // Parse the string one character at a time to detect any
                // non-allowed characters.
                if((pVal[i] < L'0') || (pVal[i] > L'9')) {
                    return ERROR_DS_INVALID_ATTRIBUTE_SYNTAX;
                }

                pInt->QuadPart = ((pInt->QuadPart * 10) + pVal[i] - '0');
            }
            pInt->QuadPart *= sign;

            // Ok, got the value, set it up.
            pAttrVal->valLen = sizeof( SYNTAX_I8 );
            pAttrVal->pVal = (UCHAR *)pInt;
        }
        break;


    case OM_S_OBJECT_IDENTIFIER_STRING:
        {
    extern _enum1
    LDAP_AttrTypeToDirAttrTyp (
            IN THSTATE       *pTHS,
            IN ULONG         CodePage,
            IN SVCCNTL*      Svccntl OPTIONAL,
            IN AttributeType *LDAP_att,
            OUT ATTRTYP      *pAttrType,
            OUT ATTCACHE     **ppAC         // OPTIONAL
            );

    extern _enum1
    LDAP_AttrTypeToDirClassTyp (
            IN  THSTATE       *pTHS,
            IN  ULONG         CodePage,
            IN  AttributeType *LDAP_att,
            OUT ATTRTYP       *pAttrType,
            OUT CLASSCACHE    **ppCC        // OPTIONAL
            );


            _enum1 code=success;
            SVCCNTL Svccntl;
            ULONG   CodePage = CP_UTF8;
            ATTCACHE *pACVal;
            AssertionValue assertionVal;

            // allocate space for the oid
            pAttrVal->valLen = sizeof( ULONG );
            pAttrVal->pVal = ( UCHAR * ) THAllocEx(pTHS, sizeof (ULONG));

            // Convert Unicode to Ascii
            assertionVal.value = (UCHAR *)String8FromUnicodeString(TRUE, 
                                                          CP_UTF8, 
                                                          pVal, 
                                                          -1, 
                                                          (LPLONG)&assertionVal.length, 
                                                          NULL);

            // not interested in the trailing NULL.
            if (assertionVal.length) {
                assertionVal.length--;
            }

            // Call support routine to translate.
            code = LDAP_AttrTypeToDirAttrTyp (
                    pTHS,
                    CodePage,
                    &Svccntl,
                    (AttributeType *)&assertionVal,
                    (ATTRTYP *)pAttrVal->pVal,
                    &pACVal);

            // Need the tokenized OID (attributeId), not the internal id (msDS-IntId)
            if (code == success) {
                *((ATTRTYP *)pAttrVal->pVal) = pACVal->Extid;
            }

            if(code == noSuchAttribute) {
                // Ok, it's not an attribute, see if it is a class.
                code = LDAP_AttrTypeToDirClassTyp (
                        pTHS,
                        CodePage,
                        (AttributeType *)&assertionVal,
                        (ATTRTYP *)pAttrVal->pVal,
                        NULL);
            }

            if(code == noSuchAttribute) {
                // Not an object we know. Could be a new id.
                // Try to parse the string as an OID string,
                // (e.g.  "OID.1.2.814.500" or "1.2.814.500")
                // The call to StringToAttrType can handle
                // both strings starting with OID. and not

                if(StringToAttrTyp(pTHS, pVal, cLen, (ATTRTYP *)pAttrVal->pVal)== -1) {
                      // failed to convert.
                     code = noSuchAttribute;
                 }
                 else {
                     code = success;
                 }
            }

            if (assertionVal.value) {
                THFreeEx (pTHS, assertionVal.value);
            }

            if (code != success) {
                dwErr = ERROR_DS_INVALID_ATTRIBUTE_SYNTAX;
            }
        }
        break;


    case OM_S_GENERALISED_TIME_STRING:
    case OM_S_UTC_TIME_STRING:
    case OM_S_OBJECT_SECURITY_DESCRIPTOR:
        
        ScriptLogPrint ( (DSLOG_ERROR, "Not Implemented OMSyntax Conversion: %d\r\n", pAC->OMsyntax) );        
        DPRINT1 (0, "Not Implemented OMSyntax Conversion: %d\n", pAC->OMsyntax);
        Assert (!"Not Implemented Syntax Conversion");

        // fall through

    case OM_S_NULL:
    case OM_S_ENCODING_STRING:
    default:
        // huh?
        dwErr = ERROR_DS_INVALID_ATTRIBUTE_SYNTAX;
    }

    return dwErr;
}


//
//  ConvertScriptElementToDirAttr
//
//  Description:
//
//     Convert a ScriptAttribute to an ATTR
//
//  Arguments:
//
//      pElement - the ScriptAttribute to convert
//      pAttr - where to store the resulted Attr
//
//  Return Value:
//
//     0 on success
//     Win32 error on failure
// 
//
DWORD ConvertScriptElementToDirAttr (THSTATE *pTHS, 
                                     ScriptAttribute *pElement,  
                                     ATTR *pAttr)
{
    DWORD            dwErr=0;
    ATTCACHE        *pAC;

    do {
        
        pAC = getAttByNameW( pTHS, pElement->m_name);
        if (!pAC) {
            dwErr = ScriptProcessError(ERROR_DS_ATT_NOT_DEF_IN_SCHEMA);
            break;
        }

        pAttr->attrTyp = pAC->id;
        pAttr->AttrVal.valCount = 1;
        pAttr->AttrVal.pAVal = (ATTRVAL *)THAllocEx(pTHS, sizeof(ATTRVAL));

        dwErr = ConvertScriptValueToDirAttrVal (pTHS, pAC, pElement->m_characters, &pAttr->AttrVal.pAVal[0]);

    } while ( 0 );

    return dwErr;
}



//
//  ScriptInstantiatedRequest
//
//  Description:
//
//     Implement the Instantiated request
//
//  Arguments:
//
//      pObjectDN - the object to be checked for instantiated
//      pfisInstantiated - where to store the result
//
//  Return Value:
//
//     0 on success
//     error on failure
// 
//
DWORD ScriptInstantiatedRequest (
    DSNAME *pObjectDN, 
    ScriptPredicate::ScriptInstanciatedTestType testType, 
    BOOL *pfMatch)
{
    THSTATE     *pTHS = pTHStls;
    DWORD        dwErr=0;
    ULONG        it;
    PWCHAR       szOper;

    switch(testType) {
    case ScriptPredicate::SCRIPT_INSTANCIATED_TEST_TYPE_READ:
        szOper = L"READ";
        break;
    case ScriptPredicate::SCRIPT_INSTANCIATED_TEST_TYPE_WRITE:
        szOper = L"WRITE";
        break;
    case ScriptPredicate::SCRIPT_INSTANCIATED_TEST_TYPE_READONLY:
        szOper = L"READONLY";
        break;
    default:
        Assert(!"Unknown testType");
        szOper = L"???";
    }

    DPRINT2 (0, "Instantiated (%ws) on Object: %ws\n", szOper, pObjectDN->StringName);

    *pfMatch = FALSE;

    do {
        switch (dwErr = DBFindDSName(pTHS->pDB, pObjectDN)){

        case 0:
            // Get the instance type
            dwErr = DBGetSingleValue(pTHS->pDB,
                                   ATT_INSTANCE_TYPE,
                                   &it,
                                   sizeof(it),
                                   NULL);

            switch(dwErr) {
            case DB_ERR_NO_VALUE:
                // No instance type is an uninstantiated object
                it = IT_UNINSTANT;
                dwErr = 0;
                break;

            case 0:
                // No action.
                break;

            case DB_ERR_VALUE_TRUNCATED:
            default:
                // Something unexpected and bad happened.  Bail out.
                LogUnhandledErrorAnonymous(dwErr);
                dwErr = ScriptProcessError(dwErr);
            }

            if (dwErr) {
                break;
            }

            if (it & IT_UNINSTANT) {
                // object is not instanciated
                break;
            }

            switch(testType) {
            case ScriptPredicate::SCRIPT_INSTANCIATED_TEST_TYPE_WRITE:
                *pfMatch = (it & IT_WRITE);
                break;

            case ScriptPredicate::SCRIPT_INSTANCIATED_TEST_TYPE_READ:
                *pfMatch = TRUE;
                break;

            case ScriptPredicate::SCRIPT_INSTANCIATED_TEST_TYPE_READONLY:
                *pfMatch = !(it & IT_WRITE);
                break;
            }

            break;

        case DIRERR_OBJ_NOT_FOUND:
        case DIRERR_NOT_AN_OBJECT:

            // object is not instantiated
            dwErr = 0;
            break;

        default:
            dwErr = ScriptProcessError (dwErr);

        }  /*switch*/

    } while ( 0 );

    if (!fNullUuid(&pObjectDN->Guid)) {
        ScriptLogGuid ( DSLOG_TRACE, "InstanceType Check. ObjectGUID:", &pObjectDN->Guid );
    }
    ScriptLogPrint ( (DSLOG_TRACE, "InstanceType Check %ws (%ws)=%ws  Result: %d(0x%x)\r\n",
                                szOper,
                                pObjectDN->NameLen ? pObjectDN->StringName : L"NULL",
                                *pfMatch ? L"TRUE" : L"FALSE", 
                                dwErr, dwErr) );        

    LogEvent8(DS_EVENT_CAT_INTERNAL_PROCESSING,
              DS_EVENT_SEV_MINIMAL,
              DIRLOG_SCRIPT_OPERATION_INSTANTIATED,
              szInsertWC(szOper),
              szInsertDN(pObjectDN),
              szInsertHex(*pfMatch),
              szInsertWin32ErrCode(dwErr), 
              szInsertWin32Msg(dwErr), 
              NULL, NULL, NULL );

    return dwErr;
}


//
//  ScriptCardinalityRequest
//
//  Description:
//
//     Implement the Cardinality request
//
//  Arguments:
//
//      pObjectDN - the object to base the search request for cardinality
//      searchType - base, onelevel, subtree
//      pFilter - the filter to use
//      dwCardinality (OUT) - the number of objects found
//
//  Return Value:
//
//     0 on success
//     error on failure
// 
//
DWORD ScriptCardinalityRequest (DSNAME *pObjectDN, DWORD searchType, FILTER *pFilter, DWORD *pCardinality)
{
    THSTATE     *pTHS = pTHStls;
    SEARCHARG    SearchArg;
    SEARCHRES    SearchRes;
    ENTINFSEL    EntInfSel;
    ATTR         attrObjectGuid;
    DWORD        dwErr=0;
    BOOL         fMoreData;
    PRESTART     pRestart;
    DWORD        i = 0;

    *pCardinality = 0;

    do {
        //
        // Setup the Attribute Select parameter
        //
        attrObjectGuid.attrTyp = ATT_OBJECT_GUID;
        attrObjectGuid.AttrVal.valCount = 0;
        attrObjectGuid.AttrVal.pAVal = NULL;

        EntInfSel.attSel = EN_ATTSET_LIST;
        EntInfSel.infoTypes = EN_INFOTYPES_TYPES_VALS;
        EntInfSel.AttrTypBlock.attrCount = 1;
        EntInfSel.AttrTypBlock.pAttr = &attrObjectGuid;

        // Initialize 

        memset(&SearchArg, 0, sizeof(SEARCHARG));
        
        InitCommarg(&SearchArg.CommArg);

        SearchArg.pObject = pObjectDN;

        SearchArg.choice = searchType;
        SearchArg.bOneNC = TRUE;
        SearchArg.pFilter = pFilter;
        SearchArg.pSelection = &EntInfSel;
        SearchArg.pSelectionRange = NULL;

        fMoreData = TRUE;
        pRestart = NULL;

        while (fMoreData) {

            SearchArg.CommArg.PagedResult.fPresent = TRUE;
            SearchArg.CommArg.PagedResult.pRestart = pRestart;
#ifdef DBG
            SearchArg.CommArg.ulSizeLimit = 4;
#else
            SearchArg.CommArg.ulSizeLimit = 200;
#endif
            
            if (eServiceShutdown) {
                break;
            }

            memset(&SearchRes, 0, sizeof (SEARCHRES));

            SearchBody(pTHS, &SearchArg, &SearchRes, 0);
            dwErr = pTHS->errCode;

            if (dwErr) {
                break;
            }

            // Set fMoreData for next iteration
            if ( !( (SearchRes.PagedResult.pRestart != NULL)
                        && (SearchRes.PagedResult.fPresent)
                  ) ) {
                // No more data needs to be read. So no iterarions needed after this
                fMoreData = FALSE;
            }
            else {
                // more data. save off the restart to use in the next iteration.
                pRestart = SearchRes.PagedResult.pRestart;
            }

            *pCardinality += SearchRes.count;

            DBFreeSearhRes (pTHS, &SearchRes, FALSE);

            DPRINT2 (0, "Cardinality (iteration: %d) = %d\n", ++i, *pCardinality);
        }

    } while ( 0 );


    if (!fNullUuid(&pObjectDN->Guid)) {
        ScriptLogGuid ( DSLOG_TRACE, "Cardinality Check. ObjectGUID:", &pObjectDN->Guid );
    }
    ScriptLogPrint ( (DSLOG_TRACE, "Cardinality Check (%ws). Found=(%d) Type=(%d) Result: %d(0x%x)\r\n", 
                                pObjectDN->NameLen ? pObjectDN->StringName : L"NULL",
                                *pCardinality, 
                                searchType,
                                dwErr, dwErr) );        

    LogEvent8(DS_EVENT_CAT_INTERNAL_PROCESSING,
              DS_EVENT_SEV_MINIMAL,
              DIRLOG_SCRIPT_OPERATION_CARDINALITY,
              szInsertDN(pObjectDN), 
              szInsertUL(*pCardinality), 
              szInsertWin32ErrCode(dwErr),
              szInsertWin32Msg(dwErr),
              NULL, NULL, NULL, NULL);

    return dwErr;
}


//
//  ScriptCompareRequest
//
//  Description:
//
//     Implement the Compare request
//
//  Arguments:
//
//      pObjectDN - the object we are checking values of
//      pAttribute - the name of the attribute we are interested in
//      pAttrVal - the expected value of the attribute
//      pDefaultVal - the default value we should compare against if the
//                    attribute is not present on the object
//      pfMatch (OUT) - the compare result (TRUE = the same)
//
//  Return Value:
//
//     0 on success
//     error on failure
// 
//
DWORD ScriptCompareRequest (DSNAME *pObjectDN, WCHAR *pAttribute, WCHAR *pAttrVal, WCHAR *pDefaultVal, BOOL *pfMatch)
{
    THSTATE         *pTHS = pTHStls;
    DWORD            dwErr=0;
    LPSTR            szErrStr;
    COMPAREARG       CompareArg;
    COMPARERES       CompareRes;
    ATTCACHE        *pAC;

    // Initialize 

    memset(&CompareArg, 0, sizeof(COMPAREARG));
    memset (&CompareRes, 0, sizeof (COMPARERES));
    InitCommarg(&CompareArg.CommArg);

    *pfMatch = FALSE;

    do {

        pAC = getAttByNameW( pTHS, pAttribute);
        if (!pAC) {
            ScriptLogLevel (0, ScriptLogPrint ( (DSLOG_ERROR, "Unknown Attribute: %ws\r\n", pAttribute) ) );
            dwErr = ScriptProcessError(ERROR_DS_ATT_NOT_DEF_IN_SCHEMA);
            SetSvcError(SV_PROBLEM_DIR_ERROR, dwErr);
            break;
        }

        CompareArg.pObject = pObjectDN;
        CompareArg.Assertion.type = pAC->id;

        if (dwErr = ConvertScriptValueToDirAttrVal (pTHS, pAC, pAttrVal, &CompareArg.Assertion.Value)) {
            SetSvcError(SV_PROBLEM_DIR_ERROR, dwErr);
            break;
        }

        if (DoNameRes(pTHS, 0, CompareArg.pObject, &CompareArg.CommArg,
                                   &CompareRes.CommRes, &CompareArg.pResObj)){
            dwErr = pTHS->errCode;
            break;
        }                                                             

        dwErr = LocalCompare(pTHS, &CompareArg, &CompareRes);
        DPRINT2 (0, "Compare result %d. Matched=%d\n", dwErr, CompareRes.matched);
        if (CompareRes.matched) {
            *pfMatch = TRUE;
        }
        else if (dwErr == attributeError && pDefaultVal) {
            if (wcscmp (pAttrVal, pDefaultVal) == 0) {
                *pfMatch = TRUE;
            }
            pTHS->errCode = dwErr = 0;
            THClearErrors();
            DPRINT2 (0, "Compare is using default value: %ws. Matched=%d\n", pDefaultVal, *pfMatch);
        }

    } while ( 0 );

    if (!fNullUuid(&pObjectDN->Guid)) {
        ScriptLogGuid ( DSLOG_TRACE, "Compare Check. ObjectGUID:", &pObjectDN->Guid );
    }
    
    szErrStr = THGetErrorString();
    ScriptLogPrint ( (DSLOG_TRACE, "Compare Check (%ws)  Attr(%ws)?=(%ws) = (%ws) Error: %S\r\n", 
                        pObjectDN->NameLen ? pObjectDN->StringName : L"NULL",
                        pAttribute,
                        pAttrVal,
                        *pfMatch ? L"TRUE" : L"FALSE", 
                        szErrStr) );

    LogEvent(DS_EVENT_CAT_INTERNAL_PROCESSING,
             DS_EVENT_SEV_MINIMAL,
             DIRLOG_SCRIPT_OPERATION_COMPARE,
             szInsertDN(pObjectDN), 
             szInsertHex(*pfMatch), 
             szInsertSz(szErrStr));
    
    THFreeEx(pTHS, szErrStr);
    
    return dwErr;
}

//
//  ScriptUpdateRequest
//
//  Description:
//
//     Implement the Update request
//
//  Arguments:
//
//      pObjectDN - the object we are updating values of
//      attributesList - the list of attributes we are updating
//      metadataUpdate - whether to update metadata or not
//
//  Return Value:
//
//     0 on success
//     error on failure
// 
//
DWORD ScriptUpdateRequest (DSNAME *pObjectDN, ScriptAttributeList &attributesList, BOOL metadataUpdate)
{
    THSTATE         *pTHS = pTHStls;
    DWORD            dwErr=0;
    MODIFYARG        modifyArg;
    MODIFYRES        modRes;
    ATTRMODLIST      *pMod, *pModTemp;
    DWORD            cModCount=0;
    DWORD            cbLen;
    DWORD            i,j;
    LPSTR            szErrStr;

    ATTCACHE        *pAC;
    BOOL            fOldMetaDataUpdate;
    
    ScriptAttributeList::iterator it = attributesList.begin();
    ScriptAttribute *pAttrEl;


    DPRINT1 (0, "Updating Object: %ws\n", pObjectDN->StringName);


    if (!fNullUuid(&pObjectDN->Guid)) {
        ScriptLogGuid ( DSLOG_TRACE, "Update. ObjectGUID:", &pObjectDN->Guid );
    }
    if (pObjectDN->NameLen) {
        ScriptLogPrint ( (DSLOG_TRACE, "Update (%ws)\r\n", pObjectDN->StringName) );        
    }


    it = attributesList.begin();
    while (it != attributesList.end()) {

        pAttrEl = *it;

        ScriptLogPrint ( (DSLOG_TRACE, "Update Attr(%ws)=%ws  Type(%d)\r\n", 
                                pAttrEl->m_name, 
                                pAttrEl->m_characters,
                                pAttrEl->m_operation_type) );      

        it++;
    }

    // Initialize 
    
    memset(&modifyArg, 0, sizeof(modifyArg));
    memset(&modRes, 0, sizeof(modRes));
    InitCommarg(&modifyArg.CommArg);

    // we don't care if the value was not there by default when we modify the object
    modifyArg.CommArg.Svccntl.fPermissiveModify = TRUE;

    // do we really want to update metadata ?
    fOldMetaDataUpdate = pTHS->pDB->fSkipMetadataUpdate;
    pTHS->pDB->fSkipMetadataUpdate = !metadataUpdate;

    do {
        modifyArg.pObject = pObjectDN;

        // Perform name resolution to locate object.  If it fails, just
        // return an error, which may be a referral.  
        // We need a writable copy of the object
        if (DoNameRes(pTHS, 0, modifyArg.pObject, &modifyArg.CommArg,
                                   &modRes.CommRes, &modifyArg.pResObj)){
            dwErr = pTHS->errCode;
            break;
        }                                                             

        pMod = &modifyArg.FirstMod;

        it = attributesList.begin();
        while (it != attributesList.end()) {

            pMod->pNextMod = NULL;
            
            pAttrEl = *it;

            pAC = getAttByNameW( pTHS, pAttrEl->m_name);
            if (!pAC) {
                DPRINT1 (0, "Attribute Not Found In Schema: %ws\n", pAttrEl->m_name);
                dwErr = ScriptProcessError(ERROR_DS_ATT_NOT_DEF_IN_SCHEMA);
                SetSvcError(SV_PROBLEM_DIR_ERROR, dwErr);
                break;
            }

            // if the attribute is not a member of partial Attribute Set
            // and the object is not writable, 
            if (!pAC->bMemberOfPartialSet && 
                !(modifyArg.pResObj->InstanceType & IT_WRITE)) {
                
                DPRINT1 (0, "Skipping update of attribute %ws because not in GC set\n", pAttrEl->m_name);

                it++;
                continue;
            }

            DPRINT2 (0, "Update Attribute %s=%ws\n", pAC->name, pAttrEl->m_characters);

            switch (pAttrEl->m_operation_type) {
            case ScriptAttribute::SCRIPT_OPERATION_APPEND:
                pMod->choice = AT_CHOICE_ADD_ATT;
                break;
            case ScriptAttribute::SCRIPT_OPERATION_REPLACE:
                pMod->choice = AT_CHOICE_REPLACE_ATT;
                break;
            case ScriptAttribute::SCRIPT_OPERATION_DELETE:
                pMod->choice = AT_CHOICE_REMOVE_ATT;
                break;
            }

            if (dwErr = ConvertScriptElementToDirAttr (pTHS, pAttrEl, &pMod->AttrInf)) {
                SetSvcError(SV_PROBLEM_DIR_ERROR, dwErr);
                break;
            }

            it++;
            cModCount++;

            if (it != attributesList.end()) {
                pMod->pNextMod = (ATTRMODLIST *) THAllocEx (pTHS, sizeof (ATTRMODLIST));
                pMod = pMod->pNextMod;
            }
        }

        if (dwErr) {
            break;
        }

        modifyArg.count = cModCount;

        // do the modify call
        //
        LocalModify(pTHS, &modifyArg);
        dwErr =  pTHS->errCode;

        DPRINT1 (0, "Update result %d\n", dwErr);

    } while ( 0 );

    // free MODIFYARG
    pMod = &modifyArg.FirstMod;
    while (pMod) {
        if (pMod->AttrInf.AttrVal.pAVal) {
            if (pMod->AttrInf.AttrVal.pAVal->pVal) {
                THFreeEx (pTHS, pMod->AttrInf.AttrVal.pAVal->pVal);
            }
            THFreeEx (pTHS, pMod->AttrInf.AttrVal.pAVal);
        }

        pModTemp = pMod;
        pMod = pMod->pNextMod;
        
        // the first one is not allocated
        if ( pModTemp != &modifyArg.FirstMod ) {
            THFreeEx (pTHS, pModTemp);
        }
    }

    pTHS->pDB->fSkipMetadataUpdate = fOldMetaDataUpdate;

    szErrStr = THGetErrorString();

    ScriptLogPrint ( (DSLOG_TRACE, "Update Result = %d(%S)\r\n", dwErr, szErrStr) );      

    LogEvent(DS_EVENT_CAT_INTERNAL_PROCESSING,
             DS_EVENT_SEV_MINIMAL,
             DIRLOG_SCRIPT_OPERATION_UPDATE,
             szInsertDN(pObjectDN), 
             szInsertSz(szErrStr), NULL );
    
    THFreeEx(pTHS, szErrStr);

    return dwErr;
}

//
//  ScriptMoveRequest
//
//  Description:
//
//     Implement the Move request
//
//  Arguments:
//
//      pObjectDN - the source object 
//      pDestDN - the destination object (not the destination parent)
//      metadataUpdate - whether to update metadata or not (1 or 0)
//
//  Return Value:
//
//     0 on success
//     error on failure
// 
//
DWORD ScriptMoveRequest (DSNAME *pObjectDN, DSNAME *pDestDN, BOOL metadataUpdate)
{
    THSTATE         *pTHS = pTHStls;
    DWORD            dwErr=0;
    MODIFYDNARG      modifyDNArg;
    MODIFYDNRES      modifyDNRes;
    DSNAME          *pNewParentDN = NULL;
    ATTRBLOCK       *pDestBlockName = NULL;
    RESOBJ          *pParentResObj = NULL;
    COMMRES          commRes;
    LPSTR            szErrStr;

    // Initialize 
    
    memset(&modifyDNArg, 0, sizeof(modifyDNArg));
    memset(&modifyDNRes, 0, sizeof(modifyDNRes));
    memset (&commRes, 0, sizeof (COMMRES));
    InitCommarg(&modifyDNArg.CommArg);

    // do we really want to update metadata ?
    pTHS->pDB->fSkipMetadataUpdate = !metadataUpdate;

    DPRINT2 (0, "Moving Object: %ws ===> %ws\n", pObjectDN->StringName, pDestDN->StringName);
        
    if (!fNullUuid(&pObjectDN->Guid)) {
        ScriptLogGuid ( DSLOG_TRACE, "Move Source. ObjectGUID:", &pObjectDN->Guid );
    }
    if (pObjectDN->NameLen) {
        ScriptLogPrint ( (DSLOG_TRACE, "Move Source (%ws)\r\n", pObjectDN->StringName) );        
    }

    if (!fNullUuid(&pDestDN->Guid)) {
        ScriptLogGuid ( DSLOG_TRACE, "Move Dest. ObjectGUID:", &pDestDN->Guid );
    }
    if (pDestDN->NameLen) {
        ScriptLogPrint ( (DSLOG_TRACE, "Move Dest (%ws)\r\n", pDestDN->StringName) );        
    }


    do {
        // break the dsname in parts
        if (dwErr = DSNameToBlockName (pTHS, pDestDN, &pDestBlockName, FALSE)) {
            SetSvcError(SV_PROBLEM_DIR_ERROR, dwErr);
            break;
        }
        Assert (pDestBlockName->attrCount);

        // get the new parent
        pNewParentDN = (DSNAME *) THAllocEx(pTHS, pDestDN->structLen);

        if (TrimDSNameBy ( pDestDN, 1, pNewParentDN)) {
            dwErr = ScriptParseError(ERROR_DS_INVALID_DN_SYNTAX);
            SetSvcError(SV_PROBLEM_DIR_ERROR, dwErr);
            break;
        }
        modifyDNArg.pObject = pObjectDN;
        modifyDNArg.pNewParent = pNewParentDN;
        
        // get the newRDN
        modifyDNArg.pNewRDN = &pDestBlockName->pAttr[pDestBlockName->attrCount-1];

        // we don't need a writable copy of the object
        modifyDNArg.CommArg.Svccntl.dontUseCopy = FALSE;

        // check to see that the new parent exists
        // if it does not exist, create a phantom
        if (DoNameRes(pTHS,
                      0,
                      pNewParentDN,
                      &modifyDNArg.CommArg,
                      &commRes,
                      &pParentResObj)){

            modifyDNArg.fAllowPhantomParent = TRUE;

            THClearErrors();

            if (DoNameRes(pTHS,
                      NAME_RES_PHANTOMS_ALLOWED | NAME_RES_VACANCY_ALLOWED,
                      pNewParentDN,
                      &modifyDNArg.CommArg,
                      &commRes,
                      &pParentResObj)) {
                
                DPRINT1 (0, "ScriptMoveRequest: New Parent does not exist: %ws\n", pNewParentDN->StringName);
            }
            else {
                DPRINT1 (0, "ScriptMoveRequest: New Parent exists as a phantom: %ws\n", pNewParentDN->StringName);
            }

            THClearErrors();

#if DBG
            dwErr = DBFindDSName( pTHS->pDB, pNewParentDN );
            DPRINT1 (0, "ScriptMoveRequest: FindParent: %d\n", dwErr);
#endif
            
            dwErr = DBFindDSName( pTHS->pDB, modifyDNArg.pObject );
            DPRINT1 (0, "ScriptMoveRequest: FindObject: %d\n", dwErr);

            // we can use a phantom as the object
            if (dwErr && dwErr != ERROR_DS_NOT_AN_OBJECT) {
                SetSvcError(SV_PROBLEM_DIR_ERROR, dwErr);
                pTHS->errCode = dwErr;
                break;
            }

            // do the trick so as to write the new phantom parent
            dwErr = DBAddAttVal(pTHS->pDB, ATT_DN_REFERENCE_UPDATE,
                              pNewParentDN->structLen,
                              pNewParentDN);

            if (dwErr) {
                SetSvcError(SV_PROBLEM_DIR_ERROR, dwErr);
                break;
            }

            // and remove the value since it is not needed
            dwErr = DBRemAttVal(pTHS->pDB, ATT_DN_REFERENCE_UPDATE,
                              pNewParentDN->structLen,
                              pNewParentDN);

            dwErr = DBRepl( pTHS->pDB, FALSE, 0, NULL, 0 );
            if (dwErr) {
                SetSvcError(SV_PROBLEM_DIR_ERROR, dwErr);
                break;
            }

            if (DoNameRes(pTHS,
                      NAME_RES_PHANTOMS_ALLOWED | NAME_RES_VACANCY_ALLOWED,
                      pNewParentDN,
                      &modifyDNArg.CommArg,
                      &commRes,
                      &pParentResObj)) {

                DPRINT1 (0, "ScriptMoveRequest: New Parent STILL does not exist: %ws\n", pNewParentDN->StringName);
                Assert (!"New Parent STILL does not exist");
                dwErr =  pTHS->errCode;
                break;
            }
        }
        
        // fill in the pResObj
        if (DoNameRes(pTHS,
                      NAME_RES_PHANTOMS_ALLOWED | NAME_RES_VACANCY_ALLOWED,
                      modifyDNArg.pObject,
                      &modifyDNArg.CommArg,
                      &modifyDNRes.CommRes,
                      &modifyDNArg.pResObj)){
            dwErr = pTHS->errCode;
            break;
        }

        ScriptLogGuid ( DSLOG_TRACE, "Move Source. ObjectGUID:",
                        &modifyDNArg.pResObj->pObj->Guid );

        // Local Modify operation

        LocalModifyDN(pTHS,
                      &modifyDNArg,
                      &modifyDNRes,
                      FALSE);

        dwErr =  pTHS->errCode;

    } while ( 0 );

    szErrStr = THGetErrorString();
    
    ScriptLogPrint ( (DSLOG_TRACE, "Move Result %d(%S)\r\n", dwErr, szErrStr) );

    pTHS->pDB->fSkipMetadataUpdate = 0;

    LogEvent(DS_EVENT_CAT_INTERNAL_PROCESSING,
             DS_EVENT_SEV_MINIMAL,
             DIRLOG_SCRIPT_OPERATION_MOVE,
             szInsertDN(pObjectDN), 
             szInsertDN(pDestDN),
             szInsertSz(szErrStr));

    THFreeEx(pTHS, szErrStr);
    
    return dwErr;
}

//
//  ScriptCreateRequest
//
//  Description:
//
//     Implement the Create request
//
//  Arguments:
//
//      pObjectDN - the object we are adding
//      attributesList - the list of attributes on the object
//
//  Return Value:
//
//     0 on success
//     error on failure
// 
//
DWORD ScriptCreateRequest (DSNAME *pObjectDN, ScriptAttributeList &attributesList)
{
    THSTATE         *pTHS = pTHStls;
    DWORD            dwErr=0;
    ADDARG           addArg;
    ADDRES           addRes;
    ATTRMODLIST      *pMod;
    DWORD            cAttrCount=0;
    ATTRBLOCK       *pAttrBlock;
    ATTR            *pAttr;
    DSNAME          *pParentDN = NULL;
    LPSTR            szErrStr;

    ScriptAttributeList::iterator it = attributesList.begin();
    ScriptAttribute *pAttrEl;

    // Initialize 
    
    memset(&addArg, 0, sizeof(addArg));
    InitCommarg(&addArg.CommArg);

    do {

        addArg.pObject = pObjectDN;

        // get the parent
        pParentDN = (DSNAME *) THAllocEx(pTHS, pObjectDN->structLen);

        if (TrimDSNameBy ( pObjectDN, 1, pParentDN)) {
            dwErr = ScriptParseError(ERROR_DS_INVALID_DN_SYNTAX);
            SetSvcError(SV_PROBLEM_DIR_ERROR, dwErr);
            break;
        }

        if (DoNameRes(pTHS, 0, pParentDN, &addArg.CommArg,
                                   &addRes.CommRes, &addArg.pResParent)){
            dwErr = pTHS->errCode;
            break;
        }                                                             

        pAttrBlock = &addArg.AttrBlock;

        pAttrBlock->pAttr = (ATTR *)THAllocEx (pTHS, sizeof (ATTR) );

        while (it != attributesList.end()) {

            pAttrEl = *it;

            pAttr = &pAttrBlock->pAttr[cAttrCount];
            
            if (dwErr = ConvertScriptElementToDirAttr (pTHS, pAttrEl, pAttr)) {
                SetSvcError(SV_PROBLEM_DIR_ERROR, dwErr);
                break;
            }

            DPRINT2 (0, "Add Attr %ws=%ws\n", pAttrEl->m_name, pAttrEl->m_characters);

            it++;
            cAttrCount++;

            if (it != attributesList.end()) {
                pAttrBlock->pAttr = (ATTR *)THReAllocEx(pTHS, pAttrBlock->pAttr, sizeof (ATTR) * (cAttrCount+1));
            }
        }

        if (dwErr) {
            break;
        }

        pAttrBlock->attrCount = cAttrCount;

        // do the call
        //
        LocalAdd(pTHS, &addArg, FALSE);
        dwErr = pTHS->errCode;

    } while ( 0 );

    szErrStr = THGetErrorString();
    
    ScriptLogPrint ( (DSLOG_TRACE, "Add Result %d(%S)\r\n", dwErr, szErrStr) );
    
    // free ADDARG

    LogEvent(DS_EVENT_CAT_INTERNAL_PROCESSING,
             DS_EVENT_SEV_MINIMAL,
             DIRLOG_SCRIPT_OPERATION_CREATE,
             szInsertDN(pObjectDN), 
             szInsertSz(szErrStr), NULL);

    THFreeEx(pTHS, szErrStr);
    
    return dwErr;
}

//
//  ScriptComputeHashAndSignature
//
//  Description:
//
//     Calculate hash keys for the script
//
//  Arguments:
//
//      pStream - the script
//      cchStream - the lenght of the script
//      ppComputedSignature - the signature H (body + key)
//      pcbComputedSignature - the lenght of the signature
//      ppHashBody - the hash of the body H (body)
//      pcbHashBody - the lenght of the hash body
//
//  Return Value:
//
//     0 on success
//     error on failure
// 
//
DWORD ScriptComputeHashAndSignature (THSTATE *pTHS,
                                     WCHAR   *pStream,
                                     DWORD    cchStream,
                                     BYTE   **ppSignature,
                                     DWORD   *pcbSignature,
                                     BYTE   **ppHashBody,
                                     DWORD   *pcbHashBody)
{
    DWORD      dwErr = 0;
    HCRYPTPROV hCryptProv = NULL; 
    HCRYPTHASH hHash = NULL;
    HCRYPTHASH hDupHash = NULL;


    *ppHashBody =  (BYTE *)THAllocEx (pTHS,  20);
    *ppSignature = (BYTE *)THAllocEx (pTHS,  20);
    *pcbSignature = *pcbHashBody = 20;

    __try {
        // Get a handle to the default PROV_RSA_FULL provider.

        if(!CryptAcquireContext(&hCryptProv, 
                                NULL, 
                                NULL, 
                                PROV_RSA_FULL, 
                                CRYPT_SILENT | CRYPT_MACHINE_KEYSET)) {

            dwErr = GetLastError();

            if (dwErr == NTE_BAD_KEYSET) {

                dwErr = 0;

                if(!CryptAcquireContext(&hCryptProv, 
                                        NULL, 
                                        NULL, 
                                        PROV_RSA_FULL, 
                                        CRYPT_SILENT | CRYPT_MACHINE_KEYSET | CRYPT_NEWKEYSET)) {

                    dwErr = GetLastError();

                    __leave;
                }
            }
            else {
                __leave;
            }
        }

        // Create the hash object.

        if(!CryptCreateHash(hCryptProv, 
                            CALG_SHA1, 
                            0, 
                            0, 
                            &hHash)) {
            dwErr = GetLastError();
            __leave;
        }


        // Compute the cryptographic hash of the buffer.

        if(!CryptHashData(hHash, 
                         (BYTE *)pStream,
                         cchStream * sizeof (WCHAR),
                         0)) {
            dwErr = GetLastError();
            __leave;
        }

        // we have the common part of the hash ready (H(buf), now duplicate it
        // so as to calc the H (buf + guid)

        if (!CryptDuplicateHash(hHash, 
                               NULL, 
                               0, 
                               &hDupHash)) {
            dwErr = GetLastError();
            __leave;
        }


        if (!CryptGetHashParam(hHash,    
                               HP_HASHVAL,
                               *ppHashBody,
                               pcbHashBody,
                               0)) {
            dwErr = GetLastError();
            __leave;
        }

        Assert (*pcbHashBody == 20);

        
        if(!CryptHashData(hDupHash, 
                         (BYTE *)&guidDomainRename,
                         sizeof (GUID),
                         0)) {
            dwErr = GetLastError();
            __leave;
        }
        
        if (!CryptGetHashParam(hDupHash,    
                               HP_HASHVAL,
                               *ppSignature,
                               pcbSignature,
                               0)) {
            dwErr = GetLastError();
            __leave;
        }

        Assert (*pcbSignature == 20);

    }
    __finally {

        if (hDupHash)
            CryptDestroyHash(hDupHash);

        if(hHash) 
            CryptDestroyHash(hHash);

        if(hCryptProv) 
            CryptReleaseContext(hCryptProv, 0);
    }

    return dwErr;
}


//
//  ScriptCalculateAndCheckHashKeys
//
//  Description:
//
//     Calculate hash keys for the script
//     Checks to see whether hash keys match the one on the script
//
//  Arguments:
//
//      pwScript - the script
//      cchScriptLen - the lenght of the script
//      ppComputedSignature - the signature H (body + key)
//      pcbComputedSignature - the lenght of the signature
//      ppHashBody - the hash of the body H (body)
//      pcbHashBody - the lenght of the hash body
//
//  Return Value:
//
//     0 on success
//     error on failure
// 
//
DWORD ScriptCalculateAndCheckHashKeys (THSTATE *pTHS, 
                                       IN WCHAR   *pwScript,
                                       IN DWORD    cchScriptLen,
                                       OUT BYTE    **ppComputedSignature,
                                       OUT DWORD   *pcbComputedSignature,
                                       OUT BYTE    **ppHashBody,
                                       OUT DWORD   *pcbHashBody
                                       )
{
    DWORD dwErr = 0;
    WCHAR *pwSignature = NULL;
    
    CHAR  *pSignatureText = NULL;
    DWORD  cbSignature;
    
    BYTE   Signature[20];

    #if DBG
    CHAR   dbgSig[100];
    DWORD  dwSig;
    #endif
    
    // get the signature out of the string
    // 20 bytes as base64 encoded as 28 bytes
    //
    if (cchScriptLen <= 28) {
        dwErr = ERROR_DS_INVALID_SCRIPT;
        goto exit;
    }

    cchScriptLen -= 28;
    pwSignature = pwScript + cchScriptLen;
    
    pSignatureText = (CHAR *)String8FromUnicodeString(TRUE, 
                                                      CP_UTF8, 
                                                      pwSignature, 
                                                      -1, 
                                                      (LPLONG)&cbSignature, 
                                                      NULL);

    dwErr = base64decode(pSignatureText, 
                         Signature,
                         sizeof (Signature),
                         &cbSignature);

    if (FAILED(dwErr)) {
        dwErr = ERROR_DS_INVALID_SCRIPT;
        goto exit;
    }


    // remove the signature from the script
    *pwSignature = 0;

    if (dwErr = ScriptComputeHashAndSignature (pTHS, 
                                               pwScript,
                                               cchScriptLen,
                                               ppComputedSignature,
                                               pcbComputedSignature,
                                               ppHashBody,
                                               pcbHashBody)) {
        goto exit;
    }

    #if DBG
    if (dwErr = base64encode(*ppComputedSignature, 
                             *pcbComputedSignature, 
                             dbgSig,
                             sizeof(dbgSig),
                             &dwSig)) {

        DPRINT (0, "Error encoding signature\n");
        goto exit;
    }
    //DPRINT1 (0, "Script: %ws\n", pwScript);
    DPRINT3 (0, "ScriptLen: %d  %d (%d)\n", cchScriptLen, wcslen (pwScript), cchScriptLen*2);
    DPRINT1 (0, "Stored Signature:%s|\n", pSignatureText);
    DPRINT1 (0, "Computed Signature:%s|\n", dbgSig);
    #endif


    if ((cbSignature != *pcbComputedSignature) || 
        memcmp (Signature, *ppComputedSignature, *pcbComputedSignature)!=0) {
        dwErr = ERROR_DS_INVALID_SCRIPT;
        goto exit;
    }
    
    DPRINT (0, "Script is SIGNED\n ");

exit:
    if (pSignatureText) {
        THFreeEx(pTHS, pSignatureText);
    }

    return dwErr;
}


//
//  ScriptReadFromDatabase
//
//  Description:
//
//     Read the script from the database
//     Checks to see whether the script is signed using the provided key
//
//  Arguments:
//
//      ppScript - where to store the script
//
//  Return Value:
//
//     0 on success
//     error on failure
// 
//
DWORD ScriptReadFromDatabase (DBPOS *pDB, WCHAR **ppScript, DWORD *pcchLen)
{
    DWORD dwErr;
    DWORD cbActual;
    
    dwErr = DBFindDSName(pDB, gAnchor.pPartitionsDN);
    switch (dwErr) {
    case 0:
        dwErr = DBGetAttVal (pDB, 
                             1, 
                             ATT_MS_DS_UPDATESCRIPT,
                             0,
                             0,
                             &cbActual,
                             (UCHAR **)ppScript);

        if (dwErr) {
            DPRINT1 (1, "ScriptReadFromDatabase: Error reading value: %d\n", dwErr);
            if (dwErr == DB_ERR_NO_VALUE) {
                dwErr = DIRERR_MISSING_EXPECTED_ATT;
            }
            else {
                dwErr = DIRERR_DATABASE_ERROR;
            }
        }
        else {
            *ppScript = (WCHAR *)THReAllocEx (pDB->pTHS, *ppScript, cbActual+sizeof(WCHAR));
            *pcchLen = cbActual/sizeof(WCHAR);
            (*ppScript)[*pcchLen]=0;

            DPRINT1 (1, "ScriptReadFromDatabase: XML script %d bytes\n", cbActual); 
        }
        break;

    case DIRERR_OBJ_NOT_FOUND:
    case DIRERR_NOT_AN_OBJECT:

    default:
        DPRINT1 (0, "Partitions container not found: %d\n", dwErr);
        return dwErr;
        break;
    }

    return dwErr;
}


//
//  ScriptGenerateAndStorePassword
//
//  Description:
//
//     Generate a password and store it in the database
//
//  Arguments:
//
//      ppPassword - the password returned to the user
//      pcbPassword - the lenght of the password generated
//
//  Return Value:
//
//     0 on success
//     error on failure
// 
//
DWORD ScriptGenerateAndStorePassword (THSTATE *pTHS, 
                                      BYTE **ppPassword, 
                                      DWORD *pcbPassword)
{
    DWORD dwErr = 0;
    HCRYPTPROV hCryptProv = NULL; 

    *pcbPassword = 8;
    *ppPassword = (BYTE *)THAllocEx (pTHS, *pcbPassword);

    __try {
        // Get a handle to the default PROV_RSA_FULL provider.

        if(!CryptAcquireContext(&hCryptProv, 
                                NULL, 
                                NULL, 
                                PROV_RSA_FULL, 
                                CRYPT_SILENT | CRYPT_MACHINE_KEYSET)) {
            dwErr = GetLastError();
            __leave;
        }

        if (!CryptGenRandom(hCryptProv,
                            *pcbPassword, 
                            *ppPassword)) {

            dwErr = GetLastError();
            __leave;
        }
    }
    __finally {

        if(hCryptProv) 
            CryptReleaseContext(hCryptProv, 0);
    }

    if (dwErr) {
        return dwErr;
    }


    dwErr = DBFindDSName(pTHS->pDB, gAnchor.pPartitionsDN);
    switch (dwErr) {
    case 0:

        if (dwErr = DBReplaceAttVal (pTHS->pDB, 
                                     1, 
                                     ATT_MS_DS_EXECUTESCRIPTPASSWORD,
                                     *pcbPassword,
                                     *ppPassword)) {

            DPRINT1 (0, "ScriptGenerateAndStorePassword: Error writing value: %d\n", dwErr);
        }
        else {
            DBUpdateRec(pTHS->pDB);
        }

        break;

    case DIRERR_OBJ_NOT_FOUND:
    case DIRERR_NOT_AN_OBJECT:

    default:
        DPRINT1 (0, "Partitions container not found: %d\n", dwErr);
        return dwErr;
        break;
    }

    return dwErr;
}



DWORD ScriptPrepareForParsing (THSTATE    *pTHS, 
                               IN WCHAR  **ppScript,
                               BSTR      *pBstrScript)
{

    // create a variant for the value passed to the Parser
    *pBstrScript = SysAllocString(  *ppScript );
    if (!*pBstrScript) {
        return ERROR_OUTOFMEMORY;
    }

    // we don't need the value anymore since we converted it
    THFreeEx (pTHS, *ppScript); *ppScript = NULL;

    return 0;
}

// exception-safe script executer
DWORD handlerScriptExecute(
    NTDSContent* handler, 
    ScriptProcessMode fMode, 
    DWORD &returnCode, 
    const WCHAR **ppErrorMessage)
{
    ULONG dwException, ulErrorCode, dsid;
    PVOID dwEA;
    DWORD err;

    __try {
        err = handler->Process(fMode, returnCode, ppErrorMessage);
    }
    __except(GetExceptionData(GetExceptionInformation(), &dwException,
                              &dwEA, &ulErrorCode, &dsid)) {
        DPRINT3 (0, "Got an exception %x (err %x) while executing script: dsid: %x\n", dwException, ulErrorCode, dsid);
        ScriptException(dwException, ulErrorCode, dsid);
        err = ERROR_EXCEPTION_IN_SERVICE;
        SetSvcError(SV_PROBLEM_DIR_ERROR, err);
    }
    return err;
}

typedef enum {
    SCRIPT_OPERATION_GET_OPTYPE,
    SCRIPT_OPERATION_PREPROCESS,
    SCRIPT_OPERATION_EXECUTE
} SCRIPT_OPERATION;

//
//  ScriptExecute
//
//  Description:
//
//     Read the script from the database
//     Checks to see whether the script is signed using the provided key
//
//  Arguments:
//
//      scriptOperation - what needs to be done
//      pBstrScript - the script to execute. This string should have been prepared with 
//                    ScriptPrepareForParsing
//      pScriptOptype - when scriptOperation==SCRIPT_GET_OPTYPE, then the script optype is returned here
//
//  Return Value:
//
//     0 on success
//     error on failure
// 
//
DWORD ScriptExecute (
    THSTATE *pTHS, 
    SCRIPT_OPERATION scriptOperation, 
    BSTR pBstrScript, 
    WCHAR ** ppErrMessage,
    ScriptOperationType* pScriptOptype
    )
{
    HRESULT             hr = S_OK;
    DWORD               err = 0;
    DWORD               retCode;
    ISAXXMLReader *     pReader = NULL;
    NTDSContent*        pHandler = NULL; 
    IClassFactory *     pFactory = NULL;

    VARIANT             varScript;
    const WCHAR         *pErrMessage = NULL;

    //::CoInitialize(NULL);
    
    try {
        // create a variant for the value passed to the Parser
        VariantInit(&varScript);
        varScript.vt = VT_BYREF|VT_BSTR;
        varScript.pbstrVal = &pBstrScript; 
        
        // do the COM creation of the SAMXMLReader manually
        GetClassFactory( CLSID_SAXXMLReader, &pFactory);

        hr = pFactory->CreateInstance( NULL, __uuidof(ISAXXMLReader), (void**)&pReader);

        if(FAILED(hr)) {
            err = ScriptParseError(hr);
            ScriptLogPrint ( (DSLOG_ERROR,  "Error from CreateInstance: 0x%08x\r\n",hr) );
            SetSvcError(SV_PROBLEM_UNABLE_TO_PROCEED, hr);
            goto ExitTry;
        }

        pHandler = new NTDSContent();
        hr = pReader->putContentHandler(pHandler);

        SAXErrorHandlerImpl * pEc = new SAXErrorHandlerImpl();
        hr = pReader->putErrorHandler(pEc);

        hr = pReader->parse(varScript);
        ScriptLogPrint ( (DSLOG_TRACE,  "XML Parse result code: 0x%08x\r\n",hr) );

        if(FAILED(hr)) {
            err = ScriptParseError(hr);
            ScriptLogPrint ( (DSLOG_ERROR,  "Error Parsing XML: 0x%08x\r\n",hr) );
            SetSvcError(SV_PROBLEM_UNABLE_TO_PROCEED, err);
            goto ExitTry;
        }
            
        err = handlerScriptExecute(pHandler, SCRIPT_PROCESS_VALIDATE_SYNTAX_PASS, retCode, &pErrMessage);
        ScriptLogPrint ( (DSLOG_TRACE,  "Syntax Validate Processing: 0x%08X retCode:%d(0x%x) ErrorMessage(%ws)\r\n", err, retCode, retCode, pErrMessage) );

        if (!err && !retCode) {
            switch (scriptOperation) {
            case SCRIPT_OPERATION_GET_OPTYPE:
                *pScriptOptype = pHandler->GetScriptOptype();
                break;

            case SCRIPT_OPERATION_PREPROCESS:
                err = handlerScriptExecute(pHandler, SCRIPT_PROCESS_PREPROCESS_PASS, retCode, &pErrMessage);
                ScriptLogPrint ( (DSLOG_TRACE,  "Execute Preprocessing(RO): 0x%08X retCode:%d(0x%x) ErrorMessage(%ws) \r\n", err, retCode, retCode, pErrMessage) );
                break;

            case SCRIPT_OPERATION_EXECUTE:
                err = handlerScriptExecute(pHandler, SCRIPT_PROCESS_EXECUTE_PASS, retCode, &pErrMessage);
                ScriptLogPrint ( (DSLOG_TRACE,  "Execute Processing(RW): 0x%08X retCode:%d(0x%x) ErrorMessage(%ws)\r\n", err, retCode, retCode, pErrMessage) );
                break;
            }
        }

        // retcode means that something was not as expected in the data
        // stored in the DS
        if (err == 0 && retCode != 0) {
            err = retCode;
        }
        // set error accordingly to fail transaction
        if (err != 0 && pTHS->errCode == 0) {
            SetSvcError(SV_PROBLEM_DIR_ERROR, err);
        }

ExitTry: ;
    }
    catch (...) {
        err = ScriptParseError(ERROR_EXCEPTION_IN_SERVICE);
        SetSvcError(SV_PROBLEM_DIR_ERROR, err);
    }

    if (pErrMessage && ppErrMessage) {
        *ppErrMessage = (WCHAR *)THAllocEx (pTHS, wcslen (pErrMessage) * sizeof (WCHAR) + sizeof (WCHAR));

        wcscpy (*ppErrMessage, pErrMessage);
    }

    if (pReader) {
        pReader->Release();
    }

    if (pHandler) {
        delete pHandler;
    }

    if (pFactory) {
        pFactory->Release();
    }

    //::CoUninitialize();

    return err;
}

//
//  ScriptAsyncDsShutdown
//
//  Description:
//
//     Asyncronously Unitialize the DS
//     This function should be the entry point of a new thread
//
//  Arguments:
//
//
//  Return Value:
//
//     0 on success
// 
//
unsigned int ScriptAsyncDsShutdown ( void * StartupParam )
{
    HMODULE    ResourceDll;
    WCHAR     *ErrorMessage = NULL;
    BOOLEAN    WasEnabled;
    DWORD      dwErr = 0;

    
    // Get Message String from NTSTATUS code
    //
    ResourceDll = (HMODULE) GetModuleHandleW( L"ntdll.dll" );

    if (NULL != ResourceDll)
    {
        FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE |    // find message from ntdll.dll
                       FORMAT_MESSAGE_IGNORE_INSERTS |  // do not insert
                       FORMAT_MESSAGE_ALLOCATE_BUFFER,  // please allocate buffer for me
                       ResourceDll,                     // source dll
                       STATUS_DS_SHUTTING_DOWN,          // message ID
                       0,                               // language ID 
                       (LPWSTR)&ErrorMessage,           // address of return Message String
                       0,                               // maximum buffer size if not 0
                       NULL                             // can not insert arguments, so set to NULL
                       );

        FreeLibrary(ResourceDll);
    }

    LogEvent(DS_EVENT_CAT_SERVICE_CONTROL,
             DS_EVENT_SEV_ALWAYS,
             DIRLOG_DS_SHUTDOWN_DOMAIN_RENAME,
             NULL, NULL, NULL );
    
    DPRINT (0, "Partially Shutting Down System\n");
    
    DsUninitialize (TRUE);

    if (gfRunningInsideLsa) {

        // adjust privilege level, issue shutdown request.
        //
        dwErr = RtlAdjustPrivilege( SE_SHUTDOWN_PRIVILEGE,
                            TRUE,       // enable shutdown privilege.
                            FALSE,
                            &WasEnabled
                           );

        ASSERT( NT_SUCCESS( dwErr ) );

        DPRINT (0, "Initiating shutdown of system\n");

        dwErr = InitiateSystemShutdownExW( NULL,       // computer name
                                         ErrorMessage, // message to display
                                         60,           // length of time to display
                                         TRUE,         // force closed option
                                         TRUE,          // reboot option
                                         ERROR_DS_SHUTTING_DOWN | SHTDN_REASON_FLAG_PLANNED 
                                        );

        if (!dwErr) {
            dwErr = GetLastError ();

            LogEvent(DS_EVENT_CAT_SERVICE_CONTROL,
                     DS_EVENT_SEV_ALWAYS,
                     DIRLOG_DS_SHUTDOWN_DOMAIN_RENAME_FORCE,
                     szInsertWin32ErrCode(dwErr),
                     szInsertWin32Msg(dwErr),
                     NULL);

            DPRINT1 (0, "Error %d performing shutdown. Forcing Shutdown of DS\n", dwErr);
        }
        else {
            dwErr=0;
        }

    }

    DPRINT (0, "Shutting Down System\n");
    
    ScriptLogPrint ( (DSLOG_TRACE, "Shutting Down System\n") );        

    DsUninitialize (FALSE);

    if (!gfRunningInsideLsa) {
        return 0;
    }

    // force shutdown if we failed
    if (dwErr) {
        ScriptLogPrint ( (DSLOG_TRACE, "Forcing Shut Down System: 0x%x\r\n", dwErr) );        
        ScriptLogFlush();
        NtShutdownSystem(ShutdownReboot);
    }

    if (ErrorMessage != NULL) {
        LocalFree(ErrorMessage);
    }

    // wait for shutdown to happen
    Sleep (120 * 1000);

    return 0;
}

//
//  ScriptExecuteDSShutdown
//
//  Description:
//
//     Shutdown the DS after the completion of the script execution
//
//  Arguments:
//
//
//  Return Value:
//
//     0 on success
// 
//
void ScriptExecuteDSShutdown()
{
    unsigned   ulThreadId;
    DWORD      dwShutDown;

    if(!_beginthreadex(NULL,
                       0,
                       ScriptAsyncDsShutdown,
                       NULL,
                       0,
                       &ulThreadId)) {

        dwShutDown = GetLastError ();

        // failed, so will do the shutdown asyncronously.
        DPRINT (0, "Failed to Asyncrounously Unitialize DS. Forcing Shutdown of DS\n");

        ScriptLogPrint ( (DSLOG_TRACE, "Failed to Asyncrounously Unitialize DS. Forcing Shutdown of DS: 0x%x\r\n", dwShutDown) );        

        LogEvent(DS_EVENT_CAT_SERVICE_CONTROL,
                 DS_EVENT_SEV_ALWAYS,
                 DIRLOG_DS_SHUTDOWN_DOMAIN_RENAME_FORCE,
                 szInsertWin32ErrCode(dwShutDown),
                 szInsertWin32Msg(dwShutDown),
                 NULL);

        DsUninitialize (FALSE);

        if (gfRunningInsideLsa) {
            NtShutdownSystem(ShutdownReboot);
            ScriptLogFlush();
        }
    }
}

//
//  ScriptGetNewDomainName
//
//  Description:
//
//     Retrieve the possible new domain name
//
//  Arguments:
//
//      pbDnsDomainNameWasChanged - set to TRUE is domain name changed
//      ppLocalDnsDomainName - set to new value of LocalDomainName
//                             the memory is malloced
//
//  Return Value:
//
//     0 on success, Error otherwise
// 
//
DWORD ScriptGetNewDomainName(THSTATE *pTHS, 
                             BOOL *pbDnsDomainNameWasChanged, 
                             LPWSTR *ppLocalDnsDomainName)
{
    WCHAR NameBuffer[DNS_MAX_NAME_LENGTH + 1];
    DWORD NameBufferSize = DNS_MAX_NAME_LENGTH + 1;
    DWORD dwErr = 0;

    *ppLocalDnsDomainName = NULL;
    *pbDnsDomainNameWasChanged = FALSE;

    dwErr = ReadCrossRefPropertySecure(gAnchor.pDomainDN, 
                                       ATT_DNS_ROOT,
                                       (FLAG_CR_NTDS_NC | FLAG_CR_NTDS_DOMAIN),
                                       ppLocalDnsDomainName);

    if (dwErr) {
        DPRINT (0, "Error getting the DnsRoot of the CrossRef object\n");
        return dwErr;
    }

    // get the current DomainName

    if ( ! GetComputerNameExW(
                      ComputerNameDnsDomain,
                      NameBuffer,
                      &NameBufferSize )) {

        dwErr = GetLastError();
        DPRINT1(0,"Cannot get host name. error %x\n", dwErr);
        return dwErr;
    }


    if (!DnsNameCompare_W( NameBuffer, *ppLocalDnsDomainName)) {
        *pbDnsDomainNameWasChanged = TRUE;
        DPRINT1 (0, "Domain Name Changed. Need to set computer name to new value: %ws\n", *ppLocalDnsDomainName);
    
        ScriptLogPrint ( (DSLOG_TRACE, "Domain Name Changed. Need to set computer name to new value: %ws\r\n", *ppLocalDnsDomainName) );        
    }

    return 0;
}

//
//  SetNewDomainName
//
//  Description:
//
//     Set the new computer name in registry as required.
//
//  Arguments:
//
//      pLocalDnsDomainName - the new DNS name of the machine
//
//  Return Value:
//
//     0 on success, Error otherwise
// 
//
DWORD SetNewDomainName (LPWSTR pLocalDnsDomainName) 
{
    DWORD NetStatus = 0;

    //
    // Change the computer name as required. The new name will take effect next time
    // the computer reboots. An error here is not fatal.
    //

    if ( pLocalDnsDomainName != NULL ) {
        if ( NetStatus = NetpSetDnsComputerNameAsRequired( pLocalDnsDomainName ) ) {
            DPRINT1 (0, "Can't NetpSetDnsComputerNameAsRequired %d\n", NetStatus );

            ScriptLogPrint ( (DSLOG_ERROR, "Cannot Set New Domain Name: %ws\r\n", pLocalDnsDomainName) );        

            LogEvent(DS_EVENT_CAT_INTERNAL_PROCESSING,
                     DS_EVENT_SEV_ALWAYS,
                     DIRLOG_DOMAIN_RENAME_CANNOT_SET_NEW_DOMAIN_NAME,
                     szInsertWC (pLocalDnsDomainName),
                     szInsertWin32ErrCode(NetStatus), 
                     szInsertWin32Msg(NetStatus) );

        } else {
            DPRINT1 (0, "Successfully set computer name with suffix %ws\n", pLocalDnsDomainName );
            ScriptLogPrint ( (DSLOG_TRACE, "Successfully set computer name with suffix %ws\r\n", pLocalDnsDomainName) );        
        }
    }

    return NetStatus;
}

//
//  ScriptHasDomainRenameExecuteScriptPassword
//
//  Description:
//
//     Checks to see if the user has the correct password to execute the script
//
//  Arguments:
//      cbPassword - Lenght of password
//      pbPassword - Pointer to password
//      pbHasAccess - result
//
//  Return Value:
//
//     0 on success
//     pbHasAccess set to TRUE if user has access, otheriwse FALSE
//
DWORD ScriptHasDomainRenameExecuteScriptPassword(THSTATE *pTHS, 
                                                 DWORD   cbPassword,
                                                 BYTE    *pbPassword,
                                                 BOOL    *pbHasAccess)
{
    DWORD       dwErr;
    ULONG       ulLen;
    BYTE        *pbStoredPassword;

    *pbHasAccess = FALSE;

    dwErr = DBFindDSName(pTHS->pDB, gAnchor.pPartitionsDN);
    switch (dwErr) {
    case 0:

        if (dwErr = DBGetAttVal(pTHS->pDB, 1, ATT_MS_DS_EXECUTESCRIPTPASSWORD,
                        0, 0, &ulLen, (PUCHAR *)&pbStoredPassword)) {
            
            DPRINT1 (0, "ScriptHasDomainRenameExecuteScriptPassword: Error reading password: %d\n", dwErr);

            return dwErr;
        }
        break;

    case DIRERR_OBJ_NOT_FOUND:
    case DIRERR_NOT_AN_OBJECT:

    default:
        DPRINT1 (0, "Partitions container not found: %d\n", dwErr);
        return dwErr;
        break;
    }

    if ((ulLen == cbPassword) &&
        memcmp (pbPassword, pbStoredPassword, cbPassword) == 0 ) {
        *pbHasAccess = TRUE;
    }

    return 0;
}

//
//  ScriptHasDomainRenameExecuteScriptRight
//
//  Description:
//
//     Checks to see if the user has the right to execute the script
//     This is the same as beeing able to write the script on the partitions 
//     container
//
//  Arguments:
//      fAuditCARUseOnly -- only do an audit of control-access-right use (used on execute step)
//      pbHasAccess - result
//
//  Return Value:
//
//     0 on success
//     pbHasAccess set to TRUE if user has access, otheriwse FALSE
//
DWORD ScriptHasDomainRenameExecuteScriptRight(THSTATE *pTHS, BOOL fAuditCARUseOnly, BOOL *pbHasAccess)
{
    DWORD                dwErr;
    PSECURITY_DESCRIPTOR pNTSD = NULL;
    ULONG                ulLen;
    ATTCACHE            *pAC;
    CLASSCACHE          *pCC;
    BOOL                 fSDIsGlobalSDRef;

    *pbHasAccess = FALSE;

    dwErr = DBFindDSName(pTHS->pDB, gAnchor.pPartitionsDN);
    if (dwErr) {
        DPRINT1 (0, "Partitions container not found: %d\n", dwErr);
        return dwErr;
    }

    dwErr = DBGetObjectSecurityInfo(
                pTHS->pDB,
                pTHS->pDB->DNT,
                &ulLen,
                &pNTSD,
                &pCC,
                NULL,
                NULL,
                DBGETOBJECTSECURITYINFO_fUSE_OBJECT_TABLE,
                &fSDIsGlobalSDRef);
    if (dwErr) {
        DPRINT1 (0, "ScriptHasDomainRenameExecuteScriptRight: Error reading SD: %d\n", dwErr);
        return dwErr;
    }

    if (fAuditCARUseOnly) {
        *pbHasAccess = TRUE;
    }
    else {
        pAC = SCGetAttById (pTHS, ATT_MS_DS_UPDATESCRIPT);
        if (!pCC || !pAC) {
            return ERROR_DS_MISSING_EXPECTED_ATT;
        }

        *pbHasAccess = IsAccessGrantedAttribute(pTHS,
                                         pNTSD,
                                         gAnchor.pPartitionsDN,
                                         1,
                                         pCC,
                                         &pAC,
                                         RIGHT_DS_WRITE_PROPERTY,
                                         TRUE);
    }

    if (*pbHasAccess) {
        // audit/check the control access right
        *pbHasAccess = IsControlAccessGrantedEx(
                            pNTSD, 
                            gAnchor.pPartitionsDN, 
                            pCC, 
                            RIGHT_DS_EXECUTE_SCRIPT, 
                            fAuditCARUseOnly ? CHECK_PERMISSIONS_AUDIT_ONLY : 0,
                            TRUE);
    }

    DPRINT1 (0, "ScriptHasDomainRenameExecuteScriptRight: Has Access %d\n", *pbHasAccess);

    if (pNTSD && !fSDIsGlobalSDRef) {
        THFreeEx(pTHS, pNTSD);
    }

    return 0;
}


#define DOMAIN_RENAME_KEYSIZE 128

//  ScriptDomainRenameVerifyCallIsSecure
//
//  Description:
//
//  This routine verifies that is using >= 128bit encryption. 
//  Domain Rename Requires a secure connection when we prepare the script
//  for execution.
//
// The keysize is extracted from the security context of the caller. 
// If the extracted keysize is less than 128, an error is returned.
//
//  Return Value:
//
//  0 on suceess, 
//  ERROR_DS_STRONG_AUTH_REQUIRED on error
//  other Win32 error
//
DWORD ScriptDomainRenameVerifyCallIsSecure(THSTATE *pTHS)
{
    DWORD                   dwErr;
    DWORD                   i;
    ULONG                   KeySize;
    PCtxtHandle             pSecurityContext;
    SecPkgContext_KeyInfo   KeyInfo;


    // clear client context on the thread state since we are going to change context
    AssignAuthzClientContext(&pTHS->pAuthzCC, NULL);
    if ( dwErr = RpcImpersonateClient(NULL) ) {
        DPRINT1 (0, "RpcImpersonateClient: Error 0x%x\n", dwErr);

        if (RPC_S_CANNOT_SUPPORT == dwErr) {
            dwErr = ERROR_DS_STRONG_AUTH_REQUIRED;
        }

        return ScriptProcessError(dwErr);
    }
    RpcRevertToSelf();

    // Get the security context from the RPC handle
    dwErr = I_RpcBindingInqSecurityContext(I_RpcGetCurrentCallHandle(),
                                           (void **)&pSecurityContext);
    if (dwErr) {
        DPRINT1 (0, "RpcBindingInqSecurityContext: Error 0x%x\n", dwErr);
        return ScriptProcessError(dwErr);
    }

    // get the keysize
    dwErr = QueryContextAttributesW(pSecurityContext,
                                    SECPKG_ATTR_KEY_INFO,
                                    &KeyInfo);
    if (dwErr) {
        // treat "not supported" as "not secure"
        if (dwErr != SEC_E_UNSUPPORTED_FUNCTION) {
            DPRINT1 (0, "QueryContextAttributesW: Error 0x%x\n", dwErr);
            return ScriptProcessError(dwErr);
        }
        KeySize = 0;
    } else {
        KeySize = KeyInfo.KeySize;
        FreeContextBuffer(KeyInfo.sSignatureAlgorithmName);
        FreeContextBuffer(KeyInfo.sEncryptAlgorithmName);
    }

    // is the key size large enough?
    if (KeySize < DOMAIN_RENAME_KEYSIZE) {
        DPRINT2(0, "Domain Rename: keysize is %d (minimum is %d)\n",
                KeySize, DOMAIN_RENAME_KEYSIZE);
        return ERROR_DS_STRONG_AUTH_REQUIRED;
    }

    DPRINT (0, "DomainRename Verified secure call \n");

    return ERROR_SUCCESS;
}


//
//  IDL_DSAPrepareScript
//
//  Description:
//
//     RPC Entry popint to prepare the script
//
//  Arguments:
//     dwInVersion - requested version of RPC call
//     pmsgIn - input message
//     pdwOutVersion - output version of RPC call
//     pmsgOut - out message
//
//  Return Value:
//
//     0 on success
//
//     ERROR_DS_INVALID_SCRIPT                   when the script does not have the right form (body + hashSig)
//                                       or when the signature is not right
//     ERROR_DS_NTDSCRIPT_PROCESS_ERROR  failed to read from database, execute, set new domain name
//     ERROR_DS_AUTHORIZATION_FAILED     when you are not authorized to modify the script on the 
//                                       partitions contaner (so we don't generate hash/passwd) 
//                                       or when you specified an invalid password         
//     ERROR_ACCESS_DENIED                           when there is an outstanding call in progress (10sec delay)
//     ERROR_INVALID_PARAMETER                   when a needed param is missing
//     ERROR_DS_INTERNAL_FAILURE                 if we failed for some internel reason (thread create fail, etc)
//
ULONG IDL_DSAPrepareScript( 
    IN  RPC_BINDING_HANDLE hRpc,
    IN  DWORD dwInVersion,
    IN  DSA_MSG_PREPARE_SCRIPT_REQ *pmsgIn,
    OUT DWORD *pdwOutVersion,
    OUT DSA_MSG_PREPARE_SCRIPT_REPLY *pmsgOut)
{
    THSTATE          *pTHS;
    DWORD             ret = 0, dwErr;
    ULONG             dwException, ulErrorCode, dsid;
    PVOID             dwEA;
    BOOL              bHasAccess;
    
    WCHAR            *pScript = NULL;
    DWORD             cchScriptLen;

    BYTE             *pbHashBody = NULL, *pbHashSignature = NULL, *pbPassword = NULL;
    DWORD            cbHashBody = 0, cbHashSignature = 0, cbPassword = 0;
    BSTR             bstrScript = NULL;
    WCHAR           *pErrMessage = NULL;

    ScriptLogPrint ( (DSLOG_TRACE, "PrepareScript: Entering\r\n") );

    Sleep (INTRUDER_DELAY);
    
    if (gAnchor.ForestBehaviorVersion < DS_BEHAVIOR_WIN_DOT_NET) {
        
        ScriptLogPrint ( (DSLOG_ERROR, "Wrong Forest Version: %d. Required Version: %d\r\n", gAnchor.ForestBehaviorVersion, DS_BEHAVIOR_WIN_DOT_NET) );
        
        return ERROR_DS_NOT_SUPPORTED;
    }

    if (InterlockedExchange(&glRunningExecuteScriptOperations, 1) != 0) {
        return ERROR_ACCESS_DENIED;
    }

    __try {
        // Sanity check arguments.

        if (    ( NULL == hRpc )
                || ( NULL == pmsgIn )
                || ( NULL == pmsgOut )
                || ( NULL == pdwOutVersion )
                || ( 1 != dwInVersion ) ) {

            ret = ERROR_INVALID_PARAMETER;
            __leave;
        }
        
        *pdwOutVersion = 1;
        memset(pmsgOut, 0, sizeof(*pmsgOut));
        pmsgOut->V1.dwOperationStatus = ERROR_DS_INTERNAL_FAILURE;

        __try {
            if ( !(pTHS = InitTHSTATE(CALLERTYPE_DRA)) ) {
                ret = dwErr = ERROR_DS_INTERNAL_FAILURE;
                __leave;
            }

            Assert(pTHS);
            Assert(VALID_THSTATE(pTHS));

            dwErr = ScriptDomainRenameVerifyCallIsSecure(pTHS);
            if (dwErr) {
                ScriptLogPrint ( (DSLOG_ERROR, "Caller is not in a secure context: 0x%x.\r\n", dwErr) );
                __leave;
            }

            SYNC_TRANS_READ();   // Identify a reader transaction

            __try {

                // check for the right priviledges
                //
                if (dwErr = ScriptHasDomainRenameExecuteScriptRight(pTHS, FALSE, &bHasAccess)) {
                    ScriptLogPrint ( (DSLOG_ERROR, "Cannot verify that caller has execute rights: 0x%x\r\n", dwErr) );
                    bHasAccess = FALSE;
                }

                if (!bHasAccess) {
                    ScriptLogPrint ( (DSLOG_ERROR, "Caller does not have execute rights\r\n") );

                    LogEvent(DS_EVENT_CAT_SECURITY,
                             DS_EVENT_SEV_ALWAYS,
                             DIRLOG_DOMAIN_RENAME_ACCESS_DENIED,
                             szInsertWin32ErrCode(dwErr), 
                             szInsertWin32Msg(dwErr), 
                             NULL);

                    dwErr = ScriptProcessErrorExt(ERROR_DS_AUTHORIZATION_FAILED, dwErr);

                    __leave;
                }

                if (dwErr = ScriptReadFromDatabase (pTHS->pDB, &pScript, &cchScriptLen)) {
                    ScriptLogPrint ( (DSLOG_ERROR, "Error reading script: 0x%x\r\n", dwErr) );

                    DPRINT1 (0, "Error 0x%x reading script\n", dwErr);

                    LogEvent(DS_EVENT_CAT_INTERNAL_PROCESSING,
                             DS_EVENT_SEV_ALWAYS,
                             DIRLOG_DOMAIN_RENAME_READ_SCRIPT_FAILED,
                             szInsertWin32ErrCode(dwErr), 
                             szInsertWin32Msg(dwErr), 
                             NULL);

                    dwErr = ScriptProcessErrorExt(ERROR_DS_NTDSCRIPT_PROCESS_ERROR, dwErr);
                    __leave;
                }

                if (dwErr = ScriptCalculateAndCheckHashKeys (pTHS,
                                                             pScript,
                                                             cchScriptLen,
                                                             &pbHashSignature,
                                                             &cbHashSignature,
                                                             &pbHashBody,
                                                             &cbHashBody
                                                             )) {

                    ScriptLogPrint ( (DSLOG_ERROR, "Error calculating hash: 0x%x\r\n", dwErr) );

                    LogEvent(DS_EVENT_CAT_INTERNAL_PROCESSING,
                             DS_EVENT_SEV_ALWAYS,
                             DIRLOG_DOMAIN_RENAME_INVALID_SIGNATURE,
                             szInsertWin32ErrCode(dwErr), 
                             szInsertWin32Msg(dwErr), 
                             NULL );

                    dwErr = ScriptProcessError(dwErr);

                    __leave;
                }

                if (dwErr = ScriptPrepareForParsing (pTHS, &pScript, &bstrScript)) {

                    ScriptLogPrint ( (DSLOG_ERROR, "Error preparing for parsing: 0x%x\r\n", dwErr) );
                    
                    LogEvent(DS_EVENT_CAT_INTERNAL_PROCESSING,
                             DS_EVENT_SEV_ALWAYS,
                             DIRLOG_DOMAIN_RENAME_CANNOT_PROCEED,
                             szInsertWin32ErrCode(dwErr), 
                             szInsertWin32Msg(dwErr),
                             szInsertHex (DSID(FILENO, __LINE__)));

                    dwErr = ScriptProcessErrorExt(ERROR_DS_NTDSCRIPT_PROCESS_ERROR, dwErr);
                    __leave;
                }
                
                // we are the DSA
                pTHS->fDSA = TRUE;

                if (dwErr = ScriptExecute (pTHS, SCRIPT_OPERATION_PREPROCESS, bstrScript, &pErrMessage, NULL)) {
                    LPSTR szErrStr;
                    szErrStr = THGetErrorString();
                    ScriptLogPrint ( (DSLOG_ERROR, "Error %S preprocessing script\r\n", szErrStr) );

                    LogEvent(DS_EVENT_CAT_INTERNAL_PROCESSING,
                             DS_EVENT_SEV_ALWAYS,
                             DIRLOG_DOMAIN_RENAME_EXECUTE_SCRIPT_FAILED,
                             szInsertSz(szErrStr), 
                             szInsertHex (DSID(FILENO, __LINE__)), NULL);
                    THFreeEx(pTHS, szErrStr);

                    ScriptProcessError(dwErr);
                    __leave;
                }

            } __finally {

                if (bstrScript) {
                    SysFreeString(bstrScript);   
                }

                CLEAN_BEFORE_RETURN( pTHS->errCode );
            }
        }
        __except(GetExceptionData(GetExceptionInformation(), &dwException,
                                  &dwEA, &ulErrorCode, &dsid)) {
            HandleDirExceptions(dwException, ulErrorCode, dsid);
            DPRINT2 (0, "Got an exception while executing script: 0x%x dsid: %x\n", ulErrorCode, dsid);
            ret = dwErr = ScriptException(dwException, ulErrorCode, dsid);
        }

        if (dwErr) {
            __leave;
        }

        __try {

            SYNC_TRANS_WRITE();   // Identify a writer transaction

            __try {

                if (dwErr = ScriptGenerateAndStorePassword (pTHS, 
                                                            &pbPassword, 
                                                            &cbPassword)) {

                    ScriptLogPrint ( (DSLOG_ERROR, "Error generating password: 0x%x\r\n", dwErr) );

                    LogEvent(DS_EVENT_CAT_INTERNAL_PROCESSING,
                             DS_EVENT_SEV_ALWAYS,
                             DIRLOG_DOMAIN_RENAME_CANNOT_PROCEED,
                             szInsertWin32ErrCode(dwErr), 
                             szInsertWin32Msg(dwErr), 
                             szInsertHex (DSID(FILENO, __LINE__)));

                    dwErr = ScriptProcessError(dwErr);

                    __leave;
                }

            } __finally {
                CLEAN_BEFORE_RETURN( pTHS->errCode );
            }

        }
        __except(GetExceptionData(GetExceptionInformation(), &dwException,
                          &dwEA, &ulErrorCode, &dsid)) {
                HandleDirExceptions(dwException, ulErrorCode, dsid);
                DPRINT2 (0, "Got an exception while executing script: 0x%x dsid: %x\n", ulErrorCode, dsid);
                ret = dwErr = ScriptException(dwException, ulErrorCode, dsid);
        }

        if (!dwErr) {
            pmsgOut->V1.cbHashBody = cbHashBody;
            pmsgOut->V1.pbHashBody = pbHashBody;
            pmsgOut->V1.cbHashSignature = cbHashSignature;
            pmsgOut->V1.pbHashSignature = pbHashSignature;
            pmsgOut->V1.pbPassword = pbPassword;
            pmsgOut->V1.cbPassword = cbPassword;
            pmsgOut->V1.pwErrMessage = NULL;
            pErrMessage = NULL;
        }

    }
    __finally {
        Assert (glRunningExecuteScriptOperations == 1);
        glRunningExecuteScriptOperations = 0;
    }

    if (pmsgOut) {
        pmsgOut->V1.dwOperationStatus = dwErr;
        pmsgOut->V1.pwErrMessage = (WCHAR *)pErrMessage;
    }

    ScriptLogPrint ( (DSLOG_TRACE, "PrepareScript: Exiting: 0x%x\r\n", dwErr) );
    
    ScriptLogFlush();
    ScriptLogClose();

    return ret;
}

ULONG
DSA_MSG_EXECUTE_SCRIPT_REQ_V1_Validate(
    DSA_MSG_EXECUTE_SCRIPT_REQ_V1 * pmsg
    )
/*
                                        DWORD   Flags;
    [range(1,MAX_CB_OF_PASSWORD_IN_EXECUTE_SCPT_REQ_V1)]
                                        DWORD   cbPassword;
    [size_is(cbPassword)]               BYTE   *pbPassword;
*/
{
    ULONG ret = ERROR_SUCCESS;

    if ((pmsg->cbPassword > 0) && (pmsg->pbPassword==NULL)) {
        ret = ERROR_INVALID_PARAMETER;
    }

    return ret;
}

ULONG
DSAExecuteScript_InputValidate(
    IN RPC_BINDING_HANDLE hRpc,
    IN DWORD dwInVersion,
    IN DSA_MSG_EXECUTE_SCRIPT_REQ *pmsgIn,
    OUT DWORD *pdwOutVersion,
    OUT DSA_MSG_EXECUTE_SCRIPT_REPLY *pmsgOut
    )
/*
    [in]		        handle_t	                     hRpc,
    [in]            DWORD                          dwInVersion,
    [in]  [ref] [switch_is(dwInVersion)]
                    DSA_MSG_EXECUTE_SCRIPT_REQ *   pmsgIn,
    [out] [ref]     DWORD *                        pdwOutVersion,
    [out] [ref] [switch_is(*pdwOutVersion)]
                    DSA_MSG_EXECUTE_SCRIPT_REPLY * pmsgOut
*/
{
    ULONG ret = ERROR_SUCCESS;

    if ( 1 != dwInVersion ) {
	ret = ERROR_INVALID_PARAMETER;
    }

    if (ret==ERROR_SUCCESS) {
	ret = DSA_MSG_EXECUTE_SCRIPT_REQ_V1_Validate(&(pmsgIn->V1));
    }

    return ret;
}

//
//  IDL_DSAExecuteScript
//
//  Description:
//
//     RPC Entry popint to execute the script
//
//  Arguments:
//     dwInVersion - requested version of RPC call
//     pmsgIn - input message
//     pdwOutVersion - output version of RPC call
//     pmsgOut - out message
//
//  Return Value:
//
//     0 on success
//
//     ERROR_DS_INVALID_SCRIPT                   when the script does not have the right form (body + hashSig)
//                                       or when the signature is not right
//     ERROR_DS_NTDSCRIPT_PROCESS_ERROR  failed to read from database, execute, set new domain name
//     ERROR_DS_AUTHORIZATION_FAILED     when you are not authorized to modify the script on the 
//                                       partitions contaner (so we don't generate hash/passwd) 
//                                       or when you specified an invalid password         
//     ERROR_ACCESS_DENIED                           when there is an outstanding call in progress (10sec delay)
//     ERROR_INVALID_PARAMETER                   when a needed param is missing
//     ERROR_DS_INTERNAL_FAILURE                 if we failed for some internel reason (thread create fail, etc)
//
ULONG IDL_DSAExecuteScript( 
    IN RPC_BINDING_HANDLE hRpc,
    IN DWORD dwInVersion,
    IN DSA_MSG_EXECUTE_SCRIPT_REQ *pmsgIn,
    OUT DWORD *pdwOutVersion,
    OUT DSA_MSG_EXECUTE_SCRIPT_REPLY *pmsgOut)
{
    THSTATE          *pTHS;
    DWORD             dwErr=0, ret=0;
    ULONG             dwException, ulErrorCode, dsid;
    PVOID             dwEA;
    WCHAR            *pScript = NULL;
    DWORD             cchScript;
    BOOL              bIsDomainAdmin = FALSE;
    BOOL              bHasAccess;

    BYTE             *pbHashBody = NULL, *pbHashSignature = NULL;
    DWORD            cbHashBody = 0, cbHashSignature = 0;
    BSTR             bstrScript = NULL;
    WCHAR           *pErrMessage = NULL;

    ScriptLogPrint ( (DSLOG_TRACE, "ExecuteScript: Entering\r\n") );

    Sleep (INTRUDER_DELAY);
    
    if (gAnchor.ForestBehaviorVersion < DS_BEHAVIOR_WIN_DOT_NET) {
        
        ScriptLogPrint ( (DSLOG_ERROR, "Wrong Forest Version: %d. Required Version: %d\r\n", gAnchor.ForestBehaviorVersion, DS_BEHAVIOR_WIN_DOT_NET) );
        
        return ERROR_DS_NOT_SUPPORTED;
    }
    
    if (InterlockedExchange(&glRunningExecuteScriptOperations, 1) != 0) {
        return ERROR_ACCESS_DENIED;
    }

    __try {

        // Sanity check arguments.

        if (    ( NULL == hRpc )
                || ( NULL == pmsgIn )
                || ( NULL == pmsgOut )
                || ( NULL == pdwOutVersion )
                || ( 1 != dwInVersion ) ) {

            ret = ERROR_INVALID_PARAMETER;
            __leave;
        }
        *pdwOutVersion = 1;
        memset(pmsgOut, 0, sizeof(*pmsgOut));
        pmsgOut->V1.dwOperationStatus = ERROR_DS_INTERNAL_FAILURE;

        
        __try {
            if ( !(pTHS = InitTHSTATE(CALLERTYPE_DRA)) ) {
                ret = ERROR_DS_INTERNAL_FAILURE;
                __leave;
            }

            Assert(pTHS);
            Assert(VALID_THSTATE(pTHS));

            if ((ret = DSAExecuteScript_InputValidate(hRpc, 
						dwInVersion, 
						pmsgIn, 
						pdwOutVersion, 
						pmsgOut))!=ERROR_SUCCESS) {
                Assert(!"RPC Server input validation error, contact Dsrepl");
                __leave;
            }

            SYNC_TRANS_READ();  // identify a reader transaction

            __try {
                // check for the right priviledges
                //
                if (dwErr = ScriptHasDomainRenameExecuteScriptPassword(pTHS, 
                                                                 pmsgIn->V1.cbPassword,
                                                                 pmsgIn->V1.pbPassword, 
                                                                 &bHasAccess)) {
                    ScriptLogPrint ( (DSLOG_ERROR, "Cannot verify caller's password: 0x%x\r\n", dwErr) );

                    bHasAccess = FALSE;
                }
                else if (!bHasAccess) {
                    ScriptLogPrint ( (DSLOG_ERROR, "Caller does not have the right password\r\n") );
                    dwErr = ERROR_DS_AUTHORIZATION_FAILED;
                }

                if (bHasAccess) {
                    // do the AUDIT_ONLY access check to generate (a sort of) audit of the execute
                    // operation. Note that we will not have the userID recorded since this is an
                    // unauthenticated RPC call.
                    if (dwErr = ScriptHasDomainRenameExecuteScriptRight(pTHS, TRUE, &bHasAccess)) {
                        ScriptLogPrint ( (DSLOG_ERROR, "Cannot audit execute operation: 0x%x\r\n", dwErr) );
                        bHasAccess = FALSE;
                    }
                }
                
                if (!bHasAccess) {
                    LogEvent(DS_EVENT_CAT_SECURITY,
                             DS_EVENT_SEV_ALWAYS,
                             DIRLOG_DOMAIN_RENAME_ACCESS_DENIED,
                             szInsertWin32ErrCode(dwErr), 
                             szInsertWin32Msg(dwErr), 
                             NULL);

                    dwErr = ERROR_DS_AUTHORIZATION_FAILED;

                    __leave;
                } 
            
                // read script
                if (dwErr = ScriptReadFromDatabase (pTHS->pDB, &pScript, &cchScript)) {
                    ScriptLogPrint ( (DSLOG_ERROR, "Error reading script: 0x%x\r\n", dwErr) );
                    
                    DPRINT1 (0, "Error 0x%x reading script\n", dwErr);
                    
                    LogEvent(DS_EVENT_CAT_INTERNAL_PROCESSING,
                             DS_EVENT_SEV_ALWAYS,
                             DIRLOG_DOMAIN_RENAME_READ_SCRIPT_FAILED,
                             szInsertWin32ErrCode(dwErr), 
                             szInsertWin32Msg(dwErr), 
                             NULL);

                    dwErr = ERROR_DS_NTDSCRIPT_PROCESS_ERROR;
                    __leave;
                }

                // calculate hash keys
                if (dwErr = ScriptCalculateAndCheckHashKeys (pTHS,
                                                             pScript,
                                                             cchScript,
                                                             &pbHashSignature,
                                                             &cbHashSignature,
                                                             &pbHashBody,
                                                             &cbHashBody
                                                             )) {

                    ScriptLogPrint ( (DSLOG_ERROR, "Error calculating hash: 0x%x\r\n", dwErr) );

                    LogEvent(DS_EVENT_CAT_INTERNAL_PROCESSING,
                             DS_EVENT_SEV_ALWAYS,
                             DIRLOG_DOMAIN_RENAME_INVALID_SIGNATURE,
                             szInsertWin32ErrCode(dwErr), 
                             szInsertWin32Msg(dwErr),
                             NULL);

                    dwErr = ScriptProcessError(dwErr);

                    __leave;
                }

                if (dwErr = ScriptPrepareForParsing (pTHS, &pScript, &bstrScript)) {
                    
                    ScriptLogPrint ( (DSLOG_ERROR, "Error preparing for parsing: 0x%x\r\n", dwErr) );

                    LogEvent(DS_EVENT_CAT_INTERNAL_PROCESSING,
                             DS_EVENT_SEV_ALWAYS,
                             DIRLOG_DOMAIN_RENAME_CANNOT_PROCEED,
                             szInsertWin32ErrCode(dwErr), 
                             szInsertWin32Msg(dwErr),
                             szInsertHex (DSID(FILENO, __LINE__)));

                    dwErr = ScriptParseErrorExt(ERROR_DS_NTDSCRIPT_PROCESS_ERROR, dwErr);
                    __leave;
                }
                
                // we are the DSA
                pTHS->fDSA = TRUE;

                // execute the preprocessing part of the script
                if (dwErr = ScriptExecute (pTHS, SCRIPT_OPERATION_PREPROCESS, bstrScript, &pErrMessage, NULL)) {
                    LPSTR szErrStr;
                    szErrStr = THGetErrorString();
                    ScriptLogPrint ( (DSLOG_ERROR, "Error %S preprocessing script\r\n", szErrStr) );

                    LogEvent(DS_EVENT_CAT_INTERNAL_PROCESSING,
                             DS_EVENT_SEV_ALWAYS,
                             DIRLOG_DOMAIN_RENAME_EXECUTE_SCRIPT_FAILED,
                             szInsertSz(szErrStr), 
                             szInsertHex (DSID(FILENO, __LINE__)), NULL);
                    THFreeEx(pTHS, szErrStr);

                    ScriptProcessError(dwErr);
                    __leave;
                }


            } __finally {
                CLEAN_BEFORE_RETURN( pTHS->errCode || dwErr);
            }

            if (dwErr || pTHS->errCode ) {
                __leave;
            }


            SYNC_TRANS_WRITE();  // identify a writer transaction

            __try {

                // continue with execution of the script.


                // we are executing the script, so go into single user mode
                //
                if (!DsaSetSingleUserMode()) {
                    DPRINT (0, "Failed to go into single user mode\n");
                    dwErr = ERROR_DS_SINGLE_USER_MODE_FAILED;

                    LogEvent(DS_EVENT_CAT_INTERNAL_PROCESSING,
                             DS_EVENT_SEV_ALWAYS,
                             DIRLOG_DOMAIN_RENAME_CANNOT_PROCEED,
                             szInsertWin32ErrCode(dwErr), 
                             szInsertWin32Msg(dwErr), 
                             szInsertHex (DSID(FILENO, __LINE__)));

                    _leave;
                }


                if (dwErr = ScriptExecute (pTHS, SCRIPT_OPERATION_EXECUTE, bstrScript, &pErrMessage, NULL)) {
                    LPSTR szErrStr;
                    szErrStr = THGetErrorString();
                    ScriptLogPrint ( (DSLOG_ERROR, "Error %S execuring script\r\n", szErrStr) );
                    DPRINT1 (0, "Error %s executing script\n", szErrStr);

                    LogEvent(DS_EVENT_CAT_INTERNAL_PROCESSING,
                             DS_EVENT_SEV_ALWAYS,
                             DIRLOG_DOMAIN_RENAME_EXECUTE_SCRIPT_FAILED,
                             szInsertSz(szErrStr), 
                             szInsertHex (DSID(FILENO, __LINE__)), NULL);
                    THFreeEx(pTHS, szErrStr);

                    ScriptProcessError(dwErr);
                    __leave;
                }

            } __finally {
                CLEAN_BEFORE_RETURN( dwErr || pTHS->errCode );
            }

            // get the error if we missed it
            if ((dwErr == 0) && (pTHS->errCode!=0)) {
                dwErr = pTHS->errCode;
            }
        }
        __except(GetExceptionData(GetExceptionInformation(), &dwException,
                                  &dwEA, &ulErrorCode, &dsid)) {
            HandleDirExceptions(dwException, ulErrorCode, dsid);
            DPRINT2 (0, "Got an exception while executing script: 0x%x dsid: %x\n", ulErrorCode, dsid);
            ret = dwErr = ScriptException(dwException, ulErrorCode, dsid);
        }

        if (bstrScript) {
            SysFreeString(bstrScript);   
        }
        
        // log an event regarding the status of the script execution 
        //
        LogEvent(DS_EVENT_CAT_INTERNAL_PROCESSING,
                 DS_EVENT_SEV_MINIMAL,
                 DIRLOG_SCRIPT_EXECUTE_STATUS,
                 szInsertWin32ErrCode(dwErr), 
                 szInsertWin32Msg(dwErr),
                 NULL);

        // shutdown computer
        // if we executed the script and we had no error or we are in singleuser mode
        //
        if (dwErr==0 || DsaIsSingleUserMode()) {
            ScriptExecuteDSShutdown ();
        }

        if (!dwErr) {
            pErrMessage = NULL;
        }

    }
    __finally {
        Assert (glRunningExecuteScriptOperations == 1);
        glRunningExecuteScriptOperations = 0;
    }

    if (pmsgOut) {
        pmsgOut->V1.dwOperationStatus = dwErr;
        pmsgOut->V1.pwErrMessage = (WCHAR *)pErrMessage;
    }

    ScriptLogPrint ( (DSLOG_TRACE, "ExecuteScript: Exiting: 0x%x\r\n", dwErr) );
    ScriptLogFlush();
    ScriptLogClose();

    return ret;
}

//
//  GeneralScriptExecute
//
//  Description:
//
//     Execute the given XML script
//
//  Arguments:
//
//      Script - the script to execute. 
//
//  Return Value:
//
//     0 on success
//     error on failure
// 
//
DWORD GeneralScriptExecute (THSTATE *pTHS, WCHAR * Script )
{
    HRESULT             hr = S_OK;
    DWORD               err = 0;
    DWORD               retCode;
    ISAXXMLReader *     pReader = NULL;
    NTDSContent*        pHandler = NULL; 
    IClassFactory *     pFactory = NULL;
    BSTR                pBstrScript = NULL;

    VARIANT             varScript;
    const WCHAR         *pErrMessage = NULL;

    pBstrScript = SysAllocString( Script );
    if (!pBstrScript) {
        pTHS->errCode = ERROR_OUTOFMEMORY;
        return pTHS->errCode;
    }

    try {

        // create a variant for the value passed to the Parser
        VariantInit(&varScript);
        varScript.vt = VT_BYREF|VT_BSTR;
        varScript.pbstrVal = &pBstrScript; 
        
        // do the COM creation of the SAMXMLReader manually
        GetClassFactory( CLSID_SAXXMLReader, &pFactory);

        hr = pFactory->CreateInstance( NULL, __uuidof(ISAXXMLReader), (void**)&pReader);

        if(!FAILED(hr)) 
        {
            pHandler = new NTDSContent();
            hr = pReader->putContentHandler(pHandler);

            SAXErrorHandlerImpl * pEc = new SAXErrorHandlerImpl();
            hr = pReader->putErrorHandler(pEc);

            hr = pReader->parse(varScript);
            ScriptLogPrint ( (DSLOG_TRACE,  "XML Parse result code: 0x%08x\r\n",hr) );

            if(FAILED(hr)) {
                err = ScriptParseError(hr);
            }
            else {
                err = handlerScriptExecute(pHandler, SCRIPT_PROCESS_VALIDATE_SYNTAX_PASS, retCode, &pErrMessage);

                ScriptLogPrint ( (DSLOG_TRACE,  "Syntax Validate Processing: 0x%08X retCode:%d(0x%x) ErrorMessage(%ws)\r\n", err, retCode, retCode, pErrMessage) );

                if (!err && !retCode) {
                     err = handlerScriptExecute(pHandler, SCRIPT_PROCESS_EXECUTE_PASS, retCode, &pErrMessage);
                    ScriptLogPrint ( (DSLOG_TRACE,  "Execute Processing(RW): 0x%08X retCode:%d(0x%x) ErrorMessage(%ws)\r\n", err, retCode, retCode, pErrMessage) );
                }

                // set error accordingly to fail transaction
                // err means that we had an error parsing / executing
                if (err) {
                    pTHS->errCode = err;
                }
                // retcode means that something was not as expected in the data
                // stored in the DS
                else if (retCode) {
                    err = pTHS->errCode = retCode;
                }
            }
        }
        else 
        {
            err = ScriptParseError(hr);
            ScriptLogPrint ( (DSLOG_ERROR,  "Error Parsing XML: 0x%08x\r\n",hr) );
        }
    }
    catch (...) {
        err = ScriptParseError(ERROR_EXCEPTION_IN_SERVICE);
    }

    if (pReader) {
        pReader->Release();
    }

    if (pHandler) {
        delete pHandler;
    }

    if (pFactory) {
        pFactory->Release();
    }

    SysFreeString(pBstrScript);
     
    return err;
}


#ifdef DBG

ULONG
ExecuteScriptLDAP (
        OPARG *pOpArg,
        OPRES *pOpRes
        )
{
    THSTATE          *pTHS = pTHStls;
    DWORD             dwErr;
    ULONG             dwException, ulErrorCode, dsid;
    PVOID             dwEA;
    WCHAR            *pwScript;
    BOOL              fPrivilegeHeld = FALSE;

    // debug privilege is required to run arbitrary scripts using DBG binaries
    if ((dwErr = CheckPrivilegeAnyClient(SE_DEBUG_PRIVILEGE, &fPrivilegeHeld)) != 0 || !fPrivilegeHeld) {
        SetSecErrorEx(SE_PROBLEM_INSUFF_ACCESS_RIGHTS, ERROR_PRIVILEGE_NOT_HELD, dwErr);
        return ERROR_PRIVILEGE_NOT_HELD;
    }

    ScriptLogPrint ( (DSLOG_TRACE, "ExecuteScriptLDAP: Entering\r\n") );

    __try {
        Assert(pTHS);
        Assert(VALID_THSTATE(pTHS));

        SYNC_TRANS_WRITE();  // identify a writer transaction

        __try {

            ScriptLogPrint ( (DSLOG_TRACE, "Script: %s\r\n", pOpArg->pBuf) );

            pwScript = UnicodeStringFromString8 (CP_UTF8, pOpArg->pBuf, pOpArg->cbBuf);

            dwErr = GeneralScriptExecute (pTHS, pwScript);

        } __finally {
            CLEAN_BEFORE_RETURN( dwErr || pTHS->errCode );
        }

        // get the error if we missed it
        if ((dwErr == 0) && (pTHS->errCode!=0)) {
            dwErr = pTHS->errCode;
        }
    }
    __except(GetExceptionData(GetExceptionInformation(), &dwException,
                              &dwEA, &ulErrorCode, &dsid)) {
        HandleDirExceptions(dwException, ulErrorCode, dsid);
        DPRINT2 (0, "Got an exception while executing script: 0x%x dsid: %x\n", ulErrorCode, dsid);
        dwErr = ScriptException(dwException, ulErrorCode, dsid);
    }

    pOpRes->ulExtendedRet = dwErr;

    return dwErr;
}

#endif

LONG gScriptOptype = SCRIPT_OPTYPE_NOT_CACHED;

DWORD
ScriptGetOptype(THSTATE* pTHS, ScriptOperationType* pScriptOptype, BOOL fDontUseCache)
{
    DBPOS *pDB;
    DWORD dwErr = 0;
    WCHAR *pScript = NULL;
    DWORD cchScriptLen;

    BYTE  *pbHashBody = NULL, *pbHashSignature = NULL, *pbPassword = NULL;
    DWORD cbHashBody = 0, cbHashSignature = 0, cbPassword = 0;
    BSTR  bstrScript = NULL;
    WCHAR *pErrMessage = NULL;

    ULONG dwException, ulErrorCode, dsid;
    PVOID dwEA;
    
    NESTED_TRANSACTIONAL_DATA* pData;
    
    Assert(pScriptOptype);

    // figure out if we changed script optype somewhere in this transaction
    for (pData = pTHS->JetCache.dataPtr; pData != NULL; pData = pData->pOuter) {
        if (pData->objCachingInfo.fUpdateScriptChanged) {
            fDontUseCache = TRUE;
            break;
        }
    }
    if (!fDontUseCache) {
        // We did not change the script in this transaction, so we can use
        // the cached value.
        *pScriptOptype = (ScriptOperationType)gScriptOptype;
        if (*pScriptOptype != SCRIPT_OPTYPE_NOT_CACHED && *pScriptOptype != SCRIPT_OPTYPE_UNDEFINED) {
            // We had a valid value cached. Return it.
            // Note: the cached value MAY be inconsistent with our transactional DB view! 
            // This will occur if we started our transaction long ago, and since then, the script 
            // has changed, and the async task has recomputed the value. However, this cached value 
            // reflects the most recent value commited to the DB, and this is what is important to us.
            return 0;
        }
    }

    // OK, we need to compute the value
    __try {
        DBOpen(&pDB);
        __try {
            // assume by default there is no script.
            // This will be the return value if we
            *pScriptOptype = SCRIPT_OPTYPE_NOSCRIPT;

            if (ScriptReadFromDatabase (pDB, &pScript, &cchScriptLen) ||        // 1. Read script. May return an error if the
                                                                                //    script is not present.
                ScriptCalculateAndCheckHashKeys (pTHS,                          // 2. Check signature. If error is returned,
                                                 pScript,                       //    i.e. signature does not match, then
                                                 cchScriptLen,                  //    we assume there is no (valid) script.
                                                 &pbHashSignature,
                                                 &cbHashSignature,
                                                 &pbHashBody,
                                                 &cbHashBody
                                                 ) ||
                (dwErr = ScriptPrepareForParsing (pTHS, &pScript, &bstrScript)) ||  // 3. Prepare for parsing, can return out_of_memory
                ScriptExecute (pTHS,                                            // 4. Get the optype. If the error is returned,
                               SCRIPT_OPERATION_GET_OPTYPE,                     //    then the script is invalid, which means
                               bstrScript,                                      //    we will keep the value as NO_SCRIPT.
                               &pErrMessage, 
                               pScriptOptype)) 
            {
                __leave;
            }
        }
        __finally {
            if (pScript) {
                THFreeEx(pTHS, pScript);
            }
            if (pbHashSignature) {
                THFreeEx(pTHS, pbHashSignature);
            }
            if (pbHashBody) {
                THFreeEx(pTHS, pbHashBody);
            }

            if (bstrScript) {
                SysFreeString(bstrScript);   
            }
            DBClose(pDB, !AbnormalTermination() && !dwErr);
        }
    }
    __except(GetExceptionData(GetExceptionInformation(), &dwException,
                                  &dwEA, &ulErrorCode, &dsid)) {
        HandleDirExceptions(dwException, ulErrorCode, dsid);
        DPRINT2 (0, "Got an exception while executing script: 0x%x dsid: %x\n", ulErrorCode, dsid);
        dwErr = ScriptException(dwException, ulErrorCode, dsid);
    }

    if (dwErr == 0 && !fDontUseCache) {
        // Ok, we determined script optype without throwing an exception. Thus, we can trust 
        // the computed value (even if it is SCRIPT_OPTYPE_NO_SCRIPT). If the cached value 
        // is still SCRIPT_OPTYPE_NOT_CACHED, then we should cache the value we read.
        // Note: if the value is SCRIPT_OPTYPE_UNDEFINED, then there was a script-modifying 
        // operation recently, and we can not be sure our value is the correct one. Leave
        // the task of updating the cached value to TQ_CacheScriptOptype.
        InterlockedCompareExchange(&gScriptOptype, *pScriptOptype, SCRIPT_OPTYPE_NOT_CACHED);
        // If the value was not SCRIPT_OPTYPE_UNDEFINED, then some other thread might have beat 
        // us here and already written the value. It is possible that this value differs from 
        // the one we just computed, because we were using our own transactional DB view, which 
        // might be quite old. This is just fine, we will use the value we computed.
    }

    return dwErr;
}

extern "C"
void
CacheScriptOptype(void * pv,
                  void ** ppvNext,
                  DWORD * pcSecsUntilNextIteration )
{
    // This routine is invoked whenever script has changed. The current value
    // of gScriptOptype was set to SCRIPT_OPTYPE_UNDEFINED to reflect that
    // the current cached value is unsafe to use.
    ScriptOperationType scriptOptype;
    DWORD dwErr;
    THSTATE* pTHS = pTHStls;

    // Get the current script optype. Don't use the cached value, make sure we go to the
    // database to compute a fresh one.
    dwErr = ScriptGetOptype(pTHS, &scriptOptype, TRUE);
    if (dwErr == 0) {
        // success! We got something. Update the cached value.
        gScriptOptype = scriptOptype;

        StartOrStopDsaOpRPC(scriptOptype != SCRIPT_OPTYPE_NOSCRIPT);
    }
    else {
        // there was an exception computing script optype. Retry the operation in 1 minute.
        *pcSecsUntilNextIteration = 60;
    }
}

extern "C"
DWORD 
VerifyDomainRenameNotInProgress(THSTATE* pTHS)
/*++
Description:
    Checks whether a domain rename operation is in progress.
    
Arguments:
    pTHS -- (IN) THSTATE. pTHS->pDB currency is not disturbed.
    
Return value:
    0 if rename domain is not in progress
    ERROR_DS_DOMAIN_RENAME_IN_PROGRESS if it is.
    Other misc errors may be returned
    
++*/
{
    DWORD  dwErr;
    ScriptOperationType scriptOptype;

    Assert(pTHS);
    // This check should not be invoked in DRA context, or in SingleUserMode context
    Assert(!pTHS->fDRA && !pTHS->fSingleUserModeThread);

    if (gAnchor.ForestBehaviorVersion < DS_BEHAVIOR_WIN_DOT_NET) {
        // FBV must be >= 2 in order to do domain renames
        return 0;
    }

    if (DsaIsInstalling()) {
        // no check for DCPROMO operation
        return 0;
    }

    dwErr = ScriptGetOptype(pTHS, &scriptOptype, FALSE);
    if (dwErr == 0 && scriptOptype == SCRIPT_OPTYPE_DOMAINRENAME) {
        dwErr = ERROR_DS_DOMAIN_RENAME_IN_PROGRESS;
    }

    return dwErr;
}

VOID ScriptResetOptype() {
    gScriptOptype = SCRIPT_OPTYPE_UNDEFINED;
}

