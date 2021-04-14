#include "std.hxx"

#ifdef DEBUG
#include <stdarg.h>
#endif

VOID UtilLoadDbinfomiscFromPdbfilehdr(
	JET_DBINFOMISC *pdbinfomisc,
	DBFILEHDR_FIX *pdbfilehdr )
	{
	pdbinfomisc->ulVersion			= pdbfilehdr->le_ulVersion;
	pdbinfomisc->dbstate			= pdbfilehdr->le_dbstate;				
	pdbinfomisc->signDb				= *(JET_SIGNATURE *) &(pdbfilehdr->signDb);	
	pdbinfomisc->lgposConsistent	= *(JET_LGPOS *) &(pdbfilehdr->le_lgposConsistent);
	pdbinfomisc->logtimeConsistent	= *(JET_LOGTIME *) &(pdbfilehdr->logtimeConsistent);	
	pdbinfomisc->logtimeAttach		= *(JET_LOGTIME *) &(pdbfilehdr->logtimeAttach);
	pdbinfomisc->lgposAttach		= *(JET_LGPOS *) &(pdbfilehdr->le_lgposAttach);
	pdbinfomisc->logtimeDetach		= *(JET_LOGTIME *) &(pdbfilehdr->logtimeDetach);
	pdbinfomisc->lgposDetach		= *(JET_LGPOS *) &(pdbfilehdr->le_lgposDetach);
	pdbinfomisc->signLog			= *(JET_SIGNATURE *) &(pdbfilehdr->signLog);
	pdbinfomisc->bkinfoFullPrev		= *(JET_BKINFO *) &(pdbfilehdr->bkinfoFullPrev);
	pdbinfomisc->bkinfoIncPrev		= *(JET_BKINFO *) &(pdbfilehdr->bkinfoIncPrev);
	pdbinfomisc->bkinfoFullCur		= *(JET_BKINFO *) &(pdbfilehdr->bkinfoFullCur);
	pdbinfomisc->fShadowingDisabled	= pdbfilehdr->FShadowingDisabled();
	pdbinfomisc->dwMajorVersion		= pdbfilehdr->le_dwMajorVersion;
	pdbinfomisc->dwMinorVersion		= pdbfilehdr->le_dwMinorVersion;
	pdbinfomisc->dwBuildNumber		= pdbfilehdr->le_dwBuildNumber;
	pdbinfomisc->lSPNumber			= pdbfilehdr->le_lSPNumber;
	pdbinfomisc->ulUpdate			= pdbfilehdr->le_ulUpdate;
	pdbinfomisc->cbPageSize			= pdbfilehdr->le_cbPageSize == 0 ? (ULONG) g_cbPageDefault : (ULONG) pdbfilehdr->le_cbPageSize;
	}
	

CODECONST(unsigned char) mpcoltypcb[] =
	{
	0,					/* JET_coltypNil (coltypNil is used for vltUninit parms) */
	sizeof(char),		/* JET_coltypBit */
	sizeof(char),		/* JET_coltypUnsignedByte */
	sizeof(short),		/* JET_coltypShort */
	sizeof(long),		/* JET_coltypLong */
	sizeof(long)*2,		/* JET_coltypCurrency */
	sizeof(float),		/* JET_coltypIEEESingle */
	sizeof(double),		/* JET_coltypIEEEDouble */
	sizeof(double),		/* JET_coltypDateTime */
	0,					/* JET_coltypBinary */
	0,					/* JET_coltypText */
	sizeof(long),		/* JET_coltypLongBinary */
	sizeof(long),		/* JET_coltypLongText */
	0,					/* JET_coltypDatabase */
	sizeof(JET_TABLEID)	/* JET_coltypTableid */
	};


LOCAL CODECONST(unsigned char) rgbValidName[16] = {
	0xff,			       /* 00-07 No control characters */
	0xff,			       /* 08-0F No control characters */
	0xff,			       /* 10-17 No control characters */
	0xff,			       /* 18-1F No control characters */
	0x02,			       /* 20-27 No ! */
	0x40,			       /* 28-2F No . */
	0x00,			       /* 30-37 */
	0x00,			       /* 38-3F */
	0x00,			       /* 40-47 */
	0x00,			       /* 48-4F */
	0x00,			       /* 50-57 */
	0x28,			       /* 58-5F No [ or ] */
	0x00,			       /* 60-67 */
	0x00,			       /* 68-6F */
	0x00,			       /* 70-77 */
	0x00,			       /* 78-7F */
	};

//	WARNING: Assumes an output buffer of JET_cbNameMost+1
//
ERR ErrUTILICheckName(
	CHAR * const		szNewName,
	const CHAR * const	szName, 
	const BOOL			fTruncate )
	{
	CHAR *				pchLast		= szNewName;
	SIZE_T				cch;
	BYTE				ch;

	//	a name may not begin with a space
	if ( ' ' == *szName )
		return ErrERRCheck( JET_errInvalidName );

	for ( cch = 0;
		cch < JET_cbNameMost && ( ( ch = (BYTE)szName[cch] ) != '\0' );
		cch++ )
		{
		//	extended characters always valid
		if ( ch < 0x80 )
			{
			if ( ( rgbValidName[ch >> 3] >> (ch & 0x7) ) & 1 )
				return ErrERRCheck( JET_errInvalidName );
			}

		szNewName[cch] = (CHAR)ch;

		//	last significant character
		if ( ' ' != ch )
			pchLast = szNewName + cch + 1;
		}

	//	check name too long
	//	UNDONE: insignificant trailing spaces that cause
	//	the length of the name to exceed cbNameMost will
	//	currently trigger an error
	if ( JET_cbNameMost == cch )
		{
		if ( !fTruncate && '\0' != szName[JET_cbNameMost] )
			return ErrERRCheck( JET_errInvalidName );
		}

	//	length of significant portion
	Assert( pchLast >= szNewName );
	Assert( pchLast <= szNewName + JET_cbNameMost );
	cch = pchLast - szNewName;

	if ( 0 == cch )
		return ErrERRCheck( JET_errInvalidName );

	//	we assume an output buffer of JET_cbNameMost+1
	Assert( cch <= JET_cbNameMost );
	szNewName[cch] = '\0';

	return JET_errSuccess;
	}


//	WARNING: Assumes an output buffer of IFileSystemAPI::cchPathMax
//
ERR ErrUTILICheckPathName(
	CHAR * const		szNewName,
	const CHAR * const	szName, 
	const BOOL			fTruncate )
	{
	SIZE_T				ichT;

	//	path may not begin with a space
	//
	if ( ' ' == *szName )
		{
		return ErrERRCheck( JET_errInvalidPath );
		}

	for ( ichT = 0;
		( ( ichT < IFileSystemAPI::cchPathMax ) && ( szName[ichT] != '\0' ) );
		ichT++ )
		{
			szNewName[ichT] = szName[ichT];
		}

	//	check for empty path
	//
	if ( 0 == ichT )
		{
		return ErrERRCheck( JET_errInvalidPath );
		}	

	//	check name too long
	//
	//	FUTURE: insignificant trailing spaces
	//	that cause the length of the name to exceed IFileSystemAPI::cchPathMax
	//	will currently trigger an error.
	//
	if ( IFileSystemAPI::cchPathMax == ichT )
		{
		if ( !fTruncate )
			{
			return ErrERRCheck( JET_errInvalidPath );
			}
		else
			{
			ichT = IFileSystemAPI::cchPathMax - 1;
			}
		}

	//	we assume an output buffer of IFileSystemAPI::cchPathMax
	//
	Assert( ichT < IFileSystemAPI::cchPathMax );
	szNewName[ichT] = '\0';

	return JET_errSuccess;
	}


#ifdef DEBUG

typedef void ( *PFNvprintf)(const char  *, va_list);

struct {
	PFNvprintf pfnvprintf;
	}  pfn = { NULL };


void VARARG DebugPrintf(const char  *szFmt, ...)
	{
	va_list arg_ptr;

	if (pfn.pfnvprintf == NULL)	       /* No op if no callback registered */
		return;

	va_start(arg_ptr, szFmt);
	(*pfn.pfnvprintf)(szFmt, arg_ptr);
	va_end(arg_ptr);
	}


	/*	The following pragma affects the code generated by the C
	/*	compiler for all FAR functions.  Do NOT place any non-API
	/*	functions beyond this point in this file.
	/**/

void JET_API JetDBGSetPrintFn(JET_SESID sesid, PFNvprintf pfnParm)
	{
	Unused( sesid );
	
	pfn.pfnvprintf = pfnParm;
	}

/*
 *	level 0 - all log s.
 *	level 1 - log read and update operations.
 *	level 2 - log update operations only.
 *	level 99- never log
 */

LOCAL CODECONST(unsigned char) mpopLogLevel[opMax] = {
/*							0	*/		0,
/*  opIdle					1	*/		2,
/*	opGetTableIndexInfo		2	*/		1,
/*	opGetIndexInfo			3	*/		1,
/*	opGetObjectInfo			4	*/		1,
/*	opGetTableInfo			5	*/		1,
/*	opCreateObject			6	*/		2,
/*	opDeleteObject			7	*/		2,
/*	opRenameObject			8	*/		2,
/*	opBeginTransaction		9	*/		2,
/*	opCommitTransaction		10	*/		2,
/*	opRollback				11	*/		2,
/*	opOpenTable				12	*/		1,
/*	opDupCursor				13	*/		1,
/*	opCloseTable			14	*/		1,
/*	opGetTableColumnInfo	15	*/		1,
/*	opGetColumnInfo			16	*/		1,
/*	opRetrieveColumn		17	*/		1,
/*	opRetrieveColumns		18	*/		1,
/*	opSetColumn				19	*/		2,
/*	opSetColumns			20	*/		2,
/*	opPrepareUpdate			21	*/		2,
/*	opUpdate				22	*/		2,
/*	opDelete				23	*/		2,
/*	opGetCursorInfo			24	*/		1,
/*	opGetCurrentIndex		25	*/		1,
/*	opSetCurrentIndex		26	*/		1,
/*	opMove					27	*/		1,
/*	opMakeKey				28	*/		1,
/*	opSeek					29	*/		1,
/*	opGetBookmark			30	*/		1,
/*	opGotoBookmark			31	*/		1,
/*	opGetRecordPosition		32	*/		1,
/*	opGotoPosition			33	*/		1,
/*	opRetrieveKey			34	*/		1,
/*	opCreateDatabase		35	*/		2,
/*	opOpenDatabase			36	*/		1,
/*	opGetDatabaseInfo		37	*/		1,
/*	opCloseDatabase			38	*/		1,
/*	opCapability			39	*/		1,
/*	opCreateTable			40	*/		2,
/*	opRenameTable			41	*/		2,
/*	opDeleteTable			42	*/		2,
/*	opAddColumn				43	*/		2,
/*	opRenameColumn			44	*/		2,
/*	opDeleteColumn			45	*/		2,
/*	opCreateIndex			46	*/		2,
/*	opRenameIndex			47	*/		2,
/*	opDeleteIndex			48	*/		2,
/*	opComputeStats			49	*/		2,
/*	opAttachDatabase		50	*/		2,
/*	opDetachDatabase		51	*/		2,
/*	opOpenTempTable			52	*/		2,
/*	opSetIndexRange			53	*/		1,
/*	opIndexRecordCount		54	*/		1,
/*	opGetChecksum			55	*/		1,
/*	opGetObjidFromName		56	*/		1,
/*	opEscrowUpdate			57	*/		1,
/*	opGetLock				58	*/		1,
/*	opRetrieveTaggedColumnList	59	*/	1,
/*	opCreateTableColumnIndex	60	*/	2,
/*	opSetColumnDefaultValue	61	*/		2,
/*	opPrepareToCommitTransaction 62 */	2,
/*	opSetTableSequential	63	*/		99,
/*	opResetTableSequential	64	*/		99,
/*	opRegisterCallback		65	*/		99,
/*	opUnregisterCallback	66	*/		99,
/*	opSetLS					67	*/		99,
/*	opGetLS					68	*/		99,
/*	opGetVersion			69	*/		99,
/*	opBeginSession			70	*/		99,
/*	opDupSession			71	*/		99,
/*	opEndSession			72	*/		99,
/*	opBackupInstance		73	*/		99,
/*	opBeginExternalBackupInstance 74 */	99,
/*	opGetAttachInfoInstance	75	*/		99,
/*	opOpenFileInstance		76	*/		99,
/*	opReadFileInstance		77	*/		99,
/*	opCloseFileInstance		78	*/		99,
/*	opGetLogInfoInstance	79	*/		99,
/*	opGetTruncateLogInfoInstance 80 */	99,
/*	opTruncateLogInstance	81	*/		99,
/*	opEndExternalBackupInstance	82	*/	99,
/*	opSnapshotStart			83	*/		99,
/*	opSnapshotStop			84	*/		99,
/*	opResetCounter			85	*/		99,
/*	opGetCounter			86	*/		99,
/*	opCompact				87	*/		99,
/*	opConvertDDL			88	*/		99,
/*	opUpgradeDatabase		89	*/		99,
/*	opDefragment			90	*/		99,
/*	opSetDatabaseSize		91	*/		99,
/*	opGrowDatabase			92	*/		99,
/*	opSetSessionContext		93	*/		99,
/*	opResetSessionContext	94	*/		99,
/*	opSetSystemParameter	95	*/		99,
/*	opGetSystemParameter	96	*/		99,
/*	opTerm					97	*/		99,
/*	opInit					98	*/		99,
/*	opIntersectIndexes		99	*/		99,
/*	opDBUtilities			100	*/		99,
/*	opEnumerateColumns		101	*/		1,
};

/* function in logapi to store jetapi calls */
extern void LGJetOp( JET_SESID sesid, int op );

void DebugLogJetOp( JET_SESID sesid, int op )
	{
	Unused( sesid );
	
	// UNDONE: should be controlled by a system parameter to decide
	// UNDONE: which log level it should be.

	/* log level 2 operations */
	if ( op < opMax && mpopLogLevel[ op ] >= lAPICallLogLevel )
		{
//		LGJetOp( sesid, op );
		}
	}

#endif	/* DEBUG */
