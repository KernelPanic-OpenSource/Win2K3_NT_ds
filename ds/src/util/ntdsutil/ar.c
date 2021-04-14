/*++

copyright (c) 1998  Microsoft Corporation

Module Name:

    ar.c

Abstract:

    This module contains the definitions of the functions for performing
    Authoritative Restores.

Author:

    Kevin Zatloukal (t-KevinZ) 05-08-98

Revision History:

    05-08-98 t-KevinZ
        Created.

    02-17-00 xinhe
        Added restore object.

    08-06-01 BrettSh
        Added list NCs (func: AuthoritativeRestoreListNcCrsWorker)

--*/


#include <NTDSpch.h>
#pragma hdrstop

#include <dsjet.h>
#include <ntdsa.h>
#include <scache.h>
#include <mdglobal.h>
#include <dbglobal.h>
#include <attids.h>
#include <dbintrnl.h>
#include <dsconfig.h>

#include <limits.h>
#include <drs.h>
#include <objids.h>
#include <dsutil.h>
#include <ntdsbsrv.h>
#include <ntdsbcli.h>
#include <objids.h>
#include "parsedn.h"
#include "ditlayer.h"
#include "ar.h"

#include "scheck.h"

#ifndef OPTIONAL
#define OPTIONAL
#endif

#include "reshdl.h"
#include "resource.h"

// ASSERT() is being used in this code base
// Some DS macro's in use here expand to Assert().
// Map them to the same form as the rest.
#define Assert(exp) ASSERT(exp)

typedef
HRESULT
(*VISIT_FUNCTION)(
    IN DB_STATE *DbState,
    IN TABLE_STATE *TableState,
    IN BOOL AlreadyFilledRetrievalArray
    );

typedef
HRESULT
(*VISIT_LINK_FUNCTION)(
    IN DB_STATE *DbState,
    IN TABLE_STATE *LinkTableState,
    IN BOOL fDirectionForward
    );

typedef
HRESULT
(*TRAVERSAL_FUNCTION)(
    IN DB_STATE *DbState,
    IN TABLE_STATE *TableState,
    IN TABLE_STATE *SearchTableState,
    IN TABLE_STATE *LinkTableState,
    IN RETRIEVAL_ARRAY *RetrievalArray,
    IN RETRIEVAL_ARRAY *SearchRetrievalArray,
    IN RETRIEVAL_ARRAY *LinkRetrievalArray,
    IN VISIT_FUNCTION Visit,
    IN VISIT_LINK_FUNCTION LinkVisit
    );


#define SECONDS_PER_DAY (60*60*24)

#define DEFAULT_DN_SIZE 1024

#define MAX_DWORD_DIGITS 10

#define HALF_RANGE 0x7fffffff
#define MAX_VERSION_INCREASE HALF_RANGE

// These constants represent the number of records to process between updates
// to the progress meter.  There are different constants for different times
// in the program.
#define COUNTING_DISPLAY_DELTA 100
#define UPDATING_DISPLAY_DELTA 10

// This global is used by traversal functions when determining how often to
// update the progress meter.  It should be set to one of the *_DISPLAY_DELTA
// constants from above.
DWORD gCurrentDisplayDelta;

// This global tells how many digits will be used in the progress meter.
DWORD gNumDigitsToPrint;

// This is the amount that version numbers are increased per day that this
// machine has been idle.
DWORD gVersionIncreasePerDay;

// This is the USN range to search for
USN gusnLow, gusnHigh;

// Used by errprintf (see its Routine Description)
BOOL gInUnfinishedLine;

// These globals contain the information that is used by
// AuthoritativeRestoreCurrentObject to update the meta-data of the current
// object.
DWORD  gVersionIncrease;
DSTIME gCurrentTime;
GUID   gDatabaseGuid;

// This global is incremented by each call to CountRecord.  After the traversal
// is completed, it will contain the total number of records that need to be
// updated.
ULONG gRecordCount;

// This global is incremented by each call to
// AuthoritativeRestoreCurrentObject. After the traversal is completed, it
// should contain the same number as gRecordCount.
ULONG gRecordsUpdated;

// This will point to the DN of the root of the subtree to update (if this is
// a subtree Authoritative Restore).
CONST WCHAR *gSubtreeRoot;

// ***************************************************************************
// This is the array of column names from which retrieval array for
// AuthoritativeRestoreCurrentObject is generated.
CONST CHAR *gMainColumnNames[] = {
    SZDNT,
    SZPDNT,
    SZINSTTYPE,
    SZISDELETED,
    SZMETADATA,
    SZOBJCLASS
    };

#define NUM_MAIN_COLUMN_NAMES 6

// This is the retrieval array that is used by
// AuthoritativeRestoreCurrentObject.  It must have been generated from the
// gMainColumnNames array above.
RETRIEVAL_ARRAY *gMainRetrievalArray;
RETRIEVAL_ARRAY *gSearchRetrievalArray;

// ***************************************************************************
// These are the names of the columns which are set by
// AuthoritativeRestoreCurrentObject but are not queried.
CHAR *gOtherColumnNames[] = {
    SZDRAUSNNAME,
    SZDRATIMENAME
    };

#define NUM_OTHER_COLUMN_NAMES 2

// These are the column ids for the values which are set by
// AuthoritativeRestoreCurrentObject but not queried.
DWORD gUsnChangedColumnId;
DWORD gWhenChangedColumnId;

// ***************************************************************************
// This is the array of column names from which retrieval array for
// CountRecord is generated.
CONST CHAR *gCountingColumnNames[] = {
    SZDNT,
    SZPDNT,
    SZINSTTYPE,
    SZISDELETED,
    SZOBJCLASS,
    SZMETADATA          // Must be last because it is optional
    };

// The METADATA column is optional. It is only included when necessary because
// it is an extra performance cost to read this long binary column
#define NUM_COUNTING_COLUMN_NAMES 5
#define NUM_COUNTING_COLUMN_NAMES_WITH_METADATA 6

// This is the retrieval array that is used by CountRecord.  It must have been
// generated by the gCountingColumnNames array above.
RETRIEVAL_ARRAY *gCountingRetrievalArray;
RETRIEVAL_ARRAY *gCountingSearchRetrievalArray;

// ***************************************************************************
// This is the array of link column names from which retrieval array for
// CountRecord is generated.
CONST CHAR *gCountingLinkColumnNames[] = {
    SZLINKDNT,
    SZLINKBASE,
    SZBACKLINKDNT,
    SZLINKMETADATA,
    SZLINKUSNCHANGED
    };

#define NUM_COUNTING_LINK_COLUMN_NAMES 5

// This is the retrieval array that is used by CountRecord.  It must have been
// generated by the gCountingColumnNames array above.
RETRIEVAL_ARRAY *gCountingLinkRetrievalArray;

// ***************************************************************************
// This is the array of column names from which retrieval array for
// link table is generated.
CONST CHAR *gLinkColumnNames[] = {
    SZLINKDNT,
    SZLINKBASE,
    SZBACKLINKDNT,
    SZLINKMETADATA,
    SZLINKDELTIME,
    SZLINKUSNCHANGED
    };

#define NUM_LINK_COLUMN_NAMES 6

// This is the retrieval array that is used by
// link table.  It must have been generated from the
// gLinkColumnNames array above.
RETRIEVAL_ARRAY *gMainLinkRetrievalArray;

// ***************************************************************************
// These following globals are all used to ease access into the retrieval
// arrays declared above.  (See AuthoritativeRestore find out how they are
// used.)
JET_RETRIEVECOLUMN *gDntVal;
JET_RETRIEVECOLUMN *gPDntVal;
JET_RETRIEVECOLUMN *gInstanceTypeVal;
JET_RETRIEVECOLUMN *gIsDeletedVal;
JET_RETRIEVECOLUMN *gMetaDataVal;
JET_RETRIEVECOLUMN *gObjClassVal;

JET_RETRIEVECOLUMN *gSearchDntVal;
JET_RETRIEVECOLUMN *gSearchPDntVal;
JET_RETRIEVECOLUMN *gSearchInstanceTypeVal;
JET_RETRIEVECOLUMN *gSearchIsDeletedVal;
JET_RETRIEVECOLUMN *gSearchMetaDataVal;
JET_RETRIEVECOLUMN *gSearchObjClassVal;

DWORD gDntIndex;
DWORD gPDntIndex;
DWORD gInstanceTypeIndex;
DWORD gIsDeletedIndex;
DWORD gMetaDataIndex;
DWORD gObjClassIndex;

JET_RETRIEVECOLUMN *gLinkDntVal;
JET_RETRIEVECOLUMN *gLinkBaseVal;
JET_RETRIEVECOLUMN *gBackLinkDntVal;
JET_RETRIEVECOLUMN *gLinkMetaDataVal;
JET_RETRIEVECOLUMN *gLinkDelTimeVal;
JET_RETRIEVECOLUMN *gLinkUsnChangedVal;

DWORD gLinkDntIndex;
DWORD gLinkBaseIndex;
DWORD gBackLinkDntIndex;
DWORD gLinkMetaDataIndex;
DWORD gLinkDelTimeIndex;
DWORD gLinkUsnChangedIndex;

// When the subtree traversal finds the head of a new DC, it adds the DNT of
// that record to this list, and after the subtree traversal is done, it
// prints out a list of the sub-NCs that were encountered.
DWORD *gSubrefList;

DWORD gSubrefListSize;
DWORD gSubrefListMaxSize;

#define DEFAULT_SUBREF_LIST_SIZE 8

// We only want to update the subref list during one of the two passes.
BOOL gUpdateSubrefList;

// List of restored DNTs
DWORD *gRestoredList;
DWORD gRestoredListSize;
DWORD gRestoredListMaxSize;

// The DNT of the Schema object.
DWORD gSchemaDnt;

// This global is used to store a description of a Jet error that has occured.
WCHAR gJetErrorDescription[MAX_JET_ERROR_LENGTH];

HRESULT
GetRegDword(
    IN CHAR *KeyName,
    OUT DWORD *OutputDword,
    IN BOOL Optional
    );

HRESULT
AuthoritativeRestore(
    IN TRAVERSAL_FUNCTION Traversal
    );

HRESULT
TraverseDit(
    IN DB_STATE *DbState,
    IN TABLE_STATE *TableState,
    IN TABLE_STATE *SearchTableState,
    IN TABLE_STATE *LinkTableState,
    IN RETRIEVAL_ARRAY *RetrievalArray,
    IN RETRIEVAL_ARRAY *SearchRetrievalArray,
    IN RETRIEVAL_ARRAY *LinkRetrievalArray,
    IN VISIT_FUNCTION Visit,
    IN VISIT_LINK_FUNCTION LinkVisit
    );

HRESULT
TraverseSubtree(
    IN DB_STATE *DbState,
    IN TABLE_STATE *TableState,
    IN TABLE_STATE *SearchTableState,
    IN TABLE_STATE *LinkTableState,
    IN RETRIEVAL_ARRAY *RetrievalArray,
    IN RETRIEVAL_ARRAY *SearchRetrievalArray,
    IN RETRIEVAL_ARRAY *LinkRetrievalArray,
    IN VISIT_FUNCTION Visit,
    IN VISIT_LINK_FUNCTION LinkVisit
    );


HRESULT
TraverseObject(
    IN DB_STATE *DbState,
    IN TABLE_STATE *TableState,
    IN TABLE_STATE *SearchTableState,
    IN TABLE_STATE *LinkTableState,
    IN RETRIEVAL_ARRAY *RetrievalArray,
    IN RETRIEVAL_ARRAY *SearchRetrievalArray,
    IN RETRIEVAL_ARRAY *LinkRetrievalArray,
    IN VISIT_FUNCTION Visit,
    IN VISIT_LINK_FUNCTION LinkVisit
    );

HRESULT
CountRecord(
    IN DB_STATE *DbState,
    IN TABLE_STATE *TableState,
    IN BOOL AlreadyFilledRetrievalArray
    );

HRESULT
CountLink(
    IN DB_STATE *DbState,
    IN TABLE_STATE *LinkTableState,
    IN BOOL fDirectionForward
    );

HRESULT
AuthoritativeRestoreCurrentObject(
    IN DB_STATE *DbState,
    IN TABLE_STATE *TableState,
    IN BOOL AlreadyFilledRetrievalArray
    );

HRESULT
AuthoritativeRestoreCurrentLink(
    IN DB_STATE *DbState,
    IN TABLE_STATE *LinkTableState,
    IN BOOL fDirectionForward
    );

HRESULT
TraverseSubtreeRecursive(
    IN DB_STATE *DbState,
    IN TABLE_STATE *TableState,
    IN TABLE_STATE *SearchTableState,
    IN TABLE_STATE *LinkTableState,
    IN RETRIEVAL_ARRAY *RetrievalArray,
    IN RETRIEVAL_ARRAY *SearchRetrievalArray,
    IN RETRIEVAL_ARRAY *LinkRetrievalArray,
    IN VISIT_FUNCTION Visit,
    IN VISIT_LINK_FUNCTION LinkVisit,
    IN BOOL SubtreeRoot
    );

HRESULT
GetVersionIncrease(
    IN DB_STATE *DbState,
    OUT DWORD *VersionIncrease
    );

HRESULT
GetCurrentDsTime(
    OUT DSTIME *CurrentTime
    );

ULONG
NumDigits(
    IN ULONG N
    );

HRESULT
MetaDataLookup(
    IN ATTRTYP AttributeType,
    IN PROPERTY_META_DATA_VECTOR *MetaDataVector,
    OUT DWORD *Index
    );

HRESULT
MetaDataInsert(
    IN ATTRTYP AttributeType,
    IN OUT PROPERTY_META_DATA_VECTOR **MetaDataVector,
    IN OUT DWORD *BufferSize
    );

HRESULT
DsTimeToString(
    IN DSTIME Time,
    OUT CHAR *String
    );

int
errprintf(
    IN char *FormatString,
    IN ...
    );

int
errprintfRes(
    IN UINT FormatStringId,
    IN ...
    );

int
dbgprintf(
    IN CHAR *FormatString,
    IN ...
    );

HRESULT
ARAlloc(
    OUT VOID **Buffer,
    IN DWORD Size
    );

HRESULT
ARRealloc(
    IN OUT VOID **Buffer,
    IN OUT DWORD *CurrentSize
    );

VOID
UpdateProgressMeter(
    IN DWORD Progress,
    IN BOOL MustUpdate
    );



HRESULT
AuthoritativeRestoreFull(
    IN DWORD VersionIncreasePerDay,
    IN USN usnLow,
    IN USN usnHigh
    )
/*++

Routine Description:

    This function performs an Authoritative Restore on the entire DIT.

Arguments:

    VersionIncreasePerDay - Supplies the amount by which to increase the
        version numbers for each day that the DIT has been idle.
    usnLow - The lower limit of USN range, or zero
    usnHigh - The higher limit of the USN range, or zero

Return Value:

    S_OK - The operation succeeded.
    E_INVALIDARG - One of the given pointers was NULL.
    E_OUTOFMEMORY - Not enough memory to allocate buffer.
    E_UNEXPECTED - Some variety of unexpected error occured.

--*/
{

    gVersionIncreasePerDay = VersionIncreasePerDay;
    gSubtreeRoot = NULL;
    gusnLow = usnLow;
    gusnHigh = usnHigh;

    return AuthoritativeRestore(&TraverseDit);

} // AuthoritativeRestoreFull



HRESULT
AuthoritativeRestoreSubtree(
    IN CONST WCHAR *SubtreeRoot,
    IN DWORD VersionIncreasePerDay
    )
/*++

Routine Description:

    This function performs an Authoritative Restore on the subtree of the DIT
    which is rooted at the given object.

Arguments:

    VersionIncreasePerDay - Supplies the amount by which to increase the
        version numbers for each day that the DIT has been idle.
    SubtreeRoot - Supplies the Subtree of the root to restore.

Return Value:

    S_OK - The operation succeeded.
    E_INVALIDARG - One of the given pointers was NULL.
    E_OUTOFMEMORY - Not enough memory to allocate buffer.
    E_UNEXPECTED - Some variety of unexpected error occured.

--*/
{

    gVersionIncreasePerDay = VersionIncreasePerDay;
    gSubtreeRoot = SubtreeRoot;
    gusnLow = gusnHigh = 0;

    return AuthoritativeRestore(&TraverseSubtree);

} // AuthoritativeRestoreSubtree



HRESULT
AuthoritativeRestoreObject(
    IN CONST WCHAR *SubtreeRoot,
    IN DWORD VersionIncreasePerDay
    )
/*++

Routine Description:

    This function performs an Authoritative Restore on the given object
    -- SubtreeRoot.

Arguments:

    VersionIncreasePerDay - Supplies the amount by which to increase the
        version numbers for each day that the DIT has been idle.
    SubtreeRoot - Supplies the object.

Return Value:

    S_OK - The operation succeeded.
    E_INVALIDARG - One of the given pointers was NULL.
    E_OUTOFMEMORY - Not enough memory to allocate buffer.
    E_UNEXPECTED - Some variety of unexpected error occured.

--*/
{

    gVersionIncreasePerDay = VersionIncreasePerDay;
    gSubtreeRoot = SubtreeRoot;
    gusnLow = gusnHigh = 0;

    return AuthoritativeRestore(&TraverseObject);

} // AuthoritativeRestoreSubtree





HRESULT
AuthoritativeRestore(
    IN TRAVERSAL_FUNCTION Traverse
    )
/*++

Routine Description:

    This function performs an authoritative restore.  The given traversal
    function is used select which records are updated.

Arguments:

    Traversal - Supplies the TRAVERSAL_FUNCTION which enumerates through the
        objects to be updated.

Return Value:

    S_OK - The operation succeeded.
    E_INVALIDARG - One of the given pointers was NULL.
    E_OUTOFMEMORY - Not enough memory to allocate buffer.
    E_UNEXPECTED - Some variety of unexpected error occured.

--*/
{

    HRESULT returnValue = S_OK;
    HRESULT result;
    JET_ERR jetResult;
    RPC_STATUS rpcStatus;

    DB_STATE *dbState = NULL;
    TABLE_STATE *tableState = NULL;
    TABLE_STATE *searchTableState = NULL;
    TABLE_STATE *linkTableState = NULL;
    ULONG maxRecordCount;
    DWORD i;
    WCHAR *dnBuffer = NULL;
    DWORD dnBufferSize = 0;
    DWORD size;
    DWORD columnIds[NUM_OTHER_COLUMN_NAMES];
    BOOL  fRestored = FALSE;
    DWORD restValue = 0;
    DWORD cCountingColumns;
    DSTIME llExpiration = 0;

    if ( Traverse == NULL ) {
        ASSERT(FALSE);
        returnValue = E_INVALIDARG;
        goto CleanUp;
    }

    /* initialize global variables */
    gInUnfinishedLine = FALSE;
    gSubrefList = NULL;
    gSubrefListSize = 0;
    gSubrefListMaxSize = 0;
    gRestoredList = NULL;
    gRestoredListSize = 0;
    gRestoredListMaxSize = 0;

    DitSetErrorPrintFunction(&errprintfRes);

    //"\nOpening DIT database... "
    RESOURCE_PRINT (IDS_AR_OPEN_DB_DIT);

    gInUnfinishedLine = TRUE;

    result = DitOpenDatabase(&dbState);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    //"done.\n"
    RESOURCE_PRINT (IDS_DONE);
    gInUnfinishedLine = FALSE;

    result = DitOpenTable(dbState, SZDATATABLE, SZDNTINDEX, &tableState);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    result = DitOpenTable(dbState, SZDATATABLE, SZDNTINDEX, &searchTableState);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    // SZLINKALLINDEX includes both present and absent link values
    result = DitOpenTable(dbState, SZLINKTABLE, SZLINKALLINDEX, &linkTableState);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    result = GetVersionIncrease(dbState, &gVersionIncrease);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    result = GetCurrentDsTime(&gCurrentTime);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    //
    // Determine if we have been restored from backup.
    //

    //
    // Determine if we have been restored from backup.
    //
    // There are 3 cases of what state the DIT could be in when open the 
    // database for authoritative restore.  Two cases for after a restore, and 
    // one case if the DIT was not restored.
    //
    //  1. Legacy restore. Reg key set, dit state not set. The dit state change 
    //     was not made to the legacy backup path in order to reduce churn 
    //     before .NET ship.
    //  2. Snapshot restore. May have been a "writerless restore" in which no 
    //     code has run since this restore. Regkey should never be set. 
    //     DitState will indicate eBackedupDit.
    //  3. No restore has taken place.  Reg key not set. Ditstate == eRunningDit.
    Assert(dbState->eDitState == eBackedupDit || dbState->eDitState == eRunningDit);
    if (GetRegDword(DSA_RESTORED_DB_KEY, &restValue, TRUE) == S_OK) {
        // Legacy API backup/restore
        Assert(dbState->eDitState == eRunningDit);
        fRestored = TRUE;
    } else if (dbState->eDitState == eBackedupDit) {
        // Snapshot backup/restore.
        fRestored = TRUE;
    }

    //
    // if this was not a restore from backup, use the current invocation ID.
    // else, create a new one.
    //

    if ( !fRestored ) {

        result = DitGetDatabaseGuid(dbState, &gDatabaseGuid);
        if ( FAILED(result) ) {
            returnValue = result;
            goto CleanUp;
        }
    } else {

        DWORD         err;
        USN           usnAtBackup;
        LPSTR         pszUsnChangedColName = SZDRAUSNNAME;
        LPSTR         pszLinkUsnChangedColName = SZLINKUSNCHANGED;
        JET_COLUMNID  usnChangedId, linkUsnChangedId;

        err = DitGetColumnIdsByName(dbState,
                                    tableState,
                                    &pszUsnChangedColName,
                                    1,
                                    &usnChangedId);
        Assert(0 == err);

        err = DitGetColumnIdsByName(dbState,
                                    linkTableState,
                                    &pszLinkUsnChangedColName,
                                    1,
                                    &linkUsnChangedId);
        Assert(0 == err);

        if (0 == err) {
            // Check the usn-at-backup in the hidden table before we allocate
            // any new usns. 

            // As part of the backup process, we wrote the usn-at-backup value
            // into the hidden table.  To verify it's there, read it back now.
            err = ErrGetBackupUsn(dbState->databaseId,
                                  dbState->sessionId,
                                  dbState->hiddenTableId,
                                  &usnAtBackup,
                                  &llExpiration); 

            // An llExpiration of 0 would mean a legacy backup.
            if (llExpiration != 0 &&
                GetSecondsSince1601() > llExpiration) {
                errprintfRes(IDS_SNAPSHOT_BACKUP_EXPIRED);
            }
            Assert(0 == err);
        }

        if (0 == err) {
            //
            // Get a uuid. This routine will check to see if we already
            // allocated a new one. If so, it uses that. This is to handle
            // the case of multiple Auth restores.
            //

            err = ErrGetNewInvocationId(NEW_INVOCID_CREATE_IF_NONE | NEW_INVOCID_SAVE,
                                        &gDatabaseGuid);
            Assert(0 == err);
        }

        if (err != ERROR_SUCCESS) {
            //"Cannot generate new invocation id for dsa. Error %d\n"
            errprintfRes(IDS_AR_ERR_GEN_INVOK_ID_DSA, err);

            returnValue = E_UNEXPECTED;
            goto CleanUp;
        }
    }

    result = DitGetSchemaDnt(dbState, &gSchemaDnt);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    if (gusnLow && gusnHigh) {
        RESOURCE_PRINT2 (IDS_AR_USN_RANGE, gusnLow, gusnHigh );
    }

    // Make a preliminary pass through the updateable objects just to count
    // them.

    if (gusnLow && gusnHigh) {
        cCountingColumns = NUM_COUNTING_COLUMN_NAMES_WITH_METADATA;
    } else {
        cCountingColumns = NUM_COUNTING_COLUMN_NAMES;
    }

    // Main retrieval array

    result = DitCreateRetrievalArray(dbState,
                                     tableState,
                                     gCountingColumnNames,
                                     cCountingColumns,
                                     &gCountingRetrievalArray);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    gDntIndex          = gCountingRetrievalArray->indexes[0];
    gPDntIndex         = gCountingRetrievalArray->indexes[1];
    gInstanceTypeIndex = gCountingRetrievalArray->indexes[2];
    gIsDeletedIndex    = gCountingRetrievalArray->indexes[3];
    gObjClassIndex     = gCountingRetrievalArray->indexes[4];

    gDntVal         = &gCountingRetrievalArray->columnVals[gDntIndex];
    gPDntVal        = &gCountingRetrievalArray->columnVals[gPDntIndex];
    gInstanceTypeVal= &gCountingRetrievalArray->columnVals[gInstanceTypeIndex];
    gIsDeletedVal   = &gCountingRetrievalArray->columnVals[gIsDeletedIndex];
    gObjClassVal    = &gCountingRetrievalArray->columnVals[gObjClassIndex];

    if (gusnLow && gusnHigh) {
        gMetaDataIndex     = gCountingRetrievalArray->indexes[5];
        gMetaDataVal    = &gCountingRetrievalArray->columnVals[gMetaDataIndex];
    } else {
        gMetaDataVal = NULL;
    }

    // Search retrieval array

    result = DitCreateRetrievalArray(dbState,
                                     tableState,
                                     gCountingColumnNames,
                                     cCountingColumns,
                                     &gCountingSearchRetrievalArray);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    gDntIndex          = gCountingSearchRetrievalArray->indexes[0];
    gPDntIndex         = gCountingSearchRetrievalArray->indexes[1];
    gInstanceTypeIndex = gCountingSearchRetrievalArray->indexes[2];
    gIsDeletedIndex    = gCountingSearchRetrievalArray->indexes[3];
    gObjClassIndex     = gCountingSearchRetrievalArray->indexes[4];

    gSearchDntVal         = &gCountingSearchRetrievalArray->columnVals[gDntIndex];
    gSearchPDntVal        = &gCountingSearchRetrievalArray->columnVals[gPDntIndex];
    gSearchInstanceTypeVal= &gCountingSearchRetrievalArray->columnVals[gInstanceTypeIndex];
    gSearchIsDeletedVal   = &gCountingSearchRetrievalArray->columnVals[gIsDeletedIndex];
    gSearchObjClassVal    = &gCountingSearchRetrievalArray->columnVals[gObjClassIndex];

    if (gusnLow && gusnHigh) {
        gMetaDataIndex     = gCountingSearchRetrievalArray->indexes[5];
        gSearchMetaDataVal    = &gCountingSearchRetrievalArray->columnVals[gMetaDataIndex];
    } else {
        gSearchMetaDataVal = NULL;
    }

    // Link retrieval array

    result = DitCreateRetrievalArray(dbState,
                                     linkTableState,
                                     gCountingLinkColumnNames,
                                     NUM_COUNTING_LINK_COLUMN_NAMES,
                                     &gCountingLinkRetrievalArray);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    gLinkDntIndex          = gCountingLinkRetrievalArray->indexes[0];
    gLinkBaseIndex         = gCountingLinkRetrievalArray->indexes[1];
    gBackLinkDntIndex      = gCountingLinkRetrievalArray->indexes[2];
    gLinkMetaDataIndex     = gCountingLinkRetrievalArray->indexes[3];
    gLinkUsnChangedIndex   = gCountingLinkRetrievalArray->indexes[4];

    gLinkDntVal         = &gCountingLinkRetrievalArray->columnVals[gLinkDntIndex];
    gLinkBaseVal        = &gCountingLinkRetrievalArray->columnVals[gLinkBaseIndex];
    gBackLinkDntVal     = &gCountingLinkRetrievalArray->columnVals[gBackLinkDntIndex];
    gLinkMetaDataVal    = &gCountingLinkRetrievalArray->columnVals[gLinkMetaDataIndex];
    gLinkUsnChangedVal  = &gCountingLinkRetrievalArray->columnVals[gLinkUsnChangedIndex];

    gNumDigitsToPrint = MAX_DWORD_DIGITS;

    //"\nCounting records that need updating...\n");
    //"Records found: %0*u", gNumDigitsToPrint, 0);
    RESOURCE_PRINT2 (IDS_AR_RECORDS_UPDATE1, gNumDigitsToPrint, 0);

    gInUnfinishedLine = TRUE;

    gCurrentDisplayDelta = COUNTING_DISPLAY_DELTA;
    gUpdateSubrefList = FALSE;
    gRecordCount = 0;

    result = (*Traverse)(dbState,
                         tableState,
                         searchTableState,
                         linkTableState,
                         gCountingRetrievalArray,
                         gCountingSearchRetrievalArray,
                         gCountingLinkRetrievalArray,
                         CountRecord,
                         CountLink);

    if ( FAILED(result) ) {
        if ( *(DWORD*)gDntVal->pvData > 0 ) {
            //"Failed to update record with DNT %u.\n"
            errprintfRes(IDS_AR_ERR_FAILED_UPDATE_REC,
                         *(DWORD*)gDntVal->pvData);
        }
        returnValue = result;
        goto CleanUp;
    }

    UpdateProgressMeter(gRecordCount, TRUE);
    putchar('\n');
    gInUnfinishedLine = FALSE;

    //"Done.\n"
    RESOURCE_PRINT (IDS_DONE);

    //"\nFound %u records to update.\n"
    RESOURCE_PRINT1 (IDS_AR_RECORDS_UPDATE2, gRecordCount);

    result = DitDestroyRetrievalArray(&gCountingRetrievalArray);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    result = DitDestroyRetrievalArray(&gCountingSearchRetrievalArray);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    result = DitDestroyRetrievalArray(&gCountingLinkRetrievalArray);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }


    // Preallocate all of the USNs that we will need.

    result = DitPreallocateUsns(dbState, gRecordCount);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }


    // Now, make a second pass.  This time update the objects for real.

    // Main retrieval array

    result = DitCreateRetrievalArray(dbState,
                                     tableState,
                                     gMainColumnNames,
                                     NUM_MAIN_COLUMN_NAMES,
                                     &gMainRetrievalArray);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    gDntIndex           = gMainRetrievalArray->indexes[0];
    gPDntIndex          = gMainRetrievalArray->indexes[1];
    gInstanceTypeIndex  = gMainRetrievalArray->indexes[2];
    gIsDeletedIndex     = gMainRetrievalArray->indexes[3];
    gMetaDataIndex      = gMainRetrievalArray->indexes[4];
    gObjClassIndex      = gMainRetrievalArray->indexes[5];

    gDntVal           = &gMainRetrievalArray->columnVals[gDntIndex];
    gPDntVal          = &gMainRetrievalArray->columnVals[gPDntIndex];
    gInstanceTypeVal  = &gMainRetrievalArray->columnVals[gInstanceTypeIndex];
    gIsDeletedVal     = &gMainRetrievalArray->columnVals[gIsDeletedIndex];
    gMetaDataVal      = &gMainRetrievalArray->columnVals[gMetaDataIndex];
    gObjClassVal      = &gMainRetrievalArray->columnVals[gObjClassIndex];

    // Search retrieval array

    result = DitCreateRetrievalArray(dbState,
                                     searchTableState,
                                     gMainColumnNames,
                                     NUM_MAIN_COLUMN_NAMES,
                                     &gSearchRetrievalArray);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    gDntIndex           = gSearchRetrievalArray->indexes[0];
    gPDntIndex          = gSearchRetrievalArray->indexes[1];
    gInstanceTypeIndex  = gSearchRetrievalArray->indexes[2];
    gIsDeletedIndex     = gSearchRetrievalArray->indexes[3];
    gMetaDataIndex      = gSearchRetrievalArray->indexes[4];
    gObjClassIndex      = gSearchRetrievalArray->indexes[5];

    gSearchDntVal           = &gSearchRetrievalArray->columnVals[gDntIndex];
    gSearchPDntVal          = &gSearchRetrievalArray->columnVals[gPDntIndex];
    gSearchInstanceTypeVal  = &gSearchRetrievalArray->columnVals[gInstanceTypeIndex];
    gSearchIsDeletedVal     = &gSearchRetrievalArray->columnVals[gIsDeletedIndex];
    gSearchMetaDataVal      = &gSearchRetrievalArray->columnVals[gMetaDataIndex];
    gSearchObjClassVal      = &gSearchRetrievalArray->columnVals[gObjClassIndex];

    // Other column names

    result = DitGetColumnIdsByName(dbState,
                                   tableState,
                                   gOtherColumnNames,
                                   NUM_OTHER_COLUMN_NAMES,
                                   columnIds);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    gUsnChangedColumnId  = columnIds[0];
    gWhenChangedColumnId = columnIds[1];

    // Link retrieval array

    result = DitCreateRetrievalArray(dbState,
                                     linkTableState,
                                     gLinkColumnNames,
                                     NUM_LINK_COLUMN_NAMES,
                                     &gMainLinkRetrievalArray);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    gLinkDntIndex          = gMainLinkRetrievalArray->indexes[0];
    gLinkBaseIndex         = gMainLinkRetrievalArray->indexes[1];
    gBackLinkDntIndex      = gMainLinkRetrievalArray->indexes[2];
    gLinkMetaDataIndex     = gMainLinkRetrievalArray->indexes[3];
    gLinkDelTimeIndex      = gMainLinkRetrievalArray->indexes[4];
    gLinkUsnChangedIndex   = gMainLinkRetrievalArray->indexes[5];

    gLinkDntVal         = &gMainLinkRetrievalArray->columnVals[gLinkDntIndex];
    gLinkBaseVal        = &gMainLinkRetrievalArray->columnVals[gLinkBaseIndex];
    gBackLinkDntVal     = &gMainLinkRetrievalArray->columnVals[gBackLinkDntIndex];
    gLinkMetaDataVal    = &gMainLinkRetrievalArray->columnVals[gLinkMetaDataIndex];
    gLinkDelTimeVal     = &gMainLinkRetrievalArray->columnVals[gLinkDelTimeIndex];
    gLinkUsnChangedVal  = &gMainLinkRetrievalArray->columnVals[gLinkUsnChangedIndex];

    //"\nUpdating records...\n"
    RESOURCE_PRINT (IDS_AR_RECORDS_UPDATE3);

    //"Records remaining: %0*u"
    RESOURCE_PRINT2( IDS_AR_RECORDS_REMAIN, gNumDigitsToPrint, gRecordCount);

    gInUnfinishedLine = TRUE;

    gCurrentDisplayDelta = UPDATING_DISPLAY_DELTA;
    gUpdateSubrefList = TRUE;
    gRecordsUpdated = 0;

    result = (*Traverse)(dbState,
                         tableState,
                         searchTableState,
                         linkTableState,
                         gMainRetrievalArray,
                         gSearchRetrievalArray,
                         gMainLinkRetrievalArray,
                         AuthoritativeRestoreCurrentObject,
                         AuthoritativeRestoreCurrentLink );
    if ( FAILED(result) ) {
        if ( *(DWORD*)gDntVal->pvData > 0 ) {
            //"Failed to update record with DNT %u.\n"
            errprintfRes(IDS_AR_ERR_FAILED_UPDATE_REC,
                         *(DWORD*)gDntVal->pvData);
        }
        returnValue = result;
        goto CleanUp;
    }

    UpdateProgressMeter(gRecordsUpdated - gRecordCount, TRUE);
    putchar('\n');
    gInUnfinishedLine = FALSE;
    //"Done.\n"
    RESOURCE_PRINT (IDS_DONE);

    //"\nSuccessfully updated %u records.\n"
    RESOURCE_PRINT1 (IDS_AR_RECORDS_UPDATED, gRecordsUpdated);


    if ( gSubrefList != NULL ) {

        dnBufferSize = sizeof(WCHAR) * DEFAULT_DN_SIZE;
        result = ARAlloc(&dnBuffer, dnBufferSize);
        if ( FAILED(result) ) {
            returnValue = result;
            goto CleanUp;
        }

        //"\nThe following sub-NCs were not updated:\n"
        RESOURCE_PRINT (IDS_AR_RECORDS_NON_UPDATED);

        for ( i = 0; i < gSubrefListSize; i++ ) {

            result = DitGetDnFromDnt(dbState,
                                     tableState,
                                     gSubrefList[i],
                                     &dnBuffer,
                                     &dnBufferSize);
            if ( FAILED(result) ) {
                returnValue = result;
                goto CleanUp;
            } else if ( result == S_FALSE ) {
                //"Could not find subref %u in the database.\n"
                errprintfRes(IDS_AR_ERR_FIND_SUBREF,
                             gSubrefList[i]);
                returnValue = E_UNEXPECTED;
                goto CleanUp;
            }

            printf(" (%d) %S\n", i, dnBuffer);
        }
    }

    if ( gRestoredList != NULL ) {

        if ( dnBuffer != NULL ) {
            free(dnBuffer);
            dnBuffer = NULL;
        }

        dnBufferSize = sizeof(WCHAR) * DEFAULT_DN_SIZE;
        result = ARAlloc(&dnBuffer, dnBufferSize);
        if ( FAILED(result) ) {
            returnValue = result;
            goto CleanUp;
        }

        RESOURCE_PRINT (IDS_AR_RECORDS_UPDATED_BY_NAME);

        for ( i = 0; i < gRestoredListSize; i++ ) {

            result = DitGetDnFromDnt(dbState,
                                     tableState,
                                     gRestoredList[i],
                                     &dnBuffer,
                                     &dnBufferSize);
            if ( FAILED(result) ) {
                returnValue = result;
                goto CleanUp;
            } else if ( result == S_FALSE ) {
                //"Could not find subref %u in the database.\n"
                errprintfRes(IDS_AR_ERR_FIND_SUBREF,
                             gRestoredList[i]);
                returnValue = E_UNEXPECTED;
                goto CleanUp;
            }

            printf("%S\n", dnBuffer);
        }
    }


CleanUp:

    if ( SUCCEEDED(returnValue) ) {
        //"\nAuthoritative Restore completed successfully.\n\n"
        errprintfRes (IDS_AR_AUTH_RESTORE_COMPLETE);
    } else {
        //"\nAuthoritative Restore failed.\n\n"
        errprintfRes(IDS_AR_AUTH_RESTORE_FAIL);
    }

    if ( gSubrefList != NULL ) {
        free(gSubrefList);
        gSubrefList = NULL;
    }

    if ( gRestoredList != NULL ) {
        free(gRestoredList);
        gRestoredList = NULL;
    }

    if ( dnBuffer != NULL ) {
        free(dnBuffer);
        dnBuffer = NULL;
    }

    if ( gCountingRetrievalArray != NULL ) {
        result = DitDestroyRetrievalArray(&gCountingRetrievalArray);
        if ( FAILED(result) ) {
            if ( SUCCEEDED(returnValue) ) {
                returnValue = result;
            }
        }
    }

    if ( gCountingSearchRetrievalArray != NULL ) {
        result = DitDestroyRetrievalArray(&gCountingSearchRetrievalArray);
        if ( FAILED(result) ) {
            if ( SUCCEEDED(returnValue) ) {
                returnValue = result;
            }
        }
    }

    if ( gCountingLinkRetrievalArray != NULL ) {
        result = DitDestroyRetrievalArray(&gCountingLinkRetrievalArray);
        if ( FAILED(result) ) {
            if ( SUCCEEDED(returnValue) ) {
                returnValue = result;
            }
        }
    }

    if ( gMainRetrievalArray != NULL ) {
        result = DitDestroyRetrievalArray(&gMainRetrievalArray);
        if ( FAILED(result) ) {
            if ( SUCCEEDED(returnValue) ) {
                returnValue = result;
            }
        }
    }

    if ( gSearchRetrievalArray != NULL ) {
        result = DitDestroyRetrievalArray(&gSearchRetrievalArray);
        if ( FAILED(result) ) {
            if ( SUCCEEDED(returnValue) ) {
                returnValue = result;
            }
        }
    }

    if ( gMainLinkRetrievalArray != NULL ) {
        result = DitDestroyRetrievalArray(&gMainLinkRetrievalArray);
        if ( FAILED(result) ) {
            if ( SUCCEEDED(returnValue) ) {
                returnValue = result;
            }
        }
    }

    if ( tableState != NULL ) {
        result = DitCloseTable(dbState, &tableState);
        if ( FAILED(result) ) {
            if ( SUCCEEDED(returnValue) ) {
                returnValue = result;
            }
        }
    }

    if ( searchTableState != NULL ) {
        result = DitCloseTable(dbState, &searchTableState);
        if ( FAILED(result) ) {
            if ( SUCCEEDED(returnValue) ) {
                returnValue = result;
            }
        }
    }

    if ( linkTableState != NULL ) {
        result = DitCloseTable(dbState, &linkTableState);
        if ( FAILED(result) ) {
            if ( SUCCEEDED(returnValue) ) {
                returnValue = result;
            }
        }
    }

    if ( dbState != NULL ) {
        result = DitCloseDatabase(&dbState);
        if ( FAILED(result) ) {
            if ( SUCCEEDED(returnValue) ) {
                returnValue = result;
            }
        }
    }

    if ( gSubrefList != NULL ) {
        free(gSubrefList);
        gSubrefList = NULL;
    }

    return returnValue;

} // AuthoritativeRestore


BOOL
ShouldObjectBeRestored(
    JET_RETRIEVECOLUMN *pDntVal,
    JET_RETRIEVECOLUMN *pPDntVal,
    JET_RETRIEVECOLUMN *pInstanceTypeVal,
    JET_RETRIEVECOLUMN *pIsDeletedVal,
    JET_RETRIEVECOLUMN *pMetaDataVal,
    JET_RETRIEVECOLUMN *pObjClassVal
    )

/*++

Routine Description:

    Test with the current object should be restored

    // we only update objects with the following properties:
    //
    // Writeable: users shouldn't be allowed to restore objects they are not
    // allowed to write.
    //
    // Not Deleted: if this object has been restored (by the system), 99% of
    // the time, it will just be restored again if we delete it again, so why
    // bother.
    //
    // LostAndFound -- replication protocol assumes this is the first object
    // replicated in in the NC.  This object can't be modified elsewhere in
    // the system, so no need to authoritatively restore it.

Arguments:

    JET_RETRIEVECOLUMN *pDntVal,
    JET_RETRIEVECOLUMN *pPDntVal,
    JET_RETRIEVECOLUMN *pInstanceTypeVal,
    JET_RETRIEVECOLUMN *pIsDeletedVal,
    JET_RETRIEVECOLUMN *pMetaDataVal,
    JET_RETRIEVECOLUMN *pObjClassVal,

Return Value:

    Boolean

--*/

{
    if ( (pIsDeletedVal->err == JET_wrnColumnNull) &&
         (*(SYNTAX_INTEGER*)pInstanceTypeVal->pvData & IT_WRITE) &&
         (*(DWORD*)pDntVal->pvData != gSchemaDnt) &&
         (*(DWORD*)pPDntVal->pvData != gSchemaDnt) &&
         (*(ATTRTYP*)pObjClassVal->pvData != CLASS_LOST_AND_FOUND) ) {
        return TRUE;
    }

    return FALSE;
}



HRESULT
TraverseLinksSingleDirection(
    IN DB_STATE *DbState,
    IN DWORD Dnt,
    IN TABLE_STATE *SearchTableState,
    IN RETRIEVAL_ARRAY *SearchRetrievalArray,
    IN TABLE_STATE *LinkTableState,
    IN RETRIEVAL_ARRAY *LinkRetrievalArray,
    IN BOOL fDirectionForward,
    IN VISIT_LINK_FUNCTION LinkVisit
    )
/*++

Routine Description:

    This function traverses through all of the links under the named
    object and calls the function Visit to process them.

    Note that this function may change the default index of the link table.

Arguments:

    DbState - Supplies the state of the opened DIT database.
    Dnt - Dnt of containing object for the links
    SearchTableState - Supplies the state of the search table.
    SearchRetrievalArray - The retrieval array to be used with the search table
    LinkTableState - Supplies the state of the opened DIT table.
    LinkRetrievalArray - Supplies the retrieval array which needs to be filled for
        this Visit function.
    fDirectionForward - Whether we are visiting forward links for backward links
    LinkVisit - Supplies the function which will be called to process each record
        visitted.

Return Value:

    S_OK - The record was modified successfully.
    E_INVALIDARG - One of the given pointers was NULL.
    E_OUTOFMEMORY - Not enough memory to allocate buffer.
    E_UNEXPECTED - Some variety of unexpected error occured.

--*/
{

    HRESULT returnValue = S_OK;
    HRESULT result;
    JET_ERR jetResult;
    DWORD linkObjectDnt, linkValueDnt;

    if ( (DbState == NULL) ||
         (LinkTableState == NULL) ||
         (LinkVisit == NULL) ) {
        ASSERT(FALSE);
        returnValue = E_INVALIDARG;
        goto CleanUp;
    }

    // Change to proper index
    // Note that this call does nothing if we are already on the right index
    result = DitSetIndex(DbState,
                         LinkTableState,
                         fDirectionForward ? SZLINKALLINDEX : SZBACKLINKALLINDEX,
                         TRUE);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    // Construct a search key that matches on the first segment dnt.  Note that we
    // do not create a second segment to match on link base, since we want all
    // links for a given object and don't care about which attribute they are.
    jetResult = JetMakeKey(DbState->sessionId,
                           LinkTableState->tableId,
                           &Dnt,
                           sizeof(Dnt),
                           JET_bitNewKey);
    if ( jetResult != JET_errSuccess ) {
        //"Could not move cursor in DIT database: %ws.\n"
        errprintfRes(IDS_AR_ERR_MOVE_CURSOR_DIT,
                  GetJetErrString(jetResult));
        returnValue = E_UNEXPECTED;
        goto CleanUp;
    }

    // find first matching record
    jetResult = JetSeek(DbState->sessionId,
                        LinkTableState->tableId,
                        JET_bitSeekGE);

    if ((jetResult != JET_errSuccess) && (jetResult != JET_wrnSeekNotEqual)) {
        // no records
        return S_OK;
    }

    jetResult = JET_errSuccess;

    while ( jetResult == JET_errSuccess ) {

        // Read the record
        result = DitGetColumnValues(DbState,
                                    LinkTableState,
                                    LinkRetrievalArray);
        if ( FAILED(result) ) {
            returnValue = result;
            goto CleanUp;
        }

        if (fDirectionForward) {
            linkObjectDnt = *(DWORD*)gLinkDntVal->pvData;
            linkValueDnt = *(DWORD*)gBackLinkDntVal->pvData;
        } else {
            linkValueDnt = *(DWORD*)gLinkDntVal->pvData;
            linkObjectDnt = *(DWORD*)gBackLinkDntVal->pvData;
        }

        if (Dnt != linkObjectDnt ) {
            // Moved beyond current object
            return S_OK;
        }

        // common filtering logic

        // Only count values with metadata. Ignore legacy values.
        if (gLinkMetaDataVal->cbActual == 0) {
            goto next_iteration;
        }

        // If USN range limiting, filter on USN
        if (gusnLow && gusnHigh) {
            if (gLinkUsnChangedVal->cbActual == 0) {
                goto next_iteration;
            }
            Assert( gLinkUsnChangedVal->cbData == sizeof( USN ) );

            if ( ( *((USN *)(gLinkUsnChangedVal->pvData)) < gusnLow ) ||
                 ( *((USN *)(gLinkUsnChangedVal->pvData)) > gusnHigh ) ) {
                goto next_iteration;
            }
        }

        // If backlink, check that owning object is eligible
        if (!fDirectionForward) {
            DWORD owningDnt = *(DWORD*)gLinkDntVal->pvData;
            Assert( owningDnt );

            result = DitSeekToDnt(DbState, SearchTableState, owningDnt);
            if ( FAILED(result) ) {
                returnValue = result;
                goto CleanUp;
            }

            result = DitGetColumnValues(DbState, SearchTableState, SearchRetrievalArray);
            if ( FAILED(result) ) {
                returnValue = result;
                goto CleanUp;
            }

            if (!ShouldObjectBeRestored( gSearchDntVal, gSearchPDntVal,
                                         gSearchInstanceTypeVal, gSearchIsDeletedVal,
                                         gSearchMetaDataVal, gSearchObjClassVal) ) {

                goto next_iteration;
            }
        }

        result = (*LinkVisit)( DbState, LinkTableState, fDirectionForward);
        if ( FAILED(result) ) {
            returnValue = result;
            goto CleanUp;
        }

    next_iteration:

        jetResult = JetMove(DbState->sessionId,
                            LinkTableState->tableId,
                            JET_MoveNext,
                            0);

    }

    if ( jetResult != JET_errNoCurrentRecord ) {
        //"Could not move cursor in DIT database: %ws.\n"
        errprintfRes(IDS_AR_ERR_MOVE_CURSOR_DIT,
                  GetJetErrString(jetResult));
        returnValue = E_UNEXPECTED;
        goto CleanUp;
    }


CleanUp:

    return returnValue;

} // TraverseDit



HRESULT
TraverseDit(
    IN DB_STATE *DbState,
    IN TABLE_STATE *TableState,
    IN TABLE_STATE *SearchTableState,
    IN TABLE_STATE *LinkTableState,
    IN RETRIEVAL_ARRAY *RetrievalArray,
    IN RETRIEVAL_ARRAY *SearchRetrievalArray,
    IN RETRIEVAL_ARRAY *LinkRetrievalArray,
    IN VISIT_FUNCTION Visit,
    IN VISIT_LINK_FUNCTION LinkVisit
    )
/*++

Routine Description:

    This function traverses through all of the objects in the DIT and calls
    the function Visit to process them.

Arguments:

    DbState - Supplies the state of the opened DIT database.
    TableState - Supplies the state of the opened DIT table.
    LinkTableState - Supplies the state of the opened DIT table.
    RetrievalArray - Supplies the retrieval array which needs to be filled for
        this Visit function.
    LinkRetrievalArray - Supplies the retrieval array which needs to be filled for
        this Visit function.
    Visit - Supplies the function which will be called to process each record
        visited.
    LinkVisit - Supplies the function which will be called to process each record
        visited.

Return Value:

    S_OK - The record was modified successfully.
    E_INVALIDARG - One of the given pointers was NULL.
    E_OUTOFMEMORY - Not enough memory to allocate buffer.
    E_UNEXPECTED - Some variety of unexpected error occured.

--*/
{

    HRESULT returnValue = S_OK;
    HRESULT result;
    JET_ERR jetResult;
    ULONG i;


    if ( (DbState == NULL) ||
         (TableState == NULL) ||
         (LinkTableState == NULL) ||
         (LinkVisit == NULL) ||
         (Visit == NULL) ) {
        ASSERT(FALSE);
        returnValue = E_INVALIDARG;
        goto CleanUp;
    }

    jetResult = JetMove(DbState->sessionId,
                        TableState->tableId,
                        JET_MoveFirst,
                        0);

    while ( jetResult == JET_errSuccess ) {

        result = (*Visit)(DbState, TableState, FALSE);
        if ( FAILED(result) ) {
            returnValue = result;
            goto CleanUp;
        }

        result = TraverseLinksSingleDirection(
            DbState,
            *(DWORD*)gDntVal->pvData,
            SearchTableState,
            SearchRetrievalArray,
            LinkTableState,
            LinkRetrievalArray,
            TRUE, // forward links
            LinkVisit );
        if ( FAILED(result) ) {
            returnValue = result;
            goto CleanUp;
        }

        result = TraverseLinksSingleDirection(
            DbState,
            *(DWORD*)gDntVal->pvData,
            SearchTableState,
            SearchRetrievalArray,
            LinkTableState,
            LinkRetrievalArray,
            FALSE, // backward links
            LinkVisit );
        if ( FAILED(result) ) {
            returnValue = result;
            goto CleanUp;
        }

        jetResult = JetMove(DbState->sessionId,
                            TableState->tableId,
                            JET_MoveNext,
                            0);

    }

    if ( jetResult != JET_errNoCurrentRecord ) {
        //"Could not move cursor in DIT database: %ws.\n"
        errprintfRes(IDS_AR_ERR_MOVE_CURSOR_DIT,
                  GetJetErrString(jetResult));
        returnValue = E_UNEXPECTED;
        goto CleanUp;
    }


CleanUp:

    return returnValue;

} // TraverseDit



HRESULT
TraverseSubtree(
    IN DB_STATE *DbState,
    IN TABLE_STATE *TableState,
    IN TABLE_STATE *SearchTableState,
    IN TABLE_STATE *LinkTableState,
    IN RETRIEVAL_ARRAY *RetrievalArray,
    IN RETRIEVAL_ARRAY *SearchRetrievalArray,
    IN RETRIEVAL_ARRAY *LinkRetrievalArray,
    IN VISIT_FUNCTION Visit,
    IN VISIT_LINK_FUNCTION LinkVisit
    )
/*++

Routine Description:

    This function traverses through all of the objects in the subtree rooted
    at the current object and calls the function Visit to process them.

Arguments:

    DbState - Supplies the state of the opened DIT database.
    TableState - Supplies the state of the opened DIT table.
    LinkTableState - Supplies the state of the opened DIT table.
    RetrievalArray - Supplies the retrieval array which needs to be filled for
        this Visit function.
    LinkRetrievalArray - Supplies the retrieval array which needs to be filled for
        this Visit function.
    Visit - Supplies the function which will be called to process each record
        visitted.
    LinkVisit - Supplies the function which will be called to process each record
        visited.

Return Value:

    S_OK - The record was modified successfully.
    E_INVALIDARG - One of the given pointers was NULL.
    E_OUTOFMEMORY - Not enough memory to allocate buffer.
    E_UNEXPECTED - Some variety of unexpected error occured.

--*/
{

    HRESULT returnValue = S_OK;
    HRESULT result;
    JET_ERR jetResult;


    if ( (DbState == NULL) ||
         (TableState == NULL) ||
         (Visit == NULL) ) {
        ASSERT(FALSE);
        returnValue = E_INVALIDARG;
        goto CleanUp;
    }

    result = DitSetIndex(DbState, TableState, SZPDNTINDEX, TRUE);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    result = DitSeekToDn(DbState, TableState, gSubtreeRoot);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    result = TraverseSubtreeRecursive(DbState,
                                      TableState,
                                      SearchTableState,
                                      LinkTableState,
                                      RetrievalArray,
                                      SearchRetrievalArray,
                                      LinkRetrievalArray,
                                      Visit,
                                      LinkVisit,
                                      TRUE);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

CleanUp:

    return returnValue;

} // TraverseSubtree



HRESULT
TraverseSubtreeRecursive(
    IN DB_STATE *DbState,
    IN TABLE_STATE *TableState,
    IN TABLE_STATE *SearchTableState,
    IN TABLE_STATE *LinkTableState,
    IN RETRIEVAL_ARRAY *RetrievalArray,
    IN RETRIEVAL_ARRAY *SearchRetrievalArray,
    IN RETRIEVAL_ARRAY *LinkRetrievalArray,
    IN VISIT_FUNCTION Visit,
    IN VISIT_LINK_FUNCTION LinkVisit,
    IN BOOL SubtreeRoot
    )
/*++

Routine Description:

    This function finishes the work of TraverseSubtree by recursively visiting
    the records in the subtree rooted at this object.

Arguments:

    DbState - Supplies the state of the opened DIT database.
    TableState - Supplies the state of the opened DIT table.
    LinkTableState - Supplies the state of the opened DIT table.
    RetrievalArray - Supplies the retrieval array which needs to be filled for
        this Visit function.
    LinkRetrievalArray - Supplies the retrieval array which needs to be filled for
        this Visit function.
    Visit - Supplies the function which will be called to process each record
        visited.
    LinkVisit - Supplies the function which will be called to process each record
        visited.
    SubtreeRoot - Supplies whether this call is being made on the root of the
        subtree (this is a slightly special case).

Return Value:

    S_OK - The record was modified successfully.
    E_INVALIDARG - One of the given pointers was NULL.
    E_OUTOFMEMORY - Not enough memory to allocate buffer.
    E_UNEXPECTED - Some variety of unexpected error occured.

--*/
{

    HRESULT returnValue = S_OK;
    HRESULT result;
    JET_ERR jetResult;

    DWORD size;
    DWORD pDnt;
    DWORD *newSubrefList;
    BOOL moveFirst = FALSE;  // we only need to move first when there is no
                             // current record


    if ( (DbState == NULL) ||
         (TableState == NULL) ||
         (Visit == NULL) ) {
        ASSERT(FALSE);
        returnValue = E_INVALIDARG;
        goto CleanUp;
    }

    result = DitGetColumnValues(DbState, TableState, RetrievalArray);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    pDnt = *(DWORD*)gDntVal->pvData;

    if ( (*(SYNTAX_INTEGER*)gInstanceTypeVal->pvData & IT_NC_HEAD) &&
         (!SubtreeRoot) ) {

        if ( gUpdateSubrefList ) {

            if ( gSubrefList == NULL ) {

                gSubrefListMaxSize = DEFAULT_SUBREF_LIST_SIZE;

                result = ARAlloc(&gSubrefList,
                                 gSubrefListMaxSize * sizeof(DWORD));
                if ( FAILED(result) ) {
                    returnValue = result;
                    goto CleanUp;
                }

            } else if ( gSubrefListSize == gSubrefListMaxSize ) {

                size = gSubrefListMaxSize * sizeof(DWORD);

                result = ARRealloc(&gSubrefList, &size);
                if ( FAILED(result) ) {
                    returnValue = result;
                    goto CleanUp;
                }

                gSubrefListMaxSize *= 2;
            }

            gSubrefList[gSubrefListSize] = *(DWORD*)gDntVal->pvData;
            gSubrefListSize++;

        }

        goto CleanUp;

    }

    result = (*Visit)(DbState, TableState, TRUE);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    result = TraverseLinksSingleDirection(
        DbState,
        *(DWORD*)gDntVal->pvData,
        SearchTableState,
        SearchRetrievalArray,
        LinkTableState,
        LinkRetrievalArray,
        TRUE, // forward links
        LinkVisit );
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    result = TraverseLinksSingleDirection(
        DbState,
        *(DWORD*)gDntVal->pvData,
        SearchTableState,
        SearchRetrievalArray,
        LinkTableState,
        LinkRetrievalArray,
        FALSE, // backward links
        LinkVisit );
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    result = DitSetIndex(DbState, TableState, SZPDNTINDEX, FALSE);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    result = DitSeekToFirstChild(DbState, TableState, pDnt);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    } else if ( result == S_FALSE ) {
        // there aren't any records at all, so just skip to the bottom
        moveFirst = TRUE;
        goto RestoreCursor;
    }

    result = DitGetColumnValues(DbState, TableState, RetrievalArray);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    jetResult = JET_errSuccess;

    while ( (jetResult == JET_errSuccess) &&
            (*(DWORD*)gPDntVal->pvData == pDnt) ) {

        result = TraverseSubtreeRecursive(DbState,
                                          TableState,
                                          SearchTableState,
                                          LinkTableState,
                                          RetrievalArray,
                                          SearchRetrievalArray,
                                          LinkRetrievalArray,
                                          Visit,
                                          LinkVisit,
                                          FALSE);
        if ( FAILED(result) ) {
            returnValue = result;
            goto CleanUp;
        }

        jetResult = JetMove(DbState->sessionId,
                            TableState->tableId,
                            JET_MoveNext,
                            0);
        if ( jetResult == JET_errNoCurrentRecord ) {

            moveFirst = TRUE;
            break;

        } else if ( jetResult != JET_errSuccess ) {

            //"Could not move in \"%hs\" table: %ws.\n"
            errprintfRes(IDS_AR_ERR_MOVE_IN_TABLE,
                      TableState->tableName,
                      GetJetErrString(jetResult));
            returnValue = E_UNEXPECTED;
            goto CleanUp;

        }

        result = DitGetColumnValues(DbState, TableState, RetrievalArray);
        if ( FAILED(result) ) {
            returnValue = result;
            goto CleanUp;
        } else if ( result == S_FALSE ) {
            break;
        }

    }

RestoreCursor:

    result = DitSetIndex(DbState, TableState, SZDNTINDEX, TRUE);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    result = DitSeekToDnt(DbState, TableState, pDnt);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    result = DitSetIndex(DbState, TableState, SZPDNTINDEX, FALSE);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }


CleanUp:

    return returnValue;

} // TraverseSubtreeRecursive


HRESULT
TraverseObject(
    IN DB_STATE *DbState,
    IN TABLE_STATE *TableState,
    IN TABLE_STATE *SearchTableState,
    IN TABLE_STATE *LinkTableState,
    IN RETRIEVAL_ARRAY *RetrievalArray,
    IN RETRIEVAL_ARRAY *SearchRetrievalArray,
    IN RETRIEVAL_ARRAY *LinkRetrievalArray,
    IN VISIT_FUNCTION Visit,
    IN VISIT_LINK_FUNCTION LinkVisit
    )
/*++

Routine Description:

    This function finds the given object in the database,
    and call function Visit to handle it.

Arguments:

    DbState - Supplies the state of the opened DIT database.
    TableState - Supplies the state of the opened DIT table.
    LinkTableState - Supplies the state of the opened DIT table.
    RetrievalArray - Supplies the retrieval array which needs to be filled for
        this Visit function.
    LinkRetrievalArray - Supplies the retrieval array which needs to be filled for
        this Visit function.
    Visit - Supplies the function which will be called to process each record
        visited.
    LinkVisit - Supplies the function which will be called to process each record
        visited.

Return Value:

    S_OK - The record was modified successfully.
    E_INVALIDARG - One of the given pointers was NULL.
    E_OUTOFMEMORY - Not enough memory to allocate buffer.
    E_UNEXPECTED - Some variety of unexpected error occured.

--*/
{

    HRESULT returnValue = S_OK;
    HRESULT result;

    if ( (DbState == NULL) ||
         (TableState == NULL) ||
         (Visit == NULL) ) {
        ASSERT(FALSE);
        returnValue = E_INVALIDARG;
        goto CleanUp;
    }

    result = DitSeekToDn(DbState, TableState, gSubtreeRoot);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    result = DitGetColumnValues(DbState, TableState, RetrievalArray);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }


    result = (*Visit)(DbState, TableState, TRUE);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    result = TraverseLinksSingleDirection(
        DbState,
        *(DWORD*)gDntVal->pvData,
        SearchTableState,
        SearchRetrievalArray,
        LinkTableState,
        LinkRetrievalArray,
        TRUE, // forward links
        LinkVisit );
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    result = TraverseLinksSingleDirection(
        DbState,
        *(DWORD*)gDntVal->pvData,
        SearchTableState,
        SearchRetrievalArray,
        LinkTableState,
        LinkRetrievalArray,
        FALSE, // backward links
        LinkVisit );
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

CleanUp:

    return returnValue;

} // TraverseObject


HRESULT
CountRecord(
    IN DB_STATE *DbState,
    IN TABLE_STATE *TableState,
    IN BOOL AlreadyFilledRetrievalArray
    )
/*++

Routine Description:

    This function increments the global variable gRecordCount.

Arguments:

    DbState - Supplies the state of the opened DIT database.
    TableState - Supplies the state of the opened DIT table.
    AlreadyFilledRetrievalArray - Supplies a boolean telling whether the
        gMainRetrievalArray has already been filled with the information for
        this record.

Return Value:

    S_OK - The record was modified successfully.
    E_UNEXPECTED - Some variety of unexpected error occured.

--*/
{

    HRESULT returnValue = S_OK;
    HRESULT result;
    JET_ERR jetResult;

    DWORD i;


    if ( !AlreadyFilledRetrievalArray ) {

        result = DitGetColumnValues(DbState,
                                    TableState,
                                    gCountingRetrievalArray);
        if ( FAILED(result) ) {
            returnValue = result;
            goto CleanUp;
        }

    }

    // If USN range limits are active, we only count an object if any of
    // its attributes have been locally modified in the given USN range

    if (gusnLow && gusnHigh) {
        PROPERTY_META_DATA_VECTOR *pMetaDataVector = NULL;
        DWORD i;

        // If record doesn't have any metadata, skip
        if (gMetaDataVal->cbActual == 0) {
            goto CleanUp;
        }

        // Incoming metadata should be valid
        VALIDATE_META_DATA_VECTOR_VERSION(((PROPERTY_META_DATA_VECTOR *)(gMetaDataVal->pvData)));
        ASSERT( gMetaDataVal->cbData >=
                MetaDataVecV1Size(((PROPERTY_META_DATA_VECTOR *)(gMetaDataVal->pvData))) );
        pMetaDataVector = (PROPERTY_META_DATA_VECTOR*) gMetaDataVal->pvData;

        for ( i = 0; i < pMetaDataVector->V1.cNumProps; i++ ) {
            if ( (pMetaDataVector->V1.rgMetaData[i].usnProperty >= gusnLow) &&
                 (pMetaDataVector->V1.rgMetaData[i].usnProperty <= gusnHigh) ) {
                break;
            }
        }
        if (i == pMetaDataVector->V1.cNumProps) {
            goto CleanUp;
        }
    } else {
        // Because of the expense, gMetaDataVal is not read, during counting,
        // except when we are using USN range limits. Verify this.
        Assert( gMetaDataVal == NULL );
    }

    if (ShouldObjectBeRestored( gDntVal, gPDntVal, gInstanceTypeVal,
                                gIsDeletedVal, gMetaDataVal, gObjClassVal) ) {

        gRecordCount++;

        UpdateProgressMeter(gRecordCount, FALSE);

    }


CleanUp:

    return returnValue;

} // CountRecord


HRESULT
CountLink(
    IN DB_STATE *DbState,
    IN TABLE_STATE *LinkTableState,
    IN BOOL fDirectionForward
    )
/*++

Routine Description:

    This function increments the global variable gRecordCount.

Arguments:

    DbState - Supplies the state of the opened DIT database.
    LinkTableState - Supplies the state of the opened DIT table.
    fDirectionForward - Whether this is a forward link or a backward link.
        Use this information to determine the two ends of the link

Return Value:

    S_OK - The record was modified successfully.
    E_UNEXPECTED - Some variety of unexpected error occured.

--*/
{

    HRESULT returnValue = S_OK;

    gRecordCount++;

    UpdateProgressMeter(gRecordCount, FALSE);

    return returnValue;

} // CountLink



HRESULT
AuthoritativeRestoreCurrentObject(
    IN DB_STATE *DbState,
    IN TABLE_STATE *TableState,
    IN BOOL AlreadyFilledRetrievalArray
    )
/*++

Routine Description:

    If the current record is writeable and non-deleted, this function updates
    its meta-data so that it appears to have been written to its current
    value at the given time at the given DC.

Arguments:

    DbState - Supplies the state of the opened DIT database.
    TableState - Supplies the state of the opened DIT table.
    AlreadyFilledRetrievalArray - Supplies a boolean telling whether the
        gMainRetrievalArray has already been filled with the information for
        this record.

Return Value:

    S_OK - The record was modified successfully.
    S_FALSE - The record was deleted or not writeable
    E_OUTOFMEMORY - Not enough memory to allocate buffer.
    E_UNEXPECTED - Some variety of unexpected error occured.

--*/
{

    HRESULT returnValue = S_FALSE;
    HRESULT result;
    JET_ERR jetResult;

    DWORD i;
    BOOL inTransaction = FALSE;
    CHAR displayTime[SZDSTIME_LEN+1];
    SYNTAX_INTEGER instanceType;
    USN nextUsn;
    PROPERTY_META_DATA_VECTOR *pMetaDataVector = NULL;

    if ( !AlreadyFilledRetrievalArray ) {

        result = DitGetColumnValues(DbState, TableState, gMainRetrievalArray);
        if ( FAILED(result) ) {
            returnValue = result;
            goto CleanUp;
        }
    }

    // If USN range limits are active, we only count an object if any of
    // its attributes have been locally modified in the given USN range

    if (gusnLow && gusnHigh) {
        PROPERTY_META_DATA_VECTOR *pMetaDataVector = NULL;
        DWORD i;

        // If record doesn't have any metadata, skip
        if (gMetaDataVal->cbActual == 0) {
            goto CleanUp;
        }

        // Incoming metadata should be valid
        VALIDATE_META_DATA_VECTOR_VERSION(((PROPERTY_META_DATA_VECTOR *)(gMetaDataVal->pvData)));
        ASSERT( gMetaDataVal->cbData >=
                MetaDataVecV1Size(((PROPERTY_META_DATA_VECTOR *)(gMetaDataVal->pvData))) );
        pMetaDataVector = (PROPERTY_META_DATA_VECTOR*) gMetaDataVal->pvData;

        for ( i = 0; i < pMetaDataVector->V1.cNumProps; i++ ) {
            if ( (pMetaDataVector->V1.rgMetaData[i].usnProperty >= gusnLow) &&
                 (pMetaDataVector->V1.rgMetaData[i].usnProperty <= gusnHigh) ) {
                break;
            }
        }
        if (i == pMetaDataVector->V1.cNumProps) {
            goto CleanUp;
        }
    }

    // we only update objects with the following properties:
    //
    // Writeable: users shouldn't be allowed to restore objects they are not
    // allowed to write.
    //
    // Not Deleted: if this object has been restored (by the system), 99% of
    // the time, it will just be restored again if we delete it again, so why
    // bother.
    //
    // LostAndFound -- replication protocol assumes this is the first object
    // replicated in in the NC.  This object can't be modified elsewhere in
    // the system, so no need to authoritatively restore it.

    if (ShouldObjectBeRestored( gDntVal, gPDntVal, gInstanceTypeVal,
                                gIsDeletedVal, gMetaDataVal, gObjClassVal) ) {

        // Incoming metadata should be valid
        VALIDATE_META_DATA_VECTOR_VERSION(((PROPERTY_META_DATA_VECTOR *)(gMetaDataVal->pvData)));
        ASSERT( gMetaDataVal->cbData >=
                MetaDataVecV1Size(((PROPERTY_META_DATA_VECTOR *)(gMetaDataVal->pvData))) );
        // gMetaDataVal->cbActual is not defined at this time

        returnValue = S_OK;

        jetResult = JetBeginTransaction(DbState->sessionId);
        if ( jetResult != JET_errSuccess ) {
            // "Could not start a new transaction: %ws.\n"
            errprintfRes(IDS_AR_ERR_START_TRANS,
                      GetJetErrString(jetResult));
            returnValue = E_UNEXPECTED;
            goto CleanUp;
        }
        inTransaction = TRUE;

        jetResult = JetPrepareUpdate(DbState->sessionId,
                                     TableState->tableId,
                                     JET_prepReplace);
        if ( jetResult != JET_errSuccess ) {
            //"Could not prepare update in \"%s\" table: %ws.\n"
            errprintfRes(IDS_AR_ERR_PREPARE_UPDATE,
                      TableState->tableName,
                      GetJetErrString(jetResult));
            returnValue = E_UNEXPECTED;
            goto CleanUp;
        }

        // allocate a USN

        result = DitGetNewUsn(DbState, &nextUsn);
        if ( FAILED(result) ) {
            returnValue = result;
            goto CleanUp;
        }

        // set the USN-Changed attribute

        jetResult = JetSetColumn(DbState->sessionId,
                                 TableState->tableId,
                                 gUsnChangedColumnId,
                                 &nextUsn,
                                 sizeof(nextUsn),
                                 0,
                                 NULL);
        if ( jetResult != JET_errSuccess ) {
            //"Could not set Usn-Changed column in \"%s\" table: %S.\n"
            errprintfRes(IDS_AR_ERR_SET_USN_CHANGED,
                      TableState->tableName,
                      GetJetErrString(jetResult));
            returnValue = E_UNEXPECTED;
            goto CleanUp;
        }

        // set the When-Changed attribute

        jetResult = JetSetColumn(DbState->sessionId,
                                 TableState->tableId,
                                 gWhenChangedColumnId,
                                 &gCurrentTime,
                                 sizeof(gCurrentTime),
                                 0,
                                 NULL);
        if ( jetResult != JET_errSuccess ) {
            //"Could not set When-Changed column in \"%s\" table: %S.\n"
            errprintfRes(IDS_AR_ERR_SET_WHEN_CHANGED,
                      TableState->tableName,
                      GetJetErrString(jetResult));
            returnValue = E_UNEXPECTED;
            goto CleanUp;
        }

        // insert an entry for isDeleted if one is not already present
        // Note that gMetaDataVal may be re-allocated as a result of this call
        result = MetaDataInsert(ATT_IS_DELETED,
                                (PROPERTY_META_DATA_VECTOR**)
                                &gMetaDataVal->pvData,
                                &gMetaDataVal->cbData);
        if ( FAILED(result) ) {
            returnValue = result;
            goto CleanUp;
        }

        VALIDATE_META_DATA_VECTOR_VERSION(((PROPERTY_META_DATA_VECTOR *)(gMetaDataVal->pvData)));
        ASSERT( gMetaDataVal->cbData >=
                MetaDataVecV1Size(((PROPERTY_META_DATA_VECTOR *)(gMetaDataVal->pvData))) );

        // munge the meta data entries

        pMetaDataVector = (PROPERTY_META_DATA_VECTOR*) gMetaDataVal->pvData;

        for ( i = 0; i < pMetaDataVector->V1.cNumProps; i++ ) {

            switch ( pMetaDataVector->V1.rgMetaData[i].attrType ) {

            case ATT_WHEN_CREATED:
            case ATT_RID_ALLOCATION_POOL:
            case ATT_RID_PREVIOUS_ALLOCATION_POOL:
            case ATT_RID_AVAILABLE_POOL:
            case ATT_RID_USED_POOL:
            case ATT_RID_NEXT_RID:
            case ATT_FSMO_ROLE_OWNER:
            case ATT_NT_MIXED_DOMAIN:
            case ATT_MS_DS_BEHAVIOR_VERSION:
                /* do not update these */
                break;

            case ATT_RDN:
                /* skip the RDN of uninstantiated NC heads */
                instanceType = *(SYNTAX_INTEGER*)gInstanceTypeVal->pvData;
                if ( (instanceType & IT_NC_HEAD) &&
                     !(instanceType & IT_UNINSTANT) )
                    break;

            default:
                pMetaDataVector->V1.rgMetaData[i].dwVersion +=
                    gVersionIncrease;
                pMetaDataVector->V1.rgMetaData[i].timeChanged = gCurrentTime;
                pMetaDataVector->V1.rgMetaData[i].uuidDsaOriginating =
                    gDatabaseGuid;
                pMetaDataVector->V1.rgMetaData[i].usnOriginating = nextUsn;
                pMetaDataVector->V1.rgMetaData[i].usnProperty = nextUsn;
                break;

            }

        }

        // Set the actual size of the vector
        gMetaDataVal->cbActual = MetaDataVecV1Size(((PROPERTY_META_DATA_VECTOR *)(gMetaDataVal->pvData)));

        // Check metadata before writing
        VALIDATE_META_DATA_VECTOR_VERSION(((PROPERTY_META_DATA_VECTOR *)(gMetaDataVal->pvData)));
        // set the metadata attribute
        jetResult = JetSetColumn(DbState->sessionId,
                                 TableState->tableId,
                                 gMetaDataVal->columnid,
                                 gMetaDataVal->pvData,
                                 gMetaDataVal->cbActual,
                                 JET_bitSetOverwriteLV,
                                 NULL);
        if ( jetResult != JET_errSuccess ) {
            //"Could not set meta-data column in \"%s\" table: %S.\n"
            errprintfRes(IDS_AR_ERR_SET_METADATA,
                      TableState->tableName,
                      GetJetErrString(jetResult));
            returnValue = E_UNEXPECTED;
            goto CleanUp;
        }

        jetResult = JetUpdate(DbState->sessionId,
                              TableState->tableId,
                              NULL,
                              0,
                              0);
        if ( jetResult != JET_errSuccess ) {
            //"Could not update column in \"%s\" table: %S.\n"
            errprintfRes(IDS_AR_ERR_UPDATE_COLUMN,
                      TableState->tableName,
                      GetJetErrString(jetResult));
            returnValue = E_UNEXPECTED;
            goto CleanUp;
        }

        jetResult = JetCommitTransaction(DbState->sessionId,
                                         JET_bitCommitLazyFlush);
        if ( jetResult != JET_errSuccess ) {
            //"Failed to commit transaction: %S.\n"
            errprintfRes(IDS_AR_ERR_FAIL_COMMIT_TRANS,
                      GetJetErrString(jetResult));
            if ( SUCCEEDED(returnValue) ) {
                returnValue = E_UNEXPECTED;
            }
            goto CleanUp;
        }
        inTransaction = FALSE;

        // Indicate which record was updated
        if (gusnLow && gusnHigh) {
            if ( gRestoredList == NULL ) {

                gRestoredListMaxSize = DEFAULT_SUBREF_LIST_SIZE;

                result = ARAlloc(&gRestoredList,
                                 gRestoredListMaxSize * sizeof(DWORD));
                if ( FAILED(result) ) {
                    returnValue = result;
                    goto CleanUp;
                }

            } else if ( gRestoredListSize == gRestoredListMaxSize ) {

                DWORD size = gRestoredListMaxSize * sizeof(DWORD);

                result = ARRealloc(&gRestoredList, &size);
                if ( FAILED(result) ) {
                    returnValue = result;
                    goto CleanUp;
                }

                gRestoredListMaxSize *= 2;
            }

            gRestoredList[gRestoredListSize] = *(DWORD*)gDntVal->pvData;
            gRestoredListSize++;
        }

        gRecordsUpdated++;

        UpdateProgressMeter(gRecordCount - gRecordsUpdated, FALSE);

    }


CleanUp:

    // if we are still in a transaction, there must have been a failure
    // somewhere along the way.

    if ( inTransaction ) {

        jetResult = JetRollback(DbState->sessionId, JET_bitRollbackAll);
        if ( jetResult != JET_errSuccess ) {
            //"Failed to rollback transaction: %S.\n"
            errprintfRes(IDS_AR_ERR_FAIL_ROLLBACK_TRANS,
                      GetJetErrString(jetResult));
            if ( SUCCEEDED(returnValue) ) {
                returnValue = E_UNEXPECTED;
            }
        }

    }

    return returnValue;

} // AuthoritativeRestoreCurrentObject



HRESULT
AuthoritativeRestoreCurrentLink(
    IN DB_STATE *DbState,
    IN TABLE_STATE *LinkTableState,
    IN BOOL fDirectionForward
    )
/*++

Routine Description:

    This function updates the metadata for the current link record so that
    its meta-data appears to have been written to its current
    value at the given time at the given DC.

Some important rules:

1. AR should remark as absent links that are absent. This follows the rule in the
design that absent values are not tombstones, but are simply a differerent flavor
of value. Unlike objects, absent values may be made present by the user. For this
reason alone, we need a way to re-assert that they are made absent consistently.

2. We do NOT rewrite legacy values. Value metadata is for values with metadata (duh)
and attribute metadata covers the legacy values.  Following this design rule, the
existing AR code will touch the attribute level metadata for the linked attribute,
and this will automatically take care of re-replicating the legacy values.

By following this rule, we can make AR independent of whether the system is in
LVR mode or not.  We always update the linked value attribute metadata if there
is any. We ONLY update the value metadata for non-legacy values.  A non-LVR system
will simply not have any, and thus none will be marked. The only inefficiency I see
is that the linked attribute metadata might be touched when there are no more
legacy values. This would cause a harmless legacy value change replication to occur.
If we wanted to avoid this, we could have the attribute value metadata marking code
check first whether there are any legacy values for this attribute.
Not sure if this is worth the trouble though.

Arguments:

    DbState - Supplies the state of the opened DIT database.
    LinkTableState - Supplies the state of the link table.
    fDirectionForward - Whether this is a forward link or a backward link.
        Use this information to determine the two ends of the link

Environment:

    When we are called, it is assumed that a LinkRetrievalArray is populated
    with the data on the current link.

Return Value:

    S_OK - The record was modified successfully.
    S_FALSE - The record was deleted or not writeable
    E_OUTOFMEMORY - Not enough memory to allocate buffer.
    E_UNEXPECTED - Some variety of unexpected error occured.

--*/
{
    HRESULT returnValue = S_OK;
    HRESULT result;
    JET_ERR jetResult;
    BOOL inTransaction = FALSE;
    USN nextUsn;
    VALUE_META_DATA_EXT metaDataExt;
    VALUE_META_DATA_EXT *pMetaDataExt = &( metaDataExt );
    VALUE_META_DATA_EXT *pOldMetaDataExt;

    // We should have already filtered out metadata-less values
    Assert(gLinkMetaDataVal->cbActual);

    if (gLinkMetaDataVal->cbActual == sizeof( VALUE_META_DATA_EXT )) {
        // Metadata is in native format
        pOldMetaDataExt = (VALUE_META_DATA_EXT *) gLinkMetaDataVal->pvData;
    } else {
        errprintfRes(IDS_AR_ERR_UNKNOWN_VALUE_METADATA_FORMAT,
                     gLinkMetaDataVal->cbActual );
        returnValue = E_UNEXPECTED;
        goto CleanUp;
    }

    Assert( pOldMetaDataExt );

    // Start a new transaction

    jetResult = JetBeginTransaction(DbState->sessionId);
    if ( jetResult != JET_errSuccess ) {
        // "Could not start a new transaction: %ws.\n"
        errprintfRes(IDS_AR_ERR_START_TRANS,
                     GetJetErrString(jetResult));
        returnValue = E_UNEXPECTED;
        goto CleanUp;
    }

    inTransaction = TRUE;

    // Prepare for update

    jetResult = JetPrepareUpdate(DbState->sessionId,
                                 LinkTableState->tableId,
                                 JET_prepReplace);
    if ( jetResult != JET_errSuccess ) {
        //"Could not prepare update in \"%s\" table: %ws.\n"
        errprintfRes(IDS_AR_ERR_PREPARE_UPDATE,
                     LinkTableState->tableName,
                     GetJetErrString(jetResult));
        returnValue = E_UNEXPECTED;
        goto CleanUp;
    }

    // allocate a USN

    result = DitGetNewUsn(DbState, &nextUsn);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    // set the Link USN-Changed attribute

    jetResult = JetSetColumn(DbState->sessionId,
                             LinkTableState->tableId,
                             gLinkUsnChangedVal->columnid,
                             &nextUsn,
                             sizeof(nextUsn),
                             0,
                             NULL);
    if ( jetResult != JET_errSuccess ) {
        //"Could not set Usn-Changed column in \"%s\" table: %S.\n"
        errprintfRes(IDS_AR_ERR_SET_USN_CHANGED,
                     LinkTableState->tableName,
                     GetJetErrString(jetResult));
        returnValue = E_UNEXPECTED;
        goto CleanUp;
    }

    // If value is absent, rewrite deletion time
    if (gLinkDelTimeVal->cbActual) {
        DSTIME timeCreated, timeDeleted;

        // Set to maximum of timeCurrent and creationTime

        timeCreated = pOldMetaDataExt->timeCreated;
        if (timeCreated > gCurrentTime) {
            timeDeleted = timeCreated;
        } else {
            timeDeleted = gCurrentTime;
        }


        jetResult = JetSetColumn(DbState->sessionId,
                                 LinkTableState->tableId,
                                 gLinkDelTimeVal->columnid,
                                 &timeDeleted,
                                 sizeof(timeDeleted),
                                 0,
                                 NULL);
        if ( jetResult != JET_errSuccess ) {
            //"Could not set Del Time column in \"%s\" table: %S.\n"
            errprintfRes(IDS_AR_ERR_SET_DEL_TIME,
                         LinkTableState->tableName,
                         GetJetErrString(jetResult));
            returnValue = E_UNEXPECTED;
            goto CleanUp;
        }
    }

    // Calculate new metadata for the link.

    // An existing value
    pMetaDataExt->timeCreated = pOldMetaDataExt->timeCreated;
    pMetaDataExt->MetaData.dwVersion =
        pOldMetaDataExt->MetaData.dwVersion + gVersionIncrease;
    pMetaDataExt->MetaData.timeChanged = gCurrentTime;
    pMetaDataExt->MetaData.uuidDsaOriginating = gDatabaseGuid;
    pMetaDataExt->MetaData.usnOriginating = nextUsn;
    // usnProperty is written in the UsnChanged Column

    // set the metadata attribute
    jetResult = JetSetColumn(DbState->sessionId,
                             LinkTableState->tableId,
                             gLinkMetaDataVal->columnid,
                             pMetaDataExt,
                             sizeof( VALUE_META_DATA_EXT ),
                             0,
                             NULL);
    if ( jetResult != JET_errSuccess ) {
        //"Could not set meta-data column in \"%s\" table: %S.\n"
        errprintfRes(IDS_AR_ERR_SET_METADATA,
                     LinkTableState->tableName,
                     GetJetErrString(jetResult));
        returnValue = E_UNEXPECTED;
        goto CleanUp;
    }

    // Update the record

    jetResult = JetUpdate(DbState->sessionId,
                          LinkTableState->tableId,
                          NULL,
                          0,
                          0);
    if ( jetResult != JET_errSuccess ) {
        //"Could not update column in \"%s\" table: %S.\n"
        errprintfRes(IDS_AR_ERR_UPDATE_COLUMN,
                     LinkTableState->tableName,
                     GetJetErrString(jetResult));
        returnValue = E_UNEXPECTED;
        goto CleanUp;
    }

    // Commit the transaction

    jetResult = JetCommitTransaction(DbState->sessionId,
                                     JET_bitCommitLazyFlush);
    if ( jetResult != JET_errSuccess ) {
        //"Failed to commit transaction: %S.\n"
        errprintfRes(IDS_AR_ERR_FAIL_COMMIT_TRANS,
                     GetJetErrString(jetResult));
        if ( SUCCEEDED(returnValue) ) {
            returnValue = E_UNEXPECTED;
        }
        goto CleanUp;
    }

    //TODO: make a list of restored links and display them at the end
    // similar to what is done for restored objects

    inTransaction = FALSE;

    gRecordsUpdated++;

    UpdateProgressMeter(gRecordCount - gRecordsUpdated, FALSE);

CleanUp:

    // if we are still in a transaction, there must have been a failure
    // somewhere along the way.

    if ( inTransaction ) {

        jetResult = JetRollback(DbState->sessionId, JET_bitRollbackAll);
        if ( jetResult != JET_errSuccess ) {
            //"Failed to rollback transaction: %S.\n"
            errprintfRes(IDS_AR_ERR_FAIL_ROLLBACK_TRANS,
                      GetJetErrString(jetResult));
            if ( SUCCEEDED(returnValue) ) {
                returnValue = E_UNEXPECTED;
            }
        }

    }

    return returnValue;
}


HRESULT
GetVersionIncrease(
    IN DB_STATE *DbState,
    OUT DWORD *VersionIncrease
    )
/*++

Routine Description:

    This function determines the amount by which each version number should
    be increase.  It searches through the database to find the time of the last
    change that occured.  It is assumed that this DC has been idle since that
    change occured.  The version increase then is computed as the number of
    idle days (rounded up) times gVersionIncreasePerDay.

Arguments:

    DbState - Supplies the state of the opened DIT database.
    VersionIncrease - Returns the amount by which to increase each version
        number.

Return Value:

    S_OK - The operation succeeded.
    E_INVALIDARG - One of the given pointers was NULL.
    E_OUTOFMEMORY - Not enough memory to allocate buffer.
    E_UNEXPECTED - Some variety of unexpected error occured.

--*/
{

    HRESULT returnValue = S_OK;
    HRESULT result;

    DSTIME currentTime;
    DSTIME mostRecentChange;
    DWORD idleSeconds;
    DWORD idleDays;
    CHAR displayTime[SZDSTIME_LEN+1];
    LONGLONG llIncrease;

    if ( (DbState == NULL) ||
         (VersionIncrease == NULL) ) {
        ASSERT(FALSE);
        returnValue = E_INVALIDARG;
        goto CleanUp;
    }

    result = GetCurrentDsTime(&currentTime);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    result = DsTimeToString(currentTime, displayTime);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    //"The current time is %s.\n"
    RESOURCE_PRINT1 (IDS_TIME, displayTime);

    result = DitGetMostRecentChange(DbState, &mostRecentChange);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    result = DsTimeToString(mostRecentChange, displayTime);
    if ( FAILED(result) ) {
        returnValue = result;
        goto CleanUp;
    }

    //"Most recent database update occured at %s.\n"
    RESOURCE_PRINT1 (IDS_AR_UPDATE_TIME, displayTime);

    ASSERT(currentTime > mostRecentChange);

    idleSeconds = (DWORD)(currentTime - mostRecentChange);

    idleDays = idleSeconds / SECONDS_PER_DAY;
    if ( idleSeconds % SECONDS_PER_DAY > 0 ) {
        idleDays++;
    }

    if (idleDays > DEFAULT_TOMBSTONE_LIFETIME) {
        idleDays = DEFAULT_TOMBSTONE_LIFETIME;
    }

    llIncrease = idleDays * gVersionIncreasePerDay;
    if ( llIncrease > MAX_VERSION_INCREASE ) {
        llIncrease = MAX_VERSION_INCREASE;
    }

    *VersionIncrease = (DWORD) llIncrease;

    //"Increasing version numbers by %u.\n"
    RESOURCE_PRINT1 (IDS_AR_INCREASE_VERSION, *VersionIncrease);


CleanUp:

    return returnValue;

} // GetVersionIncrease



HRESULT
GetCurrentDsTime(
    OUT DSTIME *CurrentTime
    )
/*++

Routine Description:

    This function gets the current time in DSTIME form.  This function was
    basically stolen from tasq\time.c.

Arguments:

    CurrentTime - Returns the current time.

Return Value:

    S_OK - The operation succeeded.
    E_INVALIDARG - One of the given pointers was NULL.
    E_UNEXPECTED - Some variety of unexpected error occured.

--*/
{

    HRESULT returnValue = S_OK;
    BOOL succeeded;

    SYSTEMTIME systemTime;
    FILETIME fileTime;


    if ( CurrentTime == NULL ) {
        ASSERT(FALSE);
        returnValue = E_INVALIDARG;
        goto CleanUp;
    }

    GetSystemTime(&systemTime);

    succeeded = SystemTimeToFileTime(&systemTime, &fileTime);
    if ( !succeeded ) {
        //"Could not convert system time to file time (Windows Error %u).\n"
        errprintfRes(IDS_AR_ERR_CONVERT_SYSTEM_TIME,
                  GetLastError());
        returnValue = E_UNEXPECTED;
        goto CleanUp;
    }

    (*CurrentTime) = fileTime.dwLowDateTime;
    (*CurrentTime) |= (DSTIME)fileTime.dwHighDateTime << 32;
    (*CurrentTime) /= 10000000L;


CleanUp:

    return returnValue;

} // GetCurrentDsTime



ULONG
NumDigits(
    IN ULONG N
    )
/*++

Routine Description:

    Counts the number of decimal digits in the given number N.

Arguments:

    N - Supplies the number of which to count the digits.

Return Value:

    The number of decimal digits in N.

--*/
{

    BOOL addExtraDigit = FALSE;
    ULONG numDigits = 0;


    while ( N > 0 ) {

        if ( N % 10 != 0 ) {
            addExtraDigit = TRUE;
        }

        N = N / 10;

        numDigits++;

    }

    return numDigits;

} // NumDigits


HRESULT
MetaDataLookup(
    IN ATTRTYP AttributeType,
    IN PROPERTY_META_DATA_VECTOR *MetaDataVector,
    OUT DWORD *Index
    )
/*++

Routine Description:

    Find the meta data for the given attribute in the meta data vector.
    Returns the index at which the entry was found, or, if the corresponding
    meta data is absent, the index at which the entry would be inserted to
    preserve the sort order.

    Note: this function was basically stolen from dsamain\dra\drameta.c

Arguments:

    AttributeType - Supplies the attribute type to search for.
    MetaDataVector - Supplies meta data vector in which to search.
    Index - Returns the index at which the meta data was found or, if absent,
        the index at which meta data should have been.

Return Values:

    S_OK - The attribute was found.
    S_FALSE - The attribute was not found.

--*/
{

    HRESULT returnValue = S_FALSE;
    ATTRTYP first, last, current;
    long delta;


    VALIDATE_META_DATA_VECTOR_VERSION(MetaDataVector);
    current = delta = first = 0;
    last = MetaDataVector->V1.cNumProps - 1;

    while ( first <= last ) {

        current = (first + last) / 2;

        delta = AttributeType -
                MetaDataVector->V1.rgMetaData[current].attrType;

        if ( delta < 0 ) {

            last = current - 1;

        } else if ( delta > 0 ) {

            first = current + 1;

        } else {

            *Index = current;
            returnValue = S_OK;
            break;

        }

    }

    // if we did not find it in the vector,
    // set index to where it should have been
    if ( returnValue == S_FALSE ) {
        if ( delta < 0 ) {
            *Index = current;
        } else {
            *Index = current + 1;
        }
    }

    return returnValue;

} // MetaDataLookup


HRESULT
MetaDataInsert(
    IN ATTRTYP AttributeType,
    IN OUT PROPERTY_META_DATA_VECTOR **MetaDataVector,
    IN OUT DWORD *BufferSize
    )
/*++

Routine Description:

    Attempts to insert the given ATTRTYP into the given MetaDataVector.  If
    the entry is not already present, it will be added and its elements will
    be nulled with the exception of the attribute type, which will be set to
    given value.

    Note: this function was basically stolen from dsamain\dra\drameta.c

Arguments:

    AttributeType - Supplies the attribute type to search for.
    MetaDataVector - Supplies meta data vector in which to search.
    BufferSize - Supplies the size of the buffer which MetaDataVector points
        to.  If the buffer is not large enough to accomodate another entry,
        a new buffer will be allocated, and the new size returned here.

Return Values:

    S_OK - The attribute was inserted.
    S_FALSE - The attribute was already present.
    E_OUTOFMEMORY - Not enough memory to allocate buffer.

--*/
{

    HRESULT returnValue = S_OK;
    HRESULT result;
    DWORD index;
    DWORD i;


    result = MetaDataLookup(AttributeType, *MetaDataVector, &index);

    if ( result == S_FALSE ) {

        returnValue = S_OK;

        while ( *BufferSize - MetaDataVecV1Size((*MetaDataVector)) <
                  sizeof(PROPERTY_META_DATA) ) {

            result = ARRealloc(MetaDataVector, BufferSize);
            if ( FAILED(result) ) {
                returnValue = result;
                goto CleanUp;
            }

        }

        // make room for the new guy

        (*MetaDataVector)->V1.cNumProps++;

        for ( i = (*MetaDataVector)->V1.cNumProps - 1;
              i > index;
              i-- ) {
            (*MetaDataVector)->V1.rgMetaData[i] =
                (*MetaDataVector)->V1.rgMetaData[i-1];
        }

        ZeroMemory(&(*MetaDataVector)->V1.rgMetaData[index],
                   sizeof(PROPERTY_META_DATA));

        // insert the new guy

        (*MetaDataVector)->V1.rgMetaData[index].attrType = AttributeType;

    } else if ( result == S_OK ) {

        // there is no need to insert it if it's already there.

        ASSERT((*MetaDataVector)->V1.rgMetaData[index].attrType ==
                 AttributeType);

        returnValue = S_FALSE;

    } else {

        returnValue = result;
        goto CleanUp;

    }


CleanUp:
    VALIDATE_META_DATA_VECTOR_VERSION((*MetaDataVector));

    return returnValue;

} // MetaDataInsert



HRESULT
DsTimeToString(
    IN DSTIME Time,
    OUT CHAR *String
    )
/*++

Routine Description:

    This function converts a DSTIME into a displayable string form.

    Note:  this function was basically stolen from dscommon\dsutil.c.

Arguments:

    Time - Supplies the DSTIME to convert.
    String - Returns the string form of the given DSTIME.  This should contain
       space for at least SZDSTIME_LEN characters.

Return Value:

    S_OK - The operation succeeded.
    E_INVALIDARG - One of the given pointers was NULL.
    E_UNEXPECTED - Some variety of unexpected error occured.

--*/
{

    HRESULT returnValue = S_OK;
    BOOL succeeded;

    SYSTEMTIME utcSystemTime;
    SYSTEMTIME systemTime;
    FILETIME fileTime;
    ULONGLONG ull;


    if ( String == NULL ) {
        ASSERT(FALSE);
        returnValue = E_INVALIDARG;
        goto CleanUp;
    }

    ASSERT(sizeof(DSTIME) == sizeof(ULONGLONG));

    // convert DSTIME to FILETIME.
    ull = (LONGLONG) Time * 10000000L;
    fileTime.dwLowDateTime  = (DWORD) (ull & 0xFFFFFFFF);
    fileTime.dwHighDateTime = (DWORD) (ull >> 32);

    succeeded = FileTimeToSystemTime(&fileTime, &utcSystemTime);
    if ( !succeeded ) {
        //"Could not convert file time to system time (Windows Error %u).\n"
        errprintfRes(IDS_AR_ERR_CONVERT_FILE_TIME,
                  GetLastError());
        returnValue = E_UNEXPECTED;
        goto CleanUp;
    }

    succeeded = SystemTimeToTzSpecificLocalTime(NULL,
                                                &utcSystemTime,
                                                &systemTime);
    if ( !succeeded ) {
        //"Could not convert system time to local time (Windows Error %u).\n"
        errprintfRes(IDS_AR_ERR_CONVERT_LOCAL_TIME,
                  GetLastError());
        returnValue = E_UNEXPECTED;
        goto CleanUp;
    }

    sprintf(String,
            "%02d-%02d-%02d %02d:%02d.%02d",
            systemTime.wMonth,
            systemTime.wDay,
            systemTime.wYear % 100,
            systemTime.wHour,
            systemTime.wMinute,
            systemTime.wSecond);


CleanUp:

    return returnValue;

} // DsTimeToString



int
errprintf(
    IN CHAR *FormatString,
    IN ...
    )
/*++

Routine Description:

    This function prints out an error message in the same manner as printf.
    The error message is sent to stderr.

    The global variable gInUnfinishedLine is TRUE whenever another part of the
    code is waiting for an operation finish before it can print out the rest
    of the line.  Clearly, an error has occured before that could happen,
    so an extra newline is printed out.

Arguments:

    FormatString - Supplies the format string to pass to vfprintf.

Return Value:

    None

--*/
{

    int result;
    va_list vl;


    va_start(vl, FormatString);

    if ( gInUnfinishedLine ) {
        putc('\n', stderr);
        gInUnfinishedLine = FALSE;
    }

    result = vfprintf(stderr, FormatString, vl);

    va_end(vl);

    return result;

} // errprintf


int
errprintfRes(
    IN UINT FormatStringId,
    IN ...
    )
/*++

Routine Description:

    This function prints out an error message in the same manner as printf.
    The error message is loaded from a resource file.
    The error message is sent to stderr.

    The global variable gInUnfinishedLine is TRUE whenever another part of the
    code is waiting for an operation finish before it can print out the rest
    of the line.  Clearly, an error has occured before that could happen,
    so an extra newline is printed out.

Arguments:

    FormatString - Supplies the format string to pass to vfprintf.

Return Value:

    None

--*/
{

    int result;
    va_list vl;
    const WCHAR *formatString;


    va_start(vl, FormatStringId);


    formatString = READ_STRING (FormatStringId);


    if ( gInUnfinishedLine ) {
        putc('\n', stderr);
        gInUnfinishedLine = FALSE;
    }

    if (formatString) {
        result = vfwprintf(stderr, formatString, vl);
    }
    else {
        result = 0;
    }

    va_end(vl);

    RESOURCE_STRING_FREE (formatString);

    return result;

} // errprintfRes


int
dbgprintf(
    IN CHAR *FormatString,
    IN ...
    )
/*++

Routine Description:

    This function is simply a wrapper on printf that is meant to be used only
    while still debugging this program.

Arguments:

    FormatString - Supplies the format string for printf.

Return Value:

    None

--*/
{

    int result;
    va_list vl;


    va_start(vl, FormatString);

    if ( gInUnfinishedLine ) {
        putc('\n', stderr);
        gInUnfinishedLine = FALSE;
    }

    fprintf(stderr, "debug: ");

    result = vfprintf(stderr, FormatString, vl);

    va_end(vl);

    return result;

} // dbgprintf



HRESULT
ARAlloc(
    OUT VOID **Buffer,
    IN DWORD Size
    )
/*++

Routine Description:

    This function allocates the specified amount of memory (if possible) and
    sets Buffer to point to the buffer allocated.

Arguments:

    Buffer - Returns a pointer to the buffer allocated.
    Size - Supplies the size of the buffer to allocate.

Return Value:

    S_OK - The operation succeeded.
    E_OUTOFMEMORY - Not enough memory to allocate buffer.

--*/
{

    HRESULT returnValue = S_OK;


    *Buffer = malloc(Size);
    if ( *Buffer == NULL ) {
        errprintfRes(IDS_ERR_MEMORY_ALLOCATION, Size);
        returnValue = E_OUTOFMEMORY;
        goto CleanUp;
    }

    ZeroMemory(*Buffer, Size);


CleanUp:

    return returnValue;

} // ARAlloc



HRESULT
ARRealloc(
    IN OUT VOID **Buffer,
    IN OUT DWORD *CurrentSize
    )
/*++

Routine Description:

    This function re-allocates the given buffer to twice the given size (if
    possible).

Arguments:

    Buffer - Returns a pointer to the new buffer allocated.
    CurrentSize - Supplies the current size of the buffer.

Return Value:

    S_OK - The operation succeeded.
    E_OUTOFMEMORY - Not enough memory to allocate buffer.

--*/
{

    HRESULT returnValue = S_OK;
    BYTE *newBuffer;


    newBuffer = (BYTE*) realloc(*Buffer, *CurrentSize * 2);
    if ( newBuffer == NULL ) {
        errprintfRes(IDS_ERR_MEMORY_ALLOCATION,
                  *CurrentSize * 2);
        returnValue = E_OUTOFMEMORY;
        goto CleanUp;
    }

    ZeroMemory(&newBuffer[*CurrentSize], *CurrentSize);

    *Buffer = newBuffer;
    *CurrentSize *= 2;


CleanUp:

    return returnValue;

} // ARRealloc



VOID
UpdateProgressMeter(
    IN DWORD Progress,
    IN BOOL MustUpdate
    )
/*++

Routine Description:

    If necessary, updates the progress meter on the screen to show the current
    progress. The update is necessary if gCurrentDisplayDelta updates have
    occured since the last update or if MustUpdate is true.  This updating can
    be removed by #define-ing NO_PROGRESS_METER.

Arguments:

    Progress - Supplies the current progress.
    MustUpdate - Supplies whether we must perform an update this time.

Return Value:

    None

--*/
{

    DWORD i;

#ifndef NO_PROGRESS_METER

    if ( MustUpdate || (Progress % gCurrentDisplayDelta == 0) ) {

        for ( i = 0; i < gNumDigitsToPrint; i++ ) {
            putchar('\b');
        }

        printf("%0*u", gNumDigitsToPrint, Progress);

    }

#endif

} // UpdateProgressMeter



HRESULT
AuthoritativeRestoreListNcCrsWorker(
    VOID
    )
/*++

Routine Description:

    This routine basically opens up the AD's DIT, walks the PDNT index looking
    for entries that match the DNT of the CN=Partions,CN=Configuration,DC=root
    (because these would be CRs), and for each one prints out the DN of the 
    cross-ref and the DN of the nCName.  This effectively prints out all NCs
    and thier CRs that are in the AD.  This code was ripped off of scheckc.c:
    SFixupCnfNc(), and so I thought it better to not change the code too much.

--*/
{

    // return
    HRESULT returnValue = S_OK;
    HRESULT result;
    JET_ERR jErr;
    // database & jet
    DB_STATE *dbState = NULL;
    TABLE_STATE *tableState = NULL;
    TABLE_STATE *linkTableState = NULL;
    // various local helpers
    DWORD dnt, pdnt = 0, dntPartitions = 0;
    LPWSTR pNcBuffer = NULL;
    LPWSTR pCrBuffer = NULL;
    DWORD  iNcInstanceType;
    BOOL   bObj;
    DWORD  cbBuffer = 0;
    DWORD * pCrDnts = NULL;
    ULONG iPartitions, i, j, iNumberOffset;
    ULONG cPartitionsBufferCount = 10; // reasonable first guess of # of CRs.
    BOOL fTempPrintJetError;



    //
    // Open database/tables
    //

    RESOURCE_PRINT (IDS_AR_OPEN_DB_DIT);

    __try{

        result = DitOpenDatabase(&dbState);
        if ( FAILED(result) ) {
            RESOURCE_PRINT1(IDS_AUTH_RESTORE_LIST_FAILED_TO_OPEN_DB, result);
            returnValue = result;
            goto CleanUp;
        }

        //"done.\n"
        RESOURCE_PRINT (IDS_DONE);


        result = DitOpenTable(dbState, SZDATATABLE, SZDNTINDEX, &tableState);
        if ( FAILED(result) ) {
            RESOURCE_PRINT1(IDS_AUTH_RESTORE_LIST_FAILED_TO_OPEN_DB, result);
            returnValue = result;
            goto CleanUp;
        }

        // SZLINKALLINDEX includes both present and absent link values
        result = DitOpenTable(dbState, SZLINKTABLE, SZLINKALLINDEX, &linkTableState);
        if ( FAILED(result) ) {
            RESOURCE_PRINT1(IDS_AUTH_RESTORE_LIST_FAILED_TO_OPEN_DB, result);
            returnValue = result;
            goto CleanUp;
        }

        result = FindPartitions(
                    dbState,
                    tableState,
                    linkTableState,
                    &dntPartitions);
        if ( FAILED(result) ) {
            RESOURCE_PRINT1(IDS_AUTH_RESTORE_LIST_FAILED_TO_OPEN_DB, result);
            returnValue = result;
            goto CleanUp;
        }


        //
        // traverse pdnt index to cycle through all partition kids.
        //

        result = DitSetIndex(dbState, tableState, SZPDNTINDEX, FALSE);
        if ( FAILED(result) ) {
            RESOURCE_PRINT1(IDS_AUTH_RESTORE_LIST_FAILED_TO_OPEN_DB, result);
            returnValue = result;
            goto CleanUp;
        }

        result = DitSeekToFirstChild(dbState, tableState, dntPartitions);
        if ( FAILED(result) ) {
            RESOURCE_PRINT1(IDS_AUTH_RESTORE_LIST_FAILED_TO_OPEN_DB, result);
            returnValue = result;
            goto CleanUp;
        }


        //
        // Create kids array, list of DNTs for each CR.
        //

        cbBuffer = sizeof(DWORD) * cPartitionsBufferCount;
        result = DitAlloc(&pCrDnts, cbBuffer);
        if ( FAILED(result) ) {
            RESOURCE_PRINT1(IDS_ERR_MEMORY_ALLOCATION, cbBuffer);
            returnValue = result;
            goto CleanUp;
        }

        pdnt = dntPartitions;
        dnt = dntPartitions;
        iPartitions = 0;
        while ( pdnt == dntPartitions ) {

            // get kid dnt
            result = DitGetColumnByName(
                        dbState,
                        tableState,
                        SZDNT,
                        &dnt,
                        sizeof(dnt),
                        NULL);
            if ( FAILED(result) ) {
                continue;
            }


            // get parent dnt
            result = DitGetColumnByName(
                        dbState,
                        tableState,
                        SZPDNT,
                        &pdnt,
                        sizeof(pdnt),
                        NULL);
            if ( FAILED(result) ) {
                continue;
            }

            if ( pdnt == dntPartitions ) {
                // proceed until we got a diff parent
                pCrDnts[iPartitions] = dnt;
                iPartitions++;
                
                if(iPartitions >= cPartitionsBufferCount){
                    // We need to get more space.
                    cPartitionsBufferCount *= 2;
                    result = DitRealloc(&pCrDnts, &cbBuffer);
                    if ( FAILED(result) ) {
                        RESOURCE_PRINT1(IDS_ERR_MEMORY_ALLOCATION, cbBuffer);
                        returnValue = result;
                        goto CleanUp;
                    }
                    // Note, DitRealloc basically just doubles the size
                    // of the array, so need to make sure that's what it
                    // still does, just in case someone changes it.
                    Assert( (sizeof(DWORD) * cPartitionsBufferCount) == cbBuffer );
                }
            }
            else{
                break;
            }

            // find next one
            jErr = JetMove(
                        dbState->sessionId,
                        tableState->tableId,
                        JET_MoveNext,
                        0);
        }

        result = DitSetIndex(dbState, tableState, SZDNTINDEX, FALSE);
        if ( FAILED(result) ) {
            RESOURCE_PRINT1(IDS_AUTH_RESTORE_LIST_FAILED_TO_OPEN_DB, result);
            returnValue = result;
            goto CleanUp;
        }

        //
        // We've got the partition dnt list.
        //  - for each partition, get the DN of the nCName and CR itself
        //        and print them.
        //

        RESOURCE_PRINT1(IDS_AUTH_RESTORE_LIST_LIST, iPartitions);

        iNumberOffset = 1; // This is the number to add to i to get the number
        // of the partition for the purpose of printing.
        for ( i = 0; i < iPartitions; i++ ) {

            result = DitSeekToDnt(
                            dbState,
                            tableState,
                            pCrDnts[i]);
            if ( FAILED(result) ) {
                RESOURCE_PRINT1(IDS_AUTH_RESTORE_LIST_SKIP, i+iNumberOffset);
                continue;
            }
            
            result = DitGetColumnByName(
                        dbState,
                        tableState,
                        SZNCNAME,
                        &dnt,
                        sizeof(dnt),
                        NULL);
            if ( FAILED(result) ) {
                // This is OK, we mention we couldn't find the NC name.
            } else {
                // Goto it and get the instance Type and DN.
                result = DitSeekToDnt(
                                dbState,
                                tableState,
                                dnt);
                if ( FAILED(result) ) {
                    RESOURCE_PRINT1(IDS_AUTH_RESTORE_LIST_SKIP, i+iNumberOffset);
                } else {
                    // Get the instance type.
                    bObj = FALSE;
                    result = DitGetColumnByName(
                                dbState,
                                tableState,
                                SZOBJ,
                                &bObj,
                                sizeof(bObj),
                                NULL);
                    if ( FAILED(result) ) {
                        // If the Obj isn't returned that's a problem.
                        // This is also a problem DitGetColumnByName printed error.
                        Assert(!"We should always have the obj column!");
                        iNcInstanceType = 0;
                    } else {

                        if(bObj){
                            result = DitGetColumnByName(
                                        dbState,
                                        tableState,
                                        SZINSTTYPE,
                                        &iNcInstanceType,
                                        sizeof(iNcInstanceType),
                                        NULL);
                            if ( FAILED(result) ) {
                                // This is also a problem DitGetColumnByName printed error.
                                Assert(!"If this is an object it should always have an instanceType!");
                                iNcInstanceType = 0;
                            } // else iNcInstance Type is set.
                        } else {
                            // We've got a phantom
                            iNcInstanceType = 0;
                        }
                    }

                    // Get the DN of the nCName attribute.
                    pNcBuffer = GetDN(dbState, tableState, dnt, TRUE);
                }
            }
            
            // Get the DN of the CR itself.
            pCrBuffer = GetDN(dbState, tableState, pCrDnts[i], TRUE);

            // We've collected our data, now print out something if this NC
            // has is a locally instantiated writeable NC, that is not in the
            // process of being added or removed
            if((iNcInstanceType & IT_NC_HEAD) &&
               (iNcInstanceType & IT_WRITE) &&
               !(iNcInstanceType & (IT_NC_GOING | IT_NC_COMING | IT_UNINSTANT))){

                if(pNcBuffer || pCrBuffer){
                    if ( pNcBuffer ) {
                        RESOURCE_PRINT2(IDS_AUTH_RESTORE_LIST_ONE_NC, i+iNumberOffset, pNcBuffer);
                        DitFree(pNcBuffer);
                        pNcBuffer = NULL;
                    } else {
                        RESOURCE_PRINT1(IDS_AUTH_RESTORE_LIST_NO_NC_NAME, i+iNumberOffset);
                    }

                    if ( pCrBuffer ) {
                        RESOURCE_PRINT1(IDS_AUTH_RESTORE_LIST_NCS_CR, pCrBuffer);
                        //We've got the DN.
                        DitFree(pCrBuffer);
                        pCrBuffer = NULL;
                    } else {
                        RESOURCE_PRINT(IDS_AUTH_RESTORE_LIST_NO_CR_DN);
                    }
                } else {
                    RESOURCE_PRINT1(IDS_AUTH_RESTORE_LIST_SKIP, i+iNumberOffset);
                }
            } else {
                // It's not locally instantiated.  Subtract one from iNumberOffset
                iNumberOffset--;
            }

        }

CleanUp:;

    } __finally {


        if ( SUCCEEDED(returnValue) ) {
            RESOURCE_PRINT(IDS_DONE);
        } else {
            RESOURCE_PRINT(IDS_FAILED);
        }

        if ( pNcBuffer ) {
            DitFree(pNcBuffer);
        }
        
        if ( pCrBuffer ) {
            DitFree(pCrBuffer);
        }

        if ( pCrDnts ) {
            DitFree(pCrDnts);
        }

        if ( tableState != NULL ) {
            result = DitCloseTable(dbState, &tableState);
            if ( FAILED(result) ) {
                if ( SUCCEEDED(returnValue) ) {
                    returnValue = result;
                }
            }
        }

        if ( tableState != NULL ) {
            result = DitCloseTable(dbState, &linkTableState);
            if ( FAILED(result) ) {
                if ( SUCCEEDED(returnValue) ) {
                    returnValue = result;
                }
            }
        }


        if ( dbState != NULL ) {
            result = DitCloseDatabase(&dbState);
            if ( FAILED(result) ) {
                if ( SUCCEEDED(returnValue) ) {
                    returnValue = result;
                }
            }
        }

    }

    return returnValue;
} // AuthoritativeRestoreListNcCrsWorker



