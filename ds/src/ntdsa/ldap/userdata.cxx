/*++

Copyright (C) Microsoft Corporation, 1996 - 1999

Module Name:

    userdata.cxx

Abstract:

    This module implements the LDAP server for the NT5 Directory Service.

    This file contains the per connection user information which is
    implemented in the LDAP_CONN class. The code which processes each
    individual command is in command.cxx.

Author:

    Colin Watson     [ColinW]    23-Jul-1996

Revision History:

--*/

#include <NTDSpchx.h>
#pragma  hdrstop

#include "ldapsvr.hxx"
extern "C" {
#include <schnlsp.h>
#include <dstrace.h>
#include <mdlocal.h>
#undef new
#undef delete
#include <taskq.h>
extern void TQ_DelayLogOutofConnections( void *, void **, DWORD * );
extern void TQ_DelayLogOutofSendQueue( void *, void **, DWORD * );
}

#define  FILENO FILENO_LDAP_USER


LDAPString DisconnectNotifyName=DEFINE_LDAP_STRING("1.3.6.1.4.1.1466.20036");
//
// The following strings are used in a DisconnectNotify message when either 
// the threadstate hasn't been created yet, and therefore SetLdapError won't work,
// or when the error indicates there is not enough memory so allocating a new
// string is not an option.
//
LDAPString OutOfMemoryError =
 DEFINE_LDAP_STRING("Insufficient resources available to process this command");
LDAPString ContextExpiredError = 
 DEFINE_LDAP_STRING("The server has timed out this connection");
LDAPString ServerShuttingDownError = 
 DEFINE_LDAP_STRING("The server is shutting down");

LDAPString NullMessage = {0,(PUCHAR)""};

DWORD	gdwOutofConnections = 0;
#define FIVE_MINS 300

//
// Cached LDAP_CONN objects
//

LIST_ENTRY  LdapConnFreeList;

//
// Send limits.
//
LONG   g_cbUnAckedSendData = 0;
LONG   g_cUnAckedSends     = 0;

ULONG  ulSendQueueRecoveryClosedConns = 0;

    

LDAP_CONN::LDAP_CONN( VOID)
/*++
  This function creates a new UserData object for the information
    required to process requests from a new User connection.

  Returns:
     a newly constructed LDAP_CONN object.

--*/
{

    Reset( );

    //
    // Set up the default commarg for this session
    //

    InitializeListHead(&m_requestList);
    InitializeListHead( &m_listEntry);
    if ( !InitializeCriticalSectionAndSpinCount(&m_csLock, LDAP_SPIN_COUNT)) {

        m_State = BlockStateInvalid;

        DPRINT1(0,"Unable to initialize csLock critical section [err %d]\n", GetLastError());
        return;
    }

    if ( !InitializeCriticalSectionAndSpinCount(&m_csClientContext, LDAP_SPIN_COUNT)) {

        m_State = BlockStateInvalid;

        DPRINT1(0,"Unable to initialize csClientContext critical section [err %d]\n", GetLastError());
        DeleteCriticalSection(&m_csLock);
        return;
    }
    
    if ( !InitializeCriticalSectionAndSpinCount(&m_csIO, LDAP_SPIN_COUNT)) {
        
        m_State = BlockStateInvalid;

        DPRINT1(0,"Unable to initialize csIO critical section [err %d]\n", GetLastError());
        DeleteCriticalSection(&m_csLock);
        DeleteCriticalSection(&m_csClientContext);
        return;
    }
    
    //
    // built in request object should not be cached
    //

    m_requestObject.m_fAllocated = FALSE;

    IF_DEBUG(CONN) {
        DPRINT1(0,"LDAP_CONN object created  @ %08lX.\n", this);
    }

} // LDAP_CONN::LDAP_CONN()


LDAP_CONN::~LDAP_CONN(VOID)
{

    IF_DEBUG(CONN) {
        DPRINT1(0,"Delete LDAP_CONN object @ %08lX.\n", this);
    }

    Assert(IsListEmpty(&m_requestList));

    if (m_State != BlockStateInvalid) {
        Cleanup();
        DeleteCriticalSection( &m_csLock );
        DeleteCriticalSection( &m_csClientContext );
        DeleteCriticalSection( &m_csIO );
    }

} // LDAP_CONN::~LDAP_CONN()


VOID
LDAP_CONN::Reset(
            VOID
            )
{
    m_signature         = SIGN_LDAP_CONN_FREE;
    m_request           = NULL;
    m_fInitRecv         = FALSE;
    m_RefCount          = 0;
    m_Version           = 3;
    m_endofrequest      = 0xEEEE;
    m_State             = BlockStateClosed;
    m_ClientNumber      = 0;
    m_nTotalRequests    = 0;
    m_userName          = NULL;

    m_CallState         = inactive;
    m_nRequests         = 0;
    m_CodePage          = CP_UTF8;
    m_countNotifies     = 0;

    InitializeListHead( &m_CookieList );
    m_CookieCount       = 0;

    m_atqContext        = NULL;

    m_dwClientID        = dsGetClientID();
    m_Notifications     = NULL;

    m_pSecurityContext  = NULL;
    m_pPartialSecContext= NULL;
    m_clientContext     = NULL;
    
    m_SslState          = Sslunbound;
    m_cipherStrength    = 0;

    m_softExpiry.QuadPart = MAXLONGLONG;
    m_hardExpiry.QuadPart = MAXLONGLONG;

    m_dwStartTime       = 0;
    m_dwTotalBindTime   = 0;
    
    m_fFastBindMode     = FALSE;

    m_fSign             = FALSE;
    m_fSeal             = FALSE;
    m_fSimple           = FALSE;
    m_fGssApi           = FALSE;
    m_fSpNego           = FALSE;
    m_fDigest           = FALSE;
    m_fUserNameAlloc    = FALSE;

    m_fCanScatterGather = TRUE;
    m_fNeedsHeader      = FALSE;
    m_fNeedsTrailer     = FALSE;
    m_HeaderSize        = 0;
    m_TrailerSize       = 0;
    m_MaxEncryptSize    = MAXDWORD;

    m_lastRequestStartTime  = 0;
    m_lastRequestFinishTime = 0;
    m_bIsAdmin              = FALSE;
    m_bIsAuthenticated      = FALSE;
    m_fSuccessfulBindOccurred = FALSE;

    m_cbUnAckedSendData = 0;
    m_cUnAckedSends     = 0;

    m_cbUnAckedSendDataLimit = LDAP_MAX_UNAUTH_UNACKED_SEND_DATA;
    m_cUnAckedSendsLimit     = LDAP_MAX_UNAUTH_UNACKED_SENDS;

    ZeroMemory(&m_RemoteSocket, sizeof(m_RemoteSocket));

} // LDAP_CONN::Reset


BOOL
LDAP_CONN::Init(IN PLDAP_ENDPOINT LdapEndpoint,
                IN SOCKADDR     *psockAddrRemote    /* = NULL */ ,
                IN PATQ_CONTEXT pNewAtqContext      /* = NULL */ ,
                IN DWORD        cbWritten
                )
{
    BOOL fDoKeepalive = TRUE;
    PCHAR        pcTmpAddrStr;
    SOCKADDR_IN  *pSockInet = (SOCKADDR_IN *) psockAddrRemote;
    LDAP_CONNECTION_TYPE ldapServiceType =
                (LDAP_CONNECTION_TYPE)LdapEndpoint->ConnectionType;

    Assert(ldapServiceType >= LdapTcpType);
    Assert(ldapServiceType <=  MaxLdapType);

    GetSystemTimeAsFileTime(&m_connectTime);

    m_atqContext = pNewAtqContext;

    if(NULL != psockAddrRemote  &&
       NULL != (pcTmpAddrStr = inet_ntoa(pSockInet->sin_addr))) {
        m_RemoteSocket = *psockAddrRemote;
        strcpy((char *)m_RemoteSocketString, pcTmpAddrStr);
    } else {
        ZeroMemory(&m_RemoteSocket, sizeof(m_RemoteSocket));
        m_RemoteSocketString[0] = '\0';
    }

    //
    // Is this SSL?
    //

    m_fUDP = FALSE;
    m_fSSL = FALSE;
    m_fTLS = FALSE;
    m_fGC  = FALSE;
    m_fUsingSSLCreds = FALSE;

    if ( ldapServiceType == LdapUdpType ) {
        m_fUDP = TRUE;
    } else if ( ldapServiceType == GcTcpType ) {
        m_fGC = TRUE;
    } else if ( ldapServiceType == GcSslType ) {
        m_fSSL = TRUE;
        m_fGC = TRUE;
    } else if ( ldapServiceType == LdapSslType ) {
        m_fSSL = TRUE;
    } else {
        Assert( ldapServiceType == LdapTcpType );
    }
    if (m_fSSL) {
        PERFINC(pcLdapSSLConnsPerSec);
    }

    SetNetBufOpts(NULL);

    // If this is a UDP connection it will be too short
    // lived to make much use of the m_MsgIds buffer anyway.
    if (!m_fUDP) {
        ZeroMemory(m_MsgIds, sizeof(m_MsgIds));
        m_MsgIdsPos = 0;
    }
    //
    // if this is ssl, try to get a cert
    //

    if ( m_fSSL && !InitializeSSL() ) {
        IF_DEBUG(ERR_NORMAL) {
            DPRINT(0,"No Cert. Rejecting SSL connection\n");
        }
        return FALSE;
    }

    //
    //  Set the context so that all calls to LdapCompletionRoutine
    //  have context pointing to the appropriate LDAP_CONN object.
    //

    AtqContextSetInfo(pNewAtqContext,
                      ATQ_INFO_COMPLETION_CONTEXT,
                      (DWORD_PTR)this);

    if (!m_fUDP) {
        //
        // On non-datagram sockets, turn on keepalives so we
        // notice dead clients.  Yes, this means on every
        // completion we reset keepalives to be on, but if
        // keepalives are already on, this is a very cheap
        // operation, done entirely in memory.
        //

        if ( ((PATQ_CONTEXT)pNewAtqContext)->hAsyncIO && 
             setsockopt( (SOCKET) ((PATQ_CONTEXT)pNewAtqContext)->hAsyncIO,
                        SOL_SOCKET,
                        SO_KEEPALIVE,
                        (const CHAR *) &fDoKeepalive,
                        sizeof( fDoKeepalive)) != 0) {

            IF_DEBUG(WARNING) {
                DPRINT2(0,"setsockopt(%d,KA,TRUE) failed. Error = %d\n",
                        ((PATQ_CONTEXT)pNewAtqContext)->hAsyncIO,
                        WSAGetLastError());
            }
        }
    }

    m_request = &m_requestObject;
    m_request->Init(pNewAtqContext,this);

    if ( cbWritten == 0 ) {

        //
        //  This is a connect with no data. Allocate the request and start
        //  a receive.
        //

        if (!m_request->PostReceive()) {

            DereferenceAndKillRequest(m_request);

            LogEvent8(DS_EVENT_CAT_LDAP_INTERFACE,
                      DS_EVENT_SEV_BASIC,
                      DIRLOG_ATQ_CLOSE_SOCKET_ERROR,
                      szInsertUL(GetLastError()),
                      szInsertHex(DSID(FILENO,__LINE__)),
                      szInsertUL(m_dwClientID),
                      szInsertWin32Msg(GetLastError()),
                      NULL, NULL, NULL, NULL);
            return FALSE;
        }
    }
    return TRUE;

} // LDAP_CONN::Init



VOID
LDAP_CONN::Cleanup( VOID)
/*++
  This cleans up data in the user data object.

 Returns:
    None

--*/
{
    PLDAP_NOTIFICATION pBack = NULL;
    NOTIFYRES         *pNotifyRes;
    BOOL               bFreeThreadState = FALSE;
    BOOL               bCallDeregister = TRUE;

    if (m_atqContext != NULL) {
        AtqFreeContext(m_atqContext,TRUE);
        m_atqContext = NULL;
    }

    (VOID)FreeAllPagedBlobs(this);

    if(m_Notifications) {

        // Need to deregister some notifications


        // Need a thread state to do this
        if(!pTHStls) {
            // Create a thread state.
            if (!InitTHSTATE(CALLERTYPE_LDAP)) {
                DPRINT(0, "Unable to allocate DS threadstate!\n");

                // Well, we won't call the deregister, but we still need to
                // call the free()
                bCallDeregister = FALSE;
            } else {

                // We created a thread state, so we should kill it.
                bFreeThreadState = TRUE;
            }
        }

        // Deregister and free any notifications
        IF_DEBUG(NOTIFICATION) {
            DPRINT(0,"Running down notifications on connection cleanup.\n");
        }

        while(m_Notifications) {
            pNotifyRes = NULL;
            pBack = m_Notifications;
            if(bCallDeregister) {
                IF_DEBUG(NOTIFICATION) {
                    DPRINT1(0,"Calling DirNotifyUnregister on %p\n", m_Notifications);
                }

                DirNotifyUnRegister( m_Notifications->hServer, &pNotifyRes);
            }
            m_Notifications = m_Notifications->pNext;
            LdapFree(pBack);
        }

        if(bFreeThreadState) {
            free_thread_state();
        }
    }

    //
    // If we have a security context, we don't want it.
    //

    ZapSecurityContext();

    // if we have a client context, we don't want it
    EnterCriticalSection(&m_csClientContext);
    __try {
        AssignAuthzClientContext(&m_clientContext, NULL);
    }
    __finally {
        LeaveCriticalSection(&m_csClientContext);
    }
    
    // also, clear the thread state
    THSTATE *pTHS = pTHStls;
    if (pTHS) {
        AssignAuthzClientContext(&pTHS->pAuthzCC, NULL);
    }

    //
    // if this is an SSL context, free it.
    //

    ZapSSLSecurityContext();

    if (m_cbUnAckedSendData) {
        InterlockedExchangeAdd( &g_cbUnAckedSendData, -m_cbUnAckedSendData );
        m_cbUnAckedSendData = 0;
    }
    if (m_cUnAckedSends) {
        InterlockedExchangeAdd( &g_cUnAckedSends, -m_cUnAckedSends );
        m_cUnAckedSends = 0;
    }

    return;

} // LDAP_CONN::Cleanup()



VOID
LDAP_CONN::IoCompletion(
                   IN PVOID pvContext,
                   IN PUCHAR pbBuffer,
                   IN DWORD cbWritten,
                   IN DWORD hrCompletionStatus,
                   IN OVERLAPPED *lpo,
                   IN DWORD StartTick
                   )
/*++

Routine Description:

    This routine will be called to process received data, send completions,
    the data received with a connect and error indications.

Arguments:

    pvContext   - Atq context object.

    pbBuffer    - Data transferred.

    cbWritten   - Number of bytes transferred.

    hrCompletionStatus - Final completion status of operation.

    lpo         - lpOverlapped associated with request.

Return Value:

    None.

--*/
{
    PATQ_CONTEXT patqContext = (PATQ_CONTEXT) pvContext;
    BOOL fResponseSent = FALSE;
    DWORD  err;
    LDAPString DisconErrorMessage;

    IF_DEBUG(IO) {
        DPRINT4(0, "IoCompletion %08lx, lpo=%08lx, bytes=%d, status=%d\n",
            this, lpo, cbWritten, hrCompletionStatus);
    }

    if ((cbWritten == 0) && (hrCompletionStatus == NO_ERROR)) {

        PLDAP_REQUEST request;

        //
        //  Connection has dropped. Record it if necessary and wait for the
        //  reference count to drop to zero.
        //
        //  Note: Atq allows multiple closes on the same context. The socket
        //  will only be closed once.
        //

        LogEvent(DS_EVENT_CAT_LDAP_INTERFACE,
                 DS_EVENT_SEV_VERBOSE,
                 DIRLOG_ATQ_CLOSE_SOCKET_OK,
                 szInsertHex(DSID(FILENO,__LINE__)),
                 NULL,
                 NULL);

        Disconnect( );

        if (lpo != &m_atqContext->Overlapped) {

            //
            //  Write completion
            //

            request = CONTAINING_RECORD(lpo,LDAP_REQUEST,m_ov);
        } else {
            request = m_request;
        }

        request->DereferenceRequest( );
        DereferenceAndKillRequest(request);

    } else if ( (lpo == NULL) && (hrCompletionStatus == NO_ERROR) ) {

        //
        // Datagram
        //

        Assert(m_request != NULL);
        Assert(cbWritten != 0);
        Assert(m_fUDP);

        if (ERROR_SUCCESS == m_request->ReceivedClientData(cbWritten,pbBuffer)) {
            m_request->m_StartTick = StartTick;
            ProcessRequest(m_request, &fResponseSent);
        }

        //
        // If no response was sent, disconnect.
        //

        if ( !fResponseSent ) {
            Disconnect( );
        }
        return;

    } else {

        if (lpo != &m_atqContext->Overlapped) {

            PLDAP_REQUEST request = CONTAINING_RECORD(lpo,
                                                      LDAP_REQUEST,
                                                      m_ov);

            IF_DEBUG(IO) {
                DPRINT1(0,"Write completion on request %x\n",request);
            }

            // Record the completion of this msgId.

            if (!m_fUDP) {
                EnterCriticalSection(&m_csLock);

                Assert(MSGIDS_BUFFER_SIZE > m_MsgIdsPos);
                
                m_MsgIds[m_MsgIdsPos++] = request->m_MessageId;

                if (MSGIDS_BUFFER_SIZE <= m_MsgIdsPos) {
                    m_MsgIdsPos = 0;
                }

                LeaveCriticalSection(&m_csLock);
            }

            //
            //  Write completion
            //

            DecUnAckedSendData( request->GetTotalSendBufferSize() );

            if ( hrCompletionStatus == NO_ERROR ) {

                IF_DEBUG(IO) {
                    DPRINT1( 0, "AtqWriteFile complete @ %08lX.\n", request);
                }

                //
                //  The client can send multiple requests in the same
                //  packet. This is where we process the follow on request.
                //

                if (request->AnyBufferedData()) {
                    IF_DEBUG(IO) {
                        DPRINT(0, "Write completion with more buffer to process.\n");
                    }

                    Assert(request == m_request);

                    //
                    //  Avoid resending the same data again
                    //

                    request->ResetSend();
                    request->m_StartTick = StartTick;
                    request->DereferenceRequest( );
                    ProcessRequest( request, &fResponseSent );
                } else {

                    IF_DEBUG(IO) {
                        DPRINT(0,"Write completion with no more buffer.\n");
                    }

                    if ( request == m_request ) {
                        DPRINT1(0,"LDAPIoCompletion: request[%x]==m_request\n", 
                                request);
                    }
                    request->DereferenceRequest( );
                    DereferenceAndKillRequest(request);
                }
            } else {

                IF_DEBUG(IO) {
                    DPRINT2(0,"Failed write completion, status = 0x%x, substatus = 0x%x\n",
                            hrCompletionStatus,
                            lpo->Internal);
                }         
                
                Disconnect( );
                DereferenceAndKillRequest(request);
                request->DereferenceRequest( );
                goto error;
            }

        } else {

            IF_DEBUG(IO) {
                DPRINT1(0,"Got a read completion on request %x\n",m_request);
            }

            //
            //  Read completion
            //

            if ( hrCompletionStatus == NO_ERROR ) {

                err = m_request->ReceivedClientData(cbWritten);
                if (err != ERROR_SUCCESS) {
                    if (SEC_E_DECRYPT_FAILURE == err) {

                        DisconErrorMessage.value =
                            (PUCHAR)LdapComments[LdapBadEncryptTransition];

                        DisconErrorMessage.length =
                            strlen(LdapComments[LdapBadEncryptTransition]);

                        LdapDisconnectNotify(protocolError, &DisconErrorMessage);
                    }
                    Disconnect();
                    m_request->DereferenceRequest( );
                    DereferenceAndKillRequest(m_request);
                    goto error;
                }

                m_request->m_StartTick = StartTick;
                m_request->DereferenceRequest();
                ProcessRequest( m_request, &fResponseSent );
            } else {

                IF_DEBUG(IO) {
                    DPRINT2(0, "Read failed, status = 0x%x, substatus = 0x%x\n",
                            hrCompletionStatus,
                            lpo->Internal);
                }

                Disconnect( );
                m_request->DereferenceRequest();
                DereferenceAndKillRequest(m_request);
                goto error;
            }
        }
    }

    return;

error:

    if ( hrCompletionStatus == ERROR_NETNAME_DELETED ) {

        //
        // TCP-IP pipe broke (often because client app quit without
        // shutting down pipe.)
        //

        LogEvent(DS_EVENT_CAT_LDAP_INTERFACE,
                 DS_EVENT_SEV_BASIC,
                 DIRLOG_ATQ_CLOSE_SOCKET_CONTACT_LOST,
                 szInsertHex(DSID(FILENO,__LINE__)),
                 NULL,
                 NULL);
    } else {

        //
        // tear down connection when all writes finish.
        //

        LogEvent8(DS_EVENT_CAT_LDAP_INTERFACE,
                  DS_EVENT_SEV_BASIC,
                  DIRLOG_ATQ_CLOSE_SOCKET_ERROR,
                  szInsertUL(hrCompletionStatus),
                  szInsertHex(DSID(FILENO,__LINE__)),
                  szInsertUL(m_dwClientID),
                  szInsertWin32Msg(hrCompletionStatus),
                  NULL, NULL, NULL, NULL);

    }

    return;

} // LDAP_CONN::IoCompletion

//
// Some useful macros, just for this routine
//

#define SetReturnValue(X,Y,Z,A)                              \
            {                                                \
               m.protocolOp.choice = X##_chosen;             \
               m.protocolOp.u.X.bit_mask = 0;                \
               m.protocolOp.u.X.resultCode = Y;              \
               m.protocolOp.u.X.errorMessage = A;            \
               m.protocolOp.u.X.matchedDN = Z;               \
            }

#define SetReferralValue(X,Y)                                  \
            {                                                  \
                switch(m_Version) {                            \
                case 2:                                        \
                    m.protocolOp.u.X.errorMessage = Y->value;  \
                        break;                                 \
                default:                                       \
                    m.protocolOp.u.X.bit_mask |=               \
                        LDAPResult_referral_present;           \
                    m.protocolOp.u.X.LDAPResult_referral= Y;   \
                }                                              \
             }

VOID
LDAP_LogDisconnectEvent(DWORD     err,
                        DWORD     dsid,
                        DWORD     dwClientID
                       )
/*++

Routine Description:

    This routine is a wrapper used to lower the stack footprint of
    ProcessRequest.  This function logs a connection disconnect without
    having to put a LOG_PARAM_BLOCK on the stack.

Arguments:

    err   -  a win32 error code.
    dsid  -  the dsid where error occured.
    dwClientID - the client id of the connection being dropped.
        
--*/
{
    LogEvent8(DS_EVENT_CAT_LDAP_INTERFACE,
              DS_EVENT_SEV_BASIC,
              DIRLOG_ATQ_CLOSE_SOCKET_ERROR,
              szInsertUL(err),
              szInsertHex(dsid),
              szInsertUL(dwClientID),
              szInsertWin32Msg(err),
              NULL, NULL, NULL, NULL);
}

VOID
LDAP_LogUdpOverflowEvent(DWORD dwLen)
/*++

Routine Description:

    This routine is a wrapper used to lower the stack footprint of
    ProcessRequest.  This function logs a UdpOverflow without
    having to put a LOG_PARAM_BLOCK on the stack.

Arguments:

    dwLen - The length of the UDP packet.
            
--*/
{
            LogEvent(DS_EVENT_CAT_LDAP_INTERFACE,
                     DS_EVENT_SEV_BASIC,
                     DIRLOG_ATQ_MULTI_PACKET_UDP,
                     szInsertUL(dwLen),
                     NULL,
                     NULL);
}


VOID
LDAP_CONN::ProcessRequest(
    IN PLDAP_REQUEST request,
    IN PBOOL fResponseSent
    )
/*++

Routine Description:

    This routine will be called to process received data.

Arguments:

    request     - Object used to track this transaction.
    fResponseSent - Did we send a response back?

Return Value:

    ZERO if the LDAP_CONN object must be deleted by the caller.

--*/
{
    LDAPMsg*    pMessage = NULL;
    LDAPMsg     message;

    int         rc = ERROR_SUCCESS;
    DecryptReturnValues Decryptrc = Processed;

    UINT        retryCount;
    UINT        messageID=0;
    UINT        choice;
    UINT        length=0;
    Referral    pReferral=NULL;
    Controls    pControls=NULL;
    LDAPDN      MatchedDN={0,NULL};
    LDAPString  ErrorMessage={0,NULL};
    LDAPString  ExtendedResponse={0,NULL};
    LDAPOID     ResponseName={0,NULL};
    BOOL        bSkipSearchResponse=FALSE;
    LDAP_SECURITY_CONTEXT *pLocalSecContext = NULL;
    CtxtHandle            *phSecurityContext;
    DWORD       errorLine=0;
    BOOL        fDontLog = FALSE;
    BOOL        resultNotEncoded = TRUE;
    THSTATE     *pTHS=NULL;
    DWORD       error;
    BOOL        fWMIEventOn = FALSE;
    DWORD       dwActualSize = 0;
    DWORD       dwActualSealedSize = 0;
    RootDseFlags DseFlag = rdseNonDse;
    BOOL        fFastBind = FALSE;
    
    VOID
        LDAP_LogDisconnectEvent(DWORD     err,
                                DWORD     dsid,
                                DWORD     dwClientID
                                );

    VOID
        LDAP_LogUdpOverflowEvent(DWORD dwLen);

    //
    // Variables we need for sending back the return code from the calls
    // below.
    //

    LDAPMsg m;
    _enum1  code= success;

    //
    //  Ensure this thread is initialized properly for the Oss routines
    //

    *fResponseSent = FALSE;

    if (eServiceShutdown) {
        rc = ERROR_SHUTDOWN_IN_PROGRESS;
        errorLine = DSID(FILENO, __LINE__);
        ErrorMessage = ServerShuttingDownError;
        code = unavailable;
        goto Abandon;
    }

    //
    // check if the context has expired. 
    //

    if ( IsContextExpired(&m_softExpiry, &m_timeNow) ) {

        IF_DEBUG(WARNING) {
            DPRINT1(0,"Context has expired for connection %p. Disconnecting.\n",
                     this);
        }

        rc = SEC_E_CONTEXT_EXPIRED;
        errorLine = DSID(FILENO, __LINE__);
        ErrorMessage = ContextExpiredError;
        code = unavailable;
        fDontLog = TRUE;
        goto Abandon;
    }

    PossiblyRecoverSendQueue();
    //
    // Make sure we haven't been disconnected by a send queue recovery.
    //
    if (BlockStateClosed == m_State) {
        goto Abandon;
    }

    // Create a thread state.
    if (!(pTHS = InitTHSTATE(CALLERTYPE_LDAP))) {
        IF_DEBUG(ERROR) {
            DPRINT(0, "Unable to allocate DS threadstate!\n");
        }
        rc = ERROR_NO_SYSTEM_RESOURCES;
        errorLine = DSID(FILENO, __LINE__);
        ErrorMessage = OutOfMemoryError;
        code = unavailable;
        goto Abandon;
    }

    // Set up generic LDAP Request Event; some inner event will tell
    // us what actually took place.  This event captures more time

    fWMIEventOn = LdapRequestLogAndTraceEventStart();

    //
    // Set the connection as the client context
    //

    pTHS->ClientContext = GetClientNumber();

    //
    //  Note sharing private data with ssl.
    //

    if ( request->HaveSealedData() ) {
        if ( IsSSLOrTLS() ) {
            Decryptrc = request->DecryptSSL( );
        } else {
            Decryptrc = request->DecryptSignedOrSealedMessage(&dwActualSealedSize);
            if (NeedMoreInput == Decryptrc) {
                dwActualSize = dwActualSealedSize;
            }
        }
    } 

    if (Decryptrc != Processed) {

        if (Decryptrc == NeedMoreInput) {

            rc = MORE_INPUT;    //  Fall through to normal code path.

        } else if (Decryptrc == ResponseSent) {

            if (request->UnencryptedDataAvailable() == 0) {

                //
                //  We sent a response, read next part of negotiation
                //

                m_request = LDAP_REQUEST::Alloc(m_atqContext,this);
                if (m_request == NULL) {
                    IF_DEBUG(NOMEM) {
                        DPRINT1(0, "Unable to create REQUEST for SSL negotiate\n",
                                GetLastError());
                    }
                    rc = ERROR_NOT_ENOUGH_MEMORY;
                    errorLine = DSID(FILENO, __LINE__);
                    ErrorMessage = OutOfMemoryError;
                    goto Abandon;
                }

                IF_DEBUG(SSL) {
                    DPRINT1(0, "Starting a receive for negotiation[req %x].\n",
                       m_request);
                }

                if (!m_request->PostReceive()) {
                    DereferenceAndKillRequest(m_request);
                    rc = GetLastError();
                    errorLine = DSID(FILENO, __LINE__);
                    code = SetLdapError(unavailable,
                                        rc,
                                        LdapNetworkError,
                                        0,
                                        &ErrorMessage);

                    goto Abandon;
                }
            } // else there is more data in the input buffer to process
            // after the send is complete.
            else {
                IF_DEBUG(SSL) {
                    DPRINT(0,
                     "Not starting a receive, we better get a write completion.\n");
                }
            }
            
            if (fWMIEventOn) {
                //
                // send -1 as the request type to indicate that the true request
                // type is unknown.
                // 
                LdapRequestLogAndTraceEventEnd(5, success, rdseNonDse, -1);
            }
            free_thread_state();
            return;

        } else {
            errorLine = DSID(FILENO,__LINE__);
            rc = Decryptrc;
            code = SetLdapError(unavailable,
                                rc,
                                LdapDecryptFailed,
                                0,
                                &ErrorMessage);
            
            goto Abandon;
        }
    } else {
        rc = 0;
    }

    BERVAL bv;
    bv.bv_len = request->UnencryptedDataAvailable();
    bv.bv_val = (PCHAR)request->m_pReceiveBuffer;
    
    if ( rc == 0 ) {
        BERVAL blob;
        blob.bv_len = sizeof(request->m_tmpBuffer);
        blob.bv_val = (PCHAR)request->m_tmpBuffer;

        ZeroMemory(request->m_tmpBuffer, sizeof(request->m_tmpBuffer));

        rc = DecodeLdapRequest(&bv, &message, &blob, &dwActualSize, &error, &errorLine);
        if ( rc == ERROR_SUCCESS ) {
            pMessage = &message;
        } else if ( rc == ERROR_INSUFFICIENT_BUFFER ) {
            rc = MORE_INPUT;
            if (IsSSLOrTLS()) {
                // In this case the size returned is the size of the request,
                // and not the size of the network data that is required to 
                // decrypt a whole request.  Reset the size to zero so that 
                // GrowReceive can't make any bad assumptions.
                dwActualSize = 0;
            } else if (IsSignSeal()) {
                //
                // DecryptSignedOrSealedMessage may have already returned to us
                // the total buffer size needed so use that if it's there.
                //
                dwActualSize = dwActualSealedSize;
            }
        }
    }

    if (rc == MORE_INPUT) {

        //
        // We need more data, the packet didn't contain the entire LDAP
        // request.
        //

        if(m_fUDP) {

            //
            // UDP requests have to be entirely in one packet.
            //
            LDAP_LogUdpOverflowEvent(bv.bv_len);

            errorLine = DSID(FILENO,__LINE__);
            rc = ERROR_BAD_LENGTH;

        } else if (request->GrowReceive(dwActualSize)) {

            //
            // We're not UDP (where requests have to be entirely in one
            // packet) and we can grow the receive buffer to get more data.
            //

            Assert(request == m_request);

            IF_DEBUG(IO) {
                DPRINT1(0,"More input for request %x\n",m_request);
            }

            if (!m_request->PostReceive()) {
                errorLine = DSID(FILENO,__LINE__);
                rc = GetLastError();
                code = SetLdapError(unavailable,
                                    rc,
                                    LdapNetworkError,
                                    0,
                                    &ErrorMessage);
                
                goto Abandon;
            }

            //
            //  We know that this thread should not delete this so
            //  return any non-zero value. Do not use QueryReference
            //  since we can have a race between this thread and
            //  the AtqReadFile we just started.
            //
            if (fWMIEventOn) {
                //
                // send -1 as the request type to indicate that the true request
                // type is unknown.
                // 
                LdapRequestLogAndTraceEventEnd(6, success, rdseNonDse, -1);
            }

            free_thread_state();

            return;

        } else {
            IF_DEBUG(NOMEM) {
                DPRINT(0, "Unable to grow request\n");
            }
            errorLine = DSID(FILENO,__LINE__);
            rc = ERROR_NOT_ENOUGH_MEMORY;
            code = SetLdapError(unavailable,
                                rc,
                                LdapServerResourcesLow,
                                0,
                                &ErrorMessage);
            
        }

        goto Abandon;

    } else if ( rc != ERROR_SUCCESS ) {

        IF_DEBUG(WARNING) {
            DPRINT1(0, "Decoding LDAPMsg failed with return code = %d\n", rc);
        }
        code = DoSetLdapError(protocolError,
                            rc,
                            error,
                            0,
                            errorLine,
                            &ErrorMessage);
        
        goto Abandon;
    }


    //
    // Set up some locals
    //

    messageID = pMessage->messageID;
    choice = pMessage->protocolOp.choice;
    length = request->UnencryptedDataAvailable() - bv.bv_len;

        
    Assert(rc == 0); // We should have returned by now if rc is set.

    //
    // Either we decoded correctly or we failed to decode correctly but we
    // managed to decode enough to send back a protocol error.
    //

    ZeroMemory( &m, sizeof(LDAPMsg));

    //
    //  Read completed.
    //

    IF_DEBUG(IO) {
        DPRINT2(0, "Process Message @ %08lx type %d\n", messageID, choice );
    }

    //
    //  Move pointers on to account for the decoded data
    //

    request->ShrinkReceive( length );

    if(!m_fUDP) {

        if (choice != unbindRequest_chosen) {

            if ( !request->AnyBufferedData()) {

                //
                //  We have processed all the data received so far, put
                //  down a read for next set of LDAP operations.
                //

                m_request = LDAP_REQUEST::Alloc(m_atqContext,this);
                if ( m_request == NULL ) {
                    IF_DEBUG(NOMEM) {
                        DPRINT(0,"Unable to create request\n");
                    }

                    errorLine = DSID(FILENO,__LINE__);
                    rc = ERROR_NOT_ENOUGH_MEMORY;
                    ErrorMessage = OutOfMemoryError;
                    code = unavailable;
                    goto Abandon;
                }

                if (!m_request->PostReceive()) {
                    DereferenceAndKillRequest(m_request);
                    errorLine = DSID(FILENO,__LINE__);
                    rc = GetLastError();
                    code = SetLdapError(unavailable,
                                        rc,
                                        LdapNetworkError,
                                        0,
                                        &ErrorMessage);
                    goto Abandon;
                }

            } else {
                IF_DEBUG(IO) {
                    DPRINT(0, "Not starting a receive, we better get a write completion.\n");
                }
            }

        } else {

            //
            //  Special case unbind since there is no read to put down.
            //

            errorLine = DSID(FILENO,__LINE__);
            rc = ERROR_VC_DISCONNECTED;
            fDontLog = TRUE;
            goto Abandon;
        }
    } else {

        //
        // only search and abandon allowed for UDP. We can't send a response
        // since the responses for the other commands are not defined for
        // UDP.
        //

        if ( (choice != searchRequest_chosen) &&
             (choice != abandonRequest_chosen) ) {

            IF_DEBUG(WARNING) {
                DPRINT1(0,"LDAP: Illegal datagram operation[%d]\n",
                        choice);
            }
            fDontLog = TRUE;
            goto Abandon;
        }
    }

    request->m_MessageId = messageID;

    if (IsSendQueueFull()) {
        if (IsClientTooFarBehind()) {
            //
            // The client has not kept up with the data we are sending back, and
            // has sent too many subsequent requests since falling
            // behind.  It's time to give up on this client and disconnect it.
            //
            code = SetLdapError(adminLimitExceeded,
                                ERROR_DS_LDAP_SEND_QUEUE_FULL,
                                LdapClientTooBehindOnReceives,
                                0,
                                &ErrorMessage);
            rc = ERROR_DS_LDAP_SEND_QUEUE_FULL;
            errorLine = DSID(FILENO, __LINE__);
            goto Abandon;
        } else {
            //
            // The client is not keeping up with the amount of data we are sending
            // back.  If this continues then the clients net buffs will fill and
            // push back on the net buffs on the server.  If the server net buffs 
            // overflow then the server could run out of non-paged pool and become
            // unresponive.
            //
            code = SetLdapError(busy,
                                ERROR_DS_LDAP_SEND_QUEUE_FULL,
                                LdapClientBehindOnReceives,
                                0,
                                &ErrorMessage);
        }
    } else if(pMessage == NULL) {

        //
        // We don't actually have a message, which means the decode
        // above failed and we are only going through here to create an
        // error.
        //

        code = SetLdapError(protocolError,
                            ERROR_DS_DECODING_ERROR,
                            LdapDecodeError,
                            0,
                            &ErrorMessage);
    } else {

        if (choice == bindRequest_chosen) {
            //
            // PERFORMANCE: for now, if this is not a "fast bind" wrap all of 
            // bind inside this critical
            // section. It drastically serializes, but at least we know
            // our m_pSecurityContext will be safe.
            //
            
            if (IsFastBind()) {
                fFastBind = TRUE;
            } else {        
                EnterCriticalSection(&m_csLock);
            }
        }
        __try {
            if (choice == bindRequest_chosen) {
                if ( m_countNotifies != 0 ) {
    
                    //
                    // if we have outstanding notifications, then fail this bind.
                    // The client was supposed to cancel them before doing a rebind.
                    //
    
                    IF_DEBUG(WARNING) {
                        DPRINT1(0,"%u notifications outstanding. Rejecting bind request.\n",
                                 m_countNotifies);
                    }
    
                    code = SetLdapError(unwillingToPerform,
                                        ERROR_DS_UNWILLING_TO_PERFORM,
                                        LdapNoRebindOnActiveNotifications,
                                        0,
                                        &ErrorMessage);
                    __leave;
    
                } else if (m_CallState != inactive) {
                    IF_DEBUG(WARNING) {
                        DPRINT(0,"Already in a bind CallState. Rejecting bind request.\n");
                    }
                    code = SetLdapError(busy,
                                        ERROR_DS_ADMIN_LIMIT_EXCEEDED,
                                        LdapBindAllowOne,
                                        0,
                                        &ErrorMessage);
                    __leave;
                }
                
                if (!fFastBind) {
                    m_CallState = activeBind;
                }
                
                // if we have a client context, we can't keep it around any longer 
                // since we are binding.
                EnterCriticalSection(&m_csClientContext);
                __try {
                    AssignAuthzClientContext(&m_clientContext, NULL);
                }
                __finally {
                    LeaveCriticalSection(&m_csClientContext);
                }
            }
    
            // Grab the security context we're going to use for this call.
            // We grab because another thread doing a bind could change the
            // value of m_pSecurityContext (but only one other thread, binds
            // are mutually exclusive).
    
            pLocalSecContext = m_pSecurityContext;
            if(pLocalSecContext) {
                phSecurityContext = pLocalSecContext->GetSecurityContext();
    
            }
            else {
                // No context, use NULL
                phSecurityContext = NULL;
            }
    
            // We passed the above thread count limit stuff.  Go ahead and try.
            // Remember to let go of the critical section stuff (in case of a bind)
    
            // Record the time at the start of the request.
            m_lastRequestStartTime = GetTickCount();
    
            // Move the connection to the back of the list, but
            // only if this connection is still open and on the
            // ActiveConnectionsList and only if we are close
            // to the maximum connection count
            if (CurrentConnections > (LONG)(LdapMaxConnections / 2)) {
                ACQUIRE_LOCK(&csConnectionsListLock);
                if (BlockStateClosed != m_State) {            
                    RemoveEntryList(&m_listEntry);
                    InsertTailList(&ActiveConnectionsList, &m_listEntry);
                }
                RELEASE_LOCK(&csConnectionsListLock);
            }
    
            // setup for retrying.
            retryCount = 5;
            do {
                //
                //  Set so that the DS can impersonate this client
                // 
                pTHS->phSecurityContext = phSecurityContext;
                pTHS->CipherStrength = m_cipherStrength;
                pTHS->dwClientID = m_dwClientID;
                pTHS->ClientIP = ((PSOCKADDR_IN)&m_RemoteSocket)->sin_addr.s_addr;
                
                // current client context is inherited by the thread
                EnterCriticalSection(&m_csClientContext);
                __try {
                    AssignAuthzClientContext(&pTHS->pAuthzCC, m_clientContext);
                }
                __finally {
                    LeaveCriticalSection(&m_csClientContext);
                }
    
                MatchedDN.length = 0;
                MatchedDN.value = NULL;
                ErrorMessage.length = 0;
                ErrorMessage.value =  NULL;
                THClearErrors();
    
                switch (choice) {
    
                case bindRequest_chosen:
    
                    if (!fFastBind) {
                        AbandonAllRequests();
                    }
    
                    code =
                        BindRequest( pTHS, request, pMessage,
                                    &m.protocolOp.u.bindResponse.serverCreds,
                                    &ErrorMessage,
                                    &MatchedDN);
                    break;
    
                case searchRequest_chosen:
                    
                    code = SearchRequest(pTHS,
                                         &bSkipSearchResponse,
                                         request, 
                                         pMessage,
                                         &pReferral,
                                         &pControls,
                                         &ErrorMessage,
                                         &MatchedDN);
    
                    resultNotEncoded = FALSE;
                    DseFlag = request->GetDseFlag();
                    break;
    
                case modifyRequest_chosen:
                    code = ModifyRequest( pTHS, request, pMessage,
                                         &pReferral,
                                         &pControls,
                                         &ErrorMessage,
                                         &MatchedDN);
                    break;
    
                case addRequest_chosen:
                    code = AddRequest( pTHS, request, pMessage, &pReferral,
                                      &pControls,
                                      &ErrorMessage,
                                      &MatchedDN);
                    break;
    
                case delRequest_chosen:
                    code = DelRequest( pTHS, request, pMessage, &pReferral,
                                      &pControls,
                                      &ErrorMessage,
                                      &MatchedDN);
                    break;
    
                case modDNRequest_chosen:
                    code = ModifyDNRequest( pTHS, request, pMessage,
                                           &pReferral,
                                           &pControls,
                                           &ErrorMessage,
                                           &MatchedDN);
                    break;
    
                case compareRequest_chosen:
                    code = CompareRequest( pTHS, request, pMessage,
                                          &pReferral,
                                          &pControls,
                                          &ErrorMessage,
                                          &MatchedDN);
                    break;
    
                case abandonRequest_chosen:
                    AbandonRequest( pTHS, request, pMessage);
                    break;
    
                case extendedReq_chosen:
                    code = ExtendedRequest( pTHS, request, pMessage,
                                           &pReferral,
                                           &ErrorMessage,
                                           &MatchedDN,
                                           &ResponseName,
                                           &ExtendedResponse);
                    break;
    
                default:
                    break;
                }

                //
                // only allow a subset of our ATQ threads to be retrying at any
                // one time so that we don't make the server unresponsive
                //
                if (code == busy && retryCount) {
                    if (WaitForSingleObject(LdapRetrySem, 0) == WAIT_OBJECT_0) {
                        Sleep((1000 << (5 - retryCount)) + (GetCurrentThreadId() % 500));
                        ReleaseSemaphore(LdapRetrySem, 1, NULL);
                    } else {
                        retryCount = 0;
                    }
                }
            } while(code == busy && retryCount--);
    
            if (choice == bindRequest_chosen && code == success) {
                // if we were a bind, we need to grab the context from the thread and keep it
                // in our current context
                // current client context is inherited by the thread
                EnterCriticalSection(&m_csClientContext);
                __try {
                    AssignAuthzClientContext(&m_clientContext, pTHS->pAuthzCC);
                }
                __finally {
                    LeaveCriticalSection(&m_csClientContext);
                }
                
                if (m_pSecurityContext && pTHS->pAuthzCC == NULL) {
                    // Since we are caching the authz context, we expect it to have a value.
                    // It should have been created in LDAP_SECURITY_CONTEXT::SetClientType().
                    // We can not assert here because it is possible we failed to get the context, 
                    // yet the bind was successful. Let's just complain to the debugger.
                    DPRINT(0, "Bind failed to create an AuthZ client context. Are we out of memory?\n");
                    // We will experience a significant perf hit for this connection, 
                    // because every request will have to create its own AuthZ context 
                    // via impersonate/unimpersonate.
                }
            }
        }
        __finally {
            pTHS->phSecurityContext = NULL;
            // OK, we've made the call.  Leave the critical section stuff.
            if(pLocalSecContext != NULL) {
                pLocalSecContext->DereferenceSecurity();
                pLocalSecContext = NULL;
            }
    
            if (choice == bindRequest_chosen && !fFastBind) {
                m_CallState = inactive;
                LeaveCriticalSection(&m_csLock);
            }
        }
    }

    //
    // Build the response
    //

    retryCount=0;
    do {
        m.bit_mask = 0;
        m.messageID = request->m_MessageId;
        switch (choice) {
        case bindRequest_chosen:
            SetReturnValue(bindResponse, code, MatchedDN, ErrorMessage);
            if(m.protocolOp.u.bindResponse.serverCreds.choice) {
                // Some serverCred was set
                m.protocolOp.u.bindResponse.bit_mask = serverCreds_present;
                if(m.protocolOp.u.bindResponse.serverCreds.choice == 
                   sasl_v3response_chosen) {
                   m.protocolOp.u.bindResponse.bit_mask |= BindResponse_ldapv3;
                }
            }
            break;

        case searchRequest_chosen:
            if(bSkipSearchResponse) {

                //
                // We're not actually returning anything (either the search was
                // abandonded or it was a notificaton registration)
                // No return from this
                //

                if ( fWMIEventOn ){
                    fWMIEventOn = LdapRequestLogAndTraceEventEnd(
                                      1,
                                      (ULONG)code,
                                      DseFlag,
                                      choice);
                }

                free_thread_state();
                if (!request->AnyBufferedData()) {
                    IF_DEBUG(IO) {
                        DPRINT(0, "Killing a search request (no response sent)\n");
                    }
                    // We're completely done with this request.

                    DereferenceAndKillRequest(request);
                    return;
                } else {

                    BOOL fTemp;

                    Assert(!m_fUDP);
                    IF_DEBUG(IO) {
                        DPRINT(0, "Recursing a search request (no response sent)\n");
                    }

                    //
                    // Hmm.  More data in this request, but we won't get a write
                    // completion (our normal trigger to keep going).  Call
                    // recursively.
                    //

                    ProcessRequest(request, &fTemp);
                    return;
                }
            }

            //
            // result has been encoded by SearchRequest
            //

            if ( code == resultsTooLarge ) {

                //
                // Cant send back result
                //
                SetLdapError(code,
                             0,
                             LdapNetworkError,
                             0,
                             &ErrorMessage);
                code = protocolError;
                goto Abandon;
            }

            break;

        case modifyRequest_chosen:
            SetReturnValue(modifyResponse,code, MatchedDN, ErrorMessage);
            if(pReferral) {
                // A referral was returned
                SetReferralValue(modifyResponse, pReferral);
            }
            break;

        case addRequest_chosen:
            SetReturnValue(addResponse,code,MatchedDN, ErrorMessage);
            if(pReferral) {
                // A referral was returned
                SetReferralValue(addResponse, pReferral);
            }
            break;

        case delRequest_chosen:
            SetReturnValue(delResponse,code, MatchedDN, ErrorMessage);
            if(pReferral) {
                // A referral was returned
                SetReferralValue(delResponse, pReferral);
            }
            break;

        case modDNRequest_chosen:
            SetReturnValue(modDNResponse,code, MatchedDN, ErrorMessage);
            if(pReferral) {
                // A referral was returned
                SetReferralValue(modDNResponse, pReferral);
            }
            break;

        case compareRequest_chosen:
            SetReturnValue(compareResponse,code, MatchedDN, ErrorMessage);
            if(pReferral) {
                // A referral was returned
                SetReferralValue(compareResponse, pReferral);
            }
            break;

        case abandonRequest_chosen:

            //
            // No return from this
            //

            if ( fWMIEventOn ){
                fWMIEventOn = LdapRequestLogAndTraceEventEnd(
                                  2,
                                  (ULONG)code,
                                  DseFlag,
                                  choice);
            }

            free_thread_state();
            if (!request->AnyBufferedData()) {

                //
                // We're completely done with this request.
                //

                IF_DEBUG(IO) {
                    DPRINT(0, "Killing an abandon request (no response sent).\n");
                }
                DereferenceAndKillRequest(request);
                return;
            } else {

                BOOL fTemp;

                Assert(!m_fUDP);

                IF_DEBUG(IO) {
                    DPRINT(0, "Recursing an abandon request (no response sent).\n");
                }

                //
                // Hmm.  More data in this request, but we won't get a write
                // completion (our normal trigger to keep going).  Call
                // recursively.
                //

                ProcessRequest(request, &fTemp);
                return;
            }
            break;

        case extendedReq_chosen:
            SetReturnValue(extendedResp, code, MatchedDN, ErrorMessage);
            if(pReferral) {
                // A referral was returned
                m.protocolOp.u.extendedResp.bit_mask |=               
                    ExtendedResponse_referral_present;           
                m.protocolOp.u.extendedResp.ExtendedResponse_referral = pReferral;                                                                 
            }
            if (ResponseName.length) {
                m.protocolOp.u.extendedResp.bit_mask |= 
                    responseName_present;
                m.protocolOp.u.extendedResp.responseName.length = ResponseName.length;
                m.protocolOp.u.extendedResp.responseName.value = ResponseName.value;
            }
            if (ExtendedResponse.length) {
                m.protocolOp.u.extendedResp.bit_mask |= 
                    response_present;
                m.protocolOp.u.extendedResp.response.length = ExtendedResponse.length;
                m.protocolOp.u.extendedResp.response.value = ExtendedResponse.value;
            }
            break;
        default:
            errorLine = DSID(FILENO,__LINE__);
            rc = choice;
            code = SetLdapError(protocolError,
                                rc,
                                LdapEncodeError,
                                0,
                                &ErrorMessage);
            goto Abandon;
            break;
        }

        if ( resultNotEncoded ) {

            //
            // Either there was an error with a search earlier that was not
            // handled by LDAP_CONN::SearchRequest, or the send below failed,
            // and this is a retry.
            //
            if ( choice == searchRequest_chosen ) {

                Assert(code == other || code == busy);

                error = EncodeSearchResult(request,
                                           this,
                                           NULL,
                                           code,
                                           NULL,       // referrals
                                           NULL,       // controls
                                           &ErrorMessage,
                                           &MatchedDN,
                                           0,
                                           FALSE);

            } else {

                if (pControls) {
                    // one or more controls was returned
                    m.bit_mask |= controls_present;
                    m.controls = pControls;
                }

                error = EncodeLdapMsg(&m, request);
            }

            if ( error != ERROR_SUCCESS ) {

                IF_DEBUG(WARNING) {
                    DPRINT2(0,"Encoding response[choice %d] failed with return code = %d\n",
                        choice, error);
                }

                errorLine = DSID(FILENO,__LINE__);
                rc = error;
                code = SetLdapError(protocolError,
                                    rc,
                                    LdapEncodeError,
                                    0,
                                    &ErrorMessage);
                goto Abandon;
            }
        }

        if (request->Send(m_fUDP,
                          &m_hSslSecurityContext)) {
            // ********************************************************
            // * After a successful Send request may have been freed. *
            // * NULL it out so that it can't be used anymore.        * 
            // ********************************************************            
            request = NULL;

            if ( fWMIEventOn ){
                fWMIEventOn = LdapRequestLogAndTraceEventEnd(
                                  3,
                                  (ULONG)code,
                                  DseFlag,
                                  choice);
            }

            free_thread_state();
            *fResponseSent = TRUE;
            
            // Record completion time of the request.

            m_lastRequestFinishTime = GetTickCount();

            //
            //  We know that this thread should not delete this so
            //  return any non-zero value. Do not use QueryReference
            //  since we can have a race between this thread and
            //  the AtqReadFile we just started.
            //

            return; //  Normal case !!!!!!
        }

        IF_DEBUG(WARNING) {
            DPRINT(0,"Failed to send encoded message\n");
        }
    
        rc = GetLastError();

        errorLine = DSID(FILENO,__LINE__);
        if(rc == ERROR_NO_SYSTEM_RESOURCES) {

            // Drat we failed because the return message was too big.  Clear out
            // the SendBuffer in the request object, tweak the message to be an
            // error, and try again.

            IF_DEBUG(WARNING) {
                DPRINT1(0,"Sending response with err %d. Sending back error\n",rc);
            }

            resultNotEncoded = TRUE;
            code = other;
            pReferral = NULL;
            MatchedDN.length = 0;
            MatchedDN.value = NULL;
            pControls = NULL;
            ErrorMessage = OutOfMemoryError;
            request->ResetSend();
        }
        retryCount++;
    } while((retryCount == 1) && // Only try to resend once.
            (rc == ERROR_NO_SYSTEM_RESOURCES )); // and only if this error

    // Yes, an error occurred, we're abandoning the socket.
    code = SetLdapError(other,
                        rc,
                        LdapNetworkError,
                        0,
                        &ErrorMessage);
Abandon:

    //
    // Send notice of disconnect
    //

    if (!m_fUDP && (rc != ERROR_VC_DISCONNECTED)) {
        IF_DEBUG(WARNING) {
            DPRINT1(0,"Sending disconnect notify. rc = %d\n", rc);
        }
        LdapDisconnectNotify(code, &ErrorMessage);
    }

    if ( fWMIEventOn ){
        fWMIEventOn = LdapRequestLogAndTraceEventEnd(
                          4,
                          (ULONG)code,
                          DseFlag,
                          choice);
    }

    free_thread_state();
    
    if ( !fDontLog ) {
        if(!errorLine) {
            errorLine = DSID(FILENO,__LINE__);
        }

        if(!rc) {
            rc = GetLastError();
            errorLine = DSID(FILENO,__LINE__);
        }

        LDAP_LogDisconnectEvent(rc,
                                errorLine,
                                m_dwClientID
                                );
    }

    Disconnect( );
    DereferenceAndKillRequest(request);
    return;

} // ProcessRequest

#undef SetReturnValue
#undef SetReferralValue

VOID
LDAP_CONN::MarkRequestAsAbandonded(
        IN DWORD MessageID
        )
{
    LDAP_REQUEST *pRequest;
    PLIST_ENTRY  listEntry;

    EnterCriticalSection(&m_csLock);

    for ( listEntry = m_requestList.Flink;
          listEntry != &m_requestList;
          listEntry = listEntry->Flink
        ) {

        pRequest = CONTAINING_RECORD(
                                listEntry,
                                LDAP_REQUEST,
                                m_listEntry
                                );

        if(pRequest->m_MessageId == MessageID) {
            pRequest->m_fAbandoned = TRUE;
            break;
        }
    }

    LeaveCriticalSection(&m_csLock);
    return;
}


VOID
LDAP_CONN::DereferenceAndKillRequest(
        PLDAP_REQUEST pRequest
        )
/*++

Routine Description:

    This routine deassociates a request object with a connection object.
    it removes the request object from the request list and dereferences
    the request object.

Arguments:

    pRequest - request object to remove from list.

Return Value:

    None

--*/
{
    EnterCriticalSection(&m_csLock);

    if ( InterlockedCompareExchange( (long*)&pRequest->m_State, BlockStateClosed, BlockStateActive ) == BlockStateActive ) {

        RemoveEntryList(
            &pRequest->m_listEntry
            );

        LeaveCriticalSection(&m_csLock);

        //
        // remove the references to the request
        //

        pRequest->DereferenceRequest( );

    } else {
        LeaveCriticalSection(&m_csLock);
    }

} // LDAP_CONN::DereferenceAndKillRequest


_enum1
LDAP_CONN::PreRegisterNotify(
        MessageID messageID
        )
{
    PLDAP_NOTIFICATION pNew;
    _enum1 retVal = success;

    IF_DEBUG(NOTIFICATION) {
        DPRINT1(0,"PreRegisterNotify called with id %d\n",messageID);
    }

    if(!pTHStls->phSecurityContext) {
        // Don't allow unauthenticated clients to register notifications.
        IF_DEBUG(WARNING) {
            DPRINT(0,"Unauthenticated clients cannot do notifications\n");
        }
        SetLastError(ERROR_NOT_AUTHENTICATED);
        return unwillingToPerform;
    }

    // Go single threaded to add this to the notification list
    EnterCriticalSection(&m_csLock);
    __try {

        if(m_countNotifies >= LdapMaxNotifications) {
            // Too many already.
            retVal = adminLimitExceeded;
            IF_DEBUG(WARNING) {
                DPRINT2(0,"Notification limit[%d max=%d] exceeded\n",
                         m_countNotifies, LdapMaxNotifications);
            }
            SetLastError(ERROR_DS_ADMIN_LIMIT_EXCEEDED);
            _leave;
        }

        pNew = (PLDAP_NOTIFICATION)LdapAlloc(sizeof(LDAP_NOTIFICATION));
        if(!pNew) {
            // Failed to allocate a notification object.
            IF_DEBUG(NOMEM) {
                DPRINT(0,"LdapAlloc failed to allocate notification block\n");
            }
            retVal = other;
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            _leave;
        }

        // OK, init the new object and put it in the head of the queue
        pNew->hServer = 0;
        pNew->messageID = messageID;
        pNew->pNext =  m_Notifications;
        m_Notifications = pNew;
        m_countNotifies++;
    }
    __finally {
        LeaveCriticalSection(&m_csLock);
    }

    return retVal;
} // LDAP_CONN::PreRegisterNotify



BOOL
LDAP_CONN::RegisterNotify(
        IN ULONG hServer,
        MessageID messageID
        )
{
    PLDAP_NOTIFICATION pNew;
    BOOL               retVal = FALSE;

    // Find the notification that had better have been preregisterd.
    EnterCriticalSection(&m_csLock);
    __try {
        pNew = m_Notifications;
        while(pNew && pNew->messageID != messageID) {
            pNew = pNew->pNext;
        }
        if(pNew) {
            // Found it.
            // We had better not already have a server handle associated with
            // this.
            Assert(pNew->hServer == 0);
            pNew->hServer = hServer;
            retVal = TRUE;
        }
    }
    __finally {
        LeaveCriticalSection(&m_csLock);
    }

    return retVal;

} // LDAP_CONN::RegisterNotify




VOID
LDAP_CONN::UnregisterNotify(
        IN MessageID messageID,
        IN BOOLEAN fCoreUnregister
        )
{
    PLDAP_NOTIFICATION p1, p2;
    NOTIFYRES *pNotifyRes;

    // Go single threaded to avoid someone else messing with this list while we
    // are trying to remove something from the list.
    EnterCriticalSection(&m_csLock);
    __try {
        if(!m_Notifications) {
            _leave;
        }

        Assert(m_countNotifies);

        if(m_Notifications->messageID == messageID) {
            p1 = m_Notifications;
            if ( fCoreUnregister ) {
                DirNotifyUnRegister( m_Notifications->hServer, &pNotifyRes);
            }
            m_Notifications = m_Notifications->pNext;
            LdapFree(p1);
            m_countNotifies--;
            _leave;
        }

        p1 = m_Notifications;
        p2 = p1->pNext;
        while(p2) {
            if(p2->messageID == messageID) {
                p1->pNext = p2->pNext;
                if ( fCoreUnregister ) {
                    DirNotifyUnRegister( p2->hServer, &pNotifyRes);
                }
                m_countNotifies--;
                LdapFree(p2);
                _leave;
            }
            p1 = p2;
            p2 = p2->pNext;
        }
    }
    __finally {
        LeaveCriticalSection(&m_csLock);
    }

    return;
} // LDAP_CONN::UnregisterNotify



BOOL
LDAP_CONN::fGetMessageIDForNotifyHandle(
        IN ULONG hServer,
        OUT MessageID *pmessageID
        )
{
    PLDAP_NOTIFICATION p1;
    BOOL retVal = FALSE;

    // Go single threaded to avoid someone else messing with this list while we
    // are reading it.
    EnterCriticalSection(&m_csLock);
    __try {
        p1 = m_Notifications;
        while(p1) {
            if(p1->hServer == hServer) {
                *pmessageID = p1->messageID;
                retVal = TRUE;
                _leave;
            }
            p1 = p1->pNext;
        }
    }
    __finally {
        LeaveCriticalSection(&m_csLock);
    }

    return retVal;
} // LDAP_CONN::fGetMessageIDForNotifyHandle


VOID
LDAP_CONN::Disconnect(
        VOID
        )

/*++

Routine Description:

    This function closes the associated ATQ socket and removes the
    connection object from the global list

Arguments:

    None.

Return Value:

    None.

--*/
{

    ACQUIRE_LOCK(&csConnectionsListLock);

    if ( m_State != BlockStateClosed ) {

        LockNetCon();
        m_State = BlockStateClosed;
        UnlockNetCon();

        //
        // Remove from list of connections
        //

        RemoveEntryList( &m_listEntry);

        //
        // Decrement count of current users
        //

        if (!m_fUDP) {

            DEC(pcLDAPClients);
            PERFINC(pcLdapClosedConnsPerSec);
            InterlockedDecrement(&CurrentConnections);
            DPRINT1(VERBOSE, " CurrentConnections = %u\n", CurrentConnections);

        } else {
            UncountedConnections--;
            DPRINT1(VERBOSE," UncountedConnections = %u\n", UncountedConnections);
        }

        RELEASE_LOCK(&csConnectionsListLock);
        AtqCloseSocket(m_atqContext, TRUE);
        DereferenceConnection( );
    } else {
        RELEASE_LOCK(&csConnectionsListLock);
    }

} // LDAP_CONN::Disconnect



VOID
LDAP_CONN::Free(
    LDAP_CONN* pConn
    )

/*++

Routine Description:

    This function frees an LDAP_CONN object.

Arguments:

    pConn - object to be freed.

Return Value:

    None.

--*/
{
    pConn->Cleanup( );
    pConn->Reset( );

    IF_DEBUG(CONN) {
        DPRINT1(0,"Conn %p freed to ", pConn);
    }

    if ( LdapConnCached < LdapBlockCacheLimit ) {
        
        const PPLS ppls = GetPLS();
        
        ACQUIRE_LOCK( &ppls->LdapConnCacheLock);
        InterlockedIncrement( (long*)&LdapConnCached );
        InsertHeadList( &ppls->LdapConnCacheList, &pConn->m_listEntry );
        RELEASE_LOCK( &ppls->LdapConnCacheLock );

        IF_DEBUG(CONN) {
            DPRINT1(0,"cache.\n", pConn);
        }

    } else {
        delete pConn;
        LdapConnAlloc--;

        IF_DEBUG(CONN) {
            DPRINT(0,"heap.\n");
        }
    }
    InterlockedDecrement(&ActiveLdapConnObjects);

} // Free


PLDAP_CONN
LDAP_CONN::Alloc(
    IN BOOL fUDP
    )

/*++

Routine Description:

    This function creates a new (LDAP_CONN) object.

    It increments the counter of current connections and returns
    the allocated object (if non NULL).

Arguments:

    fUDP     do we exempt this from the connections count?

    pfMaxExceeded  pointer to BOOL which on return indicates if max
                       connections limit was exceeded.

Return Value:

    Pointer to the LDAP_CONN object allocated.

--*/
{
    size_t iProcBase, iProc;
    PLDAP_CONN pConn = NULL;
    LONG oldCount;
    PLIST_ENTRY listEntry = NULL;

    //
    // We can add this new connection
    //

    iProcBase = GetProcessor();

    for ( iProc = 0; !listEntry && iProc < 2 * GetProcessorCount(); iProc++ ) {
            
        const PPLS ppls = GetSpecificPLS((iProcBase + iProc) % GetProcessorCount());

        if ( IsListEmpty( &ppls->LdapConnCacheList ) ) {
            continue;
        }

        if ( iProc < GetProcessorCount() ) {
            if ( !TryEnterCriticalSection( &ppls->LdapConnCacheLock ) ) {
                continue;
            }
        } else {
            ACQUIRE_LOCK( &ppls->LdapConnCacheLock );
        }

        if ( !IsListEmpty( &ppls->LdapConnCacheList ) ) {
            if ( !iProc ) {
                listEntry = RemoveHeadList( &ppls->LdapConnCacheList );
            } else {
                listEntry = RemoveTailList( &ppls->LdapConnCacheList );
            }
        }

        RELEASE_LOCK( &ppls->LdapConnCacheLock );
        
    }

    if ( listEntry ) {

        InterlockedDecrement( (long*)&LdapConnCached );

        pConn = CONTAINING_RECORD(listEntry,
                                  LDAP_CONN,
                                  m_listEntry
                                  );
        IF_DEBUG(CONN) {
            DPRINT1(0,"CONN %x alloc from cache\n", pConn);
        }

    } else {

        pConn = new LDAP_CONN;
        if (pConn == NULL ) {
            IF_DEBUG(NOMEM) {
                DPRINT(0,"LDAP: Cannot allocate connection object\n");
            }
            return NULL;
        }

        if ( pConn->m_State == BlockStateInvalid ) {
            delete pConn;
            return NULL;
        }

        LdapConnAlloc++;
        if ( LdapConnAlloc > LdapConnMaxAlloc ) {
            LdapConnMaxAlloc = LdapConnAlloc;
        }

        IF_DEBUG(CONN) {
            DPRINT1(0,"CONN %x alloc from heap\n", pConn);
        }

    }

    if(!fUDP) {

        oldCount = InterlockedExchangeAdd(&CurrentConnections, 1);

        if ( ((DWORD)oldCount >= LdapMaxConnections) && 
             !fBypassLimitsChecks ) {

            //
            // Too many connections.  Find one to get rid of.
            //

            FindAndDisconnectConnection();

            gdwOutofConnections++;

            // schedule log of out of connections event.
            InsertInTaskQueueDamped( TQ_DelayLogOutofConnections,
				&gdwOutofConnections,
				FIVE_MINS,
				FIVE_MINS,
				TaskQueueNameMatched,
				NULL );

            IF_DEBUG(WARNING) {
                DPRINT2(0,"Too many connections %d [max %d]\n",
                    oldCount+1, LdapMaxConnections);
            }
        }

        //
        //  Increment the count of connected users
        //

        INC(pcLDAPClients);
        PERFINC(pcLdapNewConnsPerSec);
        DPRINT1(VERBOSE, "CurrentConnections = %u\n",
                CurrentConnections);

        //
        // Update the current maximum connections
        //

        if ( CurrentConnections > MaxCurrentConnections) {
            MaxCurrentConnections = CurrentConnections;
        }

    } else {
        PERFINC(pcLDAPUDPClientOpsPerSecond);
        UncountedConnections++;
        DPRINT1(VERBOSE, "UncountedConnections = %u\n",
                UncountedConnections);

    }

    pConn->m_signature = SIGN_LDAP_CONN;
    pConn->m_RefCount = 1;

    ACQUIRE_LOCK(&csConnectionsListLock);

    //
    // Insert into the list of connected users.
    //

    InsertTailList(
        &ActiveConnectionsList,
        &pConn->m_listEntry
        );

    pConn->m_State = BlockStateActive;
    pConn->m_ClientNumber = GetPLS()->LdapClientID;
    GetPLS()->LdapClientID += MAXIMUM_PROCESSORS;

    RELEASE_LOCK(&csConnectionsListLock);

    InterlockedIncrement(&ActiveLdapConnObjects);
    return pConn;

} // LDAP_CONN::Alloc


BOOL
LDAP_CONN::GetSslContextAttributes(
                         VOID
                         )
/*++

Routine Description:

    This function queries for the cipher strength of a connection.

Arguments:

    None.

Return Value:

    None.

--*/
{

    SECURITY_STATUS ret;
    SecPkgContext_ConnectionInfo connInfo;

    ret = QueryContextAttributes(
                            &m_hSslSecurityContext,
                            SECPKG_ATTR_CONNECTION_INFO,
                            &connInfo
                            );

    if ( ret == ERROR_SUCCESS ) {

        m_cipherStrength = connInfo.dwCipherStrength;
        IF_DEBUG(SSL) {
            DPRINT1(0,"Cipher Strength set to %d\n",connInfo.dwCipherStrength);
        }
    } else {

        DPRINT1(0,"Cannot query cipher strength. err %x\n", ret);
        goto error_exit;
    }

    ret = QueryContextAttributes(
                             &m_hSslSecurityContext,
                             SECPKG_ATTR_STREAM_SIZES,
                             &m_SslStreamSizes
                             );

    if ( ret == ERROR_SUCCESS ) {

        IF_DEBUG(SSL) {
            DPRINT5(0,"SSL Stream: Head %d Trail %d MaxMsg %d Buffer %d Block %d\n",
                    m_SslStreamSizes.cbHeader,
                    m_SslStreamSizes.cbTrailer,
                    m_SslStreamSizes.cbMaximumMessage,
                    m_SslStreamSizes.cBuffers,
                    m_SslStreamSizes.cbBlockSize);
        }

        //  Avoid off by one error in schanlsa.dll
        //  which manifests itself as internal error return when
        //  doing a deap search with 8000 bytes of results. Use -2
        //  to keep some sort of alignment.  RRandall 2/14/00 - I don't know
        //  if this is still necessary, but I do know that it works.
        //  Rather than go through a long testing process it might as
        //  well stay.
        m_SslStreamSizes.cbMaximumMessage -= 2;

        m_SslStreamSizes.cbMaximumMessage -=
            (m_SslStreamSizes.cbHeader + m_SslStreamSizes.cbTrailer);

    } else {

        DPRINT1(0,"Cannot query stream sizes. err %x\n", ret);
    }

error_exit:
    return (ret == NO_ERROR);

} // LDAP_CONN::GetSslContextAttributes



SECURITY_STATUS
LDAP_CONN::GetSslClientCertToken(
                         PCHAR pchAuthzId
                         )
/*++

Routine Description:

    This function queries for the cipher strength of a connection.

Arguments:

    None.

Return Value:

    None.

--*/
{

    SECURITY_STATUS ret;
    HANDLE          token;


    if (m_pPartialSecContext) {
        m_pPartialSecContext->DereferenceSecurity();
    }

    m_pPartialSecContext = new LDAP_SECURITY_CONTEXT(FALSE);
    if ( m_pPartialSecContext == NULL ) {
        IF_DEBUG(NOMEM) {
            DPRINT(0,"Cannot allocate LDAP security context\n");
        }
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    ret = m_pPartialSecContext->GrabSslContext(&m_hSslSecurityContext, pchAuthzId);

    if (SEC_E_OK != ret) {
        m_pPartialSecContext->DereferenceSecurity();
        m_pPartialSecContext = NULL;
    }

    return ret;

} // LDAP_CONN::GetSslClientCertToken


BERVAL *
GenDisconnectNotify(
        IN _enum1           Errcode,
        IN LDAPString       *ErrorMessage
        )
/*++

Routine Description:

    This function constructs a disconnect notification.
        
Arguments:

    ErrCode - error to return to client.
    ErrorMessage - The error message to send back with the notification.
    
Return Value:

    On successful completion returns a pointer to a BERVAL, otherwise returns
    NULL.
    
--*/
{
    LDAPString * ldapText;
    BerElement* berElement;
    INT rc;
    BERVAL * bval = NULL;

    IF_DEBUG(ERROR) {
        PCHAR  errMsg;
        DPRINT2(0,"GenDisconnectNotify entered. code = %d\n\t%s\n", Errcode, ErrorMessage->value);
        errMsg = (PCHAR) LdapAlloc(ErrorMessage->length + 1);
        if (errMsg) {
            memcpy(errMsg, ErrorMessage->value, ErrorMessage->length);
            errMsg[ErrorMessage->length] = '\0';
            DPRINT1(0,"    %s\n",errMsg);
            LdapFree(errMsg);
        }
    }

    berElement = ber_alloc_t(LBER_USE_DER);
    if ( berElement == NULL ) {
        IF_DEBUG(WARNING) {
            DPRINT(0,"ber_alloc_t failed\n");
        }
        return NULL;
    }

    rc = ber_printf(berElement,"{it{tioo}to}",
                    0x0, LDAP_RES_EXTENDED,
                    0x0A, Errcode,
                    "", 0,
                    ErrorMessage->value, ErrorMessage->length,
                    0x8A, DisconnectNotifyName.value, DisconnectNotifyName.length);

    if ( rc != NO_ERROR ) {
        IF_DEBUG(WARNING) {
            DPRINT(0,"ber_printf failed\n");
        }
        ber_free(berElement,1);
        return NULL;
    }

    rc = ber_flatten(berElement, &bval);
    ber_free(berElement,1);
    if ( rc != NO_ERROR ) {
        IF_DEBUG(WARNING) {
            DPRINT(0,"ber_flatten failed\n");
        }
        return NULL;
    }
    if (bval->bv_len == 0) {
        //
        // Something else went wrong.
        //
        IF_DEBUG(WARNING) {
            DPRINT(0,"ber_flatten returned a zero length bval\n");
        }
        ber_bvfree(bval);
        return NULL;
    }

    return bval;
}


VOID
LDAP_CONN::LdapDisconnectNotify(
    IN _enum1           Errcode,
    IN LDAPString       *ErrorMessage
    )
/*++
Routine Description:

    Sends a Notice of Disconnection for this connection, but only if the connection
    hasn't been closed already.         

Arguments:

    Errcode - the error code to be passed on to the disconnect notify function.
    
    ErrorMessage - the error message to be passed on. 

Return Value:

    N/A
    
--*/
{
    DWORD          dwErr;
    PLDAP_REQUEST  pRequest = NULL;
    BERVAL         *bval;
    PUCHAR         pBuff = NULL;

    if (!IsBound()) {
        //
        // Don't waste resources on unbound clients.
        //
        return;
    }
    //
    // If the connection is already closed don't bother generating the
    // notification.
    //
    if (BlockStateClosed == m_State || ((m_fSSL || m_fTLS) && (m_SslState != Sslbound))) {
        //
        // Either the connection is closed, or this is an SSL connection but
        // the SSL negotiations haven't completed yet.  In either case we can't
        // send a disconnect notify.
        //
        return;
    }

    DPRINT(1, "LDAP disconnect notify entered!\n");
    
    // Now, get a request object
    pRequest = LDAP_REQUEST::Alloc(m_atqContext,this);
    if (pRequest == NULL) {
        IF_DEBUG(NOMEM) {
            DPRINT(0,"Unable to allocate request to send notification\n");
        }
        return;
    }

    bval = GenDisconnectNotify(Errcode, ErrorMessage);
    if (!bval) {
        //
        // Since we are disconnecting any way, just give up.
        //
        DereferenceAndKillRequest(pRequest);
        return;
    }

    if (bval->bv_len > pRequest->GetSendBufferSize()) {
        if(!pRequest->GrowSend(bval->bv_len)) {
            DereferenceAndKillRequest(pRequest);
            return;
        }
    }

    pBuff = pRequest->GetSendBuffer();

    CopyMemory(pBuff, bval->bv_val, bval->bv_len);

    pRequest->SetBufferPtr(pBuff + bval->bv_len);

    //
    // don't need this anymore.
    //
    ber_bvfree(bval);


    //
    // Finally send the message making sure that the connection is still up.
    //
    if ( m_State != BlockStateClosed ) {
        DPRINT(2, "Sending notify.\n");
        pRequest->SyncSend(&m_hSslSecurityContext);
    }

    DereferenceAndKillRequest(pRequest);
    return;
}



BOOL
LDAP_CONN::LdapRequestLogAndTraceEventStart(
    VOID
    )
/*++
Routine Description:

    This function starts an ldap request trace event.  The following arguments 
    are passed to the capacity planning event:
    
         The kind of encryption active on this connection.
         The remote IP address.
         Whether this is a TCP or UDP connection.
         

Arguments:

    N/A

Return Value:

    Bool indicating if the event is on (TRUE) or off (FALSE)

--*/
{
    PCHAR  pszEncryptType;

    if (IsSSLOrTLS()) {
        pszEncryptType = "SSL/TLS";
    } else if (IsSignSeal()) {
        pszEncryptType = "Sign/Seal";
    } else {
        pszEncryptType = "None";
    }

    LogAndTraceEvent(FALSE,
                     DS_EVENT_CAT_DIRECTORY_ACCESS,
                     DS_EVENT_SEV_VERBOSE,
                     DIRLOG_BEGIN_LDAP_REQUEST,
                     EVENT_TRACE_TYPE_START,
                     DsGuidLdapRequest,
                     szInsertSz(pszEncryptType),
                     szInsertSz(m_RemoteSocketString),
                     m_fUDP ? szInsertSz("UDP") : szInsertSz("TCP"),
                     NULL, NULL, NULL, NULL, NULL);
    
    return TRUE;

} // LdapRequestLogAndTraceEventStart


BOOL
LDAP_CONN::LdapRequestLogAndTraceEventEnd(
    IN ULONG        ulExitID,
    IN ULONG        code,
    IN RootDseFlags rootDseSearchFlag,
    IN ULONG        requestType
    )
/*++
Routine Description:

    This function ends a trace event for an ldap request.

Arguments:

    ulExitID - This number indicates how the request was completed.
        1 - The request was abandoned, or it was a notification registration.
        2 - The request was actually an abandon request.
        3 - The request completed normally.
        4 - The request was not completed due to an error.
        5 - The request was not started because more data was needed to complete
            decryption.
        6 - The request was not started because an entire unencrypted request
            has not been received yet.
        
    code -     The LDAPResult that was returned to the client.
    
    rootDseSearchFlag - Indicates whether the request was a rootDSE search, 
                          an LDAP ping, or some other operation.
                          
    requestType - The type of request that was performed, i.e. search, add, modify

Return Value:

    Bool indicating if the event is on (TRUE) or off (FALSE)

--*/
{
    PCHAR  pcDseFlagString;

    switch (rootDseSearchFlag) {
    case rdseNonDse:
        pcDseFlagString = "NonDSE";
        break;
    case rdseDseSearch:
        pcDseFlagString = "RootDSE";
        break;
    case rdseLdapPing:
        pcDseFlagString = "LDAPPing";
        break;
    default:
        Assert(FALSE);
        return FALSE;
    }

    LogAndTraceEvent(FALSE,
                     DS_EVENT_CAT_DIRECTORY_ACCESS,
                     DS_EVENT_SEV_VERBOSE,
                     DIRLOG_END_LDAP_REQUEST,
                     EVENT_TRACE_TYPE_END,
                     DsGuidLdapRequest,
                     szInsertUL(ulExitID),
                     szInsertUL(code),
                     szInsertSz(pcDseFlagString),
                     szInsertLdapErrMsg(code),
                     szInsertUL(requestType),
                     NULL, NULL, NULL);
    
    return FALSE;

} // LdapRequestLogAndTraceEventEnd


VOID
LDAP_CONN::SetClientAccessType()
/*++

Routine Description:

    This function check the identity of the client to see whether they are 
    an authenticated user or not, and whether they are an admin.  This info
    is used later to efficiently determine what access to grant the client
    and to find connections to disconnect in the face of a connection flood.
    
Arguments:

    pTHS  -  Pointer to the current thread state.

Return Value:

    None.

--*/
{
    DWORD                       dwError;

    m_bIsAdmin = FALSE;
    m_bIsAuthenticated = FALSE;

    if (NULL == m_pSecurityContext) {
        return; // nope
    }

    m_bIsAdmin = m_pSecurityContext->IsAdmin();
    m_bIsAuthenticated = m_pSecurityContext->IsAuthUser();

    if (m_bIsAuthenticated) {
        m_cbUnAckedSendDataLimit = LDAP_MAX_CONN_UNACKED_SEND_DATA;
        m_cUnAckedSendsLimit = LDAP_MAX_CONN_NUM_UNACKED_SENDS;
    }
}


VOID
LDAP_CONN::AbandonAllRequests(
                          VOID
                         )
/*++

Routine Description:

    Abaondons all outstanding request on the connection.  
    
    NOTE: 
        Must be called with the connection locked!

Arguments:

    None.
    
Return Value:

    None.

--*/
{
    LDAP_REQUEST        *pRequest;
    PLIST_ENTRY         listEntry;
    PLDAP_NOTIFICATION  p1, p2;
    NOTIFYRES           *pNotifyRes;

    // First mark all the requests as abandoned.

    for ( listEntry = m_requestList.Flink;
         listEntry != &m_requestList;
         listEntry = listEntry->Flink
             ) {

        pRequest = CONTAINING_RECORD(listEntry,
                                     LDAP_REQUEST,
                                     m_listEntry);

        // Don't pre-abandon requests.  The request
        // that hasn't received any data yet
        // is ok to leave alone.
        if (pRequest != m_request) {        
            pRequest->m_fAbandoned = TRUE;
        }
    }

    // Then unregister any notifications that are still
    // outstanding.
    while (m_countNotifies) {        
        p1 = m_Notifications;
        DirNotifyUnRegister( m_Notifications->hServer, &pNotifyRes);
        m_Notifications = m_Notifications->pNext;
        LdapFree(p1);
        m_countNotifies--;
    }
    Assert(!m_Notifications);

    IF_DEBUG(CONN) {
        DPRINT1(0, "Abondoning all requests on conn %p\n", this);
    }
    return;
}


VOID
LDAP_CONN::FindAndDisconnectConnection(
                      VOID
                     )
/*++

Routine Description:

    This function applies the following algorithm to find a connection to kill.
    
    Examine the first 50 non-UDP connections at the head of the connection list, (These
    should be the connections that haven't had any activity for the longest time).  Score
    them according to the following.
    
        - if the security context handle is NULL, give it 8 points, else 0 points.
        - if the lastRequestFinishTime is greater than the lastRequestStartTime, and more
          than 15 seconds ago, add 4 points
        - if the lastRequestFinishTime is greater than the lastRequestStartTime, add 2 points.
        - if the entry is not for an admin, give it 1 point.
        
    Kill the connection with the highest score.

Arguments:

    None.

Return Value:

    None.

--*/
{
    PLDAP_CONN   pConn;
    PLDAP_CONN   pBestCandidateConnection = NULL;
    DWORD        dwCurrentScore;
    DWORD        dwBestScore              = 0;
    LONG         lTimeDiff;
    LIST_ENTRY   *pListEntry;

    ACQUIRE_LOCK(&csConnectionsListLock);
    
    pListEntry = ActiveConnectionsList.Flink;
    for (int count=0; count < 50 && pListEntry != &ActiveConnectionsList; count++) {
        pConn = (PLDAP_CONN) CONTAINING_RECORD(pListEntry, LDAP_CONN, m_listEntry);

        dwCurrentScore = 0;

        if (pConn->m_fUDP) {
            // Don't count UDP connections.
            count--;
        } else {
        
            // Score the connection.
            if (NULL == pConn->m_pSecurityContext) {
                dwCurrentScore += 8;
            }
            if (!pConn->m_bIsAdmin) {
                dwCurrentScore += 1;
            }

            lTimeDiff = pConn->m_lastRequestFinishTime
                - pConn->m_lastRequestFinishTime;

            if (lTimeDiff < 0) {
                // Check for rollover.  If the time difference
                // is greater than half the longest time represented
                // by a tick count, then consider it rolled over.
                if (-lTimeDiff > ((~0L) >> 2) ) {
                    // This must be the rollover case, so 
                    // the last request finished already.
                    dwCurrentScore += 2;

                    // Was the last request finished more than
                    // 15 seconds ago.
                    if (-lTimeDiff > 15000) {
                        dwCurrentScore += 4;
                    }
                }
            } else {
                // Check for rollover.
                if (lTimeDiff < ((~0L) >> 2) ) {
                    // No rollover, so the last request
                    // really is finished.
                    dwCurrentScore += 2;

                    // Was the last request finished more than
                    // 15 seconds ago?
                    if (lTimeDiff > 15000) {
                        dwCurrentScore += 4;
                    }
                }
            }

            // Is this the largest score we've seen so far?
            if (dwCurrentScore > dwBestScore) {
                dwBestScore = dwCurrentScore;
                pBestCandidateConnection = pConn;
            } else if (NULL == pBestCandidateConnection) {
                pBestCandidateConnection = pConn;
            }
        }

        // Go on to the next connection.
        pListEntry = pListEntry->Flink;
    }

    Assert(NULL != pBestCandidateConnection);

    // Reference the connection so that it can't disappear before
    // we can disconnect it.
    pBestCandidateConnection->ReferenceConnection();

    RELEASE_LOCK(&csConnectionsListLock);

    pBestCandidateConnection->Disconnect();

    IF_DEBUG(WARNING) {    
        DPRINT(0, "DISCONNECT DUE TO TOO MANY CONNECTIONS");
    }
    

    pBestCandidateConnection->DereferenceConnection();

    return;
}


VOID
LDAP_CONN::PossiblyRecoverSendQueue( VOID )
/*++

Routine Description:

    Checks to see whether the global limit of unacknowledged sends, or the
    total limit of sent data that remains unacknowledged has been reached.
    If the limit has been reached then a fixed number of non-admin connections
    are dropped in an effort to recover send queue space.  Because there are
    other threads behind this one, this will continue to happen until enough
    send queue has been recovered.
            
Return Value:

    None.

--*/
{
    LIST_ENTRY   *pListEntry;
    DWORD        count;
    PLDAP_CONN   pDisconnectList[LDAP_SEND_QUEUE_RECOVER_NUM];
    PLDAP_CONN   pConn;

    if (g_cbUnAckedSendData < LDAP_MAX_GLOBAL_UNACKED_SEND_DATA &&
        g_cUnAckedSends < LDAP_MAX_GLOBAL_NUM_UNACKED_SENDS) {
        return;
    }

    EnterCriticalSection(&LdapSendQueueRecoveryLock);
    ACQUIRE_LOCK(&csConnectionsListLock);
    
    //
    // Recheck that we still need to do this now that we've got the locks.
    //
    if (g_cbUnAckedSendData < LDAP_MAX_GLOBAL_UNACKED_SEND_DATA &&
        g_cUnAckedSends < LDAP_MAX_GLOBAL_NUM_UNACKED_SENDS) {

        RELEASE_LOCK(&csConnectionsListLock);
        LeaveCriticalSection(&LdapSendQueueRecoveryLock);
        return;
    }

    DPRINT(0, "Global send queue is full, disconnecting non admin connections.\n");

    pListEntry = ActiveConnectionsList.Flink;
    
    //
    // Find some non-admin connections to disconnect.
    //
    count = 0;
    while (count < LDAP_SEND_QUEUE_RECOVER_NUM &&
           pListEntry != &ActiveConnectionsList) {

        pConn = (PLDAP_CONN) CONTAINING_RECORD(pListEntry, LDAP_CONN, m_listEntry);

        if (!pConn->IsAdmin()) {
            //
            // Found one to disconnect save the pointer and
            // reference the connection so that it can't disappear before
            // we can disconnect it.
            //
            pConn->ReferenceConnection();
            pDisconnectList[count] = pConn;
            count++;
        }

        // Go on to the next connection.
        pListEntry = pListEntry->Flink;
    }

    RELEASE_LOCK(&csConnectionsListLock);

    ulSendQueueRecoveryClosedConns += count;
    //
    // Now disconnect them.
    //
    while (count) {
        (pDisconnectList[count-1])->Disconnect();
        (pDisconnectList[count-1])->DereferenceConnection();
        count--;
    }

    LeaveCriticalSection(&LdapSendQueueRecoveryLock);
    
    // schedule log of send queue event.
    InsertInTaskQueueDamped( TQ_DelayLogOutofSendQueue,
                        &ulSendQueueRecoveryClosedConns,
                        FIVE_MINS,
                        FIVE_MINS,
                        TaskQueueNameMatched,
                        NULL );

    return;
}


VOID
LDAP_CONN::SetNetBufOpts(
                      LPLDAP_SECURITY_CONTEXT pSecurityContext OPTIONAL
                     )
/*++

Routine Description:

    This function sets the network buffer options according to whether and what 
    kind of encryption is currently active on the connection.  If it's possible
    that the connection may require a header or trailer, then an 
    LDAP_SECURITY_CONTEXT MUST be passed in.
        
Arguments:

    pSecurityContext - May be NULL if the connection will definitely not require
                       a header or trailer.

Return Value:

    None.

--*/
{
    PSecPkgContext_Sizes sizes;
    
    if (IsSSLOrTLS()) {
        m_fCanScatterGather = FALSE;
        m_fNeedsHeader = FALSE;
        m_fNeedsTrailer = FALSE;
    } else if (IsSignSeal()) {
        Assert(NULL != pSecurityContext);
        m_fNeedsHeader = TRUE;
        m_fNeedsTrailer = TRUE;
        m_fCanScatterGather = TRUE;
    } else {
        m_fCanScatterGather = TRUE;
        m_fNeedsHeader = FALSE;
        m_fNeedsTrailer = FALSE;
    }

    if (IsSignSeal()) {
        sizes = pSecurityContext->GetContextSizes();

        //
        // The following lines come straight from the security group.  It's a
        // strange and wonderful land where you use the trailersize as the header 
        // size.
        //

        //
        // Need to save space for the SASL header in front of the
        // encryption header.
        //
        m_HeaderSize = sizes->cbSecurityTrailer + sizeof(DWORD);
        m_TrailerSize = sizes->cbBlockSize + sizes->cbMaxSignature;

        m_MaxEncryptSize = pSecurityContext->GetMaxEncryptSize();
        if (!m_MaxEncryptSize) {
            m_MaxEncryptSize = MAXDWORD;
        }

    } else {
        m_HeaderSize = 0;
        m_TrailerSize = 0;
    }

    return;
}


BOOL
LDAP_CONN::IsSendQueueFull( VOID )
/*++

Routine Description:

    Checks whether the client is falling behind receiving the data
    we are sending.
            
Return Value:

    TRUE if the client has fallen behind.  Otherwise FALSE.

--*/
{
    if (m_fUDP) {
        // This check doesn't apply to UDP connections.
        return FALSE;
    }
    
    if (m_cbUnAckedSendData >= LDAP_UNACKED_SEND_DATA_WARN ||
        m_cUnAckedSends >= LDAP_NUM_UNACKED_SENDS_WARN) {
        //
        // The client is falling behind receiving the data
        // we are sending.
        //
        return TRUE;
    }
    return FALSE;
}


BOOL
LDAP_CONN::IsClientTooFarBehind( VOID )
/*++

Routine Description:

    Checks whether the client has fallen far enough behind on receiving that
    it should be disconnected.  This can happen if it has sent too many requests
    since we initially noticed that they fell behind, or if the send queue is
    simply too full.
                
Return Value:

    TRUE if the client should be disconnected.  Otherwise FALSE.

--*/
{
    if (m_fUDP) {
        // This check doesn't apply to UDP connections.
        return FALSE;
    }
    
    return 
        ((m_cUnAckedSends > m_cUnAckedSendsLimit)
         || (m_cbUnAckedSendData >= m_cbUnAckedSendDataLimit));
}

VOID
LDAP_CONN::ZapSecurityContext( BOOL fZapPartial )
/*++

Routine Description:

    Clears security context related info from the LDAP_CONN.
                    
--*/
{

    if (fZapPartial && m_pPartialSecContext) {
        m_pPartialSecContext->DereferenceSecurity();
        m_pPartialSecContext = NULL;
    }
    if (m_pSecurityContext) {
        m_pSecurityContext->DereferenceSecurity();
        m_pSecurityContext = NULL;
    }
    m_softExpiry.QuadPart = MAXLONGLONG;
    m_hardExpiry.QuadPart = MAXLONGLONG;
    m_fUsingSSLCreds = FALSE;
    m_fSign          = FALSE;
    m_fSeal          = FALSE;
    m_fSimple = FALSE;
    m_fGssApi = FALSE;
    m_fSpNego = FALSE;
    m_fDigest = FALSE;
    m_bIsAdmin = FALSE;
    m_bIsAuthenticated = FALSE;
    if (m_fUserNameAlloc) {
        LdapFree(m_userName);
        m_fUserNameAlloc = FALSE;
    }
    m_userName = NULL;
    m_cbUnAckedSendDataLimit = LDAP_MAX_UNAUTH_UNACKED_SEND_DATA;
    m_cUnAckedSendsLimit    = LDAP_MAX_UNAUTH_UNACKED_SENDS;
}



extern "C"
DWORD
LdapEnumConnections(
    IN THSTATE *pTHS,
    IN PDWORD Count,
    IN PVOID *Buffer
    )
/*++

Routine Description:

    This function enumerates all outstanding connections

Arguments:

    pTHS - thread state
    Count - On return, number of array elements returned.
    Buffer - On return, an array of DS_DOMAIN_CONTROLLER_INFO_FFFFFFFF

Return Value:

    if successful, ERROR_SUCCESS
    otherwise, Win32 error code

--*/
{
    PLIST_ENTRY pEntry;
    PLDAP_CONN pConn;
    DWORD nConn = 0;
    DWORD err = ERROR_SUCCESS;
    PDS_DOMAIN_CONTROLLER_INFO_FFFFFFFFW ldapConn;
    FILETIME currTime;
    LARGE_INTEGER liCurrent; 
    LARGE_INTEGER liConnect;

    GetSystemTimeAsFileTime(&currTime);
    liCurrent.LowPart = currTime.dwLowDateTime;
    liCurrent.HighPart = currTime.dwHighDateTime;

    ACQUIRE_LOCK( &csConnectionsListLock );
    
    __try {

        //
        // First, count the number of entries
        //

        pEntry = ActiveConnectionsList.Flink;
        while ( pEntry != &ActiveConnectionsList ) {
            nConn++;
            pEntry = pEntry->Flink;
        }
        
        //
        // allocate the buffer to be returned
        //

        ldapConn = (PDS_DOMAIN_CONTROLLER_INFO_FFFFFFFFW)
            THAllocEx(pTHS, nConn * sizeof(DS_DOMAIN_CONTROLLER_INFO_FFFFFFFFW));

        if ( ldapConn == NULL ) {
            DPRINT(0,"Unable to allocate buffer to connection enum\n");
            err = ERROR_NOT_ENOUGH_MEMORY;
            goto exit;
        }

        //
        // fill in the return buffer
        // 

        *Count = nConn;
        nConn = 0;
        pEntry = ActiveConnectionsList.Flink;

        while ( pEntry != &ActiveConnectionsList ) {

            PSOCKADDR_IN addr;
            DWORD flags;

            pConn = CONTAINING_RECORD(pEntry,
                                      LDAP_CONN,
                                      m_listEntry);

            addr = (PSOCKADDR_IN)&pConn->m_RemoteSocket;

            ldapConn[nConn].IPAddress = addr->sin_addr.s_addr;
            ldapConn[nConn].NotificationCount = pConn->m_countNotifies;
            ldapConn[nConn].TotalRequests = pConn->m_nTotalRequests;
            if ( pConn->m_userName != NULL ) {
                ldapConn[nConn].UserName = (PWCHAR)
                    THAllocEx(pTHS,
                              (wcslen(pConn->m_userName)+1) * sizeof(WCHAR));

                wcscpy(ldapConn[nConn].UserName, pConn->m_userName);

            } else {
                ldapConn[nConn].UserName = NULL;
            }

            //
            // compute the total time this guy has been connected
            //

            liConnect.LowPart = pConn->m_connectTime.dwLowDateTime;
            liConnect.HighPart = pConn->m_connectTime.dwHighDateTime;

            ldapConn[nConn].secTimeConnected = (DWORD)(
                ((liCurrent.QuadPart - liConnect.QuadPart) / 
                 (ULONGLONG)(10 * 1000 * 1000)));

            //
            // Set the correct flags
            //

            flags = 0;
            if ( pConn->m_pSecurityContext != NULL ) {
                flags |= LDAP_CONN_FLAG_BOUND;
            }

            if ( pConn->m_fSSL ) {
                flags |= LDAP_CONN_FLAG_SSL;
            }

            if ( pConn->m_fUDP ) {
                flags |= LDAP_CONN_FLAG_UDP;
            }

            if ( pConn->m_fGC ) {
                flags |= LDAP_CONN_FLAG_GC;
            }

            if ( pConn->m_fSign ) {
                flags |= LDAP_CONN_FLAG_SIGN;
            }

            if ( pConn->m_fSeal ) {
                flags |= LDAP_CONN_FLAG_SEAL;
            }

            if ( pConn->m_fSimple ) {
                flags |= LDAP_CONN_FLAG_SIMPLE;
            }

            if ( pConn->m_fGssApi ) {
                flags |= LDAP_CONN_FLAG_GSSAPI;
            }

            if ( pConn->m_fSpNego ) {
                flags |= LDAP_CONN_FLAG_SPNEGO;
            }

            if ( pConn->m_fDigest ) {
                flags |= LDAP_CONN_FLAG_DIGEST;
            }

            ldapConn[nConn].Flags = flags;

            pEntry = pEntry->Flink;
            nConn++;
        }

        *Buffer = (PVOID)ldapConn;
        Assert(nConn == *Count);
exit:;
    }__except(HandleAllExceptions(GetExceptionCode())) {
        err = GetExceptionCode();
    }

    RELEASE_LOCK( &csConnectionsListLock );
    return err;
} // LdapEnumConnections


//	Log out of connections.
extern "C"
void DelayLogOutofConnections( IN VOID * pv, IN VOID ** ppvNext, OUT DWORD *pcSecsUntilNextIteration )
{
	*pcSecsUntilNextIteration = TASKQ_DONT_RESCHEDULE;

	//	log event to show number of out of connection instances in last five minutes
	//
	LogEvent( DS_EVENT_CAT_SERVICE_CONTROL,
		DS_EVENT_SEV_BASIC,
		DIRLOG_OUT_OF_LDAP_CONNECTIONS,
		szInsertUL(*(ULONG *)pv), 
		NULL,
		NULL );

	//	reset number of instances of out of connection to 0
	//
	*(ULONG *)pv = 0;
	
	return;
}


extern "C"
VOID
DelayLogOutofSendQueue(
    IN VOID   *pv,
    IN VOID   **ppvNext,
    OUT DWORD *pcSecsUntilNextIteration )
{
	*pcSecsUntilNextIteration = TASKQ_DONT_RESCHEDULE;

	//	log event to show number of out of connection instances in last five minutes
	//
        LogEvent(DS_EVENT_CAT_LDAP_INTERFACE,
                 DS_EVENT_SEV_ALWAYS,
                 DIRLOG_LDAP_SEND_QUEUE_FULL,
                 szInsertUL(*(ULONG *)pv),
                 NULL, NULL);

	//	reset number of instances of out of connection to 0
	//
	*(ULONG *)pv = 0;
	
	return;
}

