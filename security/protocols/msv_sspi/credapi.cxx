//+-----------------------------------------------------------------------
//
// Microsoft Windows
//
// Copyright (c) Microsoft Corporation 1992 - 1996
//
// File:        credapi.cxx
//
// Contents:    Code for credentials APIs for the NtLm package
//              Main entry points into this dll:
//                SpAcceptCredentials
//                SpAcquireCredentialsHandle
//                SpFreeCredentialsHandle
//                SpQueryCredentialsAttributes
//                SpSaveCredentials
//                SpGetCredentials
//                SpDeleteCredentials
//
//              Helper functions:
//                CopyClientString
//
// History:     ChandanS   26-Jul-1996   Stolen from kerberos\client2\credapi.cxx
//
//------------------------------------------------------------------------
#define NTLM_CREDAPI
#include <global.h>

extern "C"
{
#include <nlp.h>
}



//+-------------------------------------------------------------------------
//
//  Function:   CopyClientString
//
//  Synopsis:   copies a client string to local memory, including
//              allocating space for it locally.
//
//  Arguments:
//              SourceString  - Could be Oem or Wchar in client process
//              SourceLength  - bytes
//              DoUnicode     - whether the string is Wchar
//
//  Returns:
//              DestinationString - Unicode String in Lsa Process
//
//  Notes:
//
//--------------------------------------------------------------------------
HRESULT
CopyClientString(
    IN PWSTR SourceString,
    IN ULONG SourceLength,
    IN BOOLEAN DoUnicode,
    OUT PUNICODE_STRING DestinationString
    )
{
    SspPrint((SSP_API_MORE,"Entering CopyClientString\n"));

    NTSTATUS Status = STATUS_SUCCESS;
    STRING TemporaryString;
    ULONG SourceSize = 0;
    ULONG CharacterSize = sizeof(CHAR);

    //
    // First initialize the string to zero, in case the source is a null
    // string
    //

    DestinationString->Length = DestinationString->MaximumLength = 0;
    DestinationString->Buffer = NULL;
    TemporaryString.Buffer = NULL;


    if (SourceString != NULL)
    {

        //
        // If the length is zero, allocate one byte for a "\0" terminator
        //

        if (SourceLength == 0)
        {
            DestinationString->Buffer = (LPWSTR) NtLmAllocate(sizeof(WCHAR));
            if (DestinationString->Buffer == NULL)
            {
                SspPrint((SSP_CRITICAL,"CopyClientString, Error from NtLmAllocate is 0x%lx\n", Status));
                Status = STATUS_NO_MEMORY;
                goto Cleanup;
            }
            DestinationString->MaximumLength = sizeof(WCHAR);
            *DestinationString->Buffer = L'\0';

        }
        else
        {
            //
            // Allocate a temporary buffer to hold the client string. We may
            // then create a buffer for the unicode version. The length
            // is the length in characters, so  possible expand to hold unicode
            // characters and a null terminator.
            //

            if (DoUnicode)
            {
                CharacterSize = sizeof(WCHAR);
            }

            SourceSize = (SourceLength + 1) * CharacterSize;

            //
            // insure no overflow aggainst UNICODE_STRING
            //

            if ( (SourceSize > 0xFFFF) ||
                 ((SourceSize - CharacterSize) > 0xFFFF)
                 )
            {
                Status = STATUS_INVALID_PARAMETER;
                SspPrint((SSP_CRITICAL,"CopyClientString, SourceSize is too large\n"));
                goto Cleanup;
            }


            TemporaryString.Buffer = (LPSTR) NtLmAllocate(SourceSize);
            if (TemporaryString.Buffer == NULL)
            {
                Status = STATUS_NO_MEMORY;
                SspPrint((SSP_CRITICAL,"CopyClientString, Error from NtLmAllocate is 0x%lx\n", Status));
                goto Cleanup;
            }
            TemporaryString.Length = (USHORT) (SourceSize - CharacterSize);
            TemporaryString.MaximumLength = (USHORT) SourceSize;


            //
            // Finally copy the string from the client
            //

            Status = LsaFunctions->CopyFromClientBuffer(
                            NULL,
                            SourceSize - CharacterSize,
                            TemporaryString.Buffer,
                            SourceString
                            );

            if (!NT_SUCCESS(Status))
            {
                SspPrint((SSP_CRITICAL,"CopyClientString, Error from LsaFunctions->CopyFromClientBuffer is 0x%lx\n", Status));
                goto Cleanup;
            }

            //
            // If we are doing unicode, finish up now
            //
            if (DoUnicode)
            {
                DestinationString->Buffer = (LPWSTR) TemporaryString.Buffer;
                DestinationString->Length = (USHORT) (SourceSize - CharacterSize);
                DestinationString->MaximumLength = (USHORT) SourceSize;

                TemporaryString.Buffer = NULL;
            }
            else
            {
                //
                // allocate enough space based on the size of the original Unicode input.
                // required so that we can use our own allocation scheme.
                //

                DestinationString->Buffer = (LPWSTR)NtLmAllocate( SourceSize*sizeof(WCHAR) );
                if( DestinationString->Buffer == NULL )
                {
                    Status = STATUS_NO_MEMORY;
                    SspPrint((SSP_CRITICAL,"Error from NtLmAllocate\n"));
                    goto Cleanup;
                }

                DestinationString->Length = (USHORT)(SourceSize - CharacterSize);
                DestinationString->MaximumLength = (USHORT)SourceSize * sizeof(WCHAR);

                Status = RtlOemStringToUnicodeString(
                            DestinationString,
                            &TemporaryString,
                            FALSE
                            );
                if (!NT_SUCCESS(Status))
                {
                    SspPrint((SSP_CRITICAL,"CopyClientString, Error from RtlOemStringToUnicodeString is 0x%lx\n", Status));
                    // set to STATUS_NO_MEMORY, as it's unlikely that locale error code would be useful.
                    Status = STATUS_NO_MEMORY;
                    goto Cleanup;
                }
            }
        }
    }

Cleanup:

    if (TemporaryString.Buffer != NULL)
    {
        NtLmFree(TemporaryString.Buffer);
    }

    SspPrint((SSP_API_MORE,"Leaving CopyClientString\n"));

    return(Status);
}


//+-------------------------------------------------------------------------
//
//  Function:   SpAcceptCredentials
//
//  Synopsis:   This routine is called after another package has logged
//              a user on.  The other package provides a user name and
//              password and the NtLm package will create a logon
//              session for this user.
//
//  Effects:    Creates a logon session
//
//  Arguments:  LogonType - Type of logon, such as network or interactive
//              Accountname - Name of the account that logged on
//              PrimaryCredentials - Primary credentials for the account,
//                  containing a domain name, password, SID, etc.
//              SupplementalCredentials - NtLm -Specific blob of
//                  supplemental credentials.
//
//  Returns:    None
//
//  Notes:
//
//--------------------------------------------------------------------------
NTSTATUS NTAPI
SpAcceptCredentials(
    IN SECURITY_LOGON_TYPE LogonType,
    IN PUNICODE_STRING AccountName,
    IN PSECPKG_PRIMARY_CRED PrimaryCredentials,
    IN PSECPKG_SUPPLEMENTAL_CRED SupplementalCredentials
    )
{
    NTSTATUS Status = S_OK;
    SspPrint((SSP_API,"Entering SpAcceptCredentials\n"));

    Status = SspAcceptCredentials(
                LogonType,
                PrimaryCredentials,
                SupplementalCredentials
                );

    SspPrint((SSP_API,"Leaving SpAcceptCredentials\n"));

    return(SspNtStatusToSecStatus(Status, SEC_E_INTERNAL_ERROR));
    UNREFERENCED_PARAMETER( AccountName );
}


//+-------------------------------------------------------------------------
//
//  Function:   SpAcquireCredentialsHandle
//
//  Synopsis:   Contains NtLm Code for AcquireCredentialsHandle which
//              creates a Credential associated with a logon session.
//
//  Effects:    Creates a SSP_CREDENTIAL
//
//  Arguments:  PrincipalName - Name of logon session for which to create credential
//              CredentialUseFlags - Flags indicating whether the credentials
//                  is for inbound or outbound use.
//              LogonId - The logon ID of logon session for which to create
//                  a credential.
//              AuthorizationData - Unused blob of NtLm-specific data
//              GetKeyFunction - Unused function to retrieve a session key
//              GetKeyArgument - Argument for GetKeyFunction
//              CredentialHandle - Receives handle to new credential
//              ExpirationTime - Receives expiration time for credential
//
//  Returns:
//    STATUS_SUCCESS -- Call completed successfully
//    SEC_E_NO_SPM -- Security Support Provider is not running
//    SEC_E_PACKAGE_UNKNOWN -- Package being queried is not this package
//    SEC_E_PRINCIPAL_UNKNOWN -- No such principal
//    SEC_E_NOT_OWNER -- caller does not own the specified credentials
//    SEC_E_INSUFFICIENT_MEMORY -- Not enough memory
//
//  Notes:
//
//--------------------------------------------------------------------------

NTSTATUS NTAPI
SpAcquireCredentialsHandle(
    IN OPTIONAL PUNICODE_STRING PrincipalName,
    IN ULONG CredentialUseFlags,
    IN OPTIONAL PLUID LogonId,
    IN PVOID AuthorizationData,
    IN PVOID GetKeyFunction,
    IN PVOID GetKeyArgument,
    OUT PULONG_PTR CredentialHandle,
    OUT PTimeStamp ExpirationTime
    )
{
    SspPrint((SSP_API,"Entering SpAcquireCredentialsHandle\n"));

    NTSTATUS Status = STATUS_SUCCESS;

    UNICODE_STRING UserName;
    UNICODE_STRING DomainName;
    UNICODE_STRING Password;
    ULONG NewCredentialUseFlags = CredentialUseFlags;
    PSEC_WINNT_AUTH_IDENTITY pAuthIdentity = NULL;
    BOOLEAN DoUnicode = TRUE;
    PSEC_WINNT_AUTH_IDENTITY_EXW pAuthIdentityEx = NULL;

    PSEC_WINNT_AUTH_IDENTITY_W TmpCredentials = NULL;
    ULONG CredSize = 0;
    ULONG Offset = 0;

    //
    // Initialization
    //

    RtlInitUnicodeString(
        &UserName,
        NULL);

    RtlInitUnicodeString(
        &DomainName,
        NULL);

    RtlInitUnicodeString(
        &Password,
        NULL);

    //
    // Validate the arguments
    //

    if ( (CredentialUseFlags & (SECPKG_CRED_OUTBOUND |SECPKG_CRED_INBOUND)) == 0)
    {
        Status = SEC_E_INVALID_CREDENTIAL_USE;
        goto Cleanup;
    }

    if ( ARGUMENT_PRESENT(GetKeyFunction) ) {
        Status = SEC_E_UNSUPPORTED_FUNCTION;
        SspPrint((SSP_CRITICAL,"Error from SpAquireCredentialsHandle is 0x%lx\n", Status));
        goto Cleanup;
    }

    // RDR2 passes in a 1 while talking to down level clients

    if ( ARGUMENT_PRESENT(GetKeyArgument) && (GetKeyArgument != (PVOID) 1)) {
        Status = SEC_E_UNSUPPORTED_FUNCTION;
        SspPrint((SSP_CRITICAL,"Error from SpAquireCredentialsHandle is 0x%lx\n", Status));
        goto Cleanup;
    }

    //
    // First get information about the caller.
    //

    SECPKG_CLIENT_INFO ClientInfo;
    PLUID LogonIdToUse;

    Status = LsaFunctions->GetClientInfo(&ClientInfo);
    if (!NT_SUCCESS(Status))
    {
        SspPrint((SSP_CRITICAL,"SpAcquireCredentialsHandle, Error from LsaFunctions->GetClientInfo is 0x%lx\n", Status));
        goto Cleanup;
    }

    //
    // If the caller supplied a logon ID, and it doesn't match the caller,
    // they must have the TCB privilege
    //


    if (ARGUMENT_PRESENT(LogonId) &&
        ((LogonId->LowPart != 0) || (LogonId->HighPart != 0)) &&
        !RtlEqualLuid( LogonId, &ClientInfo.LogonId)
        )
    {
        if (!ClientInfo.HasTcbPrivilege)
        {
            Status = STATUS_PRIVILEGE_NOT_HELD;
            SspPrint((SSP_CRITICAL,"SpAcquireCredentialsHandle, Error from ClientInfo.HasTcbPrivilege is 0x%lx\n", Status));
            goto Cleanup;
        }

        LogonIdToUse = LogonId;

        // note: there is a special case where the LogonIdToUse specifies
        // the SYSTEM token, and there may not be a credential (and access token)
        // for that Luid yet.  This special case is handled in SsprAcquireCredentialsHandle()

    }
    else
    {
        //
        // Use the callers logon id.
        //

        LogonIdToUse = &ClientInfo.LogonId;
    } 

    //
    // Got to have an impersonation level token in order to call ACH.
    // This check used to be in lsa, but moved here to enable
    // some S4Uproxy scenarios to work w/o tcb.
    //
    if (ClientInfo.ImpersonationLevel <= SecurityIdentification)
    {
        SspPrint((SSP_CRITICAL, "Trying to acquire credentials with an token no better than SecurityIdentification\n"));
        Status = SEC_E_NO_CREDENTIALS;
        goto Cleanup;
    }

    //
    // Copy over the authorization data into out address
    // space and make a local copy of the strings.
    //


    if (AuthorizationData != NULL)
    {
        SECPKG_CALL_INFO CallInfo ;
        SEC_WINNT_AUTH_IDENTITY32 Cred32 = {0};
        SEC_WINNT_AUTH_IDENTITY_EX32 CredEx32 ;

        if(!LsaFunctions->GetCallInfo( &CallInfo ))
        {
            Status = STATUS_INTERNAL_ERROR;
            SspPrint((SSP_CRITICAL,"SpAcquireCredentialsHandle, Error from LsaFunctions->GetCallInfo is 0x%lx\n", Status));
            goto Cleanup;
        }

        pAuthIdentityEx = (PSEC_WINNT_AUTH_IDENTITY_EXW) NtLmAllocate(sizeof(SEC_WINNT_AUTH_IDENTITY_EXW));

        if (pAuthIdentityEx != NULL)
        {
            if ( CallInfo.Attributes & SECPKG_CALL_WOWCLIENT )
            {

                Status = LsaFunctions->CopyFromClientBuffer(
                            NULL,
                            sizeof( Cred32 ),
                            pAuthIdentityEx,
                            AuthorizationData );

                if ( NT_SUCCESS( Status ) )
                {
                    RtlCopyMemory( &Cred32, pAuthIdentityEx, sizeof( Cred32 ) );
                }

            }
            else
            {
                Status = LsaFunctions->CopyFromClientBuffer(
                            NULL,
                            sizeof(SEC_WINNT_AUTH_IDENTITY),
                            pAuthIdentityEx,
                            AuthorizationData);
            }


            if (!NT_SUCCESS(Status))
            {
                SspPrint((SSP_CRITICAL,"SpAcquireCredentialsHandle, Error from LsaFunctions->CopyFromClientBuffer is 0x%lx\n", Status));
                goto Cleanup;
            }
        }
        else
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            SspPrint((SSP_CRITICAL,"SpAcquireCredentialsHandle, Error from NtLmAllocate is 0x%lx\n", Status));
            goto Cleanup;
        }

        //
        // Check for the ex version
        //

        if (pAuthIdentityEx->Version == SEC_WINNT_AUTH_IDENTITY_VERSION)
        {
            //
            // It's an EX structure.
            //

            if ( CallInfo.Attributes & SECPKG_CALL_WOWCLIENT )
            {
                Status = LsaFunctions->CopyFromClientBuffer(
                            NULL,
                            sizeof( CredEx32 ),
                            &CredEx32,
                            AuthorizationData );

                if ( NT_SUCCESS( Status ) )
                {
                    pAuthIdentityEx->Version = CredEx32.Version ;
                    pAuthIdentityEx->Length = (CredEx32.Length < sizeof( SEC_WINNT_AUTH_IDENTITY_EX ) ?
                                               (ULONG) sizeof( SEC_WINNT_AUTH_IDENTITY_EX ) : CredEx32.Length );

                    pAuthIdentityEx->User = (PWSTR) UlongToPtr( CredEx32.User );
                    pAuthIdentityEx->UserLength = CredEx32.UserLength ;
                    pAuthIdentityEx->Domain = (PWSTR) UlongToPtr( CredEx32.Domain );
                    pAuthIdentityEx->DomainLength = CredEx32.DomainLength ;
                    pAuthIdentityEx->Password = (PWSTR) UlongToPtr( CredEx32.Password );
                    pAuthIdentityEx->PasswordLength = CredEx32.PasswordLength ;
                    pAuthIdentityEx->Flags = CredEx32.Flags ;
                    pAuthIdentityEx->PackageList = (PWSTR) UlongToPtr( CredEx32.PackageList );
                    pAuthIdentityEx->PackageListLength = CredEx32.PackageListLength ;

                }

            }
            else
            {
                Status = LsaFunctions->CopyFromClientBuffer(
                            NULL,
                            sizeof(SEC_WINNT_AUTH_IDENTITY_EXW),
                            pAuthIdentityEx,
                            AuthorizationData);

            }


            if (!NT_SUCCESS(Status))
            {
                SspPrint((SSP_CRITICAL,"SpAcquireCredentialsHandle, Error from LsaFunctions->CopyFromClientBuffer is 0x%lx\n", Status));
                goto Cleanup;
            }
            pAuthIdentity = (PSEC_WINNT_AUTH_IDENTITY) &pAuthIdentityEx->User;
            CredSize = pAuthIdentityEx->Length;
            Offset = FIELD_OFFSET(SEC_WINNT_AUTH_IDENTITY_EXW, User);
        }
        else
        {
            pAuthIdentity = (PSEC_WINNT_AUTH_IDENTITY_W) pAuthIdentityEx;

            if ( CallInfo.Attributes & SECPKG_CALL_WOWCLIENT )
            {
                pAuthIdentity->User = (PWSTR) UlongToPtr( Cred32.User );
                pAuthIdentity->UserLength = Cred32.UserLength ;
                pAuthIdentity->Domain = (PWSTR) UlongToPtr( Cred32.Domain );
                pAuthIdentity->DomainLength = Cred32.DomainLength ;
                pAuthIdentity->Password = (PWSTR) UlongToPtr( Cred32.Password );
                pAuthIdentity->PasswordLength = Cred32.PasswordLength ;
                pAuthIdentity->Flags = Cred32.Flags ;
            }
            CredSize = sizeof(SEC_WINNT_AUTH_IDENTITY_W);
        }

        if ((pAuthIdentity->Flags & SEC_WINNT_AUTH_IDENTITY_ANSI) != 0)
        {
            DoUnicode = FALSE;
            //
            // Turn off the marshalled flag because we don't support marshalling
            // with ansi.
            //

            pAuthIdentity->Flags &= ~SEC_WINNT_AUTH_IDENTITY_MARSHALLED;
        }
        else if ((pAuthIdentity->Flags & SEC_WINNT_AUTH_IDENTITY_UNICODE) == 0)
        {
            Status = SEC_E_INVALID_TOKEN;
            SspPrint((SSP_CRITICAL,"SpAcquireCredentialsHandle, Error from pAuthIdentity->Flags is 0x%lx\n", pAuthIdentity->Flags));
            goto Cleanup;
        }


        // This is the only place where we can figure out whether null
        // session was requested

        if ((pAuthIdentity->UserLength == 0) &&
            (pAuthIdentity->DomainLength == 0) &&
            (pAuthIdentity->PasswordLength == 0) &&
            (pAuthIdentity->User != NULL) &&
            (pAuthIdentity->Domain != NULL) &&
            (pAuthIdentity->Password != NULL))
        {
            NewCredentialUseFlags |= NTLM_CRED_NULLSESSION;
        }

        if(
            // UserName may include a marshalled credential > UNLEN
            pAuthIdentity->UserLength > 0xFFFC ||   // MAX_USHORT - NULL
            pAuthIdentity->PasswordLength > PWLEN ||
            pAuthIdentity->DomainLength > DNS_MAX_NAME_LENGTH ) {

            SspPrint((SSP_CRITICAL,"Supplied credentials illegal length.\n"));
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }


        //
        // Copy over the strings
        //
        if( (pAuthIdentity->Flags & SEC_WINNT_AUTH_IDENTITY_MARSHALLED) != 0 ) {
            ULONG TmpCredentialSize;
            ULONG_PTR EndOfCreds;
            ULONG_PTR TmpUser;
            ULONG_PTR TmpDomain;
            ULONG_PTR TmpPassword;


            //
            // The callers can set the length of field to n chars, but they
            // will really occupy n+1 chars (null-terminator).
            //

            TmpCredentialSize = CredSize +
                             (  pAuthIdentity->UserLength +
                                pAuthIdentity->DomainLength +
                                pAuthIdentity->PasswordLength +
                             (((pAuthIdentity->User != NULL) ? 1 : 0) +
                             ((pAuthIdentity->Domain != NULL) ? 1 : 0) +
                             ((pAuthIdentity->Password != NULL) ? 1 : 0)) ) * (ULONG) sizeof(WCHAR);

            EndOfCreds = (ULONG_PTR) AuthorizationData + TmpCredentialSize;

            //
            // Verify that all the offsets are valid and no overflow will happen
            //

            TmpUser = (ULONG_PTR) pAuthIdentity->User;

            if ((TmpUser != NULL) &&
                ( (TmpUser < (ULONG_PTR) AuthorizationData) ||
                  (TmpUser > EndOfCreds) ||
                  ((TmpUser + (pAuthIdentity->UserLength) * sizeof(WCHAR)) > EndOfCreds ) ||
                  ((TmpUser + (pAuthIdentity->UserLength * sizeof(WCHAR))) < TmpUser)))
            {
                SspPrint((SSP_CRITICAL,"Username in supplied credentials has invalid pointer or length.\n"));
                Status = STATUS_INVALID_PARAMETER;
                goto Cleanup;
            }

            TmpDomain = (ULONG_PTR) pAuthIdentity->Domain;

            if ((TmpDomain != NULL) &&
                ( (TmpDomain < (ULONG_PTR) AuthorizationData) ||
                  (TmpDomain > EndOfCreds) ||
                  ((TmpDomain + (pAuthIdentity->DomainLength) * sizeof(WCHAR)) > EndOfCreds ) ||
                  ((TmpDomain + (pAuthIdentity->DomainLength * sizeof(WCHAR))) < TmpDomain)))
            {
                SspPrint((SSP_CRITICAL,"Domainname in supplied credentials has invalid pointer or length.\n"));
                Status = STATUS_INVALID_PARAMETER;
                goto Cleanup;
            }

            TmpPassword = (ULONG_PTR) pAuthIdentity->Password;

            if ((TmpPassword != NULL) &&
                ( (TmpPassword < (ULONG_PTR) AuthorizationData) ||
                  (TmpPassword > EndOfCreds) ||
                  ((TmpPassword + (pAuthIdentity->PasswordLength) * sizeof(WCHAR)) > EndOfCreds ) ||
                  ((TmpPassword + (pAuthIdentity->PasswordLength * sizeof(WCHAR))) < TmpPassword)))
            {
                SspPrint((SSP_CRITICAL,"Password in supplied credentials has invalid pointer or length.\n"));
                Status = STATUS_INVALID_PARAMETER;
                goto Cleanup;
            }

            //
            // Allocate a chunk of memory for the credentials
            //

            TmpCredentials = (PSEC_WINNT_AUTH_IDENTITY_W) NtLmAllocate(TmpCredentialSize - Offset);
            if (TmpCredentials == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Cleanup;
            }

            //
            // Copy the credentials from the client
            //

            Status = LsaFunctions->CopyFromClientBuffer(
                        NULL,
                        TmpCredentialSize - Offset,
                        TmpCredentials,
                        (PUCHAR) AuthorizationData + Offset
                        );
            if (!NT_SUCCESS(Status))
            {
                SspPrint((SSP_CRITICAL,"Failed to copy whole auth identity\n"));
                goto Cleanup;
            }

            //
            // Now convert all the offsets to pointers.
            //

            if (TmpCredentials->User != NULL)
            {
                USHORT cbUser;

                TmpCredentials->User = (LPWSTR) RtlOffsetToPointer(
                                                TmpCredentials->User,
                                                (PUCHAR) TmpCredentials - (PUCHAR) AuthorizationData - Offset
                                                );

                ASSERT( (TmpCredentials->UserLength*sizeof(WCHAR)) <= 0xFFFF );

                cbUser = (USHORT)(TmpCredentials->UserLength * sizeof(WCHAR));
                UserName.Buffer = (PWSTR)NtLmAllocate( cbUser );

                if (UserName.Buffer == NULL ) {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    goto Cleanup;
                }

                CopyMemory( UserName.Buffer, TmpCredentials->User, cbUser );
                UserName.Length = cbUser;
                UserName.MaximumLength = cbUser;
            }

            if (TmpCredentials->Domain != NULL)
            {
                USHORT cbDomain;

                TmpCredentials->Domain = (LPWSTR) RtlOffsetToPointer(
                                                    TmpCredentials->Domain,
                                                    (PUCHAR) TmpCredentials - (PUCHAR) AuthorizationData - Offset
                                                    );

                ASSERT( (TmpCredentials->DomainLength*sizeof(WCHAR)) <= 0xFFFF );
                cbDomain = (USHORT)(TmpCredentials->DomainLength * sizeof(WCHAR));
                DomainName.Buffer = (PWSTR)NtLmAllocate( cbDomain );

                if (DomainName.Buffer == NULL ) {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    goto Cleanup;
                }

                CopyMemory( DomainName.Buffer, TmpCredentials->Domain, cbDomain );
                DomainName.Length = cbDomain;
                DomainName.MaximumLength = cbDomain;
            }

            if (TmpCredentials->Password != NULL)
            {
                USHORT cbPassword;

                TmpCredentials->Password = (LPWSTR) RtlOffsetToPointer(
                                                    TmpCredentials->Password,
                                                    (PUCHAR) TmpCredentials - (PUCHAR) AuthorizationData - Offset
                                                    );


                ASSERT( (TmpCredentials->PasswordLength*sizeof(WCHAR)) <= 0xFFFF );
                cbPassword = (USHORT)(TmpCredentials->PasswordLength * sizeof(WCHAR));
                Password.Buffer = (PWSTR)NtLmAllocate( cbPassword );

                if (Password.Buffer == NULL ) {
                    ZeroMemory( TmpCredentials->Password, cbPassword );
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    goto Cleanup;
                }

                CopyMemory( Password.Buffer, TmpCredentials->Password, cbPassword );
                Password.Length = cbPassword;
                Password.MaximumLength = cbPassword;

                ZeroMemory( TmpCredentials->Password, cbPassword );
            }

        } else {

            if (pAuthIdentity->Password != NULL)
            {
                Status = CopyClientString(
                                pAuthIdentity->Password,
                                pAuthIdentity->PasswordLength,
                                DoUnicode,
                                &Password
                                );
                if (!NT_SUCCESS(Status))
                {
                    SspPrint((SSP_CRITICAL,"SpAcquireCredentialsHandle, Error from CopyClientString is 0x%lx\n", Status));
                    goto Cleanup;
                }

            }

            if (pAuthIdentity->User != NULL)
            {
                Status = CopyClientString(
                                pAuthIdentity->User,
                                pAuthIdentity->UserLength,
                                DoUnicode,
                                &UserName
                                );
                if (!NT_SUCCESS(Status))
                {
                    SspPrint((SSP_CRITICAL, "SpAcquireCredentialsHandle, Error from CopyClientString is 0x%lx\n", Status));
                    goto Cleanup;
                }

            }

            if (pAuthIdentity->Domain != NULL)
            {
                Status = CopyClientString(
                                pAuthIdentity->Domain,
                                pAuthIdentity->DomainLength,
                                DoUnicode,
                                &DomainName
                                );
                if (!NT_SUCCESS(Status))
                {
                    SspPrint((SSP_CRITICAL, "SpAcquireCredentialsHandle, Error from CopyClientString is 0x%lx\n", Status));
                    goto Cleanup;
                }

                //
                // Make sure that the domain name length is not greater
                // than the allowed dns domain name
                //

                if (DomainName.Length > DNS_MAX_NAME_LENGTH * sizeof(WCHAR))
                {
                    SspPrint((SSP_CRITICAL, "SpAcquireCredentialsHandle: Invalid supplied domain name %wZ\n",
                        &DomainName ));
                    Status = SEC_E_UNKNOWN_CREDENTIALS;
                    goto Cleanup;
                }

            }
        }
    }   // AuthorizationData != NULL

#if 0
    //
    // Handle UPN and composite NETBIOS syntax
    //
    {
        UNICODE_STRING User = UserName;
        UNICODE_STRING Domain = DomainName;

        Status =
            NtLmParseName(
                &User,
                &Domain,
                TRUE); // If successful, this will allocate and free buffers
        if(NT_SUCCESS(Status)){

                UserName = User;
                DomainName = Domain;

        }
    }
#endif

    if( Password.Length != 0 )
    {
        UNICODE_STRING OldPassword = Password;

        Status = NtLmDuplicatePassword( &Password, &OldPassword );
        if(!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }

        ZeroMemory( OldPassword.Buffer, OldPassword.Length );
        NtLmFree( OldPassword.Buffer );
    }

    Status = SsprAcquireCredentialHandle(
                                LogonIdToUse,
                                &ClientInfo,
                                NewCredentialUseFlags,
                                CredentialHandle,
                                ExpirationTime,
                                &DomainName,
                                &UserName,
                                &Password );

    if (!NT_SUCCESS(Status))
    {
        SspPrint((SSP_CRITICAL, "SpAcquireCredentialsHandle, Error from SsprAcquireCredentialsHandle is 0x%lx\n", Status));
        goto Cleanup;
    }

//  These will be kept in the Credential structure and freed
//  when the Credential structure is freed

    if (DomainName.Buffer != NULL)
    {
        DomainName.Buffer = NULL;
    }

    if (UserName.Buffer != NULL)
    {
        UserName.Buffer = NULL;
    }

    if (Password.Buffer != NULL)
    {
        Password.Buffer = NULL;
    }

Cleanup:

    if (TmpCredentials != NULL)
    {
        NtLmFree(TmpCredentials);
    }

    if (DomainName.Buffer != NULL)
    {
        NtLmFree(DomainName.Buffer);
    }

    if (UserName.Buffer != NULL)
    {
        NtLmFree(UserName.Buffer);
    }

    if (Password.Buffer != NULL)
    {
        ZeroMemory(Password.Buffer, Password.Length);
        NtLmFree(Password.Buffer);
    }

    if (pAuthIdentityEx != NULL)
    {
        NtLmFree(pAuthIdentityEx);
    }

    SspPrint((SSP_API, "Leaving SpAcquireCredentialsHandle, Status is %d\n", Status));
    return(SspNtStatusToSecStatus(Status, SEC_E_INTERNAL_ERROR));
}


//+-------------------------------------------------------------------------
//
//  Function:   SpFreeCredentialsHandle
//
//  Synopsis:   Frees a credential created by AcquireCredentialsHandle.
//
//  Effects:    Unlinks the credential from the global list and the list
//              for this client.
//
//  Arguments:  CredentialHandle - Handle to the credential to free
//              (acquired through AcquireCredentialsHandle)
//
//  Requires:
//
//  Returns:    STATUS_SUCCESS on success,
//              SEC_E_INVALID_HANDLE if the handle is not valid
//
//  Notes:
//
//
//--------------------------------------------------------------------------


NTSTATUS NTAPI
SpFreeCredentialsHandle(
    IN ULONG_PTR CredentialHandle
    )
{
    SspPrint((SSP_API, "Entering SpFreeCredentialsHandle\n"));
    NTSTATUS Status = STATUS_SUCCESS;
    PSSP_CREDENTIAL Credential;

    //
    // Find the referenced credential and delink it.
    //

    Status = SspCredentialReferenceCredential(
                            CredentialHandle,
                            TRUE,       // remove the instance of the credential
                            &Credential );

    if ( !NT_SUCCESS( Status ) ) {
        SspPrint((SSP_CRITICAL, "SpFreeCredentialsHandle, Error from SspCredentialReferenceCredential is 0x%lx\n", Status));
        goto Cleanup;
    }

    //
    // Dereferencing the Credential will remove the client's reference
    // to it, causing it to be rundown if nobody else is using it.
    //

    SspCredentialDereferenceCredential( Credential );

Cleanup:

    //
    // Catch spurious INVALID_HANDLE being returned to RPC.
    //

    ASSERT( NT_SUCCESS(Status) );

    SspPrint((SSP_API, "Leaving SpFreeCredentialsHandle\n"));
    return(SspNtStatusToSecStatus(Status, SEC_E_INTERNAL_ERROR));
}


NTSTATUS
NTAPI
SpQueryCredentialsAttributes(
    IN LSA_SEC_HANDLE CredentialHandle,
    IN ULONG CredentialAttribute,
    IN OUT PVOID Buffer
    )
{
    PSSP_CREDENTIAL pCredential = NULL;
    SecPkgCredentials_NamesW Names;
    LUID LogonId;
    BOOLEAN bActiveLogonsAreLocked = FALSE;

    LPWSTR pszContextNames = NULL;
    LPWSTR pWhere;

    LPWSTR pszUserName = NULL;
    LPWSTR pszDomainName = NULL;
    DWORD cchUserName = 0;
    DWORD cchDomainName = 0;
    ULONG Length;

    BOOLEAN CalledLsaLookup = FALSE;
    PLSAPR_REFERENCED_DOMAIN_LIST ReferencedDomain = NULL;
    LSAPR_TRANSLATED_NAMES ReferencedUser;

#if _WIN64
    SECPKG_CALL_INFO CallInfo;
#endif

    NTSTATUS Status = STATUS_SUCCESS;

    SspPrint((SSP_API,"In SpQueryCredentialsAttributes\n"));


    Names.sUserName = NULL;

    if (CredentialAttribute != SECPKG_CRED_ATTR_NAMES)
    {
        SspPrint((SSP_MISC, "Asked for illegal info level in QueryCredAttr: %d\n",
                CredentialAttribute));
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    Status = SspCredentialReferenceCredential(
                            CredentialHandle,
                            FALSE,
                            &pCredential );

    if ( !NT_SUCCESS( Status ) ) {
        SspPrint((SSP_CRITICAL, "SpQueryCredentialsAttributes, Error from SspCredentialReferenceCredential is 0x%lx\n", Status));
        goto Cleanup;
    }

#if _WIN64
    if(!LsaFunctions->GetCallInfo( &CallInfo ))
    {
        Status = STATUS_INTERNAL_ERROR;
        SspPrint((SSP_CRITICAL, "SpQueryCredentialsAttributes, failed to get callinfo 0x%lx\n", Status));
        goto Cleanup;
    }
#endif



    //
    // The logon id of the credential is constant, so it is o.k.
    // to use it without locking the credential
    //

    LogonId = pCredential->LogonId;


    //
    // credentials were either specified when cred built, or, they were defaulted.
    //

    if ( pCredential->UserName.Buffer == NULL &&
        pCredential->DomainName.Buffer == NULL )
    {
        PACTIVE_LOGON pActiveLogon = NULL;

        //
        // defaulted creds: pickup info from active logon table.
        //

        NlpLockActiveLogonsRead();
        bActiveLogonsAreLocked = TRUE;

        pActiveLogon = NlpFindActiveLogon(&LogonId);

        if (pActiveLogon)
        {
            //
            // found an active logon entry.
            //

            pszUserName = pActiveLogon->UserName.Buffer;
            cchUserName = pActiveLogon->UserName.Length / sizeof(WCHAR);
            pszDomainName = pActiveLogon->LogonDomainName.Buffer;
            cchDomainName = pActiveLogon->LogonDomainName.Length / sizeof(WCHAR);
        }
        else
        {
            PTOKEN_USER pTokenInfo;
            BYTE FastBuffer[ 256 ];
            DWORD cbTokenInfo;
            HANDLE ClientTokenHandle;

            BOOL fSuccess = FALSE;

            NlpUnlockActiveLogons();
            bActiveLogonsAreLocked = FALSE;


            //
            // get a token associated with the logon session.
            //

            Status = LsaFunctions->OpenTokenByLogonId(
                                        &LogonId,
                                        &ClientTokenHandle
                                        );

            if (!NT_SUCCESS(Status))
            {
                SspPrint(( SSP_CRITICAL,
                          "SpQueryCredentialsAttributes: "
                          "Could not open client token 0x%lx\n",
                          Status ));
                goto Cleanup;
            }

            //
            // get Sid associated with credential.
            //

            cbTokenInfo = sizeof(FastBuffer);
            pTokenInfo = (PTOKEN_USER)FastBuffer;


            fSuccess = GetTokenInformation(
                            ClientTokenHandle,
                            TokenUser,
                            pTokenInfo,
                            cbTokenInfo,
                            &cbTokenInfo
                            );


            CloseHandle( ClientTokenHandle );

            if ( fSuccess )
            {
                LSAPR_SID_ENUM_BUFFER SidEnumBuffer;
                LSAPR_SID_INFORMATION SidInfo;
                ULONG MappedCount;

                SidEnumBuffer.Entries = 1;
                SidEnumBuffer.SidInfo = &SidInfo;

                SidInfo.Sid = (LSAPR_SID*)pTokenInfo->User.Sid;

                ZeroMemory( &ReferencedUser, sizeof(ReferencedUser) );
                CalledLsaLookup = TRUE;

                Status = LsarLookupSids(
                            NtLmGlobalPolicyHandle,
                            &SidEnumBuffer,
                            &ReferencedDomain,
                            &ReferencedUser,
                            LsapLookupWksta,
                            &MappedCount
                            );

                if ( !NT_SUCCESS( Status ) ||
                    (MappedCount == 0) ||
                    (ReferencedUser.Entries == 0) ||
                    (ReferencedDomain == NULL) ||
                    (ReferencedDomain->Entries == 0)
                    )
                {
                    fSuccess = FALSE;
                }
                else
                {
                    LONG Index = ReferencedUser.Names->DomainIndex;

                    pszUserName = ReferencedUser.Names->Name.Buffer;
                    cchUserName = ReferencedUser.Names->Name.Length / sizeof(WCHAR);

                    pszDomainName = ReferencedDomain->Domains[Index].Name.Buffer;
                    cchDomainName = ReferencedDomain->Domains[Index].Name.Length / sizeof(WCHAR);
                }
            }

            if ( !fSuccess )
            {
                Status = STATUS_NO_SUCH_LOGON_SESSION;
                SspPrint(( SSP_CRITICAL, "SpQueryCredentialsAtributes, NlpFindActiveLogon returns FALSE\n"));
                goto Cleanup;

            }
        }

    }
    else
    {

        //
        // specified creds.
        //

        pszUserName = pCredential->UserName.Buffer;
        cchUserName = pCredential->UserName.Length / sizeof(WCHAR);
        pszDomainName = pCredential->DomainName.Buffer;
        cchDomainName = pCredential->DomainName.Length / sizeof(WCHAR);
    }

    Length = (cchUserName + 1 + cchDomainName + 1) * sizeof(WCHAR);

    pszContextNames = (LPWSTR)NtLmAllocate( Length );
    if ( pszContextNames == NULL )
    {
        goto Cleanup;
    }

    pWhere = pszContextNames;

    if ( pszDomainName)
    {
        RtlCopyMemory( pszContextNames, pszDomainName, cchDomainName * sizeof(WCHAR) );
        pszContextNames[ cchDomainName ] = L'\\';
        pWhere += (cchDomainName+1);
    }


    if ( pszUserName )
    {
        RtlCopyMemory( pWhere, pszUserName, cchUserName * sizeof(WCHAR) );
    }

    pWhere[ cchUserName ] = L'\0';

    if (bActiveLogonsAreLocked)
    {
        NlpUnlockActiveLogons();
        bActiveLogonsAreLocked = FALSE;
    }


    //
    // Allocate memory in the client's address space
    //

    Status = LsaFunctions->AllocateClientBuffer(
                NULL,
                Length,
                (PVOID *) &Names.sUserName
                );
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    //
    // Copy the string there
    //

    Status = LsaFunctions->CopyToClientBuffer(
                NULL,
                Length,
                Names.sUserName,
                pszContextNames
                );
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    //
    // Now copy the address of the string there
    //

#if _WIN64

    if ( CallInfo.Attributes & SECPKG_CALL_WOWCLIENT )
    {
        Status = LsaFunctions->CopyToClientBuffer(
                    NULL,
                    sizeof(ULONG),
                    Buffer,
                    &Names
                    );
    }
    else
    {
        Status = LsaFunctions->CopyToClientBuffer(
                    NULL,
                    sizeof(Names),
                    Buffer,
                    &Names
                    );
    }

#else

    Status = LsaFunctions->CopyToClientBuffer(
                NULL,
                sizeof(Names),
                Buffer,
                &Names
                );
#endif

    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

Cleanup:

    if (bActiveLogonsAreLocked)
    {
        NlpUnlockActiveLogons();
    }

    if ( pCredential != NULL )
    {
        SspCredentialDereferenceCredential( pCredential );
    }

    if ( CalledLsaLookup )
    {
        if ( ReferencedDomain )
        {
            LsaIFree_LSAPR_REFERENCED_DOMAIN_LIST( ReferencedDomain );
        }

        LsaIFree_LSAPR_TRANSLATED_NAMES( &ReferencedUser );
    }

    if (!NT_SUCCESS(Status))
    {
        if (Names.sUserName != NULL)
        {
            (VOID) LsaFunctions->FreeClientBuffer(
                        NULL,
                        Names.sUserName
                        );
        }
    }

    if ( pszContextNames )
    {
        NtLmFree( pszContextNames );
    }

    SspPrint((SSP_API, "Leaving SpQueryCredentialsAttributes\n"));

    return (SspNtStatusToSecStatus(Status, SEC_E_INTERNAL_ERROR));
}


NTSTATUS NTAPI
SpSaveCredentials(
    IN ULONG_PTR CredentialHandle,
    IN PSecBuffer Credentials
    )
{
    UNREFERENCED_PARAMETER(CredentialHandle);
    UNREFERENCED_PARAMETER(Credentials);
    SspPrint((SSP_API,"In SpSaveCredentials\n"));
    return(SEC_E_UNSUPPORTED_FUNCTION);
}


NTSTATUS NTAPI
SpGetCredentials(
    IN ULONG_PTR CredentialHandle,
    IN OUT PSecBuffer Credentials
    )
{
    UNREFERENCED_PARAMETER(CredentialHandle);
    UNREFERENCED_PARAMETER(Credentials);
    SspPrint((SSP_API,"In SpGetCredentials\n"));
    return(SEC_E_UNSUPPORTED_FUNCTION);
}


NTSTATUS NTAPI
SpDeleteCredentials(
    IN ULONG_PTR CredentialHandle,
    IN PSecBuffer Key
    )
{
    UNREFERENCED_PARAMETER(CredentialHandle);
    UNREFERENCED_PARAMETER(Key);
    SspPrint((SSP_API,"In SpDeleteCredentials\n"));
    return(SEC_E_UNSUPPORTED_FUNCTION);
}
