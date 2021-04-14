;/*++ BUILD Version: 0001    // Increment this if a change has global effects
;
;Copyright (c) 1992  Microsoft Corporation
;
;Module Name:
;
;    gpdasevt.h
;
;Abstract:
;
;    Definitions for Rsop Planning mode errors
;
;Author:
;
;    UShaji  (Adapted from userenv\uevents.mc)
;
;Revision History:
;
;Notes:
;
;    This file is generated by the MC tool from the gpdasevt.mc file.
;
;--*/
;
;
;#ifndef _GPDAS_EVT_
;#define _GPDAS_EVT_
;


SeverityNames=(Success=0x0:STATUS_SEVERITY_SUCCESS
               Informational=0x1:STATUS_SEVERITY_INFORMATIONAL
               Warning=0x2:STATUS_SEVERITY_WARNING
               Error=0x3:STATUS_SEVERITY_ERROR
              )
	
;
;/////////////////////////////////////////////////////////////////////////
;//
;// GPDAS Events (1000 - 1999)
;//
;/////////////////////////////////////////////////////////////////////////
;


MessageId=1000 Severity=Error SymbolicName=EVENT_GPDAS_STARTUP
Language=English
%1
.

MessageId= Severity=Error SymbolicName=EVENT_GENRSOP_FAILED
Language=English
Windows couldn't generate the Resultant Set of Policies for the given scenario. Error (%1).
.

;#endif _GPDAS_EVT_