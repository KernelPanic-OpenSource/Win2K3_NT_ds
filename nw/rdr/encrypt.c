/*++

Copyright (c) 1993  Microsoft Corporation

Module Name:

    encrypt.c

Abstract:

    This module implements the routines for the NetWare
    redirector to mangle an objectid, challenge key and
    password such that a NetWare server will accept the
    password as valid.

    This program uses information published in Byte Magazine.

Author:

    Colin Watson    [ColinW]    15-Mar-1993

Revision History:

--*/

#include <procs.h>


UCHAR Table[] = {
    0x78, 0x08, 0x64, 0xe4, 0x5c, 0x17, 0xbf, 0xa8,
    0xf8, 0xcc, 0x94, 0x1e, 0x46, 0x24, 0x0a, 0xb9,
    0x2f, 0xb1, 0xd2, 0x19, 0x5e, 0x70, 0x02, 0x66,
    0x07, 0x38, 0x29, 0x3f, 0x7f, 0xcf, 0x64, 0xa0,
    0x23, 0xab, 0xd8, 0x3a, 0x17, 0xcf, 0x18, 0x9d,
    0x91, 0x94, 0xe4, 0xc5, 0x5c, 0x8b, 0x23, 0x9e,
    0x77, 0x69, 0xef, 0xc8, 0xd1, 0xa6, 0xed, 0x07,
    0x7a, 0x01, 0xf5, 0x4b, 0x7b, 0xec, 0x95, 0xd1,
    0xbd, 0x13, 0x5d, 0xe6, 0x30, 0xbb, 0xf3, 0x64,
    0x9d, 0xa3, 0x14, 0x94, 0x83, 0xbe, 0x50, 0x52,
    0xcb, 0xd5, 0xd5, 0xd2, 0xd9, 0xac, 0xa0, 0xb3,
    0x53, 0x69, 0x51, 0xee, 0x0e, 0x82, 0xd2, 0x20,
    0x4f, 0x85, 0x96, 0x86, 0xba, 0xbf, 0x07, 0x28,
    0xc7, 0x3a, 0x14, 0x25, 0xf7, 0xac, 0xe5, 0x93,
    0xe7, 0x12, 0xe1, 0xf4, 0xa6, 0xc6, 0xf4, 0x30,
    0xc0, 0x36, 0xf8, 0x7b, 0x2d, 0xc6, 0xaa, 0x8d } ;


UCHAR Keys[32] =
{0x48,0x93,0x46,0x67,0x98,0x3D,0xE6,0x8D,
 0xB7,0x10,0x7A,0x26,0x5A,0xB9,0xB1,0x35,
 0x6B,0x0F,0xD5,0x70,0xAE,0xFB,0xAD,0x11,
 0xF4,0x47,0xDC,0xA7,0xEC,0xCF,0x50,0xC0};

#define XorArray( DEST, SRC ) {                             \
    PULONG D = (PULONG)DEST;                                \
    PULONG S = (PULONG)SRC;                                 \
    int i;                                                  \
    for ( i = 0; i <= 7 ; i++ ) {                           \
        D[i] ^= S[i];                                       \
    }                                                       \
}

VOID
Shuffle(
    UCHAR *achObjectId,
    UCHAR *szUpperPassword,
    int   iPasswordLen,
    UCHAR *achOutputBuffer
    );

int
Scramble(
    int   iSeed,
    UCHAR   achBuffer[32]
    );

#ifdef ALLOC_PRAGMA
#pragma alloc_text( PAGE, RespondToChallenge )
#pragma alloc_text( PAGE, Shuffle )
#pragma alloc_text( PAGE, Scramble )
#endif


VOID
RespondToChallenge(
    IN PUCHAR achObjectId,
    IN POEM_STRING Password,
    IN PUCHAR pChallenge,
    OUT PUCHAR pResponse
    )

/*++

Routine Description:

    This routine takes the ObjectId and Challenge key from the server and
    encrypts the user supplied password to develop a credential for the
    server to verify.

Arguments:
    IN PUCHAR achObjectId - Supplies the 4 byte user's bindery object id
    IN POEM_STRING Password - Supplies the user's uppercased password
    IN PUCHAR pChallenge - Supplies the 8 byte challenge key
    OUT PUCHAR pResponse - Returns the 8 byte response

Return Value:

    none.

--*/

{
    int     index;
    UCHAR   achK[32];
    UCHAR   achBuf[32];

    PAGED_CODE();

    Shuffle(achObjectId, Password->Buffer, Password->Length, achBuf);
    Shuffle( &pChallenge[0], achBuf, 16, &achK[0] );
    Shuffle( &pChallenge[4], achBuf, 16, &achK[16] );

    for (index = 0; index < 16; index++)
        achK[index] ^= achK[31-index];

    for (index = 0; index < 8; index++)
        pResponse[index] = achK[index] ^ achK[15-index];
}


VOID
Shuffle(
    UCHAR *achObjectId,
    UCHAR *szUpperPassword,
    int   iPasswordLen,
    UCHAR *achOutputBuffer
    )

/*++

Routine Description:

    This routine shuffles around the object ID with the password

Arguments:

    IN achObjectId - Supplies the 4 byte user's bindery object id

    IN szUpperPassword - Supplies the user's uppercased password on the
        first call to process the password. On the second and third calls
        this parameter contains the OutputBuffer from the first call

    IN iPasswordLen - length of uppercased password

    OUT achOutputBuffer - Returns the 8 byte sub-calculation

Return Value:

    none.

--*/

{
    int     iTempIndex;
    int     iOutputIndex;
    UCHAR   achTemp[32];

    PAGED_CODE();

    //
    //  Truncate all trailing zeros from the password.
    //

    while (iPasswordLen > 0 && szUpperPassword[iPasswordLen-1] == 0 ) {
        iPasswordLen--;
    }

    //
    //  Initialize the achTemp buffer. Initialization consists of taking
    //  the password and dividing it up into chunks of 32. Any bytes left
    //  over are the remainder and do not go into the initialization.
    //
    //  achTemp[0] = szUpperPassword[0] ^ szUpperPassword[32] ^ szUpper...
    //  achTemp[1] = szUpperPassword[1] ^ szUpperPassword[33] ^ szUpper...
    //  etc.
    //

    if ( iPasswordLen > 32) {

        //  At least one chunk of 32. Set the buffer to the first chunk.

        RtlCopyMemory( achTemp, szUpperPassword, 32 );

        szUpperPassword +=32;   //  Remove the first chunk
        iPasswordLen -=32;

        while ( iPasswordLen >= 32 ) {
            //
            //  Xor this chunk with the characters already loaded into
            //  achTemp.
            //

            XorArray( achTemp, szUpperPassword);

            szUpperPassword +=32;   //  Remove this chunk
            iPasswordLen -=32;
        }

    } else {

        //  No chunks of 32 so set the buffer to zero's

        RtlZeroMemory( achTemp, sizeof(achTemp));

    }

    //
    //  achTemp is now initialized. Load the remainder into achTemp.
    //  The remainder is repeated to fill achTemp.
    //
    //  The corresponding character from Keys is taken to seperate
    //  each repitition.
    //
    //  As an example, take the remainder "ABCDEFG". The remainder is expanded
    //  to "ABCDEFGwABCDEFGxABCDEFGyABCDEFGz" where w is Keys[7],
    //  x is Keys[15], y is Keys[23] and z is Keys[31].
    //
    //

    if (iPasswordLen > 0) {
        int iPasswordOffset = 0;
        for (iTempIndex = 0; iTempIndex < 32; iTempIndex++) {

            if (iPasswordLen == iPasswordOffset) {
                iPasswordOffset = 0;
                achTemp[iTempIndex] ^= Keys[iTempIndex];
            } else {
                achTemp[iTempIndex] ^= szUpperPassword[iPasswordOffset++];
            }
        }
    }

    //
    //  achTemp has been loaded with the users password packed into 32
    //  bytes. Now take the objectid that came from the server and use
    //  that to munge every byte in achTemp.
    //

    for (iTempIndex = 0; iTempIndex < 32; iTempIndex++)
        achTemp[iTempIndex] ^= achObjectId[ iTempIndex & 3];

    Scramble( Scramble( 0, achTemp ), achTemp );

    //
    //  Finally take pairs of bytes in achTemp and return the two
    //  nibbles obtained from Table. The pairs of bytes used
    //  are achTemp[n] and achTemp[n+16].
    //

    for (iOutputIndex = 0; iOutputIndex < 16; iOutputIndex++) {

        unsigned int offset = achTemp[iOutputIndex << 1],
                     shift  = (offset & 0x1) ? 0 : 4 ;

        achOutputBuffer[iOutputIndex] =
            (Table[offset >> 1] >> shift) & 0xF ;

        offset = achTemp[(iOutputIndex << 1)+1],
        shift = (offset & 0x1) ? 4 : 0 ;

        achOutputBuffer[iOutputIndex] |=
            (Table[offset >> 1] << shift) & 0xF0;
    }

    return;
}

int
Scramble(
    int   iSeed,
    UCHAR   achBuffer[32]
    )

/*++

Routine Description:

    This routine scrambles around the contents of the buffer. Each buffer
    position is updated to include the contents of at least two character
    positions plus an EncryptKey value. The buffer is processed left to right
    and so if a character position chooses to merge with a buffer position
    to its left then this buffer position will include bits derived from at
    least 3 bytes of the original buffer contents.

Arguments:

    IN iSeed
    IN OUT achBuffer[32]

Return Value:

    none.

--*/

{
    int iBufferIndex;

    PAGED_CODE();

    for (iBufferIndex = 0; iBufferIndex < 32; iBufferIndex++) {
        achBuffer[iBufferIndex] =
            (UCHAR)(
                ((UCHAR)(achBuffer[iBufferIndex] + iSeed)) ^
                ((UCHAR)(   achBuffer[(iBufferIndex+iSeed) & 31] -
                    Keys[iBufferIndex] )));

        iSeed += achBuffer[iBufferIndex];
    }
    return iSeed;
}

