/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    adtinit.c

Abstract:

    Local Security Authority - Auditing Initialization

Author:

    Scott Birrell       (ScottBi)      November 20, 1991

Environment:

Revision History:

--*/

#include <lsapch2.h>
#include "adtp.h"

NTSTATUS LsapAdtInitializeCrashOnFail( VOID );

//
// Array of drive letter to device mappings for generating path strings.
//

DRIVE_MAPPING DriveMappingArray[MAX_DRIVE_MAPPING];

//
// Name that will be used as the default subsystem name for LSA generated events
//

UNICODE_STRING LsapSubsystemName;


//
// Name that will be passed in for SubsystemName for some audits generated
// by LSA for LSA objects.
//

UNICODE_STRING LsapLsaName;


//
// Special privilege values which are not normally audited,
// but generate audits when assigned to a user.  See
// LsapAdtAuditSpecialPrivileges.
//

LUID ChangeNotifyPrivilege;
LUID AuditPrivilege;
LUID CreateTokenPrivilege;
LUID AssignPrimaryTokenPrivilege;
LUID BackupPrivilege;
LUID RestorePrivilege;
LUID DebugPrivilege;


//
// Global variable indicating whether or not we are supposed
// to crash when an audit fails.
//

BOOLEAN LsapCrashOnAuditFail = FALSE;
BOOLEAN LsapAllowAdminLogonsOnly = FALSE;



NTSTATUS
LsapAdtInitialize(
    )

/*++

Routine Description:

    This function performs initialization of auditing within the LSA, and
    it also issues commands to the Reference Monitor to enable it to
    complete any initialization of auditing variables that is dependent
    on the content of the LSA Database.  At time of call, the main
    System Init thread is in the Reference Monitor awaiting completion
    of all LSA initialization, and the Reference Monitor Command
    Server thread is waiting for commands.

    The following steps are performed:

    o Read the Audit Event and Audit Log information from the LSA
      Database.
    o Call the Event Logging function to open the Audit Log
    o Issue a Reference Monitor command to write the Audit Event Info
      to the Reference-Monitor's in-memory database.

Arguments:

    None

Return Value:

    NTSTATUS - Standard Nt Result Code.

        All Result Codes are generated by called routines.
--*/

{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG AuditLogInfoLength = sizeof (POLICY_AUDIT_LOG_INFO);
    ULONG AuditEventInfoLength = sizeof (LSARM_POLICY_AUDIT_EVENTS_INFO);
    UNICODE_STRING UnicodeString;
    PUNICODE_STRING Strings;
    LSARM_POLICY_AUDIT_EVENTS_INFO AuditEventsInfo;

    Strings = &UnicodeString;

    RtlInitUnicodeString( Strings, L"System Restart");

    RtlInitUnicodeString( &LsapSubsystemName, L"Security" );

    RtlInitUnicodeString( &LsapLsaName, L"LSA" );

    //
    // initialize debug helper support. this function
    // does nothing for free builds
    //

    LsapAdtInitDebug();
    
    //
    // init LsapCrashOnAuditFail global var so that we can crash
    // if any of the following initialization fails
    //

    (VOID) LsapAdtInitializeCrashOnFail();

    Status = LsapAdtInitGenericAudits();

    if (!NT_SUCCESS(Status)) {

        goto AuditInitError;
    }
    
    Status = LsapAdtInitializeExtensibleAuditing();

    if (!NT_SUCCESS(Status)) {

        goto AuditInitError;
    }
    
    Status = LsapAdtInitializeLogQueue();

    if (!NT_SUCCESS(Status)) {

        goto AuditInitError;
    }

    Status = LsapAdtInitializePerUserAuditing();

    if (!NT_SUCCESS(Status)) {

        LsapLogError("LsapAdtInitialize: LsapAdtInitializePerUserAuditing() returned 0x%lx\n", 
            Status);
        goto AuditInitError;
    }

    //
    // Read the Audit Log Information from the PolAdtLg attribute of the Lsa
    // Database object.
    //

    Status = LsapDbReadAttributeObject(
                 LsapDbHandle,
                 &LsapDbNames[PolAdtLg],
                 &LsapAdtLogInformation,
                 &AuditLogInfoLength
                 );

    if (!NT_SUCCESS(Status)) {

        LsapLogError(
            "LsapAdtInitialize: Read Audit Log Info returned 0x%lx\n",
            Status
            );

        goto AuditInitError;
    }


    //
    // Read the Audit Event Information from the AdtEvent attribute of the Lsa
    // Database object.  The information consists of the Auditing Mode and
    // the Auditing Options for each Audit Event Type.
    //

    Status = LsapDbReadAttributeObject(
                 LsapDbHandle,
                 &LsapDbNames[PolAdtEv],
                 &AuditEventsInfo,
                 &AuditEventInfoLength
                 );

    if (!NT_SUCCESS(Status)) {

        //
        // This section of code is temporary and allows an old
        // Policy Database to work with the new Audit Event Categories
        // without the need to re-install.  The Audit Event Information
        // is overwritten with the new format and all auditing is turned
        // off.
        //

        if (Status == STATUS_BUFFER_OVERFLOW) {

            KdPrint(("LsapAdtInitialize: Old Audit Event Info detected\n"
                    "Replacing with new format, all auditing disabled\n"));

            //
            // Initialize Default Event Auditing Options.  No auditing is specified
            // for any event type.
            //

            Status = LsapAdtInitializeDefaultAuditing(
                         LSAP_DB_UPDATE_POLICY_DATABASE,
                         &AuditEventsInfo
                         );

            if (!NT_SUCCESS(Status)) {

                goto AuditInitError;
            }

        } else {

            LsapLogError(
                "LsapAdtInitialize: Read Audit Event Info returned 0x%lx\n",
                Status
                );
            goto AuditInitError;
        }
    }

    //
    // update the LSA global var that holds audit policy
    //

    RtlCopyMemory(
        &LsapAdtEventsInformation,
        &AuditEventsInfo,
        sizeof(LSARM_POLICY_AUDIT_EVENTS_INFO)
        );
        
    //
    // generate SE_AUDITID_SYSTEM_RESTART
    //

    LsapAdtSystemRestart( &AuditEventsInfo );

    //
    // Send a command to the Reference Monitor to write the Auditing
    // State to its in-memory data.
    //

    Status = LsapCallRm(
                 RmAuditSetCommand,
                 &AuditEventsInfo,
                 sizeof (LSARM_POLICY_AUDIT_EVENTS_INFO),
                 NULL,
                 0
                 );

    if (!NT_SUCCESS(Status)) {

        LsapLogError("LsapAdtInitialize: LsapCallRm returned 0x%lx\n", Status);
        goto AuditInitError;
    }

    Status = LsapAdtInitializeDriveLetters();

    if (!NT_SUCCESS(Status)) {

        LsapLogError("LsapAdtInitialize: LsapAdtInitializeDriveLetters() returned 0x%lx\n", 
            Status);
        goto AuditInitError;
    }

    //
    // Initialize privilege values we need
    //

    ChangeNotifyPrivilege       = RtlConvertLongToLuid( SE_CHANGE_NOTIFY_PRIVILEGE      );
    AuditPrivilege              = RtlConvertLongToLuid( SE_AUDIT_PRIVILEGE              );
    CreateTokenPrivilege        = RtlConvertLongToLuid( SE_CREATE_TOKEN_PRIVILEGE       );
    AssignPrimaryTokenPrivilege = RtlConvertLongToLuid( SE_ASSIGNPRIMARYTOKEN_PRIVILEGE );
    BackupPrivilege             = RtlConvertLongToLuid( SE_BACKUP_PRIVILEGE             );
    RestorePrivilege            = RtlConvertLongToLuid( SE_RESTORE_PRIVILEGE            );
    DebugPrivilege              = RtlConvertLongToLuid( SE_DEBUG_PRIVILEGE              );


AuditInitFinish:

    return(Status);

AuditInitError:

    //
    // raise harderror if LsapCrashOnAuditFail is TRUE
    //

    LsapAuditFailed( Status );

    goto AuditInitFinish;
}


NTSTATUS
LsapAdtInitializeDefaultAuditing(
    IN ULONG Options,
    OUT PLSARM_POLICY_AUDIT_EVENTS_INFO AuditEventsInformation
    )

/*++

Routine Description:

    This routine sets an initial default Auditing State in which auditing
    is turned off.  It is called only during initialization of the LSA
    or during the installation of its Policy Database.  The initial
    auditing state may also optionally be written to the Lsa Policy
    Database provided that the Policy Object has been created and its
    internal handle is available.

Arguments:

    Options - Specifies optional actions to be taken

        LSAP_DB_UPDATE_POLICY_DATABASE - Update the corresponding information
            in the Policy Database.  This option must only be specified
            where it is known that the Policy Object exists.

    AuditEventsInformation - Pointer to structure that will receive the Audit Event
        Information

Return Values:

    None.

--*/

{
    NTSTATUS Status = STATUS_SUCCESS;
    LSAP_DB_ATTRIBUTE AuditEventsAttribute;
    BOOLEAN ObjectReferenced = FALSE;

    ULONG EventAuditingOptionsLength =
        (POLICY_AUDIT_EVENT_TYPE_COUNT * sizeof(POLICY_AUDIT_EVENT_OPTIONS));

    //
    // Turn off auditing and set the count of Audit Event Types (Categories)
    //

    AuditEventsInformation->AuditingMode = FALSE;
    AuditEventsInformation->MaximumAuditEventCount = POLICY_AUDIT_EVENT_TYPE_COUNT;

    //
    // Turn off auditing for all events.
    //

    RtlZeroMemory(AuditEventsInformation->EventAuditingOptions, EventAuditingOptionsLength);


    if (Options & LSAP_DB_UPDATE_POLICY_DATABASE) {

        ASSERT(LsapPolicyHandle != NULL);

        //
        // Start a transaction on the Policy Object
        //

        Status = LsapDbReferenceObject(
                     LsapPolicyHandle,
                     (ACCESS_MASK) 0,
                     PolicyObject,
                     PolicyObject,
                     LSAP_DB_LOCK | LSAP_DB_START_TRANSACTION
                     );

        if (!NT_SUCCESS(Status)) {

            goto InitializeDefaultAuditingError;
        }

        ObjectReferenced = TRUE;

        LsapDbInitializeAttribute(
            &AuditEventsAttribute,
            &LsapDbNames[PolAdtEv],
            AuditEventsInformation,
            sizeof (LSARM_POLICY_AUDIT_EVENTS_INFO),
            FALSE
            );

        Status = LsapDbWriteAttributesObject(
                     LsapPolicyHandle,
                     &AuditEventsAttribute,
                     (ULONG) 1
                     );

        if (!NT_SUCCESS(Status)) {

            goto InitializeDefaultAuditingError;
        }
    }

InitializeDefaultAuditingFinish:

    if (ObjectReferenced) {

        Status = LsapDbDereferenceObject(
                     LsapPolicyHandle,
                     PolicyObject,
                     PolicyObject,
                     LSAP_DB_LOCK | LSAP_DB_FINISH_TRANSACTION,
                     (SECURITY_DB_DELTA_TYPE) 0,
                     Status
                     );

        ObjectReferenced = FALSE;
    }

    return(Status);

InitializeDefaultAuditingError:

    goto InitializeDefaultAuditingFinish;
}


NTSTATUS
LsapAdtInitializePerUserAuditing(
    VOID
    )
/*++

Routine Description:

    Initializes the per user auditing hash table and reads data from the
    registry.

Arguments:

    None.

Return Value:

    NTSTATUS 

--*/
{
    NTSTATUS    Status = STATUS_SUCCESS;
    BOOLEAN     bSuccess;
    BOOLEAN     bLock  = FALSE;
    PVOID       pNotificationItem = NULL;

    RtlInitializeResource(&LsapAdtPerUserPolicyTableResource);
    RtlInitializeResource(&LsapAdtPerUserLuidTableResource);

    LsapAdtPerUserKeyEvent = CreateEvent(
                                 NULL,
                                 FALSE,
                                 FALSE,
                                 NULL
                                 );

    ASSERT(LsapAdtPerUserKeyEvent);
    
    if (!LsapAdtPerUserKeyEvent)
    {
        Status = LsapWinerrorToNtStatus(GetLastError());
        goto Cleanup;
    }             

    LsapAdtPerUserKeyTimer = CreateWaitableTimer(
                                 NULL,
                                 FALSE,
                                 NULL
                                 );

    ASSERT(LsapAdtPerUserKeyTimer);
    
    if (!LsapAdtPerUserKeyTimer)
    {
        Status = LsapWinerrorToNtStatus(GetLastError());
        goto Cleanup;
    }             

    bSuccess = LsapAdtAcquirePerUserPolicyTableWriteLock();
    ASSERT(bSuccess);
    
    if (!bSuccess) 
    {
        Status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }

    bLock  = TRUE;
    Status = LsapAdtConstructTablePerUserAuditing();

    if (!NT_SUCCESS(Status))
    {
        ASSERT(L"Failed to construct per user auditing table." && FALSE);
        goto Cleanup;
    }

    //
    // Now register for changes to the key, so that we can rebuild the
    // table to reflect current policy.  The event is signalled by
    // registry change.  The timer is set by the NotifyStub routine.
    //

    pNotificationItem =
        LsaIRegisterNotification( 
            (LPTHREAD_START_ROUTINE)LsapAdtKeyNotifyStubPerUserAuditing,
            0,
            NOTIFIER_TYPE_HANDLE_WAIT,
            0,
            0,
            0,
            LsapAdtPerUserKeyEvent
            );

    if ( !pNotificationItem )
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    pNotificationItem = 
        LsaIRegisterNotification( 
            (LPTHREAD_START_ROUTINE)LsapAdtKeyNotifyFirePerUserAuditing,
            0,
            NOTIFIER_TYPE_HANDLE_WAIT,
            0,
            0,
            0,
            LsapAdtPerUserKeyTimer 
            );

    if ( !pNotificationItem )
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

Cleanup:

    if (bLock)
    {
        LsapAdtReleasePerUserPolicyTableLock();
    }

    if (!NT_SUCCESS(Status))
    {
        LsapAuditFailed(Status);
    }

    return Status;
}


NTSTATUS
LsapAdtInitializeDriveLetters(
    VOID
    )
/*++

Routine Description:

    Initializes an array of symbolic link to drive letter mappings
    for use by auditing code.


Arguments:

    None.

Return Value:

    NTSTATUS - currently either STATUS_SUCCESS or STATUS_NO_MEMORY.

--*/
{
    UNICODE_STRING LinkName;
    PUNICODE_STRING DeviceName;
    OBJECT_ATTRIBUTES Obja;
    HANDLE LinkHandle;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG i;
    PWCHAR p;
    PWCHAR DeviceNameBuffer;
    ULONG MappingIndex = 0;

    WCHAR wszDosDevices[sizeof(L"\\DosDevices\\A:") + 1];

    wcscpy(wszDosDevices, L"\\DosDevices\\A:");

    RtlInitUnicodeString(&LinkName, wszDosDevices);

    p = (PWCHAR)LinkName.Buffer;

    //
    // Make p point to the drive letter in the LinkName string
    //

    p = p+12;

    for( i=0 ; i<26 ; i++ ){

        *p = (WCHAR)'A' + (WCHAR)i;

        InitializeObjectAttributes(
            &Obja,
            &LinkName,
            OBJ_CASE_INSENSITIVE,
            NULL,
            NULL
            );

        Status = NtOpenSymbolicLinkObject(
                    &LinkHandle,
                    SYMBOLIC_LINK_QUERY,
                    &Obja
                    );
        
        if (NT_SUCCESS( Status )) {

            //
            // Open succeeded, Now get the link value
            //

            DriveMappingArray[MappingIndex].DriveLetter = *p;
            DeviceName = &DriveMappingArray[MappingIndex].DeviceName;

            DeviceNameBuffer = LsapAllocateLsaHeap( MAXIMUM_FILENAME_LENGTH );

            //
            // if LsapAllocateLsaHeap can't get any memory then return
            //

            if (DeviceNameBuffer != NULL) {

                DeviceName->Length = 0;
                DeviceName->MaximumLength = MAXIMUM_FILENAME_LENGTH;
                DeviceName->Buffer = DeviceNameBuffer;

                Status = NtQuerySymbolicLinkObject(
                            LinkHandle,
                            DeviceName,
                            NULL
                            );

                NtClose(LinkHandle);
    
                if ( NT_SUCCESS(Status) ) {
    
                    MappingIndex++;
    
                } else {

                    LsapFreeLsaHeap( DeviceNameBuffer );
                    RtlInitUnicodeString( DeviceName, NULL );

                }

            } else {

                Status = STATUS_NO_MEMORY;
                break; // since couldn't alloc mem, get out of the for loop and return

            }
        }
    }

    //
    // Now we know all drive letters that map to devices.  However, at this point, 
    // some of the 'devices' that the letters link to may themselves be symbolic 
    // links (ie nested symbolic links).  We must check for this and find the actual
    // underlying object.  Only perform the nested symlink search if we did not run
    // out of memory above.
    //

    if (Status != STATUS_NO_MEMORY) {
        
        BOOLEAN bLinkIsNested = FALSE;
        NTSTATUS NestedSearchStatus = STATUS_SUCCESS;

        i = 0;

        while (i < MappingIndex) {
            
            bLinkIsNested = FALSE;
            DeviceName = &DriveMappingArray[i].DeviceName;

            InitializeObjectAttributes(
                &Obja,
                DeviceName,
                OBJ_CASE_INSENSITIVE,
                NULL,
                NULL
                );

            NestedSearchStatus = NtOpenSymbolicLinkObject(
                                     &LinkHandle,
                                     SYMBOLIC_LINK_QUERY,
                                     &Obja
                                     );

            if (NT_SUCCESS( NestedSearchStatus )) {
                
                //
                // The open succeeded, so DeviceName was actually a nested symbolic link.
                //

                NestedSearchStatus = NtQuerySymbolicLinkObject(
                                         LinkHandle,
                                         DeviceName,
                                         NULL
                                         );

                if (NT_SUCCESS( NestedSearchStatus )) {
                
                    bLinkIsNested = TRUE;

                } else {
                 
                    //
                    // If the query fails, then free the buffer and move on.
                    //

                    LsapFreeLsaHeap( DeviceName->Buffer );
                    RtlInitUnicodeString( DeviceName, NULL );
                
                }

                NtClose(LinkHandle);

            } else if (NestedSearchStatus == STATUS_OBJECT_TYPE_MISMATCH) {
                
                //
                // NtOpenSymbolicLinkObject failed with object type mismatch.  Good.  We
                // have reached the actual device in the nested links.
                //
            
            } else {
#if DBG
                DbgPrint("NtQuerySymbolicLinkObject on handle 0x%x returned 0x%x\n", LinkHandle, NestedSearchStatus);
#endif            
                ASSERT("NtQuerySymbolicLinkObject failed with unexpected status." && FALSE);
            }

            if (!bLinkIsNested) {

                //
                // Move on to the next drive letter.
                //

                i++;

            }

        }
    }

    // 
    // one of two values should be return. STATUS_NO_MEMORY should be returned
    // if LsapAllocateLsaHeap() fails; STATUS_SUCCESS returns in all other
    // cases.  this test must be made because Status may contain a different
    // value after the return of NtOpenSymbolicLinkObject() or
    // NtQuerySymbolicLinkObject().  if either of those functions fail,
    // LsapAdtInitializeDriveLetters() should still return STATUS_SUCCESS.
    //

    if (Status == STATUS_NO_MEMORY) {

        return Status;

    }

    return STATUS_SUCCESS;
}


NTSTATUS
LsapAdtInitializeCrashOnFail(
    VOID
    )

/*++

Routine Description:

    Reads the registry to see if the user has told us to crash if an audit fails.

Arguments:

    None.

Return Value:

    STATUS_SUCCESS

--*/

{
    HANDLE KeyHandle;
    NTSTATUS Status;
    NTSTATUS TmpStatus;
    OBJECT_ATTRIBUTES Obja;
    ULONG ResultLength;
    UNICODE_STRING KeyName;
    UNICODE_STRING ValueName;
    CHAR KeyInfo[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(BOOLEAN)];
    PKEY_VALUE_PARTIAL_INFORMATION pKeyInfo;

    //
    // Check the value of the CrashOnAudit key.
    //

    RtlInitUnicodeString( &KeyName, L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Lsa");

    InitializeObjectAttributes( &Obja,
                                &KeyName,
                                OBJ_CASE_INSENSITIVE,
                                NULL,
                                NULL
                                );

    Status = NtOpenKey(
                 &KeyHandle,
                 KEY_QUERY_VALUE | KEY_SET_VALUE,
                 &Obja
                 );


    if (Status == STATUS_OBJECT_NAME_NOT_FOUND) {
        LsapCrashOnAuditFail = FALSE;
        return( STATUS_SUCCESS );
    }

    RtlInitUnicodeString( &ValueName, CRASH_ON_AUDIT_FAIL_VALUE );

    Status = NtQueryValueKey(
                 KeyHandle,
                 &ValueName,
                 KeyValuePartialInformation,
                 KeyInfo,
                 sizeof(KeyInfo),
                 &ResultLength
                 );

    TmpStatus = NtClose(KeyHandle);
    ASSERT(NT_SUCCESS(TmpStatus));

    //
    // If it's not found, don't enable CrashOnFail.
    //

    if (!NT_SUCCESS( Status )) {

        LsapCrashOnAuditFail = FALSE;

    } else {

        //
        // Check the value of the CrashOnFail value. If it is 1, we
        // crash on audit fail. If it is two, we only allow admins to
        // logon.
        //

        pKeyInfo = (PKEY_VALUE_PARTIAL_INFORMATION)KeyInfo;
        if (*(pKeyInfo->Data) == LSAP_CRASH_ON_AUDIT_FAIL) {
            LsapCrashOnAuditFail = TRUE;
        } else if (*(pKeyInfo->Data) == LSAP_ALLOW_ADIMIN_LOGONS_ONLY) {
            LsapAllowAdminLogonsOnly = TRUE;
        }

    }

    return( STATUS_SUCCESS );
}
