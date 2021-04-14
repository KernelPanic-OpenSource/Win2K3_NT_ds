/*++

Copyright (c) 1990  Microsoft Corporation

Module Name:

    ncpbind.c

Abstract:

    Contains the RPC bind and un-bind routines for the NcpServer
    Service.

Author:

    Dan Lafferty (danl)     01-Mar-1991

Environment:

    User Mode -Win32

Revision History:

    01-Mar-1991 danl
        created
    07-Jun-1991 JohnRo
        Allowed debug output of failures.
    15-Nov-1993 Yi-Hsin
        Modify for NcpServer.

--*/

//
// INCLUDES
//
#include <nt.h>          // DbgPrint prototype
#include <rpc.h>         // DataTypes and runtime APIs
#include <ncpsvc.h>      // generated by the MIDL complier
#include <ntrpcp.h>      // Rpc utils
#include <srvnames.h>    // SERVER_INTERFACE_NAME



handle_t
NCPSVC_HANDLE_bind (
    NCPSVC_HANDLE   ServerName
    )

/*++

Routine Description:
    This routine calls a common bind routine that is shared by all services.
    This routine is called from the ncpserver service client stubs when
    it is necessary to bind to a server.

Arguments:

    ServerName - A pointer to a string containing the name of the server
        to bind with.

Return Value:

    The binding handle is returned to the stub routine.  If the
    binding is unsuccessful, a NULL will be returned.

--*/
{
    handle_t    bindingHandle = NULL;
    RPC_STATUS  status;

    status = RpcpBindRpc( ServerName,
                          SERVER_INTERFACE_NAME,
    //                      TEXT("Security=Impersonation Static True"),
                          TEXT("Security=Impersonation Dynamic False"),
                          &bindingHandle );

#if DBG
    if ( status != RPC_S_OK )
        KdPrint(("NCPSVC_HANDLE_bind: RpcpBindRpc failed status=%lC\n",status));
#endif

    return( bindingHandle );
}



void
NCPSVC_HANDLE_unbind (
    NCPSVC_HANDLE   ServerName,
    handle_t        BindingHandle
    )
/*++

Routine Description:

    This routine calls a common unbind routine that is shared by
    all services.
    This routine is called from the ncpserver service client stubs when
    it is necessary to unbind to a server.


Arguments:

    ServerName - This is the name of the server from which to unbind.

    BindingHandle - This is the binding handle that is to be closed.

Return Value:

    none.

--*/
{
    UNREFERENCED_PARAMETER( ServerName );     // This parameter is not used

    RpcpUnbindRpc ( BindingHandle );
}
