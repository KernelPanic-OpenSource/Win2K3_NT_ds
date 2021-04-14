//--------------------------------------------------------------------
// Logging - implementation
// Copyright (C) Microsoft Corporation, 2000
//
// Created by: Louis Thomas (louisth), 02-01-00
//
// routines to do logging to the event log and to a file
//

#include "pch.h" // precompiled headers

#include "EndianSwap.inl"

//--------------------------------------------------------------------
// structures


struct FileLogBuffer;
struct FileLogThreadInfo; 

typedef AutoPtr<FileLogBuffer>             FLBPtr; 
typedef MyThrowingAllocator<FLBPtr>        FLBPtrAllocator; 
typedef vector<FLBPtr, FLBPtrAllocator>    FLBPtrVec; 
typedef FLBPtrVec::iterator                FLBPtrIter; 

typedef AutoPtr<FileLogThreadInfo>         FLTIPtr; 
typedef MyThrowingAllocator<FLTIPtr>       FLTIPtrAllocator;
typedef vector<FLTIPtr, FLTIPtrAllocator>  FLTIPtrVec;
typedef FLTIPtrVec::iterator               FLTIPtrIter; 

struct LogEntryRange {
    DWORD            dwStart;
    DWORD            dwLength;
    LogEntryRange   *plerNext;
};

struct FileLogConfig {
    DWORD           dwFlags; 
    DWORD           dwFileSize; 
    LogEntryRange  *plerAllowedEntries;
    WCHAR          *wszLogFileName;
};

// Fields may only be modified by the owning thread.
// No additional synchronization is needed. 
struct FileLogBuffer { 
    DWORD           ccAllocated; 
    WCHAR          *wszText; 
    OVERLAPPED      overlapped; 

    FileLogBuffer() { 
	wszText = NULL; 
	ZeroMemory(&overlapped, sizeof(overlapped)); 
    }
    ~FileLogBuffer(); 
};

// Fields may only be modified by the owning thread.
// No additional synchronization is needed. 
struct FileLogThreadInfo { 
    bool                      bFlush;             //
    DWORD                     dwMemoryIncrement;  // 
    DWORD                     dwThreadId;         // 
    FLBPtr                    pflbPending;        //
    FLBPtrVec                 vflbWritten;        //

    ~FileLogThreadInfo(); 
}; 

struct FileLogState {
    DWORD               dwFlags;               // protected by csState
    unsigned __int64    qwFileSize;            // protected by csState
    LogEntryRange      *plerAllowedEntries;    // protected by csState
    WCHAR              *wszLogFileName;        // protected by csState
    HANDLE              hLogFile;              // protected by csState
    unsigned __int64    qwFilePointer;         // protected by csFilePointer

    // Per-thread file log buffers: 
    FLTIPtrVec                   *pvFileLogThreadInfo;   // protected by csFileLogThreadInfo
    RTL_RESOURCE                  csState;
    CRITICAL_SECTION              csFilePointer; 
    RTL_RESOURCE                  csFileLogThreadInfo;   
    bool                          bIsCsStateInitialized; 
    bool                          bIsCsFilePointerInitialized; 
    bool                          bIsCsFileLogThreadInfoInitialized;   
    bool                          bIsAllStateInitialized; 
};

class SourceChangeLogEntry { 
public:
    ~SourceChangeLogEntry() { 
        if (NULL != m_pwszName) { LocalFree(m_pwszName); } 
    }

    static HRESULT New(IN LPWSTR pwszName, OUT SourceChangeLogEntry ** ppscle) { 
        HRESULT                hr;
        LPWSTR                 pwsz  = NULL; 
        SourceChangeLogEntry  *pscle = NULL;

        pwsz = (LPWSTR)LocalAlloc(LPTR, (wcslen(pwszName) + 1) * sizeof(WCHAR)); 
        _JumpIfOutOfMemory(hr, error, pwsz); 
        wcscpy(pwsz, pwszName); 

        pscle = new SourceChangeLogEntry(pwsz); 
        _JumpIfOutOfMemory(hr, error, pscle); 
        
        *ppscle = pscle; 
        pscle = NULL; 
        pwsz = NULL; 
        hr = S_OK; 
    error:
        if (NULL != pwsz) { LocalFree(pwsz); } 
        if (NULL != pscle) { delete (pscle); } 
        return hr; 
    }

    BOOL operator==(const SourceChangeLogEntry & scle) { 
        if (NULL == m_pwszName) {
            return NULL == scle.m_pwszName; 
        } else { 
            return 0 == wcscmp(m_pwszName, scle.m_pwszName); 
        }
    }
private:
    SourceChangeLogEntry(LPWSTR pwszName) : m_pwszName(pwszName) { } 

    SourceChangeLogEntry();
    SourceChangeLogEntry(const SourceChangeLogEntry &); 
    SourceChangeLogEntry & operator=(const SourceChangeLogEntry &); 
    LPWSTR m_pwszName; 
};

typedef AutoPtr<SourceChangeLogEntry>   SCPtr; 
typedef MyThrowingAllocator<SCPtr>      SCPtrAllocator; 
typedef vector<SCPtr, SCPtrAllocator>   SCPtrVec; 
typedef SCPtrVec::iterator              SCPtrIter; 

//--------------------------------------------------------------------
// globals
MODULEPRIVATE FileLogState  *g_pflstate;  // protected by g_pflstate->csState
MODULEPRIVATE SCPtrVec      *g_pscvec;    // protected by g_pflstate->csState
MODULEPRIVATE bool           g_bIsSafeAllocaInitialized = false;  // unprotected -- must be accessed in single-threaded fashion


//--------------------------------------------------------------------
// constants

// The amount of time we allow for an asynchronous file write to complete:
const DWORD WRITE_ENTRY_TIMEOUT           = 3000;
const DWORD FLTI_DEFAULT_MEMORY_INCREMENT = 256; 

//--------------------------------------------------------------------------------
// Forward declarations
MODULEPRIVATE HRESULT WaitForFileLogBuffer(FLBPtr pflb, DWORD dwTimeout);

//--------------------------------------------------------------------------------
// Function objects for use in STL algorithms:

struct CanFreeFileLogBuffer { 
    CanFreeFileLogBuffer(DWORD dwTimeout) : m_dwTimeout(dwTimeout) { }
    BOOL operator()(FLBPtr pflb) { 
	HRESULT hr = WaitForFileLogBuffer(pflb, m_dwTimeout); 
	return SUCCEEDED(hr); 
    }
private:
    DWORD m_dwTimeout; 
}; 

//
//--------------------------------------------------------------------------------


//####################################################################
// module private

//--------------------------------------------------------------------
// Synchronization: must have shared access to g_pflstate->csState
//                  must have access to g_pflstate->csFilePointer
MODULEPRIVATE HRESULT WriteCurrentFilePos(OVERLAPPED *po) { 
    po->Offset     = static_cast<DWORD>(g_pflstate->qwFilePointer & 0xFFFFFFFF); 
    po->OffsetHigh = static_cast<DWORD>(g_pflstate->qwFilePointer >> 32); 

    return S_OK; 
}

//--------------------------------------------------------------------------------
//
// FileLogThreadInfo methods
//

//--------------------------------------------------------------------------------
FileLogThreadInfo::~FileLogThreadInfo() { 
    vflbWritten.erase(remove_if(vflbWritten.begin(), vflbWritten.end(), CanFreeFileLogBuffer(INFINITE)), vflbWritten.end()); 
}

//--------------------------------------------------------------------
// Synchronization: must have shared access to g_pflstate->csState
MODULEPRIVATE HRESULT FLTI_AddNew(FLTIPtr *ppflti) { 
    bool      bAcquiredResource  = false; 
    HRESULT   hr; 
    
    // No buffer exists for this thread.  Create one:
    FLTIPtr pflti(new FileLogThreadInfo); 
    _JumpIfOutOfMemory(hr, error, pflti); 
    pflti->bFlush = true;  // flush buffers by default
    pflti->dwThreadId = GetCurrentThreadId(); 
    pflti->dwMemoryIncrement = FLTI_DEFAULT_MEMORY_INCREMENT; 
    pflti->pflbPending = NULL; 

    // We need exclusive access to write to the global thread info list:
    RtlConvertSharedToExclusive(&g_pflstate->csFileLogThreadInfo);

    // Add our new buffer to the global list of Thread buffers. 
    _SafeStlCall(g_pflstate->pvFileLogThreadInfo->push_back(pflti), hr, error, "g_pflstate->csFileLogThreadInfo->push_back(pflti)"); 

    *ppflti = pflti; 
    hr = S_OK; 
 error: 
    // No longer need shared access. 
    RtlConvertExclusiveToShared(&g_pflstate->csFileLogThreadInfo);
    return hr; 
}


//--------------------------------------------------------------------------------
// Synchronization: must have shared access to g_pflstate->csState
MODULEPRIVATE HRESULT FLTI_GetCurrent(FLTIPtr *ppflti) { 
    bool         bAcquiredResource  = false; 
    bool         bFoundCurrent      = false; 
    DWORD        dwThreadId         = GetCurrentThreadId(); 
    FLTIPtrVec  &vFLTI              = *(g_pflstate->pvFileLogThreadInfo); 
    HRESULT      hr;

    _AcquireResourceSharedOrFail(&(g_pflstate->csFileLogThreadInfo), bAcquiredResource, hr, error); 

    for (FLTIPtrIter fltiIter = vFLTI.begin(); fltiIter != vFLTI.end(); fltiIter++) { 
	if ((*fltiIter)->dwThreadId == dwThreadId) { 
	    *ppflti = (*fltiIter); 
	    bFoundCurrent = true; 
	    break; 
	}
    } 

    if (!bFoundCurrent) { 
	// No buffer exists for this thread.  Create one:
	hr = FLTI_AddNew(ppflti); 
	_JumpIfError(hr, error, "FLTI_AddNew"); 
    }

    hr = S_OK; 
 error:
    _ReleaseResource(&(g_pflstate->csFileLogThreadInfo), bAcquiredResource); 
    return hr; 
}


//--------------------------------------------------------------------------------
// Synchronization:  requires shared access to g_pflstate->csState
MODULEPRIVATE bool FLTI_FreeAsManyAsPossible() { 
    bool      bAcquiredResource  = false; 
    bool      bFreedOne          = false;
    DWORD     dwNumBuffers; 
    FLTIPtr   pflti(NULL);              
    HRESULT   hr; 

    hr = FLTI_GetCurrent(&pflti); 
    _JumpIfError(hr, error, "FLTI_GetCurrent"); 

    FLBPtrVec &vFLB = pflti->vflbWritten; 
    dwNumBuffers = vFLB.size(); 
    vFLB.erase(remove_if(vFLB.begin(), vFLB.end(), CanFreeFileLogBuffer(0)), vFLB.end()); 

    hr = S_OK; 
 error:
    // We've freed a buffer if the number of buffers in this vector has decreased.
    return SUCCEEDED(hr) && dwNumBuffers > vFLB.size(); 
}

//--------------------------------------------------------------------
// Synchronization:  requires shared access to g_pflstate->csState
MODULEPRIVATE HRESULT FLTI_Flush() { 
    bool                bEnteredCriticalSection  = false; 
    BOOL                fResult; 
    DWORD               dwBytesToWrite; 
    DWORD               dwErr; 
    FLBPtr              pflb(NULL); 
    FLTIPtr             pflti(NULL);
    HRESULT             hr;

    hr = FLTI_GetCurrent(&pflti); 
    _JumpIfError(hr, error, "FLTI_GetCurrent"); 

    if (!pflti->bFlush) { 
	// Flushing is disabled ... nothing to do nothing here. 
	goto done; 
    }

    pflb = pflti->pflbPending; 
    _MyAssert(NULL != pflb); 
    dwBytesToWrite = sizeof(WCHAR)*(wcslen(pflb->wszText)); 

    pflb->overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL); 
    if (NULL == pflb->overlapped.hEvent) { 
	_JumpLastError(hr, error, "CreateEvent"); 
    }

    hr = myEnterCriticalSection(&(g_pflstate->csFilePointer)); 
    _JumpIfError(hr, error, "myEnterCriticalSection"); 
    bEnteredCriticalSection = true; 

    WriteCurrentFilePos(&(pflb->overlapped)); 

    while (true) { 
        fResult = WriteFile
            (g_pflstate->hLogFile, 
             (LPCVOID)pflb->wszText, 
             dwBytesToWrite, 
             NULL, 
             &(pflb->overlapped)); 
        if (fResult) { 
	    goto success; 
	} else { 
            dwErr = GetLastError(); 
            switch (dwErr) { 
            case ERROR_INVALID_USER_BUFFER:
            case ERROR_NOT_ENOUGH_MEMORY: 
                // Probably just have too many asyncronous I/O requests pending.  
                // Let some of them finish and try again:
		if (!FLTI_FreeAsManyAsPossible()) { 
		    // Couldn't free any resources... just give up. 
		    hr = HRESULT_FROM_WIN32(dwErr); 
		    _JumpError(hr, error, "WriteFile"); 
                }

                // We've freed up some resources, let's try again ... 
                break;

            case ERROR_IO_PENDING:
                // The I/O operation has been successfully started. 
                goto success; 

            default:
                // An unexpected error: 
                hr = HRESULT_FROM_WIN32(dwErr); 
                _JumpError(hr, error, "WriteFile"); 
            }
        }
    }

 success:
    // Success: we've written this buffer out to the file. 
    // 1) Increment our file pointer
    g_pflstate->qwFilePointer += dwBytesToWrite; 
    if (0 != g_pflstate->qwFileSize) { 
	// circular logging is enabled
	g_pflstate->qwFilePointer %= g_pflstate->qwFileSize; 
    }

    // 2) remove our pending buffer
    pflti->pflbPending = NULL; 

    // 3) add this buffer to the list of buffers to be freed. 
    _SafeStlCall(pflti->vflbWritten.push_back(pflb), hr, error, "pflti->vflbWritten.push_back(pflb)"); 

 done:
    hr = S_OK;
 error:
    if (bEnteredCriticalSection) { 
	HRESULT hr2 = myLeaveCriticalSection(&(g_pflstate->csFilePointer)); 
	_IgnoreIfError(hr2, "myLeaveCriticalSection"); 
    }
    return hr; 
}


//--------------------------------------------------------------------
// Synchronization: must have shared access to g_pflstate->csState
MODULEPRIVATE HRESULT FLTI_SetFlush(bool bFlush, bool *pbOldFlush) { 
    HRESULT   hr; 
    FLTIPtr   pflti(NULL); 

    hr = FLTI_GetCurrent(&pflti); 
    _JumpIfError(hr, error, "FLTI_GetCurrent"); 

    if (NULL != pbOldFlush) {
	*pbOldFlush = pflti->bFlush;
    }
    pflti->bFlush = bFlush; 
    hr = S_OK;
 error:
    return hr; 
}

//--------------------------------------------------------------------
// Synchronization: must have shared access to g_pflstate->csState
MODULEPRIVATE HRESULT FLTI_SetMemoryIncrement(DWORD dwInc, DWORD *pdwOldInc) { 
    HRESULT  hr; 
    FLTIPtr  pflti(NULL); 

    hr = FLTI_GetCurrent(&pflti); 
    _JumpIfError(hr, error, "FLTI_GetCurrent"); 

    if (NULL != pdwOldInc) {
	*pdwOldInc = pflti->dwMemoryIncrement;
    }
    pflti->dwMemoryIncrement = dwInc; 
    hr = S_OK;
 error:
    return hr; 
}

//--------------------------------------------------------------------------------
// Synchronization: must have shared access to g_pflstate->csState
MODULEPRIVATE HRESULT FLTI_Append(LPWSTR pwszEntry) { 
    DWORD               ccEntry         = wcslen(pwszEntry); 
    DWORD               ccRequired; 
    FLBPtr              pflb(NULL); 
    FLTIPtr             pflti(NULL); 
    HRESULT             hr;  
    LPWSTR              pwszText        = NULL; 

    hr = FLTI_GetCurrent(&pflti);
    _JumpIfError(hr, error, "GetCurrent"); 

    if (NULL == pflti->pflbPending) { 
	// This thread doesn't have a pending FileLogBuffer, create one:
	 FLBPtr pflbTmp(new FileLogBuffer); 
	_JumpIfOutOfMemory(hr, error, pflbTmp); 

	// No memory allocated for this buffer yet. 
	pflbTmp->ccAllocated = 0; 

	// We've successfully created our new pending buffer.  
	pflti->pflbPending = pflbTmp; 
    } 

    pflb = pflti->pflbPending; 
    // To append the text to the buffer, we need the following amount of space:
    //   (sizeof characters in buffer, if any) + sizeof characters to append + 1
    ccRequired = ((NULL != pflb->wszText) ? wcslen(pflb->wszText) : 0) + ccEntry + 1;
    if (ccRequired > pflb->ccAllocated) { 
	DWORD ccNewEntry;
	
	ccNewEntry = ccRequired + pflti->dwMemoryIncrement - 1; 
	ccNewEntry = ccNewEntry - (ccNewEntry % pflti->dwMemoryIncrement); 
	_MyAssert(ccNewEntry >= ccRequired); 

	// This buffer doesn't have enough room, allocate more space: 
	pwszText = (LPWSTR)LocalAlloc(LPTR, sizeof(WCHAR)*ccNewEntry); 
	_JumpIfOutOfMemory(hr, error, pwszText); 
	pflb->ccAllocated = ccNewEntry; 

	// If we already had text in our file log buffer, copy it to the new buffer, 
	// and free the old buffer. 
	if (NULL != pflb->wszText) { 
	    wcscpy(pwszText, pflb->wszText); 
	    LocalFree(pflb->wszText); 
	}

	// Point to the new buffer
	pflb->wszText = pwszText; 
	pwszText = NULL; 
    }

    wcscat(pflb->wszText, pwszEntry); 
    hr = S_OK; 
 error:
    if (NULL != pwszText) { LocalFree(pwszText); } 
    return hr; 
}


//
// END: FileLogThreadInfo methods
//
//--------------------------------------------------------------------------------

//--------------------------------------------------------------------
FileLogBuffer::~FileLogBuffer() { 
    if (NULL != this->overlapped.hEvent) { CloseHandle(this->overlapped.hEvent); } 
    if (NULL != this->wszText)           { LocalFree(this->wszText); }
}

//--------------------------------------------------------------------
// Synchronization: none required. 
MODULEPRIVATE HRESULT WaitForFileLogBuffer(FLBPtr pflb, DWORD dwTimeout) { 
    DWORD           dwWaitResult; 
    HRESULT         hr; 

    dwWaitResult = WaitForSingleObject(pflb->overlapped.hEvent, dwTimeout); 
    switch (dwWaitResult) { 
    case WAIT_OBJECT_0: 
        break; 
    case WAIT_TIMEOUT:  // Timeout:  shouldn't be waiting this long.
        hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT); 
        _JumpError(hr, error, "WaitForSingleObject"); 
    default:
        hr = HRESULT_FROM_WIN32(GetLastError());
        _JumpError(hr, error, "WaitForSingleObject"); 
    }
    
    hr = S_OK; 
 error:
    return hr; 
}

//--------------------------------------------------------------------
MODULEPRIVATE void FreeLogEntryRangeChain(LogEntryRange * pler) {
    while (NULL!=pler) {
        LogEntryRange * plerTemp=pler;
        pler=pler->plerNext;
        LocalFree(plerTemp);
    }
}

//--------------------------------------------------------------------
MODULEPRIVATE void FreeFileLogConfig(FileLogConfig * pflc) {
    if (NULL!=pflc->plerAllowedEntries) {
        FreeLogEntryRangeChain(pflc->plerAllowedEntries);
    }
    if (NULL!=pflc->wszLogFileName) {
        LocalFree(pflc->wszLogFileName);
    }
    LocalFree(pflc);
}

//--------------------------------------------------------------------
// Synchronization: must have exclusive access to g_pflstate->csState
MODULEPRIVATE void EmptyAllBuffers() {
    FLTIPtrVec &vFLTI = *(g_pflstate->pvFileLogThreadInfo);  // aliased for readability
    vFLTI.erase(vFLTI.begin(), vFLTI.end()); 
}

//--------------------------------------------------------------------
// Synchronization:  does not need to be synchronized. 
MODULEPRIVATE HRESULT AddRegionToLogEntryRangeChain(LogEntryRange ** pplerHead, DWORD dwStart, DWORD dwLength) {
    HRESULT hr;
    LogEntryRange ** pplerPrev;
    LogEntryRange * plerStart;

    // find the range this range starts in
    pplerPrev=pplerHead;
    plerStart=*pplerPrev;
    while (NULL!=plerStart) {
        if (dwStart>=plerStart->dwStart && dwStart<=plerStart->dwStart+plerStart->dwLength) {
            // we will extend this range
            break;
        } else if (dwStart<plerStart->dwStart) {
            // we need to insert before this range, so stop now
            plerStart=NULL;
            break;
        }
        pplerPrev=&plerStart->plerNext;
        plerStart=*pplerPrev;
    }

    if (NULL!=plerStart) {
        // extend this range forward
        if (plerStart->dwLength<dwStart-plerStart->dwStart+dwLength) {
            plerStart->dwLength=dwStart-plerStart->dwStart+dwLength;
        }

    } else if (NULL!=*pplerPrev && (*pplerPrev)->dwStart<=dwStart+dwLength) {

        // we cannot extend an existing range forward, but we can extend a range backward
        LogEntryRange * plerNext=(*pplerPrev);
        if (dwLength<plerNext->dwStart-dwStart+plerNext->dwLength) {
            dwLength=plerNext->dwStart-dwStart+plerNext->dwLength;
        }
        plerStart=plerNext;
        plerStart->dwLength=dwLength;
        plerStart->dwStart=dwStart;

    } else {
        // we need to make a new range
        plerStart=(LogEntryRange *)LocalAlloc(LPTR, sizeof(LogEntryRange));
        _JumpIfOutOfMemory(hr, error, plerStart);

        plerStart->plerNext=*pplerPrev;
        plerStart->dwStart=dwStart;
        plerStart->dwLength=dwLength;

        *pplerPrev=plerStart;
    }

    // see if we can merge with the next
    while (NULL!=plerStart->plerNext && plerStart->plerNext->dwStart <= plerStart->dwStart + plerStart->dwLength) {
        LogEntryRange * plerNext=plerStart->plerNext;
        // merge
        if (plerStart->dwLength < plerNext->dwStart - plerStart->dwStart + plerNext->dwLength) {
            plerStart->dwLength=plerNext->dwStart - plerStart->dwStart + plerNext->dwLength;
        }
        // delete
        plerStart->plerNext=plerNext->plerNext;
        LocalFree(plerNext);
    }

    hr=S_OK;
error:
    return hr;
}

//--------------------------------------------------------------------
MODULEPRIVATE void WriteLogHeader(void) {
    FileLogAdd(L"---------- Log File Opened -----------------\n");
}

//--------------------------------------------------------------------
MODULEPRIVATE void WriteLogFooter(void) {
    FileLogAdd(L"---------- Log File Closed -----------------\n");
}

//--------------------------------------------------------------------
// Synchronization:  must be called with exclusive access to g_pflstate->csState
MODULEPRIVATE void AbortCloseFile(HRESULT hr2) {
    HRESULT hr;
    const WCHAR * rgwszStrings[1]={
        NULL
    };

    // must be cleaned up
    WCHAR * wszError=NULL;

    _MyAssert(NULL!=g_pflstate->hLogFile);
    DebugWPrintf1(L"Log file '%s' had errors. File closed.\n", g_pflstate->wszLogFileName);
    CloseHandle(g_pflstate->hLogFile);
    g_pflstate->hLogFile=NULL;
    LocalFree(g_pflstate->wszLogFileName);
    g_pflstate->wszLogFileName=NULL;
    EmptyAllBuffers();

    // get the friendly error message
    hr=GetSystemErrorString(hr2, &wszError);
    _JumpIfError(hr, error, "GetSystemErrorString");

    // log the event
    rgwszStrings[0]=wszError;
    DebugWPrintf1(L"Logging error: Logging was requested, but the time service encountered an error while trying to write to the log file. The error was: %s\n", wszError);
    hr=MyLogEvent(EVENTLOG_ERROR_TYPE, MSG_FILELOG_WRITE_FAILED, 1, rgwszStrings);
    _JumpIfError(hr, error, "MyLogEvent");

error:
    if (NULL!=wszError) {
        LocalFree(wszError);
    }
}

//--------------------------------------------------------------------
// Synchronization:  must be called with exclusive access to g_pflstate->csState
MODULEPRIVATE HRESULT FlushCloseFile(void) {
    HRESULT hr;

    WriteLogFooter();
    EmptyAllBuffers(); 
    CloseHandle(g_pflstate->hLogFile);
    g_pflstate->hLogFile = NULL;
    LocalFree(g_pflstate->wszLogFileName);
    g_pflstate->wszLogFileName = NULL;

    hr = S_OK;
    return hr;
}

//--------------------------------------------------------------------
MODULEPRIVATE HRESULT ReadFileLogConfig(FileLogConfig ** ppflc) {
    HRESULT hr;
    DWORD dwError;
    DWORD dwSize;
    DWORD dwType;
    WCHAR * wszEntryRange;

    // must be cleaned up
    LogEntryRange * plerAllowedEntries=NULL;
    FileLogConfig * pflc=NULL;
    HKEY hkConfig=NULL;
    WCHAR * wszAllowedEntries=NULL;

    // initailize out params
    *ppflc=NULL;

    // allocate a structure to hold the config data
    pflc=(FileLogConfig *)LocalAlloc(LPTR, sizeof(FileLogConfig));
    _JumpIfOutOfMemory(hr, error, pflc);

    // get our config key
    dwError=RegOpenKeyEx(HKEY_LOCAL_MACHINE, wszFileLogRegKeyConfig, 0, KEY_READ, &hkConfig);
    if (ERROR_SUCCESS!=dwError) {
        hr=HRESULT_FROM_WIN32(dwError);
        _IgnoreErrorStr(hr, "RegOpenKeyEx", wszFileLogRegKeyConfig);
        goto done;
    }

    // get the AllowedEntries
    dwError=RegQueryValueEx(hkConfig, wszFileLogRegValueFileLogEntries, NULL, &dwType, NULL, &dwSize);
    if (ERROR_SUCCESS!=dwError) {
        hr=HRESULT_FROM_WIN32(dwError);
        _IgnoreErrorStr(hr, "RegQueryValueEx", wszFileLogRegValueFileLogEntries);
    } else if (REG_SZ!=dwType) {
        hr=HRESULT_FROM_WIN32(ERROR_DATATYPE_MISMATCH);
        _IgnoreErrorStr(hr, "RegQueryValueEx", wszFileLogRegValueFileLogEntries);
    } else {
        wszAllowedEntries=(WCHAR *)LocalAlloc(LPTR, dwSize);
        _JumpIfOutOfMemory(hr, error, wszAllowedEntries);
        dwError=RegQueryValueEx(hkConfig, wszFileLogRegValueFileLogEntries, NULL, &dwType, (BYTE *)wszAllowedEntries, &dwSize);
        if (ERROR_SUCCESS!=dwError) {
            hr=HRESULT_FROM_WIN32(dwError);
            _JumpErrorStr(hr, error, "RegQueryValueEx", wszFileLogRegValueFileLogEntries);
        }

        // now, parse the string
        wszEntryRange=wszAllowedEntries+wcscspn(wszAllowedEntries, L"0123456789");
        while (L'\0'!=wszEntryRange[0]) {

            DWORD dwStart;
            DWORD dwStop;
            dwStart=wcstoul(wszEntryRange, &wszEntryRange, 0);
            if (L'-'!=wszEntryRange[0]) {
                dwStop=dwStart;
            } else {
                wszEntryRange++;
                dwStop=wcstoul(wszEntryRange, &wszEntryRange, 0);
            }
            DWORD dwLen;
            if (dwStop<dwStart) {
                dwLen=1;
            } else {
                dwLen=dwStop-dwStart+1;
            }
            hr=AddRegionToLogEntryRangeChain(&pflc->plerAllowedEntries, dwStart, dwLen);
            _JumpIfError(hr, error, "AddRegionToLogEntryRangeChain");

            wszEntryRange=wszEntryRange+wcscspn(wszEntryRange, L"0123456789");
        } // <- end string parsing loop
    } // <- end if value 'FileLogEntries' available

    // get the file name
    dwError=RegQueryValueEx(hkConfig, wszFileLogRegValueFileLogName, NULL, &dwType, NULL, &dwSize);
    if (ERROR_SUCCESS!=dwError) {
        hr=HRESULT_FROM_WIN32(dwError);
        _IgnoreErrorStr(hr, "RegQueryValueEx", wszFileLogRegValueFileLogName);
    } else if (REG_SZ!=dwType) {
        hr=HRESULT_FROM_WIN32(ERROR_DATATYPE_MISMATCH);
        _IgnoreErrorStr(hr, "RegQueryValueEx", wszFileLogRegValueFileLogEntries);
    } else {
        pflc->wszLogFileName=(WCHAR *)LocalAlloc(LPTR, dwSize);
        _JumpIfOutOfMemory(hr, error, pflc->wszLogFileName);
        dwError=RegQueryValueEx(hkConfig, wszFileLogRegValueFileLogName, NULL, &dwType, (BYTE *)pflc->wszLogFileName, &dwSize);
        if (ERROR_SUCCESS!=dwError) {
            hr=HRESULT_FROM_WIN32(dwError);
            _JumpErrorStr(hr, error, "RegQueryValueEx", wszFileLogRegValueFileLogName);
        }
    }

    // get the format flags
    dwSize = sizeof(DWORD); 
    dwError=RegQueryValueEx(hkConfig, wszFileLogRegValueFileLogFlags, NULL, &dwType, (BYTE *)&(pflc->dwFlags), &dwSize);
    if (ERROR_SUCCESS!=dwError) {
        hr=HRESULT_FROM_WIN32(dwError);
        _IgnoreErrorStr(hr, "RegQueryValueEx", wszFileLogRegValueFileLogFlags);
    } else if (REG_DWORD!=dwType) {
        hr=HRESULT_FROM_WIN32(ERROR_DATATYPE_MISMATCH);
        _IgnoreErrorStr(hr, "RegQueryValueEx", wszFileLogRegValueFileLogFlags);
    } 
    
    // get the file log size (used for circular logging)
    dwSize = sizeof(DWORD); 
    dwError=RegQueryValueEx(hkConfig, wszFileLogRegValueFileLogSize, NULL, &dwType, (BYTE *)&(pflc->dwFileSize), &dwSize);
    if (ERROR_SUCCESS!=dwError) {
        hr=HRESULT_FROM_WIN32(dwError);
        _IgnoreErrorStr(hr, "RegQueryValueEx", wszFileLogRegValueFileLogSize);
    } else if (REG_DWORD!=dwType) {
        hr=HRESULT_FROM_WIN32(ERROR_DATATYPE_MISMATCH);
        _IgnoreErrorStr(hr, "RegQueryValueEx", wszFileLogRegValueFileLogSize);
    } 

done:
    hr=S_OK;
    *ppflc=pflc;
    pflc=NULL;

error:
    if (NULL!=pflc) {
        FreeFileLogConfig(pflc);
    }
    if (NULL!=hkConfig) {
        RegCloseKey(hkConfig);
    }
    if (NULL!=wszAllowedEntries) {
        LocalFree(wszAllowedEntries);
    }
    return hr;
}


//####################################################################
// module public functions


//--------------------------------------------------------------------
HRESULT MyLogEvent(WORD wType, DWORD dwEventID, unsigned int nStrings, const WCHAR ** rgwszStrings) {
    HRESULT hr;

    // must be cleaned up
    HANDLE hEventLog=NULL;
    
    hEventLog=RegisterEventSource(NULL, L"W32Time");
    if (NULL==hEventLog) {
        _JumpLastError(hr, error, "RegisterEventSource");
    }
    if (!ReportEvent(hEventLog, wType, 0/*category*/, dwEventID, NULL, (WORD)nStrings, 0, rgwszStrings, NULL)) {
        _JumpLastError(hr, error, "ReportEvent");
    }

    hr=S_OK;
error:
    if (NULL!=hEventLog) {
        DeregisterEventSource(hEventLog);
    }
    return hr;
}

//--------------------------------------------------------------------
// NOTE: this function is used log the error code along with the error
//       message. If we do not have enough memory, we may not log the
//       error message.
//
HRESULT MyLogErrorMessage(DWORD dwErr, WORD wType, DWORD dwEventID )
{
    HRESULT       hr;
    LPWSTR        errMsge = NULL;
    WCHAR         wszNumberBuf[32]; 
    WCHAR         *rgwszStrings[1]    = { wszNumberBuf }; 


    if (dwErr != ERROR_SUCCESS) {
        FormatMessage( 
            FORMAT_MESSAGE_ALLOCATE_BUFFER | 
            FORMAT_MESSAGE_FROM_SYSTEM | 
            FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            dwErr,
            0, // Default language
            (LPTSTR) &errMsge,
            0,
            NULL 
            );
    
        if (errMsge) {
    
            rgwszStrings[0] = (WCHAR *) LocalAlloc(LPTR, (wcslen(errMsge)+20)*sizeof(WCHAR));
            if (rgwszStrings[0]) {
    
                rgwszStrings[0][0] = 0;
                swprintf(rgwszStrings[0], L"0x%08X: ", dwErr);
                wcscat(rgwszStrings[0], errMsge);
    
    
            } else {
                rgwszStrings[0]    = wszNumberBuf;
                swprintf(wszNumberBuf, L"0x%08X", dwErr);
            }
            LocalFree(errMsge);
    
         } else {

            swprintf(wszNumberBuf, L"0x%08X", dwErr);

         }
    } else {

        swprintf(wszNumberBuf, L"0x%08X: Success.", dwErr);

    }

    // Log an event indicating that the service is shutting down: 
    hr = MyLogEvent(wType, dwEventID, 1, (const WCHAR **) rgwszStrings);
    if (rgwszStrings[0] != wszNumberBuf) {
        LocalFree(rgwszStrings[0]);
    }

    return hr; 
}


//--------------------------------------------------------------------
HRESULT MyLogSourceChangeEvent(LPWSTR pwszSource) { 
    bool                   bAcquiredResource  = false; 
    HRESULT                hr;
    SourceChangeLogEntry  *pscle              = NULL; 

    hr = SourceChangeLogEntry::New(pwszSource, &pscle); 
    _JumpIfError(hr, error, "SourceChangeLogEntry.New"); 

    {
        SCPtr scp(pscle); 
        pscle = NULL;  // pscle will now be deleted when scp is destructed. 

	_AcquireResourceExclusiveOrFail(&(g_pflstate->csState), bAcquiredResource, hr, error); 

        SCPtrIter scExists = find(g_pscvec->begin(), g_pscvec->end(), scp); 
        if (scExists == g_pscvec->end()) { 
            // This is the first time we've done a sync from this source.  Log the event.  
            WCHAR * rgwszStrings[1] = { pwszSource };
            FileLog1(FL_SourceChangeAnnounce, L"Logging information: The time service is now synchronizing the system time with the time source %s.\n", rgwszStrings[0]);
            hr = MyLogEvent(EVENTLOG_INFORMATION_TYPE, MSG_TIME_SOURCE_CHOSEN, 1, (const WCHAR **)rgwszStrings);
            _JumpIfError(hr, error, "MyLogEvent");

            // Add this source change log event to the list:
            _SafeStlCall(g_pscvec->push_back(scp), hr, error, "g_pscvec->push_back");
        } else { 
            // We've already logged syncing from this source -- 
            // don't log it again (we'd fill up the event log).  
        }
    }

    hr = S_OK; 
 error:
    _ReleaseResource(&(g_pflstate->csState), bAcquiredResource); 
    if (NULL != pscle) { delete (pscle); }
    return hr; 
}


//--------------------------------------------------------------------
HRESULT MyResetSourceChangeLog() { 
    bool     bAcquiredResource = false; 
    HRESULT  hr; 

    _BeginTryWith(hr) { 
	_AcquireResourceExclusiveOrFail(&(g_pflstate->csState), bAcquiredResource, hr, error); 

	g_pscvec->clear(); 

    } _TrapException(hr); 

    if (FAILED(hr)) { 
	_JumpError(hr, error, "MyResetSourceChangeLog: HANDLED EXCEPTION"); 
    }

    hr = S_OK; 
 error:
    _ReleaseResource(&(g_pflstate->csState), bAcquiredResource); 
    return hr; 
}

//--------------------------------------------------------------------
// Synchronization:  this function is not threadsafe, it may not be called concurrently
//                   with other functions in this module. 
HRESULT FileLogBegin(void) {
    HRESULT hr;

    g_pflstate = NULL; 
    g_pscvec   = NULL; 

    // initialize safealloc if it hasn't already been initialized
    if (false == g_bIsSafeAllocaInitialized) { 
        SafeAllocaInitialize(0, 0, NULL, NULL);
        g_bIsSafeAllocaInitialized = true; 
    }

    g_pflstate = (FileLogState *)LocalAlloc(LPTR, sizeof(FileLogState)); 
    _JumpIfOutOfMemory(hr, error, g_pflstate); 

    g_pscvec = (SCPtrVec *)new SCPtrVec; 
    _JumpIfOutOfMemory(hr, error, g_pscvec); 

    g_pflstate->pvFileLogThreadInfo = new FLTIPtrVec; 
    _JumpIfOutOfMemory(hr, error, g_pflstate->pvFileLogThreadInfo); 

    hr = myRtlInitializeResource(&g_pflstate->csState);
    _JumpIfError(hr, error, "myInitializeResource");
    g_pflstate->bIsCsStateInitialized = true; 

    hr = myInitializeCriticalSection(&g_pflstate->csFilePointer); 
    _JumpIfError(hr, error, "myInitializeCriticalSection"); 
    g_pflstate->bIsCsFilePointerInitialized = true; 

    hr = myRtlInitializeResource(&g_pflstate->csFileLogThreadInfo); 
    _JumpIfError(hr, error, "myInitializeResource"); 
    g_pflstate->bIsCsFileLogThreadInfoInitialized = true; 

    // read the initial configuration
    hr = UpdateFileLogConfig(); // returns only non-ignorable errors
    _JumpIfError(hr, error, "UpdateFileLogConfig");
    
    g_pflstate->bIsAllStateInitialized = true; 
    hr = S_OK;
error:
    if (FAILED(hr)) { 
        FileLogEnd(); 
    }
    return hr;
}

//--------------------------------------------------------------------
HRESULT FileLogResume(void) { 
    bool     bAcquiredResource = false; 
    HRESULT  hr; 

    _AcquireResourceExclusiveOrFail(&(g_pflstate->csState), bAcquiredResource, hr, error); 

    hr = UpdateFileLogConfig(); 
    _JumpIfError(hr, error, "UpdateFileLogConfig"); 

    hr = S_OK; 
 error:
    _ReleaseResource(&(g_pflstate->csState), bAcquiredResource);
    return hr; 
}

//--------------------------------------------------------------------
HRESULT FileLogSuspend(void) { 
    bool     bAcquiredResource = false; 
    HRESULT  hr; 

    _AcquireResourceExclusiveOrFail(&(g_pflstate->csState), bAcquiredResource, hr, error); 

    if (NULL != g_pflstate->hLogFile) { 
         hr=FlushCloseFile();
        _JumpIfError(hr, error, "FlushCloseFile");
    }

    hr = S_OK; 
 error:
    _ReleaseResource(&(g_pflstate->csState), bAcquiredResource);
    return hr; 
}

//--------------------------------------------------------------------
// NOTE: FileLogEnd cannot be synchronized, so all other threads must be 
//       stopped before calling this method. 
// 
void FileLogEnd(void) {
    if (NULL != g_pflstate) { 
        if (NULL != g_pflstate->hLogFile) {
            HRESULT hr = FlushCloseFile();
            _IgnoreIfError(hr, "FlushCloseFile");
        }
        if (NULL != g_pflstate->plerAllowedEntries) { 
            FreeLogEntryRangeChain(g_pflstate->plerAllowedEntries); 
            g_pflstate->plerAllowedEntries = NULL; 
        }
        if (NULL != g_pflstate->pvFileLogThreadInfo) { 
            delete g_pflstate->pvFileLogThreadInfo; 
            g_pflstate->pvFileLogThreadInfo = NULL;
        }
        if (g_pflstate->bIsCsStateInitialized) { 
            RtlDeleteResource(&g_pflstate->csState); 
            g_pflstate->bIsCsStateInitialized = false; 
        }
        if (g_pflstate->bIsCsFilePointerInitialized) { 
            DeleteCriticalSection(&g_pflstate->csFilePointer);
            g_pflstate->bIsCsFilePointerInitialized = false; 
        }
        if (g_pflstate->bIsCsFileLogThreadInfoInitialized) { 
            RtlDeleteResource(&g_pflstate->csFileLogThreadInfo); 
            g_pflstate->bIsCsFileLogThreadInfoInitialized = false; 
        }
        LocalFree(g_pflstate); 
        g_pflstate = NULL; 
    }
    if (NULL != g_pscvec) { 
        delete (g_pscvec); 
        g_pscvec = NULL; 
    }
}

//--------------------------------------------------------------------
bool FileLogAllowEntry(DWORD dwEntry) {
    bool bAcquiredResource = false; 
    bool bAllow=false;
    HRESULT hr;
    LogEntryRange * pler;

    if (NULL != g_pflstate && g_pflstate->bIsAllStateInitialized) { 
        _AcquireResourceSharedOrFail(&(g_pflstate->csState), bAcquiredResource, hr, error); 

        pler=g_pflstate->plerAllowedEntries;
        while (NULL!=pler) {
            if (pler->dwStart>dwEntry) {
                break;
            } else if (pler->dwStart+pler->dwLength>dwEntry) {
                bAllow=true;
                break;
            }
            pler=pler->plerNext;
        }
        _ReleaseResource(&(g_pflstate->csState), bAcquiredResource); 
    }
 
 error: 
    return bAllow;   
}

//--------------------------------------------------------------------
void FileLogAddEx(bool bAppend, const WCHAR * wszFormat, va_list vlArgs) {
    bool                  bAcquiredResource  = false; 
    bool                  bMultiLine; 
    HRESULT               hr;
    signed int            nCharsWritten;
    unsigned int          nLen;
    unsigned __int64      teTime; 
    DWORD const           ccBuf      = 1024; 
    WCHAR                *wszBuf     = NULL;
    DWORD const           ccHeader   = 256; 
    WCHAR                *wszHeader  = NULL; 
    WCHAR                *wszMessage = NULL; 
    
    SafeAllocaAllocate(wszBuf, sizeof(WCHAR)*ccBuf); 
    _JumpIfOutOfMemory(hr, error, wszBuf); 

    SafeAllocaAllocate(wszHeader, sizeof(WCHAR)*ccHeader); 
    _JumpIfOutOfMemory(hr, error, wszHeader); 
    

#ifdef DBG  
    //  *  In debug builds, expand the string first, and log to screen,
    //  *  then stop if file not open.
    // expand substuitutions to our buffer
    nCharsWritten=_vsnwprintf(wszBuf, ccBuf, wszFormat, vlArgs);
    
    // if the buffer overflowed, mark it and ignore the overflow.
    if (-1==nCharsWritten || ccBuf==nCharsWritten) {
        wszBuf[ccBuf-3]=L'#';
        wszBuf[ccBuf-2]=L'\n';
        wszBuf[ccBuf-1]=L'\0';
    }
    DebugWPrintf1(L"%s", wszBuf);
#endif // DBG

    _AcquireResourceSharedOrFail(&(g_pflstate->csState), bAcquiredResource, hr, error); 

    // if there is no open file, don't even bother.
    if (NULL==g_pflstate->hLogFile) {
        goto done; 
    }
    
    // Free up some buffers if we can.  
    FLTI_FreeAsManyAsPossible();
    
#ifndef DBG
    //  *  In free builds, stop if file not open, then expand the string
    //  *  We do not log to screen.
    // expand substuitutions to our buffer
    nCharsWritten=_vsnwprintf(wszBuf, ccBuf, wszFormat, vlArgs);
    
    // if the buffer overflowed, mark it and ignore the overflow.
    if (-1==nCharsWritten || ccBuf==nCharsWritten) {
        wszBuf[ccBuf-3]=L'#';
        wszBuf[ccBuf-2]=L'\n';
        wszBuf[ccBuf-1]=L'\0';
    }
#endif // DBG

    /////////////////////////////////////////////////////////////
    //
    // 1) Write the header of the log entry
    
    if (FALSE == bAppend) { 
        AccurateGetSystemTime(&teTime);

        if (0 != (FL_NTTimeEpochTimestamps & g_pflstate->dwFlags)) { 
            // Use the NT time epoch directly: 
            swprintf(wszHeader, L"%08X:%016I64X:", GetCurrentThreadId(), teTime);
        } else { 
            // DEFAULT: convert to human-readable time: 
            unsigned __int64 qwTemp=teTime;
            DWORD  dwNanoSecs   = (DWORD)(qwTemp%10000000);
                   qwTemp      /= 10000000;
            DWORD  dwSecs       = (DWORD)(qwTemp%60);
                   qwTemp      /= 60;
            DWORD  dwMins       = (DWORD)(qwTemp%60);
                   qwTemp      /= 60;
            DWORD  dwHours      = (DWORD)(qwTemp%24);
            DWORD  dwDays       = (DWORD)(qwTemp/24);
            swprintf(wszHeader, L"%u %02u:%02u:%02u.%07us - ", dwDays, dwHours, dwMins, dwSecs, dwNanoSecs);
        }

        hr = FLTI_Append(wszHeader); 
        _JumpIfError(hr, error, "FLTI_Append"); 
    }
	
    //
    // 2) Parse the body of the log entry, replacing "\n" with "\r\n"
    // 
    SafeAllocaAllocate(wszMessage, sizeof(WCHAR)*(wcslen(wszBuf)+3)); 
    _JumpIfOutOfMemory(hr, error, wszMessage); 

    for (WCHAR *wszEntry = wszBuf; L'\0'!=wszEntry[0]; ) {
        // find the next line in this buffer
        WCHAR  *wszEntryEnd = wcschr(wszEntry, L'\n');
        bool    bMultiLine  = NULL != wszEntryEnd; 
	
        if (bMultiLine) { 
            nLen = (unsigned int)(wszEntryEnd-wszEntry)+2; // one more than necessary, to convert "\n" to "\r\n"
        } else {
            nLen = wcslen(wszEntry);
        }

        // copy it into an allocated buffer
        wcsncpy(wszMessage, wszEntry, nLen);
        wszMessage[nLen]=L'\0'; 

        if (bMultiLine) {
            // convert "\n" to "\r\n"
            wszMessage[nLen-2]=L'\r';
            wszMessage[nLen-1]=L'\n';
            nLen--;
        }

	// Append the message to this thread's buffer: 
	hr = FLTI_Append(wszMessage); 
	_JumpIfError(hr, error, "FLTI_Append"); 

	if (bMultiLine) { 
	    // If we've supplied a full line, flush the buffer. 
	    // This does nothing if flushing has been disabled. 
	    hr = FLTI_Flush(); 
	    _JumpIfError(hr, error, "FLTI_Flush"); 
	}

        wszEntry += nLen;
    } // <- end message parsing loop

    // SUCCESS
	
 done: 
 error:
    SafeAllocaFree(wszBuf);
    SafeAllocaFree(wszHeader);
    SafeAllocaFree(wszMessage);
    _ReleaseResource(&(g_pflstate->csState), bAcquiredResource); 
}

void FileLogAdd(const WCHAR *wszFormat, ...) { 
    va_list            vlArgs;

    va_start(vlArgs, wszFormat);
    FileLogAddEx(false, wszFormat, vlArgs); 
    va_end(vlArgs);    
}

void FileLogAppend(const WCHAR *wszFormat, ...) { 
    va_list            vlArgs;

    va_start(vlArgs, wszFormat);
    FileLogAddEx(true, wszFormat, vlArgs); 
    va_end(vlArgs);    
    
}

//====================================================================
// Dump data types


//--------------------------------------------------------------------
// Print out an NT-style time
void FileLogNtTimeEpochEx(bool bAppend, NtTimeEpoch te) {
    bool      bAcquiredResource  = false; 
    bool      bOldFlush; 
    bool      bSetFlush          = false; 
    HRESULT   hr; 

    // Acquire shared access to the file log state.  This prevents the file log
    // from being shutdown during this function. 
    _AcquireResourceSharedOrFail(&(g_pflstate->csState), bAcquiredResource, hr, error); 

    hr = FLTI_SetFlush(false, &bOldFlush); 
    _JumpIfError(hr, error, "FLTI_SetFlush"); 
    bSetFlush = true; 

    { 
        FileLogAdd(L" - %I64d00ns", te.qw);

        DWORD dwNanoSecs=(DWORD)(te.qw%10000000);
        te.qw/=10000000;
        DWORD dwSecs=(DWORD)(te.qw%60);
        te.qw/=60;
        DWORD dwMins=(DWORD)(te.qw%60);
        te.qw/=60;
        DWORD dwHours=(DWORD)(te.qw%24);
        DWORD dwDays=(DWORD)(te.qw/24);
        if (bAppend) { FileLogAppend(L" - %u %02u:%02u:%02u.%07us", dwDays, dwHours, dwMins, dwSecs, dwNanoSecs); }
        else         { FileLogAdd(L" - %u %02u:%02u:%02u.%07us", dwDays, dwHours, dwMins, dwSecs, dwNanoSecs);    }
    }

 error:;
    if (bSetFlush) { 
	hr = FLTI_SetFlush(bOldFlush, NULL); 
	_IgnoreIfError(hr, "FLTI_SetFlush"); 
    }
    _ReleaseResource(&(g_pflstate->csState), bAcquiredResource); 
}

void FileLogNtTimeEpoch(NtTimeEpoch te) { 
    FileLogNtTimeEpochEx(false, te); 
}

//--------------------------------------------------------------------
// Print out an NTP-style time
void FileLogNtpTimeEpochEx(bool bAppend, NtpTimeEpoch te) {
    bool     bAcquiredResource  = false; 
    bool     bOldFlush;
    HRESULT  hr; 

    // Acquire shared access to the file log state.  This prevents the file log
    // from being shutdown during this function. 
    _AcquireResourceSharedOrFail(&(g_pflstate->csState), bAcquiredResource, hr, error); 

    hr = FLTI_SetFlush(false, &bOldFlush); 
    _JumpIfError(hr, error, "FLTI_SetFlush"); 

    if (bAppend) { FileLogAppend(L"0x%016I64X", EndianSwap(te.qw)); } 
    else         { FileLogAdd(L"0x%016I64X", EndianSwap(te.qw)); } 

    if (0==te.qw) {
        if (bAppend) { FileLogAppend(L" - unspecified"); }
        else         { FileLogAdd(L" - unspecified"); }
    } else {
        FileLogNtTimeEpochEx(bAppend, NtTimeEpochFromNtpTimeEpoch(te));
    }

    hr = FLTI_SetFlush(bOldFlush, NULL); 
    _IgnoreIfError(hr, "FLTI_SetFlush"); 
 error:;
    _ReleaseResource(&(g_pflstate->csState), bAcquiredResource); 
    // return hr;
}

void FileLogNtpTimeEpoch(NtpTimeEpoch te) {
    FileLogNtpTimeEpochEx(false, te); 
}

//--------------------------------------------------------------------
void FileLogNtTimePeriodEx(bool bAppend, NtTimePeriod tp) {
    if (bAppend) { FileLogAppend(L"%02I64u.%07I64us", tp.qw/10000000,tp.qw%10000000); }
    else         { FileLogAdd(L"%02I64u.%07I64us", tp.qw/10000000,tp.qw%10000000); }
}

void FileLogNtTimePeriod(NtTimePeriod tp) {
    FileLogNtTimePeriodEx(false, tp);
}

//--------------------------------------------------------------------
void FileLogNtTimeOffsetEx(bool bAppend, NtTimeOffset to) {
    bool          bAcquiredResource  = false; 
    bool          bOldFlush;
    HRESULT       hr; 
    NtTimePeriod  tp;
    WCHAR         pwszSign[2]; 

    // Acquire shared access to the file log state.  This prevents the file log
    // from being shutdown during this function. 
    _AcquireResourceSharedOrFail(&(g_pflstate->csState), bAcquiredResource, hr, error); 

    hr = FLTI_SetFlush(false, &bOldFlush); 
    _JumpIfError(hr, error, "FLTI_SetFlush"); 

    if (to.qw<0) {
        wcscpy(pwszSign, L"-"); 
        tp.qw=(unsigned __int64)-to.qw;
    } else {
        wcscpy(pwszSign, L"+"); 
        tp.qw=(unsigned __int64)to.qw;
    }
    
    if (bAppend) { FileLogAppend(pwszSign); } 
    else         { FileLogAdd(pwszSign); } 

    FileLogNtTimePeriodEx(true /*append*/, tp);

    hr = FLTI_SetFlush(bOldFlush, NULL); 
    _IgnoreIfError(hr, "FLTI_SetFlush"); 

 error:;
    _ReleaseResource(&(g_pflstate->csState), bAcquiredResource); 
    // return hr; 
}

void FileLogNtTimeOffset(NtTimeOffset to) {
    FileLogNtTimeOffsetEx(false, to);
}


//--------------------------------------------------------------------
// Print out the contents of an NTP packet
// If nDestinationTimestamp is zero, no round trip calculations will be done
void FileLogNtpPacket(NtpPacket * pnpIn, NtTimeEpoch teDestinationTimestamp) {
    bool     bAcquiredResource  = false; 
    bool     bOldFlush; 
    DWORD    dwOldIncrement; 
    HRESULT  hr; 

    // Acquire shared access to the file log state.  This prevents the file log
    // from being shutdown during this function. 
    _AcquireResourceSharedOrFail(&(g_pflstate->csState), bAcquiredResource, hr, error); 

    hr = FLTI_SetFlush(false, &bOldFlush); 
    _JumpIfError(hr, error, "FLTI_SetFlush"); 
    
    hr = FLTI_SetMemoryIncrement(2048, &dwOldIncrement); 
    _JumpIfError(hr, error, "FLTI_SetMemoryIncrement"); 

    FileLogAdd(L"/-- NTP Packet:\n");
    FileLogAdd(L"| LeapIndicator: ");
    if (0==pnpIn->nLeapIndicator) {
        FileLogAppend(L"0 - no warning");
    } else if (1==pnpIn->nLeapIndicator) {
        FileLogAppend(L"1 - last minute has 61 seconds");
    } else if (2==pnpIn->nLeapIndicator) {
        FileLogAppend(L"2 - last minute has 59 seconds");
    } else {
        FileLogAppend(L"3 - not synchronized");
    }

    FileLogAppend(L";  VersionNumber: %u", pnpIn->nVersionNumber);

    FileLogAppend(L";  Mode: ");
    if (0==pnpIn->nMode) {
        FileLogAppend(L"0 - Reserved");
    } else if (1==pnpIn->nMode) {
        FileLogAppend(L"1 - SymmetricActive");
    } else if (2==pnpIn->nMode) {
        FileLogAppend(L"2 - SymmetricPassive");
    } else if (3==pnpIn->nMode) {
        FileLogAppend(L"3 - Client");
    } else if (4==pnpIn->nMode) {
        FileLogAppend(L"4 - Server");
    } else if (5==pnpIn->nMode) {
        FileLogAppend(L"5 - Broadcast");
    } else if (6==pnpIn->nMode) {
        FileLogAppend(L"6 - Control");
    } else {
        FileLogAppend(L"7 - PrivateUse");
    }

    FileLogAppend(L";  LiVnMode: 0x%02X\n", ((BYTE*)pnpIn)[0]);
    FileLogAdd(L"| Stratum: %u - ", pnpIn->nStratum);
    if (0==pnpIn->nStratum) {
        FileLogAppend(L"unspecified or unavailable");
    } else if (1==pnpIn->nStratum) {
        FileLogAppend(L"primary reference (syncd by radio clock)");
    } else if (pnpIn->nStratum<16) {
        FileLogAppend(L"secondary reference (syncd by (S)NTP)");
    } else {
        FileLogAppend(L"reserved");
    }

    FileLogAppend(L"\n"); 
    FileLogAdd(L"| Poll Interval: %d - ", pnpIn->nPollInterval);
    if (pnpIn->nPollInterval<4 || pnpIn->nPollInterval>14) {
        if (0==pnpIn->nPollInterval) {
            FileLogAppend(L"unspecified");
        } else {
            FileLogAppend(L"out of valid range");
        }
    } else {
        int nSec=1<<pnpIn->nPollInterval;
        FileLogAppend(L"%ds", nSec);
    }

    FileLogAppend(L";  Precision: %d - ", pnpIn->nPrecision);
    if (pnpIn->nPrecision>-2 || pnpIn->nPrecision<-31) {
        if (0==pnpIn->nPollInterval) {
            FileLogAppend(L"unspecified");
        } else {
            FileLogAppend(L"out of valid range");
        }
    } else {
        WCHAR * wszUnit=L"s";
        double dTickInterval=1.0/(1<<(-pnpIn->nPrecision));
        if (dTickInterval<1) {
            dTickInterval*=1000;
            wszUnit=L"ms";
        }
        if (dTickInterval<1) {
            dTickInterval*=1000;
            wszUnit=L"�s"; // shows up as �s on console
        }
        if (dTickInterval<1) {
            dTickInterval*=1000;
            wszUnit=L"ns";
        }
        FileLogAppend(L"%g%s per tick", dTickInterval, wszUnit);
    }

    FileLogAppend(L"\n"); 
    FileLogAdd(L"| RootDelay: ");
    {
        DWORD dwTemp=EndianSwap((unsigned __int32)pnpIn->toRootDelay.dw);
        FileLogAppend(L"0x%04X.%04Xs", dwTemp>>16, dwTemp&0x0000FFFF);
        if (0==dwTemp) {
            FileLogAppend(L" - unspecified");
        } else {
            FileLogAppend(L" - %gs", ((double)((signed __int32)dwTemp))/0x00010000);
        }
    }

    FileLogAppend(L";  RootDispersion: ");
    {
        DWORD dwTemp=EndianSwap(pnpIn->tpRootDispersion.dw);
        FileLogAppend(L"0x%04X.%04Xs", dwTemp>>16, dwTemp&0x0000FFFF);
        if (0==dwTemp) {
            FileLogAppend(L" - unspecified");
        } else {
            FileLogAppend(L" - %gs", ((double)dwTemp)/0x00010000);
        }
    }

    FileLogAppend(L"\n"); 
    FileLogAdd(L"| ReferenceClockIdentifier: ");
    {
        DWORD dwTemp=EndianSwap(pnpIn->refid.nTransmitTimestamp);
        FileLogAppend(L"0x%08X", dwTemp);
        if (0==dwTemp) {
            FileLogAppend(L" - unspecified");
        } else if (0==pnpIn->nStratum || 1==pnpIn->nStratum) {
            char szId[5];
            szId[0]=pnpIn->refid.rgnName[0];
            szId[1]=pnpIn->refid.rgnName[1];
            szId[2]=pnpIn->refid.rgnName[2];
            szId[3]=pnpIn->refid.rgnName[3];
            szId[4]='\0';
            FileLogAppend(L" - source name: \"%S\"", szId);
        } else if (pnpIn->nVersionNumber<4) {
            FileLogAppend(L" - source IP: %d.%d.%d.%d", 
                pnpIn->refid.rgnIpAddr[0], pnpIn->refid.rgnIpAddr[1],
                pnpIn->refid.rgnIpAddr[2], pnpIn->refid.rgnIpAddr[3]);
        } else {
            FileLogAppend(L" - last reference timestamp fraction: %gs", ((double)dwTemp)/(4294967296.0));
        }
    }
    
    FileLogAppend(L"\n"); 
    FileLogAdd(L"| ReferenceTimestamp:   ");
    FileLogNtpTimeEpochEx(true /*append*/, pnpIn->teReferenceTimestamp);

    FileLogAppend(L"\n"); 
    FileLogAdd(L"| OriginateTimestamp:   ");
    FileLogNtpTimeEpochEx(true /*append*/, pnpIn->teOriginateTimestamp);

    FileLogAppend(L"\n"); 
    FileLogAdd(L"| ReceiveTimestamp:     ");
    FileLogNtpTimeEpochEx(true /*append*/, pnpIn->teReceiveTimestamp);

    FileLogAppend(L"\n"); 
    FileLogAdd(L"| TransmitTimestamp:    ");
    FileLogNtpTimeEpochEx(true /*append*/, pnpIn->teTransmitTimestamp);

    if (0!=teDestinationTimestamp.qw) {
        FileLogAppend(L"\n"); 
        FileLogAdd(L">-- Non-packet info:");

        NtTimeEpoch teOriginateTimestamp=NtTimeEpochFromNtpTimeEpoch(pnpIn->teOriginateTimestamp);
        NtTimeEpoch teReceiveTimestamp=NtTimeEpochFromNtpTimeEpoch(pnpIn->teReceiveTimestamp);
        NtTimeEpoch teTransmitTimestamp=NtTimeEpochFromNtpTimeEpoch(pnpIn->teTransmitTimestamp);

        FileLogAppend(L"\n"); 
        FileLogAdd(L"| DestinationTimestamp: ");
        {
            NtpTimeEpoch teNtpTemp=NtpTimeEpochFromNtTimeEpoch(teDestinationTimestamp);
            NtTimeEpoch teNtTemp=NtTimeEpochFromNtpTimeEpoch(teNtpTemp);
            FileLogNtpTimeEpoch(teNtpTemp);
            unsigned __int32 nConversionError;
            if (teNtTemp.qw>teDestinationTimestamp.qw) {
                nConversionError=(unsigned __int32)(teNtTemp-teDestinationTimestamp).qw;
            } else {
                nConversionError=(unsigned __int32)(teDestinationTimestamp-teNtTemp).qw;
            }
            if (0!=nConversionError) {
                FileLogAppend(L" - CnvErr:%u00ns", nConversionError);
            }
        }

        FileLogAppend(L"\n"); 
        FileLogAdd(L"| RoundtripDelay: ");
        {
            NtTimeOffset toRoundtripDelay=
                (teDestinationTimestamp-teOriginateTimestamp)
                - (teTransmitTimestamp-teReceiveTimestamp);
            FileLogAppend(L"%I64d00ns (%I64ds)", toRoundtripDelay.qw, toRoundtripDelay.qw/10000000);
        }

        FileLogAppend(L"\n"); 
        FileLogAdd(L"| LocalClockOffset: ");
        {
            NtTimeOffset toLocalClockOffset=
                (teReceiveTimestamp-teOriginateTimestamp)
                + (teTransmitTimestamp-teDestinationTimestamp);
            toLocalClockOffset/=2;
            FileLogAppend(L"%I64d00ns", toLocalClockOffset.qw);
            unsigned __int64 nAbsOffset;
            if (toLocalClockOffset.qw<0) {
                nAbsOffset=(unsigned __int64)(-toLocalClockOffset.qw);
            } else {
                nAbsOffset=(unsigned __int64)(toLocalClockOffset.qw);
            }
            DWORD dwNanoSecs=(DWORD)(nAbsOffset%10000000);
            nAbsOffset/=10000000;
            DWORD dwSecs=(DWORD)(nAbsOffset%60);
            nAbsOffset/=60;
            FileLogAppend(L" - %I64u:%02u.%07u00s", nAbsOffset, dwSecs, dwNanoSecs);
        }
    } // <- end if (0!=nDestinationTimestamp)

    FileLogAppend(L"\n"); 

    // We've fully built the NTP packet lot, we can flush the next write.  
    hr = FLTI_SetFlush(bOldFlush, NULL); 
    _IgnoreIfError(hr, "FLTI_SetFlush"); 

    hr = FLTI_SetMemoryIncrement(dwOldIncrement, NULL); 
    _IgnoreIfError(hr, "FLTI_SetMemoryIncrement"); 

    FileLogAdd(L"\\--\n");
    
 error:;
    _ReleaseResource(&(g_pflstate->csState), bAcquiredResource); 
    // return hr; 
}

//--------------------------------------------------------------------
void FileLogSockaddrInEx(bool bAppend, sockaddr_in * psai) { 
    if (bAppend) { 
        FileLogAppend(L"%u.%u.%u.%u:%u",
                      psai->sin_addr.S_un.S_un_b.s_b1,
                      psai->sin_addr.S_un.S_un_b.s_b2,
                      psai->sin_addr.S_un.S_un_b.s_b3,
                      psai->sin_addr.S_un.S_un_b.s_b4,
                      EndianSwap((unsigned __int16)psai->sin_port));
    } else { 
        FileLogAdd(L"%u.%u.%u.%u:%u",
		   psai->sin_addr.S_un.S_un_b.s_b1,
		   psai->sin_addr.S_un.S_un_b.s_b2,
		   psai->sin_addr.S_un.S_un_b.s_b3,
		   psai->sin_addr.S_un.S_un_b.s_b4,
		   EndianSwap((unsigned __int16)psai->sin_port));
    }
}

void FileLogSockaddrIn(sockaddr_in * psai) {
    FileLogSockaddrInEx(false, psai); 
}


//--------------------------------------------------------------------
HRESULT UpdateFileLogConfig(void) {
    bool     bAcquiredResource  = false; 
    HRESULT  hr;
    HRESULT  hr2                = S_OK;
    LPWSTR   wszLogFileName     = NULL; 
    
    // must be cleaned up
    FileLogConfig * pflc=NULL;
    WCHAR * wszError=NULL;

    _BeginTryWith(hr) { 

	// read the config if possible
	hr=ReadFileLogConfig(&pflc); // returns mostly OOM errors
	_JumpIfError(hr, error,"ReadFileLogConfig");

	_AcquireResourceExclusiveOrFail(&(g_pflstate->csState), bAcquiredResource, hr, error); 

	g_pflstate->dwFlags = pflc->dwFlags; 
	g_pflstate->qwFileSize = pflc->dwFileSize; 

	// replace the list of allowed entries
	if (NULL!=g_pflstate->plerAllowedEntries) {
	    FreeLogEntryRangeChain(g_pflstate->plerAllowedEntries);
	}
	g_pflstate->plerAllowedEntries=pflc->plerAllowedEntries;
	pflc->plerAllowedEntries=NULL;

	// see what to do about the file
	if (NULL==pflc->wszLogFileName || L'\0'==pflc->wszLogFileName[0]) {
	    // close the file, if necessary
	    if (NULL!=g_pflstate->hLogFile) {
		hr=FlushCloseFile();
		_IgnoreIfError(hr, "FlushCloseFile");
	    }
	} else {
	    // open the file, if it is not opened already
	    if (NULL!=g_pflstate->wszLogFileName && 0==wcscmp(pflc->wszLogFileName, g_pflstate->wszLogFileName)) {
		// same file - no change
	    } else {
		// different file - open it
		LARGE_INTEGER liEOFPos; 

		// close the old file
		if (NULL!=g_pflstate->hLogFile) {
		    hr=FlushCloseFile();
		    _IgnoreIfError(hr, "FlushCloseFile");
		}
		g_pflstate->wszLogFileName=pflc->wszLogFileName;
		pflc->wszLogFileName=NULL;

		//open the new file
		g_pflstate->hLogFile=CreateFile(g_pflstate->wszLogFileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		if (INVALID_HANDLE_VALUE==g_pflstate->hLogFile) {
		    hr2=HRESULT_FROM_WIN32(GetLastError());
		    _IgnoreErrorStr(hr2, "CreateFile", g_pflstate->wszLogFileName);
		    g_pflstate->hLogFile=NULL;
		    wszLogFileName = g_pflstate->wszLogFileName; // preserve this value for error reporting
		    g_pflstate->wszLogFileName=NULL;
		    EmptyAllBuffers();
		} else if (!GetFileSizeEx(g_pflstate->hLogFile, &liEOFPos)) { 
		    hr2 = HRESULT_FROM_WIN32(GetLastError());
		    _IgnoreError(hr2, "GetFileSizeEx");
		    AbortCloseFile(hr2);
		    hr2 = S_OK;
		} else {
		    g_pflstate->qwFilePointer = liEOFPos.QuadPart; 
		    if (0 != g_pflstate->qwFileSize) { 
			g_pflstate->qwFilePointer %= g_pflstate->qwFileSize;
		    }
		    WriteLogHeader();
		}
	    } // <- end if need to open file
	} // <- end if file name given
    
	hr=S_OK;
	if (FAILED(hr2)) {
	    _MyAssert(NULL != wszLogFileName); 

	    // log an event on failure, but otherwise ignore it.
	    const WCHAR * rgwszStrings[2]={
		wszLogFileName, 
		NULL
	    };

	    // get the friendly error message
	    hr2=GetSystemErrorString(hr2, &wszError);
	    _JumpIfError(hr2, error, "GetSystemErrorString");

	    // log the event
	    rgwszStrings[1]=wszError;
	    DebugWPrintf1(L"Logging error: Logging was requested, but the time service encountered an error while trying to set up the log file. The error was: %s\n", wszError);
	    hr2=MyLogEvent(EVENTLOG_ERROR_TYPE, MSG_FILELOG_FAILED, 2, rgwszStrings);
	    _JumpIfError(hr2, error, "MyLogEvent");

	    LocalFree(wszLogFileName); 
	}
    } _TrapException(hr); 

    if (FAILED(hr)) { 
	_JumpError(hr, error, "UpdateFileLogConfig: HANDLED EXCEPTION"); 
    }

error:
    _ReleaseResource(&(g_pflstate->csState), bAcquiredResource); 
    if (NULL!=pflc) {
        FreeFileLogConfig(pflc);
    }
    if (NULL!=wszError) {
        LocalFree(wszError);
    }
    return hr;
}
