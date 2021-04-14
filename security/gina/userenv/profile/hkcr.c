//*************************************************************
//
//  HKCR management routines
//
//  hkcr.c
//
//  Microsoft Confidential
//  Copyright (c) Microsoft Corporation 1997
//  All rights reserved
//
//*************************************************************

/*++

Abstract:

    This module contains the code executed at logon for 
    creating a user classes hive and mapping it into the standard
    user hive.  The user classes hive and its machine classes
    counterpart make up the registry subtree known as 
    HKEY_CLASSES_ROOT.

Author:

    Adam P. Edwards     (adamed)  10-Oct-1997
    Gregory Jensenworth (gregjen) 1-Jul-1997

Key Functions:

    LoadUserClasses
    UnloadClasses

Notes:

    Starting with NT5, the HKEY_CLASSES_ROOT key is per-user
    instead of per-machine -- previously, HKCR was an alias for 
    HKLM\Software\Classes.  

    The per-user HKCR combines machine classes stored it the 
    traditional HKLM\Software\Classes location with classes
    stored in HKCU\Software\Classes.

    Certain keys, such as CLSID, will have subkeys that come
    from both the machine and user locations.  When there is a conflict
    in key names, the user oriented key overrides the other one --
    only the user key is seen in that case.

    Originally, the code in this module was responsible for 
    creating this combined view.  That responsibility has moved
    to the win32 registry api's, so the main responsibility of 
    this module is the mapping of the user-specific classes into
    the registry.

    It should be noted that HKCU\Software\Classes is not the true
    location of the user-only class data.  If it were, all the class
    data would be in ntuser.dat, which roams with the user.  Since
    class data can get very large, installation of a few apps
    would cause HKCU (ntuser.dat) to grow from a few hundred thousand K
    to several megabytes.  Since user-only class data comes from
    the directory, it does not need to roam and therefore it was
    separated from HKCU (ntuser.dat) and stored in another hive
    mounted under HKEY_USERS.

    It is still desirable to allow access to this hive through
    HKCU\Software\Classes, so we use some trickery (symlinks) to
    make it seem as if the user class data exists there.


--*/

#include "uenv.h"
#include <malloc.h>
#include <wow64reg.h>
#include "strsafe.h"

#define USER_CLASSES_HIVE_NAME     TEXT("\\UsrClass.dat")
#define CLASSES_SUBTREE            TEXT("Software\\Classes\\")

#define CLASSES_SUBDIRECTORY       TEXT("\\Microsoft\\Windows\\")
#define MAX_HIVE_DIR_CCH           (MAX_PATH + 1 + lstrlen(CLASSES_SUBDIRECTORY))

#define TEMPHIVE_FILENAME          TEXT("TempClassesHive.dat")

#define CLASSES_CLSID_SUBTREE      TEXT("Software\\Classes\\Clsid\\")
#define EXPLORER_CLASSES_SUBTREE   TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Clsid\\")
#define LENGTH(x)                  (sizeof(x) - sizeof(WCHAR))
#define INIT_SPECIALKEY(x)         x

typedef WCHAR* SpecialKey;

SpecialKey SpecialSubtrees[]= {
    INIT_SPECIALKEY(L"*"),
    INIT_SPECIALKEY(L"*\\shellex"),
    INIT_SPECIALKEY(L"*\\shellex\\ContextMenuHandlers"),
    INIT_SPECIALKEY(L"*\\shellex\\PropertyShellHandlers"),
    INIT_SPECIALKEY(L"AppID"),
    INIT_SPECIALKEY(L"ClsID"), 
    INIT_SPECIALKEY(L"Component Categories"),
    INIT_SPECIALKEY(L"Drive"),
    INIT_SPECIALKEY(L"Drive\\shellex"),
    INIT_SPECIALKEY(L"Drive\\shellex\\ContextMenuHandlers"),
    INIT_SPECIALKEY(L"Drive\\shellex\\PropertyShellHandlers"),
    INIT_SPECIALKEY(L"FileType"),
    INIT_SPECIALKEY(L"Folder"),
    INIT_SPECIALKEY(L"Folder\\shellex"),
    INIT_SPECIALKEY(L"Folder\\shellex\\ColumnHandler"),
    INIT_SPECIALKEY(L"Folder\\shellex\\ContextMenuHandlers"), 
    INIT_SPECIALKEY(L"Folder\\shellex\\ExtShellFolderViews"),
    INIT_SPECIALKEY(L"Folder\\shellex\\PropertySheetHandlers"),
    INIT_SPECIALKEY(L"Installer\\Components"),
    INIT_SPECIALKEY(L"Installer\\Features"),
    INIT_SPECIALKEY(L"Installer\\Products"),
    INIT_SPECIALKEY(L"Interface"),
    INIT_SPECIALKEY(L"Mime"),
    INIT_SPECIALKEY(L"Mime\\Database"), 
    INIT_SPECIALKEY(L"Mime\\Database\\Charset"),
    INIT_SPECIALKEY(L"Mime\\Database\\Codepage"),
    INIT_SPECIALKEY(L"Mime\\Database\\Content Type"),
    INIT_SPECIALKEY(L"Typelib")
};
    
#define NUM_SPECIAL_SUBTREES    (sizeof(SpecialSubtrees)/sizeof(*SpecialSubtrees))


//*************************************************************
//
//  CreateRegLink()
//
//  Purpose:    Create a link from the hkDest + SubKeyName
//              pointing to lpSourceRootName
//
//              if the key (link) already exists, do nothing
//
//  Parameters: hkDest            - root of destination
//              SubKeyName        - subkey of destination
//              lpSourceName      - target of link
//
//  Return:     ERROR_SUCCESS if successful
//              other NTSTATUS if an error occurs
//
//*************************************************************/

LONG CreateRegLink(HKEY hkDest,
                   LPTSTR SubKeyName,
                   LPTSTR lpSourceName)
{
    NTSTATUS Status;
    UNICODE_STRING  LinkTarget;
    UNICODE_STRING  SubKey;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE hkInternal;
    UNICODE_STRING  SymbolicLinkValueName;

    //
    // Initialize special key value used to make symbolic links
    //
    RtlInitUnicodeString(&SymbolicLinkValueName, L"SymbolicLinkValue");

    //
    // Initialize unicode string for our in params
    //
    RtlInitUnicodeString(&LinkTarget, lpSourceName);
    RtlInitUnicodeString(&SubKey, SubKeyName);

    //
    // See if this link already exists -- this is necessary because
    // NtCreateKey fails with STATUS_OBJECT_NAME_COLLISION if a link
    // already exists and will not return a handle to the existing
    // link.
    //
    InitializeObjectAttributes(&Attributes,
                               &SubKey,
                               OBJ_CASE_INSENSITIVE | OBJ_OPENLINK,
                               hkDest,
                               NULL);

    //
    // If this call succeeds, we get a handle to the existing link
    //
    Status = NtOpenKey( &hkInternal,
                        MAXIMUM_ALLOWED,
                        &Attributes );

    if (STATUS_OBJECT_NAME_NOT_FOUND == Status) {

        //
        // There is no existing link, so use NtCreateKey to make a new one
        //
        Status = NtCreateKey( &hkInternal,
                              KEY_CREATE_LINK | KEY_SET_VALUE,
                              &Attributes,
                              0,
                              NULL,
                              REG_OPTION_VOLATILE | REG_OPTION_CREATE_LINK,
                              NULL);
    }

    //
    // Whether the link existed already or not, we should still set
    // the value which determines the link target
    //
    if (NT_SUCCESS(Status)) {

        Status = NtSetValueKey( hkInternal,
                                &SymbolicLinkValueName,
                                0,
                                REG_LINK,
                                LinkTarget.Buffer,
                                LinkTarget.Length);
        NtClose(hkInternal);
    }

    return RtlNtStatusToDosError(Status);
}


//*************************************************************
//
//  DeleteRegLink()
//
//  Purpose:    Deletes a registry key (or link) without
//              using the advapi32 registry apis
//
//
//  Parameters: hkRoot          -   parent key
//              lpSubKey        -   subkey to delete
//
//  Return:     ERROR_SUCCESS if successful
//              other error if not
//
//  Comments:
//
//  History:    Date        Author     Comment
//              3/6/98      adamed     Created
//
//*************************************************************

LONG DeleteRegLink(HKEY hkRoot, LPTSTR lpSubKey)
{
    OBJECT_ATTRIBUTES Attributes;
    HKEY              hKey;
    NTSTATUS          Status;
    UNICODE_STRING    Subtree;

    //
    // Initialize string for lpSubKey param
    //
    RtlInitUnicodeString(&Subtree, lpSubKey);

    InitializeObjectAttributes(&Attributes,
                               &Subtree,
                               OBJ_CASE_INSENSITIVE | OBJ_OPENLINK,
                               hkRoot,
                               NULL);

    //
    // Open the link
    //
    Status = NtOpenKey( &hKey,
                        MAXIMUM_ALLOWED,
                        &Attributes );

    //
    // If we succeeded in opening it, delete it
    //
    if (NT_SUCCESS(Status)) {

        Status = NtDeleteKey(hKey);
        NtClose(hKey);
    }

    return RtlNtStatusToDosError(Status);
}


//*************************************************************
//
//  MapUserClassesIntoUserHive()
//
//  Purpose:    Makes HKCU\\Software\\Classes point to
//              the user classes hive. This is done by using
//              a symbolic link, a feature of the kernel's
//              object manager.  We use this to make 
//              HKCU\Software\Classes poing to another hive
//              where the classes exist physically.
//              If there is an existing HKCU\\Software\\Classes,
//              it is deleted along with everything below it.
//
//
//  Parameters: lpProfile       -   user's profile
//              lpSidString     -   string representing user's sid
//
//  Return:     ERROR_SUCCESS if successful
//              other error if not
//
//  Comments:
//
//  History:    Date        Author     Comment
//              3/6/98      adamed     Created
//
//*************************************************************
LONG MapUserClassesIntoUserHive(
    LPPROFILE lpProfile,
    LPTSTR lpSidString)
{
    LONG   Error;
    LPTSTR lpClassesKeyName = NULL;
    DWORD  cchClassesKeyName;

    //
    // get memory for user classes keyname
    //
    cchClassesKeyName = lstrlen(lpSidString) + 
                        lstrlen(USER_CLASSES_HIVE_SUFFIX) +
                        lstrlen(USER_KEY_PREFIX) + 
                        1;
                        
    lpClassesKeyName = (LPTSTR) LocalAlloc(LPTR, cchClassesKeyName * sizeof(TCHAR));

    //
    // Can't continue if no memory;
    //
    if ( !lpClassesKeyName ) {
        Error = ERROR_NOT_ENOUGH_MEMORY;
        goto Exit;
    }

    //
    // concoct user classes keyname
    //
    StringCchCopy( lpClassesKeyName, cchClassesKeyName, USER_KEY_PREFIX);
    StringCchCat ( lpClassesKeyName, cchClassesKeyName, lpSidString );
    StringCchCat ( lpClassesKeyName, cchClassesKeyName, USER_CLASSES_HIVE_SUFFIX);

    //
    // Eliminate any existing form of HKCU\Software\Classes
    //
    
    //
    // First, delete existing link
    //
    Error = DeleteRegLink(lpProfile->hKeyCurrentUser, CLASSES_SUBTREE);

    //
    // It's ok if the deletion fails because the classes key, link or nonlink,
    // doesn't exist.  It's also ok if it fails because the key exists but is not
    // a link and has children -- in this case, the key and its children will
    // be eliminated by a subsequent call to RegDelNode.
    //
    if (ERROR_SUCCESS != Error) {
        if ((ERROR_FILE_NOT_FOUND != Error) && (ERROR_ACCESS_DENIED != Error)) {
            goto Exit;
        }
    }

    //
    // Just to be safe, destroy any existing HKCU\Software\Classes and children.
    // This key may exist from previous unreleased versions of NT5, or from
    // someone playing around with hive files and adding bogus keys
    //
    if ((Error = RegDelnode (lpProfile->hKeyCurrentUser, CLASSES_SUBTREE)) != ERROR_SUCCESS) {
        //
        // It's ok if this fails because the key doesn't exist, since
        // nonexistence is our goal.
        //
        if (ERROR_FILE_NOT_FOUND != Error) {
            goto Exit;
        }
    }

    //
    // At this point, we know that no HKCU\Software\Classes exists, so we should
    // be able to make a link there which points to the hive with the user class
    // data.
    // 
    Error = CreateRegLink(lpProfile->hKeyCurrentUser,
                         CLASSES_SUBTREE,
                         lpClassesKeyName);

Exit:
    if (lpClassesKeyName)
        LocalFree(lpClassesKeyName);
        
    return Error;
}


//*************************************************************
//
//  CreateClassesFolder()
//
//  Purpose:    Create the directory for the classes hives
//
//
//  Parameters:
//              pProfile        - pointer to profile struct
//              szLocalHiveDir  - out param for location of
//                                classes hive folder.
//              cchLocalHiveDir - size of the buffer, in TCHARs
//
//  Return:     ERROR_SUCCESS if successful
//              other error if an error occurs
//
//
//*************************************************************
LONG CreateClassesFolder(LPPROFILE pProfile, LPTSTR szLocalHiveDir, DWORD cchLocalHiveDir)
{
    BOOL   fGotLocalData;
    BOOL   fCreatedSubdirectory;

    //
    // Find out the correct shell location for our subdir --
    // this call will create it if it doesn't exist.
    // This is a subdir of the user profile which does not 
    // roam.
    //

    //
    // Need to do this to fix up a localisation prob. in NT4
    //

    
    PatchLocalAppData(pProfile->hTokenUser);
    
    fGotLocalData = GetFolderPath (
        CSIDL_LOCAL_APPDATA,
        pProfile->hTokenUser,
        szLocalHiveDir);

    if (!fGotLocalData) {
        // a bogus error, check the debug output of GetFolderPath() for correct 
        // error code.
        return ERROR_INVALID_FUNCTION; 
    }


    //
    // append the terminating pathsep so we can
    // add more paths to the newly retrieved subdir
    //
    StringCchCat(szLocalHiveDir, cchLocalHiveDir, CLASSES_SUBDIRECTORY);

    //
    // We will now create our own subdir, CLASSES_SUBDIRECTORY,
    // inside the local appdata subdir we just received above.
    //
    fCreatedSubdirectory = CreateNestedDirectory(szLocalHiveDir, NULL);

    if (fCreatedSubdirectory) {
        return ERROR_SUCCESS;
    }

    return GetLastError();
}


//*************************************************************
//
//  UnloadClassHive()
//
//  Purpose:    unmounts a classes hive
//
//  Parameters: lpSidString     -   string representing user's
//                                  sid
//              lpSuffix        -   hive name suffix
//
//  Return:     ERROR_SUCCESS if successful,
//              other error if not
//
//  Comments:
//
//  History:    Date        Author     Comment
//              3/6/98      adamed     Created
//
//*************************************************************
LONG UnloadClassHive(
    LPTSTR lpSidString,
    LPTSTR lpSuffix)
{
    LPTSTR lpHiveName = NULL;
    LONG error;
    OBJECT_ATTRIBUTES Attributes;
    NTSTATUS Status;
    HKEY hKey;
    UNICODE_STRING ClassesFullPath;
    DWORD cchHiveName;

    //
    // Get memory for the combined hive key name 
    //
    cchHiveName = lstrlen(lpSidString) +
                  lstrlen(USER_KEY_PREFIX) + 
                  lstrlen(lpSuffix) + 
                  1;
                  
    lpHiveName = (LPTSTR) LocalAlloc(LPTR, cchHiveName * sizeof(TCHAR));

    if (!lpHiveName)
    {
        error = ERROR_NOT_ENOUGH_MEMORY;
        goto Exit;
    }

    //
    // build the key name of the combined hive
    //
    StringCchCopy( lpHiveName, cchHiveName, USER_KEY_PREFIX );
    StringCchCat ( lpHiveName, cchHiveName, lpSidString );
    StringCchCat ( lpHiveName, cchHiveName, lpSuffix);

    //
    // Prepare to open the root of the classes hive
    // 
    RtlInitUnicodeString(&ClassesFullPath, lpHiveName);

    InitializeObjectAttributes(&Attributes,
                               &ClassesFullPath,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    Status = NtOpenKey( &hKey,
                        KEY_READ,
                        &Attributes );

    if (NT_SUCCESS(Status)) {

        //
        // Make sure the hive is persisted properly
        //
        RegFlushKey(hKey);
        RegCloseKey(hKey);

        //
        // Unmount the hive -- this should only fail if
        // someone has a subkey of the hive open -- this 
        // should not normally happen and probably means there's a service
        // that is leaking keys.
        //
        if (MyRegUnLoadKey(HKEY_USERS,
                           lpHiveName + ((sizeof(USER_KEY_PREFIX) / sizeof(TCHAR))-1))) {
            error = ERROR_SUCCESS;
        } else {
            error = GetLastError();
        }

    } else {

        error = RtlNtStatusToDosError(Status);
    }


Exit:
    if (lpHiveName)
        LocalFree(lpHiveName);
        
    if (error != ERROR_SUCCESS) {
        DebugMsg((DM_WARNING, TEXT("UnLoadClassHive: failed to unload classes key with %x"), error));
    } else {
        DebugMsg((DM_VERBOSE, TEXT("UnLoadClassHive: Successfully unmounted %s%s"), lpSidString, lpSuffix));
    }


    return  error;
}

//*************************************************************
//
//  UnloadClasses()
//
//  Purpose:    Free the special combined hive
//
//  Parameters: lpProfile -   Profile information
//              SidString -   User's Sid as a string
//
//  Return:     TRUE if successful
//              FALSE if not
//
//*************************************************************
BOOL UnloadClasses(
    LPTSTR lpSidString)
{
    LONG Error;
  
    // unload user classes hive
    Error = UnloadClassHive(
        lpSidString,
        USER_CLASSES_HIVE_SUFFIX);

    return ERROR_SUCCESS == Error;
}


HRESULT MyRegLoadKeyEx(HKEY hKeyRoot, LPTSTR lpSubKey, LPTSTR lpFile, HKEY hKeyTrustClass)
{
    HRESULT             hr = E_FAIL;
    NTSTATUS            Status;
    TCHAR               lpKeyName[MAX_PATH];
    UNICODE_STRING      UnicodeKeyName;
    UNICODE_STRING      UnicodeFileName;
    OBJECT_ATTRIBUTES   keyAttributes;
    OBJECT_ATTRIBUTES   fileAttributes;
    BOOLEAN             WasEnabled;
    BOOL                bAdjustPriv = FALSE;
    BOOL                bAllocatedFileName = FALSE;
    HANDLE              hToken = NULL;
    

    DebugMsg((DM_VERBOSE, TEXT("MyRegLoadKeyEx:  Loading key <%s>"), lpSubKey));

    //
    //  Only support loading hive to HKU or HKLM
    //

    if (hKeyRoot != HKEY_USERS && hKeyRoot != HKEY_LOCAL_MACHINE)
    {
        DebugMsg((DM_WARNING, TEXT("MyRegLoadKeyEx:  only HKU or HKLM is supported!")));
        hr = HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
        goto Exit;
    }
    
    //
    //  Construct the key name for kernel object
    //
    
    hr =  StringCchCopy(lpKeyName, ARRAYSIZE(lpKeyName), (hKeyRoot == HKEY_USERS) ? TEXT("\\Registry\\User\\") : TEXT("\\Registry\\Machine\\"));

    if (FAILED(hr))
    {
        DebugMsg((DM_WARNING, TEXT("MyRegLoadKeyEx: StringCchCopy failed, hr = %08X"), hr));
        goto Exit;
    }

    hr = StringCchCat (lpKeyName, ARRAYSIZE(lpKeyName), lpSubKey);

    if (FAILED(hr))
    {
        DebugMsg((DM_WARNING, TEXT("MyRegLoadKeyEx: StringCchCat failed, hr = %08X"), hr));
        goto Exit;
    }

    //
    //  Initialize the key object attribute
    //

    RtlInitUnicodeString(&UnicodeKeyName, lpKeyName);

    InitializeObjectAttributes(&keyAttributes,
                               &UnicodeKeyName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    //
    //  Convert the file name to kernel file name
    //

    if (!RtlDosPathNameToNtPathName_U(lpFile,
                                      &UnicodeFileName,
                                      NULL,
                                      NULL))
    {
        DebugMsg((DM_WARNING, TEXT("MyRegLoadKeyEx: RtlDosPathNameToNtPathName_U failed for <%s>!"), lpFile));
        hr = HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);
        goto Exit;
    }

    bAllocatedFileName = TRUE;
    
    //
    //  Initialize the file object attribute
    //

    InitializeObjectAttributes(&fileAttributes,
                               &UnicodeFileName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    //
    // Check to see if we are impersonating, if not, we need to enable the Restore privilege
    //

    if(!OpenThreadToken(GetCurrentThread(), TOKEN_READ, TRUE, &hToken) || hToken == NULL)
    {
        Status = RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, TRUE, FALSE, &WasEnabled);

        if (!NT_SUCCESS(Status))
        {
            DebugMsg((DM_WARNING, TEXT("MyRegLoadKeyEx:  Failed to enable restore privilege to load registry key, Status = %08x"), Status));
            hr = HRESULT_FROM_WIN32(RtlNtStatusToDosError(Status));
            goto Exit;
        }

        bAdjustPriv = TRUE;
    }
    else
    {
        CloseHandle(hToken);
    }

    //
    //  Now loading the key
    //

    
/*
    Status = NtLoadKey(&keyAttributes,
                       &fileAttributes);
*/
    Status = NtLoadKeyEx(&keyAttributes,
                         &fileAttributes,
                         0,
                         hKeyTrustClass);
    
    if (!NT_SUCCESS(Status))
    {
        TCHAR   szErr[MAX_PATH];
        DWORD   dwErr;
        
        DebugMsg((DM_WARNING, TEXT("MyRegLoadKeyEx: NtLoadKey failed for <%s>, Status = %08x"), lpSubKey, Status));
        dwErr = RtlNtStatusToDosError(Status); 
        ReportError(NULL, PI_NOUI, 2, EVENT_REGLOADKEYFAILED, GetErrString(dwErr, szErr), lpFile);
        hr = HRESULT_FROM_WIN32(dwErr);
        goto Exit;
    }

#if defined(_WIN64)
    else
    {
        //
        // Notify Wow64 service that it need to watch this hive if it care to do so
        //
        if ( hKeyRoot == HKEY_USERS )
            Wow64RegNotifyLoadHiveUserSid ( lpSubKey );
    }
#endif

    DebugMsg((DM_VERBOSE, TEXT("MyRegLoadKeyEx: Successfully loaded <%s>"), lpSubKey));
    hr = S_OK;

Exit:

    //
    // Restore the privilege to its previous state
    //

    if(bAdjustPriv)
    {
        Status = RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, WasEnabled, FALSE, &WasEnabled);
        
        if (!NT_SUCCESS(Status))
        {
            DebugMsg((DM_WARNING, TEXT("MyRegLoadKeyEx:  Failed to restore RESTORE privilege to previous enabled state. Status = %08X"), Status));
        }
    }

    if (bAllocatedFileName)
    {
        RtlFreeUnicodeString(&UnicodeFileName);
    }
    
    return hr;
}


//*************************************************************
//
//  CreateUserClasses()
//
//  Purpose:    Creates necessary hives for user classes
//
//  Parameters: lpProfile   -   Profile information
//              lpSidString -   User's Sid as a string
//              lpSuffix    -   Suffix to follow the user's sid
//                              when naming the hive
//              lpHiveFileName - full path for backing hive file
//                               of user classes
//              phkResult      - root of created hive on
//                               success
//
//  Return:     ERROR_SUCCESS if successful
//              other NTSTATUS if an error occurs
//
//*************************************************************
LONG CreateClassHive(
    LPPROFILE lpProfile,
    LPTSTR    lpSidString,
    LPTSTR    lpSuffix,
    LPTSTR    lpHiveFilename,
    BOOL      bNewlyIssued)
{
    LONG                      res;
    LPTSTR                    lpHiveKeyName = NULL;
    WIN32_FILE_ATTRIBUTE_DATA fd;
    BOOL                      fHiveExists;
    HKEY                      hkRoot = NULL;
    DWORD                     cchHiveKeyName;
    HKEY                      hKeyUser = NULL;
    HRESULT                   hr;

    //
    // allocate a space big enough for the hive name
    //
    cchHiveKeyName = lstrlen(lpSidString) +
                     lstrlen(lpSuffix) + 
                     1;
                     
    lpHiveKeyName = (LPTSTR) LocalAlloc(LPTR, cchHiveKeyName * sizeof(TCHAR) );

    if ( !lpHiveKeyName )
    {
        res = ERROR_NOT_ENOUGH_MEMORY;
        goto Exit;
    }

    //
    // Open the HKU\{Sid} first, we use this handle as the trust class
    //
    StringCchCopy(lpHiveKeyName, cchHiveKeyName, lpSidString);

    res = RegOpenKeyEx(HKEY_USERS,
                       lpHiveKeyName,
                       0,
                       KEY_READ,
                       &hKeyUser);

    if (res != ERROR_SUCCESS)
    {
        DebugMsg((DM_WARNING, TEXT("CreateClassHive: fail to open user hive. Error %d"),res));
        goto Exit;
    }

    //
    // Append the suffix to construct the class hive key name
    //
    StringCchCat (lpHiveKeyName, cchHiveKeyName, lpSuffix);
   
    //
    // First, see if this hive already exists. We need to do this rather than just letting
    // RegLoadKey create or load the existing hive because if the hive is new,
    // we need to apply security. 
    //
    fHiveExists = GetFileAttributesEx(
        lpHiveFilename,
        GetFileExInfoStandard,
        &fd );

    //
    // mount the hive
    //
    hr = MyRegLoadKeyEx(HKEY_USERS, lpHiveKeyName, lpHiveFilename, hKeyUser);

    if (FAILED(hr))
    {
        DebugMsg((DM_WARNING, TEXT("CreateClassHive: MyRegLoadKeyEx failed, hr = %08X"), hr));
        res = HRESULT_CODE(hr);
        goto Exit;
    } 

    //
    // If we succeeded, open the root
    //

    res = RegOpenKeyEx( HKEY_USERS,
                        lpHiveKeyName,
                        0,
                        WRITE_DAC | KEY_ENUMERATE_SUB_KEYS | READ_CONTROL,
                        &hkRoot);

    if (ERROR_SUCCESS != res) {
        MyRegUnLoadKey(HKEY_USERS, lpHiveKeyName);
        DebugMsg((DM_WARNING, TEXT("CreateClassHive: fail to open classes hive. Error %d"),res));
        goto Exit;
    }

    if (!fHiveExists || bNewlyIssued) {

        if (!fHiveExists) {
            DebugMsg((DM_VERBOSE, TEXT("CreateClassHive: existing user classes hive not found")));
        }
        else {
            DebugMsg((DM_VERBOSE, TEXT("CreateClassHive: user classes hive copied from Default User")));
        }

        //
        // This hive is newly issued i.e. either created fresh or copied from
        // "Default User" profile, so we need to set security on the new hive
        //

        //
        // set security on this hive
        //
        if (!SetDefaultUserHiveSecurity(lpProfile, NULL, hkRoot)) {
            res = GetLastError();
            DebugMsg((DM_WARNING, TEXT("CreateClassHive: Fail to assign proper security on new classes hive")));
        }
        
        //
        // If we succeed, set the hidden attribute on the backing hive file
        //
        if (ERROR_SUCCESS == res) {

            if (!SetFileAttributes (lpHiveFilename, FILE_ATTRIBUTE_HIDDEN)) {
                DebugMsg((DM_WARNING, TEXT("CreateClassHive: unable to set file attributes")
                          TEXT(" on classes hive %s with error %x"), lpHiveFilename, GetLastError()));
            }
        }

    } else {
        DebugMsg((DM_VERBOSE, TEXT("CreateClassHive: existing user classes hive found")));
    }

    

Exit:

    if (hKeyUser)
    {
        RegCloseKey(hKeyUser);
    }
    
    if (hkRoot) {
        RegCloseKey(hkRoot);
    }

    if (lpHiveKeyName)
        LocalFree(lpHiveKeyName);
        
    return res;
}


//*************************************************************
//
//  CreateUserClassesHive()
//
//  Purpose:    create the user-specific classes hive
//
//  Parameters: lpProfile -   Profile information
//              SidString -   User's Sid as a string
//              szLocalHiveDir - directory in userprofile 
//                               where hive should be located
//
//  Return:     ERROR_SUCCESS if successful,
//              other error if not
//
//  Comments:
//
//  History:    Date        Author     Comment
//              3/6/98      adamed     Created
//
//*************************************************************
LONG CreateUserClassesHive(
    LPPROFILE lpProfile,
    LPTSTR SidString,
    LPTSTR szLocalHiveDir,
    BOOL   bNewlyIssued)
{
    LPTSTR  lpHiveFilename = NULL;
    LONG    res;
    DWORD   cchHiveFilename;

    // allocate a space big enough for the hive filename (including trailing null)
    cchHiveFilename = lstrlen(szLocalHiveDir) +
                      lstrlen(USER_CLASSES_HIVE_NAME) +
                      1;
                      
    lpHiveFilename = (LPTSTR) LocalAlloc(LPTR, cchHiveFilename * sizeof(TCHAR));

    if ( !lpHiveFilename ) {
        res = ERROR_NOT_ENOUGH_MEMORY;
        goto Exit;
    }
    StringCchCopy( lpHiveFilename, cchHiveFilename, szLocalHiveDir);
    StringCchCat ( lpHiveFilename, cchHiveFilename, USER_CLASSES_HIVE_NAME);

    res = CreateClassHive(
        lpProfile,
        SidString,
        USER_CLASSES_HIVE_SUFFIX,
        lpHiveFilename,
        bNewlyIssued);

    if (ERROR_SUCCESS != res) {
        goto Exit;
    }

    res = MapUserClassesIntoUserHive(lpProfile, SidString);

Exit:
    if (lpHiveFilename)
        LocalFree(lpHiveFilename);
        
    return res;
}


//*************************************************************
//
//  MoveUserClassesBeforeMerge
//
//  Purpose:    move HKCU\Software\Classes before
//              MapUserClassesIntoUserHive() deletes it.
//
//  Parameters: lpProfile -   Profile information
//              lpcszLocalHiveDir - Temp Hive location
//
//  Return:     ERROR_SUCCESS if successful,
//              other error if not
//
//  Comments:
//
//  History:    Date        Author     Comment
//              5/6/99      vtan       Created
//
//*************************************************************
LONG MoveUserClassesBeforeMerge(
    LPPROFILE lpProfile,
    LPCTSTR lpcszLocalHiveDir)
{
    LONG    res;
    HKEY    hKeySource;

    // Open HKCU\Software\Classes and see if there is a subkey.
    // No subkeys would indicate that the move has already been
    // done or there is no data to move.

    res = RegOpenKeyEx(lpProfile->hKeyCurrentUser, CLASSES_CLSID_SUBTREE, 0, KEY_ALL_ACCESS, &hKeySource);
    if (ERROR_SUCCESS == res)
    {
        DWORD   dwSubKeyCount;

        if ((ERROR_SUCCESS == RegQueryInfoKey(hKeySource, NULL, NULL, NULL, &dwSubKeyCount, NULL, NULL, NULL, NULL, NULL, NULL, NULL)) &&
            (dwSubKeyCount > 0))
        {
            LPTSTR  pszLocalTempHive;
            DWORD   cchLocalTempHive;

            // Allocate enough space for the local hive directory and the temp hive filename.

            cchLocalTempHive = lstrlen(lpcszLocalHiveDir) + lstrlen(TEMPHIVE_FILENAME) + 1;
            pszLocalTempHive = (LPTSTR) LocalAlloc(LPTR, cchLocalTempHive * sizeof(TCHAR));

            // Get a path to a file to save HKCU\Software\Classes into.

            if (pszLocalTempHive != NULL)
            {
                HANDLE  hToken = NULL;
                BOOL    bAdjustPriv = FALSE;

                StringCchCopy(pszLocalTempHive, cchLocalTempHive, lpcszLocalHiveDir);
                StringCchCat (pszLocalTempHive, cchLocalTempHive, TEMPHIVE_FILENAME);

                // RegSaveKey() fails if the file exists so delete it first.

                DeleteFile(pszLocalTempHive);

                //
                // Check to see if we are impersonating.
                //

                if(!OpenThreadToken(GetCurrentThread(), TOKEN_READ, TRUE, &hToken) || hToken == NULL) {
                    bAdjustPriv = TRUE;
                }
                else {
                    CloseHandle(hToken);
                }

                if(!bAdjustPriv) {

                    DWORD   dwDisposition;
                    HKEY    hKeyTarget;
                    BOOL    fSavedHive;

                    // Save HKCU\Software\Classes into the temp hive
                    // and restore the state of SE_BACKUP_NAME privilege

                    res = RegSaveKey(hKeySource, pszLocalTempHive, NULL);
                    
                    if (ERROR_SUCCESS == res)
                    {
                        res = RegCreateKeyEx(lpProfile->hKeyCurrentUser, EXPLORER_CLASSES_SUBTREE, 0, TEXT(""), REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKeyTarget, &dwDisposition);
                        if (ERROR_SUCCESS == res)
                        {

                            // Restore temp hive to a new location at
                            // HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer
                            // This performs the upgrade from NT4 to NT5.

                            res = RegRestoreKey(hKeyTarget, pszLocalTempHive, 0);
                            if (ERROR_SUCCESS != res)
                            {
                                DebugMsg((DM_WARNING, TEXT("RegRestoreKey failed with error %d"), res));
                            }
                            RegCloseKey(hKeyTarget);
                        }
                        else
                        {
                            DebugMsg((DM_WARNING, TEXT("RegCreateKeyEx failed to create key %s with error %d"), EXPLORER_CLASSES_SUBTREE, res));
                        }
                    }
                    else
                    {
                        DebugMsg((DM_WARNING, TEXT("RegSaveKey failed with error %d"), res));
                    }
                }
                else {
                    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
                    {
                        DWORD               dwReturnTokenPrivilegesSize;
                        TOKEN_PRIVILEGES    oldTokenPrivileges, newTokenPrivileges;

                        // Enable SE_BACKUP_NAME privilege

                        if (LookupPrivilegeValue(NULL, SE_BACKUP_NAME, &newTokenPrivileges.Privileges[0].Luid))
                        {
                            newTokenPrivileges.PrivilegeCount = 1;
                            newTokenPrivileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                            if (AdjustTokenPrivileges(hToken, FALSE, &newTokenPrivileges, sizeof(newTokenPrivileges), &oldTokenPrivileges, &dwReturnTokenPrivilegesSize))
                            {
                                BOOL    fSavedHive;

                                // Save HKCU\Software\Classes into the temp hive
                                // and restore the state of SE_BACKUP_NAME privilege

                                res = RegSaveKey(hKeySource, pszLocalTempHive, NULL);
                                if (!AdjustTokenPrivileges(hToken, FALSE, &oldTokenPrivileges, 0, NULL, NULL))
                                {
                                    DebugMsg((DM_WARNING, TEXT("AdjustTokenPrivileges failed to restore old privileges with error %d"), GetLastError()));
                                }
                                if (ERROR_SUCCESS == res)
                                {

                                    // Enable SE_RESTORE_NAME privilege.

                                    if (LookupPrivilegeValue(NULL, SE_RESTORE_NAME, &newTokenPrivileges.Privileges[0].Luid))
                                    {
                                        newTokenPrivileges.PrivilegeCount = 1;
                                        newTokenPrivileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                                        if (AdjustTokenPrivileges(hToken, FALSE, &newTokenPrivileges, sizeof(newTokenPrivileges), &oldTokenPrivileges, &dwReturnTokenPrivilegesSize))
                                        {
                                            DWORD   dwDisposition;
                                            HKEY    hKeyTarget;

                                            res = RegCreateKeyEx(lpProfile->hKeyCurrentUser, EXPLORER_CLASSES_SUBTREE, 0, TEXT(""), REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKeyTarget, &dwDisposition);
                                            if (ERROR_SUCCESS == res)
                                            {

                                                // Restore temp hive to a new location at
                                                // HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer
                                                // This performs the upgrade from NT4 to NT5.

                                                res = RegRestoreKey(hKeyTarget, pszLocalTempHive, 0);
                                                if (ERROR_SUCCESS != res)
                                                {
                                                    DebugMsg((DM_WARNING, TEXT("RegRestoreKey failed with error %d"), res));
                                                }
                                                RegCloseKey(hKeyTarget);
                                            }
                                            else
                                            {
                                                DebugMsg((DM_WARNING, TEXT("RegCreateKeyEx failed to create key %s with error %d"), EXPLORER_CLASSES_SUBTREE, res));
                                            }
                                            if (!AdjustTokenPrivileges(hToken, FALSE, &oldTokenPrivileges, 0, NULL, NULL))
                                            {
                                                DebugMsg((DM_WARNING, TEXT("AdjustTokenPrivileges failed to restore old privileges with error %d"), GetLastError()));
                                            }
                                        }
                                        else
                                        {
                                            res = GetLastError();
                                            DebugMsg((DM_WARNING, TEXT("AdjustTokenPrivileges failed with error %d"), res));
                                        }
                                    }
                                    else
                                    {
                                        res = GetLastError();
                                        DebugMsg((DM_WARNING, TEXT("LookupPrivilegeValue failed with error %d"), res));
                                    }
                                }
                                else
                                {
                                    DebugMsg((DM_WARNING, TEXT("RegSaveKey failed with error %d"), res));
                                }
                            }
                            else
                            {
                                res = GetLastError();
                                DebugMsg((DM_WARNING, TEXT("AdjustTokenPrivileges failed with error %d"), res));
                            }
                        }
                        else
                        {
                            res = GetLastError();
                            DebugMsg((DM_WARNING, TEXT("LookupPrivilegeValue failed with error %d"), res));
                        }
                        CloseHandle(hToken);
                    }
                    else
                    {
                        res = GetLastError();
                        DebugMsg((DM_WARNING, TEXT("OpenProcessToken failed to get token with error %d"), res));
                    }
                } // if(!bAdjustPriv) else

                // Delete local temporary hive file.

                DeleteFile(pszLocalTempHive);

                LocalFree(pszLocalTempHive);
            }
            else
            {
                res = ERROR_NOT_ENOUGH_MEMORY;
                DebugMsg((DM_WARNING, TEXT("LocalAlloc failed to allocate temp hive path buffer")));
            }
        }
        RegCloseKey(hKeySource);
    }
    else if (ERROR_FILE_NOT_FOUND == res)
    {
        res = ERROR_SUCCESS;
    }
    return res;
}


//*************************************************************
//
//  LoadUserClasses()
//
//  Purpose:    Combines the HKLM\Software\Classes subtree with the
//              HKCU\Software\Classes subtree
//
//  Parameters: lpProfile -   Profile information
//              SidString -   User's Sid as a string
//
//  Return:     ERROR_SUCCESS if successful
//              other NTSTATUS if an error occurs
//
//*************************************************************
LONG LoadUserClasses( LPPROFILE lpProfile, LPTSTR SidString, BOOL bNewlyIssued)
{
    LONG   error;
    LPTSTR szLocalHiveDir = NULL;

    error = ERROR_SUCCESS;

    //
    // first, we will create a directory for the user-specific
    // classes hive -- we need memory for it:
    //
    szLocalHiveDir = (LPTSTR) LocalAlloc(LPTR, MAX_HIVE_DIR_CCH * sizeof(TCHAR));

    if (!szLocalHiveDir) {
        error =ERROR_NOT_ENOUGH_MEMORY;
        goto Exit;
    }

    //
    // create the directory for the user-specific classes hive
    //
    error = CreateClassesFolder(lpProfile, szLocalHiveDir, MAX_HIVE_DIR_CCH);

    if (ERROR_SUCCESS != error) {
        DebugMsg((DM_WARNING, TEXT("LoadUserClasses: Failed to create folder for combined hive (%d)."),
                 error));
        goto Exit;
    }

    // Move HKCU\Software\Classes before merging the two
    // branches. Ignore any errors here as this branch is
    // about to be deleted by the merge anyway.
    // The reason for this move is because NT4 stores customized
    // shell icons in HKCU\Software\Classes\CLSID\{CLSID_x} and
    // NT5 stores this at HKCU\Software\Microsoft\Windows\
    // CurrentVersion\Explorer\CLSID\{CLSID_x} and must be moved
    // now before being deleted.

    error = MoveUserClassesBeforeMerge(lpProfile, szLocalHiveDir);
    if (ERROR_SUCCESS != error) {
        DebugMsg((DM_WARNING, TEXT("MoveUserClassesBeforeMerge: Failed unexpectedly (%d)."),
                 error));
    }

    //
    // Now create the user classes hive
    //
    error = CreateUserClassesHive( lpProfile, SidString, szLocalHiveDir, bNewlyIssued);

    if (ERROR_SUCCESS != error) {
        DebugMsg((DM_WARNING, TEXT("LoadUserClasses: Failed to create user classes hive (%d)."),
                 error));
    }

Exit:
    if (szLocalHiveDir)
        LocalFree(szLocalHiveDir);
        
    return error;
}
