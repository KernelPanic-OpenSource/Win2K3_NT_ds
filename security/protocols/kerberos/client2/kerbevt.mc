;/*++
;
;Copyright (c) 1998  Microsoft Corporation
;
;Module Name:
;
;    kerbevt.h
;
;Abstract:
;
;    Definitions for Kerberos SSP events
;
;Author:
;
;    MikeSw, 5-Oct-98
;
;Revision History:
;
;Notes:
;
;    This file is generated by the MC tool from the kerbevt.mc file.
;
;--*/
;
;
;#ifndef __KERBEVT_H__
;#define __KERBEVT_H__
;


SeverityNames=(Success=0x0:STATUS_SEVERITY_SUCCESS
               Informational=0x1:STATUS_SEVERITY_INFORMATIONAL
               Warning=0x2:STATUS_SEVERITY_WARNING
               Error=0x3:STATUS_SEVERITY_ERROR
              )


MessageId= Severity=Success SymbolicName=CATEGORY_KERBEROS
Language=English
Kerberos
.

MessageId= Severity=Success SymbolicName=CATEGORY_MAX_CATEGORY
Language=English
Max
.

;//
;// Kerberos Error Message
;//

MessageId= Severity=Warning SymbolicName=KERBEVT_KERB_ERROR_MSG
Language=English
A Kerberos Error Message was received:%n
        on logon session %1%n
Client Time: %2%n
Server Time: %3%n
Error Code: %4 %5%n
Extended Error: %6%n
Client Realm: %7%n
Client Name: %8%n
Server Realm: %9%n
Server Name: %10%n
Target Name: %11%n
Error Text: %12%n
File: %13%n
Line: %14%n
Error Data is in record data.
.

;//
;// Ap error modified warning
;//

MessageId= Severity=Informational SymbolicName=KERBEVT_KRB_AP_ERR_MODIFIED
Language=English
The kerberos client received a KRB_AP_ERR_MODIFIED error from the
server %1.  The target name used was %3. This
indicates that the password used to encrypt the kerberos service ticket
is different than that on the target server. Commonly, this is due to identically named 
machine accounts in the target realm (%2), and the client realm.  
Please contact your system administrator.
.

;//
;// Ticket not yet valid warning
;//

MessageId= Severity=Informational SymbolicName=KERBEVT_KRB_AP_ERR_TKT_NYV
Language=English
The kerberos client received a KRB_AP_ERR_TKT_NYV error from the
server %1.  This indicates that the ticket used against that server is not yet
valid (in relationship to that server time).  Contact your system administrator 
to make sure the client and server times are in sync, and that the KDC in realm %2 is 
in sync with the KDC in the client realm.
.

;//
;// Token too big warning
;//

MessageId= Severity=Warning SymbolicName=KERBEVT_INSUFFICIENT_TOKEN_SIZE
Language=English
The kerberos SSPI package generated an output token of size %1 bytes, which was too
large to fit in the %2 buffer provided by process id %3.  If the condition persists,
please contact your system administrator.
.

;//
;// PAC signature failed to verify
;//

MessageId= Severity=Error SymbolicName=KERBEVT_KRB_PAC_VERIFICATION_FAILURE
Language=English
The kerberos subsystem encountered a PAC verification failure. 
This indicates that the PAC from the client %1 in realm %2 had a PAC which failed to
verify or was modified.  Contact your system administrator.
.

;//
;// Client certificate is garbage, according to KDC
;//

MessageId= Severity=Error SymbolicName=KERBEVT_BAD_CLIENT_CERTIFICATE
Language=English
The Domain Controller rejected the client certificate used for smartcard logon. 
The following error was returned from the certificate validation process: 
%1. Contact your system administrator to determine why
your smartcard logon certificate is invalid.
.


;//
;// Kdc certificate is garbage, according to the client
;//

MessageId= Severity=Error SymbolicName=KERBEVT_BAD_KDC_CERTIFICATE
Language=English
The client has failed to validate the Domain Controller certificate for 
%2. The following error was returned from the certificate 
validation process: %1.  Contact your system administrator to 
determine why the Domain Controller certificate is invalid.
.

;//
;// UDP transmission problems?
;//

MessageId= Severity=Warning SymbolicName=KERBEVT_UDP_TIMEOUT
Language=English
The kerberos subsystem is having problems fetching tickets from
your domain controller using the UDP network protocol.  This is 
typically due to network problems.  Please contact your system 
administrator.
.

;//
;// Missing naming information in certificate
;//

MessageId= Severity=Error SymbolicName=KERBEVT_NO_RDN
Language=English
The Distinguished Name in the subject field of your smartcard logon 
certificate does not contain enough information to locate the appropriate
domain on an unjoined machine.  Please contact your system administrator. 
.

;//
;// Encountered a card error on a *Session cred
;//

MessageId= Severity=Warning SymbolicName=KERBEVT_RAS_CARD_ERROR
Language=English
While using your smartcard over a VPN connection, the Kerberos subsystem 
encountered an error.  Typically, this indicates the card has been pulled
from the reader during the VPN session.  One possible solution is to close
the VPN connection, reinsert the card, and restablish the connection.
.

;//
;// Encountered a card error
;//
MessageId= Severity=Warning SymbolicName=KERBEVT_CREDMAN_CARD_ERROR
Language=English
While using your smartcard for the Credential Manager the Kerberos 
subsystem encountered an error that appears to be from a 
missing or incorrect smartcard PIN.  To remedy, launch the Stored User Names and Passwords
control panel applet, and reenter the pin for the credential for %1%2%3.
.
   
;//
;// Encountered a password error
;//
MessageId= Severity=Warning SymbolicName=KERBEVT_CREDMAN_PWD_ERROR
Language=English
There were password errors using the Credential Manager.
To remedy, launch the Stored User Names and Passwords control panel
applet, and reenter the password for the credential %1%2%3.
.
  
  
;
;#endif // __KERBEVT_H_
;