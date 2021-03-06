/*++

Copyright (c) 2001  Microsoft Corporation

Module Name:

    sid.cxx

Abstract:

    Routines implementing the SID pseudo-object.

Author:

    Cliff Van Dyke (cliffv) 8-May-2001

--*/

#include "pch.hxx"



DWORD
AzpSidInit(
    IN PGENERIC_OBJECT ParentGenericObject,
    IN PGENERIC_OBJECT ChildGenericObject
    )
/*++

Routine Description:

    This routine is a worker routine for AzSidCreate.  It does any object specific
    initialization that needs to be done.

    On entry, AzGlResource must be locked exclusively.

Arguments:

    ParentGenericObject - Specifies the parent object to add the child object onto.
        The reference count has been incremented on this object.

    ChildGenericObject - Specifies the newly allocated child object.
        The reference count has been incremented on this object.

Return Value:

    NO_ERROR - The operation was successful
    ERROR_NOT_ENOUGH_MEMORY - not enough memory
    Other exception status codes

--*/
{
    PAZP_SID AzpSid = (PAZP_SID) ChildGenericObject;

    //
    // Initialization
    //

    ASSERT( AzpIsLockedExclusive( &AzGlResource ) );


    //
    // Behave differently depending on the object type of the parent object
    //

    ASSERT( ParentGenericObject->ObjectType == OBJECT_TYPE_AZAUTHSTORE ||
            ParentGenericObject->ObjectType == OBJECT_TYPE_APPLICATION ||
            ParentGenericObject->ObjectType == OBJECT_TYPE_SCOPE );

    //
    // Sids are referenced by groups and roles.
    //
    //  Let the generic object manager know all of the lists we support
    //

    ChildGenericObject->GenericObjectLists = &AzpSid->backGroupMembers,

    // Sids are referenced by groups
    ObInitObjectList( &AzpSid->backGroupMembers,
                      &AzpSid->backGroupNonMembers,
                      TRUE,     // backward link
                      AZP_LINKPAIR_SID_MEMBERS,
                      0,    // No dirty bit on back link
                      NULL,
                      NULL,
                      NULL );

    ObInitObjectList( &AzpSid->backGroupNonMembers,
                      &AzpSid->backRoles,
                      TRUE,     // backward link
                      AZP_LINKPAIR_SID_NON_MEMBERS,
                      0,    // No dirty bit on back link
                      NULL,
                      NULL,
                      NULL );

    // Sids are referenced by "Roles"
    ObInitObjectList( &AzpSid->backRoles,
                      &AzpSid->backAdmins,
                      TRUE,     // Backward link
                      0,        // No link pair id
                      0,    // No dirty bit on back link
                      NULL,
                      NULL,
                      NULL );


    // Sids are referenced by object admins
    ObInitObjectList( &AzpSid->backAdmins,
                      &AzpSid->backReaders,
                      TRUE,     // Backward link
                      AZP_LINKPAIR_POLICY_ADMINS,      // diff admins and readers
                      0,    // No dirty bit on back link
                      NULL,
                      NULL,
                      NULL );


    if ( !IsDelegatorObject( ParentGenericObject->ObjectType ) ) {

        // Sids are referenced by object readers
        ObInitObjectList( &AzpSid->backReaders,
                          NULL,
                          TRUE,     // Backward link
                          AZP_LINKPAIR_POLICY_READERS,     // diff admins and readers
                          0,    // No dirty bit on back link
                          NULL,
                          NULL,
                          NULL );

    } else {

        // Sids are referenced by object readers
        ObInitObjectList( &AzpSid->backReaders,
                          &AzpSid->backDelegatedPolicyUsers,
                          TRUE,     // Backward link
                          AZP_LINKPAIR_POLICY_READERS,     // diff admins and readers
                          0,    // No dirty bit on back link
                          NULL,
                          NULL,
                          NULL );

        // Sids are referenced by delegated object users

        ObInitObjectList( &AzpSid->backDelegatedPolicyUsers,
                          NULL,
                          TRUE,     // Backward link
                          AZP_LINKPAIR_DELEGATED_POLICY_USERS,
                          0,    // No dirty bit on back link
                          NULL,
                          NULL,
                          NULL );
    }

    return NO_ERROR;
}


VOID
AzpSidFree(
    IN PGENERIC_OBJECT GenericObject
    )
/*++

Routine Description:

    This routine is a worker routine for Sid object free.  It does any object specific
    cleanup that needs to be done.

    On entry, AzGlResource must be locked exclusively.

Arguments:

    GenericObject - Specifies a pointer to the object to be deleted.

Return Value:

    None

--*/
{
    // PAZP_SID AzpSid = (PAZP_SID) GenericObject;
    UNREFERENCED_PARAMETER( GenericObject );

    //
    // Initialization
    //

    ASSERT( AzpIsLockedExclusive( &AzGlResource ) );

    //
    // Free any local strings
    //



}
