#include "main.h"
#define  PCOMMON_IMPL
#include "pcommon.h"

//*************************************************************
//
//  CheckSlash()
//
//  Purpose:    Checks for an ending slash and adds one if
//              it is missing.
//
//  Parameters: lpDir   -   directory
//
//  Return:     Pointer to the end of the string
//
//  Comments:
//
//  History:    Date        Author     Comment
//              6/19/95     ericflo    Created
//
//*************************************************************
LPTSTR CheckSlash (LPTSTR lpDir)
{
    DWORD dwStrLen;
    LPTSTR lpEnd;

    lpEnd = lpDir + lstrlen(lpDir);

    if (*(lpEnd - 1) != TEXT('\\')) {
        *lpEnd =  TEXT('\\');
        lpEnd++;
        *lpEnd =  TEXT('\0');
    }

    return lpEnd;
}

//*************************************************************
//
//  RegCleanUpValue()
//
//  Purpose:    Removes the target value and if no more values / keys
//              are present, removes the key.  This function then
//              works up the parent tree removing keys if they are
//              also empty.  If any parent key has a value / subkey,
//              it won't be removed.
//
//  Parameters: hKeyRoot    -   Root key
//              lpSubKey    -   SubKey
//              lpValueName -   Value to remove
//
//
//  Return:     TRUE if successful
//              FALSE if an error occurs
//
//*************************************************************

BOOL RegCleanUpValue (HKEY hKeyRoot, LPTSTR lpSubKey, LPTSTR lpValueName)
{
    TCHAR szDelKey[2 * MAX_PATH];
    LPTSTR lpEnd;
    DWORD dwKeys, dwValues;
    LONG lResult;
    HKEY hKey;


    //
    // Make a copy of the subkey so we can write to it.
    //

    lstrcpy (szDelKey, lpSubKey);


    //
    // First delete the value
    //

    lResult = RegOpenKeyEx (hKeyRoot, szDelKey, 0, KEY_WRITE, &hKey);

    if (lResult == ERROR_SUCCESS)
    {
        lResult = RegDeleteValue (hKey, lpValueName);

        RegCloseKey (hKey);

        if (lResult != ERROR_SUCCESS)
        {
            if (lResult != ERROR_FILE_NOT_FOUND)
            {
                DebugMsg((DM_WARNING, TEXT("RegCleanUpKey:  Failed to delete value <%s> with %d."), lpValueName, lResult));
                return FALSE;
            }
        }
    }

    //
    // Now loop through each of the parents.  If the parent is empty
    // eg: no values and no other subkeys, then remove the parent and
    // keep working up.
    //

    lpEnd = szDelKey + lstrlen(szDelKey) - 1;

    while (lpEnd >= szDelKey)
    {

        //
        // Find the parent key
        //

        while ((lpEnd > szDelKey) && (*lpEnd != TEXT('\\')))
            lpEnd--;


        //
        // Open the key
        //

        lResult = RegOpenKeyEx (hKeyRoot, szDelKey, 0, KEY_READ, &hKey);

        if (lResult != ERROR_SUCCESS)
        {
            if (lResult == ERROR_FILE_NOT_FOUND)
            {
                goto LoopAgain;
            }
            else
            {
                DebugMsg((DM_WARNING, TEXT("RegCleanUpKey:  Failed to open key <%s> with %d."), szDelKey, lResult));
                return FALSE;
            }
        }

        //
        // See if there any any values / keys
        //

        lResult = RegQueryInfoKey (hKey, NULL, NULL, NULL, &dwKeys, NULL, NULL,
                         &dwValues, NULL, NULL, NULL, NULL);

        RegCloseKey (hKey);

        if (lResult != ERROR_SUCCESS)
        {
            DebugMsg((DM_WARNING, TEXT("RegCleanUpKey:  Failed to query key <%s> with %d."), szDelKey, lResult));
            return FALSE;
        }


        //
        // Exit now if this key has values or keys
        //

        if ((dwKeys != 0) || (dwValues != 0))
        {
            return TRUE;
        }

        RegDeleteKey (hKeyRoot, szDelKey);

LoopAgain:
        //
        // If we are at the beginning of the subkey, we can leave now.
        //

        if (lpEnd == szDelKey)
        {
            return TRUE;
        }


        //
        // There is a parent key.  Remove the slash and loop again.
        //

        if (*lpEnd == TEXT('\\'))
        {
            *lpEnd = TEXT('\0');
        }
    }

    return TRUE;
}
