/*++

Copyright (c) 1996-2001  Microsoft Corporation

Module Name:

    rrlist.c

Abstract:

    Domain Name System (DNS) Library

    Record list manipulation.

Author:

    Jim Gilroy (jamesg)     January, 1997

Environment:

    User Mode - Win32

Revision History:

--*/


#include "local.h"




PDNS_RECORD
Dns_RecordSetDetach(
    IN OUT  PDNS_RECORD     pRR
    )
/*++

Routine Description:

    Detach first RR set from the rest of the list.

Arguments:

    pRR - incoming record set

Return Value:

    Ptr to first record of next RR set.
    NULL if at end of list.

--*/
{
    PDNS_RECORD prr = pRR;
    PDNS_RECORD pback;      // previous RR in set
    WORD        type;       // first RR set type
    DWORD       section;    // section of first RR set

    if ( !prr )
    {
        return( NULL );
    }

    //
    //  loop until find start of new RR set
    //      - new type or
    //      - new section or
    //      - new name
    //      note that NULL name is automatically considered
    //      previous name
    //  

    type = prr->wType;
    section = prr->Flags.S.Section;
    pback = prr;

    while ( prr = pback->pNext )
    {
        if ( prr->wType == type &&
             prr->Flags.S.Section == section &&
             ( prr->pName == NULL ||
               Dns_NameComparePrivate(
                    prr->pName,
                    pback->pName,
                    pback->Flags.S.CharSet ) ) )
        {
            pback = prr;
            continue;
        }

        //  should not be detaching nameless record
        //      - fixup for robustness

        if ( !prr->pName )
        {
            ASSERT( prr->pName );
            prr->pName = Dns_NameCopyAllocate(
                            pRR->pName,
                            0,      // length unknown
                            pRR->Flags.S.CharSet,
                            prr->Flags.S.CharSet );
            SET_FREE_OWNER( prr );
        }
        break;
    }

    //  have following RR set, NULL terminate first set

    if ( prr )
    {
        pback->pNext = NULL;
    }
    return( prr );
}



PDNS_RECORD
WINAPI
Dns_RecordListAppend(
    IN OUT  PDNS_RECORD     pHeadList,
    IN      PDNS_RECORD     pTailList
    )
/*++

Routine Description:

    Append record list onto another.

Arguments:

    pHeadList -- record list to be head

    pTailList -- record list to append to pHeadList

Return Value:

    Ptr to first record of combined RR set.
        - pHeadList UNLESS pHeadList is NULL,
        then it is pTailList.

--*/
{
    PDNS_RECORD prr = pHeadList;

    if ( !pTailList )
    {
        return  prr;
    }
    if ( !prr )
    {
        return  pTailList;
    }

    //  find end of first list and append second list

    while ( prr->pNext )
    {
        prr = prr->pNext;
    }

    //  should be appending new set (with new name)
    //  or matching previous set

    DNS_ASSERT( !pTailList || pTailList->pName ||
                (pTailList->wType == prr->wType &&
                 pTailList->Flags.S.Section == prr->Flags.S.Section) );

    prr->pNext = pTailList;

    return pHeadList;
}



DWORD
Dns_RecordListCount(
    IN      PDNS_RECORD     pRRList,
    IN      WORD            wType
    )
/*++

Routine Description:

    Count records in list.

Arguments:

    pRRList - incoming record set

Return Value:

    Count of records of given type in list.

--*/
{
    DWORD   count = 0;

    //
    //  loop counting all records that match
    //      - either direct match
    //      - or if matching type is ALL
    //

    while ( pRRList )
    {
        if ( pRRList->wType == wType ||
             wType == DNS_TYPE_ALL )
        {
            count++;
        }

        pRRList = pRRList->pNext;
    }

    return( count );
}



DWORD
Dns_RecordListGetMinimumTtl(
    IN      PDNS_RECORD     pRRList
    )
/*++

Routine Description:

    Get minimum TTL of record list

Arguments:

    pRRList - incoming record set

Return Value:

    Minimum TTL of records in list.

--*/
{
    PDNS_RECORD prr = pRRList;
    DWORD       minTtl = MAXDWORD;

    DNSDBG( TRACE, (
        "Dns_RecordListGetMinimumTtl( %p )\n",
        pRRList ));

    //
    //  loop through list build minimum TTL
    //

    while ( prr )
    {
        if ( prr->dwTtl < minTtl )
        {
            minTtl = prr->dwTtl;
        }
        prr = prr->pNext;
    }

    return  minTtl;
}




//
//  Record screening
//

BOOL
Dns_ScreenRecord(
    IN      PDNS_RECORD     pRR,
    IN      DWORD           ScreenFlag
    )
/*++

Routine Description:

    Screen a record.

Arguments:

    pRR - incoming record

    ScreenFlag - screeing flag

Return Value:

    TRUE if passes screening.
    FALSE if record fails screen.

--*/
{
    BOOL    fsave = TRUE;

    DNSDBG( TRACE, (
        "Dns_ScreenRecord( %p, %08x )\n",
        pRR,
        ScreenFlag ));

    //  section screening

    if ( ScreenFlag & SCREEN_OUT_SECTION )
    {
        if ( IS_ANSWER_RR(pRR) )
        {
            fsave = !(ScreenFlag & SCREEN_OUT_ANSWER);
        }
        else if ( IS_AUTHORITY_RR(pRR) )
        {
            fsave = !(ScreenFlag & SCREEN_OUT_AUTHORITY);
        }
        else if ( IS_ADDITIONAL_RR(pRR) )
        {
            fsave = !(ScreenFlag & SCREEN_OUT_ADDITIONAL);
        }
        if ( !fsave )
        {
            return  FALSE;
        }
    }

    //  type screening

    if ( ScreenFlag & SCREEN_OUT_NON_RPC )
    {
        fsave = Dns_IsRpcRecordType( pRR->wType );
    }

    return  fsave;
}



PDNS_RECORD
Dns_RecordListScreen(
    IN      PDNS_RECORD     pRR,
    IN      DWORD           ScreenFlag
    )
/*++

Routine Description:

    Screen records from record set.

Arguments:

    pRR - incoming record set

    ScreenFlag - flag with record screening parameters

Return Value:

    Ptr to new record set, if successful.
    NULL on error.

--*/
{
    PDNS_RECORD     prr;
    PDNS_RECORD     pnext;
    DNS_RRSET       rrset;

    DNSDBG( TRACE, (
        "Dns_RecordListScreen( %p, %08x )\n",
        pRR,
        ScreenFlag ));

    //  init copy rrset

    DNS_RRSET_INIT( rrset );

    //
    //  loop through RR list
    //

    pnext = pRR;

    while ( pnext )
    {
        prr = pnext;
        pnext = prr->pNext;

        //
        //  screen
        //      - reappend record passing screen
        //      - delete record failing screen
        //

        if ( Dns_ScreenRecord( prr, ScreenFlag ) )
        {
            prr->pNext = NULL;
            DNS_RRSET_ADD( rrset, prr );
            continue;
        }
        else
        {
            Dns_RecordFree( prr );
        }
    }

    return( rrset.pFirstRR );
}



//
//  List sorting
//

PDNS_RECORD
Dns_PrioritizeSingleRecordSet(
    IN OUT  PDNS_RECORD     pRecordSet,
    IN      PDNS_ADDR_ARRAY pArray
    )
/*++

Routine Description:

    Prioritize records in record set.

    Note:  REQUIRES single record set.
    Caller should use Dns_PrioritizeRecordList() for multiple lists.

Arguments:

    pRecordSet -- record set to prioritize

    pArray -- address array to sort against

Return Value:

    Ptr to prioritized set.
    Set is NOT new, but is same set as pRecordSet, with records shuffled.

--*/
{
    PDNS_RECORD     prr;
    PDNS_RECORD     pprevRR;
    PDNS_RECORD     prrUnmatched;
    DWORD           iter;
    DNS_LIST        listSubnetMatch;
    DNS_LIST        listClassMatch;
    DNS_LIST        listUnmatched;

    //
    //  DCR_FIX:  this whole routine is bogus
    //      - it lets you do no intermediate ranking
    //      it's binary and in order of IPs in list
    //
    //  need
    //      - knowledge of fast\slow interfaces (WAN for example)
    //  then
    //      - do best match on each RR in turn (rank it)
    //      - then arrange in rank order
    //

    //
    //  verify multirecord set
    //      -- currently only handle type A
    //
    //  DCR_ENHANCE:  prioritize AAAA records?
    //      may need scope info to do properly
    //

    prr = pRecordSet;

    if ( !prr ||
         prr->pNext == NULL  ||
         prr->wType != DNS_TYPE_A )
    {
        return( pRecordSet );
    }

    //  init prioritized list

    DNS_LIST_STRUCT_INIT( listSubnetMatch );
    DNS_LIST_STRUCT_INIT( listClassMatch );
    DNS_LIST_STRUCT_INIT( listUnmatched );


    //
    //  loop through all RRs in set
    //

    while ( prr )
    {
        PDNS_RECORD pnext;
        DWORD       matchLevel;

        ASSERT( prr->wType == DNS_TYPE_A );

        pnext = prr->pNext;
        prr->pNext = NULL;

        //  check for subnet match

        matchLevel = DnsAddrArray_NetworkMatchIp4(
                        pArray,
                        prr->Data.A.IpAddress,
                        NULL        // don't need match addr
                        );

        if ( matchLevel == 0 )
        {
            DNS_LIST_STRUCT_ADD( listUnmatched, prr );
        }
        else if ( matchLevel == DNSADDR_NETMATCH_SUBNET )
        {
            DNS_LIST_STRUCT_ADD( listSubnetMatch, prr );
        }
        else
        {
            DNS_LIST_STRUCT_ADD( listClassMatch, prr );
        }

        prr = pnext;
    }
    
    //
    //  pull lists back together
    //

    if ( prr = listClassMatch.pFirst )
    {
        DNS_LIST_STRUCT_ADD( listSubnetMatch, prr );
    }
    if ( prr = listUnmatched.pFirst )
    {
        DNS_LIST_STRUCT_ADD( listSubnetMatch, prr );
    }
    prr = (PDNS_RECORD) listSubnetMatch.pFirst;

    DNS_ASSERT( prr );

    //
    //  make sure first record has name
    //      - use the name from the original first record
    //      - or copy it
    //

    if ( !prr->pName  ||  !FLAG_FreeOwner(prr) )
    {
        //  steal name from first record

        if ( pRecordSet->pName && FLAG_FreeOwner(pRecordSet) )
        {
            prr->pName = pRecordSet->pName;
            FLAG_FreeOwner(prr) = TRUE;
            pRecordSet->pName = NULL;
            FLAG_FreeOwner(pRecordSet) = FALSE;
        }

        //  if can't poach name, copy it
        //  if copy fails, just point at it
        //      note:  if cared enough about mem failure could
        //             just put original record back at the front

        else
        {
            PBYTE pnameCopy = NULL;

            pnameCopy = Dns_NameCopyAllocate(
                            pRecordSet->pName,
                            0,              // length unknown
                            RECORD_CHARSET( prr ),
                            RECORD_CHARSET( prr )
                            );
            if ( pnameCopy )
            {
                prr->pName = pnameCopy;
                FLAG_FreeOwner( prr ) = TRUE;
            }
            else if ( !prr->pName )
            {
                prr->pName = pRecordSet->pName;
                FLAG_FreeOwner( prr ) = FALSE;
            }
        }
    }

    //
    //  return prioritized list
    //

    return  prr;
}



PDNS_RECORD
Dns_PrioritizeRecordList(
    IN OUT  PDNS_RECORD     pRecordList,
    IN      PDNS_ADDR_ARRAY pArray
    )
/*++

Routine Description:

    Prioritize records in record list.

    Record list may contain multiple record sets.
    Note, currently only prioritize A records, but may
    later do A6 also.

Arguments:

    pRecordSet -- record set to prioritize

    pArray -- address array to sort against

Return Value:

    Ptr to prioritized set.
    Set is NOT new, but is same set as pRecordSet, with records shuffled.

--*/
{
    PDNS_RECORD     pnewList = NULL;
    PDNS_RECORD     prr;
    PDNS_RECORD     prrNextSet;

    if ( ! pRecordList ||
         ! pArray  ||
         pArray->AddrCount == 0 )
    {
        return pRecordList;
    }

    //
    //  loop through all record sets prioritizing
    //      - whack off each RR set in turn
    //      - prioritize it (if possible)
    //      - pour it back into full list
    //      
    //

    prr = pRecordList;

    while ( prr )
    {
        prrNextSet = Dns_RecordSetDetach( prr );

        prr = Dns_PrioritizeSingleRecordSet(
                    prr,
                    pArray );

        DNS_ASSERT( prr );

        pnewList = Dns_RecordListAppend(
                        pnewList,
                        prr );

        prr = prrNextSet;
    }

    return  pnewList;
}

//
//  End rrlist.c
//
