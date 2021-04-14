/*++

Copyright (c) 1998  Microsoft Corporation

Module Name:

    table.c

Abstract:

    Subject table routines

// The following helper routines are used with the link list package to create
// a hierarchy of lists to keep track of subjects strings

This lookaside list represents our recent history of messages that were sent.  We
want the list large enough to hold all the messages sent during a retry interval.
That way if we come to the next interval and we find that we have already sent
this subject before, we will tag it with the guid of the original message and
the mail system can filter it out.
How many is enough?
(number ncs) x (2 types [req/resp]) x (4 flags variations)
64 x 2 x 4 = 512
The only drawback with making the list very large is that the only way it is cleared
is when new items push older items out the end.  So when things are not backed up
you end up searching this long history each time you send.

Author:

    Will Lees (wlees) 10-May-1999

Environment:

    optional-environment-info (e.g. kernel mode only...)

Notes:

    optional-notes

Revision History:

    most-recent-revision-date email-name
        description
        .
        .
    least-recent-revision-date email-name
        description

--*/

#include <ntdspch.h>

#include <ismapi.h>
#include <debug.h>

// Logging headers.
// TODO: better place to put these?
typedef ULONG MessageId;
typedef ULONG ATTRTYP;
#include "dsevent.h"                    /* header Audit\Alert logging */
#include "mdcodes.h"                    /* header for error codes */

#include <ntrtl.h>                      // Generic table package

#include <fileno.h>
#define  FILENO FILENO_ISMSERV_XMITRECV

#include "common.h"
#include "ismsmtp.h"

#define DEBSUB "SMTPTAB:"

#define MAXIMUM_SEND_SUBJECT_ENTRIES 512

/* External */

/* Static */

/* Forward */ /* Generated by Emacs 19.34.1 on Tue May 11 14:36:11 1999 */

PVOID NTAPI
tableAllocate(
    struct _RTL_GENERIC_TABLE *Table,
    CLONG ByteSize
    );

VOID NTAPI
tableFree(
    struct _RTL_GENERIC_TABLE *Table,
    PVOID Buffer
    );

PSUBJECT_INSTANCE
subjectEntryCreate(
    LPCWSTR Name,
    LPDWORD pdwInstanceSize
    );

VOID
subjectEntryFree(
    PSUBJECT_INSTANCE pSubject
    );

PSUBJECT_INSTANCE
LookupInsertSubjectEntry(
    IN PTARGET_INSTANCE pTarget,
    IN LPCWSTR pszMessageSubject
    );

RTL_GENERIC_COMPARE_RESULTS NTAPI
subjectTableCompare(
    struct _RTL_GENERIC_TABLE *Table,
    PVOID FirstStruct,
    PVOID SecondStruct
    );

VOID
subjectTableDestroy(
    RTL_GENERIC_TABLE *pSubjectTable
    );

PTARGET_INSTANCE
targetEntryCreate(
    LPCWSTR Name,
    LPDWORD pdwInstanceSize
    );

VOID
targetEntryFree(
    PTARGET_INSTANCE pTarget
    );

PTARGET_INSTANCE
LookupInsertTargetEntry(
    RTL_GENERIC_TABLE *pTargetTable,
    IN  LPCWSTR pszRemoteTransportAddress
    );

RTL_GENERIC_COMPARE_RESULTS NTAPI
targetTableCompare(
    struct _RTL_GENERIC_TABLE *Table,
    PVOID FirstStruct,
    PVOID SecondStruct
    );

VOID
targetTableDestroy(
    RTL_GENERIC_TABLE *pTargetTable
    );

DWORD __cdecl
serviceConstruct(
    PLIST_ENTRY_INSTANCE pListEntry
    );

DWORD __cdecl
SmtpServiceDestruct(
    PLIST_ENTRY_INSTANCE pListEntry
    );

DWORD
SmtpTableFindSendSubject(
    IN  TRANSPORT_INSTANCE *  pTransport,
    IN  LPCWSTR               pszRemoteTransportAddress,
    IN  LPCWSTR               pszServiceName,
    IN  LPCWSTR               pszMessageSubject,
    OUT PSUBJECT_INSTANCE  *  ppSubject
    );

RTL_GENERIC_COMPARE_RESULTS NTAPI
guidTableCompare(
    struct _RTL_GENERIC_TABLE *Table,
    PVOID FirstStruct,
    PVOID SecondStruct
    );

PGUID_TABLE 
SmtpCreateGuidTable(
    VOID
    );

VOID
SmtpDestroyGuidTable(
    PGUID_TABLE pGuidTable
    );

BOOL
SmtpGuidPresentInTable(
    PGUID_TABLE pGuidTable,
    GUID *pGuid
    );

BOOL
SmtpGuidInsertInTable(
    PGUID_TABLE pGuidTable,
    GUID *pGuid
    );

/* End Forward */











PVOID NTAPI
tableAllocate(
    struct _RTL_GENERIC_TABLE *Table,
    CLONG ByteSize
    )

/*++

Routine Description:

Memory allocation helper routine for use by Rtl Generic Table.

I am assuming the Rtl routine does the right think if you return null
out of here.

Arguments:

    Table - 
    ByteSize - 

Return Value:

    PVOID NTAPI - 

--*/

{
    return NEW_TYPE_ARRAY( ByteSize, CHAR );
} /* tableAllocate */


VOID NTAPI
tableFree(
    struct _RTL_GENERIC_TABLE *Table,
    PVOID Buffer
    )

/*++

Routine Description:

Memory free helper routine for use by Rtl Generic Table.

Arguments:

    Table - 
    Buffer - 

Return Value:

    VOID NTAPI - 

--*/

{
    FREE_TYPE( Buffer );
} /* tableFree */
















PSUBJECT_INSTANCE
subjectEntryCreate(
    LPCWSTR Name,
    LPDWORD pdwInstanceSize
    )

/*++

Routine Description:

Allocate a zero-initialized, correctly formatted subject instance.
The instance is variable sized, depending on the name.

Arguments:

    Name - Name of instance
    pdwInstanceSize - Returned total size of instance allocated

Return Value:

    PSUBJECT_INSTANCE - 

--*/

{
    PSUBJECT_INSTANCE pSubject;
    DWORD length = wcslen( Name ) + 1, size;
    size = sizeof( SUBJECT_INSTANCE ) + (length * sizeof( WCHAR ));

    pSubject = (SUBJECT_INSTANCE *) NEW_TYPE_ARRAY_ZERO( size, CHAR );
    if (pSubject == NULL) {
        DPRINT( 0, "subjectEntryCreate failed to allocate memory\n" );
        return NULL;
    }
    pSubject->NameLength = length;
    wcscpy( pSubject->Name, Name );
    *pdwInstanceSize = size;

    return pSubject;
} /* subjectEntryCreate */


VOID
subjectEntryFree(
    PSUBJECT_INSTANCE pSubject
    )

/*++

Routine Description:

Free a subject entry.

Arguments:

    pSubject - 

Return Value:

    None

--*/

{
    // There are no contained pointers to release

    FREE_TYPE( pSubject );
} /* subjectEntryFree */


PSUBJECT_INSTANCE
LookupInsertSubjectEntry(
    IN PTARGET_INSTANCE pTarget,
    IN LPCWSTR pszMessageSubject
    )

/*++

Routine Description:

Lookup a subject instance by name. The subject table and supporting variables
are the target instance passed in.

Arguments:

    pTarget - 
    pszMessageSubject - 

Return Value:

    PSUBJECT_INSTANCE - 

--*/

{
    PVOID pElement;
    PSUBJECT_INSTANCE pDummySubject = NULL;
    PSUBJECT_INSTANCE pSubject = NULL;
    DWORD subjectSize;
    BOOLEAN fNewElement;

    // Create an empty subject instance for matching purposes
    pDummySubject = subjectEntryCreate( pszMessageSubject, &subjectSize );
    if (pDummySubject == NULL) {
        return NULL;
    }

    // Lookup existing element or insert the new element in the table
    pElement = RtlInsertElementGenericTable(
        &(pTarget->SendSubjectTable),
        pDummySubject,
        subjectSize,
        &fNewElement );
    if (!pElement) {
        // Error, not created for some reason
        // pSubject is already NULL
        goto cleanup;
    }

    // Note, after the insertion, pElement points to the actual table member,
    // while pDummy is only a copy.
    pSubject = (PSUBJECT_INSTANCE) pElement;

    // See if it was in the table
    if (!fNewElement) {
        // pSubject is set to the found element
        goto cleanup;
    }

    // It wasn't in the table
    DPRINT2( 4, "subject entry create, %ws(%d)\n",
             pDummySubject->Name, pDummySubject->NameLength );

    // BEGIN Initialize SUBJECT instance
        
    // Link the element into the list as the newest element
    InsertHeadList( &(pTarget->SendSubjectListHead), &(pSubject->ListEntry) );

    // END Initialize SUBJECT instance



    // See if we need to get rid of oldest entry
    if (pTarget->NumberSendSubjectEntries == pTarget->MaximumSendSubjectEntries) {
        // Older entries at the end of the list
        PSUBJECT_INSTANCE pDeadSubject;
        PLIST_ENTRY pListEntry;
        BOOL found;

        pListEntry = RemoveTailList( &(pTarget->SendSubjectListHead) );
        pDeadSubject = CONTAINING_RECORD( pListEntry, SUBJECT_INSTANCE, ListEntry );

        DPRINT1( 4, "Deleting oldest subject: %ws\n", pDeadSubject->Name );

        found = RtlDeleteElementGenericTable(
            &(pTarget->SendSubjectTable),
            pDeadSubject );
        Assert( found );
        pDeadSubject = NULL;

        // The code giveth and the code taketh away
        // Entry count stays the same
    } else {
        (pTarget->NumberSendSubjectEntries)++;
    }

cleanup:
    if (pDummySubject) {
        subjectEntryFree( pDummySubject );
    }
    return pSubject;
} /* LookupInsertSubjectEntry */


RTL_GENERIC_COMPARE_RESULTS NTAPI
subjectTableCompare(
    struct _RTL_GENERIC_TABLE *Table,
    PVOID FirstStruct,
    PVOID SecondStruct
    )

/*++

Routine Description:

Helper routine for Rtl generic table to compare two subject instances.

Arguments:

    Table - 
    FirstStruct - 
    SecondStruct - 

Return Value:

    RTL_GENERIC_COMPARE_RESULTS NTAPI - 

--*/

{
    int diff;
    PSUBJECT_INSTANCE pFirstSubject = (PSUBJECT_INSTANCE) FirstStruct;
    PSUBJECT_INSTANCE pSecondSubject = (PSUBJECT_INSTANCE) SecondStruct;

    diff = pFirstSubject->NameLength - pSecondSubject->NameLength;
    if (diff == 0) {
        diff = wcscmp( pFirstSubject->Name, pSecondSubject->Name );
    }
    DPRINT5( 5, "Comparing %ws(%d) with %ws(%d) = %d\n",
             pFirstSubject->Name,
             pFirstSubject->NameLength,
             pSecondSubject->Name,
             pSecondSubject->NameLength,
             diff);

    if ( 0 == diff )
        return(GenericEqual);
    else if ( diff > 0 )
        return(GenericGreaterThan);

    return(GenericLessThan);
} /* subjectTableCompare */


VOID
subjectTableDestroy(
    RTL_GENERIC_TABLE *pSubjectTable
    )

/*++

Routine Description:

Deallocate all the elements of the table.

Arguments:

    pSubjectTable - 

Return Value:

    None

--*/

{
    PVOID pElement;

    // Note that we restart the enumeration each time through because the
    // table has changed as a result of the delete.
    for( pElement = RtlEnumerateGenericTable( pSubjectTable, TRUE );
         pElement != NULL;
         pElement = RtlEnumerateGenericTable( pSubjectTable, TRUE ) ) {
        PSUBJECT_INSTANCE pSubject = (PSUBJECT_INSTANCE) pElement;
        BOOLEAN found;

        DPRINT1( 4, "\t\tCleaning up subject %ws\n", pSubject->Name );
        found = RtlDeleteElementGenericTable( pSubjectTable, pElement );
        Assert( found );
    }
} /* subjectTableDestroy */


















PTARGET_INSTANCE
targetEntryCreate(
    LPCWSTR Name,
    LPDWORD pdwInstanceSize
    )

/*++

Routine Description:

Create a zero-initialized, properly formed target instance.  The target instance
is a variable length structure.

Arguments:

    Name - 
    pdwInstanceSize - Returned total byte count allocated

Return Value:

    PTARGET_INSTANCE - null on error

--*/

{
    PTARGET_INSTANCE pTarget;
    DWORD length = wcslen( Name ) + 1, size;
    size = sizeof( TARGET_INSTANCE ) + (length * sizeof( WCHAR ));

    pTarget = (TARGET_INSTANCE *) NEW_TYPE_ARRAY_ZERO( size, CHAR );
    if (pTarget == NULL) {
        DPRINT( 0, "targetEntryCreate failed to allocate memory\n" );
        return NULL;
    }
    pTarget->NameLength = length;
    wcscpy( pTarget->Name, Name );
    *pdwInstanceSize = size;

    return pTarget;
} /* targetEntryCreate */


VOID
targetEntryFree(
    PTARGET_INSTANCE pTarget
    )

/*++

Routine Description:

Free a target instance.

This routine should not be used to remove elements from the table.

Arguments:

    pTarget - 

Return Value:

    None

--*/

{
    // Secondary pointers not cleaned up here by design

    FREE_TYPE( pTarget );
} /* targetEntryFree */


PTARGET_INSTANCE
LookupInsertTargetEntry(
    RTL_GENERIC_TABLE *pTargetTable,
    IN  LPCWSTR pszRemoteTransportAddress
    )

/*++

Routine Description:

Lookup or instert a new target entry in the target table.

Arguments:

    pTargetTable - 
    pszRemoteTransportAddress - 

Return Value:

    PTARGET_INSTANCE - 

--*/

{
    PVOID pElement;
    PTARGET_INSTANCE pDummyTarget = NULL;
    PTARGET_INSTANCE pTarget = NULL;
    DWORD targetSize;
    BOOLEAN fNewElement;

    // Create a empty target instance for matching against
    pDummyTarget = targetEntryCreate( pszRemoteTransportAddress, &targetSize );
    if (pDummyTarget == NULL) {
        return NULL;
    }

    // Lookup or Insert the new element in the table
    pElement = RtlInsertElementGenericTable(
        pTargetTable,
        pDummyTarget,
        targetSize,
        &fNewElement );
    if (!pElement) {
        // Error, not created for some reason
        // pTarget is set to NULL already
        goto cleanup;
    }

    // Note, after the insertion, pElement points to the actual table member,
    // while pDummy is only a copy.
    pTarget = (PTARGET_INSTANCE) pElement;

    // Element was already in table
    if (!fNewElement) {
        // pTarget points to the found element
        goto cleanup;
    }

    // It wasn't in the table
    DPRINT1( 4, "target entry create, %ws\n", pDummyTarget->Name );

    // BEGIN Initialize empty TARGET instance
    pTarget->MaximumSendSubjectEntries = MAXIMUM_SEND_SUBJECT_ENTRIES;
    RtlInitializeGenericTable( &(pTarget->SendSubjectTable),
                               subjectTableCompare,
                               tableAllocate,
                               tableFree,
                               NULL );
    InitializeListHead( &(pTarget->SendSubjectListHead) );
    // END Initialize empty TARGET instance
        

cleanup:
    if (pDummyTarget) {
        targetEntryFree( pDummyTarget );
    }

    return pTarget;
} /* LookupInsertTargetEntry */


RTL_GENERIC_COMPARE_RESULTS NTAPI
targetTableCompare(
    struct _RTL_GENERIC_TABLE *Table,
    PVOID FirstStruct,
    PVOID SecondStruct
    )

/*++

Routine Description:

Helper routine for Rtl generic table. Compare two target instances.

Arguments:

    Table - 
    FirstStruct - 
    SecondStruct - 

Return Value:

    RTL_GENERIC_COMPARE_RESULTS NTAPI - 

--*/

{
    int diff;
    PTARGET_INSTANCE pFirstTarget = (PTARGET_INSTANCE) FirstStruct;
    PTARGET_INSTANCE pSecondTarget = (PTARGET_INSTANCE) SecondStruct;

    diff = pFirstTarget->NameLength - pSecondTarget->NameLength;
    if (diff == 0) {
        diff = wcscmp( pFirstTarget->Name, pSecondTarget->Name );
    }
    if ( 0 == diff )
        return(GenericEqual);
    else if ( diff > 0 )
        return(GenericGreaterThan);

    return(GenericLessThan);
} /* targetTableCompare */


VOID
targetTableDestroy(
    RTL_GENERIC_TABLE *pTargetTable
    )

/*++

Routine Description:

Destroy all the elements in the target table.

Arguments:

    pTargetTable - 

Return Value:

    None

--*/

{
    PVOID pElement;

    // Note that we restart the enumeration each time through because the
    // table has changed as a result of the delete.
    for( pElement = RtlEnumerateGenericTable( pTargetTable, TRUE );
         pElement != NULL;
         pElement = RtlEnumerateGenericTable( pTargetTable, TRUE ) ) {
        PTARGET_INSTANCE pTarget = (PTARGET_INSTANCE) pElement;
        BOOLEAN found;

        DPRINT1( 4, "\tCleaning up target %ws\n", pTarget->Name );

        subjectTableDestroy( &(pTarget->SendSubjectTable) );
        // number entries and list head now incoherent

        found = RtlDeleteElementGenericTable( pTargetTable, pElement );
        Assert( found );
    }

} /* targetTableDestroy */

















DWORD __cdecl
serviceConstruct(
    PLIST_ENTRY_INSTANCE pListEntry
    )

/*++

Routine Description:

Callback routine for the generic list package
Initializes a structure of type SERVICE_INSTANCE

Arguments:

    pListEntry - 

Return Value:

    DWORD - 

--*/

{
    PSERVICE_INSTANCE pService = CONTAINING_RECORD( pListEntry, SERVICE_INSTANCE, ListEntryInstance );

    DPRINT1( 4, "serviceCreate %ws\n", pListEntry->Name );

    RtlInitializeGenericTable( &(pService->TargetTable),
                               targetTableCompare,
                               tableAllocate,
                               tableFree,
                               NULL );

    return ERROR_SUCCESS;
} /* serviceCreate */


DWORD __cdecl
SmtpServiceDestruct(
    PLIST_ENTRY_INSTANCE pListEntry
    )

/*++

Routine Description:

Callback routine for the generic list package.
Destroys a service instance

Arguments:

    None

Return Value:

    None

--*/

{
    PSERVICE_INSTANCE pService = CONTAINING_RECORD( pListEntry, SERVICE_INSTANCE, ListEntryInstance );

    DPRINT1( 4, "serviceDestroy %ws\n", pListEntry->Name );

    targetTableDestroy( &(pService->TargetTable) );

    return ERROR_SUCCESS;
}



















DWORD
SmtpTableFindSendSubject(
    IN  TRANSPORT_INSTANCE *  pTransport,
    IN  LPCWSTR               pszRemoteTransportAddress,
    IN  LPCWSTR               pszServiceName,
    IN  LPCWSTR               pszMessageSubject,
    OUT PSUBJECT_INSTANCE  *  ppSubject
    )

/*++

Routine Description:

    Description

Arguments:

    pTransport - 
    pszRemoteTransportAddress - 
    pszServiceName - 
    pszMessageSubject - 
    ppSubject - 

Return Value:

    DWORD - 

--*/

{
    DWORD status;
    PSERVICE_INSTANCE pService;
    PTARGET_INSTANCE pTarget;
    PLIST_ENTRY_INSTANCE pListEntry;

// First level search, look up service in service list
// This list is a linear linked list unsorted

    status = ListFindCreateEntry(
        serviceConstruct,
        SmtpServiceDestruct,
        sizeof( SERVICE_INSTANCE ),
        ISM_MAX_SERVICE_LIMIT,
        &(pTransport->ServiceListHead),
        &(pTransport->ServiceCount),
        pszServiceName,
        TRUE, // Create
        &(pListEntry) );
    if (status != ERROR_SUCCESS) {
        DPRINT2( 0, "Couldn't find/create service entry %ws, error %d\n",
                 pszServiceName, status );
        LogUnhandledError( status );
        goto cleanup;
    }
    pService = CONTAINING_RECORD( pListEntry, SERVICE_INSTANCE, ListEntryInstance );

// Second level search, look up target in target table

    pTarget = LookupInsertTargetEntry(
        &( pService->TargetTable ),
        pszRemoteTransportAddress
        );
    if (pTarget == NULL) {
        DPRINT1( 0, "Failed to allocate new target table entry for %ws\n",
                 pszRemoteTransportAddress);
        status = ERROR_NOT_ENOUGH_MEMORY;
        LogUnhandledError( status );
        goto cleanup;
    }

// Third level search: look up subject in subject table

    *ppSubject = LookupInsertSubjectEntry(
        pTarget,
        pszMessageSubject
        );
    if (*ppSubject == NULL) {
        DPRINT1( 0, "Failed to allocate new subject table entry for %ws\n",
                pszMessageSubject);
        status = ERROR_NOT_ENOUGH_MEMORY;
        LogUnhandledError( status );
        goto cleanup;
    }

    status = ERROR_SUCCESS;

cleanup:

    return status;
} /* SmtpTableFindSendSubject */


RTL_GENERIC_COMPARE_RESULTS NTAPI
guidTableCompare(
    struct _RTL_GENERIC_TABLE *Table,
    PVOID FirstStruct,
    PVOID SecondStruct
    )

/*++

Routine Description:

Helper routine for Rtl generic table to compare two guid instances.

Arguments:

    Table - 
    FirstStruct - 
    SecondStruct - 

Return Value:

    RTL_GENERIC_COMPARE_RESULTS NTAPI - 

--*/

{
    int diff;
    PGUID_ENTRY pFirstEntry = (PGUID_ENTRY) FirstStruct;
    PGUID_ENTRY pSecondEntry = (PGUID_ENTRY) SecondStruct;

    diff = memcmp( &(pFirstEntry->Guid), &(pSecondEntry->Guid), sizeof( GUID ) );
    if ( 0 == diff )
        return(GenericEqual);
    else if ( diff > 0 )
        return(GenericGreaterThan);

    return(GenericLessThan);
} /* guidTableCompare */


PGUID_TABLE 
SmtpCreateGuidTable(
    VOID
    )

/*++

Routine Description:

    Description

Arguments:

    VOID - 

Return Value:

    PGUID_TABLE  - 

--*/

{
    PGUID_TABLE pGuidTable = NULL;

    pGuidTable = NEW_TYPE_ZERO( GUID_TABLE );
    if (pGuidTable == NULL) {
        goto cleanup;
    }

    // BEGIN Initialize guid table here
    RtlInitializeGenericTable( &(pGuidTable->GuidTable),
                               guidTableCompare,
                               tableAllocate,
                               tableFree,
                               NULL );
    // END Initialize guid table here

cleanup:
    return pGuidTable;
} /* SmtpCreateGuidTable */


VOID
SmtpDestroyGuidTable(
    PGUID_TABLE pGuidTable
    )

/*++

Routine Description:

    Description

Arguments:

    pGuidTable - 

Return Value:

    None

--*/

{
    PVOID pElement;

    Assert( pGuidTable );

    // Note that we restart the enumeration each time through because the
    // table has changed as a result of the delete.
    for( pElement = RtlEnumerateGenericTable( &(pGuidTable->GuidTable), TRUE );
         pElement != NULL;
         pElement = RtlEnumerateGenericTable( &(pGuidTable->GuidTable), TRUE ) ) {
        BOOLEAN found;
        PGUID_ENTRY pGuidEntry = (PGUID_ENTRY) pElement;
#if DBG
        LPWSTR pszUuid;
        UuidToStringW( &(pGuidEntry->Guid), &pszUuid );
        DPRINT1( 4, "cleaning up guid %ws\n", pszUuid );
        RpcStringFreeW( &pszUuid );
#endif

        found = RtlDeleteElementGenericTable( &(pGuidTable->GuidTable), pElement );
        Assert( found );
    }

    FREE_TYPE( pGuidTable );
} /* SmtpDestroyGuidTable */


BOOL
SmtpGuidPresentInTable(
    PGUID_TABLE pGuidTable,
    GUID *pGuid
    )

/*++

Routine Description:

    Description

Arguments:

    pGuidTable - 
    pGuid - 

Return Value:

    BOOL - 

--*/

{
    PVOID pElement;
    GUID_ENTRY dummyEntry;

    ZeroMemory( &dummyEntry, sizeof( GUID_ENTRY ) );
    dummyEntry.Guid = *pGuid;

    pElement = RtlLookupElementGenericTable(
        &(pGuidTable->GuidTable),
        &dummyEntry );

    return (pElement != NULL);
} /* SmtpGuidPresentInTable */


BOOL
SmtpGuidInsertInTable(
    PGUID_TABLE pGuidTable,
    GUID *pGuid
    )

/*++

Routine Description:

    Description

Arguments:

    pGuidTable - 
    pGuid - 

Return Value:

    BOOL - 

--*/

{
    PVOID pElement;
    GUID_ENTRY dummyEntry;
    BOOLEAN fNewElement;

    ZeroMemory( &dummyEntry, sizeof( GUID_ENTRY ) );
    dummyEntry.Guid = *pGuid;

    pElement = RtlInsertElementGenericTable(
        &(pGuidTable->GuidTable),
        &dummyEntry,
        sizeof( GUID_ENTRY ),
        &fNewElement );

    return (pElement != NULL) && fNewElement;

} /* SmtpGuidInsertInTable */

/* end table.c */