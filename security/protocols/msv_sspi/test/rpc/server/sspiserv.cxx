/*++

Copyright (c) 2001 Microsoft Corporation
All rights reserved.

Module Name:

    sspiserv.cxx

Abstract:

    sspiserv

Author:

    Larry Zhu (LZhu)                       Januray 1, 2002

Revision History:

--*/

#include <stdlib.h>
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <rpc.h>    // RPC data structures and APIs
#include <hresult.hxx>
#include <ntsecapi.h>
#include <output.hxx>
#include <lsasspi.hxx>

#include "sspitest.h"    // header file generated by MIDL compiler

void Usage(char * pszProgramName)
{
    fprintf(stderr, "Usage:  %s\n", pszProgramName);
    fprintf(stderr, " -p protocol_sequence\n");
    fprintf(stderr, " -e endpoint\n");
    fprintf(stderr, " -o options\n");
    fprintf(stderr, " -s authn service\n");
    fprintf(stderr, " -t target service principal name\n");
    exit(1);
}

HANDLE TerminateEvent;
ULONG AuthnService = RPC_C_AUTHN_GSS_NEGOTIATE;

INT __cdecl
main(
    INT argc,
    PSTR argv[]
    )
{
    RPC_STATUS status;
    PSTR pszProtocolSequence = "ncacn_ip_tcp";
    PSTR pszEndpoint = "10";
    PSTR pszOptions = NULL;
    PSTR pszStringBinding = NULL;
    PSTR PrincipalName = NULL;
    int i;
    DWORD WaitStatus;

    // allow the user to override settings with command line switches
    for (i = 1; i < argc; i++)
    {
        if ((*argv[i] == '-') || (*argv[i] == '/'))
        {
            switch (tolower(*(argv[i]+1)))
            {
            case 'p':  // protocol sequence
                pszProtocolSequence = argv[++i];
                break;
            case 'e':
                pszEndpoint = argv[++i];
                break;
            case 'o':
                pszOptions = argv[++i];
                break;
            case 's':
                AuthnService = strtol(argv[++i], NULL, 0);
                break;
            case 't':
                PrincipalName = argv[++i];
                break;
            case 'h':
            case '?':
            default:
                Usage(argv[0]);
            }
        }
        else
        {
            Usage(argv[0]);
        }
    }

    //
    // Create an event to wait on
    //

    TerminateEvent = CreateEventA(
        NULL,  // No security attributes
        TRUE,  // Must be manually reset
        FALSE, // Initially not signaled
        NULL   // No name
        );

    if (TerminateEvent == NULL)
    {
        printf( "Couldn't CreateEvent %ld\n", GetLastError() );
        return 2;
    }


    printf("Server using protseq %s endpoint %s\n", pszProtocolSequence, pszEndpoint);
    status = RpcServerUseProtseqEpA(
        (UCHAR*) pszProtocolSequence,
        3, // maximum concurrent calls
        (UCHAR*)pszEndpoint,
        0
        );
    if (status)
    {
        printf("RpcServerUseProtseqEp returned 0x%x\n", status);
        exit(2);
    }

    status = RpcServerRegisterIf(srv_sspitest_ServerIfHandle, 0, 0);
    if (status)
    {
        printf("RpcServerRegisterIf returned 0x%x\n", status);
        exit(2);
    }

    printf("RpcServerRegisterAuthInfoA AuthnService %#x, PrincipalName %s\n", AuthnService, PrincipalName);

    status = RpcServerRegisterAuthInfoA(
        (UCHAR*) PrincipalName,
        AuthnService,
        NULL,  // GetKeyFn
        NULL // Arg to GetKeyFn
        );

    if (status)
    {
        printf("RpcServerRegisterAuthInfo returned 0x%x\n", status);
        exit(2);
    }

    status = RpcServerInqDefaultPrincNameA(
        AuthnService,
        (UCHAR**)&PrincipalName
        );
    if (status)
    {
        printf("RpcServerInqDefaultPrincName returned %d\n", status);
        exit(2);
    }

    printf("RpcServerInqDefaultPrincNameA obtained AuthnService %#x, PrincipalName %s\n", AuthnService, PrincipalName);

    printf("Calling RpcServerListen\n");
    status = RpcServerListen(
        1, //MinimumCallThreads
        12345, // MaxCalls
        TRUE // DontWait
        );
    if (status)
    {
        printf("RpcServerListen returned: 0x%x\n", status);
        exit(2);
    }

    WaitStatus = WaitForSingleObject(TerminateEvent, INFINITE);

    if ( WaitStatus != WAIT_OBJECT_0)
    {
        printf("Couldn't WaitForSingleObject %ld %ld\n", WaitStatus, GetLastError());
        return 2;
    }

    return ERROR_SUCCESS;

} /* end main() */


// ====================================================================
//                MIDL allocate and free
// ====================================================================


void __RPC_FAR * __RPC_API
MIDL_user_allocate(size_t len)
{
    return(malloc(len));
}

void __RPC_API
MIDL_user_free(void __RPC_FAR * ptr)
{
    free(ptr);
}

ULONG
RecurseRemoteCall(
    ULONG Options,
    PSTR RemoteAddress,
    PSTR RemoteProtocol,
    PSTR RemoteEndpoint,
    PSTR Principal,
    PSTR Address,
    ULONG AuthnLevel,
    ULONG AuthnSvc,
    ULONG RecursionLevel
    )
{
    PSTR pszStringBinding = NULL;
    RPC_STATUS status;
    handle_t BindingHandle;

    // Use a convenience function to concatenate the elements of
    // the string binding into the proper sequence.

    status = RpcStringBindingComposeA(NULL,
        (UCHAR*) RemoteProtocol,
        (UCHAR*) RemoteAddress,
        (UCHAR*) RemoteEndpoint,
        NULL, // no network options
        (UCHAR**) &pszStringBinding);

    if (status)
    {
        printf("RpcStringBindingCompose returned %d\n", status);
        return(status);
    }
    printf("pszStringBinding = %s\n", pszStringBinding);

    //
    // Set the binding handle that will be used to bind to the server.
    //

    status = RpcBindingFromStringBindingA((UCHAR*) pszStringBinding,
        &BindingHandle);

    RpcStringFree((UCHAR**) &pszStringBinding);

    if (status)
    {
        printf("RpcBindingFromStringBinding returned %d\n", status);
        return (status);
    }

    //
    // Tell RPC to do the security thing.
    //

    printf("Binding auth info set to level %d, service %d, principal %s\n",
        AuthnLevel, AuthnService, Principal);
    status = RpcBindingSetAuthInfoA(
        BindingHandle,
        (UCHAR*)Principal,
        AuthnLevel,
        AuthnService,
        NULL,  // no SID
        RPC_C_AUTHZ_NAME);

    if (status)
    {
        printf("RpcBindingSetAuthInfo returned %ld\n", status);
        return( status );
    }

    //
    // Do the actual RPC calls to the server.
    //

    RpcTryExcept
    {
        status = RemoteCall(
            BindingHandle,
            Options,
            (UCHAR*) Address,
            (UCHAR*) RemoteProtocol,
            (UCHAR*) RemoteEndpoint,
            (UCHAR*) Principal,
            (UCHAR*) RemoteAddress,
            AuthnLevel,
            AuthnService,
            RecursionLevel
            );
        if (status != ERROR_SUCCESS)
        {
            printf("RemoteCall failed: 0x%x\n",status);
        }
    }
    RpcExcept(EXCEPTION_EXECUTE_HANDLER)
    {
        printf("Runtime library reported an exception %d\n", RpcExceptionCode());

    } RpcEndExcept

    // The calls to the remote procedures are complete.
    // Free the binding handle

    status = RpcBindingFree(&BindingHandle);  // remote calls done; unbind
    if (status)
    {
        printf("RpcBindingFree returned %d\n", status);
        exit(2);
    }

    return status;
}

ULONG
srv_RemoteCall(
    handle_t BindingHandle,
    ULONG Options,
    UCHAR* RemoteAddress,
    UCHAR* RemoteProtocol,
    UCHAR* RemoteEndpoint,
    UCHAR* Principal,
    UCHAR* Address,
    ULONG AuthnLevel,
    ULONG AuthnSvc,
    ULONG RecursionLevel
    )
{
    RPC_STATUS RpcStatus;
    CHAR ClientName[100];
    ULONG NameLen = sizeof(ClientName);

    RpcStatus = RpcImpersonateClient( NULL );

    if ( RpcStatus != RPC_S_OK )
    {
        printf( "RpcImpersonateClient Failed %ld\n", RpcStatus );
        goto Cleanup;
    }
    GetUserName(ClientName,&NameLen);
    printf("Recursion %d: Client called: name = %s\n", RecursionLevel, ClientName);

    if (RecursionLevel != 0)
    {
        RpcStatus = RecurseRemoteCall(
            Options,
            (PSTR) RemoteAddress,
            (PSTR) RemoteProtocol,
            (PSTR) RemoteEndpoint,
            (PSTR) Principal,
            (PSTR) Address,
            AuthnLevel,
            AuthnSvc,
            RecursionLevel - 1
            );
    }
    (void) RpcRevertToSelf(); // could fail?

Cleanup:

    return(RpcStatus);
}

void
srv_Shutdown(
    handle_t BindingHandle
    )
{
    RPC_STATUS status;

    status = RpcMgmtStopServerListening(NULL);
    if (status)
    {
        printf("RpcMgmtStopServerListening returned: 0x%x\n", status);
        exit(2);
    }

    status = RpcServerUnregisterIf(NULL, NULL, FALSE);
    if (status)
    {
        printf("RpcServerUnregisterIf returned 0x%x\n", status);
        exit(2);
    }

    if ( !SetEvent( TerminateEvent) )
    {
        printf("Couldn't SetEvent %ld\n", GetLastError());
    }
}

unsigned long srv_ReadRegistryValueData(
    /* [in] */ handle_t BindingHandle,
    /* [in] */ unsigned long RootKeyLower,
    /* [unique][string][in] */ unsigned char *pszRegistryKey,
    /* [unique][string][in] */ unsigned char *pszRegistryValue,
    /* [in] */ unsigned long cbBuf,
    /* [size_is][unique][out][in] */ unsigned char *pBuf,
    /* [out] */ unsigned long *pDataType,
    /* [out] */ unsigned long *pcbReturned)
{
    THResult hRetval;

    HKEY KeyRoot = ( HKEY ) (ULONG_PTR)((LONG)(0x80000000 | RootKeyLower));  // open registry key to Lsa\MSV1_0

    HKEY KeyHandle = NULL;

    ULONG RegValueType = 0;
    ULONG RegValue = 0;
    ULONG RegValueSize = 0;

    hRetval DBGCHK = HResultFromWin32(RpcImpersonateClient(BindingHandle));

    if (SUCCEEDED(hRetval))
    {
        DebugPrintf(SSPI_LOG, "*********srv_ReadRegistryValueData checking client user data************\n");
        hRetval DBGCHK = CheckUserData();
    }

    if (SUCCEEDED(hRetval))
    {
        hRetval DBGCHK = HResultFromWin32(RegOpenKey(KeyRoot, (PSTR) pszRegistryKey, &KeyHandle));
    }

    if (SUCCEEDED(hRetval))
    {
        RegValueSize = sizeof(RegValue);

        hRetval DBGCHK = HResultFromWin32(
            RegQueryValueExA(
                KeyHandle,
                (PSTR) pszRegistryValue,
                0, // reserved
                &RegValueType,
                pBuf,
                &cbBuf
                ));
    }

    if (SUCCEEDED(hRetval))
    {
        DebugPrintf(SSPI_LOG, "srv_ReadRegistryValueData KeyRoot %#x, Key %s, Value %s, type %#x\n", KeyRoot, pszRegistryKey, pszRegistryValue, RegValueType);
        DebugPrintHex(SSPI_LOG, "ValueData", cbBuf, pBuf);

        *pcbReturned = cbBuf;
    }

    (void) RpcRevertToSelf(); // could fail?

    return HRESULT_CODE(hRetval);
}
