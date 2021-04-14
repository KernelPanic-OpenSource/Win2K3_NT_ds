/**************************************************************************
   Copyright (C) 1999  Microsoft Corporation.  All Rights Reserved.

   MODULE:     FILECFG.CPP

   PURPOSE:    Source module reading/writing PM config sets from a file

   FUNCTIONS:

   COMMENTS:
      
**************************************************************************/

/**************************************************************************
   Include Files
**************************************************************************/

#include "pmcfg.h"

#define MAX_EXT     10
#define MAX_FILTER  256

TCHAR g_szPassportManager[] = TEXT("PassportManager");

/**************************************************************************

    PMAdmin_GetFileName

*******************************************************************************/
BOOL PMAdmin_GetFileName
(
    HWND    hWnd,
    BOOL    fOpen,
    LPTSTR  lpFileName,
    DWORD   cbFileName
)
{
    UINT            TitleStringID, FilterID;
    TCHAR           szTitle[MAX_TITLE];
    TCHAR           szDefaultExtension[MAX_EXT];
    TCHAR           szFilter[MAX_FILTER];
    LPTSTR          lpFilterChar;
    OPENFILENAME    OpenFileName;
    BOOL            fSuccess;

    //
    //  Load various strings that will be displayed and used by the common open
    //  or save dialog box.  Note that if any of these fail, the error is not
    //  fatal-- the common dialog box may look odd, but will still work.
    //

    if (fOpen)
    {
        TitleStringID = IDS_OPENFILETITLE;
        FilterID = IDS_PMOPENFILEFILTER;
    }
    else
    {
        TitleStringID = IDS_SAVEFILETITLE;
        FilterID = IDS_PMSAVEFILEFILTER;
    }        

    LoadString(g_hInst, TitleStringID, szTitle, DIMENSION(szTitle));
    LoadString(g_hInst, IDS_PMCONFIGDEFEXT, szDefaultExtension, DIMENSION(szDefaultExtension));

    if (LoadString(g_hInst, FilterID, szFilter, DIMENSION(szFilter)))
    {
        //
        //  The common dialog library requires that the substrings of the
        //  filter string be separated by nulls, but we cannot load a string
        //  containing nulls.  So we use some dummy character in the resource
        //  that we now convert to nulls.
        //
        for (lpFilterChar = szFilter; 
             *lpFilterChar != 0; 
              lpFilterChar = CharNext(lpFilterChar)) 
        {

            if (*lpFilterChar == TEXT('#'))
                *lpFilterChar++ = 0;
        }
    }

    ZeroMemory(&OpenFileName, sizeof(OPENFILENAME));

    OpenFileName.lStructSize = sizeof(OPENFILENAME);
    OpenFileName.hwndOwner = hWnd;
    OpenFileName.hInstance = g_hInst;
    OpenFileName.lpstrFilter = szFilter;
    OpenFileName.lpstrFile = lpFileName;
    OpenFileName.nMaxFile = cbFileName;
    OpenFileName.lpstrTitle = szTitle;
    OpenFileName.lpstrDefExt = szDefaultExtension;
    if (fOpen) 
    {
        OpenFileName.Flags = OFN_HIDEREADONLY | OFN_EXPLORER | OFN_FILEMUSTEXIST;
        fSuccess = GetOpenFileName(&OpenFileName);
    }
    else 
    {
        OpenFileName.Flags = OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT |
            OFN_EXPLORER | OFN_NOREADONLYRETURN | OFN_PATHMUSTEXIST;
        fSuccess = GetSaveFileName(&OpenFileName);
    }
    return fSuccess;
}

// prototype of function implemented in regcfg.cpp
BOOL OpenTopRegKey
(
    HWND            hWndDlg,
    LPTSTR          lpszRemoteComputer,
    HKEY            *phklm,
    HKEY            *phkeyPassport
);

/**************************************************************************

    ReadFileConfigSet
    
    Read the current passport manager config set from the specified file
    
**************************************************************************/
BOOL ReadFileConfigSet
(
    LPPMSETTINGS    lpPMConfig,
    LPCTSTR         lpszFileName
)
{
    HKEY  hklm = 0;
    HKEY  hkeyPassport = 0;
    DWORD dwTemp;
    TCHAR achTemp[INTERNET_MAX_URL_LENGTH];

    // makesure the specified file exists.
    if (!PathFileExists(lpszFileName))
    {
        ReportError(NULL, IDS_FILENOTFOUND);
        return FALSE;
    }

    // make sure this user can save any changes, if not simply bail
    if (!OpenTopRegKey(NULL, g_szRemoteComputer, &hklm, &hkeyPassport))
    {
        if (hklm && hklm != HKEY_LOCAL_MACHINE)
            RegCloseKey(hklm);
        return FALSE;
    }
    RegCloseKey(hkeyPassport);
    if (hklm != HKEY_LOCAL_MACHINE)
        RegCloseKey(hklm);

    // Zero Init the structure
    ZeroMemory(lpPMConfig, sizeof(PMSETTINGS));

    // Read the Time Window Number
    dwTemp = GetPrivateProfileInt(g_szPassportManager,
                                  g_szTimeWindow,
                                  -1,
                                  lpszFileName);
    if(dwTemp != -1)
        lpPMConfig->dwTimeWindow = dwTemp;
        
    // Read the value for Forced Signin
    dwTemp = GetPrivateProfileInt(g_szPassportManager,
                                  g_szForceSignIn,
                                  -1,
                                  lpszFileName);
    if(dwTemp != -1)
        lpPMConfig->dwForceSignIn = dwTemp;

    // Read the value for NSRefresh
    dwTemp = GetPrivateProfileInt(g_szPassportManager,
                                  g_szNSRefresh,
                                  -1,
                                  lpszFileName);
    if(dwTemp != -1)
        lpPMConfig->dwEnableManualRefresh = dwTemp;

    // Read the default language ID
    dwTemp = GetPrivateProfileInt(g_szPassportManager,
                                  g_szLanguageID,
                                  -1,
                                  lpszFileName);
    if(dwTemp != -1)
        lpPMConfig->dwLanguageID = dwTemp;

    // Get the co-branding template

    GetPrivateProfileString(g_szPassportManager,
                            g_szCoBrandTemplate,
                            (LPTSTR)TEXT("\xFF"),
                            achTemp,
                            DIMENSION(achTemp),
                            lpszFileName);

    if(lstrcmp(achTemp, TEXT("\xFF")) != 0)
        lstrcpy(lpPMConfig->szCoBrandTemplate, achTemp);

    
    // Get the SiteID
    dwTemp = GetPrivateProfileInt(g_szPassportManager,
                                  g_szSiteID,
                                  -1,
                                  lpszFileName);
    if(dwTemp != -1)
        lpPMConfig->dwSiteID = dwTemp;
    
    // Get the return URL template

    GetPrivateProfileString(g_szPassportManager,
                            g_szReturnURL,
                            (LPTSTR)TEXT("\xFF"),
                            achTemp,
                            DIMENSION(achTemp),
                            lpszFileName);

    if(lstrcmp(achTemp, TEXT("\xFF")) != 0)
        lstrcpy(lpPMConfig->szReturnURL, achTemp);

    // Get the ticket cookie domain

    GetPrivateProfileString(g_szPassportManager,
                            g_szTicketDomain,
                            (LPTSTR)TEXT("\xFF"),
                            achTemp,
                            DIMENSION(achTemp),
                            lpszFileName);

    if(lstrcmp(achTemp, TEXT("\xFF")) != 0)
        lstrcpy(lpPMConfig->szTicketDomain, achTemp);

    // Get the ticket cookie path

    GetPrivateProfileString(g_szPassportManager,
                            g_szTicketPath,
                            (LPTSTR)TEXT("\xFF"),
                            achTemp,
                            DIMENSION(achTemp),
                            lpszFileName);

    if(lstrcmp(achTemp, TEXT("\xFF")) != 0)
        lstrcpy(lpPMConfig->szTicketPath, achTemp);

    // Get the profile cookie domain
    GetPrivateProfileString(g_szPassportManager,
                            g_szProfileDomain,
                            (LPTSTR)TEXT("\xFF"),
                            achTemp,
                            DIMENSION(achTemp),
                            lpszFileName);
    if(lstrcmp(achTemp, TEXT("\xFF")) != 0)
        lstrcpy(lpPMConfig->szProfileDomain, achTemp);

    // Get the profile cookie path
    GetPrivateProfileString(g_szPassportManager,
                            g_szProfilePath,
                            (LPTSTR)TEXT("\xFF"),
                            achTemp,
                            DIMENSION(achTemp),
                            lpszFileName);
    if(lstrcmp(achTemp, TEXT("\xFF")) != 0)
        lstrcpy(lpPMConfig->szProfilePath, achTemp);

    // Get the secure cookie domain
    GetPrivateProfileString(g_szPassportManager,
                            g_szSecureDomain,
                            (LPTSTR)TEXT("\xFF"),
                            achTemp,
                            DIMENSION(achTemp),
                            lpszFileName);
    if(lstrcmp(achTemp, TEXT("\xFF")) != 0)
        lstrcpy(lpPMConfig->szSecureDomain, achTemp);

    // Get the secure cookie path
    GetPrivateProfileString(g_szPassportManager,
                            g_szSecurePath,
                            (LPTSTR)TEXT("\xFF"),
                            achTemp,
                            DIMENSION(achTemp),
                            lpszFileName);
    if(lstrcmp(achTemp, TEXT("\xFF")) != 0)
        lstrcpy(lpPMConfig->szSecurePath, achTemp);

    // Get the DisasterURL
    GetPrivateProfileString(g_szPassportManager,
                            g_szDisasterURL,
                            (LPTSTR)TEXT("\xFF"),
                            achTemp,
                            DIMENSION(achTemp),
                            lpszFileName);
    if(lstrcmp(achTemp, TEXT("\xFF")) != 0)
        lstrcpy(lpPMConfig->szDisasterURL, achTemp);

    // Get Standalone mode setting
    dwTemp = GetPrivateProfileInt(g_szPassportManager,
                                  g_szStandAlone,
                                  -1,
                                  lpszFileName);
    if(dwTemp != -1)
        lpPMConfig->dwStandAlone = dwTemp;
    
    // Get DisableCookies mode setting
    dwTemp = GetPrivateProfileInt(g_szPassportManager,
                                  g_szDisableCookies,
                                  -1,
                                  lpszFileName);
    if(dwTemp != -1)
        lpPMConfig->dwDisableCookies = dwTemp;

    // Get the HostName
    GetPrivateProfileString(g_szPassportManager,
                            g_szHostName,
                            (LPTSTR)TEXT("\xFF"),
                            achTemp,
                            DIMENSION(achTemp),
                            lpszFileName);
    if(lstrcmp(achTemp, TEXT("\xFF")) != 0)
        lstrcpyn(lpPMConfig->szHostName, achTemp, DIMENSION(lpPMConfig->szHostName));

    // Get the HostIP
    GetPrivateProfileString(g_szPassportManager,
                            g_szHostIP,
                            (LPTSTR)TEXT("\xFF"),
                            achTemp,
                            DIMENSION(achTemp),
                            lpszFileName);
    if(lstrcmp(achTemp, TEXT("\xFF")) != 0)
        lstrcpyn(lpPMConfig->szHostIP, achTemp, DIMENSION(lpPMConfig->szHostIP));

    // Get the Verbose Mode
    dwTemp = GetPrivateProfileInt(g_szPassportManager,
                                  g_szVerboseMode,
                                  -1,
                                  lpszFileName);
    if(dwTemp != -1)
        lpPMConfig->dwVerboseMode = dwTemp;


    // Get the EnvName
    GetPrivateProfileString(g_szPassportManager,
                            g_szEnvName,
                            (LPTSTR)TEXT("\xFF"),
                            achTemp,
                            DIMENSION(achTemp),
                            lpszFileName);
    if(lstrcmp(achTemp, TEXT("\xFF")) != 0)
        lstrcpyn(lpPMConfig->szEnvName, achTemp, DIMENSION(lpPMConfig->szEnvName));

    // Get the RemoteFile
    GetPrivateProfileString(g_szPassportManager,
                            g_szRemoteFile,
                            (LPTSTR)TEXT("\xFF"),
                            achTemp,
                            DIMENSION(achTemp),
                            lpszFileName);
    if(lstrcmp(achTemp, TEXT("\xFF")) != 0)
        lstrcpy(lpPMConfig->szRemoteFile, achTemp);

    return TRUE; 
}


/**************************************************************************

    WriteFileConfigSet
    
    Writes the current passport manager config set to the specified file
    
**************************************************************************/
BOOL WriteFileConfigSet
(
    LPPMSETTINGS    lpPMConfig,
    LPCTSTR         lpszFileName
)
{
    TCHAR   szTemp[MAX_PATH];
    
    // Write the Time Window Number
    wsprintf (szTemp, TEXT("%lu"), lpPMConfig->dwTimeWindow);
    WritePrivateProfileString(g_szPassportManager,
                              g_szTimeWindow,
                              szTemp,
                              lpszFileName);
        
    // write the value for Forced Signin
    wsprintf (szTemp, TEXT("%lu"), lpPMConfig->dwForceSignIn);
    WritePrivateProfileString(g_szPassportManager,
                              g_szForceSignIn,
                              szTemp,
                              lpszFileName);

    // write the value for NSRefresh
    wsprintf (szTemp, TEXT("%lu"), lpPMConfig->dwEnableManualRefresh);
    WritePrivateProfileString(g_szPassportManager,
                              g_szNSRefresh,
                              szTemp,
                              lpszFileName);

    // Read the default language ID
    wsprintf (szTemp, TEXT("%lu"), lpPMConfig->dwLanguageID);
    WritePrivateProfileString(g_szPassportManager,
                              g_szLanguageID,
                              szTemp,
                              lpszFileName);
                              
    // Write the co-branding template
    WritePrivateProfileString(g_szPassportManager,
                             g_szCoBrandTemplate,
                             lpPMConfig->szCoBrandTemplate,
                             lpszFileName);
    
    // Write the SiteID
    wsprintf (szTemp, TEXT("%lu"),lpPMConfig->dwSiteID);
    WritePrivateProfileString(g_szPassportManager,
                              g_szSiteID,
                              szTemp,
                              lpszFileName);
    
    // Write the return URL template
    WritePrivateProfileString(g_szPassportManager,
                              g_szReturnURL,
                              lpPMConfig->szReturnURL,
                              lpszFileName);
    
    // Write the ticket cookie domain
    WritePrivateProfileString(g_szPassportManager,
                              g_szTicketDomain,
                              lpPMConfig->szTicketDomain,
                              lpszFileName);
    
    // Write the ticket cookie path
    WritePrivateProfileString(g_szPassportManager,
                              g_szTicketPath,
                              lpPMConfig->szTicketPath,
                              lpszFileName);

    // Write the profile cookie domain
    WritePrivateProfileString(g_szPassportManager,
                              g_szProfileDomain,
                              lpPMConfig->szProfileDomain,
                              lpszFileName);
    
    // Write the profile cookie path
    WritePrivateProfileString(g_szPassportManager,
                              g_szProfilePath,
                              lpPMConfig->szProfilePath,
                              lpszFileName);

    // Write the secure cookie domain
    WritePrivateProfileString(g_szPassportManager,
                              g_szSecureDomain,
                              lpPMConfig->szSecureDomain,
                              lpszFileName);

    // Write the secure profile cookie path
    WritePrivateProfileString(g_szPassportManager,
                              g_szSecurePath,
                              lpPMConfig->szSecurePath,
                              lpszFileName);

    // Write the Disaster URL
    WritePrivateProfileString(g_szPassportManager,
                              g_szDisasterURL,
                              lpPMConfig->szDisasterURL,
                              lpszFileName);
    
    // Write Standalone mode setting
    wsprintf (szTemp, TEXT("%lu"), lpPMConfig->dwStandAlone);
    WritePrivateProfileString(g_szPassportManager,
                           g_szStandAlone,
                           szTemp,
                           lpszFileName);
    
    // Write DisableCookies mode setting
    wsprintf (szTemp, TEXT("%lu"), lpPMConfig->dwDisableCookies);
    WritePrivateProfileString(g_szPassportManager,
                              g_szDisableCookies,
                              szTemp,
                              lpszFileName);

    // Write the Host Name
    WritePrivateProfileString(g_szPassportManager,
                              g_szHostName,
                              lpPMConfig->szHostName,
                              lpszFileName);
    
    // Write the Host IP
    WritePrivateProfileString(g_szPassportManager,
                              g_szHostIP,
                              lpPMConfig->szHostIP,
                              lpszFileName);
    
    // Write the Verbose Mode
    wsprintf (szTemp, TEXT("%lu"), lpPMConfig->dwVerboseMode);
    WritePrivateProfileString(g_szPassportManager,
                              g_szVerboseMode,
                              szTemp,
                              lpszFileName);
    
    // Write the EnvName
    WritePrivateProfileString(g_szPassportManager,
                              g_szEnvName,
                              lpPMConfig->szEnvName,
                              lpszFileName);
    
    // Write the RemoteFile
    WritePrivateProfileString(g_szPassportManager,
                              g_szRemoteFile,
                              lpPMConfig->szRemoteFile,
                              lpszFileName);
    
    return TRUE; 
}
