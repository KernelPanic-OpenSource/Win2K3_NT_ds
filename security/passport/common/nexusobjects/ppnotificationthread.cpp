/**********************************************************************/
/**                       Microsoft Passport                         **/
/**                Copyright(c) Microsoft Corporation, 1999 - 2001   **/
/**********************************************************************/

/*
    ppnotificationthread.cpp
        implement the methods runing a separte thread watching for registry
        changes, and timer for CCD refresh


    FILE HISTORY:

*/
#include "precomp.h"

PassportLockedInteger PpNotificationThread::m_NextHandle;

//
//  Constructor
//

PpNotificationThread::PpNotificationThread()
{
    LocalConfigurationUpdated();

    AddLocalConfigClient(dynamic_cast<IConfigurationUpdate*>(this), NULL);
}

//
//  Destructor
//


PpNotificationThread::~PpNotificationThread()
{
}

//
//  Add a CCD client to the notification list.
//

HRESULT
PpNotificationThread::AddCCDClient(
    tstring& strCCDName,
    ICCDUpdate* piUpdate,
    HANDLE* phClientHandle)
{
    HRESULT hr;
    NOTIFICATION_CLIENT clientInfo;

    try
    {
        clientInfo.dwNotificationType = NOTIF_CCD;
        clientInfo.NotificationInterface.piCCDUpdate = piUpdate;
        clientInfo.strCCDName = strCCDName;
        clientInfo.hClientHandle = (HANDLE)(LONG_PTR)(++m_NextHandle);

        {
            PassportGuard<PassportLock> guard(m_ClientListLock);
            m_ClientList.push_back(clientInfo);
        }

        if(phClientHandle != NULL)
        {
            *phClientHandle = clientInfo.hClientHandle;
        }

        hr = S_OK;
    }
    catch(...)
    {
        hr = E_OUTOFMEMORY;
    }

    return hr;
}

//
//  Add a configuration client to the notification list
//

HRESULT
PpNotificationThread::AddLocalConfigClient(
    IConfigurationUpdate* piUpdate,
    HANDLE* phClientHandle)
{
    HRESULT hr;
    NOTIFICATION_CLIENT clientInfo;

    clientInfo.dwNotificationType = NOTIF_CONFIG;
    clientInfo.NotificationInterface.piConfigUpdate = piUpdate;
    clientInfo.hClientHandle = (HANDLE)(LONG_PTR)(++m_NextHandle);

    {
        PassportGuard<PassportLock> guard(m_ClientListLock);
        try
        {
            m_ClientList.push_back(clientInfo);
        }
        catch(...)
        {
            hr = E_OUTOFMEMORY;
            goto Ret;
        }
    }

    if(phClientHandle != NULL)
    {
        *phClientHandle = clientInfo.hClientHandle;
    }

    hr = S_OK;
Ret:
    return hr;
}

//
//  Remove a client (either type) from the notification list.
//

HRESULT
PpNotificationThread::RemoveClient(
    HANDLE hClientHandle)
{
    HRESULT hr;
    PassportGuard<PassportLock> guard(m_ClientListLock);

    for(CLIENT_LIST::iterator it = m_ClientList.begin(); it != m_ClientList.end(); it++)
    {
        if((*it).hClientHandle == hClientHandle)
        {
            m_ClientList.erase(it);
            hr = S_OK;
            goto Cleanup;
        }
    }

    hr = E_FAIL;

Cleanup:

    return hr;
}

//
//  Do a manual refresh of a CCD.
//

HRESULT
PpNotificationThread::GetCCD(
    tstring&        strCCDName,
    IXMLDocument**  ppiXMLDocument,
    BOOL            bForceFetch)
{
    HRESULT                 hr;
    PpShadowDocument*       pShadowDoc;
    CCD_INFO                ccdInfo;

    {
        PassportGuard<PassportLock> guard(m_CCDInfoLock);

        //  Get the CCD Information for the requested CCD
        if(!GetCCDInfo(strCCDName, ccdInfo))
        {
            hr = E_INVALIDARG;
            pShadowDoc = NULL;
            goto Cleanup;
        }

        //  Create a new shadow document for the CCD
        if(ccdInfo.strCCDLocalFile.empty())
            pShadowDoc = new PpShadowDocument(ccdInfo.strCCDURL);
        else
            pShadowDoc = new PpShadowDocument(ccdInfo.strCCDURL, ccdInfo.strCCDLocalFile);
    }

    if(!pShadowDoc)
    {
        hr = E_OUTOFMEMORY;
        goto Cleanup;
    }

    //  Do the update.
    hr = pShadowDoc->GetDocument(ppiXMLDocument, bForceFetch);

    //BUGBUG  This is weird because currently other clients of this CCD will NOT get
    //        notified.  I don't want to loop through the notification list here for
    //        two reasons:
    //
    //        1.  The caller might be in the notification list and I don't to notify
    //            unnecessarily.
    //        2.  Don't want to put the overhead of notifying all clients on the
    //            caller's thread.
    //
    //        The ideal solution would be to wake up our dedicated thread and have it
    //        do the notification.  I haven't been able to find a way to signal a
    //        waitable time though.

Cleanup:

    if(pShadowDoc != NULL)
        delete pShadowDoc;

    return hr;
}

//
//
// register configure change notification
// register CCD update timeer
// call client notification sink
//
//
void
PpNotificationThread::run(void)
{
    {
        HANDLE*         pHandleArray    = NULL;
        LONG            lResult;
        PassportEvent   RegChangeEvent(FALSE,FALSE);
        DWORD           dwCurCCDInfo;
        DWORD           dwCurArrayLen;
        DWORD           dwWaitResult;
        DWORD           dwError;
        CCD_INFO_LIST::iterator it;
        CRegKey         PassportKey;
        BOOL            bKeyOpened;
        HRESULT         hr;
    
        hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        _ASSERT(hr != S_FALSE);
    
        lResult = PassportKey.Open(HKEY_LOCAL_MACHINE, TEXT("Software\\Microsoft\\Passport"), KEY_NOTIFY);
        bKeyOpened = (lResult == ERROR_SUCCESS);
    
        m_StartupThread.Set();
    
        while(WaitForSingleObject(m_ShutdownThread, 0) != WAIT_OBJECT_0)
        {
            if(bKeyOpened)
            {
                lResult = RegNotifyChangeKeyValue((HKEY)PassportKey, TRUE,
                                                  REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET,
                                                  (HANDLE)RegChangeEvent,
                                                  TRUE);
                if(lResult != ERROR_SUCCESS)
                    dwError = GetLastError();
            }
    
            {
                PassportGuard<PassportLock> guard(m_CCDInfoLock);
    
                dwCurArrayLen = m_aciCCDInfoList.size() + 2;
    
                pHandleArray = new HANDLE[dwCurArrayLen];
                if(pHandleArray == NULL)
                {
                    //  BUGBUG  Throw a low-memory alert here?
                    continue;
                }
    
                pHandleArray[0] = (HANDLE)m_ShutdownThread; //  Handle 0 always contains the thread shutdown signal.
                pHandleArray[1] = (HANDLE)RegChangeEvent; //  Handle 1 always contains the registry change event.
    
                for(it = m_aciCCDInfoList.begin(), dwCurCCDInfo = 0; it != m_aciCCDInfoList.end(); it++, dwCurCCDInfo++)
                {
                    pHandleArray[dwCurCCDInfo + 2] = (*it).hCCDTimer;
                }
            }
    
            dwWaitResult = WaitForMultipleObjects(dwCurArrayLen,
                                                  pHandleArray,
                                                  FALSE,
                                                  INFINITE);
            switch(dwWaitResult)
            {
            case WAIT_FAILED:
    
                dwError = GetLastError();
    
                break;
    
            //  Thread shutdown has been signalled.  Exit this thread.
            case WAIT_OBJECT_0:
                goto Cleanup;
    
            //  Registry change has been signalled.  Notify all local config clients.
            case WAIT_OBJECT_0 + 1:
    
                {
                    PassportGuard<PassportLock> guard(m_ClientListLock);
    
                    CLIENT_LIST::iterator cl_iter;
                    for(cl_iter = m_ClientList.begin(); cl_iter != m_ClientList.end(); cl_iter++)
                    {
                        if((*cl_iter).dwNotificationType == NOTIF_CONFIG)
                        {
                            (*cl_iter).NotificationInterface.piConfigUpdate->LocalConfigurationUpdated();
                        }
                    }
                }
    
                break;
    
            //  One of the CCD timers has been signalled.  Read the CCD and notify all CCD clients.
            default:
    
                {
                    IXMLDocumentPtr     xmlDoc;
                    PpShadowDocument    ShadowDoc;
                    DWORD               dwInfoIndex = dwWaitResult - WAIT_OBJECT_0 - 2;
    
                    //
                    // Due to the ugly nature of the code an allocation can fail within a constructor
                    // and cause AVs in this code.  So unfortunately we'll wrap this code to
                    // account for that.
                    //
                    try
                    {
                        m_CCDInfoLock.acquire();
                        CCD_INFO_LIST   aciTempCCDInfoList(m_aciCCDInfoList);
                        m_CCDInfoLock.release();
    
                        m_aciCCDInfoList[dwInfoIndex].SetTimer();
    
                         //  Fetch the CCD
                        ShadowDoc.SetURL(aciTempCCDInfoList[dwInfoIndex].strCCDURL);
                        if(!aciTempCCDInfoList[dwInfoIndex].strCCDLocalFile.empty())
                            ShadowDoc.SetLocalFile(aciTempCCDInfoList[dwInfoIndex].strCCDLocalFile);
    
                        if(ShadowDoc.GetDocument(&xmlDoc) == S_OK)
                        {
                            PassportGuard<PassportLock> guard(m_ClientListLock);
    
                            LPCTSTR pszUpdatedName = aciTempCCDInfoList[dwInfoIndex].strCCDName.c_str();
    
                            //  Loop through client list and call any clients registered for the CCD that
                            //  changed.
                            CLIENT_LIST::iterator cl_iter;
                            for(cl_iter = m_ClientList.begin(); cl_iter != m_ClientList.end(); cl_iter++)
                            {
                                if(lstrcmpi(pszUpdatedName, (*cl_iter).strCCDName.c_str()) == 0)
                                {
                                    (*cl_iter).NotificationInterface.piCCDUpdate->CCDUpdated(
                                                pszUpdatedName,
                                                (IXMLDocument*)xmlDoc);
                                }
                            }
                        }
                    }
                    catch(...)
                    {
                        if (g_pAlert)
                        {
                            g_pAlert->report(PassportAlertInterface::ERROR_TYPE, PM_CCD_NOT_LOADED, 0);
                        }
                    }
                }
    
                break;
    
            }
    
            delete [] pHandleArray;
            pHandleArray = NULL;
        }
    
    Cleanup:
    
        if(pHandleArray != NULL)
            delete [] pHandleArray;
    
        CoUninitialize();
    }
    m_ShutdownAck.Set();
}

//
//  Update our configuration.  This is called from the constructor, and
//  from the notification thread whenever the registry changes.
//

void
PpNotificationThread::LocalConfigurationUpdated()
{
    CRegKey NexusRegKey;
    LONG    lResult;
    DWORD   dwIndex;
    DWORD   dwNameLen;
    DWORD   dwDefaultRefreshInterval;
    TCHAR   achNameBuf[64];

    lResult = NexusRegKey.Open(HKEY_LOCAL_MACHINE,
                               TEXT("Software\\Microsoft\\Passport\\Nexus"),
                               KEY_READ);
    if(lResult != ERROR_SUCCESS)
    {
        //BUGBUG  This is a required reg key, throw an event.
        return;
    }

    //  Get the default refresh interval.
    lResult = NexusRegKey.QueryDWORDValue(TEXT("CCDRefreshInterval"), dwDefaultRefreshInterval);
    if(lResult != ERROR_SUCCESS)
    {
        //BUGBUG  This is a required reg value, throw an event.
        return;
    }

    //
    //  Lock down the list.
    //

    {
        PassportGuard<PassportLock> guard(m_CCDInfoLock);

        //
        //  Loop through existing list and remove any items whose corresponding keys
        //  have been removed.
        //

        CCD_INFO_LIST::iterator it;
        for(it = m_aciCCDInfoList.begin(); it != m_aciCCDInfoList.end(); )
        {
            CRegKey CCDRegKey;

            lResult = CCDRegKey.Open((HKEY)NexusRegKey, (*it).strCCDName.c_str(), KEY_READ);
            if(lResult != ERROR_SUCCESS)
            {
                it = m_aciCCDInfoList.erase(it);
            }
            else
                it++;
        }

        //
        //  Loop through each subkey and add/update the CCD info therein.
        //

        dwIndex = 0;
        dwNameLen = sizeof(achNameBuf) / sizeof(achNameBuf[0]);

        while(RegEnumKeyEx((HKEY)NexusRegKey, dwIndex,achNameBuf, &dwNameLen, NULL, NULL, NULL, NULL ) == ERROR_SUCCESS)
        {
            CRegKey CCDRegKey;

            lResult = CCDRegKey.Open((HKEY)NexusRegKey, achNameBuf, KEY_READ);
            if(lResult == ERROR_SUCCESS)
            {
                ReadCCDInfo(tstring(achNameBuf), dwDefaultRefreshInterval, CCDRegKey);
            }

            dwIndex++;
            dwNameLen = sizeof(achNameBuf);
        }
    }
}

//
//  This method starts the thread and then wait for the thread to get going.
//

bool
PpNotificationThread::start(void)
{
    m_StartupThread.Reset();

    bool bReturn = PassportThread::start();

    //
    //  Now wait for the thread to start.
    //

    WaitForSingleObject((HANDLE)m_StartupThread, INFINITE);
    return bReturn;
}

//
//  This method just signals the shutdown event causing the thread to terminate immediately.
//

void
PpNotificationThread::stop(void)
{
    m_ShutdownThread.Set();
    WaitForSingleObject(m_ShutdownAck, 1000);
    //  give it a chance to terminate
    Sleep(20);
}

//
//  Private method for reading the CCD info for a single CCD subkey from
//  the registry.
//

BOOL
PpNotificationThread::ReadCCDInfo(
    tstring&    strCCDName,
    DWORD       dwDefaultRefreshInterval,
    CRegKey&    CCDRegKey
    )
{
    BOOL                    bReturn = TRUE;
    LONG                    lResult;
    DWORD                   dwBufLen = 0;
    DWORD                   dwType;
    CCD_INFO_LIST::iterator it;
    LPTSTR                  pszRemoteFile = NULL;
    LPTSTR                  pszLocalFile = TEXT("");
    BOOL                    fLocalFileAllocated = FALSE;
    DWORD                   dwCCDRefreshInterval;
    LPTSTR                  pszTempFile = NULL;

    //
    //  Read in the remote path to the CCD.
    //  CCDRemoteFile is the only required value.  If it's not there, return FALSE.
    //

    lResult = CCDRegKey.QueryStringValue(TEXT("CCDRemoteFile"), NULL, &dwBufLen);
    if(lResult == ERROR_SUCCESS)
    {
        pszRemoteFile = (LPTSTR)LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, dwBufLen * sizeof(TCHAR));
        if (NULL == pszRemoteFile)
        {
            bReturn = FALSE;
            goto Cleanup;
        }

        CCDRegKey.QueryStringValue(TEXT("CCDRemoteFile"), pszRemoteFile, &dwBufLen);
        while(*pszRemoteFile && _istspace(*pszRemoteFile))
            pszRemoteFile++;
    }
    else
    {
        bReturn = FALSE;
        goto Cleanup;
    }

    //
    //  Read in the refresh interval for this CCD.
    //

    lResult = CCDRegKey.QueryDWORDValue(TEXT("CCDRefreshInterval"), dwCCDRefreshInterval);
    if(lResult != ERROR_SUCCESS)
        dwCCDRefreshInterval = 0xFFFFFFFF;

    //
    //  Read in local (backup) path for the CCD.  This is an optional value.  Use
    //  empty string (initialized above) as the default.
    //

    lResult = CCDRegKey.QueryValue(TEXT("CCDLocalFile"), &dwType, NULL, &dwBufLen);

    if(lResult == ERROR_SUCCESS)
    {
        if (dwType == REG_EXPAND_SZ)
        {
            pszTempFile = (LPTSTR) LocalAlloc(LMEM_FIXED, dwBufLen);

            if (pszTempFile)
            {
                lResult = CCDRegKey.QueryValue(TEXT("CCDLocalFile"), &dwType, pszTempFile, &dwBufLen);

                if (lResult == ERROR_SUCCESS)
                {
                    //
                    //  Expand out the environment variable
                    //

                    TCHAR tszTemp;

                    dwBufLen = ExpandEnvironmentStrings(pszTempFile, &tszTemp, 1);

                    if (dwBufLen > 1)
                    {
                        DWORD dwChars;

                        pszLocalFile = (LPTSTR)LocalAlloc(LMEM_FIXED, dwBufLen * sizeof(TCHAR));
                        if (NULL == pszLocalFile)
                        {
                            bReturn = FALSE;
                            goto Cleanup;
                        }
                        else
                        {
                            fLocalFileAllocated = TRUE;
                        }

                        dwChars = ExpandEnvironmentStrings(pszTempFile, pszLocalFile, dwBufLen);

                        if (dwChars > dwBufLen)
                        {
                            LocalFree(pszLocalFile);
                            fLocalFileAllocated = FALSE;
                            pszLocalFile = TEXT("");
                        }

                        while(*pszLocalFile && _istspace(*pszLocalFile)) pszLocalFile++;
                    }
                }
           }
        }
        else if (dwType == REG_SZ)
        {
            pszLocalFile = (LPTSTR)LocalAlloc(LMEM_FIXED, dwBufLen * sizeof(TCHAR));
            if (NULL == pszLocalFile)
            {
                bReturn = FALSE;
                goto Cleanup;
            }
            else
            {
                fLocalFileAllocated = TRUE;
            }

            if (CCDRegKey.QueryValue(TEXT("CCDLocalFile"), &dwType, pszLocalFile, &dwBufLen) != ERROR_SUCCESS)
            {
                LocalFree(pszLocalFile);
                fLocalFileAllocated = FALSE;
                pszLocalFile = TEXT("");
            }

            while(*pszLocalFile && _istspace(*pszLocalFile)) pszLocalFile++;
        }
    }

    //
    //  If this CCD is already in the list, then update it.
    //

    for(it = m_aciCCDInfoList.begin(); it != m_aciCCDInfoList.end(); it++)
    {
        if(lstrcmp((*it).strCCDName.c_str(), strCCDName.c_str()) == 0)
        {
            //  Check to see if the information has changed.
            if(lstrcmpi(pszRemoteFile, (*it).strCCDURL.c_str()) != 0 ||
               lstrcmpi(pszLocalFile,  (*it).strCCDLocalFile.c_str()) != 0 ||
               dwCCDRefreshInterval != (*it).dwCCDRefreshInterval ||
               dwDefaultRefreshInterval != (*it).dwDefaultRefreshInterval
              )
            {
                DWORD   dwOldRefreshInterval = ((*it).dwCCDRefreshInterval == 0xFFFFFFFF ?
                                                (*it).dwDefaultRefreshInterval :
                                                (*it).dwCCDRefreshInterval);
                DWORD   dwNewRefreshInterval = (dwCCDRefreshInterval == 0xFFFFFFFF ?
                                                dwDefaultRefreshInterval :
                                                dwCCDRefreshInterval);

                (*it).strCCDURL                 = pszRemoteFile;
                (*it).strCCDLocalFile           = pszLocalFile;
                (*it).dwCCDRefreshInterval      = dwCCDRefreshInterval;
                (*it).dwDefaultRefreshInterval  = dwDefaultRefreshInterval;

                if(dwOldRefreshInterval != dwNewRefreshInterval)
                    (*it).SetTimer();
            }

            break;
        }
    }

    //
    //  This is a new CCD, add it to the list.
    //

    if(it == m_aciCCDInfoList.end())
    {
        CCD_INFO ccdInfo;

        ccdInfo.strCCDName                  = strCCDName;
        ccdInfo.strCCDURL                   = pszRemoteFile;
        ccdInfo.strCCDLocalFile             = pszLocalFile;
        ccdInfo.dwCCDRefreshInterval        = dwCCDRefreshInterval;
        ccdInfo.dwDefaultRefreshInterval    = dwDefaultRefreshInterval;

        ccdInfo.SetTimer();

        try
        {
            m_aciCCDInfoList.push_back(ccdInfo);
        }
        catch(...)
        {
            bReturn = FALSE;
            goto Cleanup;
        }
    }

    bReturn = TRUE;

Cleanup:
    if (pszTempFile)
    {
        LocalFree(pszTempFile);
    }

    if (pszRemoteFile)
    {
        LocalFree(pszRemoteFile);
    }

    if (fLocalFileAllocated  && pszLocalFile)
    {
        LocalFree(pszLocalFile);
    }

    return bReturn;
}

//
//  Private method for retrieving a CCD_INFO structure given the CCD name.
//

BOOL
PpNotificationThread::GetCCDInfo(
    tstring&    strCCDName,
    CCD_INFO&   ccdInfo
    )
{
    CCD_INFO_LIST::iterator     it;

    for(it = m_aciCCDInfoList.begin(); it != m_aciCCDInfoList.end(); it++)
    {
        if(lstrcmpi((*it).strCCDName.c_str(), strCCDName.c_str()) == 0)
        {
            ccdInfo = (*it);
            return TRUE;
        }
    }

    return FALSE;
}

