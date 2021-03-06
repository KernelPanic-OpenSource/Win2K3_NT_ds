// This is a part of the Microsoft Management Console.
// Copyright (C) Microsoft Corporation, 1995 - 1999
// All rights reserved.
//
// This source code is only intended as a supplement to the
// Microsoft Management Console and related
// electronic documentation provided with the interfaces.

#include "stdafx.h"
#include "resource.h"
#include "initguid.h"

#include "misc.h"

#include <shlguid.h>
#include <shlobj.h>

#define __dwFILE__	__dwFILE_CERTMMC_SNAPIN_CPP__

#define WSZCERTMMC_DLL "certmmc.dll"

CComModule _Module;
HINSTANCE g_hInstance = NULL;
HMODULE g_hmodRichEdit = NULL;

BEGIN_OBJECT_MAP(ObjectMap)
    OBJECT_ENTRY(CLSID_Snapin, CComponentDataPrimaryImpl)
    OBJECT_ENTRY(CLSID_About, CSnapinAboutImpl)
END_OBJECT_MAP()

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


void CreateRegEntries();
void RemoveRegEntries();

void CreateProgramGroupLink();
void RemoveProgramGroupLink();


// #define CERTMMC_DEBUG_REGSVR

BOOL WINAPI
DllMain(  
    HINSTANCE hinstDLL,  // handle to DLL module
    DWORD dwReason,     // reason for calling function
    LPVOID /* lpvReserved */ )
{
    switch (dwReason)
    {
    case DLL_PROCESS_ATTACH:
    {
        g_hInstance = hinstDLL;
        _Module.Init(ObjectMap, hinstDLL);

        DisableThreadLibraryCalls(hinstDLL);
	LogOpen(FALSE);
        break;
    }
    case DLL_PROCESS_DETACH:
    {
        // last call process should do this

        if (NULL != g_hmodRichEdit)
	{
            FreeLibrary(g_hmodRichEdit);
        }
        myFreeColumnDisplayNames();   
	myFreeResourceStrings("certmmc.dll");
	delete g_pResources;
        _Module.Term();
	myRegisterMemDump();
        LogClose();

        DEBUG_VERIFY_INSTANCE_COUNT(CSnapin);
        DEBUG_VERIFY_INSTANCE_COUNT(CComponentDataImpl);
        break;
    }

    default:
        break;
    }
    
    return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
// Used to determine whether the DLL can be unloaded by OLE

STDAPI DllCanUnloadNow(void)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return (_Module.GetLockCount() == 0) ? S_OK : S_FALSE;
}

/////////////////////////////////////////////////////////////////////////////
// Returns a class factory to create an object of the requested type

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    return _Module.GetClassObject(rclsid, riid, ppv);
}

/////////////////////////////////////////////////////////////////////////////
// DllRegisterServer - Adds entries to the system registry

STDAPI DllRegisterServer(void)
{
    CreateRegEntries();
    CreateProgramGroupLink();

    // registers object, typelib and all interfaces in typelib
    return _Module.RegisterServer(FALSE);
}

/////////////////////////////////////////////////////////////////////////////
// DllUnregisterServer - Removes entries from the system registry

STDAPI DllUnregisterServer(void)
{
    _Module.UnregisterServer();

    RemoveRegEntries();
    RemoveProgramGroupLink();

    return S_OK;
}


/////////////////////////////////////////////////////////////////////////////
// Register/UnRegister nodetypes, etc

typedef struct _REGVALUE
{
    DWORD        dwFlags;
    WCHAR const *pwszKeyName;     // NULL implies place value under CA name key
    WCHAR const *pwszValueName;
    WCHAR const *pwszValueString; // NULL implies use REG_DWORD value (dwValue)
    DWORD        dwValue;
} REGVALUE;

// Flags
#define CERTMMC_REG_DELKEY 1    // delete this key on removal

// Values Under "HKLM" from base to leaves
REGVALUE g_arvCA[] =
{
  // main snapin uuid
#define IREG_SNAPINNAME		0
  { CERTMMC_REG_DELKEY, wszREGKEYMGMTSNAPINUUID1, NULL, NULL, 0},
#define IREG_SNAPINNAMESTRING	1
  { 0,                  wszREGKEYMGMTSNAPINUUID1, wszSNAPINNAMESTRING, NULL, 0},
#define IREG_SNAPINNAMESTRINGINDIRECT	2
  { 0,                  wszREGKEYMGMTSNAPINUUID1, wszSNAPINNAMESTRINGINDIRECT, NULL, 0},

  { 0,                  wszREGKEYMGMTSNAPINUUID1, wszSNAPINABOUT, wszSNAPINNODETYPE_ABOUT, 0},
  { CERTMMC_REG_DELKEY, wszREGKEYMGMTSNAPINUUID1_STANDALONE, NULL, NULL, 0}
  ,
  { CERTMMC_REG_DELKEY, wszREGKEYMGMTSNAPINUUID1_NODETYPES, NULL, NULL, 0},
  { CERTMMC_REG_DELKEY, wszREGKEYMGMTSNAPINUUID1_NODETYPES_1, NULL, NULL, 0},
  { CERTMMC_REG_DELKEY, wszREGKEYMGMTSNAPINUUID1_NODETYPES_2, NULL, NULL, 0},
  { CERTMMC_REG_DELKEY, wszREGKEYMGMTSNAPINUUID1_NODETYPES_3, NULL, NULL, 0},
  { CERTMMC_REG_DELKEY, wszREGKEYMGMTSNAPINUUID1_NODETYPES_4, NULL, NULL, 0},
  { CERTMMC_REG_DELKEY, wszREGKEYMGMTSNAPINUUID1_NODETYPES_5, NULL, NULL, 0},

  // register each snapin nodetype
  { CERTMMC_REG_DELKEY, wszREGKEYMGMTSNAPIN_NODETYPES_1, NULL, wszREGCERTSNAPIN_NODETYPES_1, 0},
  { CERTMMC_REG_DELKEY, wszREGKEYMGMTSNAPIN_NODETYPES_2, NULL, wszREGCERTSNAPIN_NODETYPES_2, 0},
  { CERTMMC_REG_DELKEY, wszREGKEYMGMTSNAPIN_NODETYPES_3, NULL, wszREGCERTSNAPIN_NODETYPES_3, 0},
  { CERTMMC_REG_DELKEY, wszREGKEYMGMTSNAPIN_NODETYPES_4, NULL, wszREGCERTSNAPIN_NODETYPES_4, 0},
  { CERTMMC_REG_DELKEY, wszREGKEYMGMTSNAPIN_NODETYPES_5, NULL, wszREGCERTSNAPIN_NODETYPES_5, 0},

  { 0,                  NULL, NULL, NULL, 0 }
};


HRESULT
InitRegEntries(
    OPTIONAL IN OUT CString *pcstrName,
    OPTIONAL IN OUT CString *pcstrNameString,
    OPTIONAL IN OUT CString *pcstrNameStringIndirect)
{
    HRESULT hr = S_OK;
    WCHAR const *pwsz;

    pwsz = NULL;
    if (NULL != pcstrName)
    {
	pcstrName->LoadString(IDS_CERTMMC_SNAPINNAME);
	if (pcstrName->IsEmpty())
	{
	    hr = myHLastError();
	    _PrintError(hr, "LoadString");
	}
	else
	{
	    pwsz = (LPCWSTR) *pcstrName;
	}
    }
    g_arvCA[IREG_SNAPINNAME].pwszValueString = pwsz;

    pwsz = NULL;
    if (NULL != pcstrNameString)
    {
	pcstrNameString->LoadString(IDS_CERTMMC_SNAPINNAMESTRING);
	if (pcstrNameString->IsEmpty())
	{
	    hr = myHLastError();
	    _PrintError(hr, "LoadString");
	}
	else
	{
	    pwsz = (LPCWSTR) *pcstrNameString;
	}
    }
    g_arvCA[IREG_SNAPINNAMESTRING].pwszValueString = pwsz;


    pwsz = NULL;
    if (NULL != pcstrNameStringIndirect)
    {
	pcstrNameStringIndirect->Format(wszSNAPINNAMESTRINGINDIRECT_TEMPLATE, L"CertMMC.dll", IDS_CERTMMC_SNAPINNAMESTRING);
	if (pcstrNameStringIndirect->IsEmpty())
	{
	    hr = myHLastError();
	    _PrintError(hr, "LoadString");
	}
	else
	{
	    pwsz = (LPCWSTR) *pcstrNameStringIndirect;
	}
    }
    g_arvCA[IREG_SNAPINNAMESTRINGINDIRECT].pwszValueString = pwsz;


//error:
    return(hr);
}


void CreateRegEntries()
{
    DWORD err;
    HKEY hKeyThisValue = NULL;
    REGVALUE const *prv;
    CString cstrName;
    CString cstrNameString;
    CString cstrNameStringIndirect;

    InitRegEntries(&cstrName, &cstrNameString, &cstrNameStringIndirect);

    // run until not creating key or value
    for (   prv=g_arvCA; 
            !(NULL == prv->pwszValueName && NULL == prv->pwszKeyName);
            prv++ )
    {
        DWORD dwDisposition;
        ASSERT(NULL != prv->pwszKeyName);
        if (NULL == prv->pwszKeyName)
             continue;

#ifdef CERTMMC_DEBUG_REGSVR
            CString cstr;
            cstr.Format(L"RegCreateKeyEx: %ws\n", prv->pwszKeyName);
            OutputDebugString((LPCWSTR)cstr);
#endif

        err = RegCreateKeyEx(HKEY_LOCAL_MACHINE,
             prv->pwszKeyName,
             0,
             NULL,
             REG_OPTION_NON_VOLATILE,
             KEY_ALL_ACCESS,
             NULL,
             &hKeyThisValue,
             &dwDisposition);
        if (err != ERROR_SUCCESS)
            goto error;
        

        // for now, don't set any value if unnamed, unvalued string
        // UNDONE: can't set unnamed dword!

        if (NULL != prv->pwszValueName || NULL != prv->pwszValueString)
        {
            if (NULL != prv->pwszValueString)
            {
#ifdef CERTMMC_DEBUG_REGSVR
            CString cstr;
            cstr.Format(L"RegSetValueEx: %ws : %ws\n", prv->pwszValueName, prv->pwszValueString);
            OutputDebugString((LPCWSTR)cstr);
#endif
                err = RegSetValueEx(
                        hKeyThisValue,
                        prv->pwszValueName,
                        0,
                        REG_SZ,
                        (const BYTE *) prv->pwszValueString,
                        WSZ_BYTECOUNT(prv->pwszValueString));
            }
            else
            {
#ifdef CERTMMC_DEBUG_REGSVR
            CString cstr;
            cstr.Format(L"RegSetValueEx: %ws : %ul\n", prv->pwszValueName, prv->dwValue);
            OutputDebugString((LPCWSTR)cstr);
#endif
                err = RegSetValueEx(
                        hKeyThisValue,
                        prv->pwszValueName,
                        0,
                        REG_DWORD,
                        (const BYTE *) &prv->dwValue,
                        sizeof(prv->dwValue));

            }
            if (err != ERROR_SUCCESS)
                goto error;
        }

        if (NULL != hKeyThisValue)
        {
            RegCloseKey(hKeyThisValue);
            hKeyThisValue = NULL;
        }
    }

error:            
    if (hKeyThisValue)
        RegCloseKey(hKeyThisValue);

    InitRegEntries(NULL, NULL, NULL);
    return;
}

void RemoveRegEntries()
{
    REGVALUE const *prv;

    // walk backwards through array until hit array start
    for (   prv= (&g_arvCA[ARRAYLEN(g_arvCA)]) - 2;     // goto zero-based end AND skip {NULL}
            prv >= g_arvCA;                             // until we walk past beginning
            prv-- )                                     // walk backwards
    {
        if (prv->dwFlags & CERTMMC_REG_DELKEY) 
        {
            ASSERT(prv->pwszKeyName != NULL);
#ifdef CERTMMC_DEBUG_REGSVR
            CString cstr;
            cstr.Format(L"RegDeleteKey: %ws\n", prv->pwszKeyName);
            OutputDebugString((LPCWSTR)cstr);
#endif

            RegDeleteKey(
                HKEY_LOCAL_MACHINE,
                prv->pwszKeyName);
        }
    }

//error:            

    return;
}

#include <shlobj.h>         // CSIDL_ #defines
#include <userenv.h>
#include <userenvp.h>   // CreateLinkFile API

typedef struct _PROGRAMENTRY
{
    UINT        uiLinkName;
    UINT        uiDescription;
    DWORD       csidl;          // special folder index
    WCHAR const *pwszExeName;
    WCHAR const *pwszArgs;
} PROGRAMENTRY;

PROGRAMENTRY const g_aProgramEntry[] = {
    {
        IDS_STARTMENU_CERTMMC_LINKNAME,         // uiLinkName
        IDS_STARTMENU_CERTMMC_DESCRIPTION,      // uiDescription
        CSIDL_COMMON_ADMINTOOLS,                // "All Users\Start Menu\Programs\Administrative Tools"
        L"certsrv.msc",                         // pwszExeName
        L"",                                    // pwszArgs
    },
};

#define CPROGRAMENTRY   ARRAYSIZE(g_aProgramEntry)


BOOL FFileExists(LPCWSTR szFile)
{
    WIN32_FILE_ATTRIBUTE_DATA data;

    return(
    GetFileAttributesEx(szFile, GetFileExInfoStandard, &data) &&
    !(FILE_ATTRIBUTE_DIRECTORY & data.dwFileAttributes) 
          );
}

HRESULT
DeleteMatchingLinks(
    IN WCHAR const *pwszLinkDir,	// C:\Documents...Administrative Tools
    IN WCHAR const *pwszLinkPath,	// C:\ " \Certification Authority.lnk
    IN WCHAR const *pwszTargetPath,	// C:\WINDOWS\system32\certsrv.msc
    IN WCHAR const *pwszArgs,
    IN WCHAR const *pwszDLLPath)	// C:\WINDOWS\system32\certmmc.dll
{
    HRESULT hr;
    WCHAR *pwszPattern = NULL;
    WCHAR *pwszFile = NULL;
    HANDLE hf = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATA wfd;
    IShellLink *psl = NULL; 
    IPersistFile *ppf = NULL; 
    int i;
    
    hr = myBuildPathAndExt(pwszLinkDir, L"*.lnk", NULL, &pwszPattern);
    _JumpIfError(hr, error, "myBuildPathAndExt");

    hf = FindFirstFile(pwszPattern, &wfd);
    if (INVALID_HANDLE_VALUE == hf)
    {
	hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
	_JumpErrorStr2(hr, error, "no *.lnk files", pwszPattern, hr);
    }

    hr = HRESULT_FROM_WIN32(ERROR_DIRECTORY);
    do {
        WCHAR wszPath[MAX_PATH];

	if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	{
	    continue;
	}

        if (NULL != psl)
	{
	    psl->Release(); 
	    psl = NULL;
	}
        if (NULL != ppf)
	{
	    ppf->Release(); 
	    ppf = NULL;
	}
	if (NULL != pwszFile)
	{
	    LocalFree(pwszFile);
	    pwszFile = NULL;
	}
	hr = myBuildPathAndExt(pwszLinkDir, wfd.cFileName, NULL, &pwszFile);
	_JumpIfError(hr, error, "myBuildPathAndExt");

	if (0 == mylstrcmpiL(pwszLinkPath, pwszFile))
	{
	    continue;	// skip exact match
	}

        // Get a pointer to the IShellLink interface. 
        hr = CoCreateInstance(CLSID_ShellLink, NULL, 
            CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID *) &psl); 
	if (S_OK != hr)
	    psl = NULL;
        _JumpIfError(hr, error, "CoCreateInstance");

        // Query IShellLink for the IPersistFile interface for saving the 
        // shortcut in persistent storage. 
        hr = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf); 
	if (S_OK != hr)
	    ppf = NULL;
        _JumpIfError(hr, error, "QI");
        
        // Load the link by calling IPersistFile::Load. 
        hr = ppf->Load(pwszFile, STGM_READ); 
        _JumpIfError(hr, error, "Load");

        hr = psl->GetPath(wszPath, ARRAYSIZE(wszPath), &wfd, SLGP_UNCPRIORITY);
        _JumpIfError(hr, error, "GetPath");

	if (0 != mylstrcmpiL(pwszTargetPath, wszPath))
	{
	    continue;	// different path: skip
	}

        hr = psl->GetArguments(wszPath, ARRAYSIZE(wszPath));
        _JumpIfError(hr, error, "GetArguments");

	if (0 != mylstrcmpiL(pwszArgs, wszPath))
	{
	    continue;	// different arguments: skip
	}

	hr = psl->GetIconLocation(wszPath, ARRAYSIZE(wszPath), &i);
        _JumpIfError(hr, error, "GetIconLocation");

	if (0 != mylstrcmpiL(pwszDLLPath, wszPath))
	{
	    continue;	// different icon DLL: skip
	}
	psl->Release(); 
	psl = NULL;
	ppf->Release(); 
	ppf = NULL;
	DeleteFile(pwszFile);

    } while (FindNextFile(hf, &wfd));
    hr = S_OK;

error:
    if (INVALID_HANDLE_VALUE != hf)
    {
	FindClose(hf);
    }
    if (NULL != pwszFile)
    {
	LocalFree(pwszFile);
    }
    if (NULL != pwszPattern)
    {
	LocalFree(pwszPattern);
    }
    if (NULL != ppf)
    {
        ppf->Release(); 
    }
    if (NULL != psl)
    {
        psl->Release(); 
    }
    return(hr);
}


void CreateProgramGroupLink()
{
    HRESULT hr;
    PROGRAMENTRY const *ppe;
    IShellLink *psl = NULL; 
    IPersistFile *ppf = NULL; 

    for (ppe = g_aProgramEntry; ppe < &g_aProgramEntry[CPROGRAMENTRY]; ppe++)
    {
        WCHAR wszPath[MAX_PATH];
        CString strDLLPath, strPath, strDescr, strLinkName, strLinkPath;
        LPWSTR pszTmp;

        if (NULL == (pszTmp = strPath.GetBuffer(MAX_PATH)))
        {
            hr = E_OUTOFMEMORY;
            _JumpError(hr, error, "GetBuffer");
        }
        
        GetSystemDirectory(pszTmp, MAX_PATH);

        strPath += L"\\";

        strDLLPath = strPath;
        strDLLPath += WSZCERTMMC_DLL;

        strPath += ppe->pwszExeName;

        // don't create link for file that doesn't exist
        if (!FFileExists(strPath))
            continue;

        strDescr.Format(L"@%s,-%d", strDLLPath.GetBuffer(), ppe->uiDescription);
        
        hr = SHGetFolderPath(
            NULL,
            ppe->csidl,
            NULL,
            SHGFP_TYPE_CURRENT,
            wszPath);
        _JumpIfError(hr, error, "SHGetFolderPath(Administrative Tools)");

        strLinkName.LoadString(ppe->uiLinkName);
        strLinkPath.Format(L"%s\\%s.lnk", wszPath, strLinkName);
	DeleteMatchingLinks(
			wszPath,
			strLinkPath,
			strPath,
			ppe->pwszArgs,
			strDLLPath);

        // Get a pointer to the IShellLink interface. 
        hr = CoCreateInstance(CLSID_ShellLink, NULL, 
            CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID *) &psl); 
	if (S_OK != hr)
	    psl = NULL;
        _JumpIfError(hr, error, "CoCreateInstance");

        strPath += ppe->pwszArgs;
        hr = psl->SetPath(strPath);
        _JumpIfError(hr, error, "SetPath");

        hr = psl->SetIconLocation(strDLLPath, 0);
        _JumpIfError(hr, error, "SetIconLocation");

        hr = psl->SetDescription(strDescr);
        _JumpIfError(hr, error, "SetDescription");

        // Query IShellLink for the IPersistFile interface for saving the 
        // shortcut in persistent storage. 
        hr = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf); 
	if (S_OK != hr)
	    ppf = NULL;
        _JumpIfError(hr, error, "QI");
        
        // Save the link by calling IPersistFile::Save. 
        hr = ppf->Save(strLinkPath.GetBuffer(), TRUE); 
        _JumpIfError(hr, error, "Save");

        hr = SHSetLocalizedName(
            strLinkPath.GetBuffer(),
            strDLLPath,
            ppe->uiLinkName);
        _JumpIfError(hr, error, "SHSetLocalizedName");

        ppf->Release(); 
	ppf = NULL;
        psl->Release(); 
	psl = NULL;
    }

    hr = S_OK;

error:
    if (NULL != ppf)
        ppf->Release(); 
    if (NULL != psl)
        psl->Release(); 
    return;
}

void RemoveProgramGroupLink()
{
    HRESULT hr = S_OK;
    PROGRAMENTRY const *ppe;

    for (ppe = g_aProgramEntry; ppe < &g_aProgramEntry[CPROGRAMENTRY]; ppe++)
    {
        CString cstrLinkName;
        cstrLinkName.LoadString(ppe->uiLinkName);
        if (cstrLinkName.IsEmpty())
        {
            hr = myHLastError();
            _PrintError(hr, "LoadString");
            continue;
        }

        if (!DeleteLinkFile(
            ppe->csidl,     // CSIDL_*
            NULL,               // IN LPCSTR lpSubDirectory
            (LPCWSTR)cstrLinkName,      // IN LPCSTR lpFileName
            FALSE))         // IN BOOL fDeleteSubDirectory
        {
            hr = myHLastError();
            _PrintError2(hr, "DeleteLinkFile", hr);
        }
    }

//error:
    _PrintIfError2(hr, "RemoveProgramGroupLink", hr);    

    return;
}
