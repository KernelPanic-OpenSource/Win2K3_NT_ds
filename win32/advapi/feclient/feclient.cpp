/*++

Copyright (c) 1997-1999  Microsoft Corporation

Module Name:

    feclient.cpp

Abstract:

    This module implements stubs to call EFS Api

Author:

    Robert Reichel (RobertRe)
    Robert Gu (RobertG)

Revision History:

--*/

//
// Turn off lean and mean so we get wincrypt.h and winefs.h included
//

#undef WIN32_LEAN_AND_MEAN

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include <feclient.h>
#include <efsstruc.h>
#include <userenv.h>

#include <overflow.h>

//
// Constants used in export\import file
//

#define INISECTIONNAME   L"Encryption"
#define INIKEYNAME       L"Disable"
#define INIFILENAME      L"\\Desktop.ini"
#define TRUSTEDPEOPLE L"TrustedPeople"

#define BASIC_KEY_INFO  1
#define UPDATE_KEY_USED 0x100

#if DBG

ULONG DebugLevel = 0;

#endif


LPSTR   EfsOidlpstr  = szOID_KP_EFS;

//
// External prototypes
//
extern "C" {
DWORD
EfsReadFileRawRPCClient(
    IN      PFE_EXPORT_FUNC ExportCallback,
    IN      PVOID           CallbackContext,
    IN      PVOID           Context
    );

DWORD
EfsWriteFileRawRPCClient(
    IN      PFE_IMPORT_FUNC ImportCallback,
    IN      PVOID           CallbackContext,
    IN      PVOID           Context
    );

DWORD
EfsAddUsersRPCClient(
    IN LPCWSTR lpFileName,
    IN PENCRYPTION_CERTIFICATE_LIST pEncryptionCertificates
    );


DWORD
EfsRemoveUsersRPCClient(
    IN LPCWSTR lpFileName,
    IN PENCRYPTION_CERTIFICATE_HASH_LIST pHashes
    );

DWORD
EfsQueryRecoveryAgentsRPCClient(
    IN LPCWSTR lpFileName,
    OUT PENCRYPTION_CERTIFICATE_HASH_LIST * pRecoveryAgents
    );


DWORD
EfsQueryUsersRPCClient(
    IN LPCWSTR lpFileName,
    OUT PENCRYPTION_CERTIFICATE_HASH_LIST * pUsers
    );

DWORD
EfsSetEncryptionKeyRPCClient(
    IN PENCRYPTION_CERTIFICATE pEncryptionCertificate
    );

DWORD
EfsDuplicateEncryptionInfoRPCClient(
    IN LPCWSTR lpSrcFileName,
    IN LPCWSTR lpDestFileName,
    IN DWORD dwCreationDistribution, 
    IN DWORD dwAttributes, 
    IN PEFS_RPC_BLOB pRelativeSD,
    IN BOOL bInheritHandle
    );

DWORD
EfsFileKeyInfoRPCClient(
    IN      LPCWSTR        lpFileName,
    IN      DWORD          InfoClass,
    OUT     PEFS_RPC_BLOB  *KeyInfo
    );


}

//
// Exported function prototypes
//

DWORD
EfsClientEncryptFile(
    IN LPCWSTR      FileName
    );

DWORD
EfsClientDecryptFile(
    IN LPCWSTR      FileName,
    IN DWORD        Recovery
    );

BOOL
EfsClientFileEncryptionStatus(
    IN LPCWSTR      FileName,
    OUT LPDWORD     lpStatus
    );

DWORD
EfsClientOpenFileRaw(
    IN      LPCWSTR         lpFileName,
    IN      ULONG           Flags,
    OUT     PVOID *         Context
    );

DWORD
EfsClientReadFileRaw(
    IN      PFE_EXPORT_FUNC    ExportCallback,
    IN      PVOID           CallbackContext,
    IN      PVOID           Context
    );

DWORD
EfsClientWriteFileRaw(
    IN      PFE_IMPORT_FUNC    ImportCallback,
    IN      PVOID           CallbackContext,
    IN      PVOID           Context
    );

VOID
EfsClientCloseFileRaw(
    IN      PVOID           Context
    );

DWORD
EfsClientAddUsers(
    IN LPCTSTR lpFileName,
    IN PENCRYPTION_CERTIFICATE_LIST pEncryptionCertificates
    );

DWORD
EfsClientRemoveUsers(
    IN LPCTSTR lpFileName,
    IN PENCRYPTION_CERTIFICATE_HASH_LIST pHashes
    );

DWORD
EfsClientQueryRecoveryAgents(
    IN      LPCTSTR                             lpFileName,
    OUT     PENCRYPTION_CERTIFICATE_HASH_LIST * pRecoveryAgents
    );

DWORD
EfsClientQueryUsers(
    IN      LPCTSTR                             lpFileName,
    OUT     PENCRYPTION_CERTIFICATE_HASH_LIST * pUsers
    );

DWORD
EfsClientSetEncryptionKey(
    IN PENCRYPTION_CERTIFICATE pEncryptionCertificate
    );

VOID
EfsClientFreeHashList(
    IN PENCRYPTION_CERTIFICATE_HASH_LIST pHashList
    );

DWORD
EfsClientDuplicateEncryptionInfo(
    IN LPCWSTR lpSrcFile,
    IN LPCWSTR lpDestFile,
    IN DWORD dwCreationDistribution, 
    IN DWORD dwAttributes, 
    IN LPSECURITY_ATTRIBUTES lpSecurityAttributes
    );

BOOL
EfsClientEncryptionDisable(
    IN LPCWSTR DirPath,
    IN BOOL Disable
	);

DWORD
EfsClientFileKeyInfo(
    IN      LPCWSTR        lpFileName,
    IN      DWORD          InfoClass,
    OUT     PEFS_RPC_BLOB  *KeyInfo
    );

VOID
EfsClientFreeKeyInfo(
    IN PEFS_RPC_BLOB  pKeyInfo
    );

FE_CLIENT_DISPATCH_TABLE DispatchTable = {  EfsClientEncryptFile,
                                            EfsClientDecryptFile,
                                            EfsClientFileEncryptionStatus,
                                            EfsClientOpenFileRaw,
                                            EfsClientReadFileRaw,
                                            EfsClientWriteFileRaw,
                                            EfsClientCloseFileRaw,
                                            EfsClientAddUsers,
                                            EfsClientRemoveUsers,
                                            EfsClientQueryRecoveryAgents,
                                            EfsClientQueryUsers,
                                            EfsClientSetEncryptionKey,
                                            EfsClientFreeHashList,
                                            EfsClientDuplicateEncryptionInfo,
                                            EfsClientEncryptionDisable,
                                            EfsClientFileKeyInfo,
                                            EfsClientFreeKeyInfo
                                            };


FE_CLIENT_INFO ClientInfo = {
                            FE_REVISION_1_0,
                            &DispatchTable
                            };

//
// Internal function prototypes
//


BOOL
TranslateFileName(
    IN LPCWSTR FileName,
    OUT PUNICODE_STRING FullFileNameU
    );

BOOL
RemoteFile(
    IN LPCWSTR FileName
    );

extern "C"
BOOL
EfsClientInit(
    IN PVOID hmod,
    IN ULONG Reason,
    IN PCONTEXT Context
    )
{
    return( TRUE );
}

extern "C"
BOOL
FeClientInitialize(
    IN     DWORD           dwFeRevision,
    OUT    LPFE_CLIENT_INFO       *lpFeInfo
    )

/*++

Routine Description:

    description-of-function.

Arguments:

    dwFeRevision - Is the revision of the current FEAPI interface.

    lpFeInfo - On successful return, must contain a pointer to a structure
         describing the FE Client Interface.  Once returned, the FE Client
         must assume that the caller will continue to reference this table until
         an unload call has been made.  Any changes to this information, or
         deallocation of the memory containing the information may result in
         system corruptions.


Return Value:

    TRUE - Indicates the Client DLL successfully initialized.

    FALSE - Indicates the client DLL has not loaded.  More information may be
         obtained by calling GetLastError().

--*/

{

    *lpFeInfo = &ClientInfo;

    return( TRUE );
}

BOOL
TranslateFileName(
    IN LPCWSTR FileName,
    OUT PUNICODE_STRING FullFileNameU
    )

/*++

Routine Description:

    This routine takes the filename passed by the user and converts
    it to a fully qualified pathname in the passed Unicode string.

Arguments:

    FileName - Supplies the user-supplied file name.

    FullFileNameU - Returns the fully qualified pathname of the passed file.
        The buffer in this string is allocated out of heap memory and
        must be freed by the caller.

Return Value:

    TRUE on success, FALSE otherwise.

--*/


//
// Note: need to free the buffer of the returned string
//
{

    UNICODE_STRING FileNameU;
    LPWSTR SrcFileName = (LPWSTR)FileName;

    if (0 == FileName[0]) {
       SetLastError(ERROR_INVALID_PARAMETER);
       return FALSE;
    }
    FullFileNameU->Buffer = (PWSTR)RtlAllocateHeap( RtlProcessHeap(), 0, MAX_PATH * sizeof( WCHAR ));
    if (!FullFileNameU->Buffer) {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }

    FullFileNameU->MaximumLength = MAX_PATH * sizeof( WCHAR );

    FullFileNameU->Length = (USHORT)RtlGetFullPathName_U(
                                         SrcFileName,
                                         FullFileNameU->MaximumLength,
                                         FullFileNameU->Buffer,
                                         NULL
                                         );

    //
    // The return value is supposed to be the length of the filename, without counting
    // the trailing NULL character.  MAX_PATH is supposed be long enough to contain
    // the length of the file name and the trailing NULL, so what we get back had
    // better be less than MAX_PATH wchars.
    //

    if ( FullFileNameU->Length >= FullFileNameU->MaximumLength ){

        RtlFreeHeap( RtlProcessHeap(), 0, FullFileNameU->Buffer );
        FullFileNameU->Buffer = (PWSTR)RtlAllocateHeap( RtlProcessHeap(), 0, FullFileNameU->Length + sizeof(WCHAR));

        if (FullFileNameU->Buffer == NULL) {
            return( FALSE );
        }
        FullFileNameU->MaximumLength = FullFileNameU->Length + sizeof(WCHAR);

        FullFileNameU->Length = (USHORT)RtlGetFullPathName_U(
                                            SrcFileName,
                                            FullFileNameU->MaximumLength,
                                            FullFileNameU->Buffer,
                                            NULL
                                            );
    }


    if (FullFileNameU->Length == 0) {
        //
        // We failed for some reason
        //
    
        RtlFreeHeap( RtlProcessHeap(), 0, FullFileNameU->Buffer );
        return( FALSE );
    }
    
    return( TRUE );

}

BOOL
WriteEfsIni(
    IN LPCWSTR SectionName,
	IN LPCWSTR KeyName,
	IN LPCWSTR WriteValue,
	IN LPCWSTR IniFileName
	)
/*++

Routine Description:

    This routine writes to the ini file. A wrap of WritePrivateProfileString
    
Arguments:

    SectionName - Section name (Encryption).

    KeyName - Key name (Disable).
    
    WriteValue - The value to be write (1).
    
    IniFileName - The path for ini file (dir\desktop.ini).

Return Value:

    TRUE on success

--*/
{
    BOOL bRet;

	bRet = WritePrivateProfileString(
                SectionName,
                KeyName,
                WriteValue,
                IniFileName
                );

    //
    // If SetFileAttributes fails, life should go on.
    //

    SetFileAttributes(IniFileName, FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN );

    return bRet;
}


BOOL
EfsClientEncryptionDisable(
    IN LPCWSTR DirPath,
    IN BOOL Disable
	)
/*++

Routine Description:

    This routine disable and enable EFS in the directory DirPath.
        
Arguments:

    DirPath - Directory path.

    Disable - TRUE to disable
    

Return Value:

    TRUE for SUCCESS

--*/
{
    LPWSTR IniFilePath;
    WCHAR  WriteValue[2];
    BOOL   bRet = FALSE;

    if (DirPath) {

        IniFilePath = (LPWSTR)RtlAllocateHeap( 
                                RtlProcessHeap(), 
                                0,
                                (wcslen(DirPath)+1+wcslen(INIFILENAME))*sizeof(WCHAR) 
                                );
        if (IniFilePath) {
            if (Disable) {
                wcscpy(WriteValue, L"1");
            } else {
                wcscpy(WriteValue, L"0");
            }
    
            wcscpy(IniFilePath, DirPath);
            wcscat(IniFilePath, INIFILENAME);
            bRet = WriteEfsIni(INISECTIONNAME, INIKEYNAME, WriteValue, IniFilePath);
            RtlFreeHeap( RtlProcessHeap(), 0, IniFilePath );
    
        }

    } else {
        SetLastError(ERROR_INVALID_PARAMETER);
    }

    return bRet;
}

BOOL
EfsDisabled(
    IN LPCWSTR SectionName,
	IN LPCWSTR KeyName,
	IN LPCWSTR IniFileName
	)
/*++

Routine Description:

    This routine checks if the encryption has been turned off for the ini file.
        
Arguments:

    SectionName - Section name (Encryption).

    KeyName - Key name (Disable).
    
    IniFileName - The path for ini file (dir\desktop.ini).

Return Value:

    TRUE for disabled

--*/
{
    DWORD ValueLength;
    WCHAR ResultString[4];

    memset( ResultString, 0, sizeof(ResultString) );

    ValueLength = GetPrivateProfileString(
                      SectionName,
                      KeyName,
                      L"0",
                      ResultString,
                      sizeof(ResultString)/sizeof(WCHAR),
                      IniFileName
                      );

    //
    // If GetPrivateProfileString failed, EFS will be enabled
    //

    return (!wcscmp(L"1", ResultString));
}

BOOL
DirEfsDisabled(
    IN LPCWSTR  DirName
    )
/*++

Routine Description:

    This routine checks if the encryption has been turned off for the dir.
        
Arguments:

    SectionName - Section name (Encryption).

    KeyName - Key name (Disable).
    
    IniFileName - The path for ini file (dir\desktop.ini).

Return Value:

    TRUE for disabled

--*/
{
    LPWSTR FileName;
    DWORD  FileLength = (wcslen(INIFILENAME)+wcslen(DirName)+1)*sizeof (WCHAR);
    BOOL   bRet = FALSE;

    FileName = (PWSTR)RtlAllocateHeap( RtlProcessHeap(), 0, FileLength );
    if (FileName) {
        wcscpy( FileName, DirName );
        wcscat( FileName, INIFILENAME );
        bRet = EfsDisabled( INISECTIONNAME, INIKEYNAME, FileName );
        RtlFreeHeap( RtlProcessHeap(), 0, FileName );
    }

    return bRet;
}

BOOL
RemoteFile(
    IN LPCWSTR FileName
    )
/*++

Routine Description:

    This routine checks if the file is a local file.
    If a UNC name is passed in, it assumes a remote file. A loopback operation will occur.

Arguments:

    FileName - Supplies the user-supplied file name.

Return Value:

    TRUE for remote file.

--*/

{

    if ( FileName[1] == L':' ){

        WCHAR DriveLetter[3];
        DWORD BufferLength = 3;
        DWORD RetCode = ERROR_SUCCESS;

        DriveLetter[0] = FileName[0];
        DriveLetter[1] = FileName[1];
        DriveLetter[2] = 0;

        RetCode = WNetGetConnectionW(
                                DriveLetter,
                                DriveLetter,
                                &BufferLength
                                );

        if (RetCode == ERROR_NOT_CONNECTED) {
            return FALSE;
        } else {
            return TRUE;
        }

    } else {
        return TRUE;
    }

}

DWORD
EfsClientEncryptFile(
    IN LPCWSTR      FileName
    )
{
    DWORD           rc;
    BOOL            Result;


    UNICODE_STRING FullFileNameU;

    if (NULL == FileName) {
       return ERROR_INVALID_PARAMETER;
    }
    Result = TranslateFileName( FileName, &FullFileNameU );

    if (Result) {

        //
        // Call the server
        //

        rc = EfsEncryptFileRPCClient( &FullFileNameU );
        RtlFreeHeap(RtlProcessHeap(), 0, FullFileNameU.Buffer);

    } else {
        rc = GetLastError();
    }

    return( rc );
}

DWORD
EfsClientDecryptFile(
    IN LPCWSTR      FileName,
    IN DWORD        dwRecovery
    )
{
    DWORD           rc;
    BOOL            Result;

    UNICODE_STRING FullFileNameU;

    if (NULL == FileName) {
       return ERROR_INVALID_PARAMETER;
    }
    Result = TranslateFileName( FileName, &FullFileNameU );

    if (Result) {

        //
        // Call the server
        //

        rc = EfsDecryptFileRPCClient( &FullFileNameU, dwRecovery );
        RtlFreeHeap(RtlProcessHeap(), 0, FullFileNameU.Buffer);

    } else {
        rc = GetLastError();
    }


    return( rc );
}

BOOL
EfsClientFileEncryptionStatus(
    IN LPCWSTR      FileName,
    OUT LPDWORD      lpStatus
    )
/*++

Routine Description:

    This routine checks if a file is encryptable or not.

    We do not test the NTFS Volume 5 for the reason of performance.
    This means we might return a file encryptable (on FAT at etc.), but
    actually it could not be encrypted. This should be OK. This is a best effort
    API. We have the same problem with network file. Any way, a file could fail
    to be encrypted for many reasons, delegation, disk space and etc.
    We disable the encryption from %windir% down.
    We might change these features later.

Arguments:

    FileName - The file to be checked.

    lpStatus - The encryption status of the file. Error code if the return value is
                    FALSE.

Return Value:

    TRUE on success, FALSE otherwise.

--*/

{
    BOOL            Result;
    DWORD        FileAttributes;

    UNICODE_STRING FullFileNameU;

    if ((NULL == FileName) || ( NULL == lpStatus)) {
       SetLastError(ERROR_INVALID_PARAMETER);
       return FALSE;
    }

    //
    // GetFileAttributes should use the name before TanslateFileName
    // in case the passed in name is longer than MAX_PATH and using the
    // format \\?\
    //

    FileAttributes = GetFileAttributes( FileName );

    if (FileAttributes == -1){
        *lpStatus = GetLastError();
        return FALSE;
    }

    Result = TranslateFileName( FileName, &FullFileNameU );

    //
    // FullFileNameU.Length does not include the ending 0. The data returned from TranslateFileName does have the ending 0.
    //

    ASSERT(FullFileNameU.Buffer[FullFileNameU.Length / 2] == 0);

    if (Result) {

        if ( (FileAttributes & FILE_ATTRIBUTE_ENCRYPTED) ||
             (FileAttributes & FILE_ATTRIBUTE_SYSTEM) ) {

            //
            // File not encryptable. Either it is encypted or a system file.
            //

            if ( FileAttributes & FILE_ATTRIBUTE_ENCRYPTED ){

                *lpStatus = FILE_IS_ENCRYPTED;

            } else {

                *lpStatus = FILE_SYSTEM_ATTR ;

            }

        } else {

            LPWSTR  TmpBuffer;
            LPWSTR  FullPathName;
            UINT    TmpBuffLen;
            UINT    FullPathLen;
            UINT    PathLength;
            BOOL    GotRoot;
            BOOL    EfsDisabled = FALSE;

            //
            // Check if it is the root.
            //

            if ( FullFileNameU.Length >= MAX_PATH * sizeof(WCHAR)){

                //
                // We need to put back the \\?\ or \\?\UNC\ to use the
                // Win 32 API. The extra max bytes needed is 7. ( \\?\UNC + NULL - \) 
                //

                FullPathLen = FullFileNameU.Length + 7 * sizeof(WCHAR);
                TmpBuffLen = FullPathLen;
                FullPathName = (LPWSTR)RtlAllocateHeap(
                                            RtlProcessHeap(),
                                            0,
                                            FullPathLen
                                            );
                TmpBuffer = (LPWSTR)RtlAllocateHeap(
                                            RtlProcessHeap(),
                                            0,
                                            TmpBuffLen
                                            );

                if ((FullPathName == NULL) || (TmpBuffer == NULL)){
                    RtlFreeHeap(RtlProcessHeap(), 0, FullFileNameU.Buffer);
                    if (FullPathName){
                        RtlFreeHeap(RtlProcessHeap(), 0, FullPathName);
                    }
                    if (TmpBuffer){
                        RtlFreeHeap(RtlProcessHeap(), 0, TmpBuffer);
                    }
                    *lpStatus = ERROR_OUTOFMEMORY;
                    return FALSE;
                }

                if ( FullFileNameU.Buffer[0] == L'\\' ){

                    //
                    // Put back the \\?\UNC\
                    //

                    wcscpy(FullPathName, L"\\\\?\\UNC");
                    wcscat(FullPathName, &(FullFileNameU.Buffer[1]));
                    FullPathLen = FullFileNameU.Length + 6 * sizeof(WCHAR);

                } else {

                    //
                    // Put back the \\?\
                    //

                    wcscpy(FullPathName, L"\\\\?\\");
                    wcscat(FullPathName, FullFileNameU.Buffer);
                    FullPathLen = FullFileNameU.Length + 4 * sizeof(WCHAR);
                }

            } else {
                TmpBuffLen = MAX_PATH * sizeof(WCHAR);
                TmpBuffer = (LPWSTR)RtlAllocateHeap(
                                            RtlProcessHeap(),
                                            0,
                                            TmpBuffLen
                                            );
                if (TmpBuffer == NULL){
                    RtlFreeHeap(RtlProcessHeap(), 0, FullFileNameU.Buffer);
                    *lpStatus = ERROR_OUTOFMEMORY;
                    return FALSE;
                }

                FullPathName = FullFileNameU.Buffer;
                FullPathLen = FullFileNameU.Length;
            }

            //
            // Check desktop.ini here
            //


            wcscpy(TmpBuffer, FullFileNameU.Buffer); 
            if (!(FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {

                //
                // This is a file. Get the DIR path
                //

                int ii;

                ii = wcslen(TmpBuffer) - 1;
                while ((ii >= 0) && (TmpBuffer[ii] != L'\\')) {
                    ii--;
                }
                if (ii>=0) {
                    TmpBuffer[ii] = 0;
                }

            }

            EfsDisabled = DirEfsDisabled( TmpBuffer );

            if (EfsDisabled) {
               *lpStatus = FILE_DIR_DISALLOWED;
            } else if (!(FileAttributes & FILE_ATTRIBUTE_DIRECTORY) && (FileAttributes & FILE_ATTRIBUTE_READONLY)){

                //
                // Read only file
                //

                *lpStatus = FILE_READ_ONLY;
            } else {
                GotRoot = GetVolumePathName(
                                    FullPathName,
                                    TmpBuffer,
                                    TmpBuffLen
                                    );
    
                if ( GotRoot ){
    
                    DWORD RootLength = wcslen(TmpBuffer) - 1;
                    TmpBuffer[RootLength] = NULL;
                    if ( (FullPathLen == RootLength * sizeof (WCHAR))
                           && !wcscmp(TmpBuffer, FullPathName)){
    
                        //
                        // It is the root
                        //
    
                        *lpStatus = FILE_ROOT_DIR;
    
                    } else {
    
                        //
                        // Check if it is the Windows\system32 directories
                        //
    
                        PathLength = GetSystemWindowsDirectory( TmpBuffer, TmpBuffLen );                    
                        //PathLength = GetWindowsDirectory( TmpBuffer, TmpBuffLen );
                        //PathLength = GetSystemDirectory( TmpBuffer, TmpBuffLen );
    
                        ASSERT(PathLength <= TmpBuffLen);
    
                        if ( PathLength > TmpBuffLen ) {
    
                            //
                            // This is unlikely. Not sure who will ever have the length
                            // of %windir%\system32 longer than MAXPATH in the real world. 
                            // Even this happen, user could still encrypt the file. FILE_UNKNOWN
                            // does not mean file could\or couldn't be encrypted.
                            //
    
                            *lpStatus = FILE_UNKNOWN ;
    
                        } else {
    
                            if ( ( FullFileNameU.Length < PathLength * sizeof (WCHAR) ) ||
                                  ( ( FullFileNameU.Buffer[PathLength] ) &&
                                    ( FullFileNameU.Buffer[PathLength] != L'\\') )){
    
                                //
                                // Check if a remote file
                                //
    
                                if ( RemoteFile( FullFileNameU.Buffer ) ){
    
                                    *lpStatus = FILE_UNKNOWN;
    
                                } else {
    
                                    *lpStatus = FILE_ENCRYPTABLE;
    
                                }
    
                            } else {
    
                                if ( _wcsnicmp(TmpBuffer, FullFileNameU.Buffer, PathLength)){
    
                                    //
                                    // Not under %SystemRoot%
                                    //
    
                                    if ( RemoteFile( FullFileNameU.Buffer ) ){
    
                                        *lpStatus = FILE_UNKNOWN;
    
                                    } else {
    
                                        *lpStatus = FILE_ENCRYPTABLE;
    
                                    }
                                } else {
    
                                    //
                                    // In windows root directory. WINNT
                                    //
    
                                    BOOL bRet;
                                    DWORD allowPathLen;
    
                                    //
                                    // Check for allow lists
                                    //
    
                                    allowPathLen = (DWORD) TmpBuffLen;
                                    bRet = GetProfilesDirectory(TmpBuffer, &allowPathLen);
                                    if (!bRet){
                                        RtlFreeHeap(RtlProcessHeap(), 0, TmpBuffer);
                                        TmpBuffer = (LPWSTR)RtlAllocateHeap(
                                                                RtlProcessHeap(),
                                                                0,
                                                                allowPathLen
                                                                );
                                        if (TmpBuffer){
                                            bRet = GetProfilesDirectory(TmpBuffer, &allowPathLen);
                                        } else {
                                            *lpStatus = ERROR_OUTOFMEMORY;
                                            Result = FALSE;
                                        }
                                    }
                                    if (bRet){
    
                                        //
                                        // Check for Profiles directory. allowPathLen including NULL.
                                        //
    
                                        if ((FullFileNameU.Length >= ((allowPathLen-1) * sizeof (WCHAR)) ) && 
                                             !_wcsnicmp(TmpBuffer, FullFileNameU.Buffer, allowPathLen - 1)){
                                            *lpStatus = FILE_ENCRYPTABLE;
                                        } else {
    
                                            //
                                            // Under %windir% but not profiles
                                            //
    
                                            *lpStatus = FILE_SYSTEM_DIR;
                                        }
                                    } else {
    
                                        if ( *lpStatus != ERROR_OUTOFMEMORY){
    
                                            //
                                            // This should not happen, unless a bug in GetProfilesDirectoryEx()
                                            //
                                            ASSERT(FALSE);
    
                                            *lpStatus = FILE_UNKNOWN;
                                        }
    
                                    }
                                }
                            }
                        }
                    }
                } else {
    
                    //
                    // Cannot get the root. The reason might very well be out of memory.
                    // Return FILE_UNKNOWN and let other codes dealing with the memory
                    // problem.
                    //
    
                    *lpStatus = FILE_UNKNOWN ;
    
                }
            }

            if ((FullPathName != FullFileNameU.Buffer) && FullPathName){
                RtlFreeHeap(RtlProcessHeap(), 0, FullPathName);
            }

            if (TmpBuffer){
                RtlFreeHeap(RtlProcessHeap(), 0, TmpBuffer);
            }

        }

        RtlFreeHeap(RtlProcessHeap(), 0, FullFileNameU.Buffer);

    } else {
        *lpStatus = GetLastError();
    }

    return  Result;
}

DWORD
EfsClientOpenFileRaw(
    IN      LPCWSTR         FileName,
    IN      ULONG           Flags,
    OUT     PVOID *         Context
    )

/*++

Routine Description:

    This routine is used to open an encrypted file. It opens the file and
    prepares the necessary context to be used in ReadRaw data and WriteRaw
    data.


Arguments:

    FileName  --  File name of the file to be exported

    Flags -- Indicating if open for export or import; for directory or file.

    Context - Export context to be used by READ operation later. Caller should
              pass this back in ReadRaw().


Return Value:

    Result of the operation.

--*/

{
    DWORD        rc;
    BOOL            Result;
    UNICODE_STRING FullFileNameU;

    if ((NULL == FileName) || ( NULL == Context)) {
       return ERROR_INVALID_PARAMETER;
    }

    Result = TranslateFileName( FileName, &FullFileNameU );

    if (Result) {

        rc =  (EfsOpenFileRawRPCClient(
                        FullFileNameU.Buffer,
                        Flags,
                        Context
                        )
                    );

        RtlFreeHeap(RtlProcessHeap(), 0, FullFileNameU.Buffer);

    } else {
        rc = GetLastError();
    }

    return rc;

}

DWORD
EfsClientReadFileRaw(
    IN      PFE_EXPORT_FUNC ExportCallback,
    IN      PVOID           CallbackContext,
    IN      PVOID           Context
    )
/*++

Routine Description:

    This routine is used to read encrypted file's raw data. It uses
    NTFS FSCTL to get the data.

Arguments:

    ExportCallback --  Caller supplied callback function to process the
                       raw data.

    CallbackContext -- Caller's context passed back in ExportCallback.

    Context - Export context created in the CreateRaw.

Return Value:

    Result of the operation.

--*/

{
    return ( EfsReadFileRawRPCClient(
                    ExportCallback,
                    CallbackContext,
                    Context
                    ));
}

DWORD
EfsClientWriteFileRaw(
    IN      PFE_IMPORT_FUNC ImportCallback,
    IN      PVOID           CallbackContext,
    IN      PVOID           Context
    )

/*++

Routine Description:

    This routine is used to write encrypted file's raw data. It uses
    NTFS FSCTL to write the data.

Arguments:

    ImportCallback --  Caller supplied callback function to provide the
                       raw data.

    CallbackContext -- Caller's context passed back in ImportCallback.

    Context - Import context created in the CreateRaw.

Return Value:

    Result of the operation.

--*/

{

    return ( EfsWriteFileRawRPCClient(
                    ImportCallback,
                    CallbackContext,
                    Context
                    ));
}

VOID
EfsClientCloseFileRaw(
    IN      PVOID           Context
    )
/*++

Routine Description:

    This routine frees the resources allocated by the CreateRaw

Arguments:

    Context - Created by the CreateRaw.

Return Value:

    NO.

--*/
{
    if ( !Context ){
        return;
    }

    EfsCloseFileRawRPCClient( Context );
}


//
// Beta 2 API
//

DWORD
EfsClientAddUsers(
    IN LPCWSTR lpFileName,
    IN PENCRYPTION_CERTIFICATE_LIST pEncryptionCertificates
    )
/*++

Routine Description:

    Calls client stub for AddUsersToFile EFS API.

Arguments:

    lpFileName - Supplies the name of the file to be modified.

    nUsers - Supplies the number of entries in teh pEncryptionCertificates array

    pEncryptionCertificates - Supplies an array of pointers to PENCRYPTION_CERTIFICATE
        structures.  Length of array is given in nUsers parameter.

Return Value:

--*/
{
    DWORD        rc = ERROR_SUCCESS;
    UNICODE_STRING FullFileNameU;
    DWORD        ii = 0;
    CERT_CHAIN_PARA CertChainPara;


    if ((NULL == lpFileName) || (NULL == pEncryptionCertificates)) {
       return ERROR_INVALID_PARAMETER;
    }

    //
    // Let's check to see if the certs are good or not.
    //

    CertChainPara.cbSize = sizeof(CERT_CHAIN_PARA);
    CertChainPara.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
    CertChainPara.RequestedUsage.Usage.cUsageIdentifier = 1;
    CertChainPara.RequestedUsage.Usage.rgpszUsageIdentifier=&EfsOidlpstr;

    while ((ERROR_SUCCESS == rc) && (ii < pEncryptionCertificates->nUsers)) {

        PCCERT_CONTEXT pCertContext = CertCreateCertificateContext(
                                          X509_ASN_ENCODING,
                                          pEncryptionCertificates->pUsers[ii]->pCertBlob->pbData,
                                          pEncryptionCertificates->pUsers[ii]->pCertBlob->cbData
                                          );
        if (pCertContext != NULL) {

            PCCERT_CHAIN_CONTEXT pChainContext;

            //
            // Do the chain validation
            //
            
            if (CertGetCertificateChain (
                                        HCCE_CURRENT_USER,
                                        pCertContext,
                                        NULL,
                                        NULL,
                                        &CertChainPara,
                                        0,
                                        NULL,
                                        &pChainContext
                                        )) {
                //
                // Let's check the chain
                //

                PCERT_SIMPLE_CHAIN pChain = pChainContext->rgpChain[ 0 ];
                PCERT_CHAIN_ELEMENT pElement = pChain->rgpElement[ 0 ];
                BOOL bSelfSigned = pElement->TrustStatus.dwInfoStatus & CERT_TRUST_IS_SELF_SIGNED;
                DWORD dwErrorStatus = pChainContext->TrustStatus.dwErrorStatus;

                if (dwErrorStatus) {
                    if ((dwErrorStatus == CERT_TRUST_IS_UNTRUSTED_ROOT) && bSelfSigned ){

                        //
                        // Self signed. Check if it is in the my trusted store
                        //
                        HCERTSTORE trustedStore;
                        PCCERT_CONTEXT pCert=NULL;

                        trustedStore = CertOpenStore(
                                                CERT_STORE_PROV_SYSTEM_W,
                                                0,       // dwEncodingType
                                                0,       // hCryptProv,
                                                CERT_SYSTEM_STORE_CURRENT_USER,
                                                TRUSTEDPEOPLE
                                                );

                        if (trustedStore) {

                            pCert = CertFindCertificateInStore(
                                        trustedStore,
                                        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                        0,
                                        CERT_FIND_EXISTING,
                                        pCertContext,
                                        pCert
                                        );
                            if (pCert) {
                    
                                //
                                // We found it.
                                //
                                CertFreeCertificateContext(pCert);

                            } else {

                                //
                                // Not trusted self-signed cert
                                //

                                rc = CERT_E_UNTRUSTEDROOT;

                            }

                            CertCloseStore( trustedStore, 0 );

                        } else {
                            rc = GetLastError();
                        }

                    } else {

                        //
                        // Other chain build error
                        //  Let's get the error code of the chain building.
                        //

                        CERT_CHAIN_POLICY_PARA PolicyPara;
                        CERT_CHAIN_POLICY_STATUS PolicyStatus;

                        RtlZeroMemory(&PolicyPara, sizeof(CERT_CHAIN_POLICY_PARA));
                        RtlZeroMemory(&PolicyStatus, sizeof(CERT_CHAIN_POLICY_STATUS));

                        PolicyPara.cbSize = sizeof(CERT_CHAIN_POLICY_PARA);
                        PolicyStatus.cbSize = sizeof(CERT_CHAIN_POLICY_STATUS);

                        if (!CertVerifyCertificateChainPolicy(
                                CERT_CHAIN_POLICY_BASE,
                                pChainContext,
                                &PolicyPara,
                                &PolicyStatus
                                )) {

                            rc = PolicyStatus.dwError;
                        }

                    }
                }

                CertFreeCertificateChain( pChainContext );

            } else {

                rc = GetLastError();

            }

            CertFreeCertificateContext(pCertContext);

        } else {

            rc = GetLastError();

        }

        ii++;
    }

    if (ERROR_SUCCESS == rc) {

        if (TranslateFileName( lpFileName, &FullFileNameU )) {
    
            rc = EfsAddUsersRPCClient( FullFileNameU.Buffer, pEncryptionCertificates );
    
            RtlFreeHeap(RtlProcessHeap(), 0, FullFileNameU.Buffer);
    
        } else {
    
            rc = GetLastError();
        }

    }

    return rc;
}

DWORD
EfsClientRemoveUsers(
    IN LPCWSTR lpFileName,
    IN PENCRYPTION_CERTIFICATE_HASH_LIST pHashes
    )
/*++

Routine Description:

    Calls client stub for RemoveUsersFromFile EFS API

Arguments:

    lpFileName - Supplies the name of the file to be modified.

    pHashes - Supplies a structure containing a list of PENCRYPTION_CERTIFICATE_HASH
        structures, each of which represents a user to remove from the specified file.

Return Value:

--*/
{
    DWORD        rc;
    UNICODE_STRING FullFileNameU;


    if ((NULL == lpFileName) || (NULL == pHashes) || (pHashes->pUsers == NULL)) {
       return ERROR_INVALID_PARAMETER;
    }
    if (TranslateFileName( lpFileName, &FullFileNameU )) {

        rc =  EfsRemoveUsersRPCClient( FullFileNameU.Buffer, pHashes );

        RtlFreeHeap(RtlProcessHeap(), 0, FullFileNameU.Buffer);

    } else {

        rc = GetLastError();
    }

    return rc;
}

DWORD
EfsClientQueryRecoveryAgents(
    IN      LPCWSTR                             lpFileName,
    OUT     PENCRYPTION_CERTIFICATE_HASH_LIST * pRecoveryAgents
    )
/*++

Routine Description:

    Calls client stub for QueryRecoveryAgents EFS API

Arguments:

    lpFileName - Supplies the name of the file to be modified.

    pRecoveryAgents - Returns a pointer to a structure containing a list
        of PENCRYPTION_CERTIFICATE_HASH structures, each of which represents
        a recovery agent on the file.

Return Value:

--*/
{
    DWORD        rc;
    UNICODE_STRING FullFileNameU;


    if ((NULL == lpFileName) || (NULL == pRecoveryAgents)) {
       return ERROR_INVALID_PARAMETER;
    }
    if (TranslateFileName( lpFileName, &FullFileNameU )) {

        rc =  EfsQueryRecoveryAgentsRPCClient( FullFileNameU.Buffer, pRecoveryAgents );

        RtlFreeHeap(RtlProcessHeap(), 0, FullFileNameU.Buffer);

    } else {

        rc = GetLastError();
    }

    return rc;
}

DWORD
EfsClientQueryUsers(
    IN      LPCWSTR                             lpFileName,
    OUT     PENCRYPTION_CERTIFICATE_HASH_LIST * pUsers
    )
/*++

Routine Description:

    Calls client stub for QueryUsersOnFile EFS API

Arguments:

    lpFileName - Supplies the name of the file to be modified.

    pUsers - Returns a pointer to a structure containing a list
        of PENCRYPTION_CERTIFICATE_HASH structures, each of which represents
        a user of this file (that is, someone who can decrypt the file).

Return Value:

--*/
{
    DWORD        rc;
    UNICODE_STRING FullFileNameU;

    if ((NULL == lpFileName) || (NULL == pUsers)) {
       return ERROR_INVALID_PARAMETER;
    }
    if (TranslateFileName( lpFileName, &FullFileNameU )) {

        rc =  EfsQueryUsersRPCClient( FullFileNameU.Buffer, pUsers );

        RtlFreeHeap(RtlProcessHeap(), 0, FullFileNameU.Buffer);

    } else {

        rc = GetLastError();
    }

    return rc;
}


DWORD
EfsClientSetEncryptionKey(
    IN PENCRYPTION_CERTIFICATE pEncryptionCertificate
    )
/*++

Routine Description:

    Calls client stub for SetFileEncryptionKey EFS API

Arguments:

    pEncryptionCertificate - Supplies a pointer to an EFS certificate
        representing the public key to use for future encryption operations.

Return Value:

--*/
{
    /*
    if ((NULL == pEncryptionCertificate) || ( NULL == pEncryptionCertificate->pCertBlob)) {
       return ERROR_INVALID_PARAMETER;
    }
    */

    if ( pEncryptionCertificate && ( NULL == pEncryptionCertificate->pCertBlob)) {
       return ERROR_INVALID_PARAMETER;
    }

    DWORD rc =  EfsSetEncryptionKeyRPCClient( pEncryptionCertificate );

    return( rc );
}

VOID
EfsClientFreeHashList(
    IN PENCRYPTION_CERTIFICATE_HASH_LIST pHashList
    )
/*++

Routine Description:

    This routine frees the memory allocated by a call to
    QueryUsersOnEncryptedFile and QueryRecoveryAgentsOnEncryptedFile

Arguments:

    pHashList - Supplies the hash list to be freed.

Return Value:

    None.  Faults in user's context if passed bogus data.

--*/

{
    if (NULL == pHashList) {
       SetLastError(ERROR_INVALID_PARAMETER);
       return;
    }

    for (DWORD i=0; i<pHashList->nCert_Hash ; i++) {

         PENCRYPTION_CERTIFICATE_HASH pHash = pHashList->pUsers[i];

         if (pHash->lpDisplayInformation) {
             MIDL_user_free( pHash->lpDisplayInformation );
         }

         if (pHash->pUserSid) {
             MIDL_user_free( pHash->pUserSid );
         }

         MIDL_user_free( pHash->pHash->pbData );
         MIDL_user_free( pHash->pHash );
         MIDL_user_free( pHash );
    }

    MIDL_user_free( pHashList->pUsers );
    MIDL_user_free( pHashList );

    return;
}

DWORD
EfsGetMySDRpcBlob(
    IN PSECURITY_DESCRIPTOR pInSD,
    OUT PEFS_RPC_BLOB *pOutSDRpcBlob
    )
{
    DWORD rc = ERROR_SUCCESS;
    PSECURITY_DESCRIPTOR pRelativeSD;
    ULONG SDLength = 0;

    if ( (pInSD == NULL) || !RtlValidSecurityDescriptor(pInSD) ) {
        return(ERROR_INVALID_PARAMETER);
    }

    if ( ((PISECURITY_DESCRIPTOR)pInSD)->Control & SE_SELF_RELATIVE) {

        //
        // The input SD is already RELATIVE
        // Just fill EFS_RPC_BLOB
        //


        *pOutSDRpcBlob = (PEFS_RPC_BLOB) RtlAllocateHeap(
                                             RtlProcessHeap(),
                                             0,
                                             sizeof(EFS_RPC_BLOB) 
                                             );
        if (*pOutSDRpcBlob) {

            (*pOutSDRpcBlob)->cbData = RtlLengthSecurityDescriptor (
                                            pInSD
                                            );
            (*pOutSDRpcBlob)->pbData = (PBYTE) pInSD;

        } else {

            return(ERROR_NOT_ENOUGH_MEMORY);

        }


    } else {

        //
        // get the length
        //
        RtlMakeSelfRelativeSD( pInSD,
                               NULL,
                               &SDLength
                             );
    
        if ( SDLength > 0 ) {
    
            *pOutSDRpcBlob = (PEFS_RPC_BLOB) RtlAllocateHeap(
                                                 RtlProcessHeap(),
                                                 0,
                                                 SDLength + sizeof(EFS_RPC_BLOB) 
                                                 );

    
            if ( !(*pOutSDRpcBlob) ) {
                return(ERROR_NOT_ENOUGH_MEMORY);
            }

            pRelativeSD = (PSECURITY_DESCRIPTOR)(*pOutSDRpcBlob + 1);
            (*pOutSDRpcBlob)->cbData = SDLength;
            (*pOutSDRpcBlob)->pbData = (PBYTE) pRelativeSD;

    
            rc = RtlNtStatusToDosError(
                     RtlMakeSelfRelativeSD( pInSD,
                                            pRelativeSD,
                                            &SDLength
                                          ));
            if ( rc != ERROR_SUCCESS ) {
    
                RtlFreeHeap(RtlProcessHeap(), 0, *pOutSDRpcBlob);
                *pOutSDRpcBlob = NULL;
                return(rc);

            }
    
        } else {
    
            //
            // something is wrong with the SD
            //
            return(ERROR_INVALID_PARAMETER);
        }
    }

    return(rc);

}

DWORD
EfsClientDuplicateEncryptionInfo(
    IN LPCWSTR lpSrcFile,
    IN LPCWSTR lpDestFile,
    IN DWORD dwCreationDistribution, 
    IN DWORD dwAttributes, 
    IN LPSECURITY_ATTRIBUTES lpSecurityAttributes
    )
{
    DWORD rc = ERROR_SUCCESS;

    UNICODE_STRING SrcFullFileNameU;
    UNICODE_STRING DestFullFileNameU;

    if (TranslateFileName( lpSrcFile, &SrcFullFileNameU )) {

        if (TranslateFileName( lpDestFile, &DestFullFileNameU )) {

            PEFS_RPC_BLOB pRpcBlob = NULL;
            BOOL bInheritHandle = FALSE;

            if (lpSecurityAttributes) {
                rc = EfsGetMySDRpcBlob(lpSecurityAttributes->lpSecurityDescriptor, &pRpcBlob);
                bInheritHandle = lpSecurityAttributes->bInheritHandle;
            }
            
            if (ERROR_SUCCESS == rc) {

                rc = EfsDuplicateEncryptionInfoRPCClient(
                        SrcFullFileNameU.Buffer,
                        DestFullFileNameU.Buffer,
                        dwCreationDistribution,
                        dwAttributes,
                        pRpcBlob,
                        bInheritHandle
                        );

                if (pRpcBlob) {

                    RtlFreeHeap(RtlProcessHeap(), 0, pRpcBlob);

                }
    
            }

            RtlFreeHeap(RtlProcessHeap(), 0, DestFullFileNameU.Buffer);

        } else {

            rc = GetLastError();
        }

        RtlFreeHeap(RtlProcessHeap(), 0, SrcFullFileNameU.Buffer);

    } else {

        rc = GetLastError();
    }

    return( rc );

}


DWORD
EfsClientFileKeyInfo(
    IN      LPCWSTR        lpFileName,
    IN      DWORD          InfoClass,
    OUT     PEFS_RPC_BLOB  *KeyInfo
    )
/*++

Routine Description:

    Calls client stub for EncryptedFileKeyInfo EFS API

Arguments:

    lpFileName - Supplies the name of the file.

    KeyInfo - Returns a pointer to a structure containing key info.
    
Return Value:

--*/
{
    DWORD        rc;
    UNICODE_STRING FullFileNameU;
    DWORD          FileAttributes;

    if ((NULL == lpFileName) || (NULL == KeyInfo)) {
       return ERROR_INVALID_PARAMETER;
    }

    if ((InfoClass != BASIC_KEY_INFO) && (InfoClass != UPDATE_KEY_USED)) {
        return ERROR_INVALID_PARAMETER;
    }
    
    if ( InfoClass == UPDATE_KEY_USED) {

        FileAttributes = GetFileAttributes( lpFileName );
        if ( (FileAttributes == -1) || !(FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ) {

            return ERROR_INVALID_PARAMETER;

        }
    }

    if (TranslateFileName( lpFileName, &FullFileNameU )) {

        rc =  EfsFileKeyInfoRPCClient( FullFileNameU.Buffer, InfoClass, KeyInfo );

        RtlFreeHeap(RtlProcessHeap(), 0, FullFileNameU.Buffer);

    } else {

        rc = GetLastError();
    }

    return rc;
}


VOID
EfsClientFreeKeyInfo(
    IN PEFS_RPC_BLOB  pKeyInfo
    )
/*++

Routine Description:

    This routine frees the memory allocated by a call to
    EfsClientFileKeyInfo

Arguments:

    pKeyInfo - Supplies the memory pointer to be freed.

Return Value:

    None.  Faults in user's context if passed bogus data.

--*/

{
    if (NULL == pKeyInfo) {
       SetLastError(ERROR_INVALID_PARAMETER);
       return;
    }

    if (pKeyInfo->pbData) {
        MIDL_user_free( pKeyInfo->pbData );
    }
    
    MIDL_user_free( pKeyInfo );

    return;
}
