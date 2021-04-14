/*++

Microsoft Windows

Copyright (C) Microsoft Corporation, 1998 - 2001

Module Name:

    ndutil.cxx

Abstract:

    Common functions to be shared between the netdom features

--*/
#include "pch.h"
#pragma hdrstop
#include <netdom.h>

bool
CmdFlagOn(ARG_RECORD * rgNetDomArgs, NETDOM_ARG_ENUM eArgIndex)
{
   if (!rgNetDomArgs || eArgIndex >= eArgEnd)
   {
      ASSERT(FALSE);
      return false;
   }

   return rgNetDomArgs[eArgIndex].bDefined == TRUE;
}
 
BOOL
NetDompGetUserConfirmation(
    IN DWORD PromptResId,
    IN PWSTR pwzName
    )
/*++

Routine Description:

    Prompt the user to press the y or n button.

Arguments:

    PrompteResId - Resource ID of the prompt to be displayed
    pwzName - Optional name to put in the prompt string

Return Value:

    TRUE if the user pressed y or Y, FALSE otherwise.

--*/
{
    if (!PromptResId)
    {
       ASSERT(FALSE);
       return IDNO;
    }
    WCHAR wzBuf[MAX_PATH+1], wzTitle[31];
    PWSTR pwzMsg = NULL;
    int nRet = 0, cchPrompt = 0;

    cchPrompt = LoadString(g_hInstance, PromptResId, wzBuf, MAX_PATH);

    if (!cchPrompt)
    {
        printf("LoadString FAILED!\n");
        return FALSE;
    }

    if (!LoadString(g_hInstance, IDS_PROMPT_TITLE, wzTitle, 30))
    {
        printf("LoadString FAILED!\n");
        return FALSE;
    }

    if (pwzName)
    {
        size_t cchBuf = cchPrompt + wcslen(pwzName) + 1;
        if (NetApiBufferAllocate(cchBuf * sizeof(WCHAR),
                                 (PVOID*)&pwzMsg) != ERROR_SUCCESS)
        {
            printf("Memory allocation FAILED!\n");
            return FALSE;
        }

        HRESULT hr = StringCchPrintf(pwzMsg, cchBuf, wzBuf, pwzName);
        if (FAILED(hr))
        {
           ASSERT(FALSE);
           printf("NetDompGetUserConfirmation assertion failed!\n");
           return FALSE;
        }
    }
    else
    {
        pwzMsg = wzBuf;
    }

    nRet = MessageBox(GetFocus(),
                      pwzMsg,
                      wzTitle,
                      MB_YESNO | 
                          MB_ICONEXCLAMATION |
                          MB_DEFAULT_DESKTOP_ONLY |
                          MB_SETFOREGROUND |
                          MB_DEFBUTTON2);
    if (pwzName)
    {
        NetApiBufferFree(pwzMsg);
    }

    return (nRet == IDYES);
}


DWORD
NetDompGetPasswordString(
    DWORD PromptResId,
    PWSTR Buffer,
    ULONG BufferLength
    )
/*++

Routine Description:

    This function will get the password string for an account object.  It reads it from
    the user input

Arguments:

    PromptResId - Resource ID of the prompt to be displayed

    Buffer - The buffer in which to return the password

    BufferLength - Length of the buffer (in characters)

Return Value:

    ERROR_SUCCESS - The call succeeded

    NERR_BufTooSmall - The password entered was larger than would fit in the buffer

--*/
{
    ULONG Win32Err = ERROR_SUCCESS;
    DWORD CurrentMode, Read = 0;
    HANDLE InputHandle = GetStdHandle( STD_INPUT_HANDLE );
    PWSTR TempBuf = NULL;

    // Display the password prompt, if specified
    if ( PromptResId != 0 ) 
    {
        NetDompDisplayMessage( PromptResId );
    }

    BufferLength -= 1;    /* make space for null terminator */

    // If input is from console, turn off echo
    if ( FileIsConsole ( InputHandle ) )
    {
        if (!GetConsoleMode( InputHandle, &CurrentMode ) ) 
        {
            return GetLastError();
        }
        SetConsoleMode( InputHandle, ~( ENABLE_ECHO_INPUT )  & CurrentMode );
    }

    // Read the password and copy to Buffer
    Read = ReadFromIn ( &TempBuf );
    if ( (Read == -1) || ( (Read + 1) > BufferLength ) )
    {
        Win32Err = NERR_BufTooSmall;
    }
    else
    {
        HRESULT hr = StringCchCopy ( Buffer, Read + 1, TempBuf );
        if ( FAILED (hr) )
        {
            Win32Err = NERR_BufTooSmall;
        }
    }

    if ( TempBuf )
    {
        LocalFree ( TempBuf );
    }

    // Restore console echo setting
    if ( FileIsConsole ( InputHandle ) )
    {
        SetConsoleMode( InputHandle, CurrentMode );
    }

    // Clear the line for the next prompt
    if ( PromptResId ) {

        printf( "\n" );
    }
    return( Win32Err );
}


DWORD
NetDompGetDomainForOperation(ARG_RECORD * rgNetDomArgs,
                             PWSTR Server OPTIONAL,
                             BOOL CanDefaultToCurrent,
                             PWSTR *DomainName)
/*++

Routine Description:

    This function will get name of the domain for the current operation.  It does this
    by parsing the command line parameters.  If no argument is found, it uses the default
    domain for that machine

Arguments:

    Args - List of arguments, can be NULL if CanDefaultToCurrent is true

    Server - Optional name of servre for which we wish to have the default domain

    CanDefaultToCurrent - if TRUE and the domain name not specified on the command line,
                          use the current domain for the specified machine

    DomainName - Where the domain name is returned.  Freed via NetApiBufferFree().

Return Value:

    ERROR_SUCCESS - The call succeeded

    ERROR_INVALID_PARAMETER - The domain name was not specified and it was requried

--*/
{
    if (!DomainName)
    {
       ASSERT(DomainName);
       return ERROR_INVALID_PARAMETER;
    }

    DWORD Win32Err = ERROR_SUCCESS;
    ULONG i;
    NETSETUP_JOIN_STATUS JoinStatus;

    *DomainName = NULL;

    //
    // See the name is specifed
    //
    if (rgNetDomArgs && CmdFlagOn(rgNetDomArgs, eCommDomain)) {

        Win32Err = NetDompGetArgumentString(rgNetDomArgs,
                                            eCommDomain,
                                            DomainName);
    }

    //
    // If so, convert it
    //
    if ( Win32Err == ERROR_SUCCESS || CanDefaultToCurrent) {

        if ( *DomainName == NULL ) {

            if ( CanDefaultToCurrent ) {

                //
                // See if we can use the current domain
                //
                Win32Err = NetGetJoinInformation( Server,
                                                  DomainName,
                                                  &JoinStatus );

                if ( Win32Err == ERROR_SUCCESS ) {

                    if ( JoinStatus != NetSetupDomainName ) {

                        NetApiBufferFree( *DomainName );
                        NetDompDisplayMessage( MSG_NETDOM5_DOMAIN_REQUIRED );
                        Win32Err = ERROR_INVALID_PARAMETER;
                    }
                }
                else if (RPC_S_PROCNUM_OUT_OF_RANGE == Win32Err)
                {
                   // Must be an NT4 or earlier machine.
                   //
                   WKSTA_INFO_100 * pWkstaInfo = NULL;

                   Win32Err = NetWkstaGetInfo(Server, 100, (LPBYTE*)&pWkstaInfo);

                   if (NERR_Success == Win32Err)
                   {
                      if (!pWkstaInfo->wki100_langroup)
                      {
                         return ERROR_INVALID_PARAMETER;
                      }
                      Win32Err = NetApiBufferAllocate(
                         (wcslen(pWkstaInfo->wki100_langroup) + 1) * sizeof(WCHAR),
                         (PVOID*)DomainName);
                      if (ERROR_SUCCESS == Win32Err)
                      {
                         // NOTICE-2002/02/27-ericb - SecurityPush: buffer allocated large enough.
                         wcscpy(*DomainName, pWkstaInfo->wki100_langroup);
                      }
                      NetApiBufferFree(pWkstaInfo);
                   }
                }

            } else {

                NetDompDisplayMessage( MSG_NETDOM5_DOMAIN_REQUIRED );
                Win32Err = ERROR_INVALID_PARAMETER;
            }
        }

    } else {

        //
        // Hmm, guess we don't have what we need
        //
        NetDompDisplayMessage( MSG_NETDOM5_DOMAIN_REQUIRED );
        Win32Err = ERROR_INVALID_PARAMETER;
    }

    return( Win32Err );
}


typedef struct _ND5_USER_FLAG_MAP {

    NETDOM_ARG_ENUM UserFlag;
    NETDOM_ARG_ENUM PasswordFlag;
    ULONG PasswordPromptId;

}  ND5_USER_FLAG_MAP, *PND5_USER_FLAG_MAP;


DWORD
NetDompGetUserAndPasswordForOperation(ARG_RECORD * rgNetDomArgs,
                                      NETDOM_ARG_ENUM eUserType,
                                      PWSTR DefaultDomain,
                                      PND5_AUTH_INFO AuthIdent)
/*++

Routine Description:

    This function will get the user name and password from the command line, as required.  If
    necessary, the function will cause a prompt for the password to be displayed and processed

Arguments:

    eUserType - Whether to prompt for object or domain user

    DefaultDomain - Default domain for the operation, in case a relative name was supplied

    AuthIdent - Structure to initialize with the user name and password

Return Value:

    ERROR_SUCCESS - The call succeeded

    ERROR_INVALID_PARAMETER - The domain name was not specified and it was requried

--*/
{
    DWORD Win32Err = ERROR_SUCCESS;
    PWSTR SpecifiedUser = NULL, SpecifiedPassword = NULL;
    PWSTR pwzUserWoDomain = NULL, pwzUsersDomain = NULL;
    size_t cchDomain = 0;
    ND5_USER_FLAG_MAP FlagMap[] = {
        { eCommUserNameD, eCommPasswordD, MSG_NETDOM5_USERD_PWD },
        { eCommUserNameO, eCommPasswordO, MSG_NETDOM5_USERO_PWD },
        { eMoveUserNameF, eMovePasswordF, MSG_NETDOM5_USERF_PWD },
        };

    if (!rgNetDomArgs || !AuthIdent)
    {
       ASSERT(rgNetDomArgs);
       ASSERT(AuthIdent);
       return ERROR_INVALID_PARAMETER;
    }

    //
    // Return success if the name wasn't supplied
    //
    if (!CmdFlagOn(rgNetDomArgs, eUserType))
    {
        return(ERROR_SUCCESS);
    }

    // NOTICE-2002/02/27-ericb - SecurityPush: zeroing the structure pointed to by AuthIdent.
    RtlZeroMemory( AuthIdent, sizeof( ND5_AUTH_INFO ) );

    //
    // See if the name is specifed
    //
    SpecifiedUser = rgNetDomArgs[eUserType].strValue;

    //
    // If so, use it
    //
    if ( SpecifiedUser ) {

        size_t cchUser = 0;
        HRESULT hr = StringCchLength(SpecifiedUser, MAXSTR, &cchUser);
        if (FAILED(hr))
        {
           ASSERT(FALSE);
           return HRESULT_CODE(hr);
        }
        if ( wcschr( SpecifiedUser, L'\\' ) || wcschr( SpecifiedUser, L'@' ) ) {

            Win32Err = NetApiBufferAllocate((cchUser + 1) * sizeof(WCHAR),
                                            (PVOID*)&(AuthIdent->User));
            if ( Win32Err == ERROR_SUCCESS && AuthIdent->User) {

                // NOTICE-2002/02/27-ericb - SecurityPush: buffer allocated large enough.
                wcscpy(AuthIdent->User, SpecifiedUser);
            }
            else
            {
                return ERROR_NOT_ENOUGH_MEMORY;
            }

        } else {

            if (cchUser < 1) {

                Win32Err = NetApiBufferAllocate(sizeof(WCHAR), (PVOID*)&(AuthIdent->User));

                if (ERROR_SUCCESS == Win32Err && AuthIdent->User) {

                    *(AuthIdent->User) = L'\0';
                }
                else
                {
                    return ERROR_NOT_ENOUGH_MEMORY;
                }

            } else {

                if ( !DefaultDomain ) {

                    return ERROR_INVALID_PARAMETER;

                } else {

                    hr = StringCchLength(DefaultDomain, MAXSTR, &cchDomain);
                    if (FAILED(hr))
                    {
                       ASSERT(FALSE);
                       return HRESULT_CODE(hr);
                    }
                    size_t cchLength = cchDomain + 1 + cchUser + 1;
                    Win32Err = NetApiBufferAllocate((cchLength + 1) * sizeof(WCHAR),
                                                     (PVOID*)&(AuthIdent->User));

                    if ( Win32Err == ERROR_SUCCESS ) {

                        // NOTICE-2002/02/27-ericb - SecurityPush: buffer allocated large enough.
                        wcscpy( AuthIdent->User, DefaultDomain );
                        wcscat( AuthIdent->User, L"\\" );
                        wcscat( AuthIdent->User, SpecifiedUser);
                    }
                }
            }
        }

        if (pwzUserWoDomain = wcschr(AuthIdent->User, L'\\')) {

            cchDomain = pwzUserWoDomain - AuthIdent->User;

            Win32Err = NetApiBufferAllocate((cchDomain + 1) * sizeof(WCHAR),
                                            (PVOID*)&(AuthIdent->pwzUsersDomain));

            if ( Win32Err == ERROR_SUCCESS ) {

                wcsncpy(AuthIdent->pwzUsersDomain, AuthIdent->User, cchDomain);

                AuthIdent->pwzUsersDomain[cchDomain] = L'\0';

                pwzUserWoDomain++;

                if (pwzUserWoDomain) {

                    Win32Err = NetApiBufferAllocate((wcslen(pwzUserWoDomain) + 1) * sizeof(WCHAR),
                                                    (PVOID*)&(AuthIdent->pwzUserWoDomain));

                    if ( Win32Err == ERROR_SUCCESS ) {

                        // NOTICE-2002/02/27-ericb - SecurityPush: buffer allocated large enough.
                        wcscpy(AuthIdent->pwzUserWoDomain, pwzUserWoDomain);
                    }
                }
            }
        }

    } else {

        Win32Err = ERROR_INVALID_PARAMETER;
    }

    //
    // Now, the password if it exists
    //
    if ( Win32Err == ERROR_SUCCESS ) {

        NETDOM_ARG_ENUM ePasswordArg;
        ULONG PasswordPrompt = 0;

        for (ULONG i = 0; i < sizeof( FlagMap ) / sizeof( ND5_USER_FLAG_MAP ); i++ ) {

            if ( eUserType == FlagMap[ i ].UserFlag ) {

                ePasswordArg = FlagMap[ i ].PasswordFlag;
                PasswordPrompt = FlagMap[ i ].PasswordPromptId;
                break;
            }
        }

        ASSERT( ePasswordArg != eArgBegin );

        //
        // Now, get the password
        //
        SpecifiedPassword = rgNetDomArgs[ePasswordArg].strValue;

        if (SpecifiedPassword)
        {
           size_t cchPw = 0;
           HRESULT hr = StringCchLength(SpecifiedPassword, MAXSTR, &cchPw);
           if (FAILED(hr))
           {
              ASSERT(FALSE);
              return HRESULT_CODE(hr);
           }
           if (1 == cchPw && L'*' == *SpecifiedPassword) {

               //
               // Prompt for it...
               //
               Win32Err = NetApiBufferAllocate( ( PWLEN + 1 ) * sizeof( WCHAR ),
                                                (PVOID*)&( AuthIdent->Password ) );

               if ( Win32Err == ERROR_SUCCESS  && AuthIdent->Password) {

                   Win32Err = NetDompGetPasswordString( PasswordPrompt,
                                                        AuthIdent->Password,
                                                        PWLEN );
               }
               else
               {
                   return ERROR_NOT_ENOUGH_MEMORY;
               }

           } else {

               //
               // It's a password, so go with it..
               //
               Win32Err = NetApiBufferAllocate((cchPw + 1) * sizeof(WCHAR),
                                               (PVOID*)&(AuthIdent->Password));

               if ( Win32Err == ERROR_SUCCESS  && AuthIdent->Password) {

                   // NOTICE-2002/02/27-ericb - SecurityPush: buffer allocated large enough.
                   wcscpy(AuthIdent->Password, SpecifiedPassword);
               }
               else
               {
                   return ERROR_NOT_ENOUGH_MEMORY;
               }
           }
        }

        // Password not specified, create an empty one.
        //
        if( AuthIdent->Password == NULL )
        {
           Win32Err = NetApiBufferAllocate(sizeof(WCHAR), (PVOID*)&(AuthIdent->Password));

           if ( Win32Err == ERROR_SUCCESS && AuthIdent->Password) {

               *AuthIdent->Password = L'\0';
           }
           else
           {
               return ERROR_NOT_ENOUGH_MEMORY;
           }
        }
    }

    if ( Win32Err != ERROR_SUCCESS ) {

        NetDompFreeAuthIdent( AuthIdent );
    }

    return( Win32Err );
}


DWORD
NetDompGetArgumentString(ARG_RECORD * rgNetDomArgs,
                         NETDOM_ARG_ENUM eArgToGet,
                         PWSTR *ArgString)
/*++

Routine Description:

    This function will get the string associated with a command line parameter, if it exists.

Arguments:

    Args - List of arguments

    ArgToGet - Argument to get the string for

    ArgString - Where the arg string is returned if found.  Freed via NetApiBufferFree

Return Value:

    ERROR_SUCCESS - The call succeeded

--*/
{
   if (!rgNetDomArgs || !ArgString)
   {
      ASSERT(rgNetDomArgs);
      ASSERT(ArgString);
      return ERROR_INVALID_PARAMETER;
   }
   DWORD Win32Err = ERROR_SUCCESS;

   *ArgString = NULL;

   if (eArgToGet >= eArgEnd)
   {
      return ERROR_INVALID_PARAMETER;
   }

   if (!rgNetDomArgs[eArgToGet].strValue)
   {
      // Allow null strings.
      //
      return ERROR_SUCCESS;
   }

   // NOTICE-2002/02/27-ericb - SecurityPush: rgNetDomArgs strValue tested for
   // non-null above. If non-null, the string is assigned by ParseCmd.
   Win32Err = NetApiBufferAllocate((wcslen(rgNetDomArgs[eArgToGet].strValue) + 1) *
                                                                       sizeof(WCHAR),
                                   (PVOID*)ArgString);

   if ( Win32Err == ERROR_SUCCESS ) {

      wcscpy(*ArgString, rgNetDomArgs[eArgToGet].strValue);
   }

   return( Win32Err );
}


BOOL
NetDompGetArgumentBoolean(ARG_RECORD * rgNetDomArgs,
                          NETDOM_ARG_ENUM eArgToGet)
/*++

Routine Description:

    This function will determine whether the given command line argument was present on the
    command line or not.

Arguments:

    Args - List of arguments

    ArgToGet - Argument to get the string for

Return Value:

    TRUE - The argument was found

    FALSE - The argument wasn't found

--*/
{
   if (!rgNetDomArgs)
   {
      ASSERT(FALSE);
      return FALSE;
   }
   if (eArgToGet >= eArgEnd)
   {
      ASSERT(FALSE);
      return FALSE;
   }

   return rgNetDomArgs[eArgToGet].bDefined;
}



DWORD
NetDompControlService(
    IN PWSTR Server,
    IN PWSTR Service,
    IN DWORD ServiceOptions
    )
/*++

Routine Description:

    This function will control the given service on the given machine.

Arguments:

    Server - Machine on which to control the service

    Service - Service to control

    ServiceOptions - What do with the service.  Uses standard SvcCtrl bits

Return Value:

    ERROR_SUCCESS - The operation succeeded

--*/
{
    DWORD Win32Err = ERROR_SUCCESS;
    SC_HANDLE ScMgr, Svc;
    DWORD OpenMode;

    //
    // Open the service control manager
    //
    ScMgr = OpenSCManager( Server,
                           SERVICES_ACTIVE_DATABASE,
                           GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE );

    if ( ScMgr == NULL ) {

        Win32Err = GetLastError();

    } else {

        //
        // Set the open mode
        //
        if( FLAG_ON( ServiceOptions, SERVICE_STOP ) ) {

            LOG_VERBOSE(( MSG_VERBOSE_SVC_STOP, Service ));
            OpenMode = SERVICE_STOP                     |
                           SERVICE_ENUMERATE_DEPENDENTS |
                           SERVICE_QUERY_STATUS         |
                           SERVICE_CHANGE_CONFIG;

        } else if( FLAG_ON( ServiceOptions, SERVICE_START ) ) {

            LOG_VERBOSE(( MSG_VERBOSE_SVC_START, Service ));
            OpenMode = SERVICE_START;

        } else {

            LOG_VERBOSE(( MSG_VERBOSE_SVC_CONFIG, Service ));
            OpenMode = SERVICE_CHANGE_CONFIG | SERVICE_QUERY_CONFIG;

        }

        if ( FLAG_ON( ServiceOptions, SERVICE_STOP ) ) {

            Win32Err = NetpStopService( Service,
                                        ScMgr );

        }

        if ( Win32Err == ERROR_SUCCESS ) {

            //
            // Open the service
            //
            Svc = OpenService( ScMgr,
                               Service,
                               OpenMode );

            if ( Svc == NULL ) {

                Win32Err = GetLastError();

            } else {

                if ( FLAG_ON( ServiceOptions, SERVICE_START ) ) {

                    //
                    // See about changing its state
                    //
                    if ( StartService( Svc, 0, NULL  ) == FALSE ) {

                        Win32Err = GetLastError();

                        if ( Win32Err == ERROR_SERVICE_ALREADY_RUNNING ) {

                            Win32Err = ERROR_SUCCESS;

                        }

                    }


                } else {

                    if ( ChangeServiceConfig( Svc,
                                              SERVICE_NO_CHANGE,
                                              ServiceOptions,
                                              SERVICE_NO_CHANGE,
                                              NULL, NULL, 0, NULL, NULL, NULL,
                                              NULL ) == FALSE ) {

                        Win32Err = GetLastError();
                    }
                }

                CloseServiceHandle( Svc );
            }
        }

        CloseServiceHandle( ScMgr );
    }

    return( Win32Err );
}

DWORD
EnablePrivilege( PCWSTR PrivilegeName )
{
   HANDLE hToken;              // handle to process token 
   TOKEN_PRIVILEGES tkp;       // pointer to token structure 
 
 
   // Get the current process token handle so we can get the specified 
   // privilege. 
 
   if (!OpenProcessToken( GetCurrentProcess(), 
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, 
                          &hToken)) 
      return GetLastError();
    
 
   // Get the LUID for the specified privilege. 
 
   if (!LookupPrivilegeValue(NULL, PrivilegeName, 
                             &tkp.Privileges[0].Luid))
      return GetLastError();
 
   tkp.PrivilegeCount = 1;  // one privilege to set    
   tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED; 
 
   AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, 
                         (PTOKEN_PRIVILEGES) NULL, 0);
     
   // Cannot test the return value of AdjustTokenPrivileges. 

   CloseHandle(hToken);

   if ( GetLastError() != ERROR_SUCCESS ) 
      return GetLastError();

   return ERROR_SUCCESS;
}

DWORD
NetDompRestartAsRequired(ARG_RECORD * rgNetDomArgs,
                         PWSTR Machine,
                         PWSTR User,
                         DWORD PreliminaryStatus,
                         DWORD MsgID)
/*++

Routine Description:

    This function will (remotely) shutdown a machine if the command line arguments indicate
    that it should

Arguments:

    Args - List of arguments

    ArgCount - Number of arguments in the list

    Machine - Machine which should be restarted

    User - The user who connected to the machine, doing whatever operation needed a reboot.
           If NULL is specified, the current user is used.

    PreliminaryStatus - Status from the operation.  If it's not SUCCESS, the restart isn't
                        attempted

    MsgID - Message ID of string to display on system being shut down.

Return Value:

    ERROR_SUCCESS - The operation succeeded

--*/
{
    if (!rgNetDomArgs || !Machine || !MsgID)
    {
       ASSERT(rgNetDomArgs);
       ASSERT(Machine);
       ASSERT(MsgID);
       return ERROR_INVALID_PARAMETER;
    }
    DWORD Win32Err = PreliminaryStatus;
    BOOL Restart = FALSE;
    PWSTR UserName = NULL, DisplayString = NULL, Delay, End;
    ULONG Length = 0, RestartDelay = 30;

    if ( PreliminaryStatus != ERROR_SUCCESS ) {

        return( PreliminaryStatus );
    }

    //
    // See if the argument is specified
    //
    Restart = NetDompGetArgumentBoolean(rgNetDomArgs,
                                        eCommRestart);

    if ( Restart ) {

        //
        // Get the delay time
        //
        RestartDelay = rgNetDomArgs[eCommRestart].nValue;

        //
        // Get the user display name
        //
        if ( User ) {

            UserName = User;

        } else {

            // NOTICE-2002/02/27-ericb - SecurityPush: The first call to GetUserName returns
            // the length required which is used to allocate the buffer.
            Length = 0;
            GetUserName( UserName, &Length );

            Win32Err = NetApiBufferAllocate( Length * sizeof( WCHAR ),
                                             (PVOID*)&UserName );

            if ( Win32Err == ERROR_SUCCESS ) {

                if ( !GetUserName( UserName, &Length ) ) {

                    Win32Err = GetLastError();
                }
            }
        }

        //
        // Build the message to display, and schedule the shutdown
        //
        if ( Win32Err == ERROR_SUCCESS ) {

            // NOTICE-2002/02/27-ericb - SecurityPush: checked parameters at beginning
            // of the function.
            Length = FormatMessageW( FORMAT_MESSAGE_FROM_HMODULE |
                                        FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                        FORMAT_MESSAGE_ARGUMENT_ARRAY,
                                     NULL,
                                     MsgID,
                                     0,
                                     ( PWSTR )&DisplayString,
                                     0,
                                     ( va_list * )&UserName );
            if ( Length == 0 ) {

                Win32Err = GetLastError();

            } else {

                LOG_VERBOSE(( MSG_VERBOSE_REBOOTING, Machine ));
                // NTRAID#NTBUG9-620218-2002/05/13-ericb
                DWORD dwReason = SHTDN_REASON_FLAG_PLANNED |
                                 SHTDN_REASON_MINOR_RECONFIG |
                                 SHTDN_REASON_MAJOR_OPERATINGSYSTEM;
                //If Machine is localMachine, Enable SE_SHUTDOWN_NAME privilege
                if( IsLocalMachine( Machine ) ){
                     if( ( Win32Err = EnablePrivilege(SE_SHUTDOWN_NAME) ) == ERROR_SUCCESS ){
                        if ( InitiateSystemShutdownEx(Machine,
                                                      DisplayString,
                                                      RestartDelay,
                                                      TRUE,
                                                      TRUE,
                                                      dwReason) == FALSE ) {

                           Win32Err = GetLastError();
                        }
                     }
                }
                else
                {
                   if (InitiateSystemShutdownEx(Machine,
                                                DisplayString,
                                                RestartDelay,
                                                TRUE,
                                                TRUE,
                                                dwReason) == FALSE)
                   {
                      Win32Err = GetLastError();
                   }

                   if (RPC_S_PROCNUM_OUT_OF_RANGE == Win32Err)
                   {
                      // Machine must be running a pre-Win2k OS.
                      //
                      if (InitiateSystemShutdown(Machine,
                                                 DisplayString,
                                                 RestartDelay,
                                                 TRUE,
                                                 TRUE) == FALSE)
                      {
                         Win32Err = GetLastError();
                      }
                      else
                      {
                         Win32Err = NO_ERROR;
                      }
                   }
                }
            }
        }
    }

    if ( Win32Err != ERROR_SUCCESS ) {

        NetDompDisplayMessage( MSG_NO_RESTART );
        NetDompDisplayErrorMessage( Win32Err );
    }

    LocalFree( DisplayString );


    if ( UserName != User ) {

        NetApiBufferFree( UserName );
    }

    return( Win32Err );
}



DWORD
NetDompCheckDomainMembership(
    IN PWSTR Server,
    IN PND5_AUTH_INFO AuthInfo,
    IN BOOL EstablishSessionIfRequried,
    IN OUT BOOL * DomainMember
    )
/*++

Routine Description:

    This function will determine whether the specified machine is a member of a domain or not

Arguments:

    Server - Machine in question

    AuthInfo - User name and password used to connect to the machine if necessary

    EstablishSessionIfRequired - Establish an authenticated session to the machine if necessary

    DomainMember - Gets set to TRUE if the machine is a domain member.  Otherwise, it's FALSE.

Return Value:

    ERROR_SUCCESS - The operation succeeded

--*/
{
    DWORD Win32Err = ERROR_SUCCESS;
    NTSTATUS Status;
    LSA_HANDLE LsaHandle = NULL;
    PPOLICY_PRIMARY_DOMAIN_INFO PolicyPDI = NULL;
    OBJECT_ATTRIBUTES OA;
    UNICODE_STRING ServerU;


    *DomainMember = FALSE;

    //
    // Establish a session, if necessary
    //
    if ( Server && EstablishSessionIfRequried ) {

        LOG_VERBOSE(( MSG_VERBOSE_ESTABLISH_SESSION, Server ));
        Win32Err = NetpManageIPCConnect( Server,
                                         AuthInfo->User,
                                         AuthInfo->Password,
                                         NETSETUPP_CONNECT_IPC );
    }

    //
    // See if it's a domain member.  Use the LSA apis, since this might be an
    // NT4 box.
    //
    if ( Win32Err == ERROR_SUCCESS ) {

        if ( Server ) {

            RtlInitUnicodeString( &ServerU, Server );
        }

        InitializeObjectAttributes( &OA, NULL, 0, NULL, NULL );

        Status = LsaOpenPolicy( Server ? &ServerU : NULL,
                                &OA,
                                MAXIMUM_ALLOWED,
                                &LsaHandle );

        if ( NT_SUCCESS( Status ) ) {

            Status = LsaQueryInformationPolicy( LsaHandle,
                                                PolicyPrimaryDomainInformation,
                                                ( PVOID * )&PolicyPDI );

            if ( NT_SUCCESS( Status ) ) {

                if ( PolicyPDI->Sid ) {

                    *DomainMember = TRUE;
                }

                LsaFreeMemory( PolicyPDI );
            }

            LsaClose( LsaHandle );
        }

        Win32Err = RtlNtStatusToDosError( Status );
    }


    //
    // Tear down the session
    //
    if ( Server && EstablishSessionIfRequried ) {

        LOG_VERBOSE(( MSG_VERBOSE_DELETE_SESSION, Server ));
        NetpManageIPCConnect( Server,
                              AuthInfo->User,
                              AuthInfo->Password,
                              NETSETUPP_DISCONNECT_IPC );

    }

    return( Win32Err );
}


DWORD
NetDompValidateSecondaryArguments(ARG_RECORD * rgNetDomArgs,
                                  NETDOM_ARG_ENUM eFirstValidParam, ...)
    //PND5_ARG Args,
/*++

Routine Description:

    This function will determine whether the supplied command line options are
    valid for this operation or not

Return Value:

    ERROR_SUCCESS - The operation succeeded

    ERROR_INVALID_PARAMETER - A bad parameter was specified

--*/
{
   if (!rgNetDomArgs)
   {
      ASSERT(FALSE);
      return ERROR_INVALID_PARAMETER;
   }
   DWORD Win32Err = ERROR_SUCCESS;
   va_list ArgList;

   for (int i = eArgBegin; i < eArgEnd; i++)
   {
      if (rgNetDomArgs[i].bDefined)
      {
         int j = eFirstValidParam;
         bool fFound = false;
         va_start(ArgList, eFirstValidParam);

         while (j != eArgEnd)
         {
            if (i == j)
            {
               fFound = true;
               break;
            }
            j = va_arg(ArgList, int);
         }

         if (!fFound)
         {
            NetDompDisplayUnexpectedParameter(rgNetDomArgs[i].strArg1);
            return ERROR_INVALID_PARAMETER;
         }
      }
   }

   va_end(ArgList);
   return(Win32Err);
}



DWORD
NetDompGenerateRandomPassword(
    IN PWSTR Buffer,
    IN ULONG Length
    )
/*++

Routine Description:

    This function will generate a random password of the specified length

Arguments:

    Buffer - Place to put the randomly generated password

    Length - Length of the password (in characters) to generate


Return Value:

    ERROR_SUCCESS - The operation succeeded

--*/
{
    DWORD Win32Err = ERROR_SUCCESS;
    HCRYPTPROV CryptProvider = 0;
    LARGE_INTEGER Time;
    ULONG Seed, i;
    UCHAR Filler;

    NtQuerySystemTime( &Time );
    Seed = ( ( PLONG )( &Time ) )[ 0 ] ^ ( ( PLONG )( &Time ) )[ 1 ];

    Filler = ( UCHAR )( RtlRandom( &Seed ) % ( 254 ) + 1 );  // Generate a fill character to use

    //
    // Generate a random password.
    //
    if ( CryptAcquireContext( &CryptProvider,
                              NULL,
                              NULL,
                              PROV_RSA_FULL,
                              CRYPT_VERIFYCONTEXT ) ) {

        if ( CryptGenRandom( CryptProvider,
                              Length * sizeof( WCHAR ),
                              ( LPBYTE )Buffer ) ) {

            Buffer[ Length ] = UNICODE_NULL;

            //
            // Make sure there are no NULL's in the middle of the list
            //
            for ( i = 0; i < Length; i++ ) {

                if ( Buffer[ i ] == UNICODE_NULL ) {

                    Buffer[ i ] = Filler;
                }
            }

        } else {

            Win32Err = GetLastError();
        }

        CryptReleaseContext( CryptProvider, 0 );


    } else {

        Win32Err = GetLastError();
    }

    return( Win32Err );
}

VOID
NetDompFreeAuthIdent(
    IN PND5_AUTH_INFO pAuthIdent
    )
{
    if (pAuthIdent->User)
        NetApiBufferFree(pAuthIdent->User);
    if (pAuthIdent->Password)
        NetApiBufferFree(pAuthIdent->Password);
    if (pAuthIdent->pwzUserWoDomain)
        NetApiBufferFree(pAuthIdent->pwzUserWoDomain);
    if (pAuthIdent->pwzUsersDomain)
        NetApiBufferFree(pAuthIdent->pwzUsersDomain);
}

//+----------------------------------------------------------------------------
//
//  Function:  IsLocalMachine
//
//  Synopsis:  Checks if pwzMachine is the local machine.
//
//  NOTICE-2002/08/01-shasan - Added code so that function can be used 
//  even if the machine name passed in has \\ before it.
//
//-----------------------------------------------------------------------------
BOOL
IsLocalMachine(PCWSTR pwzMachine)
{
   if (!pwzMachine)
   {
      ASSERT(false);
      return FALSE;
   }

   // Skip the initial \\ if present
   if ( wcslen (pwzMachine) >= 2 )
   {
       if ( *pwzMachine == L'\\' && * (pwzMachine + 1) == L'\\' )
           pwzMachine += 2;
   }

   // NOTICE-2002/02/27-ericb - SecurityPush: the SDK docs say the buffer
   // should be large enough for MAX_COMPUTERNAME_LENGTH + 1 characters but
   // don't explicitly say if the null is included in that count. So add one
   // more to make sure there is room for the null.
   WCHAR szLocalComputer[MAX_COMPUTERNAME_LENGTH + 2];
   DWORD nSize = MAX_COMPUTERNAME_LENGTH + 1 ;

   WCHAR szLocalComputerDns[DNS_MAX_NAME_LENGTH + 2];
   DWORD nDnsSize = DNS_MAX_NAME_LENGTH + 1;

   if (!GetComputerName(szLocalComputer, &nSize))
      return FALSE;

   if (_wcsnicmp(pwzMachine, szLocalComputer, nSize) == 0)
      return TRUE;

   // The netBIOS name hasnt matched, check if the DNS name matches
   if ( !GetComputerNameEx ( ComputerNamePhysicalDnsFullyQualified, szLocalComputerDns, &nDnsSize ) )
       return FALSE;

   if ( _wcsnicmp ( pwzMachine, szLocalComputerDns, nDnsSize ) == 0)
       return TRUE;
   else
      return FALSE;
}


