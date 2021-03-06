//+-----------------------------------------------------------------------
//
// Microsoft Windows
//
// Copyright (c) Microsoft Corporation 2000
//
// File:        userapi.cxx
//
// Contents:    User-mode APIs to the NtDigest security package
//
//              Main user mode entry points into this dll:
//                SpUserModeInitialize
//                SpInstanceInit
//                SpDeleteUserModeContext
//                SpInitUserModeContext
//                SpMakeSignature
//                SpVerifySignature
//                SpSealMessage
//                SpUnsealMessage
//                SpGetContextToken
//                SpQueryContextAttributes
//                SpCompleteAuthToken
//                SpFormatCredentials
//                SpMarshallSupplementalCreds
//                SpExportSecurityContext
//                SpImportSecurityContext
//
//              Helper functions:
//                SspCreateTokenDacl
//                SspMapContext (this is called in Lsa mode)
//
// History:     ChandanS 26-Jul-1996   Stolen from kerberos\client2\userapi.cxx
//              KDamour  18Mar00       Stolen from NTLM userapi.cxx
//
//------------------------------------------------------------------------

//
//  This area is still under determination as to support for userlevel functions
//

#include "global.h"
#include <stdio.h>         // For sprintf

#if (DBG | DBG2)
#define TEMPSIZE 4000
#endif

// Winsock-ish host/network byte order converters for short and long integers.
//
#define htons(x)        ((((x) >> 8) & 0x00FF) | (((x) << 8) & 0xFF00))

#define htonl(x)        ((((x) >> 24) & 0x000000FFL) | \
                        (((x) >>  8) & 0x0000FF00L) | \
                        (((x) <<  8) & 0x00FF0000L) | \
                        (((x) << 24) & 0xFF000000L))



//+-------------------------------------------------------------------------
//
//  Function:   SpUserModeInitialize
//
//  Synopsis:   Initialize an the Digest DLL in a client's
//              address space also called in LSA
//
//  Effects:
//
//  Arguments:  LsaVersion - Version of the security dll loading the package
//              PackageVersion - Version of the Digest package
//              UserFunctionTable - Receives a copy of Digests's user mode
//                  function table
//              pcTables - Receives count of tables returned.
//
//  Requires:
//
//  Returns:    STATUS_SUCCESS  - normal completion
//              STATUS_INVALID_PARAMETER - LsaVersion specified is incorrect
//
//  Notes: 
//
//
//--------------------------------------------------------------------------
NTSTATUS
SEC_ENTRY
SpUserModeInitialize(
    IN ULONG    LsaVersion,
    OUT PULONG  PackageVersion,
    OUT PSECPKG_USER_FUNCTION_TABLE * UserFunctionTable,
    OUT PULONG  pcTables
    )
{
#if DBG
    DebugInitialize();
#endif

    DebugLog((DEB_TRACE_FUNC, "SpUserModeInitialize: Entering\n" ));

    NTSTATUS Status = STATUS_SUCCESS;

    if (LsaVersion != SECPKG_INTERFACE_VERSION)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto CleanUp;
    }

    *PackageVersion = SECPKG_INTERFACE_VERSION;

    g_NtDigestUserFuncTable.InstanceInit          = SpInstanceInit;
    g_NtDigestUserFuncTable.MakeSignature         = SpMakeSignature;
    g_NtDigestUserFuncTable.VerifySignature       = SpVerifySignature;
    g_NtDigestUserFuncTable.SealMessage           = SpSealMessage;
    g_NtDigestUserFuncTable.UnsealMessage         = SpUnsealMessage;
    g_NtDigestUserFuncTable.GetContextToken       = SpGetContextToken;
    g_NtDigestUserFuncTable.QueryContextAttributes = SpQueryContextAttributes;
    g_NtDigestUserFuncTable.CompleteAuthToken     = SpCompleteAuthToken;
    g_NtDigestUserFuncTable.InitUserModeContext   = SpInitUserModeContext;
    g_NtDigestUserFuncTable.DeleteUserModeContext = SpDeleteUserModeContext;
    g_NtDigestUserFuncTable.FormatCredentials     = SpFormatCredentials;
    g_NtDigestUserFuncTable.MarshallSupplementalCreds = SpMarshallSupplementalCreds;
    g_NtDigestUserFuncTable.ExportContext         = SpExportSecurityContext;
    g_NtDigestUserFuncTable.ImportContext         = SpImportSecurityContext;

    *UserFunctionTable = &g_NtDigestUserFuncTable;
    *pcTables = 1;

CleanUp:
    DebugLog((DEB_TRACE_FUNC, "SpUserModeInitialize: Leaving    Status 0x%x\n", Status));
    return(Status);
}




//+-------------------------------------------------------------------------
//
//  Function:   SpInstanceInit
//
//  Synopsis:   Initialize an instance of the NtDigest package in a client's
//              address space. Also called once in LSA
//
//  Effects:
//
//  Arguments:  Version - Version of the security dll loading the package
//                         and it is Unused and Un-initialized
//              FunctionTable - Contains helper routines for use by NtDigest
//                         and it is fixed static
//              UserFunctions - Receives a copy of NtDigest's user mode
//                  function table - NOPE - has No information at all
//
//  Requires:
//
//  Returns:    STATUS_SUCCESS
//
//  Notes: 
//
//
//--------------------------------------------------------------------------
NTSTATUS NTAPI
SpInstanceInit(
    IN ULONG Version,
    IN PSECPKG_DLL_FUNCTIONS DllFunctionTable,
    OUT PVOID * UserFunctionTable
    )
{
    DebugLog((DEB_TRACE_FUNC, "SpInstanceInit: Entering\n" ));
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(UserFunctionTable);
    UNREFERENCED_PARAMETER(Version);

    // Save the Alloc/Free functions
    // Check if called in LSA or from Usermode - LSA calls SPInitialize then SPInstanceInit

    if (g_NtDigestState != NtDigestLsaMode)
    {
        g_NtDigestState = NtDigestUserMode;   // indicate in user address space
    }
    g_UserFunctions = DllFunctionTable;

    //  Initialize reading of registry and load in values
    NtDigestInitReadRegistry();

    // Need to initialize Crypto stuff and nonce creations
    Status = NonceInitialize();
    if (!NT_SUCCESS (Status))
    {
        DebugLog((DEB_ERROR, "SpInstanceInit: Error from NonceInitialize is %d\n", Status));
        goto CleanUp;
    }

    //
    // Init the UserMode Context stuff
    //
    Status = UserCtxtHandlerInit();
    if (!NT_SUCCESS (Status))
    {
        DebugLog((DEB_ERROR, "SpInstanceInit: Error from UserCtxtHandlerInit 0x%x\n", Status));
        goto CleanUp;
    }

    //
    // Read in the registry values for SSP configuration - in user mode space
    //
    SPLoadRegOptions();

CleanUp:

    DebugLog((DEB_TRACE_FUNC, "SpInstanceInit: Leaving    Status = 0x%lx\n", Status ));
    return(Status);
}




//+-------------------------------------------------------------------------
//
//  Function:   SpDeleteUserModeContext
//
//  Synopsis:   Deletes a user mode context by unlinking it and then
//              dereferencing it.
//
//  Effects:
//
//  Arguments:  ContextHandle - Lsa context handle of the context to delete
//
//  Requires:
//
//  Returns:    STATUS_SUCCESS on success, STATUS_INVALID_HANDLE if the
//              context can't be located
//
//  Notes:
//        If this is an exported context, send a flag back to the LSA so that
//        Lsa does not call the SecpDeleteSecurityContext in the lsa process
//
//
//--------------------------------------------------------------------------
NTSTATUS NTAPI
SpDeleteUserModeContext(
    IN ULONG_PTR ContextHandle
    )
{
    DebugLog((DEB_TRACE_FUNC, "SpDeleteUserModeContext: Entering   ContextHandle 0x%lx\n", ContextHandle ));
    PDIGEST_USERCONTEXT pUserContext = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    //
    // Find the currently existing user context and dereference the app SecurityContext Handle
    //
    Status = UserCtxtHandlerHandleToContext(ContextHandle, TRUE, FALSE, &pUserContext);
    if (!NT_SUCCESS(Status))
    {
        //
        // pContext is legally NULL when we are dealing with an incomplete
        // context.  This can often be the case when the second call to
        // InitializeSecurityContext() fails.
        //
        ///        Status = STATUS_INVALID_HANDLE;
        Status = STATUS_SUCCESS;
        DebugLog((DEB_WARN, "SpDeleteUserModeContext: UserCtxtHandlerHandleToContext not found 0x%x\n", Status ));
        goto CleanUp;
    }

    Status = UserCtxtHandlerRelease(pUserContext);
    if (!NT_SUCCESS(Status))
    {
        DebugLog((DEB_ERROR, "SpDeleteUserModeContext: UserCtxtHandlerRelease error  Status 0x%x\n", Status ));
    }

CleanUp:

    DebugLog((DEB_TRACE_FUNC, "SpDeleteUserModeContext: Leaving ContextHandle 0x%lx    status 0x%x\n",
               ContextHandle, Status ));
    return(Status);
}



//+-------------------------------------------------------------------------
//
//  Function:   SpInitUserModeContext
//
//  Synopsis:   Creates/updates a user-mode context from a packed LSA mode context
//
//  Effects:
//
//  Arguments:  ContextHandle - Lsa mode context handle for the context
//              PackedContext - A marshalled buffer containing the LSA
//                  mode context.
//
//  Requires:
//
//  Returns:    STATUS_SUCCESS or STATUS_INSUFFICIENT_RESOURCES
//
//  Notes:     This function is called from ISC() or ASC() when there is Context
//    data to map over to Usermode Application space.  This context might be a partial
//    context (which is not valid until fully auth'ed) or an update to indicate
//    that ASC() has provided the application with a re-connect (so you just increment the
//    ref count on the context if it already exists)
//
//
//--------------------------------------------------------------------------


NTSTATUS NTAPI
SpInitUserModeContext(
    IN ULONG_PTR ContextHandle,
    IN PSecBuffer PackedContext
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    NTSTATUS StatusSub = STATUS_SUCCESS;
    PDIGEST_USERCONTEXT pContext = NULL;
    PDIGEST_USERCONTEXT pContextSearch = NULL;
    PDIGEST_PACKED_USERCONTEXT pPackedUserContext = NULL;
    BOOLEAN fRefCount = FALSE;

    DebugLog((DEB_TRACE_FUNC, "SpInitUserModeContext: Entering  ContextHandle 0x%lx\n", ContextHandle ));
    
    ASSERT(PackedContext);


    // If Marshalled data is too small for holding a Client Context - reject it
    if (PackedContext->cbBuffer < sizeof(DIGEST_PACKED_USERCONTEXT))
    {
        Status = STATUS_INVALID_PARAMETER;
        DebugLog((DEB_ERROR, "SpInitUserModeContext:  ContextData size < DIGEST_PACKED_USERCONTEXT\n" ));
        goto CleanUp;
    }

    pPackedUserContext = (PDIGEST_PACKED_USERCONTEXT) DigestAllocateMemory(PackedContext->cbBuffer);
    if (!pPackedUserContext)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        DebugLog((DEB_ERROR, "SpInitUserModeContext: DigestAllocateMemory for Packed Copy returns NULL\n" ));
        goto CleanUp;
    }

    // Copy the Packed User Context from LSA to local memory so it wil be long word aligned
    memcpy(pPackedUserContext, PackedContext->pvBuffer, PackedContext->cbBuffer);

    DebugLog((DEB_TRACE, "SpInitUserModeContext: FlagOptions 0x%x\n", pPackedUserContext->ulFlags));

    // Check to see if Context should be refcounted.  With session reconnect - this will be set
    // since a new SecurityContext Handle will be returned from ASC().  If an OldSecurityContextHandle
    // is passed into ASC() then this will not be set
    if (pPackedUserContext->ulFlags & FLAG_CONTEXT_REFCOUNT)
    {
        fRefCount = TRUE;
        DebugLog((DEB_TRACE, "SpInitUserModeContext: RefCounting Context\n"));
    }

    // Check to see if this is an update of reference count or add new context
    // If the context is found, increment the application SecurityContext Handle ref count
    Status = UserCtxtHandlerHandleToContext(ContextHandle, FALSE, fRefCount, &pContextSearch);
    if (NT_SUCCESS(Status))
    {
        // Found the app user SecurityContext - just update as needed
        // Most of the calls from ASC() will be to update and complete a partial context here
        // Some will be for a simple ref-count on a reconnect as called with ASC() return SEC_I_COMPLETE_NEEDED
        // No need to release the pContextSearch since in both cases we want to increment ref count from ASC() call

        DebugLog((DEB_TRACE, "SpInitUserModeContext: Found UserContext - update and ref count\n"));

        if (pContextSearch->ulFlags & FLAG_CONTEXT_PARTIAL)
        {
            // update the context as it is only a initial partial context and not used yet
            Status = DigestUnpackContext(pPackedUserContext, pContextSearch);
            if (!NT_SUCCESS(Status))
            {
                DebugLog((DEB_ERROR, "SpInitUserModeContext: DigestUnpackContext for update context error 0x%x\n", Status));
                goto CleanUp;
            }

            UserContextPrint(pContextSearch);
        }

    }
    else if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        DebugLog((DEB_TRACE, "SpInitUserModeContext: UserContextInit creating 0x%x\n", pContext));

        // Now we will unpack this transfered LSA context into UserMode space Context List
        pContext = (PDIGEST_USERCONTEXT) DigestAllocateMemory( sizeof(DIGEST_USERCONTEXT) );
        if (!pContext)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            DebugLog((DEB_ERROR, "SpInitUserModeContext: DigestAllocateMemory returns NULL\n" ));
            goto CleanUp;
        }

        Status = UserCtxtInit(pContext);
        if (!NT_SUCCESS(Status))
        {
            DebugLog((DEB_ERROR, "SpInitUserModeContext: UserContextInit error 0x%x\n", Status));
            goto CleanUp;
        }

        // Store the location of the context in the LSA
        pContext->LsaContext =  ContextHandle;
        pContext->ulNC = 1;                    // Force to one to account for ISC/ASC first message verify
        pContext->lReferenceHandles = 1;       // Indicate that a handle has been given to the application

        Status = DigestUnpackContext(pPackedUserContext, pContext);
        if (!NT_SUCCESS(Status))
        {
            DebugLog((DEB_ERROR, "SpInitUserModeContext: DigestUnpackContext for new context error 0x%x\n", Status));
            goto CleanUp;
        }

        UserContextPrint(pContext);

        // App SecurityContext not located - add in a new one for this call
        Status = UserCtxtHandlerInsertCred(pContext);
        if (!NT_SUCCESS(Status))
        {
            DebugLog((DEB_ERROR, "SpInitUserModeContext: UserCtxtHandlerInsertCred error  status 0x%x\n", Status));
            goto CleanUp;
        }

        pContext = NULL;   // turned memory over to CtxtHandler

        DebugLog((DEB_TRACE, "SpInitUserModeContext: (RefCount) created & listed 0x%x\n", pContext));
    }
    else
    {
        Status = StatusSub;
        DebugLog((DEB_ERROR, "SpInitUserModeContext: Could not find UserContextHandle  Status 0x%x\n", Status));
        goto CleanUp;
    }


CleanUp:

    if (pContext)
    {
        // Release the User context on error if allocated
        UserCtxtFree(pContext);
        pContext = NULL;
    }

    if (pContextSearch)
    {
        StatusSub = UserCtxtHandlerRelease(pContextSearch);
        if (!NT_SUCCESS(StatusSub))
        {
            if (NT_SUCCESS(Status))
            {
                Status = StatusSub;               // replace status only on success
            }
            DebugLog((DEB_ERROR, "SpInitUserModeContext: UserCtxtHandlerInsertCred error  statussub 0x%x\n", StatusSub));
        }
    }

    if (pPackedUserContext)
    {
        DigestFreeMemory(pPackedUserContext);
        pPackedUserContext = NULL;
    }

    // Let FreeContextBuffer handle freeing the virtual allocs

    if (PackedContext->pvBuffer != NULL)
    {
        FreeContextBuffer(PackedContext->pvBuffer);
        PackedContext->pvBuffer = NULL;
        PackedContext->cbBuffer = 0;
    }

    DebugLog((DEB_TRACE_FUNC, "SpInitUserModeContext: Leaving      status 0x%x\n", Status ));
    return(Status);
}



//+-------------------------------------------------------------------------
//
//  Function:   SpMakeSignature
//
//  Synopsis:   Signs a message buffer by calculating a checksum over all
//              the non-read only data buffers and encrypting the checksum
//              along with a nonce.
//
//  Effects:
//
//  Arguments:  ContextHandle - Handle of the context to use to sign the
//                      message.
//              QualityOfProtection - Unused flags.
//              MessageBuffers - Contains an array of buffers to sign and
//                      to store the signature.
//              MessageSequenceNumber - Sequence number for this message,
//                      only used in datagram cases.
//
//  Requires:   STATUS_INVALID_HANDLE - the context could not be found or
//                      was not configured for message integrity.
//              STATUS_INVALID_PARAMETER - the signature buffer could not
//                      be found.
//              STATUS_BUFFER_TOO_SMALL - the signature buffer is too small
//                      to hold the signature
//
//  Returns:
//
//  Notes: This was stolen from net\svcdlls\ntlmssp\client\sign.c ,
//         routine SspHandleSignMessage. It's possible that
//         bugs got copied too
//
//
//--------------------------------------------------------------------------
NTSTATUS NTAPI
SpMakeSignature(
    IN ULONG_PTR ContextHandle,
    IN ULONG fQOP,
    IN OUT PSecBufferDesc pMessage,
    IN ULONG MessageSeqNo
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    NTSTATUS SubStatus = STATUS_SUCCESS;
    PDIGEST_USERCONTEXT pContext = NULL;
    BOOL    bServer = FALSE;
    DIGESTMODE_TYPE typeDigestMode = DIGESTMODE_UNDEFINED;   // Are we in SASL or HTTP mode

    DebugLog((DEB_TRACE_FUNC, "SpMakeSignature:Entering   ContextHandle 0x%lx\n", ContextHandle ));
    UNREFERENCED_PARAMETER(fQOP);


    Status = UserCtxtHandlerHandleToContext(ContextHandle, FALSE, FALSE, &pContext);
    if (!NT_SUCCESS(Status))
    {
        Status = STATUS_INVALID_HANDLE;
        DebugLog((DEB_ERROR, "SpMakeSignature: Could not find ContextHandle\n" ));
        goto CleanUp;
    }

    UserContextPrint(pContext);

    // Since we are in UserMode we MUST have a sessionkey to use - if not then can not process
    if (!pContext->strSessionKey.Length)
    {
        Status = STATUS_NO_USER_SESSION_KEY;
        DebugLog((DEB_ERROR, "SpMakeSignature: No Session Key contained in UserContext\n"));
        goto CleanUp;
    }

    // Check to see if Integrity is negotiated for SC
    bServer = pContext->CredentialUseFlags & DIGEST_CRED_INBOUND;
    if ((pContext->typeDigest == SASL_CLIENT) ||
        (pContext->typeDigest == SASL_SERVER))
    {
        typeDigestMode = DIGESTMODE_SASL;
    }
    else
    {
        typeDigestMode = DIGESTMODE_HTTP;
    }

    if (typeDigestMode == DIGESTMODE_HTTP)
    {
        DebugLog((DEB_TRACE, "SpMakeSignature: HTTP SignMessage selected\n"));
        Status = DigestUserHTTPHelper(
                            pContext,
                            eSign,
                            pMessage,
                            MessageSeqNo
                            );
    }
    else
    {
        if ((bServer && !(pContext->ContextReq & ASC_REQ_INTEGRITY)) ||
            (!bServer && !(pContext->ContextReq & ISC_REQ_INTEGRITY)) )
        {
            Status = SEC_E_QOP_NOT_SUPPORTED;
            DebugLog((DEB_ERROR, "SpMakeSignature: Did not negotiate INTEGRITY\n" ));
            goto CleanUp;
        }

        DebugLog((DEB_TRACE, "SpMakeSignature: SASL SignMessage selected\n"));
        Status = DigestUserSignHelperMulti(
                            pContext,
                            pMessage,
                            MessageSeqNo
                            );
    }

    if (!NT_SUCCESS(Status))
    {
        DebugLog((DEB_ERROR, "SpMakeSignature: DigestUserHTTP/SASLSignHelper returns %lx\n", Status ));
        goto CleanUp;
    }

CleanUp:

    if (pContext != NULL)
    {
        SubStatus = UserCtxtHandlerRelease(pContext);

        // Don't destroy previous status

        if (NT_SUCCESS(Status))
        {
            Status = SubStatus;
        }
    }
    
    DebugLog((DEB_TRACE_FUNC, "SpMakeSignature:Leaving   status 0x%lx\n", Status ));
    return(Status);
}



//+-------------------------------------------------------------------------
//
//  Function:   SpVerifySignature
//
//  Synopsis:   Verifies a signed message buffer by calculating the Digest Access
//              for data bufferswith the current Security Context state.
//
//  Effects:
//
//  Arguments:  ContextHandle - Handle of the context to use to sign the
//                      message.
//              MessageBuffers - Contains an array of signed buffers  and
//                      a signature buffer.
//              MessageSequenceNumber - Unused ULONG
//              QualityOfProtection - Unused flags.
//
//  Requires:   STATUS_INVALID_HANDLE - the context could not be found or
//                      was not configured for message integrity.
//              STATUS_INVALID_PARAMETER - the signature buffer could not
//                      be found or was too small.
//
//  Returns:
//
//  Notes: This routine should be called AFTER you have a valid security context
//      from (usually) acceptsecuritycontext.  The usermode context has a nonce
//      count that is automatically incremented for each successful verify signature
//      function call.  Therefore, calling this functio with the same noncecount
//      will return a failed status message.
//
//
//--------------------------------------------------------------------------
NTSTATUS NTAPI
SpVerifySignature(
    IN ULONG_PTR ContextHandle,
    IN PSecBufferDesc pMessage,
    IN ULONG MessageSeqNo,
    OUT PULONG pfQOP
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    NTSTATUS SubStatus = STATUS_SUCCESS;
    PDIGEST_USERCONTEXT pContext = NULL;
    BOOL    bServer = FALSE;
    DIGESTMODE_TYPE typeDigestMode = DIGESTMODE_UNDEFINED;   // Are we in SASL or HTTP mode

    DebugLog((DEB_TRACE_FUNC, "SpVerifySignature:Entering   ContextHandle 0x%lx\n", ContextHandle ));

    // Reset output flags
    if (pfQOP)
    {
        *pfQOP = 0;
    }

    Status = UserCtxtHandlerHandleToContext(ContextHandle, FALSE, FALSE, &pContext);
    if (!NT_SUCCESS(Status))
    {
        Status = STATUS_INVALID_HANDLE;
        DebugLog((DEB_ERROR, "SpVerifySignature: Could not find ContextHandle\n" ));
        goto CleanUp;
    }

    UserContextPrint(pContext);

    // Since we are in UserMode we MUST have a sessionkey to use - if not then can not process
    if (!pContext->strSessionKey.Length)
    {
        Status = STATUS_NO_USER_SESSION_KEY;
        DebugLog((DEB_ERROR, "SpVerifySignature: No Session Key contained in UserContext\n"));
        goto CleanUp;
    }

    // Check to see if Integrity is negotiated for SC
    bServer = pContext->CredentialUseFlags & DIGEST_CRED_INBOUND;
    if ((pContext->typeDigest == SASL_CLIENT) ||
        (pContext->typeDigest == SASL_SERVER))
    {
        typeDigestMode = DIGESTMODE_SASL;
    }
    else
    {
        typeDigestMode = DIGESTMODE_HTTP;
    }

    if (typeDigestMode == DIGESTMODE_HTTP)
    {
        DebugLog((DEB_TRACE, "SpVerifySignature: HTTP VerifyMessage selected\n"));
        Status = DigestUserHTTPHelper(
                            pContext,
                            eVerify,
                            pMessage,
                            MessageSeqNo
                            );
    }
    else
    {
        if ((bServer && !(pContext->ContextReq & ASC_REQ_INTEGRITY)) ||
            (!bServer && !(pContext->ContextReq & ISC_REQ_INTEGRITY)) )
        {
            Status = SEC_E_QOP_NOT_SUPPORTED;
            DebugLog((DEB_ERROR, "SpVerifySignature: Did not negotiate INTEGRITY\n" ));
            goto CleanUp;
        }
        else
        {
            DebugLog((DEB_TRACE, "SpVerifySignature: SASL VerifyMessage selected\n"));
            Status = DigestUserVerifyHelper(
                                pContext,
                                pMessage,
                                MessageSeqNo
                                );
        }
    }

    if (!NT_SUCCESS(Status))
    {
        DebugLog((DEB_ERROR, "SpVerifySignature: DigestUserHTTP/SASLSignHelper returns %lx\n", Status ));
        goto CleanUp;
    }

CleanUp:

    if (pContext != NULL)
    {
        SubStatus = UserCtxtHandlerRelease(pContext);

        // Don't destroy previous status

        if (NT_SUCCESS(Status))
        {
            Status = SubStatus;
        }
    }
    
    DebugLog((DEB_TRACE_FUNC, "SpVerifySignature:Leaving   status 0x%lx\n", Status ));
    return(Status);
}



//+-------------------------------------------------------------------------
//
//  Function:   SpSealMessage
//
//  Synopsis:   Verifies a signed message buffer by calculating a checksum over all
//              the non-read only data buffers and encrypting the checksum
//              along with a nonce.
//
//  Effects:
//
//  Arguments:  ContextHandle - Handle of the context to use to sign the
//                      message.
//              MessageBuffers - Contains an array of signed buffers  and
//                      a signature buffer.
//              MessageSequenceNumber - Sequence number for this message,
//                      only used in datagram cases.
//              QualityOfProtection - Unused flags.
//
//  Requires:   STATUS_INVALID_HANDLE - the context could not be found or
//                      was not configured for message integrity.
//              STATUS_INVALID_PARAMETER - the signature buffer could not
//                      be found or was too small.
//
//  Returns:
//
//  Notes: This was stolen from net\svcdlls\ntlmssp\client\sign.c ,
//         routine SspHandleSealMessage. It's possible that
//         bugs got copied too
//
//
//--------------------------------------------------------------------------
NTSTATUS NTAPI
SpSealMessage(
    IN ULONG_PTR ContextHandle,
    IN ULONG fQOP,
    IN PSecBufferDesc pMessage,
    IN ULONG MessageSeqNo
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    NTSTATUS SubStatus = STATUS_SUCCESS;
    PDIGEST_USERCONTEXT pContext = NULL;
    BOOL     bServer = FALSE;

    DebugLog((DEB_TRACE_FUNC, "SpSealMessage:Entering   ContextHandle 0x%lx\n", ContextHandle ));
    UNREFERENCED_PARAMETER(fQOP);


    Status = UserCtxtHandlerHandleToContext(ContextHandle, FALSE, FALSE, &pContext);
    if (!NT_SUCCESS(Status))
    {
        Status = STATUS_INVALID_HANDLE;
        DebugLog((DEB_ERROR, "SpSealMessage: Could not find ContextHandle\n" ));
        goto CleanUp;
    }

    UserContextPrint(pContext);

    // Since we are in UserMode we MUST have a sessionkey to use - if not then can not process
    if (!pContext->strSessionKey.Length)
    {
        Status = STATUS_NO_USER_SESSION_KEY;
        DebugLog((DEB_ERROR, "SpSealMessage: No Session Key contained in UserContext\n"));
        goto CleanUp;
    }

    // Check to see if Confidentiality is negotiated for SC
    bServer = pContext->CredentialUseFlags & DIGEST_CRED_INBOUND;
    if ((bServer && !(pContext->ContextReq & ASC_RET_CONFIDENTIALITY)) ||
        (!bServer && !(pContext->ContextReq & ISC_RET_CONFIDENTIALITY)) )
    {
        // Since CONFIDENTIALITY not negoiated - check if integrity selected
        if ((bServer && (pContext->ContextReq & ASC_RET_INTEGRITY)) ||
            (!bServer && (pContext->ContextReq & ISC_RET_INTEGRITY)) )
        {
            DebugLog((DEB_TRACE, "SpSealMessage: No Confidentiality selected - use Integrity ONLY\n"));
            // Just call the Sign routine only
            Status = DigestUserSignHelperMulti(
                                pContext,
                                pMessage,
                                MessageSeqNo
                                );
        }
        else
        {
            DebugLog((DEB_ERROR, "SpSealMessage: Neither Confidentiality  nor Integrity selected\n"));
            Status = SEC_E_QOP_NOT_SUPPORTED;
            DebugLog((DEB_ERROR, "SpSealMessage: Did not negotiate CONFIDENTIALITY\n" ));
            goto CleanUp;
        }
    }
    else
    {
        if (fQOP & SECQOP_WRAP_NO_ENCRYPT) {
            DebugLog((DEB_ERROR, "SpSealMessage: Negotiated Confidentiality but selected Sign only\n"));
            Status = SEC_E_QOP_NOT_SUPPORTED;
            goto CleanUp;
        }
        // Use SignHelper for both SASL  - HTTP not speced
        Status = DigestUserSealHelperMulti(
                            pContext,
                            pMessage,
                            MessageSeqNo
                            );
    }

    if (!NT_SUCCESS(Status))
    {
        DebugLog((DEB_ERROR, "SpSealMessage: DigestUserSASLHelper returns %lx\n", Status ));
        goto CleanUp;
    }

CleanUp:

    if (pContext != NULL)
    {
        SubStatus = UserCtxtHandlerRelease(pContext);

        // Don't destroy previous status

        if (NT_SUCCESS(Status))
        {
            Status = SubStatus;
        }
    }
    
    DebugLog((DEB_TRACE_FUNC, "SpSealMessage:Leaving   status 0x%lx\n", Status ));
    return(Status);
}



//+-------------------------------------------------------------------------
//
//  Function:   SpUnsealMessage
//
//  Synopsis:   Verifies a signed message buffer by calculating a checksum over all
//              the non-read only data buffers and encrypting the checksum
//              along with a nonce.
//
//  Effects:
//
//  Arguments:  ContextHandle - Handle of the context to use to sign the
//                      message.
//              MessageBuffers - Contains an array of signed buffers  and
//                      a signature buffer.
//              MessageSequenceNumber - Sequence number for this message,
//                      only used in datagram cases.
//              QualityOfProtection - Unused flags.
//
//  Requires:   STATUS_INVALID_HANDLE - the context could not be found or
//                      was not configured for message integrity.
//              STATUS_INVALID_PARAMETER - the signature buffer could not
//                      be found or was too small.
//
//  Returns:
//
//  Notes: This was stolen from net\svcdlls\ntlmssp\client\sign.c ,
//         routine SspHandleUnsealMessage. It's possible that
//         bugs got copied too
//
//
//--------------------------------------------------------------------------
NTSTATUS NTAPI
SpUnsealMessage(
    IN ULONG_PTR ContextHandle,
    IN PSecBufferDesc pMessage,
    IN ULONG MessageSeqNo,
    OUT PULONG pfQOP
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    NTSTATUS SubStatus = STATUS_SUCCESS;
    PDIGEST_USERCONTEXT pContext = NULL;
    BOOL  bServer = FALSE;    // acting as the server ?

    DebugLog((DEB_TRACE_FUNC, "SpUnsealMessage:Entering   ContextHandle 0x%lx\n", ContextHandle ));

    // Reset output flags
    if (pfQOP)
    {
        *pfQOP = 0;
    }


    Status = UserCtxtHandlerHandleToContext(ContextHandle, FALSE, FALSE, &pContext);
    if (!NT_SUCCESS(Status))
    {
        Status = STATUS_INVALID_HANDLE;
        DebugLog((DEB_ERROR, "SpUnsealMessage: Could not find ContextHandle\n" ));
        goto CleanUp;
    }

    UserContextPrint(pContext);

    // Since we are in UserMode we MUST have a sessionkey to use - if not then can not process
    if (!pContext->strSessionKey.Length)
    {
        Status = STATUS_NO_USER_SESSION_KEY;
        DebugLog((DEB_ERROR, "SpUnsealMessage: No Session Key contained in UserContext\n"));
        goto CleanUp;
    }

    // Check to see if Confidentiality is negotiated for SC
    bServer = pContext->CredentialUseFlags & DIGEST_CRED_INBOUND;
    if ((bServer && !(pContext->ContextReq & ASC_RET_CONFIDENTIALITY)) ||
        (!bServer && !(pContext->ContextReq & ISC_RET_CONFIDENTIALITY)) )
    {
        if ((bServer && (pContext->ContextReq & ASC_RET_INTEGRITY)) ||
            (!bServer && (pContext->ContextReq & ISC_RET_INTEGRITY)) )
        {
            DebugLog((DEB_TRACE, "SpUnsealMessage: No Confidentiality selected - use Integrity ONLY\n"));
            Status = DigestUserVerifyHelper(
                                pContext,
                                pMessage,
                                MessageSeqNo
                                );

            // signal QOP was only for integrity
            if (pfQOP)
            {
                *pfQOP = SECQOP_WRAP_NO_ENCRYPT;
            }
        }
        else
        {
            DebugLog((DEB_ERROR, "SpUnsealMessage: Neither Confidentiality  nor Integrity selected\n"));
            Status = SEC_E_QOP_NOT_SUPPORTED;
            DebugLog((DEB_ERROR, "SpUnsealMessage: Did not negotiate CONFIDENTIALITY\n" ));
            goto CleanUp;
        }
    }
    else
    {
        Status = DigestUserUnsealHelper(
                            pContext,
                            pMessage,
                            MessageSeqNo
                            );
    }

    if (!NT_SUCCESS(Status))
    {
        DebugLog((DEB_ERROR, "SpUnsealMessage: DigestUserSASLHelper returns %lx\n", Status ));
        goto CleanUp;
    }

CleanUp:

    if (pContext != NULL)
    {
        SubStatus = UserCtxtHandlerRelease(pContext);

        // Don't destroy previous status

        if (NT_SUCCESS(Status))
        {
            Status = SubStatus;
        }
    }
    
    DebugLog((DEB_TRACE_FUNC, "SpUnsealMessage:Leaving   status 0x%lx\n", Status ));
    return(Status);
}



//+-------------------------------------------------------------------------
//
//  Function:   SpGetContextToken
//
//  Synopsis:   returns a pointer to the token for a server-side context
//
//  Effects:
//
//  Arguments:
//
//  Requires:
//
//  Returns:
//
//  Notes: Used in ImpersonateSecurityContext SSPI Call
//
//
//--------------------------------------------------------------------------
NTSTATUS NTAPI
SpGetContextToken(
    IN ULONG_PTR ContextHandle,
    OUT PHANDLE ImpersonationToken
    )
{
    DebugLog((DEB_TRACE_FUNC, "SpGetContextToken: Entering   ContextHandle 0x%lx\n", ContextHandle ));

    NTSTATUS Status = STATUS_SUCCESS;
    PDIGEST_USERCONTEXT pContext = NULL;

    Status = UserCtxtHandlerHandleToContext(ContextHandle, FALSE, FALSE, &pContext);
    if (!NT_SUCCESS(Status))
    {
        DebugLog((DEB_ERROR, "SpGetContextToken: UserCtxtHandlerHandleToContext error 0x%x\n", Status));
    }

    if (pContext && pContext->ClientTokenHandle)
    {
        DebugLog((DEB_TRACE, "SpGetContextToken:       Client ImpersonationToken  0x%lx\n", pContext->ClientTokenHandle ));
        *ImpersonationToken = pContext->ClientTokenHandle;
        goto CleanUp;
    }

    Status = STATUS_INVALID_HANDLE;
    DebugLog((DEB_ERROR, "SpGetContextToken: no token handle\n" ));

CleanUp:

    if (pContext != NULL)
    {
        Status = UserCtxtHandlerRelease(pContext);
    }

    DebugLog((DEB_TRACE_FUNC, "SpGetContextToken: Leaving  Status 0x%lx\n", Status ));
    return(Status);
}



//+-------------------------------------------------------------------------
//
//  Function:   SpQueryContextAttributes
//
//  Synopsis:   Querys attributes of the specified context
//              This API allows a customer of the security
//              services to determine certain attributes of
//              the context.  These are: sizes, names, and lifespan.
//
//  Effects:
//
//  Arguments:
//
//    ContextHandle - Handle to the context to query.
//
//    Attribute - Attribute to query.
//
//
//    Buffer - Buffer to copy the data into.  The buffer must
//             be large enough to fit the queried attribute.
//
//
//  Requires:
//
//  Returns:
//
//        STATUS_SUCCESS - Call completed successfully
//
//        STATUS_INVALID_HANDLE -- Credential/Context Handle is invalid
//        STATUS_NOT_SUPPORTED -- Function code is not supported
//
//  Notes:
//
//--------------------------------------------------------------------------
NTSTATUS NTAPI
SpQueryContextAttributes(
    IN ULONG_PTR ContextHandle,
    IN ULONG Attribute,
    IN OUT PVOID Buffer
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    NTSTATUS SubStatus = STATUS_SUCCESS;
    PDIGEST_USERCONTEXT pContext = NULL;

    DebugLog((DEB_TRACE_FUNC, "SpQueryContextAttributes: Entering ContextHandle 0x%lx\n", ContextHandle ));

    PSecPkgContext_Sizes ContextSizes = NULL;
    PSecPkgContext_DceInfo ContextDceInfo = NULL;
    PSecPkgContext_Names ContextNames = NULL;
    PSecPkgContext_PackageInfo PackageInfo = NULL;
    PSecPkgContext_NegotiationInfo NegInfo = NULL;
    PSecPkgContext_PasswordExpiry PasswordExpires = NULL;
    PSecPkgContext_KeyInfo KeyInfo = NULL;
    PSecPkgContext_AccessToken AccessToken = NULL;
    PSecPkgContext_StreamSizes StreamSizes = NULL;
    PSecPkgContext_AuthzID ContextAuthzID = NULL;
    PSecPkgContext_Target ContextTarget = NULL;

    ULONG PackageInfoSize = 0;
    BOOL    bServer = FALSE;
    LPWSTR pszEncryptAlgorithmName = NULL;
    LPWSTR pszSignatureAlgorithmName = NULL;
    ULONG ulBytes = 0;
    ULONG ulMaxMessage = 0;

    DIGESTMODE_TYPE typeDigestMode = DIGESTMODE_UNDEFINED;   // Are we in SASL or HTTP mode


    Status = UserCtxtHandlerHandleToContext(ContextHandle, FALSE, FALSE, &pContext);
    if (!NT_SUCCESS(Status))
    {
        DebugLog((DEB_ERROR, "SpQueryContextAttributes: HandleToContext error 0x%x\n", Status));
        Status = STATUS_INVALID_HANDLE;
        goto CleanUp;
    }


    // Check to see if Integrity is negotiated for SC
    bServer = pContext->CredentialUseFlags & DIGEST_CRED_INBOUND;

    if ((pContext->typeDigest == SASL_CLIENT) ||
        (pContext->typeDigest == SASL_SERVER))
    {
        typeDigestMode = DIGESTMODE_SASL;
    }
    else
    {
        typeDigestMode = DIGESTMODE_HTTP;
    }

    //
    // Handle each of the various queried attributes
    //

    DebugLog((DEB_TRACE, "SpQueryContextAttributes : 0x%lx\n", Attribute ));
    switch ( Attribute) {
    case SECPKG_ATTR_SIZES:

        ContextSizes = (PSecPkgContext_Sizes) Buffer;
        ZeroMemory(ContextSizes, sizeof(SecPkgContext_Sizes));
        ContextSizes->cbMaxToken = NTDIGEST_SP_MAX_TOKEN_SIZE;
        if (typeDigestMode == DIGESTMODE_HTTP)
        {      // HTTP has signature the same as token in Authentication Header info
            ContextSizes->cbMaxSignature = NTDIGEST_SP_MAX_TOKEN_SIZE;
        }
        else
        {    // SASL has specialized signature block
            ContextSizes->cbMaxSignature = MAC_BLOCK_SIZE + MAX_PADDING;
        }
        if ((pContext->typeCipher == CIPHER_3DES) || 
            (pContext->typeCipher == CIPHER_DES))
        {
            ContextSizes->cbBlockSize = DES_BLOCKSIZE;
            ContextSizes->cbSecurityTrailer = MAC_BLOCK_SIZE + MAX_PADDING;
        }
        else if ((pContext->typeCipher == CIPHER_RC4) || 
                 (pContext->typeCipher == CIPHER_RC4_40) ||
                 (pContext->typeCipher == CIPHER_RC4_56))
        {
            ContextSizes->cbBlockSize = RC4_BLOCKSIZE;
            ContextSizes->cbSecurityTrailer = MAC_BLOCK_SIZE + MAX_PADDING;
        }
        else
        {
            ContextSizes->cbBlockSize = 0;
            if (typeDigestMode == DIGESTMODE_HTTP)
            {      // HTTP has signature the same as token in Authentication Header info
                ContextSizes->cbSecurityTrailer = 0;
            }
            else
            {    // SASL has specialized signature block
                ContextSizes->cbSecurityTrailer = MAC_BLOCK_SIZE + MAX_PADDING;   // handle Auth-int case
            }
        }
        break;
    
    case SECPKG_ATTR_DCE_INFO:

        ContextDceInfo = (PSecPkgContext_DceInfo) Buffer;
        ZeroMemory(ContextDceInfo, sizeof(SecPkgContext_DceInfo));
        ContextDceInfo->AuthzSvc = 0;

        break;

    case SECPKG_ATTR_NAMES:

        ContextNames = (PSecPkgContext_Names) Buffer;
        ZeroMemory(ContextNames, sizeof(SecPkgContext_Names));

        if (pContext->ustrAccountName.Length && pContext->ustrAccountName.Buffer)
        {
            ulBytes = pContext->ustrAccountName.Length + sizeof(WCHAR);
            ContextNames->sUserName = (LPWSTR)g_UserFunctions->AllocateHeap(ulBytes);
            if (ContextNames->sUserName)
            {
                ZeroMemory(ContextNames->sUserName, ulBytes);
                memcpy(ContextNames->sUserName, pContext->ustrAccountName.Buffer, pContext->ustrAccountName.Length);
            }
            else
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
        }
        else
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }

        break;

    case SECPKG_ATTR_TARGET:

        ContextTarget = (PSecPkgContext_Target) Buffer;
        ZeroMemory(ContextTarget, sizeof(SecPkgContext_Target));

        if (pContext->strParam[MD5_AUTH_URI].Length && pContext->strParam[MD5_AUTH_URI].Buffer)
        {
            ulBytes = pContext->strParam[MD5_AUTH_URI].Length;
            ContextTarget->Target = (LPSTR)g_UserFunctions->AllocateHeap(ulBytes);
            if (ContextTarget->Target)
            {
                memcpy(ContextTarget->Target, pContext->strParam[MD5_AUTH_URI].Buffer, ulBytes);
                ContextTarget->TargetLength = ulBytes;
            }
            else
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
        }
        break;

    case SECPKG_ATTR_AUTHENTICATION_ID:
        ContextAuthzID = (PSecPkgContext_AuthzID) Buffer;
        ZeroMemory(ContextAuthzID, sizeof(SecPkgContext_AuthzID));
        
        if (pContext->ulFlags & FLAG_CONTEXT_AUTHZID_PROVIDED)
        {
            if (pContext->strParam[MD5_AUTH_AUTHZID].Length && pContext->strParam[MD5_AUTH_AUTHZID].Buffer)
            {
                ulBytes = pContext->strParam[MD5_AUTH_AUTHZID].Length;
                ContextAuthzID->AuthzID = (LPSTR)g_UserFunctions->AllocateHeap(ulBytes);
                if (ContextAuthzID->AuthzID)
                {
                    memcpy(ContextAuthzID->AuthzID, pContext->strParam[MD5_AUTH_AUTHZID].Buffer, ulBytes);
                    ContextAuthzID->AuthzIDLength = ulBytes;
                }
                else
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                }
            }
            else
            {    // a valid buffer and a zero length indicate a NULL strign was supplied by calling application
                ContextAuthzID->AuthzID = (LPSTR)g_UserFunctions->AllocateHeap(1);
                if (ContextAuthzID->AuthzID)
                {
                    ContextAuthzID->AuthzIDLength = 0;      // to indicate that "" was used
                }
                else
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                }
            }
        }
        break;

    case SECPKG_ATTR_PACKAGE_INFO:
    case SECPKG_ATTR_NEGOTIATION_INFO:
        //
        // Return the information about this package. This is useful for
        // callers who used SPNEGO and don't know what package they got.
        //

        if ((Attribute == SECPKG_ATTR_NEGOTIATION_INFO) && (g_fParameter_Negotiate == FALSE))
        {
            Status = STATUS_NOT_SUPPORTED;
            goto CleanUp;
        }

        PackageInfo = (PSecPkgContext_PackageInfo) Buffer;
        ZeroMemory(PackageInfo, sizeof(SecPkgContext_PackageInfo));
        PackageInfoSize = sizeof(SecPkgInfoW) + sizeof(WDIGEST_SP_NAME) + sizeof(NTDIGEST_SP_COMMENT);
        PackageInfo->PackageInfo = (PSecPkgInfoW) g_UserFunctions->AllocateHeap(PackageInfoSize);
        if (PackageInfo->PackageInfo == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto CleanUp;
        }
        PackageInfo->PackageInfo->Name = (LPWSTR) (PackageInfo->PackageInfo + 1);
        PackageInfo->PackageInfo->Comment = (LPWSTR) ((((PBYTE) PackageInfo->PackageInfo->Name)) + sizeof(WDIGEST_SP_NAME));
        wcscpy(
            PackageInfo->PackageInfo->Name,
            WDIGEST_SP_NAME
            );

        wcscpy(
            PackageInfo->PackageInfo->Comment,
            NTDIGEST_SP_COMMENT
            );
        PackageInfo->PackageInfo->wVersion      = SECURITY_SUPPORT_PROVIDER_INTERFACE_VERSION;
        PackageInfo->PackageInfo->wRPCID        = RPC_C_AUTHN_DIGEST;
        PackageInfo->PackageInfo->fCapabilities = NTDIGEST_SP_CAPS;
        PackageInfo->PackageInfo->cbMaxToken    = NTDIGEST_SP_MAX_TOKEN_SIZE;

        if ( Attribute == SECPKG_ATTR_NEGOTIATION_INFO )
        {
            NegInfo = (PSecPkgContext_NegotiationInfo) PackageInfo ;
            NegInfo->NegotiationState = SECPKG_NEGOTIATION_COMPLETE ;
        }

        break;

    case SECPKG_ATTR_PASSWORD_EXPIRY:
        PasswordExpires = (PSecPkgContext_PasswordExpiry) Buffer;
        if (pContext->ExpirationTime.QuadPart != 0)
        {
            PasswordExpires->tsPasswordExpires = pContext->ExpirationTime;
        }
        else
            Status = STATUS_NOT_SUPPORTED;
        break;

    case SECPKG_ATTR_KEY_INFO:
        KeyInfo = (PSecPkgContext_KeyInfo) Buffer;
        ZeroMemory(KeyInfo, sizeof(SecPkgContext_KeyInfo));
        if (typeDigestMode == DIGESTMODE_HTTP)
        {
            // HTTP mode
            KeyInfo->SignatureAlgorithm = CALG_MD5;
            pszSignatureAlgorithmName = WSTR_CIPHER_MD5;
            KeyInfo->sSignatureAlgorithmName = (LPWSTR)
                g_UserFunctions->AllocateHeap(sizeof(WCHAR) * ((ULONG)wcslen(pszSignatureAlgorithmName) + 1));
            if (KeyInfo->sSignatureAlgorithmName != NULL)
            {
                wcscpy(
                    KeyInfo->sSignatureAlgorithmName,
                    pszSignatureAlgorithmName
                    );
            }
            else
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
        }
        else
        {
            // SASL mode
            KeyInfo->KeySize = 128;       // All modes use a 128 bit key - may have less entropy though (i.e. rc4-XX)
            KeyInfo->SignatureAlgorithm = CALG_HMAC;
            pszSignatureAlgorithmName = WSTR_CIPHER_HMAC_MD5;
            switch (pContext->typeCipher)
            {
                case CIPHER_RC4:
                case CIPHER_RC4_40:
                case CIPHER_RC4_56:
                    KeyInfo->KeySize = 16 * 8;    // All modes use a 128 bit key - may have less entropy though (i.e. rc4-XX)
                    KeyInfo->EncryptAlgorithm = CALG_RC4;
                    pszEncryptAlgorithmName = WSTR_CIPHER_RC4;
                    break;
                case CIPHER_DES:
                    KeyInfo->KeySize = 7 * 8;
                    KeyInfo->EncryptAlgorithm = CALG_DES;
                    pszEncryptAlgorithmName = WSTR_CIPHER_DES;
                    break;
                case CIPHER_3DES:
                    KeyInfo->KeySize = 14 * 8;
                    KeyInfo->EncryptAlgorithm = CALG_3DES_112;
                    pszEncryptAlgorithmName = WSTR_CIPHER_3DES;
                    break;
            }
            if (pszEncryptAlgorithmName)
            {
                KeyInfo->sEncryptAlgorithmName = (LPWSTR)
                    g_UserFunctions->AllocateHeap(sizeof(WCHAR) * ((ULONG)wcslen(pszEncryptAlgorithmName) + 1));
                if (KeyInfo->sEncryptAlgorithmName != NULL)
                {
                    wcscpy(
                        KeyInfo->sEncryptAlgorithmName,
                        pszEncryptAlgorithmName
                        );
                }
                else
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                }
            }
            if (pszSignatureAlgorithmName)
            {
                KeyInfo->sSignatureAlgorithmName = (LPWSTR)
                    g_UserFunctions->AllocateHeap(sizeof(WCHAR) * ((ULONG)wcslen(pszSignatureAlgorithmName) + 1));
                if (KeyInfo->sSignatureAlgorithmName != NULL)
                {
                    wcscpy(
                        KeyInfo->sSignatureAlgorithmName,
                        pszSignatureAlgorithmName
                        );
                }
                else
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                }
            }
        }

        // Make sure that EncryptAlgorithmName and SignatureAlgorithmName is a valid NULL terminated string #601928
        if (NT_SUCCESS(Status) && !KeyInfo->sEncryptAlgorithmName)
        {
            KeyInfo->sEncryptAlgorithmName = (LPWSTR)
                g_UserFunctions->AllocateHeap(sizeof(WCHAR));

            if (KeyInfo->sEncryptAlgorithmName)
            {
                KeyInfo->sEncryptAlgorithmName[0] = L'\0';
            }
            else
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
        }

        if (NT_SUCCESS(Status) && !KeyInfo->sSignatureAlgorithmName)
        {
            KeyInfo->sSignatureAlgorithmName = (LPWSTR)
                g_UserFunctions->AllocateHeap(sizeof(WCHAR));

            if (KeyInfo->sSignatureAlgorithmName)
            {
                KeyInfo->sSignatureAlgorithmName[0] = L'\0';
            }
            else
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
        }
        break;
    case SECPKG_ATTR_STREAM_SIZES:
        StreamSizes = (PSecPkgContext_StreamSizes) Buffer;
        ZeroMemory(StreamSizes, sizeof(SecPkgContext_StreamSizes));

        if (typeDigestMode == DIGESTMODE_HTTP)
        { 
        }
        else
        {    // SASL
            ulMaxMessage = pContext->ulRecvMaxBuf;
            if (pContext->ulSendMaxBuf < ulMaxMessage)
            {
                ulMaxMessage = pContext->ulSendMaxBuf;
            }
            StreamSizes->cbMaximumMessage = ulMaxMessage - (MAC_BLOCK_SIZE + MAX_PADDING);
        }

        if ((pContext->typeCipher == CIPHER_3DES) || 
            (pContext->typeCipher == CIPHER_DES))
        {
            StreamSizes->cbBlockSize = DES_BLOCKSIZE;
            StreamSizes->cbTrailer = MAC_BLOCK_SIZE + MAX_PADDING;
        }
        else if ((pContext->typeCipher == CIPHER_RC4) || 
                 (pContext->typeCipher == CIPHER_RC4_40) ||
                 (pContext->typeCipher == CIPHER_RC4_56))
        {
            StreamSizes->cbBlockSize = RC4_BLOCKSIZE;
            StreamSizes->cbTrailer = MAC_BLOCK_SIZE + MAX_PADDING;
        }
        break;
    case SECPKG_ATTR_ACCESS_TOKEN:
        AccessToken = (PSecPkgContext_AccessToken) Buffer;
        //
        // ClientTokenHandle can be NULL, for instance:
        // 1. client side context.
        // 2. incomplete server context.
        //      Token is not duped - caller must not CloseHandle
        AccessToken->AccessToken = (void*)pContext->ClientTokenHandle;
        break;

    default:
        Status = STATUS_NOT_SUPPORTED;
        break;
    }


CleanUp:

    if (!NT_SUCCESS(Status))
    {
        switch (Attribute) {

        case SECPKG_ATTR_NAMES:

            if (ContextNames && ContextNames->sUserName )
            {
                g_UserFunctions->FreeHeap(ContextNames->sUserName);
                ContextNames->sUserName = NULL;
            }
            break;

        case SECPKG_ATTR_DCE_INFO:

            if (ContextDceInfo && ContextDceInfo->pPac)
            {
                g_UserFunctions->FreeHeap(ContextDceInfo->pPac);
                ContextDceInfo->pPac = NULL;
            }
            break;

        case SECPKG_ATTR_KEY_INFO:
            if (KeyInfo && KeyInfo->sEncryptAlgorithmName)
            {
                g_UserFunctions->FreeHeap(KeyInfo->sEncryptAlgorithmName);
                KeyInfo->sEncryptAlgorithmName = NULL;
            }
            if (KeyInfo && KeyInfo->sSignatureAlgorithmName)
            {
                g_UserFunctions->FreeHeap(KeyInfo->sSignatureAlgorithmName);
                KeyInfo->sSignatureAlgorithmName = NULL;
            }
            break;
        }
    }

    if (pContext != NULL)
    {
        SubStatus = UserCtxtHandlerRelease(pContext);
    }

    DebugLog((DEB_TRACE_FUNC, "SpQueryContextAttributes: Leaving ContextHandle 0x%lx    status 0x%x\n",
               ContextHandle, Status ));
    return(Status);
    
}



//+-------------------------------------------------------------------------
//
//  Function:   SpCompleteAuthToken
//
//  Synopsis:   Completes a context  - used to perform user mode verification of
//          challenge response for non-persistent connections re-established via ASC
//          call.
//
//  Effects:
//
//  Arguments:
//
//  Requires:
//
//  Returns:
//
//  Notes:  Called after a Opaque Context lookup of SecurityContext.  ASC will determine that
//    this is a completed context and inform the app that it must call CompleteAuthToken.  Currently,
//    only HTTP mode has this processing done.
//
//
//--------------------------------------------------------------------------


NTSTATUS NTAPI
SpCompleteAuthToken(
    IN ULONG_PTR ContextHandle,
    IN PSecBufferDesc InputBuffer
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG ulQOP = 0;

    DebugLog((DEB_TRACE_FUNC, "SpCompleteAuthToken: Entering    ContextHandle 0x%lx\n", ContextHandle ));

    Status = SpVerifySignature(ContextHandle, InputBuffer, 0, &ulQOP);

    DebugLog((DEB_TRACE_FUNC, "SpCompleteAuthToken: Leaving    ContextHandle 0x%lx    Status = 0x%x\n",
               ContextHandle, Status));

    return(Status);
}


NTSTATUS NTAPI
SpFormatCredentials(
    IN PSecBuffer Credentials,
    OUT PSecBuffer FormattedCredentials
    )
{
    UNREFERENCED_PARAMETER (Credentials);
    UNREFERENCED_PARAMETER (FormattedCredentials);
    DebugLog((DEB_TRACE_FUNC, "SpFormatCredentials: Entering/Leaving\n"));
    return(SEC_E_UNSUPPORTED_FUNCTION);
}

NTSTATUS NTAPI
SpMarshallSupplementalCreds(
    IN ULONG CredentialSize,
    IN PUCHAR Credentials,
    OUT PULONG MarshalledCredSize,
    OUT PVOID * MarshalledCreds
    )
{
    UNREFERENCED_PARAMETER (CredentialSize);
    UNREFERENCED_PARAMETER (Credentials);
    UNREFERENCED_PARAMETER (MarshalledCredSize);
    UNREFERENCED_PARAMETER (MarshalledCreds);
    DebugLog((DEB_TRACE_FUNC, "SpMarshallSupplementalCreds: Entering/Leaving\n"));
    return(SEC_E_UNSUPPORTED_FUNCTION);
}

//+-------------------------------------------------------------------------
//
//  Function:   NtDigestMakePackedContext
//
//  Synopsis:   Maps a context to the caller's address space
//
//  Effects:
//
//  Arguments:  Context - The context to map
//              MappedContext - Set to TRUE on success
//              ContextData - Receives a buffer in the caller's address space
//                      with the mapped context.
//
//  Requires:
//
//  Returns:
//
//  Notes:
//
//
//--------------------------------------------------------------------------
NTSTATUS
NtDigestMakePackedContext(
    IN PDIGEST_USERCONTEXT Context,
    OUT PBOOLEAN MappedContext,
    OUT PSecBuffer ContextData,
    IN ULONG Flags
    )
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(MappedContext);
    UNREFERENCED_PARAMETER(ContextData);
    UNREFERENCED_PARAMETER(Flags);

    DebugLog((DEB_TRACE_FUNC, "NtDigestMakePackedContext: Entering/Leaving\n"));

    return(SEC_E_UNSUPPORTED_FUNCTION);
}

//+-------------------------------------------------------------------------
//
//  Function:   SpExportSecurityContext
//
//  Synopsis:   Exports a security context to another process
//
//  Effects:    Allocates memory for output
//
//  Arguments:  ContextHandle - handle to context to export
//              Flags - Flags concerning duplication. Allowable flags:
//                      SECPKG_CONTEXT_EXPORT_DELETE_OLD - causes old context
//                              to be deleted.
//              PackedContext - Receives serialized context to be freed with
//                      FreeContextBuffer
//              TokenHandle - Optionally receives handle to context's token.
//
//  Requires:
//
//  Returns:
//
//  Notes:
//
//
//--------------------------------------------------------------------------

NTSTATUS
SpExportSecurityContext(
    IN ULONG_PTR ContextHandle,
    IN ULONG Flags,
    OUT PSecBuffer PackedContext,
    OUT PHANDLE TokenHandle
    )
{
    UNREFERENCED_PARAMETER(ContextHandle);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(PackedContext);
    UNREFERENCED_PARAMETER(TokenHandle);

    DebugLog((DEB_TRACE_FUNC, "SpExportSecurityContext:Entering/Leaving     ContextHandle 0x%x\n", ContextHandle ));

    return(SEC_E_UNSUPPORTED_FUNCTION);
}


//+-------------------------------------------------------------------------
//
//  Function:   SpImportSecurityContext
//
//  Synopsis:
//
//  Effects:
//
//  Arguments:
//
//  Requires:
//
//  Returns:
//
//  Notes:
//
//
//--------------------------------------------------------------------------
NTSTATUS
SpImportSecurityContext(
    IN PSecBuffer PackedContext,
    IN HANDLE Token,
    OUT PULONG_PTR ContextHandle
    )
{
    UNREFERENCED_PARAMETER(PackedContext);
    UNREFERENCED_PARAMETER(Token);
    UNREFERENCED_PARAMETER(ContextHandle);

    DebugLog((DEB_TRACE_FUNC, "SpImportSecurityContext: Entering/Leaving   ContextHandle 0x%x\n", ContextHandle));

    return(SEC_E_UNSUPPORTED_FUNCTION);
}



/*++

RoutineDescription:

    Gets the TOKEN_USER from an open token

Arguments:

    Token - Handle to a token open for TOKEN_QUERY access

Return Value:

    STATUS_INSUFFICIENT_RESOURCES - not enough memory to complete the
        function.

    Errors from NtQueryInformationToken.

--*/

NTSTATUS
SspGetTokenUser(
    HANDLE Token,
    PTOKEN_USER * pTokenUser
    )
{
    PTOKEN_USER LocalTokenUser = NULL;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG TokenUserSize = 0;

    DebugLog((DEB_TRACE_FUNC, "SspGetTokenUser:  Entering  Token 0x%x    pTokenUser 0x%x\n", Token, pTokenUser));

    //
    // Query the token user.  First pass in NULL to get back the
    // required size.
    //

    Status = NtQueryInformationToken(
                Token,
                TokenUser,
                NULL,
                0,
                &TokenUserSize
                );

    if (Status != STATUS_BUFFER_TOO_SMALL)
    {
        ASSERT(Status != STATUS_SUCCESS);
        DebugLog((DEB_ERROR, "SspGetTokenUser: NtQueryInformationToken (1st call) returns 0x%lx for Token 0x%x\n", Status, Token ));
        goto CleanUp;
    }

    //
    // Now allocate the required ammount of memory and try again.
    //

    LocalTokenUser = (PTOKEN_USER) DigestAllocateMemory(TokenUserSize);
    if (LocalTokenUser == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto CleanUp;
    }
    Status = NtQueryInformationToken(
                Token,
                TokenUser,
                LocalTokenUser,
                TokenUserSize,
                &TokenUserSize
                );

    if (NT_SUCCESS(Status))
    {
        *pTokenUser = LocalTokenUser;
    }
    else
    {
        DigestFreeMemory(LocalTokenUser);
        DebugLog((DEB_ERROR, "SspGetTokenUser: NtQueryInformationToken (2nd call) returns 0x%lx for Token 0x%x\n", Status, Token ));
    }

CleanUp:

    DebugLog((DEB_TRACE_FUNC, "SspGetTokenUser:  Leaving  Token 0x%x with Status 0x%x\n", Token, Status));
    return(Status);
}



/*++

RoutineDescription:

    Create a local context for a real context
    Don't link it to out list of local contexts.
    Called inside LSA to prep packed Context buffer to send to UserMode addr space

Arguments:
   pLsaContext - pointer to a Context in LSA to map over to User space
   pDigest - pointer to digest auth parameters - may be NULL and use Context instead
   ulFlagOptions - options set for this context - will be OR'ed into ulFlag for mapped context
   ContextData - packed Context information to send to usermode process

Return Value:

--*/
NTSTATUS
SspMapDigestContext(
    IN PDIGEST_CONTEXT   pLsaContext,           // LSA Context
    IN PDIGEST_PARAMETER pDigest,
    IN ULONG ulFlagOptions,
    OUT PSecBuffer  ContextData
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    PDIGEST_PACKED_USERCONTEXT pPackedUserCtxt = NULL;      // Return buffer to on good auth to UserMode addr space
    USHORT cbLenNeeded = 0;
    PUCHAR  pucLoc = NULL;
    HANDLE  hTemp = NULL;
    int iAuth = 0;
    USHORT usAcctNameSize = 0;

    DebugLog((DEB_TRACE_FUNC, "SspMapContext: Entering  for LSA context %lx\n", pLsaContext));
    ASSERT(ContextData);
    ASSERT(pLsaContext);

    if (!pLsaContext)
    {
        Status = STATUS_INVALID_HANDLE;
        DebugLog((DEB_ERROR, "SspMapContext: pLsaContext invalid\n"));
        goto CleanUp;
    }

    // Copy over only selected fields
    cbLenNeeded = sizeof(DIGEST_PACKED_USERCONTEXT);
    if (pDigest)
    {
        cbLenNeeded = cbLenNeeded + pDigest->refstrParam[MD5_AUTH_USERNAME].Length;
        cbLenNeeded = cbLenNeeded + pDigest->refstrParam[MD5_AUTH_REALM].Length;
        cbLenNeeded = cbLenNeeded + pDigest->refstrParam[MD5_AUTH_NONCE].Length;
        cbLenNeeded = cbLenNeeded + pDigest->refstrParam[MD5_AUTH_CNONCE].Length;
        cbLenNeeded = cbLenNeeded + pDigest->refstrParam[MD5_AUTH_ALGORITHM].Length;
        cbLenNeeded = cbLenNeeded + pDigest->refstrParam[MD5_AUTH_QOP].Length;
        cbLenNeeded = cbLenNeeded + pDigest->refstrParam[MD5_AUTH_URI].Length;
        cbLenNeeded = cbLenNeeded + pDigest->refstrParam[MD5_AUTH_AUTHZID].Length;
        cbLenNeeded = cbLenNeeded + pDigest->refstrParam[MD5_AUTH_OPAQUE].Length;
    }
    else
    {
        cbLenNeeded = cbLenNeeded + pLsaContext->strDirective[MD5_AUTH_USERNAME].Length;
        cbLenNeeded = cbLenNeeded + pLsaContext->strDirective[MD5_AUTH_REALM].Length;
        cbLenNeeded = cbLenNeeded + pLsaContext->strDirective[MD5_AUTH_NONCE].Length;
        cbLenNeeded = cbLenNeeded + pLsaContext->strDirective[MD5_AUTH_CNONCE].Length;
        cbLenNeeded = cbLenNeeded + pLsaContext->strDirective[MD5_AUTH_ALGORITHM].Length;
        cbLenNeeded = cbLenNeeded + pLsaContext->strDirective[MD5_AUTH_QOP].Length;
        cbLenNeeded = cbLenNeeded + pLsaContext->strDirective[MD5_AUTH_URI].Length;
        cbLenNeeded = cbLenNeeded + pLsaContext->strDirective[MD5_AUTH_AUTHZID].Length;
        cbLenNeeded = cbLenNeeded + pLsaContext->strDirective[MD5_AUTH_OPAQUE].Length;
    }

    // Next Output the session key only if nonce and cnonce are used
    if (pLsaContext->typeAlgorithm == MD5_SESS)
    {
        cbLenNeeded = cbLenNeeded + pLsaContext->strSessionKey.Length;
    }

    // Now output the unicode domain\accountname
    usAcctNameSize = pLsaContext->ustrAccountName.Length + pLsaContext->ustrDomain.Length + sizeof(WCHAR); // for \ char
    cbLenNeeded = cbLenNeeded + usAcctNameSize;

    DebugLog((DEB_TRACE, "SspMapContext:  Packed Digest will be %d bytes \n", cbLenNeeded));

    //   DigestAllocateMemory will use g_LsaFunctions->AllocateLsaHeap()
    pPackedUserCtxt = (PDIGEST_PACKED_USERCONTEXT)g_LsaFunctions->AllocateLsaHeap(cbLenNeeded);
    if (!pPackedUserCtxt)
    {
        // Failed to allocate memory to send info to usermode space
        ContextData->cbBuffer = 0;
        Status = SEC_E_INSUFFICIENT_MEMORY;
        DebugLog((DEB_ERROR, "SspMapContext: out of memory on usermode contextdata\n"));
        goto CleanUp;
    }

       // Now initialize the UserMode Context struct to return
    ZeroMemory(pPackedUserCtxt, cbLenNeeded);
    pPackedUserCtxt->ExpirationTime = pLsaContext->ExpirationTime;
    pPackedUserCtxt->typeAlgorithm = (ULONG)pLsaContext->typeAlgorithm;
    pPackedUserCtxt->typeCipher = (ULONG)pLsaContext->typeCipher;
    pPackedUserCtxt->typeCharset = (ULONG)pLsaContext->typeCharset;
    pPackedUserCtxt->typeDigest = (ULONG)pLsaContext->typeDigest;
    pPackedUserCtxt->typeQOP = (ULONG)pLsaContext->typeQOP;
    pPackedUserCtxt->ulSendMaxBuf = pLsaContext->ulSendMaxBuf;
    pPackedUserCtxt->ulRecvMaxBuf = pLsaContext->ulRecvMaxBuf;
    pPackedUserCtxt->ContextReq = pLsaContext->ContextReq;
    pPackedUserCtxt->CredentialUseFlags = pLsaContext->CredentialUseFlags;

    // Incorporate any options set for this context (such as FLAG_CONTEXT_REFCOUNT)
    pPackedUserCtxt->ulFlags = pLsaContext->ulFlags | ulFlagOptions;

    // Now mark that there is data for these items  ONLY non-zero items will be written out!!!
    if (pDigest)
    {
        pPackedUserCtxt->uDigestLen[MD5_AUTH_USERNAME] = (ULONG)pDigest->refstrParam[MD5_AUTH_USERNAME].Length;
        pPackedUserCtxt->uDigestLen[MD5_AUTH_REALM] = (ULONG)pDigest->refstrParam[MD5_AUTH_REALM].Length;
        pPackedUserCtxt->uDigestLen[MD5_AUTH_NONCE] = (ULONG)pDigest->refstrParam[MD5_AUTH_NONCE].Length;
        pPackedUserCtxt->uDigestLen[MD5_AUTH_CNONCE] = (ULONG)pDigest->refstrParam[MD5_AUTH_CNONCE].Length;
        pPackedUserCtxt->uDigestLen[MD5_AUTH_ALGORITHM] = (ULONG)pDigest->refstrParam[MD5_AUTH_ALGORITHM].Length;
        pPackedUserCtxt->uDigestLen[MD5_AUTH_QOP] = (ULONG)pDigest->refstrParam[MD5_AUTH_QOP].Length;
        pPackedUserCtxt->uDigestLen[MD5_AUTH_URI] = (ULONG)pDigest->refstrParam[MD5_AUTH_URI].Length;
        pPackedUserCtxt->uDigestLen[MD5_AUTH_AUTHZID] = (ULONG)pDigest->refstrParam[MD5_AUTH_AUTHZID].Length;
        pPackedUserCtxt->uDigestLen[MD5_AUTH_OPAQUE] = (ULONG)pDigest->refstrParam[MD5_AUTH_OPAQUE].Length;
    }
    else
    {
        pPackedUserCtxt->uDigestLen[MD5_AUTH_USERNAME] = (ULONG)pLsaContext->strDirective[MD5_AUTH_USERNAME].Length;
        pPackedUserCtxt->uDigestLen[MD5_AUTH_REALM] = (ULONG)pLsaContext->strDirective[MD5_AUTH_REALM].Length;
        pPackedUserCtxt->uDigestLen[MD5_AUTH_NONCE] = (ULONG)pLsaContext->strDirective[MD5_AUTH_NONCE].Length;
        pPackedUserCtxt->uDigestLen[MD5_AUTH_CNONCE] = (ULONG)pLsaContext->strDirective[MD5_AUTH_CNONCE].Length;
        pPackedUserCtxt->uDigestLen[MD5_AUTH_ALGORITHM] = (ULONG)pLsaContext->strDirective[MD5_AUTH_ALGORITHM].Length;
        pPackedUserCtxt->uDigestLen[MD5_AUTH_QOP] = (ULONG)pLsaContext->strDirective[MD5_AUTH_QOP].Length;
        pPackedUserCtxt->uDigestLen[MD5_AUTH_URI] = (ULONG)pLsaContext->strDirective[MD5_AUTH_URI].Length;
        pPackedUserCtxt->uDigestLen[MD5_AUTH_AUTHZID] = (ULONG)pLsaContext->strDirective[MD5_AUTH_AUTHZID].Length;
        pPackedUserCtxt->uDigestLen[MD5_AUTH_OPAQUE] = (ULONG)pLsaContext->strDirective[MD5_AUTH_OPAQUE].Length;
    }

    // the session key is mapped only if nonce and cnonce are used
    if (pLsaContext->typeAlgorithm == MD5_SESS)
    {
        pPackedUserCtxt->uSessionKeyLen = (ULONG)pLsaContext->strSessionKey.Length;
    }
    else
    {
        pPackedUserCtxt->uSessionKeyLen = 0;
    }

    pPackedUserCtxt->uAccountNameLen = (ULONG)usAcctNameSize;


    // dup token if it exists
    if (pLsaContext->TokenHandle != NULL)
    {
        Status = g_LsaFunctions->DuplicateHandle(
                           pLsaContext->TokenHandle,
                           &(hTemp));

        if (!NT_SUCCESS(Status))
        {
            if (pPackedUserCtxt)
            {
                DigestFreeMemory(pPackedUserCtxt);
                pPackedUserCtxt = NULL;
            }
            ContextData->cbBuffer = 0;
            DebugLog((DEB_ERROR, "SspMapContext: DuplicateHandle returns 0x%lx\n", Status));
            goto CleanUp;
        }
        // Must pack the HANDLE into a fixed size structure for IA64 and i32 formats
        pPackedUserCtxt->ClientTokenHandle = (ULONG) ((ULONG_PTR)hTemp);
        DebugLog((DEB_TRACE, "SspMapContext: DuplicateHandle successful  ClientTokenHandle 0x%x\n", pPackedUserCtxt->ClientTokenHandle));
    }

    // Now copy over the string data elements
    pucLoc = &(pPackedUserCtxt->ucData);
    if (pDigest)
    {
        for (iAuth = 0; iAuth < MD5_AUTH_LAST; iAuth++)
        {
           if (pPackedUserCtxt->uDigestLen[iAuth])
           {
               memcpy(pucLoc, pDigest->refstrParam[iAuth].Buffer, pPackedUserCtxt->uDigestLen[iAuth]);
               pucLoc += pPackedUserCtxt->uDigestLen[iAuth];
           }
        }

        if (pDigest->usFlags & FLAG_AUTHZID_PROVIDED)
        {
            pPackedUserCtxt->ulFlags |= FLAG_CONTEXT_AUTHZID_PROVIDED;
        }
    }
    else
    {
        for (iAuth = 0; iAuth < MD5_AUTH_LAST; iAuth++)
        {
           if (pPackedUserCtxt->uDigestLen[iAuth])
           {
               memcpy(pucLoc, pLsaContext->strDirective[iAuth].Buffer, pPackedUserCtxt->uDigestLen[iAuth]);
               pucLoc += pPackedUserCtxt->uDigestLen[iAuth];
           }
        }
    }

    if (pPackedUserCtxt->uSessionKeyLen)
    {
        memcpy(pucLoc, pLsaContext->strSessionKey.Buffer, pPackedUserCtxt->uSessionKeyLen);
        pucLoc += pPackedUserCtxt->uSessionKeyLen;
    }

    if (usAcctNameSize)
    {
        memcpy(pucLoc, pLsaContext->ustrDomain.Buffer, pLsaContext->ustrDomain.Length);
        pucLoc = pucLoc + pLsaContext->ustrDomain.Length;
        memcpy(pucLoc, L"\\", sizeof(WCHAR));
        pucLoc = pucLoc + sizeof(WCHAR);
        memcpy(pucLoc, pLsaContext->ustrAccountName.Buffer, pLsaContext->ustrAccountName.Length);
        pucLoc = pucLoc + pLsaContext->ustrAccountName.Length;
    }

    ContextData->pvBuffer = pPackedUserCtxt;
    ContextData->cbBuffer = cbLenNeeded;
    ContextData->BufferType = SECBUFFER_TOKEN;


CleanUp:

    DebugLog((DEB_TRACE_FUNC, "SspMapContext: Leaving  LsaContext  %lx    Status 0x%x\n", pLsaContext, Status));
    return(Status);
}




//+--------------------------------------------------------------------
//
//  Function:   DigestUserHTTPHelper
//
//  Synopsis:   Process a SecBuffer with a given User Security Context
//              Used with HTTP for auth after initial ASC/ISC exchange
//
//  Arguments:  pContext - UserMode Context for the security state
//              Op - operation to perform on the Sec buffers
//              pMessage - sec buffers to processs and return output
//
//  Returns: NTSTATUS
//
//  Notes:
//
//---------------------------------------------------------------------

NTSTATUS NTAPI
DigestUserHTTPHelper(
                        IN PDIGEST_USERCONTEXT pContext,
                        IN eSignSealOp Op,
                        IN OUT PSecBufferDesc pSecBuff,
                        IN ULONG MessageSeqNo
                        )
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG     ulSeqNo = 0;
    PSecBuffer pChalRspInputToken = NULL;
    PSecBuffer pMethodInputToken = NULL;
    PSecBuffer pURIInputToken = NULL;
    PSecBuffer pHEntityInputToken = NULL;
    PSecBuffer pFirstOutputToken = NULL;
    DIGEST_PARAMETER Digest;
    USHORT usLen = 0;
    int iAuth = 0;
    char *cptr = NULL;
    char  szNCOverride[2*NCNUM];             // Overrides the provided NC if non-zero using only NCNUM digits
    STRING strURI = {0};

    DebugLog((DEB_TRACE_FUNC, "DigestUserHTTPHelper: Entering \n"));

    Status = DigestInit(&Digest);
    if (!NT_SUCCESS(Status))
    {
        DebugLog((DEB_ERROR, "DigestUserHTTPHelper: Digest init error status 0x%x\n", Status));
        goto CleanUp;
    }

    if (pSecBuff->cBuffers < 1)
    {
        Status = SEC_E_INVALID_TOKEN;
        DebugLog((DEB_ERROR, "DigestUserHTTPHelper: Not enough input buffers 0x%x\n", Status));
        goto CleanUp;
    }
    pChalRspInputToken = &(pSecBuff->pBuffers[0]);
    if (!ContextIsTokenOK(pChalRspInputToken, NTDIGEST_SP_MAX_TOKEN_SIZE))
    {
        Status = SEC_E_INVALID_TOKEN;
        DebugLog((DEB_ERROR, "DigestUserHTTPHelper: ContextIsTokenOK (ChalRspInputToken) failed  0x%x\n", Status));
        goto CleanUp;
    }

    // Set any digest processing parameters based on Context
    if (pContext->ulFlags & FLAG_CONTEXT_NOBS_DECODE)
    {
        Digest.usFlags |= FLAG_NOBS_DECODE;      
    }

    // We have input in the SECBUFFER 0th location - parse it
    Status = DigestParser2(pChalRspInputToken, MD5_AUTH_NAMES, MD5_AUTH_LAST, &Digest);
    if (!NT_SUCCESS(Status))
    {
        DebugLog((DEB_ERROR, "DigestUserHTTPHelper: DigestParser error 0x%x\n", Status));
        goto CleanUp;
    }

       // Now determine all of the other buffers

    DebugLog((DEB_TRACE, "DigestUserHTTPHelper: pContext->ContextReq 0x%lx \n", pContext->ContextReq));

    DebugLog((DEB_TRACE, "DigestUserHTTPHelper: HTTP SecBuffer Format\n"));
    // Retrieve the information from the SecBuffers & check proper formattting
    if (pSecBuff->cBuffers < 4)
    {
        Status = SEC_E_INVALID_TOKEN;
        DebugLog((DEB_ERROR, "DigestUserHTTPHelper: Not enough input buffers 0x%x\n", Status));
        goto CleanUp;
    }
    
    pMethodInputToken = &(pSecBuff->pBuffers[1]);
    if (!ContextIsTokenOK(pMethodInputToken, NTDIGEST_SP_MAX_TOKEN_SIZE))
    {                           // Check to make sure that string is present
        Status = SEC_E_INVALID_TOKEN;
        DebugLog((DEB_ERROR, "DigestUserHTTPHelper: ContextIsTokenOK (MethodInputToken) failed  0x%x\n", Status));
        goto CleanUp;
    }

    pURIInputToken = &(pSecBuff->pBuffers[2]);
    if (!ContextIsTokenOK(pURIInputToken, NTDIGEST_SP_MAX_TOKEN_SIZE))
    {
        Status = SEC_E_INVALID_TOKEN;
        DebugLog((DEB_ERROR, "DigestUserHTTPHelper: ContextIsTokenOK (URIInputToken) failed  0x%x\n", Status));
        goto CleanUp;
    }

    pHEntityInputToken = &(pSecBuff->pBuffers[3]);
    if (!ContextIsTokenOK(pHEntityInputToken, NTDIGEST_SP_MAX_TOKEN_SIZE))
    {
        Status = SEC_E_INVALID_TOKEN;
        DebugLog((DEB_ERROR, "DigestUserHTTPHelper: ContextIsTokenOK (HEntityInputToken) failed  0x%x\n", Status));
        goto CleanUp;
    }

    // Take care of the output buffer
    if (Op == eSign)
    {
        if (pSecBuff->cBuffers < 5)
        {
            Status = SEC_E_INVALID_TOKEN;
            DebugLog((DEB_ERROR, "DigestUserHTTPHelper: No Output Buffers %d\n", Status));
            goto CleanUp;
        }
        pFirstOutputToken = &(pSecBuff->pBuffers[4]);
        if (!ContextIsTokenOK(pFirstOutputToken, 0))
        {
            Status = SEC_E_INVALID_TOKEN;
            DebugLog((DEB_ERROR, "DigestUserHTTPHelper, ContextIsTokenOK (FirstOutputToken) failed  0x%x\n", Status));
            goto CleanUp;
        }

        // Reset output buffer
        if (pFirstOutputToken && (pFirstOutputToken->pvBuffer) && (pFirstOutputToken->cbBuffer >= 1))
        {
            cptr = (char *)pFirstOutputToken->pvBuffer;
            *cptr = '\0';
        }

    }
    else
    {
        pFirstOutputToken = NULL;    // There is no output buffer
    }

    // Verify that there is a valid Method provided
    if (!pMethodInputToken->pvBuffer || !pMethodInputToken->cbBuffer ||
        (PBUFFERTYPE(pMethodInputToken) != SECBUFFER_PKG_PARAMS))
    {
        Status = SEC_E_INVALID_TOKEN;
        DebugLog((DEB_ERROR, "DigestUserHTTPHelper: Method SecBuffer must have valid method string status 0x%x\n", Status));
        goto CleanUp;
    }

    usLen = strlencounted((char *)pMethodInputToken->pvBuffer, (USHORT)pMethodInputToken->cbBuffer);
    if (!usLen)
    {
        Status = SEC_E_INVALID_TOKEN;
        DebugLog((DEB_ERROR, "DigestUserHTTPHelper: Method SecBuffer must have valid method string status 0x%x\n", Status));
        goto CleanUp;
    }
    Digest.refstrParam[MD5_AUTH_METHOD].Length = usLen;
    Digest.refstrParam[MD5_AUTH_METHOD].MaximumLength = (unsigned short)(pMethodInputToken->cbBuffer);
    Digest.refstrParam[MD5_AUTH_METHOD].Buffer = (char *)pMethodInputToken->pvBuffer;       // refernce memory - no alloc!!!!


    // Check to see if we have H(Entity) data to utilize
    if (pHEntityInputToken->cbBuffer)
    {
        // Verify that there is a valid Method provided
        if (!pHEntityInputToken->pvBuffer || (PBUFFERTYPE(pMethodInputToken) != SECBUFFER_PKG_PARAMS))
        {
            Status = SEC_E_INVALID_TOKEN;
            DebugLog((DEB_ERROR, "DigestUserHTTPHelper: HEntity SecBuffer must have valid string status 0x%x\n", Status));
            goto CleanUp;
        }

        usLen = strlencounted((char *)pHEntityInputToken->pvBuffer, (USHORT)pHEntityInputToken->cbBuffer);

        if ((usLen != 0) && (usLen != (MD5_HASH_BYTESIZE * 2)))
        {
            Status = SEC_E_INVALID_TOKEN;
            DebugLog((DEB_ERROR, "DigestUserHTTPHelper: HEntity SecBuffer must have valid MD5 Hash data 0x%x\n", Status));
            goto CleanUp;
        }

        if (usLen)
        {
            Digest.refstrParam[MD5_AUTH_HENTITY].Length = usLen;
            Digest.refstrParam[MD5_AUTH_HENTITY].MaximumLength = (unsigned short)(pHEntityInputToken->cbBuffer);
            Digest.refstrParam[MD5_AUTH_HENTITY].Buffer = (char *)pHEntityInputToken->pvBuffer;       // refernce memory - no alloc!!!!
        }
    }


    // Import the URI if it is a sign otherwise verify URI match if verify
    if (Op == eSign)
    {
        // Pull in the URI provided in SecBuffer
        if (!pURIInputToken || !pURIInputToken->cbBuffer || !pURIInputToken->pvBuffer)
        {
            Status = SEC_E_INVALID_TOKEN;
            DebugLog((DEB_ERROR, "DigestUserHTTPHelper: URI SecBuffer must have valid string 0x%x\n", Status));
            goto CleanUp;
        }


        if (PBUFFERTYPE(pURIInputToken) == SECBUFFER_PKG_PARAMS)
        {
            usLen = strlencounted((char *)pURIInputToken->pvBuffer, (USHORT)pURIInputToken->cbBuffer);

            if (usLen > 0)
            {
                Status = StringCharDuplicate(&strURI, (char *)pURIInputToken->pvBuffer, usLen);
                if (!NT_SUCCESS(Status))
                {
                    DebugLog((DEB_ERROR, "DigestUserHTTPHelper: StringCharDuplicate   error 0x%x\n", Status));
                    goto CleanUp;
                }
            }
        }
        else
        {
            Status = SEC_E_INVALID_TOKEN;
            DebugLog((DEB_ERROR, "DigestUserHTTPHelper: URI buffer type invalid   error %d\n", Status));
            goto CleanUp;
        }

        StringReference(&(Digest.refstrParam[MD5_AUTH_URI]), &strURI);  // refernce memory - no alloc!!!!
    }

    // If we have a NonceCount in the MessageSequenceNumber then use that
    if (MessageSeqNo)
    {
        ulSeqNo = MessageSeqNo;
    }
    else
    {
        ulSeqNo = pContext->ulNC + 1;           // Else use the next sequence number
    }

    sprintf(szNCOverride, "%0.8x", ulSeqNo); // Buffer is twice as big as we need (for safety) so just clip out first 8 characters
    szNCOverride[NCNUM] = '\0';         // clip to 8 digits
    DebugLog((DEB_TRACE, "DigestUserHTTPHelper: Message Sequence NC is %s\n", szNCOverride));
    Digest.refstrParam[MD5_AUTH_NC].Length = (USHORT)NCNUM;
    Digest.refstrParam[MD5_AUTH_NC].MaximumLength = (unsigned short)(NCNUM+1);
    Digest.refstrParam[MD5_AUTH_NC].Buffer = (char *)szNCOverride;          // refernce memory - no alloc!!!!

    // Now link in the stored context values into the digest if this is a SignMessage
    // If there are values there from the input auth line then override them with context's value
    if (Op == eSign)
    {
        for (iAuth = 0; iAuth < MD5_AUTH_LAST; iAuth++)
        {
            if ((iAuth != MD5_AUTH_URI) &&
                (iAuth != MD5_AUTH_HENTITY) &&
                (iAuth != MD5_AUTH_METHOD) &&
                pContext->strParam[iAuth].Length)
            {       // Link in only if passed into the user context from the LSA context
                Digest.refstrParam[iAuth].Length = pContext->strParam[iAuth].Length;
                Digest.refstrParam[iAuth].MaximumLength = pContext->strParam[iAuth].MaximumLength;
                Digest.refstrParam[iAuth].Buffer = pContext->strParam[iAuth].Buffer;          // reference memory - no alloc!!!!
            }
        }
    }

    // Verify that ChallengeResponses directive values are the same as in original ChallangeResponse
    if (Op == eVerify)
    {
        Status = DigestUserCompareDirectives(pContext, &Digest);
        if (!NT_SUCCESS(Status))
        {
            DebugLog((DEB_ERROR, "DigestUserHTTPHelper: DigestUserCompareDirectives     error 0x%x\n", Status));
            goto CleanUp;
        }
    }

    DebugLog((DEB_TRACE, "DigestUserHTTPHelper: Digest inputs processing completed\n"));

    Status = DigestUserProcessParameters(pContext, &Digest, pFirstOutputToken);
    if (!NT_SUCCESS(Status))
    {
        DebugLog((DEB_ERROR, "DigestUserHTTPHelper: DigestUserProcessParameters     error 0x%x\n", Status));
        goto CleanUp;
    }

    pContext->ulNC = ulSeqNo;                           // Everything verified so increment to next nonce count

    // Keep a copy of the new URI in ChallengeResponse
    StringFree(&(pContext->strParam[MD5_AUTH_URI]));
    Status = StringDuplicate(&(pContext->strParam[MD5_AUTH_URI]), &(Digest.refstrParam[MD5_AUTH_URI])); 
    if (!NT_SUCCESS(Status))
    {
        DebugLog((DEB_ERROR, "DigestUserHTTPHelper: Failed to copy URI\n"));
        goto CleanUp;
    }

CleanUp:

    DigestFree(&Digest);

    StringFree(&strURI);

    DebugLog((DEB_TRACE_FUNC, "DigestUserHTTPHelper: Leaving    Status 0x%x\n", Status));

    return(Status);
}



//+--------------------------------------------------------------------
//
//  Function:   DigestUserSignHelperMulti
//
//  Synopsis:   Process multiple SecBuffers with a given User Security Context
//              Used with SASL section 2.3 RFC
//
//  Arguments:  pContext - UserMode Context for the security state
//              Op - operation to perform on the Sec buffers
//              pMessage - sec buffers to processs and return output
//                    
//
//  Returns: NTSTATUS
//
//  Notes:
//
//---------------------------------------------------------------------

NTSTATUS NTAPI
DigestUserSignHelperMulti(
                        IN PDIGEST_USERCONTEXT pContext,
                        IN OUT PSecBufferDesc pSecBuff,
                        IN ULONG MessageSeqNo
                        )
{
    NTSTATUS Status = STATUS_SUCCESS;

    PDWORD    pdwSeqNum = NULL;             // points to the Sequence number to use
    PSecBuffer pSecBufToken = NULL;
    PSecBuffer pSecBufPad = NULL;

    PSecBuffer pSecBufHMAC = NULL;          // Points to the HMAC appended to the data block

    BOOL bServer = FALSE;
    SASL_MAC_BLOCK  MacBlock = {0};
    STRING  strcSignKeyConst = {0};     // pointer to a constant valued string

    ULONG Index = 0;

    UNREFERENCED_PARAMETER(MessageSeqNo);    

    DebugLog((DEB_TRACE_FUNC, "DigestUserSignHelperMulti: Entering \n"));

    bServer = pContext->CredentialUseFlags & DIGEST_CRED_INBOUND;

    
    //
    // Find the body and signature SecBuffers from pMessage
    //

    for (Index = 0; Index < pSecBuff->cBuffers ; Index++ )
    {
        if (BUFFERTYPE(pSecBuff->pBuffers[Index]) == SECBUFFER_TOKEN)
        {
            pSecBufToken = &pSecBuff->pBuffers[Index];
        }
        if (BUFFERTYPE(pSecBuff->pBuffers[Index]) == SECBUFFER_PADDING)
        {
            pSecBufPad = &pSecBuff->pBuffers[Index];
        }
    }

    if ((!pSecBufPad) || (!pSecBufPad->cbBuffer))
    {   // If no SECBUFFER_PADDING, use SECBUFFER_TOKEN
        pSecBufHMAC = pSecBufToken;
    }
    else
    {
        pSecBufHMAC = pSecBufPad;
        if (pSecBufToken)
        {
            pSecBufToken->cbBuffer = 0;
        }
    }
    if (!pSecBufHMAC || !ContextIsTokenOK(pSecBufHMAC, 0) || (pSecBufHMAC->cbBuffer < MAC_BLOCK_SIZE))
    {
        Status = SEC_E_BUFFER_TOO_SMALL;
        DebugLog((DEB_ERROR, "DigestUserSignHelperMulti: ContextIsTokenOK (SignatureToken) failed  0x%x\n", Status));
        goto CleanUp;
    }

    // Determine the sequence number & Constant Key Sring to utilize acting as the server
    if (bServer)
    {
        pdwSeqNum = &(pContext->dwSendSeqNum);
        RtlInitString(&strcSignKeyConst, SASL_S2C_SIGN_KEY);
        DebugLog((DEB_TRACE, "DigestUserSignHelperMulti: Signing in Server Mode (Message StoC)  SeqNum %d\n", *pdwSeqNum));
    }
    else
    {             // acting as the client
        pdwSeqNum = &(pContext->dwSendSeqNum);
        RtlInitString(&strcSignKeyConst, SASL_C2S_SIGN_KEY);
        DebugLog((DEB_TRACE, "DigestUserSignHelperMulti: Signing in Client Mode (Message CtoS)  SeqNum %d\n", *pdwSeqNum));
    }

    Status = CalculateSASLHMACMulti(pContext, TRUE, &strcSignKeyConst, *pdwSeqNum,
                               pSecBuff, &MacBlock);
    if (!NT_SUCCESS (Status))
    {
        DebugLog((DEB_ERROR, "DigestUserSignHelperMulti: Error in CalculateSASLHMACMulti   status 0x%x\n", Status));
        goto CleanUp;
    }

        // Write the calculated MAC block out to the SecBuffer
    memcpy(pSecBufHMAC->pvBuffer, &MacBlock, MAC_BLOCK_SIZE);
    DebugLog((DEB_TRACE, "DigestUserSignHelper: Wrote out the calculated MAC Block.\n"));
    pSecBufHMAC->cbBuffer = MAC_BLOCK_SIZE;           // indicate number of bytes we used for padding and HMAC block

    // completed all tasks down to here.  Need to update the sequence number
    (*pdwSeqNum)++;
    DebugLog((DEB_TRACE, "DigestUserSignHelperMulti: Updated SeqNum to %d\n", *pdwSeqNum));


CleanUp:

    DebugLog((DEB_TRACE_FUNC, "DigestUserSignHelperMulti: Leaving    Status 0x%x\n", Status));

    return(Status);
}




//+--------------------------------------------------------------------
//
//  Function:   DigestUserVerifyHelper
//
//  Synopsis:   Process a SecBuffer with a given User Security Context
//              Used with SASL section 2.3 RFC
//
//  Arguments:  pContext - UserMode Context for the security state
//              Op - operation to perform on the Sec buffers
//              pMessage - sec buffers to processs and return output
//                    
//
//  Returns: NTSTATUS
//
//  Notes:
//
//---------------------------------------------------------------------

NTSTATUS NTAPI
DigestUserVerifyHelper(
                        IN PDIGEST_USERCONTEXT pContext,
                        IN OUT PSecBufferDesc pSecBuff,
                        IN ULONG MessageSeqNo
                        )
{
    NTSTATUS Status = STATUS_SUCCESS;

    PDWORD    pdwSeqNum = NULL;             // points to the Sequence number to use
    PBYTE     pMsgHMAC  = NULL;             // Location of the HMAC in the message
    PSecBuffer pSecBufData = NULL;
    PSecBuffer pSecBufStream = NULL;
    PSecBuffer pSecBufMsg = NULL;          // Points to the data section

    BOOL bServer = FALSE;
    SASL_MAC_BLOCK  MacBlock = {0};
    SASL_MAC_BLOCK  TokenMacBlock = {0};
    STRING  strcSignKeyConst = {0};
    ULONG cbSecBufMsgIntegrity = 0;        // Number of bytes in message to calc HMAC on

    ULONG Index = 0;

#if DBG
    char szTemp[TEMPSIZE];
    ZeroMemory(szTemp, TEMPSIZE);
#endif
    
    UNREFERENCED_PARAMETER(MessageSeqNo);    

    DebugLog((DEB_TRACE_FUNC, "DigestUserVerifyHelper: Entering \n"));

    bServer = pContext->CredentialUseFlags & DIGEST_CRED_INBOUND;

    
    //
    // Find the body and signature SecBuffers from pMessage
    //

    for (Index = 0; Index < pSecBuff->cBuffers ; Index++ )
    {
        if (BUFFERTYPE(pSecBuff->pBuffers[Index]) == SECBUFFER_DATA)
        {
            pSecBufData = &pSecBuff->pBuffers[Index];
        }
        if (BUFFERTYPE(pSecBuff->pBuffers[Index]) == SECBUFFER_STREAM)
        {
            pSecBufStream = &pSecBuff->pBuffers[Index];
        }
    }


    // Must be for decrypt/verify
    if ((!pSecBufStream) || (!pSecBufStream->cbBuffer))
    {   // If no SECBUFFER_STREAM, use SECBUFFER_DATA
        pSecBufMsg = pSecBufData;
    }
    else
    {
        pSecBufMsg = pSecBufStream;
    }

    if (!pSecBufMsg || (!ContextIsTokenOK(pSecBufMsg, 0)) || (pSecBufMsg->cbBuffer < MAC_BLOCK_SIZE))
    {
        Status = SEC_E_INVALID_TOKEN;
        DebugLog((DEB_ERROR, "DigestUserVerifyHelper: ContextIsTokenOK (SecBufMsg) decrypt/verify failed  0x%x\n", Status));
        goto CleanUp;
    }
    
    // Strip off the MsgType and the Sequence Number
    cbSecBufMsgIntegrity = pSecBufMsg->cbBuffer - (MAC_BLOCK_SIZE);


    // Determine the sequence number to utilize acting as the server
    if (bServer)
    {
        pdwSeqNum = &(pContext->dwRecvSeqNum);
        RtlInitString(&strcSignKeyConst, SASL_C2S_SIGN_KEY);
        DebugLog((DEB_TRACE, "DigestUserVerifyHelper: Verifying in Server Mode (Message CtoS)  SeqNum %d\n", *pdwSeqNum));
    }
    else
    {             // acting as the client
        pdwSeqNum = &(pContext->dwRecvSeqNum);
        RtlInitString(&strcSignKeyConst, SASL_S2C_SIGN_KEY);
        DebugLog((DEB_TRACE, "DigestUserVerifyHelper: Verifying in Client Mode (Message StoC)  SeqNum %d\n", *pdwSeqNum));
    }



    Status = CalculateSASLHMAC(pContext, FALSE, &strcSignKeyConst, *pdwSeqNum,
                               (PBYTE)pSecBufMsg->pvBuffer, cbSecBufMsgIntegrity, &MacBlock);
    if (!NT_SUCCESS (Status))
    {
        DebugLog((DEB_ERROR, "DigestUserVerifyHelper: Error in CalculateSASLHMAC   status 0x%x\n", Status));
        goto CleanUp;
    }


    DebugLog((DEB_TRACE, "DigestUserVerifyHelper: Ready to compare MacBlocks\n"));

    // Check validity of MAC block ONLY do not write it out
    pMsgHMAC =  (PBYTE)pSecBufMsg->pvBuffer + cbSecBufMsgIntegrity;
    memcpy(&TokenMacBlock, pMsgHMAC, MAC_BLOCK_SIZE); 
    if (MacBlock.dwSeqNumber != TokenMacBlock.dwSeqNumber)
    {
        Status = SEC_E_OUT_OF_SEQUENCE;
        DebugLog((DEB_ERROR, "DigestUserVerifyHelper: SASL MAC blocks out of sequence. Failed verify.  Status 0x%x\n", Status));
#if DBG
    ZeroMemory(szTemp, TEMPSIZE);
    BinToHex((PUCHAR)&TokenMacBlock, MAC_BLOCK_SIZE, szTemp);
    DebugLog((DEB_TRACE, "CalculateSASLHMAC: Token's HMAC-MD5 block %s\n", szTemp));
    ZeroMemory(szTemp, TEMPSIZE);
    BinToHex((PUCHAR)&MacBlock, MAC_BLOCK_SIZE, szTemp);
    DebugLog((DEB_TRACE, "CalculateSASLHMAC: TComputed HMAC-MD5 block %s\n", szTemp));
#endif
        goto CleanUp;
    }
    if (memcmp(&MacBlock, &TokenMacBlock, MAC_BLOCK_SIZE))
    {
        Status = SEC_E_MESSAGE_ALTERED;
        DebugLog((DEB_ERROR, "DigestUserVerifyHelper: SASL MAC blocks do not match. Failed verify.  Status 0x%x\n", Status));
#if DBG
    ZeroMemory(szTemp, TEMPSIZE);
    BinToHex((PUCHAR)&TokenMacBlock, MAC_BLOCK_SIZE, szTemp);
    DebugLog((DEB_TRACE, "CalculateSASLHMAC: Token's HMAC-MD5 block %s\n", szTemp));
    ZeroMemory(szTemp, TEMPSIZE);
    BinToHex((PUCHAR)&MacBlock, MAC_BLOCK_SIZE, szTemp);
    DebugLog((DEB_TRACE, "CalculateSASLHMAC: TComputed HMAC-MD5 block %s\n", szTemp));
#endif
        goto CleanUp;
    }
    else
    {
        DebugLog((DEB_TRACE, "DigestUserVerifyHelper: SASL MAC blocks match!\n"));
    }


    // completed all tasks down to here.  Need to update the sequence number

    (*pdwSeqNum)++;

    // Update the Data information (without the attached HMAC info block
    if (pSecBufData)
    {
        pSecBufData->cbBuffer = pSecBufMsg->cbBuffer - MAC_BLOCK_SIZE;
        pSecBufData->pvBuffer = pSecBufMsg->pvBuffer;
    }
    DebugLog((DEB_TRACE, "DigestUserVerifyHelper: Updated SeqNum to %d\n", *pdwSeqNum));


CleanUp:

    DebugLog((DEB_TRACE_FUNC, "DigestUserVerifyHelper: Leaving    Status 0x%x\n", Status));

    return(Status);

}



//+--------------------------------------------------------------------
//
//  Function:   DigestUserSealHelperMulti
//
//  Synopsis:   Process a SecBuffer with a given User Security Context
//              Used with SASL section 2.3 RFC Supports Multiple Data Secbuffers
//
//  Arguments:  pContext - UserMode Context for the security state
//              Op - operation to perform on the Sec buffers
//              pMessage - sec buffers to processs and return output
//                    
//
//  Returns: NTSTATUS
//
//  Notes:
//
//---------------------------------------------------------------------

NTSTATUS NTAPI
DigestUserSealHelperMulti(
                        IN PDIGEST_USERCONTEXT pContext,
                        IN OUT PSecBufferDesc pSecBuff,
                        IN ULONG MessageSeqNo
                        )
{
    NTSTATUS Status = STATUS_SUCCESS;


    PDWORD    pdwSeqNum = NULL;             // points to the Sequence number to use
    PSecBuffer pSecBufToken = NULL;
    PSecBuffer pSecBufPad = NULL;
    PSecBuffer pSecBufHMAC = NULL;          // Points to the HMAC appended to the data block

    BOOL bServer = FALSE;
    SASL_MAC_BLOCK  MacBlock = {0};
    STRING  strcSignKeyConst = {0};
    STRING  strcSealKeyConst = {0};
    PUCHAR  pbIV = NULL;

    BYTE bKcTempData[MD5_HASH_BYTESIZE];    // Message integrity keys RFC 2831 sec 2.3

    ULONG Index = 0;
    USHORT cbHA1n = 0;         // Number of bytes for Ha1 in Kcc/Kcs
    DWORD cbKey = 0;             // Number of bytes of Kcc/Kcs to use for the key
    DWORD cbKeyNoParity = 0;             // Number of bytes of Kcc/Kcs to use for the key with no parity
    DWORD cbTempKey = 0;
    ULONG cbBlockSize = RC4_BLOCKSIZE;    // Blocksize for the given cipher
    ULONG cbPrefixPadding = 0;   // number of bytes needed for padding out to blocksize
    ULONG cbBlocks = 0;
    PBYTE pHMACTemp = NULL;
    ALG_ID Algid = 0;
    ULONG cbTotalData = 0;           // total number of bytes to process in Data SecBuffers

    UNREFERENCED_PARAMETER(MessageSeqNo);    

    DebugLog((DEB_TRACE_FUNC, "DigestUserSealHelperMulti: Entering \n"));

    bServer = pContext->CredentialUseFlags & DIGEST_CRED_INBOUND;

    
    //
    // Find the body and signature SecBuffers from pMessage
    //

    for (Index = 0; Index < pSecBuff->cBuffers ; Index++ )
    {
        if (BUFFERTYPE(pSecBuff->pBuffers[Index]) == SECBUFFER_TOKEN)
        {
            pSecBufToken = &pSecBuff->pBuffers[Index];
        }
        if (BUFFERTYPE(pSecBuff->pBuffers[Index]) == SECBUFFER_PADDING)
        {
            pSecBufPad = &pSecBuff->pBuffers[Index];
        }
    }

    if ((!pSecBufPad) || (!pSecBufPad->cbBuffer))
    {   // If no SECBUFFER_PADDING, use SECBUFFER_TOKEN
        pSecBufHMAC = pSecBufToken;
    }
    else
    {
        pSecBufHMAC = pSecBufPad;
        if (pSecBufToken)
        {
            pSecBufToken->cbBuffer = 0;
        }
    }
    if (!pSecBufHMAC || !ContextIsTokenOK(pSecBufHMAC, 0) || (pSecBufHMAC->cbBuffer < (MAC_BLOCK_SIZE + MAX_PADDING)))
    {
        Status = SEC_E_BUFFER_TOO_SMALL;
        DebugLog((DEB_ERROR, "DigestUserSealHelperMulti: ContextIsTokenOK (SignatureToken) failed  0x%x\n", Status));
        goto CleanUp;
    }

    // Determine the sequence number & Constant Key Sring to utilize acting as the server
    if (bServer)
    {
        pdwSeqNum = &(pContext->dwSendSeqNum);
        RtlInitString(&strcSignKeyConst, SASL_S2C_SIGN_KEY);
        RtlInitString(&strcSealKeyConst, SASL_S2C_SEAL_KEY);
        DebugLog((DEB_TRACE, "DigestUserSealHelperMulti: Signing in Server Mode (Message StoC)  SeqNum %d\n", *pdwSeqNum));
    }
    else
    {             // acting as the client
        pdwSeqNum = &(pContext->dwSendSeqNum);
        RtlInitString(&strcSignKeyConst, SASL_C2S_SIGN_KEY);
        RtlInitString(&strcSealKeyConst, SASL_C2S_SEAL_KEY);
        DebugLog((DEB_TRACE, "DigestUserSealHelperMulti: Signing in Client Mode (Message CtoS)  SeqNum %d\n", *pdwSeqNum));
    }

    // Based on the Cypher selected - establish the byte count parameters - magic numbers from RFC

    if (pContext->typeCipher == CIPHER_RC4)
    {
        cbHA1n = 16;    // RFC 2831 sect 2.4
        cbKey = 16;    // number of bytes to use from Kcc/Kcs
        Algid = CALG_RC4;
    }
    else if (pContext->typeCipher == CIPHER_RC4_40)
    {
        cbHA1n = 5;    // RFC 2831 sect 2.4
        cbKey = 16;    // number of bytes to use from Kcc/Kcs
        Algid = CALG_RC4;
    }
    else if (pContext->typeCipher == CIPHER_RC4_56)
    {
        cbHA1n = 7;    // RFC 2831 sect 2.4
        cbKey = 16;    // number of bytes to use from Kcc/Kcs
        Algid = CALG_RC4;
    }
    else if (pContext->typeCipher == CIPHER_DES)
    {
        cbHA1n = 16;    // RFC 2831 sect 2.4
        cbKey = 8;    // number of bytes to use from Kcc/Kcs
        cbKeyNoParity = 7;
        cbBlockSize = DES_BLOCKSIZE;  // DES uses a blocksize of 8
        Algid = CALG_DES;
    }
    else if (pContext->typeCipher == CIPHER_3DES)
    {
        cbHA1n = 16;    // RFC 2831 sect 2.4
        cbKey = 16;    // number of bytes to use from Kcc/Kcs
        cbKeyNoParity = 14;
        cbBlockSize = DES_BLOCKSIZE;  // DES uses a blocksize of 8
        Algid = CALG_3DES_112;
    }
    else
    {
        Status = SEC_E_CRYPTO_SYSTEM_INVALID;
        DebugLog((DEB_ERROR, "DigestUserSealHelperMulti: ContextIsTokenOK (SecBufMsg) failed  0x%x\n", Status));
        goto CleanUp;
    }

    Status = CalculateDataCount(pSecBuff, &cbTotalData);
    if (!NT_SUCCESS (Status))
    {
        DebugLog((DEB_ERROR, "DigestUserSealHelperMulti: Error in CalculateDataCount   status 0x%x\n", Status));
        goto CleanUp;
    }

    // If the cipher is not a stream cipher - the place prefix padding before SASL MAC
    //  Modified to include padding based on message datasize + the 10 byte HMAC
    if (cbBlockSize != 1)
    {
        cbBlocks =  (cbTotalData + SASL_MAC_HMAC_SIZE) / cbBlockSize;         // integer divison
        cbPrefixPadding = cbBlockSize - ((cbTotalData + SASL_MAC_HMAC_SIZE) - (cbBlockSize * cbBlocks));
        if (!cbPrefixPadding)
        {
            cbPrefixPadding = cbBlockSize;      // if padding is zero set it to the blocksize - i.e. always pad
        }
        DebugLog((DEB_TRACE, "DigestUserSealHelperMulti: TotalDataSize %lu  BlockSize %lu  Padding %lu\n",
                   cbTotalData, cbBlockSize, cbPrefixPadding));
    }

    Status = CalculateSASLHMACMulti(pContext, TRUE, &strcSignKeyConst, *pdwSeqNum,
                               pSecBuff, &MacBlock);
    if (!NT_SUCCESS (Status))
    {
        DebugLog((DEB_ERROR, "DigestUserSealHelper: Error in CalculateSASLHMACMulti   status 0x%x\n", Status));
        goto CleanUp;
    }

        // Write the calculated MAC block out to the SecBuffer
        // Put the padding as the prefix
    pHMACTemp = (PBYTE)pSecBufHMAC->pvBuffer;
    memset(pHMACTemp, cbPrefixPadding, cbPrefixPadding);
    memcpy(pHMACTemp + cbPrefixPadding, &MacBlock, MAC_BLOCK_SIZE);
    DebugLog((DEB_TRACE, "DigestUserSealHelperMulti: Wrote out the calculated MAC Block.\n"));
    pSecBufHMAC->cbBuffer = MAC_BLOCK_SIZE + cbPrefixPadding;  // indicate number of bytes we used for padding and HMAC block

        // Completed the Integrity calculation, now encrypt the data if requested
        // Encrypt the message, padding and first SASL_MAC_HMAC_SIZE (10) bytes of HMAC (the integrity value)

    // Compute Kc for encryption (seal) & generate Cryptkey
    if (pContext->hSealCryptKey == NULL)
    {
        ASSERT(*pdwSeqNum == 0);    // Should be first call into package

        // Compute on first time call to encrypt - save for other sequence numbers
        Status = CalculateKc(pContext->bSessionKey, cbHA1n, &strcSealKeyConst, pContext->bKcSealHashData);
        if (!NT_SUCCESS (Status))
        {
            DebugLog((DEB_ERROR, "DigestUserSealHelperMulti: Error in CalculateKc   status 0x%x\n", Status));
            goto CleanUp;
        }

        // code to expand the DES key into multiple of 8 bytes (key with parity)
        if ((pContext->typeCipher == CIPHER_DES) || (pContext->typeCipher == CIPHER_3DES))
        {
            Status = AddDESParity(pContext->bKcSealHashData,
                                  cbKeyNoParity,
                                  bKcTempData,
                                  &cbTempKey);
            if (!NT_SUCCESS (Status))
            {
                DebugLog((DEB_ERROR, "DigestUserSealHelperMulti: Error in AddDESParity   status 0x%x\n", Status));
                goto CleanUp;
            }
            // replace with DES parity version
            ASSERT(cbKey == cbTempKey);
            memcpy(pContext->bSealKey, bKcTempData, cbTempKey);
            pbIV = &(pContext->bKcSealHashData[8]);
        }
        else
        {
            memcpy(pContext->bSealKey, pContext->bKcSealHashData, MD5_HASH_BYTESIZE);
            pbIV = NULL;
        }

        //  generate symmetric key from the cleartext
        Status = CreateSymmetricKey(Algid, cbKey, pContext->bSealKey, pbIV, &pContext->hSealCryptKey);
        if (!NT_SUCCESS (Status))
        {
            DebugLog((DEB_ERROR, "DigestUserSealHelperMulti: Error in CalculateKc   status 0x%x\n", Status));
            goto CleanUp;
        }

    }

    Status = EncryptData2Multi(pContext->hSealCryptKey,
                          cbBlockSize,
                          pSecBuff,
                          (cbPrefixPadding + SASL_MAC_HMAC_SIZE),
                          pHMACTemp);

    if (!NT_SUCCESS (Status))
    {
        DebugLog((DEB_ERROR, "DigestUserSealHelperMulti: Error in EncryptData3   status 0x%x\n", Status));
        goto CleanUp;
    }

    DebugLog((DEB_TRACE, "DigestUserSealHelperMulti: Data encrypted\n"));

    // completed all tasks down to here.  Need to update the sequence number
    (*pdwSeqNum)++;
    DebugLog((DEB_TRACE, "DigestUserSealHelperMulti: Updated SeqNum to %d\n", *pdwSeqNum));


CleanUp:

    DebugLog((DEB_TRACE_FUNC, "DigestUserSealHelperMulti: Leaving    Status 0x%x\n", Status));
    

    return(Status);
}



//+--------------------------------------------------------------------
//
//  Function:   DigestUserUnsealHelper
//
//  Synopsis:   Process a SecBuffer with a given User Security Context
//              Used with SASL section 2.3 RFC
//
//  Arguments:  pContext - UserMode Context for the security state
//              Op - operation to perform on the Sec buffers
//              pMessage - sec buffers to processs and return output
//                    
//
//  Returns: NTSTATUS
//
//  Notes:
//
//---------------------------------------------------------------------
NTSTATUS NTAPI
DigestUserUnsealHelper(
                        IN PDIGEST_USERCONTEXT pContext,
                        IN OUT PSecBufferDesc pSecBuff,
                        IN ULONG MessageSeqNo
                        )
{
    NTSTATUS Status = STATUS_SUCCESS;

    PDWORD    pdwSeqNum = NULL;             // points to the Sequence number to use
    PSecBuffer pSecBufData = NULL;
    PSecBuffer pSecBufStream = NULL;
    PSecBuffer pSecBufMsg = NULL;          // Points to the data section

    BOOL bServer = FALSE;
    SASL_MAC_BLOCK  MacBlock = {0};
    SASL_MAC_BLOCK  TokenMacBlock = {0};         // Extract the HMAC block imbedded in the message
    STRING  strcSignKeyConst = {0};
    STRING  strcSealKeyConst = {0};
    PBYTE  pMsgHMAC = NULL;

    BYTE bKcTempData[MD5_HASH_BYTESIZE];    // Message integrity keys RFC 2831 sec 2.3
    PUCHAR pbIV = NULL;

    ULONG Index = 0;
    USHORT cbHA1n = 0;          // Number of bytes for Ha1 in Kcc/Kcs
    DWORD cbKey = 0;             // Number of bytes of Kcc/Kcs to use for the key
    DWORD cbKeyNoParity = 0;     // Number of bytes of Kcc/Kcs to use for the key with no parity
    DWORD cbTempKey = 0;
    ULONG cbBlockSize = 1;    // Blocksize for the given cipher
    UCHAR cbPrefixPadding = 0;   // number of bytes needed for padding out to blocksize
    ULONG cbMsg = 0;            // number of bytes in the actual message
    PBYTE pMsgPadding = NULL;   // Location of a padding byte
    ALG_ID Algid = 0;

    ULONG cbSecBufMsgPrivacy = 0;            // Number of bytes to decrypt (unseal)

#if DBG
    char szTemp[TEMPSIZE];
    ULONG  iTempLen = 20;
    ZeroMemory(szTemp, TEMPSIZE);
#endif
    
    UNREFERENCED_PARAMETER(MessageSeqNo);    

    DebugLog((DEB_TRACE_FUNC, "DigestUserUnsealHelper: Entering\n"));

    bServer = pContext->CredentialUseFlags & DIGEST_CRED_INBOUND;

    
    //
    // Find the body and signature SecBuffers from pMessage
    //

    for (Index = 0; Index < pSecBuff->cBuffers ; Index++ )
    {
        if (BUFFERTYPE(pSecBuff->pBuffers[Index]) == SECBUFFER_DATA)
        {
            pSecBufData = &pSecBuff->pBuffers[Index];
        }
        if (BUFFERTYPE(pSecBuff->pBuffers[Index]) == SECBUFFER_STREAM)
        {
            pSecBufStream = &pSecBuff->pBuffers[Index];
        }
    }

    // Must be for decrypt/verify
    if ((!pSecBufStream) || (!pSecBufStream->cbBuffer))
    {   // If no SECBUFFER_STREAM, use SECBUFFER_DATA
        pSecBufMsg = pSecBufData;
    }
    else
    {
        pSecBufMsg = pSecBufStream;
    }

    if (!pSecBufMsg || (!ContextIsTokenOK(pSecBufMsg, 0)) || (pSecBufMsg->cbBuffer < MAC_BLOCK_SIZE))
    {
        Status = SEC_E_INVALID_TOKEN;
        DebugLog((DEB_ERROR, "DigestUserUnsealHelper: ContextIsTokenOK (SecBufMsg) decrypt/verify failed  0x%x\n", Status));
        goto CleanUp;
    }
    
    // Strip off the MsgType and the Sequence Number
    cbSecBufMsgPrivacy = pSecBufMsg->cbBuffer - (SASL_MAC_MSG_SIZE + SASL_MAC_SEQ_SIZE);

    if (!pSecBufMsg || !ContextIsTokenOK(pSecBufMsg, 0) || (pSecBufMsg->cbBuffer < MAC_BLOCK_SIZE))
    {
        Status = SEC_E_BUFFER_TOO_SMALL;
        DebugLog((DEB_ERROR, "DigestUserUnsealHelper: ContextIsTokenOK (SignatureToken) failed  0x%x\n", Status));
        goto CleanUp;
    }

    // Determine the sequence number & Constant Key Sring to utilize acting as the server
    if (bServer)
    {
        pdwSeqNum = &(pContext->dwRecvSeqNum);
        RtlInitString(&strcSignKeyConst, SASL_C2S_SIGN_KEY);
        RtlInitString(&strcSealKeyConst, SASL_C2S_SEAL_KEY);
        DebugLog((DEB_TRACE, "DigestUserUnsealHelper: Signing in Server Mode (Message StoC)  SeqNum %d\n", *pdwSeqNum));
    }
    else
    {             // acting as the client
        pdwSeqNum = &(pContext->dwRecvSeqNum);
        RtlInitString(&strcSignKeyConst, SASL_S2C_SIGN_KEY);
        RtlInitString(&strcSealKeyConst, SASL_S2C_SEAL_KEY);
        DebugLog((DEB_TRACE, "DigestUserUnsealHelper: Signing in Client Mode (Message CtoS)  SeqNum %d\n", *pdwSeqNum));
    }

    // Based on the Cypher selected - establish the byte count parameters - magic numbers from RFC

    if (pContext->typeCipher == CIPHER_RC4)
    {
        cbHA1n = 16;    // RFC 2831 sect 2.4
        cbKey = 16;    // number of bytes to use from Kcc/Kcs
        Algid = CALG_RC4;
    }
    else if (pContext->typeCipher == CIPHER_RC4_40)
    {
        cbHA1n = 5;    // RFC 2831 sect 2.4
        cbKey = 16;    // number of bytes to use from Kcc/Kcs
        Algid = CALG_RC4;
    }
    else if (pContext->typeCipher == CIPHER_RC4_56)
    {
        cbHA1n = 7;    // RFC 2831 sect 2.4
        cbKey = 16;    // number of bytes to use from Kcc/Kcs
        Algid = CALG_RC4;
    }
    else if (pContext->typeCipher == CIPHER_DES)
    {
        cbHA1n = 16;    // RFC 2831 sect 2.4
        cbKey = 8;    // number of bytes to use from Kcc/Kcs
        cbKeyNoParity = 7;
        cbBlockSize = 8;  // DES uses a blocksize of 8
        Algid = CALG_DES;
    }
    else if (pContext->typeCipher == CIPHER_3DES)
    {
        cbHA1n = 16;    // RFC 2831 sect 2.4
        cbKey = 16;    // number of bytes to use from Kcc/Kcs
        cbKeyNoParity = 14;
        cbBlockSize = 8;  // DES uses a blocksize of 8
        Algid = CALG_3DES_112;
    }
    else
    {
        Status = SEC_E_CRYPTO_SYSTEM_INVALID;
        DebugLog((DEB_ERROR, "DigestUserUnsealHelper: ContextIsTokenOK (SecBufMsg) failed  0x%x\n", Status));
        goto CleanUp;
    }

        // Decrypt the message, padding and first SASL_MAC_HMAC_SIZE (10) bytes of HMAC (the integrity value)

    // Compute Kc for encryption (seal)
    if (pContext->hUnsealCryptKey == NULL)
    {
        ASSERT(*pdwSeqNum == 0);
        // Compute on first time call to encrypt - save for other sequence numbers
        Status = CalculateKc(pContext->bSessionKey, cbHA1n, &strcSealKeyConst, pContext->bKcUnsealHashData);
        if (!NT_SUCCESS (Status))
        {
            DebugLog((DEB_ERROR, "DigestUserUnsealHelper: Error in CalculateKc   status 0x%x\n", Status));
            goto CleanUp;
        }

        // code to expand the DES key into multiple of 8 bytes (key with parity)
        if ((pContext->typeCipher == CIPHER_DES) || (pContext->typeCipher == CIPHER_3DES))
        {
            Status = AddDESParity(pContext->bKcUnsealHashData,
                                  cbKeyNoParity,
                                  bKcTempData,
                                  &cbTempKey);
            if (!NT_SUCCESS (Status))
            {
                DebugLog((DEB_ERROR, "DigestUserUnsealHelper: Error in AddDESParity   status 0x%x\n", Status));
                goto CleanUp;
            }
            // replace with DES parity version
            ASSERT(cbKey == cbTempKey);
            memcpy(pContext->bUnsealKey, bKcTempData, cbKey);
            pbIV = &(pContext->bKcUnsealHashData[8]);
        }
        else
        {
            // For RC4 ciphers
            memcpy(pContext->bUnsealKey, pContext->bKcUnsealHashData, MD5_HASH_BYTESIZE);
            pbIV = NULL;
        }

        //  generate the symmetric key from the cleartext
        Status = CreateSymmetricKey(Algid, cbKey, pContext->bUnsealKey, pbIV, &pContext->hUnsealCryptKey);
        if (!NT_SUCCESS (Status))
        {
            DebugLog((DEB_ERROR, "DigestUserUnsealHelper: Error in CalculateKc   status 0x%x\n", Status));
            goto CleanUp;
        }
    }


    if ((pContext->typeCipher == CIPHER_3DES) || (pContext->typeCipher == CIPHER_DES))
    {

             // Specify IV  - take only the last 8 bytes per RFC 2831 sect 2.4
        Status = DecryptData(pContext->hUnsealCryptKey, cbSecBufMsgPrivacy,
                             (PUCHAR)pSecBufMsg->pvBuffer);

        if (!NT_SUCCESS (Status))
        {
            DebugLog((DEB_ERROR, "DigestUserUnsealHelper: Error in DecryptData   status 0x%x\n", Status));
            goto CleanUp;
        }

        // Padding length is indicated in the actual padding - get the pad byte near HMAC
        if (pSecBufMsg->cbBuffer  < (MAC_BLOCK_SIZE + 1))
        {
            Status = STATUS_INTERNAL_ERROR;
            DebugLog((DEB_ERROR, "DigestUserUnsealHelper: Determining padding not enough space   status 0x%x\n", Status));
            goto CleanUp;
        }
        pMsgPadding =  (PBYTE)pSecBufMsg->pvBuffer + (pSecBufMsg->cbBuffer - (MAC_BLOCK_SIZE + 1));

#if DBG
            // Now convert the Hash to Hex  - for TESTING ONLY
        ZeroMemory(szTemp, TEMPSIZE);
        if ((MAC_BLOCK_SIZE + 1) < iTempLen)
        {
            iTempLen = (MAC_BLOCK_SIZE + 1);
        }
        BinToHex(pMsgPadding, iTempLen, szTemp);
    
        if (szTemp)
        {
            DebugLog((DEB_TRACE, "DecryptData: HMAC & padding byte Data bytes (%dof%d bytes) %s\n",
                      iTempLen, (MAC_BLOCK_SIZE + 1), szTemp));
        }
        DebugLog((DEB_TRACE, "DecryptData:  MAC block size %d bytes\n", MAC_BLOCK_SIZE));
#endif

        cbPrefixPadding = *pMsgPadding;
        if (cbPrefixPadding > MAX_PADDING)
        {
            Status = STATUS_INTERNAL_ERROR;
            DebugLog((DEB_ERROR, "DigestUserUnsealHelper: Padding selected (%d) too large   status 0x%x\n",
                      cbPrefixPadding, Status));
            goto CleanUp;
        }

        if (pSecBufMsg->cbBuffer  < (MAC_BLOCK_SIZE + cbPrefixPadding))
        {
            Status = STATUS_INTERNAL_ERROR;
            DebugLog((DEB_ERROR, "DigestUserUnsealHelper: Message incorrect length   status 0x%x\n", Status));
            goto CleanUp;
        }
        cbMsg = pSecBufMsg->cbBuffer - (MAC_BLOCK_SIZE + cbPrefixPadding);

        DebugLog((DEB_TRACE, "DigestUserUnsealHelper: Padding found to be %d bytes\n", cbPrefixPadding));
    }
    else
    {
        Status = DecryptData(pContext->hUnsealCryptKey, cbSecBufMsgPrivacy,
                             (PUCHAR)pSecBufMsg->pvBuffer);

        if (!NT_SUCCESS (Status))
        {
            DebugLog((DEB_ERROR, "DigestUserUnsealHelper: Error in EncryptData   status 0x%x\n", Status));
            goto CleanUp;
        }

        // There is no padding on stream ciphers, so just remove the SASL HMAC block
        cbMsg = pSecBufMsg->cbBuffer - MAC_BLOCK_SIZE;
        DebugLog((DEB_TRACE, "DigestUserUnsealHelper: Stream Cipher - No padding\n"));
    }

    // Locate the beginning of the message
    pMsgHMAC =  (PBYTE)pSecBufMsg->pvBuffer + (pSecBufMsg->cbBuffer - MAC_BLOCK_SIZE);

    Status = CalculateSASLHMAC(pContext, FALSE, &strcSignKeyConst, *pdwSeqNum,
                               (PBYTE)pSecBufMsg->pvBuffer, cbMsg, &MacBlock);
    if (!NT_SUCCESS (Status))
    {
        DebugLog((DEB_ERROR, "DigestUserUnsealHelper: Error in CalculateSASLHMAC   status 0x%x\n", Status));
        goto CleanUp;
    }


    DebugLog((DEB_TRACE, "DigestUserUnsealHelper: Ready to compare MacBlocks\n"));

    // Check validity of MAC block ONLY do not write it out
    memcpy(&TokenMacBlock, pMsgHMAC, MAC_BLOCK_SIZE); 
    if (MacBlock.dwSeqNumber != TokenMacBlock.dwSeqNumber)
    {
        Status = SEC_E_OUT_OF_SEQUENCE;
        DebugLog((DEB_ERROR, "DigestUserUnsealHelper: SASL MAC blocks out of sequence. Failed verify.  Status 0x%x\n", Status));
#if DBG
    ZeroMemory(szTemp, TEMPSIZE);
    BinToHex((PUCHAR)&TokenMacBlock, MAC_BLOCK_SIZE, szTemp);
    DebugLog((DEB_TRACE, "CalculateSASLHMAC: Token's HMAC-MD5 block %s\n", szTemp));
    ZeroMemory(szTemp, TEMPSIZE);
    BinToHex((PUCHAR)&MacBlock, MAC_BLOCK_SIZE, szTemp);
    DebugLog((DEB_TRACE, "CalculateSASLHMAC: TComputed HMAC-MD5 block %s\n", szTemp));
#endif
        goto CleanUp;
    }
    if (memcmp(&MacBlock, &TokenMacBlock, MAC_BLOCK_SIZE))
    {
        Status = SEC_E_MESSAGE_ALTERED;
        DebugLog((DEB_ERROR, "DigestUserUnsealHelper: SASL MAC blocks do not match. Failed verify.  Status 0x%x\n", Status));
#if DBG
    ZeroMemory(szTemp, TEMPSIZE);
    BinToHex((PUCHAR)&TokenMacBlock, MAC_BLOCK_SIZE, szTemp);
    DebugLog((DEB_TRACE, "CalculateSASLHMAC: Token's HMAC-MD5 block %s\n", szTemp));
    ZeroMemory(szTemp, TEMPSIZE);
    BinToHex((PUCHAR)&MacBlock, MAC_BLOCK_SIZE, szTemp);
    DebugLog((DEB_TRACE, "CalculateSASLHMAC: TComputed HMAC-MD5 block %s\n", szTemp));
#endif
        goto CleanUp;
    }
    else
    {
        DebugLog((DEB_TRACE, "DigestUserUnsealHelper: SASL MAC blocks match!\n"));
    }

    // Write out to SECBUFFERDATA the length and location of message
    if (pSecBufData)
    {
        pSecBufData->cbBuffer = cbMsg;
        pSecBufData->pvBuffer = pSecBufMsg->pvBuffer;
    }

    // completed all tasks down to here.  Need to update the sequence number
    (*pdwSeqNum)++;
    DebugLog((DEB_TRACE, "DigestUserUnsealHelper: Updated SeqNum to %d\n", *pdwSeqNum));


CleanUp:

    DebugLog((DEB_TRACE_FUNC, "DigestUserUnsealHelper: Leaving    Status 0x%x\n", Status));

    return(Status);
}


// Process the Digest information with the context info and generate any output token info
NTSTATUS NTAPI
DigestUserProcessParameters(
                           IN PDIGEST_USERCONTEXT pContext,
                           IN PDIGEST_PARAMETER pDigest,
                           OUT PSecBuffer pFirstOutputToken)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG ulNonceCount = 0;

    DebugLog((DEB_TRACE_FUNC, "DigestUserProcessParameters: Entering\n"));


    // Some common input verification tests

    // We must have a noncecount specified since we specified a qop in the Challenge
    // If we decide to support no noncecount modes then we need to make sure that qop is not specified
    if (pDigest->refstrParam[MD5_AUTH_NC].Length)
    {
        Status = RtlCharToInteger(pDigest->refstrParam[MD5_AUTH_NC].Buffer, HEXBASE, &ulNonceCount);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INVALID_PARAMETER;
            DebugLog((DEB_ERROR, "DigestUserProcessParameters: Nonce Count badly formatted\n"));
            goto CleanUp;
        }
    }

    // Check nonceCount is incremented to preclude replay
    if (!(ulNonceCount > pContext->ulNC))
    {
        // We failed to verify next noncecount
        Status = SEC_E_OUT_OF_SEQUENCE;
        DebugLog((DEB_ERROR, "DigestUserProcessParameters: NonceCount failed to increment!\n"));
        goto CleanUp;
    }

    // Copy the SessionKey from the Context into the Digest Structure to verify against
    // This will have Digest Auth routines use the SessionKey rather than recompute H(A1)
    StringFree(&(pDigest->strSessionKey));
    Status = StringDuplicate(&(pDigest->strSessionKey), &(pContext->strSessionKey));
    if (!NT_SUCCESS(Status))
    {
        DebugLog((DEB_ERROR, "DigestUserProcessParameters: Failed to copy over SessionKey\n"));
        goto CleanUp;
    }

    // Set the type of Digest Parameters we are to process
    pDigest->typeDigest = pContext->typeDigest;
    pDigest->typeQOP = pContext->typeQOP;
    pDigest->typeAlgorithm = pContext->typeAlgorithm;
    pDigest->typeCharset = pContext->typeCharset;

    if (pContext->ulFlags & FLAG_CONTEXT_QUOTE_QOP)
    {
        pDigest->usFlags |= FLAG_QUOTE_QOP;
    }

    DigestPrint(pDigest);

    // No check locally that Digest is authentic
    Status = DigestCalculation(pDigest, NULL);
    if (!NT_SUCCESS(Status))
    {
        DebugLog((DEB_ERROR, "DigestUserProcessParameters: Oh no we FAILED Authentication!!!!\n"));
        goto CleanUp;
    }

       // Send to output buffer only if there is an output buffer
       // This allows this routine to be used in UserMode
    if (pFirstOutputToken)
    {
        Status = DigestCreateChalResp(pDigest, NULL, pFirstOutputToken);
        if (!NT_SUCCESS(Status))
        {
            DebugLog((DEB_ERROR, "DigestUserProcessParameters: Failed to create Output String\n"));
            goto CleanUp;
        }
    }

CleanUp:
    
    DebugLog((DEB_TRACE_FUNC, "DigestUserProcessParameters: Leaving   Status 0x%x\n", Status));
    return(Status);
}



//+-------------------------------------------------------------------------
//
//  Function:   DigestUnpackContext
//
//  Synopsis:   Unpack the context from LSA mode into the User mode Context
//
//  Effects:    no global effect.
//
//  Arguments:
//
//  IN PDIGEST_PACKED_USERCONTEXT pPackedUserContext    -- packed Context data
//  OUT PDIGEST_USERCONTEXT pContext    -- pointer to the UserContext to unpack data into
// 
//  Requires:   no global requirements
//
//  Returns:    STATUS_SUCCESS, or resource error
//
//  Notes:  This routine is called by the LSA from ISC() and ASC() based on setting MappedContext
//     on the completion of these routines.
//
//
//
//--------------------------------------------------------------------------
NTSTATUS
DigestUnpackContext(
    IN PDIGEST_PACKED_USERCONTEXT pPackedUserContext,
    OUT PDIGEST_USERCONTEXT pContext)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PUCHAR  pucLoc = NULL;
    USHORT uNumWChars = 0;
    int iAuth = 0;

    ASSERT(pPackedUserContext);
    ASSERT(pContext);

    DebugLog((DEB_TRACE_FUNC, "DigestUnpackContext: Entering\n"));

    if (pPackedUserContext->ulFlags & FLAG_CONTEXT_PARTIAL)
    {
        // This partial context has no real data that we must process so just leave context alone for now
        // This is needed for proper ref counting
        DebugLog((DEB_TRACE, "DigestUnpackContext: partial context - no unpack needed  context 0x%x\n",
                  pContext->LsaContext ));
        pContext->ulFlags = pPackedUserContext->ulFlags;
        goto CleanUp;
    }

    if (pContext->ulFlags & FLAG_CONTEXT_PARTIAL)
    {
        DebugLog((DEB_TRACE, "DigestUnpackContext: Completing partial context to full context\n" ));
    }
    
    //
    // We now pass explicit flags to indicate ASC or ISC context
    // Right now FLAG_CONTEXT_PARTIAL will catch ASC calls without a ClientTokenHandle,
    // but this explicit check is better than checking on the TokenHandle should some ASC update calls not provide
    // a TokenHandle
    //
    if (pPackedUserContext->ulFlags & FLAG_CONTEXT_SERVER)
    {
        DebugLog((DEB_TRACE, "DigestUnpackContext: Called from ASC\n" ));
        if (pPackedUserContext->ClientTokenHandle != NULL)
        {
            ASSERT(pContext->ClientTokenHandle == NULL);
            pContext->ClientTokenHandle = (HANDLE) ((ULONG_PTR)pPackedUserContext->ClientTokenHandle);
            if (FAILED(SspCreateTokenDacl(pContext->ClientTokenHandle)))
            {
                Status = STATUS_INVALID_HANDLE;
                DebugLog((DEB_ERROR, "DigestUnpackContext: SspCreateTokenDacl failed\n" ));
                goto CleanUp;
            }
            DebugLog((DEB_TRACE, "DigestUnpackContext: SspCreateTokenDacl has created the DACL\n" ));
        }
    }
    else
    {
        DebugLog((DEB_TRACE, "DigestUnpackContext: Called from ISC\n" ));
    }

    //
    // Copy over all of the other fields - some data might be binary so
    // use RtlCopyMemory(Dest, Src, len)
    //
    pContext->ExpirationTime = pPackedUserContext->ExpirationTime;
    pContext->typeAlgorithm = (ALGORITHM_TYPE)pPackedUserContext->typeAlgorithm;
    pContext->typeCharset = (CHARSET_TYPE)pPackedUserContext->typeCharset;
    pContext->typeCipher = (CIPHER_TYPE)pPackedUserContext->typeCipher;
    pContext->typeDigest = (DIGEST_TYPE)pPackedUserContext->typeDigest;
    pContext->typeQOP = (QOP_TYPE)pPackedUserContext->typeQOP;
    pContext->ulSendMaxBuf = pPackedUserContext->ulSendMaxBuf;
    pContext->ulRecvMaxBuf = pPackedUserContext->ulRecvMaxBuf;
    pContext->ContextReq = pPackedUserContext->ContextReq;
    pContext->CredentialUseFlags = pPackedUserContext->CredentialUseFlags;
    pContext->ulFlags = pPackedUserContext->ulFlags;

    // Now check on the strings attached
    pucLoc = &(pPackedUserContext->ucData);
    for (iAuth = 0; iAuth < MD5_AUTH_LAST; iAuth++)
    {
        if (pPackedUserContext->uDigestLen[iAuth])
        {
            Status = StringAllocate(&(pContext->strParam[iAuth]), (USHORT)pPackedUserContext->uDigestLen[iAuth]);
            if (!NT_SUCCESS(Status))
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                DebugLog((DEB_ERROR, "DigestUnpackContext: DigestAllocateMemory for Params returns NULL\n" ));
                goto CleanUp;
            }
            memcpy(pContext->strParam[iAuth].Buffer, pucLoc, (USHORT)pPackedUserContext->uDigestLen[iAuth]);
            pContext->strParam[iAuth].Length = (USHORT)pPackedUserContext->uDigestLen[iAuth];
            pucLoc +=  (USHORT)pPackedUserContext->uDigestLen[iAuth];
            // DebugLog((DEB_TRACE, "DigestUnpackContext: Param[%d] is length %d - %.50s\n",
            //           iAuth, pPackedUserContext->uDigestLen[iAuth], pContext->strParam[iAuth].Buffer ));
        }
    }
        // Now do the SessionKey
    if (pPackedUserContext->uSessionKeyLen)
    {
        ASSERT(pPackedUserContext->uSessionKeyLen == MD5_HASH_HEX_SIZE);
        if (pPackedUserContext->uSessionKeyLen != MD5_HASH_HEX_SIZE)
        {
            Status = STATUS_NO_USER_SESSION_KEY;
            DebugLog((DEB_ERROR, "DigestUnpackContext: Session key length incorrect\n" ));
            goto CleanUp;
        }

        Status = StringAllocate(&(pContext->strSessionKey), (USHORT)pPackedUserContext->uSessionKeyLen);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            DebugLog((DEB_ERROR, "DigestUnpackContext: DigestAllocateMemory for SessionKey returns NULL\n" ));
            goto CleanUp;
        }
        memcpy(pContext->strSessionKey.Buffer, pucLoc, pPackedUserContext->uSessionKeyLen);
        pContext->strSessionKey.Length = (USHORT)pPackedUserContext->uSessionKeyLen;
        pucLoc +=  (USHORT)pPackedUserContext->uSessionKeyLen;

        // Now determine the binary version of the SessionKey from HEX() version
        HexToBin(pContext->strSessionKey.Buffer, MD5_HASH_HEX_SIZE, pContext->bSessionKey);
    }
    
        // Now do the AccountName
    if (pPackedUserContext->uAccountNameLen)
    {
        uNumWChars = (USHORT)pPackedUserContext->uAccountNameLen / sizeof(WCHAR);
        Status = UnicodeStringAllocate(&(pContext->ustrAccountName), uNumWChars);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            DebugLog((DEB_ERROR, "DigestUnpackContext: DigestAllocateMemory for AccountName returns NULL\n" ));
            goto CleanUp;
        }
        memcpy(pContext->ustrAccountName.Buffer, pucLoc, pPackedUserContext->uAccountNameLen);
        pContext->ustrAccountName.Length = (USHORT)pPackedUserContext->uAccountNameLen;
        pucLoc +=  (USHORT)pPackedUserContext->uAccountNameLen;
    }


#if DBG2
    {
        char szTemp[TEMPSIZE];
        ZeroMemory(szTemp, TEMPSIZE);

        BinToHex(pContext->bSessionKey, MD5_HASH_BYTESIZE, szTemp);
        DebugLog((DEB_TRACE, "DigestUnpackContext: verify SessionKey %Z is binary %s\n",
                  &(pContext->strSessionKey), szTemp));
    }
#endif

CleanUp:

    DebugLog((DEB_TRACE_FUNC, "DigestUnpackContext: Leaving       Status 0x%x\n", Status));
    return(Status);
}


// Printout the fields present in usercontext pContext
NTSTATUS
UserContextPrint(PDIGEST_USERCONTEXT pContext)
{
    NTSTATUS Status = STATUS_SUCCESS;
    int i = 0;

    if (!pContext)
    {
        return (STATUS_INVALID_PARAMETER); 
    }


    DebugLog((DEB_TRACE_FUNC, "UserContext:      Entering for Context Handle at 0x%x\n", pContext));

    DebugLog((DEB_TRACE, "UserContext:      NC %ld\n", pContext->ulNC));

    DebugLog((DEB_TRACE, "UserContext:      LSA Context 0x%x\n", pContext->LsaContext));


    if (pContext->typeDigest == DIGEST_CLIENT)
    {
        DebugLog((DEB_TRACE, "UserContext:       DIGEST_CLIENT\n"));
    }
    if (pContext->typeDigest == DIGEST_SERVER)
    {
        DebugLog((DEB_TRACE, "UserContext:       DIGEST_SERVER\n"));
    }
    if (pContext->typeDigest == SASL_SERVER)
    {
        DebugLog((DEB_TRACE, "UserContext:       SASL_SERVER\n"));
    }
    if (pContext->typeDigest == SASL_CLIENT)
    {
        DebugLog((DEB_TRACE, "UserContext:       SASL_CLIENT\n"));
    }

    if (pContext->typeQOP == AUTH)
    {
        DebugLog((DEB_TRACE, "UserContext:       QOP: AUTH\n"));
    }
    if (pContext->typeQOP == AUTH_INT)
    {
        DebugLog((DEB_TRACE, "UserContext:       QOP: AUTH_INT\n"));
    }
    if (pContext->typeQOP == AUTH_CONF)
    {
        DebugLog((DEB_TRACE, "UserContext:       QOP: AUTH_CONF\n"));
    }
    if (pContext->typeAlgorithm == MD5)
    {
        DebugLog((DEB_TRACE, "UserContext:       Algorithm: MD5\n"));
    }
    if (pContext->typeAlgorithm == MD5_SESS)
    {
        DebugLog((DEB_TRACE, "UserContext:       Algorithm: MD5_SESS\n"));
    }


    if (pContext->typeCharset == ISO_8859_1)
    {
        DebugLog((DEB_TRACE, "UserContext:       Charset: ISO 8859-1\n"));
    }
    if (pContext->typeCharset == UTF_8)
    {
        DebugLog((DEB_TRACE, "UserContext:       Charset: UTF-8\n"));
    }

    if (pContext->typeCipher == CIPHER_RC4)
    {
        DebugLog((DEB_TRACE, "UserContext:       Cipher: CIPHER_RC4\n"));
    }
    else if (pContext->typeCipher == CIPHER_RC4_40)
    {
        DebugLog((DEB_TRACE, "UserContext:       Cipher: CIPHER_RC4_40\n"));
    }
    else if (pContext->typeCipher == CIPHER_RC4_56)
    {
        DebugLog((DEB_TRACE, "UserContext:       Cipher: CIPHER_RC4_56\n"));
    }
    else if (pContext->typeCipher == CIPHER_DES)
    {
        DebugLog((DEB_TRACE, "UserContext:       Cipher: CIPHER_DES\n"));
    }
    else if (pContext->typeCipher == CIPHER_3DES)
    {
        DebugLog((DEB_TRACE, "UserContext:       Cipher: CIPHER_3DES\n"));
    }

    DebugLog((DEB_TRACE, "UserContext:       ContextReq 0x%lx     CredentialUseFlags 0x%x\n",
              pContext->ContextReq,
              pContext->CredentialUseFlags));

    for (i=0; i < MD5_AUTH_LAST;i++)
    {
        if (pContext->strParam[i].Buffer &&
            pContext->strParam[i].Length)
        {
            DebugLog((DEB_TRACE, "UserContext:       Digest[%d] = \"%Z\"\n", i,  &pContext->strParam[i]));
        }
    }

    if (pContext->strSessionKey.Length)
    {
        DebugLog((DEB_TRACE, "UserContext:      SessionKey %.10Z*********\n", &pContext->strSessionKey));
    }

    if (pContext->ustrAccountName.Length)
    {
        DebugLog((DEB_TRACE, "UserContext:      AccountName %wZ\n", &pContext->ustrAccountName));
    }

    DebugLog((DEB_TRACE_FUNC, "UserContext:      Leaving\n"));

    return(Status);
}

// CryptoAPI function support

NTSTATUS
SEC_ENTRY
CreateSymmetricKey(
    IN ALG_ID     Algid,
    IN DWORD       cbKey,
    IN UCHAR      *pbKey,
    IN UCHAR      *pbIV,
    OUT HCRYPTKEY *phKey
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    PLAINTEXTBLOB PlainBlob = {0};

    DebugLog((DEB_TRACE_FUNC, "CreateSymmetricKey: Entering\n"));

    ASSERT(*phKey == NULL);

    if (cbKey > MD5_HASH_BYTESIZE)
    {
        DebugLog((DEB_ERROR, "CreateSymmetricKey: Shared key too long\n"));
        Status = STATUS_INTERNAL_ERROR;
        goto CleanUp;
    }

#if DBG
        char szTemp[TEMPSIZE];
            // Now convert the Hash to Hex  - for TESTING ONLY
        ZeroMemory(szTemp, TEMPSIZE);
        BinToHex(pbKey, cbKey, szTemp);
    
        if (szTemp)
        {
            DebugLog((DEB_TRACE, "CreateSymmetricKey: Creating symmetric for  %s\n", szTemp));
        }
#endif

    PlainBlob.Blob.bType = PLAINTEXTKEYBLOB;
    PlainBlob.Blob.bVersion = CUR_BLOB_VERSION;
    PlainBlob.Blob.reserved = 0;
    PlainBlob.Blob.aiKeyAlg = Algid;
    memcpy(PlainBlob.bKey, pbKey, cbKey);
    PlainBlob.dwKeyLen = cbKey;


    // import thw simpleblob to get a handle to the symmetric key
    if (!CryptImportKey(g_hCryptProv,
                        (BYTE *)&PlainBlob,
                        sizeof(PlainBlob),
                        0,
                        0,
                        phKey))
    {
        DebugLog((DEB_ERROR, "CreateSymmetricKey: CryptImportKey failed     error 0x%x\n", GetLastError()));
        Status = STATUS_INTERNAL_ERROR;
    }


    if ((Algid == CALG_DES) || (Algid == CALG_3DES_112))
    {

       if (!pbIV)
       {
           DebugLog((DEB_WARN, "CreateSymmetricKey: No IV selected for DES\n"));
       }
       else
       {
#if DBG
                // Now convert the Hash to Hex  - for TESTING ONLY
           ZeroMemory(szTemp, TEMPSIZE);
           BinToHex(pbIV, 8, szTemp);
           if (szTemp)
           {
               DebugLog((DEB_TRACE, "CreateSymmetricKey: IV bytes set to  %s\n", szTemp));
           }
#endif
           if (!CryptSetKeyParam(*phKey, KP_IV, pbIV, 0))
           {
               DebugLog((DEB_ERROR, "CreateSymmetricKey:CryptSetKeyParam() failed : 0x%x\n", GetLastError()));
               Status = STATUS_INTERNAL_ERROR;
               goto CleanUp;
           }
       }

    }

CleanUp:

    DebugLog((DEB_TRACE_FUNC, "CreateSymmetricKey: Leaving     status 0x%x\n", Status));
    return(Status);
}



//+-------------------------------------------------------------------------
//
//  Function:   EncryptData2Multi
//
//  Synopsis:   Encrypt a data buffers
//
//  Effects:    no global effect.
//
//  Arguments:
//
//  IN   hKey               -- symmetric key to utilize
//  IN   cbBlocklength      -- natural block length for encoding (RC will be 1 and DES will be 8)
//  IN   pSecBuff           -- SecBuffer list containing data buffers to encrypt
//  IN   cbSignature        -- number of signature bytes to encrypt after Data is encrypted
//  IN   pbSignature        -- number of bytes in signature to encrypt
//
//  Requires:   no global requirements
//
//  Returns:    STATUS_SUCCESS, or resource error
//
//  Notes: 
//
//
//--------------------------------------------------------------------------
NTSTATUS
SEC_ENTRY
EncryptData2Multi(
    IN HCRYPTKEY  hKey,
    IN ULONG      cbBlocklength,
    IN PSecBufferDesc pSecBuff,    // List of databuffers to Encrypt. May be 1 or more SecBuffers
    IN ULONG      cbSignature,
    IN OUT UCHAR  *pbSignature
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG    cbBlocks = 0;
    ULONG    cbDataExtra = 0;
    ULONG    cbDataBytesUsed = 0;        // Number of bytes already processed from current buffer (data or padding SecBuffer)
    PBYTE    pbData = NULL;              // location for start of extra memory bytes
    ULONG    ulIndex = 0;
    BOOL     fDataBuffers = TRUE;
    DWORD    dwBytesEncrypt = 0;

    DebugLog((DEB_TRACE_FUNC, "EncryptData2Multi: Entering\n"));

    ASSERT(pSecBuff);
    ASSERT(pbSignature);

    // Scan through the SecBuffer list for data buffers to encrypt
    // Currently look at SecBUffer[Index] and increment Index over all SecBuffers
    cbDataBytesUsed = 0;              // Number of databytes already processed in current buffer
    while (fDataBuffers == TRUE)
    {
        if (ulIndex < pSecBuff->cBuffers)
        {
            // Locate a SecBuffer Data that has data to encrypt
            if ((pSecBuff->pBuffers[ulIndex].BufferType != SECBUFFER_DATA) ||
                (!pSecBuff->pBuffers[ulIndex].cbBuffer))
            {
                ulIndex++;    // Inspect the next SecBuffer
                continue;   // restart while loop
            }
        }
        else
        {
            // No more SecBuffers Data to process
            fDataBuffers = FALSE;
            continue;
        }

        // If SecBuffer Data size greater than zero, encrypt maximum multiple of blockcount
        DebugLog((DEB_TRACE, "EncryptData2Multi: located buffer %d   %ld bytes to encrypt\n",
                  ulIndex, (pSecBuff->pBuffers[ulIndex].cbBuffer - cbDataBytesUsed)));
        ASSERT(cbDataBytesUsed < cbBlocklength);                   // should always have used less than blocksize bytes 
        // Identify if there are extra bytes beyond blocksize for cipher
        cbBlocks = (pSecBuff->pBuffers[ulIndex].cbBuffer - cbDataBytesUsed) / cbBlocklength;    // integer division
        cbDataExtra =  (pSecBuff->pBuffers[ulIndex].cbBuffer - cbDataBytesUsed) - (cbBlocklength * cbBlocks);

        DebugLog((DEB_TRACE, "EncryptData2Multi: in buffer %lu  blocks %lu   extrbytes %lu\n",
                  ulIndex, cbBlocks, cbDataExtra));

        // If there are blocks to encrypt - do that
        if (cbBlocks)
        {
            dwBytesEncrypt = cbBlocklength * cbBlocks;
            DebugLog((DEB_TRACE, "EncryptData2Multi:    buffer %lu  start at %lu   length %lu\n", 
                      ulIndex, cbDataBytesUsed, dwBytesEncrypt));
            pbData = (PBYTE)pSecBuff->pBuffers[ulIndex].pvBuffer + cbDataBytesUsed;
            if (!CryptEncrypt(hKey, 0, FALSE, 0, pbData, &dwBytesEncrypt, dwBytesEncrypt))
            {
                DebugLog((DEB_ERROR, "EncryptData2Multi:CryptEncrypt first buffer (blocklength) failed : 0x%x\n", GetLastError()));
                Status = STATUS_INTERNAL_ERROR;
                goto CleanUp;
            }
            cbDataBytesUsed = cbDataBytesUsed + dwBytesEncrypt;
        }
        
        // Handle the extra bytes not block encrypted
        if (cbDataExtra)
        {
            // This will have to link potentially multiple SecBuffers or the Padding to fill in a blocklength
            // of data to encrypt.  The value of ulIndex and cbDataBytesUsed can change.  If ulIndex points outside
            // range of SecBuffers, then cbDataBytesUsed value refers to the padding SecBuffer state.
            DebugLog((DEB_TRACE, "EncryptData2Multi:    buffer %lu has %lu bytes to process. Use link.\n", 
                      ulIndex, cbDataExtra));
            Status = LinkBuffersToEncrypt(hKey, cbBlocklength, pSecBuff, &ulIndex, &cbDataBytesUsed, pbSignature, cbSignature); 
            if (!NT_SUCCESS(Status))
            {
                DebugLog((DEB_ERROR, "EncryptData2Multi: LinkBuffer failed   0x%x\n", Status ));
                goto CleanUp;
            }

        }
        else
        {
            // No extra bytes in this SecBuffer to process, go to next Buffer
            ulIndex++;
            cbDataBytesUsed = 0;
        }

    }

    // Encrypt remaining bytes in signature - assert that it must be a multiple of blocksize
    DebugLog((DEB_TRACE, "EncryptData2Multi:  Now process Signature buffer. link processed %lu of %lu bytes already\n", 
              cbDataBytesUsed, cbSignature));
    ASSERT(cbDataBytesUsed < cbSignature);
    dwBytesEncrypt = cbSignature - cbDataBytesUsed;

    DebugLog((DEB_TRACE, "EncryptData2Multi:  Encrypt %lu signature bytes\n", dwBytesEncrypt));
    if (!CryptEncrypt(hKey, 0, FALSE, 0, (pbSignature + cbDataBytesUsed), &dwBytesEncrypt, dwBytesEncrypt))
    {
        DebugLog((DEB_ERROR, "EncryptData2Multi:CryptEncrypt one buffer failed : 0x%x\n", GetLastError()));
        Status = STATUS_INTERNAL_ERROR;
        goto CleanUp;
    }


CleanUp:

    DebugLog((DEB_TRACE_FUNC, "EncryptData2Multi: Leaving     status 0x%x\n", Status));

    return(Status);
}



//+-------------------------------------------------------------------------
//
//  Function:   LinkBuffersToEncrypt
//
//  Synopsis:   Encrypt the boundary bytes that span over multiple data SecBuffers and Padding
//
//  Effects:    no global effect.
//
//  Arguments:
//
//  IN   hKey               -- symmetric key to utilize
//  IN   cbBlocklength      -- BlockLength for symmetric cipher (should be greater than 1)
//  IN   pSecBuff           -- pointer to SecBuffers containing data to encrypt
//  IN OUT pSecBuff         -- pointer to current SecBuffer to process (must be a Data buffer)
//  IN OUT pulIndex         -- pointer to current SecBuffer to process (must be a Data buffer)
//  IN OUT pcbDataBytesUsed -- pointer to number of bytes processed in current SecBuffer
//  IN   pbSignature        -- pointer to buffer for signature block (contains padding plus HMAC)
//  IN   cbSignature        -- number of bytes in signature block
//
//  Requires:   no global requirements
//
//  Returns:    STATUS_SUCCESS, or resource error
//
//  Notes:  This function will handle the blocklength encryption boundaries between data SecBuffers and
//    the padding bytes.  The value
//
//
//
//--------------------------------------------------------------------------
NTSTATUS
SEC_ENTRY
LinkBuffersToEncrypt(
    IN HCRYPTKEY  hKey,
    IN ULONG cbBlocklength,
    IN PSecBufferDesc pSecBuff,
    IN OUT PULONG pulIndex,
    IN OUT PULONG pcbDataBytesUsed,
    IN PUCHAR pbSignature,
    IN ULONG  cbSignature)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PBYTE    pbTempBuff = NULL;             // temp alloc for merge of cross buffer bytes (sized to BlockLength)
    ULONG    cbTempBuff = 0;
    ULONG    ulScanIndex = 0;               // scan along the SecBuffers
    ULONG    cbScanDataBytesUsed = 0;       // number of bytes used in ulScanIndex's SecBuffer
    ULONG    cbScanDataBytesLeft = 0;       // number of bytes remaining in ulScanIndex's SecBuffer
    DWORD    dwBytesEncrypt = 0;
    ULONG    cbBytesNeeded = 0;
    PBYTE    pbSecBuff = NULL;

    DebugLog((DEB_TRACE_FUNC, "LinkBuffersToEncrypt: Entering\n"));

    ASSERT(cbBlocklength > 1);
    ASSERT(cbSignature > SASL_MAC_HMAC_SIZE);    // check to make sure that Signature inlcudes padding plus HMAC

    UNREFERENCED_PARAMETER(cbSignature);

    // Allocate memory to store cross data buffer bytes
    pbTempBuff = (PBYTE)DigestAllocateMemory(cbBlocklength);
    if (!pbTempBuff)
    {
            DebugLog((DEB_ERROR, "LinkBuffersToEncrypt:out of memory\n"));
            Status = SEC_E_INSUFFICIENT_MEMORY;
            goto CleanUp;
    }

    // pulIndex points to the current data buffer with pcbDataBytesUsed already processed
    // Start reading in bytes to fill TempBuff up to BlockLength. Normally the next buffer in sequence (data or padding)
    // will have enough bytes to cover filling up the TempBuffer, but someone could send in a tiny buffer and cause
    // 3 or more buffers to have to be utilized

    ulScanIndex = *pulIndex;                          // set a marker to where to start processing
    cbScanDataBytesUsed = *pcbDataBytesUsed;

    // Since we were called, there must be left over bytes in SecBuffer to process
    ASSERT(cbScanDataBytesUsed < pSecBuff->pBuffers[ulScanIndex].cbBuffer);
    
    while ((cbTempBuff != cbBlocklength) && (ulScanIndex < pSecBuff->cBuffers))
    {
        DebugLog((DEB_TRACE, "LinkBuffersToEncrypt: Link buffer has %lu bytes - needs %lu. Scan Buffers\n",
                  cbTempBuff, cbBlocklength));
        if ((pSecBuff->pBuffers[ulScanIndex].BufferType != SECBUFFER_DATA) ||
            (!pSecBuff->pBuffers[ulScanIndex].cbBuffer))
        {
            DebugLog((DEB_TRACE, "LinkBuffersToEncrypt: Scan skip SecBuffer Index %lu\n", ulScanIndex));
            ulScanIndex++;                // Inspect the next SecBuffer
            cbScanDataBytesUsed = 0;
            continue;                     // restart while loop
        }

        // How many bytes can we process in this buffer
        cbScanDataBytesLeft = pSecBuff->pBuffers[ulScanIndex].cbBuffer - cbScanDataBytesUsed;

        // copy over the bytes into the temp location
        cbBytesNeeded = cbBlocklength - cbTempBuff;

        DebugLog((DEB_TRACE, "LinkBuffersToEncrypt: Temp buffer needs %lu bytes, SecBuffer has %lu bytes\n",
                  cbBytesNeeded, cbScanDataBytesLeft));
        pbSecBuff = (PBYTE)pSecBuff->pBuffers[ulScanIndex].pvBuffer;

        if (cbScanDataBytesLeft < cbBytesNeeded)
        {
            // Can not fill up with only this buffer - need more data buffers
            DebugLog((DEB_TRACE, "LinkBuffersToEncrypt: Can not fill Link with Index %lu. Need more data\n", ulScanIndex));
            memcpy((pbTempBuff + cbTempBuff), 
                   (pbSecBuff + cbScanDataBytesUsed),
                    cbScanDataBytesLeft);
            cbTempBuff = cbTempBuff + cbScanDataBytesLeft;
            ulScanIndex++;                  // move to next SecBuffer and for possible data
            cbScanDataBytesUsed = 0;
        }
        else
        {
            // We can fill up the reset of the Temp link Buffer with Scan SecBuffer
            DebugLog((DEB_TRACE, "LinkBuffersToEncrypt: Can fill Link with Index %lu\n", ulScanIndex));
            ASSERT(cbScanDataBytesUsed == 0);                  // this should be a new buffer to be processed
            memcpy((pbTempBuff + cbTempBuff), 
                    pbSecBuff,
                    cbBytesNeeded);
            cbTempBuff = cbTempBuff + cbBytesNeeded;
            ASSERT(cbTempBuff == cbBlocklength);      // will exit while loop now that buffer is full
            cbScanDataBytesUsed = cbBytesNeeded;      // show number of bytes already processed in new SecBuffer
        }
    }

    if (cbTempBuff < cbBlocklength)
    {
        // processesed all of the data SecBuffers - now use the padding in the Signature buffer
        DebugLog((DEB_TRACE, "LinkBuffersToEncrypt: Link buffer has %lu bytes - needs %lu. Use padding.\n",
                  cbTempBuff, cbBlocklength));
        ASSERT(ulScanIndex == pSecBuff->cBuffers);           // should have exhausted all of the SecBuffers

        cbBytesNeeded = cbBlocklength - cbTempBuff;

        DebugLog((DEB_TRACE, "LinkBuffersToEncrypt: Need %lu bytes from Signature buffer\n",
                  cbBytesNeeded));
        memcpy((pbTempBuff + cbTempBuff), 
                pbSignature,
                cbBytesNeeded);
        cbTempBuff = cbTempBuff + cbBytesNeeded;
        cbScanDataBytesUsed = cbBytesNeeded;
    }

    // We now should have a full link buffer to encrypt
    ASSERT(cbTempBuff == cbBlocklength);
    
    dwBytesEncrypt = cbBlocklength;
    if (!CryptEncrypt(hKey, 0, FALSE, 0, pbTempBuff, &dwBytesEncrypt, cbBlocklength))
    {
        DebugLog((DEB_ERROR, "LinkBuffersToEncrypt:CryptEncrypt link buffer failed : 0x%x\n", GetLastError()));
        Status = STATUS_INTERNAL_ERROR;
        goto CleanUp;
    }
    DebugLog((DEB_TRACE, "LinkBuffersToEncrypt: encrypted link buffer\n"));


    // Now place data back into proper locations - perform same operations as above but put data back
    ulScanIndex = *pulIndex;                          // set a marker to where to start processing
    cbScanDataBytesUsed = *pcbDataBytesUsed;
    cbTempBuff = 0;                                  // we have placed zero bytes back into SecBuffers

    // Since we were called, there must be left over bytes in SecBuffer to process
    ASSERT(cbScanDataBytesUsed < pSecBuff->pBuffers[ulScanIndex].cbBuffer);
    
    while ((cbTempBuff != cbBlocklength) && (ulScanIndex < pSecBuff->cBuffers))
    {
        DebugLog((DEB_TRACE, "LinkBuffersToEncrypt: (put)Link buffer has %lu bytes - needs %lu. Scan Buffers\n",
                  cbTempBuff, cbBlocklength));
        if ((pSecBuff->pBuffers[ulScanIndex].BufferType != SECBUFFER_DATA) ||
            (!pSecBuff->pBuffers[ulScanIndex].cbBuffer))
        {
            DebugLog((DEB_TRACE, "LinkBuffersToEncrypt: (put)Scan skip SecBuffer Index %lu\n", ulScanIndex));
            ulScanIndex++;                // Inspect the next SecBuffer
            cbScanDataBytesUsed = 0;
            continue;                     // restart while loop
        }

        // How many bytes can we process in this buffer
        cbScanDataBytesLeft = pSecBuff->pBuffers[ulScanIndex].cbBuffer - cbScanDataBytesUsed;

        // copy over the bytes into the temp location
        cbBytesNeeded = cbBlocklength - cbTempBuff;

        DebugLog((DEB_TRACE, "LinkBuffersToEncrypt: (put)Temp buffer needs %lu bytes, SecBuffer has %lu bytes\n",
                  cbBytesNeeded, cbScanDataBytesLeft));
        pbSecBuff = (PBYTE)pSecBuff->pBuffers[ulScanIndex].pvBuffer;

        if (cbScanDataBytesLeft < cbBytesNeeded)
        {
            // Can not fill up with only this buffer - need more data buffers
            DebugLog((DEB_TRACE, "LinkBuffersToEncrypt: Can not place Link into Index %lu. Need more data\n", ulScanIndex));
            memcpy((pbSecBuff + cbScanDataBytesUsed),
                   (pbTempBuff + cbTempBuff),                    
                    cbScanDataBytesLeft);
            cbTempBuff = cbTempBuff + cbScanDataBytesLeft;
            ulScanIndex++;                  // move to next SecBuffer and for possible data
            cbScanDataBytesUsed = 0;
        }
        else
        {
            // We can fill up the reset of the Temp link Buffer with Scan SecBuffer
            DebugLog((DEB_TRACE, "LinkBuffersToEncrypt: Can place Link into Index %lu\n", ulScanIndex));
            ASSERT(cbScanDataBytesUsed == 0);                  // this should be a new buffer to be processed
            memcpy(pbSecBuff,
                   (pbTempBuff + cbTempBuff),                   
                    cbBytesNeeded);
            cbTempBuff = cbTempBuff + cbBytesNeeded;
            ASSERT(cbTempBuff == cbBlocklength);      // will exit while loop now that buffer is full
            cbScanDataBytesUsed = cbBytesNeeded;      // show number of bytes already processed in new SecBuffer
        }
    }

    if (cbTempBuff < cbBlocklength)
    {
        // processesed all of the data SecBuffers - now use the padding in the Signature buffer
        DebugLog((DEB_TRACE, "LinkBuffersToEncrypt: Link buffer has %lu bytes - needs %lu. Place into padding.\n",
                  cbTempBuff, cbBlocklength));
        ASSERT(ulScanIndex == pSecBuff->cBuffers);           // should have exhausted all of the SecBuffers

        cbBytesNeeded = cbBlocklength - cbTempBuff;

        DebugLog((DEB_TRACE, "LinkBuffersToEncrypt: Put %lu bytes into Signature buffer (should be padding bytes)\n",
                  cbBytesNeeded));
        memcpy(pbSignature,
               (pbTempBuff + cbTempBuff),               
                cbBytesNeeded);
        cbTempBuff = cbTempBuff + cbBytesNeeded;
        cbScanDataBytesUsed = cbBytesNeeded;
    }

    // Update current Index and number of DataBytesUsed
    // If ulScanIndex is equal to the number of SecBuffers, then we used padding to complete link
    *pulIndex = ulScanIndex;                          // set a marker to where finished link processing
    *pcbDataBytesUsed = cbScanDataBytesUsed;

CleanUp:

    if (pbTempBuff)
    {
        DigestFreeMemory(pbTempBuff);
        pbTempBuff = NULL;
    }

    DebugLog((DEB_TRACE_FUNC, "LinkBuffersToEncrypt: Leaving     status 0x%x\n", Status));

    return(Status);
}



//+-------------------------------------------------------------------------
//
//  Function:   DecryptData
//
//  Synopsis:   Decrypt a data buffer
//
//  Effects:    no global effect.
//
//  Arguments:
//
//  IN   hKey               -- symmetric key to utilize
//  IN   cbData             -- number of data bytes to encrypt
//  IN   pbData             -- pointer to data bytes to encrypt
//
//  Requires:   no global requirements
//
//  Returns:    STATUS_SUCCESS, or resource error
//
//  Notes: 
//
//
//--------------------------------------------------------------------------
NTSTATUS
SEC_ENTRY
DecryptData(
    IN HCRYPTKEY  hKey,
    IN ULONG      cbData,
    IN OUT UCHAR  *pbData
    )
{
    ULONG    cb = cbData;
    NTSTATUS Status = STATUS_SUCCESS;

#if DBG
    char szTemp[TEMPSIZE];
    ULONG  iTempLen = 20;
#endif

    DebugLog((DEB_TRACE_FUNC, "DecryptData: Entering   %lu bytes at 0x%x\n", cbData, pbData));

#if DBG
            // Now convert the Hash to Hex  - for TESTING ONLY
    iTempLen = 20;
    ZeroMemory(szTemp, TEMPSIZE);
    if (cbData < iTempLen)
    {
        iTempLen = cbData;
    }
    BinToHex(pbData, iTempLen, szTemp);

    if (szTemp)
    {
        DebugLog((DEB_TRACE, "DecryptData: Encrypted Data bytes (%dof%d bytes) %s\n",
                  iTempLen, cbData, szTemp));
    }

    iTempLen = 20;
    ZeroMemory(szTemp, TEMPSIZE);
    if (cbData < iTempLen)
    {
        iTempLen = cbData;
    }
    BinToHex((pbData + cbData - iTempLen), iTempLen, szTemp);

    if (szTemp)
    {
        DebugLog((DEB_TRACE, "DecryptData: Encrypted end of buffer (%dof%d bytes) %s\n",
                  iTempLen, cbData, szTemp));
    }
#endif
    
    // import the simpleblob to get a handle to the symmetric key
    if (!CryptDecrypt(hKey, 0, FALSE, 0, pbData, &cb))
    {
        DebugLog((DEB_ERROR, "DecryptData:CryptCreateHash() failed : 0x%x\n", GetLastError()));
        Status = STATUS_INTERNAL_ERROR;
        goto CleanUp;
    }


#if DBG

    DebugLog((DEB_TRACE, "DecryptData:  Decrypted number of bytes %lu\n", cb));

            // Now convert the Hash to Hex  - for TESTING ONLY
    iTempLen = 20;
    ZeroMemory(szTemp, TEMPSIZE);
    if (cb < iTempLen)
    {
        iTempLen = cb;
    }
    BinToHex(pbData, iTempLen, szTemp);

    if (szTemp)
    {
        DebugLog((DEB_TRACE, "DecryptData: Decrypted Data bytes (%dof%d bytes) %s\n",
                  iTempLen, cbData, szTemp));
    }

    iTempLen = 20;
    ZeroMemory(szTemp, TEMPSIZE);
    if (cb < iTempLen)
    {
        iTempLen = cb;
    }
    BinToHex((pbData + cb - iTempLen), iTempLen, szTemp);

    if (szTemp)
    {
        DebugLog((DEB_TRACE, "DecryptData: Decrypted end of buffer (%dof%d bytes) %s\n",
                  iTempLen, cbData, szTemp));
    }
#endif

CleanUp:

    DebugLog((DEB_TRACE_FUNC, "DecryptData: Leaving     status 0x%x\n", Status));

    return(Status);
}


NTSTATUS
SEC_ENTRY
CalculateSASLHMAC(
    IN PDIGEST_USERCONTEXT pContext,
    IN BOOL  fSign,
    IN PSTRING pstrSignKeyConst,
    IN DWORD dwSeqNum,                     // Sequence number to process
    IN PBYTE pData,                        // location of data to HMAC
    IN ULONG cbData,                       // How many bytes of data to process
    OUT PSASL_MAC_BLOCK pMacBlock)
{
    NTSTATUS Status = STATUS_SUCCESS;

    HCRYPTHASH hHash = NULL;
    HCRYPTKEY hCryptKey = NULL;
    HMAC_INFO hmacinfo = {0};

    BYTE bKiHashData[MD5_HASH_BYTESIZE];    // Message integrity keys RFC 2831 sec 2.3
    DWORD cbKiHashData = 0;                 // Size of Message integrity keys

    BYTE bHMACData[HMAC_MD5_HASH_BYTESIZE];
    DWORD cbHMACData = 0;

#if DBG
    char szTemp[TEMPSIZE];
    ULONG  iTempLen = 20;
    ZeroMemory(szTemp, TEMPSIZE);
#endif

    ASSERT(pMacBlock);


    DebugLog((DEB_TRACE_FUNC, "CalculateSASLHMAC: Entering\n"));
    DebugLog((DEB_TRACE, "CalculateSASLHMAC: Processing %d bytes in data block\n", cbData));

    // Clear the output
    ZeroMemory(pMacBlock, sizeof(SASL_MAC_BLOCK));

    // Initialize local variables
    ZeroMemory(bKiHashData, MD5_HASH_BYTESIZE);
    ZeroMemory(bHMACData, HMAC_MD5_HASH_BYTESIZE);


    // Always do an integrety calculation on the input data
    // We should have clear text data at this stage
    if (!dwSeqNum)
    {
        if ( !CryptCreateHash( g_hCryptProv,
                               CALG_MD5,
                               0,
                               0,
                               &hHash ) )
        {
            DebugLog((DEB_ERROR, "CalculateSASLHMAC: CryptCreateHash failed : 0x%lx\n", GetLastError()));
            Status = STATUS_ENCRYPTION_FAILED;
            goto CleanUp;
        }

        if ( !CryptHashData( hHash,
                             (const unsigned char *)pContext->bSessionKey,
                             MD5_HASH_BYTESIZE,
                             0 ) )
        {
            DebugLog((DEB_ERROR, "CalculateSASLHMAC: CryptHashData failed : 0x%lx\n", GetLastError()));
            Status = STATUS_ENCRYPTION_FAILED;
            goto CleanUp;
        }

        if (pstrSignKeyConst->Length)
        {
            if ( !CryptHashData( hHash,
                                 (const unsigned char *)pstrSignKeyConst->Buffer,
                                 pstrSignKeyConst->Length,
                                 0 ) )
            {
                DebugLog((DEB_ERROR, "CalculateSASLHMAC: CryptHashData failed : 0x%lx\n", GetLastError()));
                Status = STATUS_ENCRYPTION_FAILED;
                goto CleanUp;
            }
        }

        cbKiHashData = MD5_HASH_BYTESIZE;
        if ( !CryptGetHashParam( hHash,
                                 HP_HASHVAL,
                                 bKiHashData,
                                 &cbKiHashData,
                                 0 ) )
        {
            DebugLog((DEB_ERROR, "CalculateSASLHMAC: CryptGetHashParam failed : 0x%lx\n", GetLastError()));
            Status = STATUS_ENCRYPTION_FAILED;
            goto CleanUp;
        }

        CryptDestroyHash( hHash );
        hHash = NULL;

        ASSERT(cbKiHashData == MD5_HASH_BYTESIZE);

        // save the key for later sign/verify use
        if (fSign == TRUE)
        {
            memcpy(pContext->bKiSignHashData, bKiHashData, MD5_HASH_BYTESIZE);
        }
        else
        {
            memcpy(pContext->bKiVerifyHashData, bKiHashData, MD5_HASH_BYTESIZE);
        }

#if DBG
            // Now convert the Hash to Hex  - for TESTING ONLY
        ZeroMemory(szTemp, TEMPSIZE);
        BinToHex(bKiHashData, MD5_HASH_BYTESIZE, szTemp);
    
        if (szTemp)
        {
            DebugLog((DEB_TRACE, "CalculateSASLHMAC: Calculated Ki hash is %s\n", szTemp));
        }
#endif

    }
    else
    {
        // retrieve it from the saved context info
        if (fSign == TRUE)
        {
            memcpy(bKiHashData, pContext->bKiSignHashData, MD5_HASH_BYTESIZE);
        }
        else
        {
            memcpy(bKiHashData, pContext->bKiVerifyHashData, MD5_HASH_BYTESIZE);
        }
        cbKiHashData = MD5_HASH_BYTESIZE;
#if DBG
            // Now convert the Hash to Hex  - for TESTING ONLY
        ZeroMemory(szTemp, TEMPSIZE);
        BinToHex(bKiHashData, MD5_HASH_BYTESIZE, szTemp);
    
        if (szTemp)
        {
            DebugLog((DEB_TRACE, "CalculateSASLHMAC: Ki retrieved from context is %s\n", szTemp));
        }
#endif

    }

    DebugLog((DEB_TRACE, "CalculateSASLHMAC: Ready to start the HMAC calculation\n"));

    // We now have Kic or Kis depending on if we are running as server or client
    // Now calculate the SASL_MAC_BLOCK structure to compare or set for message

    pMacBlock->wMsgType    = htons(1);
    pMacBlock->dwSeqNumber = htonl(dwSeqNum);
    
    DebugLog((DEB_TRACE, "CalculateSASLHMAC: SeqNumber is %ld\n", dwSeqNum));


    // Need to create the symmetric key from the cleartext shared secret
    // Specified CALC_RC4 since we need to provide a valid encrypt type for import key
    // not actually utilized when we do the HMAC which is simply a hash function
    Status = CreateSymmetricKey(CALG_RC4, MD5_HASH_BYTESIZE, bKiHashData, NULL, &hCryptKey);
    if (!NT_SUCCESS (Status))
    {
        DebugLog((DEB_ERROR, "CalculateSASLHMAC: Error in CreateSymmetricKey     Status 0x%x\n", Status));
        goto CleanUp;
    }

    if ( !CryptCreateHash( g_hCryptProv,
                           CALG_HMAC,
                           hCryptKey,
                           0,
                           &hHash ) )
    {
        DebugLog((DEB_ERROR, "CalculateSASLHMAC: HMAC CryptCreateHash failed : 0x%lx\n", GetLastError()));
        Status = STATUS_ENCRYPTION_FAILED;
        goto CleanUp;
    }

    hmacinfo.HashAlgid = CALG_MD5;     // Use MD5 as the hashing function for the HMAC
    hmacinfo.cbOuterString = 0;        // use default 64 byte outerstring
    hmacinfo.cbInnerString = 0;        // use default 64 byte innerstring

    if ( !CryptSetHashParam( hHash,
                           HP_HMAC_INFO,
                           (PBYTE)&hmacinfo,
                           0 ) )
    {
        DebugLog((DEB_ERROR, "CalculateSASLHMAC: HMAC CryptSetHashParam failed : 0x%lx\n", GetLastError()));
        Status = STATUS_ENCRYPTION_FAILED;
        goto CleanUp;
    }
           // Prepend SeqNum to the data stream to perform HMAC on
           //  Need to form the network order version first

#if DBG
            // Now convert the Hash to Hex  - for TESTING ONLY
        ZeroMemory(szTemp, TEMPSIZE);
        BinToHex((PUCHAR)&pMacBlock->dwSeqNumber, sizeof(DWORD), szTemp);
    
        if (szTemp)
        {
            DebugLog((DEB_TRACE, "CalculateSASLHMAC: HMAC component SeqNum %s\n", szTemp));
        }
#endif
    if ( !CryptHashData( hHash,
                         (const unsigned char *)&pMacBlock->dwSeqNumber,
                         sizeof(DWORD),
                         0 ) )
    {
        DebugLog((DEB_ERROR, "CalculateSASLHMAC: HMAC CryptHashData failed : 0x%lx\n", GetLastError()));
        Status = STATUS_ENCRYPTION_FAILED;
        goto CleanUp;
    }

    // Now HMAC the data to protect

#if DBG
            // Now convert the Hash to Hex  - for TESTING ONLY
        ZeroMemory(szTemp, TEMPSIZE);
        if (cbData < iTempLen)
        {
            iTempLen = cbData;
        }
        BinToHex(pData, iTempLen, szTemp);
    
        if (szTemp)
        {
            DebugLog((DEB_TRACE, "CalculateSASLHMAC: HMAC component Data (%dof%d bytes) %s\n",
                      iTempLen, cbData, szTemp));
        }
#endif
    if (cbData)
    {
        if ( !CryptHashData( hHash,
                             pData,
                             cbData,
                             0 ) )
        {
            DebugLog((DEB_ERROR, "CalculateSASLHMAC: HMAC CryptHashData failed : 0x%lx\n", GetLastError()));
            Status = STATUS_ENCRYPTION_FAILED;
            goto CleanUp;
        }
    }

    cbHMACData = HMAC_MD5_HASH_BYTESIZE;
    if ( !CryptGetHashParam( hHash,
                             HP_HASHVAL,
                             bHMACData,
                             &cbHMACData,
                             0 ) )
    {
        DebugLog((DEB_ERROR, "CalculateSASLHMAC: HMAC CryptGetHashParam failed : 0x%lx\n", GetLastError()));
        Status = STATUS_ENCRYPTION_FAILED;
        goto CleanUp;
    }

    DebugLog((DEB_TRACE, "CalculateSASLHMAC: HMAC hash length  %d bytes\n", cbHMACData));
    ASSERT(cbHMACData == HMAC_MD5_HASH_BYTESIZE);

    CryptDestroyKey( hCryptKey );
    hCryptKey = NULL;

    CryptDestroyHash( hHash );
    hHash = NULL;


    // We now have the HMAC so form up the MAC block for SASL

    // Now convert the Hash to Hex  - for TESTING ONLY
    if (cbHMACData != HMAC_MD5_HASH_BYTESIZE)
    {
        // This should never happen
        DebugLog((DEB_ERROR, "CalculateSASLHMAC: HMAC-MD5 result length incorrect\n"));
        Status = STATUS_ENCRYPTION_FAILED;
        goto CleanUp;
    }

#if DBG
    ZeroMemory(szTemp, TEMPSIZE);
    BinToHex(bHMACData, HMAC_MD5_HASH_BYTESIZE, szTemp);
    DebugLog((DEB_TRACE, "CalculateSASLHMAC: HMAC-MD5 is %s\n", szTemp));
#endif

    memcpy(pMacBlock->hmacMD5, bHMACData, SASL_MAC_HMAC_SIZE);

#if DBG
    ZeroMemory(szTemp, TEMPSIZE);
    BinToHex((PUCHAR)pMacBlock, HMAC_MD5_HASH_BYTESIZE, szTemp);
    DebugLog((DEB_TRACE, "CalculateSASLHMAC: HMAC-MD5 block is %s\n", szTemp));
#endif


CleanUp:

        // Release Key resources
    if (hCryptKey)
    {
        CryptDestroyKey( hCryptKey );
        hCryptKey = NULL;
    }
        // Release Hash resources
    if (hHash)
    {
        CryptDestroyHash( hHash );
        hHash = NULL;
    }

    DebugLog((DEB_TRACE_FUNC, "CalculateSASLHMAC: Leaving     status 0x%x\n", Status));

    return(Status);
}




NTSTATUS
SEC_ENTRY
CalculateKc(
    IN PBYTE pbSessionKey,
    IN USHORT cbHA1n,
    IN PSTRING pstrSealKeyConst,
    IN PBYTE pHashData)                    // MD5 hash for Kc
{
    NTSTATUS Status = STATUS_SUCCESS;

    HCRYPTHASH hHash = NULL;

    DWORD cbKcHashData = 0;                 // Size of Message integrity keys

    ASSERT(cbHA1n <= MD5_HASH_BYTESIZE);
    ASSERT(cbHA1n > 0);

    DebugLog((DEB_TRACE_FUNC, "CalculateKc: Entering\n"));

#if DBG
    char szTemp[TEMPSIZE];
    ZeroMemory(szTemp, TEMPSIZE);

    BinToHex(pbSessionKey, MD5_HASH_BYTESIZE, szTemp);

    DebugLog((DEB_TRACE_FUNC, "CalculateKc: Binary SessionKey %s\n", szTemp));
    DebugLog((DEB_TRACE_FUNC, "CalculateKc: cbHA1n %d\n", cbHA1n));
    DebugLog((DEB_TRACE_FUNC, "CalculateKc: SealKeyConst %Z\n", pstrSealKeyConst));
#endif


    // Clear the output
    ZeroMemory(pHashData, MD5_HASH_BYTESIZE);


    // Kc = MD5( {H(A1)[0...cbHA1n], ConstantString})    take only the first cbHA1n bytes of H(A1)
    if ( !CryptCreateHash( g_hCryptProv,
                           CALG_MD5,
                           0,
                           0,
                           &hHash ) )
    {
        DebugLog((DEB_ERROR, "CalculateKc: CryptCreateHash failed : 0x%lx\n", GetLastError()));
        Status = STATUS_ENCRYPTION_FAILED;
        goto CleanUp;
    }

    if ( !CryptHashData( hHash,
                         (const unsigned char *)pbSessionKey,
                         cbHA1n,
                         0 ) )
    {
        DebugLog((DEB_ERROR, "CalculateKc: CryptHashData failed : 0x%lx\n", GetLastError()));
        Status = STATUS_ENCRYPTION_FAILED;
        goto CleanUp;
    }

    if (pstrSealKeyConst->Length)
    {
        if ( !CryptHashData( hHash,
                             (const unsigned char *)pstrSealKeyConst->Buffer,
                             pstrSealKeyConst->Length,
                             0 ) )
        {
            DebugLog((DEB_ERROR, "CalculateKc: CryptHashData failed : 0x%lx\n", GetLastError()));
            Status = STATUS_ENCRYPTION_FAILED;
            goto CleanUp;
        }
    }

    cbKcHashData = MD5_HASH_BYTESIZE;
    if ( !CryptGetHashParam( hHash,
                             HP_HASHVAL,
                             pHashData,
                             &cbKcHashData,
                             0 ) )
    {
        DebugLog((DEB_ERROR, "CalculateKc: CryptGetHashParam failed : 0x%lx\n", GetLastError()));
        Status = STATUS_ENCRYPTION_FAILED;
        goto CleanUp;
    }

    CryptDestroyHash( hHash );
    hHash = NULL;

    DebugLog((DEB_TRACE, "CalculateKc: readback hash with %d bytes\n", cbKcHashData));

#if DBG
        // Now convert the Hash to Hex  - for TESTING ONLY
    ZeroMemory(szTemp, TEMPSIZE);
    BinToHex(pHashData, MD5_HASH_BYTESIZE, szTemp);

    if (szTemp)
    {
        DebugLog((DEB_TRACE, "CalculateKc: Kc hash is %s\n", szTemp));
    }
#endif


CleanUp:

        // Release Hash resources
    if (hHash)
    {
        CryptDestroyHash( hHash );
        hHash = NULL;
    }

    DebugLog((DEB_TRACE_FUNC, "CalculateKc: Leaving     status 0x%x\n", Status));

    return(Status);
}




BYTE DESParityTable[] = {0x00,0x01,0x01,0x02,0x01,0x02,0x02,0x03,
                      0x01,0x02,0x02,0x03,0x02,0x03,0x03,0x04};

//
// set the parity on the DES key - ODD parity
// NOTE : must be called before deskey
// key must be cbKey number of bytes
// routine from RSA lib
//
void
SetDESParity(
        PBYTE           pbKey,
        DWORD           cbKey
        )
{
    DWORD i;

    for (i=0;i<cbKey;i++)
    {
        if (!((DESParityTable[pbKey[i]>>4] + DESParityTable[pbKey[i]&0x0F]) % 2))
            pbKey[i] = pbKey[i] ^ 0x01;
    }
}



//+-------------------------------------------------------------------------
//
//  Function:   addDESParity
//
//  Synopsis:   This routine is called for DES plaintext keys to add in Odd parity bits
//              Input of 7 bytes will be expanded to 8bytes with parity
//              Input of 14 bytes will be expanded to 14 bytes
//
//  Effects:    no global effect.
//
//  Arguments:
//
//  IN   pbSrckey              -- buffer with key to expand
//  IN   cbKey             -- size of input non-parity expanded key
//  OUT   pbOutputkey              -- buffer with key to expand
//
//  Requires:   no global requirements
//
//  Returns:    STATUS_SUCCESS, or resource error
//
//  Notes: 
//
//
//--------------------------------------------------------------------------
NTSTATUS
AddDESParity(
    IN PBYTE           pbSrcKey,
    IN DWORD           cbSrcKey,
    OUT PBYTE          pbDstKey,
    OUT PDWORD          pcbDstKey
    )
{
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(pbSrcKey);
    ASSERT(pbDstKey);
    ASSERT(pcbDstKey);

    ZeroMemory(pbDstKey, MD5_HASH_BYTESIZE);

    if ((cbSrcKey != 7) && (cbSrcKey != 14))
    {
        DebugLog((DEB_ERROR, "AddDESParity: wrong input size buffer\n"));
        Status = STATUS_INTERNAL_ERROR;
        goto CleanUp;
    }


	pbDstKey[0] =  pbSrcKey[0];
    pbDstKey[1] = (pbSrcKey[1] >> 1) | ((pbSrcKey[0] & 0x01) << 7);
	pbDstKey[2] = (pbSrcKey[2] >> 2) | ((pbSrcKey[1] & 0x03) << 6);
	pbDstKey[3] = (pbSrcKey[3] >> 3) | ((pbSrcKey[2] & 0x07) << 5);
	pbDstKey[4] = (pbSrcKey[4] >> 4) | ((pbSrcKey[3] & 0x0F) << 4);
	pbDstKey[5] = (pbSrcKey[5] >> 5) | ((pbSrcKey[4] & 0x1F) << 3);
	pbDstKey[6] = (pbSrcKey[6] >> 6) | ((pbSrcKey[5] & 0x3F) << 2);
	pbDstKey[7] = (pbSrcKey[6] << 1);

    SetDESParity(pbDstKey, 8);
    *pcbDstKey = 8;

    // Now check if need to expand the 14 bytes into the full 16 byte buffer
    if (cbSrcKey == 14)
    {
        pbDstKey[0 + 8] =  pbSrcKey[0 + 7];
        pbDstKey[1 + 8] = (pbSrcKey[1 + 7] >> 1) | ((pbSrcKey[0 + 7] & 0x01) << 7);
        pbDstKey[2 + 8] = (pbSrcKey[2 + 7] >> 2) | ((pbSrcKey[1 + 7] & 0x03) << 6);
        pbDstKey[3 + 8] = (pbSrcKey[3 + 7] >> 3) | ((pbSrcKey[2 + 7] & 0x07) << 5);
        pbDstKey[4 + 8] = (pbSrcKey[4 + 7] >> 4) | ((pbSrcKey[3 + 7] & 0x0F) << 4);
        pbDstKey[5 + 8] = (pbSrcKey[5 + 7] >> 5) | ((pbSrcKey[4 + 7] & 0x1F) << 3);
        pbDstKey[6 + 8] = (pbSrcKey[6 + 7] >> 6) | ((pbSrcKey[5 + 7] & 0x3F) << 2);
        pbDstKey[7 + 8] = (pbSrcKey[6 + 7] << 1);
        SetDESParity(pbDstKey + 8, 8);
        *pcbDstKey = 16;
    }

#if DBG
    char szTemp[TEMPSIZE];
    ZeroMemory(szTemp, TEMPSIZE);

    BinToHex(pbSrcKey, (UINT)cbSrcKey, szTemp);
    DebugLog((DEB_TRACE, "AddDESParity: Key no-parity : %s\n", szTemp));


    ZeroMemory(szTemp, TEMPSIZE);
    BinToHex(pbDstKey, (UINT)*pcbDstKey, szTemp);
    DebugLog((DEB_TRACE, "AddDESParity: Key expanded with parity : %s\n", szTemp));
#endif

CleanUp:

    return Status;
}


//+--------------------------------------------------------------------
//
//  Function:   CalculateDataCount
//
//  Synopsis:   Determine the number of data bytes to process in the SecBuffers
//            
//
//  Arguments:  pContext - UserMode Context for the security state
//              Op - operation to perform on the Sec buffers
//              pMessage - sec buffers to processs and return output
//                    
//
//  Returns: NTSTATUS
//
//  Notes:
//
//---------------------------------------------------------------------
NTSTATUS
CalculateDataCount(
        IN PSecBufferDesc pSecBuff,
        OUT PULONG pulData
        )
{
    NTSTATUS Status = STATUS_SUCCESS;
    USHORT Index = 0;
    ULONG ulcb = 0;            // number of bytes in the actual message

    ASSERT(pulData);
    ASSERT(pSecBuff);

    for (Index = 0; Index < pSecBuff->cBuffers ; Index++ )
    {
        if (BUFFERTYPE(pSecBuff->pBuffers[Index]) == SECBUFFER_DATA)
        {
            ulcb = ulcb + pSecBuff->pBuffers[Index].cbBuffer;
        }
    }

    *pulData = ulcb;

    return(Status);
}


// Multiple Data buffers in SecBuffer version of CalculateSASLHMAC
// This was added to allow LDAP server to do gather-scatter processing
NTSTATUS
SEC_ENTRY
CalculateSASLHMACMulti(
    IN PDIGEST_USERCONTEXT pContext,
    IN BOOL  fSign,
    IN PSTRING pstrSignKeyConst,
    IN DWORD dwSeqNum,                     // Sequence number to process
    IN PSecBufferDesc pSecBuff,            // location of data buffers to HMAC
    OUT PSASL_MAC_BLOCK pMacBlock)
{
    NTSTATUS Status = STATUS_SUCCESS;

    HCRYPTHASH hHash = NULL;
    HCRYPTKEY hCryptKey = NULL;
    HMAC_INFO hmacinfo = {0};

    BYTE bKiHashData[MD5_HASH_BYTESIZE];    // Message integrity keys RFC 2831 sec 2.3
    DWORD cbKiHashData = 0;                 // Size of Message integrity keys

    BYTE bHMACData[HMAC_MD5_HASH_BYTESIZE];
    DWORD cbHMACData = 0;

#if DBG
    char szTemp[TEMPSIZE];
    ZeroMemory(szTemp, TEMPSIZE);
#endif


    DebugLog((DEB_TRACE_FUNC, "CalculateSASLHMACMulti: Entering\n"));

    // Clear the output
    ZeroMemory(pMacBlock, sizeof(SASL_MAC_BLOCK));

    // Initialize local variables
    ZeroMemory(bKiHashData, MD5_HASH_BYTESIZE);
    ZeroMemory(bHMACData, HMAC_MD5_HASH_BYTESIZE);


    // Always do an integrety calculation on the input data
    // We should have clear text data at this stage
    if (!dwSeqNum)
    {
        if ( !CryptCreateHash( g_hCryptProv,
                               CALG_MD5,
                               0,
                               0,
                               &hHash ) )
        {
            DebugLog((DEB_ERROR, "CalculateSASLHMACMulti: CryptCreateHash failed : 0x%lx\n", GetLastError()));
            Status = STATUS_ENCRYPTION_FAILED;
            goto CleanUp;
        }

        if ( !CryptHashData( hHash,
                             (const unsigned char *)pContext->bSessionKey,
                             MD5_HASH_BYTESIZE,
                             0 ) )
        {
            DebugLog((DEB_ERROR, "CalculateSASLHMACMulti: CryptHashData failed : 0x%lx\n", GetLastError()));
            Status = STATUS_ENCRYPTION_FAILED;
            goto CleanUp;
        }

        if (pstrSignKeyConst->Length)
        {
            if ( !CryptHashData( hHash,
                                 (const unsigned char *)pstrSignKeyConst->Buffer,
                                 pstrSignKeyConst->Length,
                                 0 ) )
            {
                DebugLog((DEB_ERROR, "CalculateSASLHMACMulti: CryptHashData failed : 0x%lx\n", GetLastError()));
                Status = STATUS_ENCRYPTION_FAILED;
                goto CleanUp;
            }
        }

        cbKiHashData = MD5_HASH_BYTESIZE;
        if ( !CryptGetHashParam( hHash,
                                 HP_HASHVAL,
                                 bKiHashData,
                                 &cbKiHashData,
                                 0 ) )
        {
            DebugLog((DEB_ERROR, "CalculateSASLHMACMulti: CryptGetHashParam failed : 0x%lx\n", GetLastError()));
            Status = STATUS_ENCRYPTION_FAILED;
            goto CleanUp;
        }

        CryptDestroyHash( hHash );
        hHash = NULL;

        ASSERT(cbKiHashData == MD5_HASH_BYTESIZE);

        // save the key for later sign/verify use
        if (fSign == TRUE)
        {
            memcpy(pContext->bKiSignHashData, bKiHashData, MD5_HASH_BYTESIZE);
        }
        else
        {
            memcpy(pContext->bKiVerifyHashData, bKiHashData, MD5_HASH_BYTESIZE);
        }

#if DBG
            // Now convert the Hash to Hex  - for TESTING ONLY
        ZeroMemory(szTemp, TEMPSIZE);
        BinToHex(bKiHashData, MD5_HASH_BYTESIZE, szTemp);
    
        if (szTemp)
        {
            DebugLog((DEB_TRACE, "CalculateSASLHMACMulti: Calculated Ki hash is %s\n", szTemp));
        }
#endif

    }
    else
    {
        // retrieve it from the saved context info
        if (fSign == TRUE)
        {
            memcpy(bKiHashData, pContext->bKiSignHashData, MD5_HASH_BYTESIZE);
        }
        else
        {
            memcpy(bKiHashData, pContext->bKiVerifyHashData, MD5_HASH_BYTESIZE);
        }
        cbKiHashData = MD5_HASH_BYTESIZE;
#if DBG
            // Now convert the Hash to Hex  - for TESTING ONLY
        ZeroMemory(szTemp, TEMPSIZE);
        BinToHex(bKiHashData, MD5_HASH_BYTESIZE, szTemp);
    
        if (szTemp)
        {
            DebugLog((DEB_TRACE, "CalculateSASLHMACMulti: Ki retrieved from context is %s\n", szTemp));
        }
#endif

    }

    DebugLog((DEB_TRACE, "CalculateSASLHMACMulti: Ready to start the HMAC calculation\n"));

    // We now have Kic or Kis depending on if we are running as server or client
    // Now calculate the SASL_MAC_BLOCK structure to compare or set for message

    pMacBlock->wMsgType    = htons(1);
    pMacBlock->dwSeqNumber = htonl(dwSeqNum);
    
    DebugLog((DEB_TRACE, "CalculateSASLHMACMulti: SeqNumber is %ld\n", dwSeqNum));


    // Need to create the symmetric key from the cleartext shared secret
    // Specified CALC_RC4 since we need to provide a valid encrypt type for import key
    // not actually utilized when we do the HMAC which is simply a hash function
    Status = CreateSymmetricKey(CALG_RC4, MD5_HASH_BYTESIZE, bKiHashData, NULL, &hCryptKey);
    if (!NT_SUCCESS (Status))
    {
        DebugLog((DEB_ERROR, "CalculateSASLHMACMulti: Error in CreateSymmetricKey     Status 0x%x\n", Status));
        goto CleanUp;
    }

    if ( !CryptCreateHash( g_hCryptProv,
                           CALG_HMAC,
                           hCryptKey,
                           0,
                           &hHash ) )
    {
        DebugLog((DEB_ERROR, "CalculateSASLHMACMulti: HMAC CryptCreateHash failed : 0x%lx\n", GetLastError()));
        Status = STATUS_ENCRYPTION_FAILED;
        goto CleanUp;
    }

    hmacinfo.HashAlgid = CALG_MD5;     // Use MD5 as the hashing function for the HMAC
    hmacinfo.cbOuterString = 0;        // use default 64 byte outerstring
    hmacinfo.cbInnerString = 0;        // use default 64 byte innerstring

    if ( !CryptSetHashParam( hHash,
                           HP_HMAC_INFO,
                           (PBYTE)&hmacinfo,
                           0 ) )
    {
        DebugLog((DEB_ERROR, "CalculateSASLHMACMulti: HMAC CryptSetHashParam failed : 0x%lx\n", GetLastError()));
        Status = STATUS_ENCRYPTION_FAILED;
        goto CleanUp;
    }
           // Prepend SeqNum to the data stream to perform HMAC on
           //  Need to form the network order version first

#if DBG
            // Now convert the Hash to Hex  - for TESTING ONLY
        ZeroMemory(szTemp, TEMPSIZE);
        BinToHex((PUCHAR)&pMacBlock->dwSeqNumber, sizeof(DWORD), szTemp);
    
        if (szTemp)
        {
            DebugLog((DEB_TRACE, "CalculateSASLHMACMulti: HMAC component SeqNum %s\n", szTemp));
        }
#endif
    if ( !CryptHashData( hHash,
                         (const unsigned char *)&pMacBlock->dwSeqNumber,
                         sizeof(DWORD),
                         0 ) )
    {
        DebugLog((DEB_ERROR, "CalculateSASLHMACMulti: HMAC CryptHashData failed : 0x%lx\n", GetLastError()));
        Status = STATUS_ENCRYPTION_FAILED;
        goto CleanUp;
    }

    // Now HMAC the data to protect
    // Now scan the SecBuffers for Data buffers to process

    for (ULONG Index = 0; Index < pSecBuff->cBuffers ; Index++ )
    {
        if (BUFFERTYPE(pSecBuff->pBuffers[Index]) == SECBUFFER_DATA)
        {
            if (pSecBuff->pBuffers[Index].cbBuffer && pSecBuff->pBuffers[Index].pvBuffer)
            {
                if ( !CryptHashData( hHash,
                                     (PBYTE)pSecBuff->pBuffers[Index].pvBuffer,
                                     pSecBuff->pBuffers[Index].cbBuffer,
                                     0 ) )
                {
                    DebugLog((DEB_ERROR, "CalculateSASLHMACMulti: HMAC CryptHashData failed : 0x%lx\n", GetLastError()));
                    Status = STATUS_ENCRYPTION_FAILED;
                    goto CleanUp;
                }
            }
        }
    }

    cbHMACData = HMAC_MD5_HASH_BYTESIZE;
    if ( !CryptGetHashParam( hHash,
                             HP_HASHVAL,
                             bHMACData,
                             &cbHMACData,
                             0 ) )
    {
        DebugLog((DEB_ERROR, "CalculateSASLHMACMulti: HMAC CryptGetHashParam failed : 0x%lx\n", GetLastError()));
        Status = STATUS_ENCRYPTION_FAILED;
        goto CleanUp;
    }

    DebugLog((DEB_TRACE, "CalculateSASLHMACMulti: HMAC hash length  %d bytes\n", cbHMACData));
    ASSERT(cbHMACData == HMAC_MD5_HASH_BYTESIZE);

    CryptDestroyKey( hCryptKey );
    hCryptKey = NULL;

    CryptDestroyHash( hHash );
    hHash = NULL;


    // We now have the HMAC so form up the MAC block for SASL

    // Now convert the Hash to Hex  - for TESTING ONLY
    if (cbHMACData != HMAC_MD5_HASH_BYTESIZE)
    {
        // This should never happen
        DebugLog((DEB_ERROR, "CalculateSASLHMACMulti: HMAC-MD5 result length incorrect\n"));
        Status = STATUS_ENCRYPTION_FAILED;
        goto CleanUp;
    }

#if DBG
    ZeroMemory(szTemp, TEMPSIZE);
    BinToHex(bHMACData, HMAC_MD5_HASH_BYTESIZE, szTemp);
    DebugLog((DEB_TRACE, "CalculateSASLHMACMulti: HMAC-MD5 is %s\n", szTemp));
#endif

    memcpy(pMacBlock->hmacMD5, bHMACData, SASL_MAC_HMAC_SIZE);

#if DBG
    ZeroMemory(szTemp, TEMPSIZE);
    BinToHex((PUCHAR)pMacBlock, HMAC_MD5_HASH_BYTESIZE, szTemp);
    DebugLog((DEB_TRACE, "CalculateSASLHMACMulti: HMAC-MD5 block is %s\n", szTemp));
#endif


CleanUp:

        // Release Key resources
    if (hCryptKey)
    {
        CryptDestroyKey( hCryptKey );
        hCryptKey = NULL;
    }
        // Release Hash resources
    if (hHash)
    {
        CryptDestroyHash( hHash );
        hHash = NULL;
    }

    DebugLog((DEB_TRACE_FUNC, "CalculateSASLHMACMulti: Leaving     status 0x%x\n", Status));

    return(Status);
}


// Compare the directive values passed in from client on ChallengeResponse to make
// sure that they are the same for subsequent ChallengeResponses
NTSTATUS
SEC_ENTRY
DigestUserCompareDirectives(
    IN PDIGEST_USERCONTEXT pContext,
    IN PDIGEST_PARAMETER pDigest)
{
    NTSTATUS Status = STATUS_SUCCESS;
    int iAuth = 0;

    DebugLog((DEB_TRACE_FUNC, "DigestUserCompareDirectives: Entering\n"));

    for (iAuth = 0; iAuth < MD5_AUTH_LAST; iAuth++)
    {
        switch (iAuth)
        {
        case MD5_AUTH_USERNAME:
        case MD5_AUTH_REALM:
        case MD5_AUTH_NONCE:
        case MD5_AUTH_CNONCE:
            if (!RtlEqualString(&(pContext->strParam[iAuth]),
                                 &(pDigest->refstrParam[iAuth]),
                                 FALSE))
            {
                Status = SEC_E_ILLEGAL_MESSAGE;
                DebugLog((DEB_ERROR, "DigestUserCompareDirectives: Directive value %d mismatch     status 0x%x\n",
                          iAuth, Status));
                goto CleanUp;

            }
        }
    }


CleanUp:

    DebugLog((DEB_TRACE_FUNC, "DigestUserCompareDirectives: Leaving     status 0x%x\n", Status));

    return(Status);
}


