/*++

Copyright (c) 1998  Microsoft Corporation

Module Name:

    list.c

Abstract:

    Generic linked list package    

    Current functions include: create element or lookup element, and delete whole
    list.

    Callers define their list element.  They embed in the structure an element of
    type LIST_ENTRY. It is the address of this field that is used to identify an
    element in the list.  The caller can convert from a pointer to a LIST_ENTRY in
    their structure, to the base of their structure, by using CONTAINING_RECORD.

    Because the caller's structure may have other dynamically allocated storage, the
    caller must pass in callback routines to create and destroy their element as
    needed.

    In the future it would be reasonable to add a sorted version of the list.
    Also, single element removal/deletion function.
    Note there is no provision for protecting the lifetime of single elements,
    especially when a pointer has been handed out to another caller. In order
    to do that, a reference counting mechanism would need to be added.

Author:

    Will Lees (wlees) 19-Oct-1998

Environment:

Notes:

Revision History:

--*/

#include <ntdspch.h>

#include <ismapi.h>
#include <debug.h>

#include <winsock.h>

#include "common.h"

#include <fileno.h>
#define FILENO  FILENO_ISMSERV_LIST

#define DEBSUB "ISMLIST:"

/* External */

/* Static */

/* Forward */ /* Generated by Emacs 19.34.1 on Tue Oct 20 15:27:32 1998 */

DWORD
ListFindCreateEntry(
    LIST_CREATE_CALLBACK_FN *pfnCreate,
    LIST_DESTROY_CALLBACK_FN *pfnDestroy,
    DWORD cbEntry,
    DWORD MaximumNumberEntries,
    PLIST_ENTRY pListHead,
    LPDWORD pdwEntryCount,
    LPCWSTR EntryName,
    BOOL Create,
    PLIST_ENTRY_INSTANCE *ppListEntry
    );

DWORD
ListDestroyList(
    LIST_DESTROY_CALLBACK_FN *pfnDestroy,
    PLIST_ENTRY pListHead,
    LPDWORD pdwEntryCount
    );

/* End Forward */


DWORD
ListFindCreateEntry(
    LIST_CREATE_CALLBACK_FN *pfnCreate,
    LIST_DESTROY_CALLBACK_FN *pfnDestroy,
    DWORD cbEntry,
    DWORD MaximumNumberEntries,
    PLIST_ENTRY pListHead,
    LPDWORD pdwEntryCount,
    LPCWSTR EntryName,
    BOOL Create,
    PLIST_ENTRY_INSTANCE *ppListEntry
    )

/*++

Routine Description:

Generic list lookup and element creation routine.

Look up an element with the given name. If not found and Create is true, create
a new one.  If there are too many elements, release the oldest one.

List lock should be held by caller, if necessary.

Caller should have initialized listhead

Arguments:

    pfnCreate - Routine to initialize the callers element, but not allocate
    pfnDestroy - Routine to clear the caller's element, but not deallocate
    cbEntry - Size of callers element, including LIST_ENTRY_INSTANCE
    MaximumNumberEntries - List limit, or 0 for none
    pListHead - List head, must already be initialized
        NOTE, this is a LIST_ENTRY not a LIST_ENTRY_INSTANCE
    pdwEntryCount - Updated count of entries in list, caller must initialize
    EntryName - Name of new entry. Matching is on the basis of this name
    Create - Set true if we should create a new element if this name not found
    ppListEntry - Returned entry, either existing or newly created

Return Value:

    DWORD - 

--*/

{
    DWORD status, length;
    PLIST_ENTRY curr;
    PLIST_ENTRY_INSTANCE pNewEntry = NULL;

    // Be defensive
    if ( !( ppListEntry ) ||
         !( pdwEntryCount ) ||
         ( cbEntry < sizeof( LIST_ENTRY_INSTANCE ) ) ||
         !( pListHead ) ||
         !( pListHead->Flink ) ||
         !( pListHead->Blink ) ) {
        Assert( FALSE );
        return E_INVALIDARG;
    }

    // See if the entry is already present

    curr = pListHead->Flink;
    while (curr != pListHead) {
        PLIST_ENTRY_INSTANCE pListEntry;

        pListEntry = CONTAINING_RECORD( curr, LIST_ENTRY_INSTANCE, ListEntry );

        if (_wcsicmp( EntryName, pListEntry->Name ) == 0) {
            *ppListEntry = pListEntry;
            return ERROR_SUCCESS;
        }
        curr = curr->Flink;
    }

    // If we are not allowed to create it, exit at this point

    if (!Create) {
        *ppListEntry = NULL;
        return ERROR_FILE_NOT_FOUND;
    }

    // Create new record of user-specified size

    pNewEntry = (PLIST_ENTRY_INSTANCE) NEW_TYPE_ARRAY_ZERO( cbEntry, BYTE );
    if (pNewEntry == NULL) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    // Initialize our part
    // Do this first so callback can use this info

    length = wcslen( EntryName );
    if (length == 0) {
        status = ERROR_INVALID_PARAMETER;
        goto cleanup;
    }

    pNewEntry->Name = NEW_TYPE_ARRAY( (length + 1), WCHAR );
    if (pNewEntry->Name == NULL) {
        status = ERROR_NOT_ENOUGH_MEMORY;
        goto cleanup;
    }
    wcscpy( pNewEntry->Name, EntryName );

    // Initialize caller's part

    status = (*pfnCreate)( pNewEntry );
    if (status != ERROR_SUCCESS) {
        goto cleanup;
    }

    // If too many instances, get rid of one
    if ( (MaximumNumberEntries) &&
         (*pdwEntryCount == MaximumNumberEntries) ) {
        PLIST_ENTRY entry;
        PLIST_ENTRY_INSTANCE pListEntry;

        // Select least recent
        entry = pListHead->Flink;
        Assert( !IsListEmpty( pListHead ) );

        RemoveEntryList( entry );
        pListEntry = CONTAINING_RECORD( entry, LIST_ENTRY_INSTANCE,
                                        ListEntry);

        // Deallocate caller's part
        (VOID) (*pfnDestroy)( pListEntry );

        // Deallocate our part
        if (pListEntry->Name) {
            FREE_TYPE( pListEntry->Name );
        }
        FREE_TYPE( pListEntry );

    } else {
        (*pdwEntryCount)++;
    }

    // Link the new instance onto the list at the end

    InsertTailList( pListHead, &(pNewEntry->ListEntry) );

    // Success!

    *ppListEntry = pNewEntry;

    return ERROR_SUCCESS;

cleanup:
    if (pNewEntry) {
        if (pNewEntry->Name != NULL) {
            FREE_TYPE( pNewEntry->Name );
        }
        FREE_TYPE( pNewEntry );
    }

    return status;

} /* ListFindCreateEntry */


DWORD
ListDestroyList(
    LIST_DESTROY_CALLBACK_FN *pfnDestroy,
    PLIST_ENTRY pListHead,
    LPDWORD pdwEntryCount
    )

/*++

Routine Description:

Destroy a generic list.

Caller should hold list lock if necessary

Arguments:

    pfnDestroy - Caller's element release function
    pListHead - List head of list
    pdwEntryCount - Updated count of elements in list, set to zero
        Note, we check this for accuracy

Return Value:

    DWORD - Always success

--*/

{
    PLIST_ENTRY entry;
    PLIST_ENTRY_INSTANCE pListEntry;

    Assert( pdwEntryCount );

    while (!IsListEmpty( pListHead )) {
        Assert( *pdwEntryCount );
        (*pdwEntryCount)--;

        entry = RemoveHeadList( pListHead );

        pListEntry = CONTAINING_RECORD( entry, LIST_ENTRY_INSTANCE,
                                        ListEntry );

        // Release caller's part
        (VOID) (*pfnDestroy)( pListEntry );

        // Release our part
        FREE_TYPE( pListEntry->Name );

        FREE_TYPE( pListEntry );
    }

    Assert( *pdwEntryCount == 0 );
    *pdwEntryCount = 0;

    return ERROR_SUCCESS;
} /* ListDestroyList */

/* end list.c */
