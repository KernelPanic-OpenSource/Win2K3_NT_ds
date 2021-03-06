/*++

Copyright (c) 1997-2000 Microsoft Corporation

Module Name:

    rrwrite.c

Abstract:

    Domain Name System (DNS) Library

    Write resource record to packet routines.

Author:

    Jim Gilroy (jamesg)     January, 1997

Revision History:

--*/


#include "local.h"




PCHAR
A_RecordWrite(
    IN OUT  PDNS_RECORD     pRR,
    IN      PCHAR           pch,
    IN      PCHAR           pchEnd
    )
/*++

Routine Description:

    Write A record data to packet.

Arguments:

    pRR - ptr to record to write

    pch - ptr to position in buffer to write

    pchEnd - end of buffer position

Return Value:

    Ptr to next byte in buffer to write.
    NULL on error.  (Error code through GetLastError())

--*/
{
    if ( pch + sizeof(IP4_ADDRESS) > pchEnd )
    {
        SetLastError( ERROR_MORE_DATA );
        return( NULL );
    }

    *(UNALIGNED DWORD *) pch = pRR->Data.A.IpAddress;

    return ( pch + sizeof(IP4_ADDRESS) );
}



PCHAR
Ptr_RecordWrite(
    IN OUT  PDNS_RECORD     pRR,
    IN      PCHAR           pch,
    IN      PCHAR           pchEnd
    )
/*++

Routine Description:

    Write PTR compatible record data to packet.
    Includes: PTR, CNAME, MB, MR, MG, MD, MF

Arguments:

    pRR - ptr to record to write

    pch - ptr to position in buffer to write

    pchEnd - end of buffer position

Return Value:

    Ptr to next byte in buffer to write.
    NULL on error.  (Error code through GetLastError())

--*/
{
    //
    //  write name to packet
    //      - no compression in data
    //

    pch = Dns_WriteDottedNameToPacket(
                pch,
                pchEnd,
                pRR->Data.PTR.pNameHost,
                NULL,
                0,
                IS_UNICODE_RECORD(pRR) );
    if ( !pch )
    {
        SetLastError( ERROR_MORE_DATA );
    }
    return( pch );
}



PCHAR
Soa_RecordWrite(
    IN OUT  PDNS_RECORD     pRR,
    IN      PCHAR           pch,
    IN      PCHAR           pchEnd
    )
/*++

Routine Description:

    Write SOA compatible to wire.

Arguments:

    pRR - ptr to record to write

    pch - ptr to position in buffer to write

    pchEnd - end of buffer position

Return Value:

    Ptr to next byte in buffer to write.
    NULL on error.  (Error code through GetLastError())

--*/
{
    PCHAR   pchdone;
    PDWORD  pdword;

    //
    //  SOA names: primary server, responsible party
    //

    pch = Dns_WriteDottedNameToPacket(
                pch,
                pchEnd,
                pRR->Data.SOA.pNamePrimaryServer,
                NULL,
                0,
                IS_UNICODE_RECORD(pRR) );
    if ( !pch )
    {
        SetLastError( ERROR_MORE_DATA );
        return( NULL );
    }
    pch = Dns_WriteDottedNameToPacket(
                pch,
                pchEnd,
                pRR->Data.SOA.pNameAdministrator,
                NULL,
                0,
                IS_UNICODE_RECORD(pRR) );
    if ( !pch )
    {
        SetLastError( ERROR_MORE_DATA );
        return( NULL );
    }

    //
    //  SOA integer fields
    //

    pchdone = pch + SIZEOF_SOA_FIXED_DATA;
    if ( pchdone > pchEnd )
    {
        SetLastError( ERROR_MORE_DATA );
        return( NULL );
    }

    pdword = &pRR->Data.SOA.dwSerialNo;
    while( pch < pchdone )
    {
        *(UNALIGNED DWORD *) pch = htonl( *pdword++ );
        pch += sizeof( DWORD );
    }
    return( pch );
}



PCHAR
Mx_RecordWrite(
    IN OUT  PDNS_RECORD     pRR,
    IN      PCHAR           pch,
    IN      PCHAR           pchEnd
    )
/*++

Routine Description:

    Write MX compatible record to wire.
    Includes: MX, RT, AFSDB

Arguments:

    pRR - ptr to record to write

    pch - ptr to position in buffer to write

    pchEnd - end of buffer position

Return Value:

    Ptr to next byte in buffer to write.
    NULL on error.  (Error code through GetLastError())

--*/
{
    //
    //  MX preference value
    //  RT preference
    //  AFSDB subtype
    //

    if ( pch + sizeof(WORD) > pchEnd )
    {
        SetLastError( ERROR_MORE_DATA );
        return( NULL );
    }
    *(UNALIGNED WORD *) pch = htons( pRR->Data.MX.wPreference );
    pch += sizeof( WORD );

    //
    //  MX exchange
    //  RT exchange
    //  AFSDB hostname
    //

    pch = Dns_WriteDottedNameToPacket(
                pch,
                pchEnd,
                pRR->Data.MX.pNameExchange,
                NULL,
                0,
                IS_UNICODE_RECORD(pRR) );
    if ( !pch )
    {
        SetLastError( ERROR_MORE_DATA );
    }
    return( pch );
}



PCHAR
Txt_RecordWrite(
    IN OUT  PDNS_RECORD     pRR,
    IN      PCHAR           pch,
    IN      PCHAR           pchEnd
    )
/*++

Routine Description:

    Write TXT compatible record to wire.
    Includes: TXT, HINFO, ISDN, X25 types.

Arguments:

    pRR - ptr to record to write

    pch - ptr to position in buffer to write

    pchEnd - end of buffer position

Return Value:

    Ptr to next byte in buffer to write.
    NULL on error.  (Error code through GetLastError())

--*/
{
    WORD    i;
    PCHAR * ppstring;

    //
    //  write all available text strings
    //

    i = (WORD) pRR->Data.TXT.dwStringCount;

    if ( ! Dns_IsStringCountValidForTextType( pRR->wType, i ) )
    {
        SetLastError( ERROR_INVALID_DATA );
        return( NULL );
    }

    ppstring = pRR->Data.TXT.pStringArray;
    while ( i-- )
    {
        pch = Dns_WriteStringToPacket(
                pch,
                pchEnd,
                *ppstring,
                IS_UNICODE_RECORD(pRR) );
        if ( !pch )
        {
            SetLastError( ERROR_MORE_DATA );
            break;
        }
        ppstring++;
    }
    return( pch );
}



PCHAR
Hinfo_RecordWrite(
    IN OUT  PDNS_RECORD     pRR,
    IN      PCHAR           pch,
    IN      PCHAR           pchEnd
    )
/*++

Routine Description:

    Write HINFO (string like) record to wire.
    Includes: HINFO, ISDN, X25 types.

Arguments:

    pRR - ptr to record to write

    pch - ptr to position in buffer to write

    pchEnd - end of buffer position

Return Value:

    Ptr to next byte in buffer to write.
    NULL on error.  (Error code through GetLastError())

--*/
{
    WORD    i;
    PCHAR * ppstring;

    //
    //  write all available text strings
    //
    //  DCR:  ISDN HINFO write -- not sure works
    //      not sure this works, because NULL may need to
    //      be written for ISDN or even HINFO
    //

    i = 2;
    if ( pRR->wType == DNS_TYPE_X25 )
    {
        i=1;
    }

    ppstring = (PSTR *) & pRR->Data.TXT;
    while ( i-- )
    {
        if ( ! *ppstring )
        {
            break;
        }
        pch = Dns_WriteStringToPacket(
                pch,
                pchEnd,
                *ppstring,
                IS_UNICODE_RECORD(pRR) );
        if ( !pch )
        {
            SetLastError( ERROR_MORE_DATA );
            break;
        }
        ppstring++;
    }
    return( pch );
}



PCHAR
Minfo_RecordWrite(
    IN OUT  PDNS_RECORD     pRR,
    IN      PCHAR           pch,
    IN      PCHAR           pchEnd
    )
/*++

Routine Description:

    Write MINFO compatible to wire.
    Includes MINFO and RP types.

Arguments:

    pRR - ptr to record to write

    pch - ptr to position in buffer to write

    pchEnd - end of buffer position

Return Value:

    Ptr to next byte in buffer to write.
    NULL on error.  (Error code through GetLastError())

--*/
{
    //
    //  MINFO responsible mailbox
    //  RP responsible person mailbox

    pch = Dns_WriteDottedNameToPacket(
                pch,
                pchEnd,
                pRR->Data.MINFO.pNameMailbox,
                NULL,
                0,
                IS_UNICODE_RECORD(pRR) );
    if ( !pch )
    {
        SetLastError( ERROR_MORE_DATA );
        return( NULL );
    }

    //
    //  MINFO errors to mailbox
    //  RP text RR location

    pch = Dns_WriteDottedNameToPacket(
                pch,
                pchEnd,
                pRR->Data.MINFO.pNameErrorsMailbox,
                NULL,
                0,
                IS_UNICODE_RECORD(pRR) );
    if ( !pch )
    {
        SetLastError( ERROR_MORE_DATA );
        return( NULL );
    }

    return( pch );
}



PCHAR
Flat_RecordWrite(
    IN OUT  PDNS_RECORD     pRR,
    IN      PCHAR           pch,
    IN      PCHAR           pchEnd
    )
/*++

Routine Description:

    Write flat record type data to packet.
    Flat types include:
        AAAA
        WINS

Arguments:

    pRR - ptr to record to write

    pch - ptr to position in buffer to write

    pchEnd - end of buffer position

Return Value:

    Ptr to next byte in buffer to write.
    NULL on error.  (Error code through GetLastError())

--*/
{
    WORD    length = pRR->wDataLength;

    if ( pch + length > pchEnd )
    {
        SetLastError( ERROR_MORE_DATA );
        return( NULL );
    }

    memcpy(
        pch,
        (PCHAR)&pRR->Data,
        length );

    pch += length;
    return( pch );
}



PCHAR
Srv_RecordWrite(
    IN OUT  PDNS_RECORD     pRR,
    IN      PCHAR           pch,
    IN      PCHAR           pchEnd
    )
/*++

Routine Description:

    Write SRV record to wire.

Arguments:

    pRR - ptr to record to write

    pch - ptr to position in buffer to write

    pchEnd - end of buffer position

Return Value:

    Ptr to next byte in buffer to write.
    NULL on error.  (Error code through GetLastError())

--*/
{
    PCHAR   pchname;
    PWORD   pword;

    //
    //  SRV integer values
    //

    pchname = pch + SIZEOF_SRV_FIXED_DATA;
    if ( pchname > pchEnd )
    {
        SetLastError( ERROR_MORE_DATA );
        return( NULL );
    }
    pword = &pRR->Data.SRV.wPriority;
    while ( pch < pchname )
    {
        *(UNALIGNED WORD *) pch = htons( *pword++ );
        pch += sizeof(WORD);
    }

    //
    //  SRV target host
    //

    pch = Dns_WriteDottedNameToPacket(
                pch,
                pchEnd,
                pRR->Data.SRV.pNameTarget,
                NULL,
                0,
                IS_UNICODE_RECORD(pRR) );
    if ( !pch )
    {
        SetLastError( ERROR_MORE_DATA );
    }
    return( pch );
}


PCHAR
Atma_RecordWrite(
    IN OUT  PDNS_RECORD     pRR,
    IN      PCHAR           pch,
    IN      PCHAR           pchEnd
    )
/*++

Routine Description:

    Write ATMA record to wire.

Arguments:

    pRR - ptr to record to write

    pch - ptr to position in buffer to write

    pchEnd - end of buffer position

Return Value:

    Ptr to next byte in buffer to write.
    NULL on error.  (Error code through GetLastError())

--*/
{
    PBYTE  pbyte;

    //
    //  ATMA integer values
    //

    if ( ( pch + sizeof( DNS_ATMA_DATA ) + DNS_ATMA_MAX_ADDR_LENGTH ) >
         pchEnd )
    {
        SetLastError( ERROR_MORE_DATA );
        return( NULL );
    }

    pbyte = &pRR->Data.ATMA.AddressType;
    *(BYTE *) pch = *pbyte;
    pch += sizeof( BYTE );

    //
    // write ATMA address
    //
    memcpy( pch,
            (PCHAR)&pRR->Data.ATMA.Address,
            pRR->wDataLength );

    pch += pRR->wDataLength;

    return( pch );
}


//
//  stubs until read to go
//

PCHAR
Tkey_RecordWrite(
    IN OUT  PDNS_RECORD     pRR,
    IN      PCHAR           pch,
    IN      PCHAR           pchEnd
    )
{
    return( NULL );
}

PCHAR
Tsig_RecordWrite(
    IN OUT  PDNS_RECORD     pRR,
    IN      PCHAR           pch,
    IN      PCHAR           pchEnd
    )
{
    return( NULL );
}




//
//  RR write to packet jump table
//

RR_WRITE_FUNCTION   RR_WriteTable[] =
{
    NULL,               //  ZERO
    A_RecordWrite,      //  A
    Ptr_RecordWrite,    //  NS
    Ptr_RecordWrite,    //  MD
    Ptr_RecordWrite,    //  MF
    Ptr_RecordWrite,    //  CNAME
    Soa_RecordWrite,    //  SOA
    Ptr_RecordWrite,    //  MB
    Ptr_RecordWrite,    //  MG
    Ptr_RecordWrite,    //  MR
    NULL,               //  NULL
    NULL,   //Wks_RecordWrite,     //  WKS
    Ptr_RecordWrite,    //  PTR
    Txt_RecordWrite,    //  HINFO
    Minfo_RecordWrite,  //  MINFO
    Mx_RecordWrite,     //  MX
    Txt_RecordWrite,    //  TXT
    Minfo_RecordWrite,  //  RP
    Mx_RecordWrite,     //  AFSDB
    Txt_RecordWrite,    //  X25
    Txt_RecordWrite,    //  ISDN
    Mx_RecordWrite,     //  RT
    NULL,               //  NSAP
    NULL,               //  NSAPPTR
    NULL,               //  SIG
    NULL,               //  KEY
    NULL,               //  PX
    NULL,               //  GPOS
    Flat_RecordWrite,   //  AAAA
    NULL,               //  LOC
    NULL,               //  NXT
    NULL,               //  EID   
    NULL,               //  NIMLOC
    Srv_RecordWrite,    //  SRV   
    Atma_RecordWrite,   //  ATMA  
    NULL,               //  NAPTR 
    NULL,               //  KX    
    NULL,               //  CERT  
    NULL,               //  A6    
    NULL,               //  DNAME 
    NULL,               //  SINK  
    NULL,               //  OPT   
    NULL,               //  42
    NULL,               //  43
    NULL,               //  44
    NULL,               //  45
    NULL,               //  46
    NULL,               //  47
    NULL,               //  48

    //
    //  NOTE:  last type indexed by type ID MUST be set
    //         as MAX_SELF_INDEXED_TYPE #define in record.h
    //         (see note above in record info table)

    //
    //  Pseudo record types
    //

    Tkey_RecordWrite,   //  TKEY
    Tsig_RecordWrite,   //  TSIG

    //
    //  MS only types
    //

    Flat_RecordWrite,   //  WINS
    NULL,               //  WINSR
};

//
//  End rrwire.c
//
