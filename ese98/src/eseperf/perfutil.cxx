#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <tchar.h>
#include <aclapi.h>

#include "perfutil.hxx"
#include "perfmon.hxx"

#include <overflow.h>


	/*  DLL entry point for JETPERF.DLL  */

extern "C"
	{
	BOOL WINAPI DLLEntryPoint( HINSTANCE, DWORD, LPVOID )
		{
		return( TRUE );
		}
	}


	/*  Registry support functions  */

DWORD DwPerfUtilRegOpenKeyEx(HKEY hkeyRoot,LPCTSTR lpszSubKey,PHKEY phkResult)
{
	return RegOpenKeyEx(hkeyRoot,lpszSubKey,0,KEY_QUERY_VALUE,phkResult);
}


DWORD DwPerfUtilRegCloseKeyEx(HKEY hkey)
{
	return RegCloseKey(hkey);
}


DWORD DwPerfUtilRegCreateKeyEx(HKEY hkeyRoot,LPCTSTR lpszSubKey,PHKEY phkResult,LPDWORD lpdwDisposition)
{
	return RegCreateKeyEx(hkeyRoot,lpszSubKey,0,NULL,REG_OPTION_NON_VOLATILE,
		KEY_ALL_ACCESS,NULL,phkResult,lpdwDisposition);
}


DWORD DwPerfUtilRegDeleteKeyEx(HKEY hkeyRoot,LPCTSTR lpszSubKey)
{
	return RegDeleteKey(hkeyRoot,lpszSubKey);
}


DWORD DwPerfUtilRegDeleteValueEx(HKEY hkey,LPTSTR lpszValue)
{
	return RegDeleteValue(hkey,lpszValue);
}


DWORD DwPerfUtilRegSetValueEx(HKEY hkey,LPCTSTR lpszValue,DWORD fdwType,CONST BYTE *lpbData,DWORD cbData)
{
		/*  make sure type is set correctly by deleting value first  */

	(VOID)DwPerfUtilRegDeleteValueEx(hkey,(LPTSTR)lpszValue);
	return RegSetValueEx(hkey,lpszValue,0,fdwType,lpbData,cbData);
}


	/*  DwPerfUtilRegQueryValueEx() adds to the functionality of RegQueryValueEx() by returning
	/*  the data in callee malloc()ed memory and automatically converting REG_EXPAND_SZ
	/*  strings using ExpandEnvironmentStrings() to REG_SZ strings.
	/*
	/*  NOTE:  references to nonexistent env vbles will be left unexpanded :-(  (Ex.  %UNDEFD% => %UNDEFD%)
	/**/

DWORD DwPerfUtilRegQueryValueEx(HKEY hkey,LPTSTR lpszValue,LPDWORD lpdwType,LPBYTE *lplpbData)
{
	DWORD cbData;
	LPBYTE lpbData;
	DWORD errWin;
	DWORD cbDataExpanded;
	LPBYTE lpbDataExpanded;

	*lplpbData = NULL;
	if ((errWin = RegQueryValueEx(hkey,lpszValue,0,lpdwType,NULL,&cbData)) != ERROR_SUCCESS)
		return errWin;

	if ((lpbData = reinterpret_cast<LPBYTE>(malloc(cbData))) == NULL)
		return ERROR_OUTOFMEMORY;
	if ((errWin = RegQueryValueEx(hkey,lpszValue,0,lpdwType,lpbData,&cbData)) != ERROR_SUCCESS)
	{
		free(lpbData);
		return errWin;
	}

	if (*lpdwType == REG_EXPAND_SZ)
	{
		cbDataExpanded = ExpandEnvironmentStrings((const char *)lpbData,NULL,0);
		if ((lpbDataExpanded = reinterpret_cast<LPBYTE>(malloc(cbDataExpanded))) == NULL)
		{
			free(lpbData);
			return ERROR_OUTOFMEMORY;
		}
		if (!ExpandEnvironmentStrings((const char *)lpbData,(char *)lpbDataExpanded,cbDataExpanded))
		{
			free(lpbData);
			free(lpbDataExpanded);
			return GetLastError();
		}
		free(lpbData);
		*lplpbData = lpbDataExpanded;
		*lpdwType = REG_SZ;
	}
	else  /*  lpdwType != REG_EXPAND_SZ  */
	{
		*lplpbData = lpbData;
	}

	return ERROR_SUCCESS;
}


	/*  shared performance data area resources  */

HANDLE	hPERFInstanceMutex	= NULL;
HANDLE	hPERFGDAMMF			= NULL;
PGDA	pgdaPERFGDA			= NULL;


	/*  Event Logging support  */

HANDLE hOurEventSource = NULL;

void PerfUtilLogEvent( DWORD evncat, WORD evntyp, const char *szDescription )
{
    const char *rgsz[3];

    	/*  convert args from internal types to event log types  */

	rgsz[0]	= "";
	rgsz[1] = "";
	rgsz[2] = (char *)szDescription;

		/*  write to our event log, if it has been opened  */

	if (hOurEventSource)
	{
		ReportEvent(
			hOurEventSource,
			(WORD)evntyp,
			(WORD)evncat,
			0,
			0,
			3,
			0,
			rgsz,
			0 );
	}

	return;
}


	/*  Init/Term routines for system indirection layer  */

DWORD dwInitCount = 0;

inline _TCHAR* SzPerfGlobal()
	{
	OSVERSIONINFO osverinfo;
	osverinfo.dwOSVersionInfoSize = sizeof( OSVERSIONINFO );
	if ( !GetVersionEx( &osverinfo ) ) 
		{
		return _T("");
		}
	//	Under Win2000 terminal server object names must be preceded by Global\
	//	to share the same name space
	return ( VER_PLATFORM_WIN32_NT == osverinfo.dwPlatformId && 5 <= osverinfo.dwMajorVersion )? _T("Global\\"): _T("");
	}

DWORD DwPerfUtilInit( VOID )
{
	DWORD						err							= ERROR_SUCCESS;
	PSECURITY_DESCRIPTOR		pSD							= NULL;
	DWORD						cbSD;
	SECURITY_ATTRIBUTES			sa							= { 0 };
	_TCHAR						szT[ 256 ];

			/*  if we haven't been initialized already, perform init  */

	if (!dwInitCount)
	{
			/*  open the event log  */

	    if (!(hOurEventSource = RegisterEventSource( NULL, SZVERSIONNAME )))
	    	return GetLastError();


		//	generic read, write and execute granted to System, Built-in Administrators
		//	and Authenticated Users.  Authenticated users do also need write
		//	access to performance counter objects.
		//
	    //	D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;AU)
	    //
		if ( !ConvertStringSecurityDescriptorToSecurityDescriptor(
						"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;AU)",
						SDDL_REVISION_1,
						&pSD,
						&cbSD) )
			{
				err = GetLastError();
				goto CloseEventLog;
			}

		sa.nLength = cbSD;
	    sa.bInheritHandle = FALSE;
	    sa.lpSecurityDescriptor = pSD;

		/*  create/open the instance mutex, but do not acquire
		/**/
		_stprintf( szT, _T( "%sInstance:  %s" ), SzPerfGlobal(), szPERFVersion );
		if ( !( hPERFInstanceMutex = CreateMutex( &sa, FALSE, szT ) ) )
			{
			err = GetLastError();
			goto CloseEventLog;
			}

		/*  open/create the shared global data area
		/**/
		_stprintf( szT, _T( "%sGDA:  %s" ), SzPerfGlobal(), szPERFVersion );
		if ( !( hPERFGDAMMF = CreateFileMapping(	INVALID_HANDLE_VALUE,
													&sa,
													PAGE_READWRITE | SEC_COMMIT,
													0,
													PERF_SIZEOF_GLOBAL_DATA,
													szT ) ) )
			{
			err = GetLastError();
			goto FreeInstanceMutex;
			}
		if ( !( pgdaPERFGDA = PGDA( MapViewOfFile(	hPERFGDAMMF,
													FILE_MAP_READ,
													0,
													0,
													0 ) ) ) )
			{
			err = GetLastError();
			goto CloseFileMap;
			}

	}

		/*  init succeeded   */

	dwInitCount++;

	if ( err != ERROR_SUCCESS )
		{
CloseFileMap:
		CloseHandle( hPERFGDAMMF );
		hPERFGDAMMF = NULL;
FreeInstanceMutex:
		CloseHandle( hPERFInstanceMutex );
		hPERFInstanceMutex = NULL;
CloseEventLog:
		DeregisterEventSource( hOurEventSource );
		hOurEventSource = NULL;
		}

	LocalFree( pSD );
	return err;
}


VOID PerfUtilTerm( VOID )
{
		/*  last one out, turn out the lights!  */

	if (!dwInitCount)
		return;
	dwInitCount--;
	if (!dwInitCount)
	{
		UnmapViewOfFile( pgdaPERFGDA );
		pgdaPERFGDA = NULL;

		CloseHandle( hPERFGDAMMF );
		hPERFGDAMMF = NULL;

		CloseHandle( hPERFInstanceMutex );
		hPERFInstanceMutex = NULL;

		DeregisterEventSource( hOurEventSource );
		hOurEventSource = NULL;
	}
}

