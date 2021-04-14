
#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include <shellapi.h>
#include "SmartPtr.h"
#include "strings.h"
#include <strsafe.h>

extern "C"
{
BOOL PrependToPath( LPWSTR, LPWSTR*);
void PathUnquoteSpaces( LPWSTR );
void UpdateUserEnvironment();
LPWSTR GetSidString( HANDLE UserToken );
void DeleteSidString( LPWSTR SidString );
};

#define GPO_SCRIPTS_KEY L"Software\\Policies\\Microsoft\\Windows\\System\\Scripts"
#define GP_STATE_KEY    L"Software\\Microsoft\\Windows\\CurrentVersion\\Group Policy\\State"
#define ARRAYSIZE(a) (sizeof(a)/sizeof(a[0]))

#define SCRIPT          L"Script"
#define PARAMETERS      L"Parameters"
#define EXECTIME        L"ExecTime"
#define GPOID           L"GPO-ID"
#define SOMID           L"SOM-ID"
#define FILESYSPATH     L"FileSysPath"

#define SCR_STARTUP     L"Startup"
#define SCR_SHUTDOWN    L"Shutdown"
#define SCR_LOGON       L"Logon"
#define SCR_LOGOFF      L"Logoff"

LPTSTR
CheckSlash (LPTSTR lpDir)
{
    LPTSTR lpEnd;

    lpEnd = lpDir + lstrlen(lpDir);

    if (*(lpEnd - 1) != TEXT('\\')) {
        *lpEnd =  TEXT('\\');
        lpEnd++;
        *lpEnd =  TEXT('\0');
    }

    return lpEnd;
}

PSID
GetUserSid( HANDLE UserToken )
{
    PTOKEN_USER pUser, pTemp;
    PSID pSid;
    DWORD BytesRequired = 200;
    NTSTATUS status;


    //
    // Allocate space for the user info
    //

    pUser = (PTOKEN_USER)LocalAlloc(LMEM_FIXED, BytesRequired);


    if ( pUser == NULL )
    {
        return NULL;
    }


    //
    // Read in the UserInfo
    //

    status = NtQueryInformationToken(
                 UserToken,                 // Handle
                 TokenUser,                 // TokenInformationClass
                 pUser,                     // TokenInformation
                 BytesRequired,             // TokenInformationLength
                 &BytesRequired             // ReturnLength
                 );

    if ( status == STATUS_BUFFER_TOO_SMALL )
    {

        //
        // Allocate a bigger buffer and try again.
        //

        pTemp = (PTOKEN_USER)LocalReAlloc(pUser, BytesRequired, LMEM_MOVEABLE);
        if ( pTemp == NULL )
        {
            LocalFree (pUser);
            return NULL;
        }

        pUser = pTemp;

        status = NtQueryInformationToken(
                     UserToken,             // Handle
                     TokenUser,             // TokenInformationClass
                     pUser,                 // TokenInformation
                     BytesRequired,         // TokenInformationLength
                     &BytesRequired         // ReturnLength
                     );

    }

    if ( !NT_SUCCESS(status) )
    {
        LocalFree(pUser);
        return NULL;
    }


    BytesRequired = RtlLengthSid(pUser->User.Sid);
    pSid = (PSID)LocalAlloc(LMEM_FIXED, BytesRequired);
    if ( pSid == NULL )
    {
        LocalFree(pUser);
        return NULL;
    }


    status = RtlCopySid(BytesRequired, pSid, pUser->User.Sid);

    LocalFree(pUser);

    if ( !NT_SUCCESS(status) )
    {
        LocalFree(pSid);
        pSid = NULL;
    }


    return pSid;
}

LPWSTR
GetSidString( HANDLE UserToken )
{
    NTSTATUS NtStatus;
    PSID UserSid;
    UNICODE_STRING UnicodeString;

    //
    // Get the user sid
    //
    UserSid = GetUserSid( UserToken );
    if ( !UserSid )
    {
        return 0;
    }

    //
    // Convert user SID to a string.
    //
    NtStatus = RtlConvertSidToUnicodeString(&UnicodeString,
                                            UserSid,
                                            (BOOLEAN)TRUE ); // Allocate
    LocalFree( UserSid );

    if ( !NT_SUCCESS(NtStatus) )
    {
        return 0;
    }

    return UnicodeString.Buffer ;
}

void
DeleteSidString( LPWSTR SidString )
{
    UNICODE_STRING String;

    RtlInitUnicodeString( &String, SidString );
    RtlFreeUnicodeString( &String );
}

typedef BOOL  (*PFNSHELLEXECUTEEX)(LPSHELLEXECUTEINFO lpExecInfo);

DWORD
ExecuteScript(  LPWSTR  szCmdLine,
                LPWSTR  szArgs,
                LPWSTR  szWorkingDir,
                BOOL    bSync,
                BOOL    bHide,
                BOOL    bRunMin,
                LPWSTR  szType,
                PFNSHELLEXECUTEEX pfnShellExecuteEx,
                HANDLE  hEventLog )
{
    WCHAR   szCmdLineEx[MAX_PATH];
    WCHAR   szArgsEx[3 * MAX_PATH];
    WCHAR   szCurDir[MAX_PATH];
    LPWSTR  szOldPath = 0;
    BOOL    bResult;
    DWORD   dwError = ERROR_SUCCESS;;
    SHELLEXECUTEINFO ExecInfo;

    if ( GetSystemDirectory( szCurDir, ARRAYSIZE( szCurDir ) ) )
    {
        bResult = SetCurrentDirectory( szCurDir );

        if ( ! bResult )
        {
            dwError = GetLastError();
        }
    }
    else
    {
        dwError = GetLastError();
    }

    if ( ERROR_SUCCESS != dwError )
    {
        goto ExecuteScript_Exit;
    }

    //
    // Expand the command line and args
    //
    DWORD cchExpanded;
    
    cchExpanded = ExpandEnvironmentStrings( szCmdLine, szCmdLineEx, ARRAYSIZE(szCmdLineEx ) );

    if ( cchExpanded > 0 )
    {
        cchExpanded = ExpandEnvironmentStrings( szArgs, szArgsEx, ARRAYSIZE(szArgsEx) );
    }

    if ( 0 == cchExpanded )
    {
        dwError = GetLastError();
        goto ExecuteScript_Exit;
    }

    //
    // Put the working directory on the front of the PATH
    // environment variable
    //
    bResult = PrependToPath( szWorkingDir, &szOldPath );

    if ( ! bResult )
    {
        dwError = GetLastError();

        goto ExecuteScript_Exit;
    }

    //
    // Run the script
    //
    PathUnquoteSpaces( szCmdLineEx );

    ZeroMemory(&ExecInfo, sizeof(ExecInfo));
    ExecInfo.cbSize = sizeof(ExecInfo);
    ExecInfo.fMask = SEE_MASK_DOENVSUBST |
                     SEE_MASK_FLAG_NO_UI |
                     SEE_MASK_NOZONECHECKS |
                     SEE_MASK_NOCLOSEPROCESS;
    ExecInfo.lpFile = szCmdLineEx;
    ExecInfo.lpParameters = !szArgsEx[0] ? 0 : szArgsEx;
    ExecInfo.lpDirectory = szWorkingDir;

    if ( bHide )
    {
        ExecInfo.nShow = SW_HIDE;
    }
    else
    {
        ExecInfo.nShow = (bRunMin ? SW_SHOWMINNOACTIVE : SW_SHOWNORMAL );
    }

    bResult = pfnShellExecuteEx( &ExecInfo );
    dwError = GetLastError();

    //
    // Try to put the PATH environment variable back the way it was
    // If this fails, we have to continue
    //
    if ( szOldPath )
    {
        SetEnvironmentVariable( L"PATH", szOldPath );
        LocalFree( szOldPath );
        szOldPath = 0;
    }

    if ( bResult )
    {
        dwError = 0;
        if (bSync)
        {
            WaitForSingleObject(ExecInfo.hProcess, INFINITE);
            UpdateUserEnvironment();
        }
        CloseHandle(ExecInfo.hProcess);
    }
    else
    {
        if ( hEventLog != 0 )
        {
            LPWSTR szMsgBuf[2] = { (LPTSTR) ExecInfo.lpFile, 0 };

            FormatMessage( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
                           0,
                           dwError,
                           0,
                           (LPTSTR) (&szMsgBuf[1]),
                           1,
                           0);

            ReportEvent(hEventLog,
                        EVENTLOG_ERROR_TYPE,
                        0,
                        SHELLEXEC_ERROR,
                        0,
                        2,
                        0,
                        (LPCTSTR*) szMsgBuf,
                        0);
            if ( szMsgBuf[1] )
            {
                LocalFree( szMsgBuf[1] );
            }
        }
    }

ExecuteScript_Exit:

    return dwError;
}

ScrExecGPOFromReg(  HKEY hKeyGPO,
                    HKEY hKeyStateGPO,
                    BOOL bSync,
                    BOOL bHidden,
                    BOOL bRunMin,
                    LPWSTR  szType,
                    PFNSHELLEXECUTEEX pfnShellExecuteEx,
                    HANDLE  hEventLog )
{
    DWORD   dwError = ERROR_SUCCESS;
    DWORD   cSubKeys = 0;
    WCHAR   szFileSysPath[3*MAX_PATH];
    DWORD   dwType;
    DWORD   dwSize;
    HRESULT hr = S_OK;
    
    //
    // FILESYSPATH
    // 
    dwType = REG_SZ;
    dwSize = sizeof( szFileSysPath );
    dwError = RegQueryValueEx(  hKeyGPO,
                                FILESYSPATH,
                                0,
                                &dwType,
                                (LPBYTE) szFileSysPath,
                                &dwSize );
    if ( dwError != ERROR_SUCCESS )
    {
        return dwError;
    }

    hr = StringCchCat( szFileSysPath, sizeof(szFileSysPath)/sizeof(WCHAR), L"\\Scripts\\" );
    if(FAILED(hr)){
        SetLastError(HRESULT_CODE(hr));
        return HRESULT_CODE(hr);
    }
    hr = StringCchCat( szFileSysPath, sizeof(szFileSysPath)/sizeof(WCHAR), szType );
    if(FAILED(hr)){
        SetLastError(HRESULT_CODE(hr));
        return HRESULT_CODE(hr);
    }

    //
    // get the numer of Scripts
    //
    dwError = RegQueryInfoKey(  hKeyGPO,
                                0,
                                0,
                                0,
                                &cSubKeys,
                                0,
                                0,
                                0,
                                0,
                                0,
                                0,
                                0 );
    if ( dwError == ERROR_SUCCESS )
    {
        //
        // for every Script
        //
        for ( DWORD dwIndex = 0 ; dwIndex < cSubKeys ; dwIndex++ )
        {
            XKey    hKeyScript;
            XKey    hKeyStateScript;
            WCHAR   szTemp[32];

            dwError = RegOpenKeyEx( hKeyStateGPO,
                                    _itow( dwIndex, szTemp, 16 ),
                                    0,
                                    KEY_ALL_ACCESS,
                                    &hKeyStateScript );
            if ( dwError != ERROR_SUCCESS )
            {
                return dwError;
            }
                                        
            //
            // open the Script key (we need only read perms)
            //
            dwError = RegOpenKeyEx( hKeyGPO,
                                    szTemp,
                                    0,
                                    KEY_READ,
                                    &hKeyScript );
            if ( dwError != ERROR_SUCCESS )
            {
                return dwError;
            }

            WCHAR   szScript[MAX_PATH];
            WCHAR   szParameters[MAX_PATH];
            SYSTEMTIME  execTime;

            //
            // script
            // 
            dwType = REG_SZ;
            dwSize = sizeof( szScript );
            dwError = RegQueryValueEx(  hKeyScript,
                                        SCRIPT,
                                        0,
                                        &dwType,
                                        (LPBYTE) szScript,
                                        &dwSize );
            if ( dwError != ERROR_SUCCESS )
            {
                break;
            }

            //
            // parameters
            // 
            dwType = REG_SZ;
            dwSize = sizeof( szParameters );
            dwError = RegQueryValueEx(  hKeyScript,
                                        PARAMETERS,
                                        0,
                                        &dwType,
                                        (LPBYTE) szParameters,
                                        &dwSize );
            if ( dwError != ERROR_SUCCESS )
            {
                break;
            }

            //
            // execute script
            //
            GetSystemTime( &execTime );
            dwError = ExecuteScript(szScript,
                                    szParameters,
                                    szFileSysPath,
                                    bSync,
                                    bHidden,
                                    bRunMin,
                                    szType,
                                    pfnShellExecuteEx,
                                    hEventLog );
            if ( dwError != ERROR_SUCCESS )
            {
                ZeroMemory( &execTime, sizeof( execTime ) );
            }

            //
            // write exec time
            // 
            RegSetValueEx(  hKeyStateScript,
                            EXECTIME,
                            0,
                            REG_QWORD,
                            (LPBYTE) &execTime,
                            sizeof( execTime ) );
        }
    }

    return dwError;
}

extern "C" DWORD
ScrExecGPOListFromReg(  LPWSTR szType,
                        BOOL bMachine,
                        BOOL bSync,
                        BOOL bHidden,
                        BOOL bRunMin,
                        HANDLE  hEventLog )
{
    DWORD   dwError = ERROR_SUCCESS;
    WCHAR   szBuffer[MAX_PATH];
    XKey    hKeyType;
    XKey    hKeyState;
    XKey    hKeyStateType;
    HRESULT hr = S_OK;

    //
    // create the following key
    // HKLM\Software\Microsoft\Windows\CurrentVersion\Group Policy\State\<Target>\Scripts\<Type>
    //
    hr = StringCchCopy( szBuffer, sizeof(szBuffer)/sizeof(WCHAR), GP_STATE_KEY L"\\" );
    if(FAILED(hr))
    {
        SetLastError(HRESULT_CODE(hr));
        return HRESULT_CODE(hr);
    }

    if ( bMachine )
    {
        hr = StringCchCat( szBuffer, sizeof(szBuffer)/sizeof(WCHAR), L"Machine\\Scripts" );
        if(FAILED(hr)){
            SetLastError(HRESULT_CODE(hr));
            return HRESULT_CODE(hr);
        }
    }
    else
    {
        XHandle hToken;

        if ( !OpenProcessToken( GetCurrentProcess(),
                                TOKEN_ALL_ACCESS,
                                &hToken ) )
        {
            return GetLastError();
        }

        LPWSTR szSid = GetSidString( hToken );

        if ( !szSid )
        {
            return GetLastError();
        }

        hr = StringCchCat( szBuffer, sizeof(szBuffer)/sizeof(WCHAR), szSid );
        if(FAILED(hr)){
            SetLastError(HRESULT_CODE(hr));
            return HRESULT_CODE(hr);
        }

        hr = StringCchCat( szBuffer, sizeof(szBuffer)/sizeof(WCHAR), L"\\Scripts" );
        if(FAILED(hr)){
            SetLastError(HRESULT_CODE(hr));
            return HRESULT_CODE(hr);
        }

        DeleteSidString( szSid );
    }

    //
    // state
    //
    dwError = RegOpenKeyEx( HKEY_LOCAL_MACHINE,
                            szBuffer,
                            0,
                            KEY_ALL_ACCESS,
                            &hKeyState );
    if ( dwError != ERROR_SUCCESS )
    {
        return dwError;
    }

    dwError = RegOpenKeyEx( hKeyState,
                            szType,
                            0,
                            KEY_ALL_ACCESS,
                            &hKeyStateType );
    if ( dwError != ERROR_SUCCESS )
    {
        return dwError;
    }

    //
    // construct "Software\\Policies\\Microsoft\\Windows\\System\\Scripts\\<Type>
    //
    hr = StringCchCopy( szBuffer, sizeof(szBuffer)/sizeof(WCHAR), GPO_SCRIPTS_KEY L"\\" );
    if(FAILED(hr)){
        SetLastError(HRESULT_CODE(hr));
        return HRESULT_CODE(hr);
    }

    hr = StringCchCat( szBuffer, sizeof(szBuffer)/sizeof(WCHAR), szType );
    if(FAILED(hr)){
        SetLastError(HRESULT_CODE(hr));
        return HRESULT_CODE(hr);
    }

    //
    // open the key
    //
    dwError = RegOpenKeyEx( bMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,
                            szBuffer,
                            0,
                            KEY_READ,
                            &hKeyType );
    if ( dwError != ERROR_SUCCESS )
    {
        return dwError;
    }
    DWORD   cSubKeys = 0;

    //
    // get the numer of GPOs
    //
    dwError = RegQueryInfoKey(  hKeyType,
                                0,
                                0,
                                0,
                                &cSubKeys,
                                0,
                                0,
                                0,
                                0,
                                0,
                                0,
                                0 );
    if ( dwError != ERROR_SUCCESS )
    {
        return dwError;
    }

    HINSTANCE hShell32;
    PFNSHELLEXECUTEEX pfnShellExecuteEx = NULL;

    hShell32 = LoadLibrary( L"shell32.dll" );

    if ( hShell32 )
    {
        pfnShellExecuteEx = (PFNSHELLEXECUTEEX) GetProcAddress( hShell32, "ShellExecuteExW" );
    }

    if ( !pfnShellExecuteEx )
    {
        return GetLastError();
    }
  
    //
    // for every GPO
    //
    for ( DWORD dwIndex = 0 ; dwIndex < cSubKeys ; dwIndex++ )
    {
        XKey hKeyGPO;
        XKey hKeyStateGPO;

        //
        // open the state GPO key
        //
        dwError = RegOpenKeyEx( hKeyStateType,
                                _itow( dwIndex, szBuffer, 16 ),
                                0,
                                KEY_ALL_ACCESS,
                                &hKeyStateGPO );
        if ( dwError != ERROR_SUCCESS )
        {
            break;
        }

        //
        // open the policy GPO key (we need only read perms)
        //
        dwError = RegOpenKeyEx( hKeyType,
                                szBuffer,
                                0,
                                KEY_READ,
                                &hKeyGPO );
        if ( dwError != ERROR_SUCCESS )
        {
            break;
        }

        //
        // execute all scripts in the GPO
        //
        DWORD dwExecError;
        dwExecError = ScrExecGPOFromReg(hKeyGPO,
                                        hKeyStateGPO,
                                        bSync,
                                        bHidden,
                                        bRunMin,
                                        szType,
                                        pfnShellExecuteEx,
                                        hEventLog );
        if ( dwExecError != ERROR_SUCCESS )
        {
            dwError = dwExecError;
        }
    }

    return dwError;
}


