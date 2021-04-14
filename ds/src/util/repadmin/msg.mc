;/*++
;
; Copyright (c) 1999 Microsoft Corporation.
; All rights reserved.
;
; MODULE NAME:
;
;      msg.mc
;
; ABSTRACT:
;
;      Message file containing messages for the repadmin utility
;
; CREATED:
;
;    Sept 2, 1999 William Lees (wlees)
;
; REVISION HISTORY:
;
;
; NOTES:
; Add new messages at the end of each severity section. Do not reorder
; existing messages.  If a message is no longer used, keep a placeholder
; in its place and mark it unused.
;
; Non string insertion parameters are indicated using the following syntax:
; %<arg>!<printf-format>!, for example %1!d! for decimal and %1!x! for hex
;
; BUGBUG - FormatMessage() doesn't handle 64 bit intergers properly.
; Unfortunately, the FormatMessage()/va_start()/va_end() stuff doesn't quite
; correctly support printf's %I64d format correctly.  It prints it out 
; seemly correctly, but it treats it as a 32 bit slot on the stack, so all 
; arguments in a message string after this point will be off by one.  So
; a hack/workaround is to make sure you put all I64d I64x etc format
; qualifiers at the end of a message string.  I've done this, and cut up
; messages that really should be one message with a _HACKx string at the
; end of the message constant, so someday they can be unified when this
; bug is fixed.
;
;--*/

MessageIdTypedef=DWORD

MessageId=0
SymbolicName=REPADMIN_SUCCESS
Severity=Success
Language=English
The operation was successful.
.

;
;// Severity=Informational Messages (Range starts at 1000)
;

MessageId=1000
SymbolicName=REPADMIN_NOVICE_HELP
Severity=Informational
Language=English
Usage: repadmin <cmd> <args> [/u:{domain\\user}] [/pw:{password|*}]
                             [/rpc] [/ldap]
                             [/csv] - see /csvhelp

Supported <cmd>s & args:
     /bind [DC_LIST]
     /bridgeheads [DC_LIST] [/verbose]
     /checkprop [DC_LIST from which to enumerate host DCs] <Naming Context> 
          <Originating DC Invocation ID> <Originating USN> 
     /dsaguid [DC_LIST] [GUID]
     /failcache [DC_LIST]
     /istg [DC_LIST] [/verbose]
     /kcc [DC_LIST] [/async]
     /latency [DC_LIST] [/verbose]
     /notifyopt [DC_LIST] <Naming Context> [/first:<value>] [/subs:<value>]
     /queue [DC_LIST]
     /querysites <From-Site-RDN> <To-Site-RDN-1> [<To-Site-RDN-2> ...]
          (may not be called with alternate credentials)
     /replicate <Dest_DC_LIST> <Naming Context> /allsources [/force] [/async]
          [/full] [/addref] [/readonly]
     /replicate <Dest_DC_LIST> <Source DC_NAME> <Naming Context> [/force] [/async]
          [/full] [/addref] [/readonly]
     /replsingleobj <DC_LIST> <dsa-source-guid> <obj dn>
     /replsummary [DC_LIST] /bysrc /bydest /errorsonly 
          [/sort:{ delta | partners | failures | error | percent | unresponsive }]
     /showattr <DC_LIST> <OBJ_LIST> [OBJ_LIST OPTIONS] 
          [/atts:<att1>,<att2>...] [/allvalues] [/long] [/dumpallblob]
     /showcert [DC_LIST]
     /showchanges . <SourceDC> <NamingContext> [/cookie:<file>]
          [/atts:<att1>,<att2>,...] [/long] [
     /showchanges <Dest_DC_LIST> <SourceDCObjectGUID> <NamingContext>
          [/verbose] [/statistics] [/noincremental] [/objectsecurity]
          [/ancestors] [/atts:<att1>,<att2>,...] [/filter:<ldap filter>]
     /showconn [DC_LIST] {serverRDN | Container DN | <DC GUID>} (default is local site)
          [/from:serverRDN] [/intersite]
     /showctx [DC_LIST] [/nocache]
     /showism [<Transport DN>] [/verbose] (must be executed locally)
     /showmsg {<Win32 error> | <DS event ID> /NTDSMSG}
     /showncsig [DC_LIST]
     /showobjmeta [DC_LIST] <Object DN>  [/nocache] [/linked]
     /showoutcalls [DC_LIST]
     /showproxy [DC_LIST] [Naming Context] [matchstring]            (search xdommove proxies)
     /showproxy [DC_LIST] [Object DN] [matchstring] /movedobject    (dump xdommoved object)
     /showrepl [DC_LIST [Source DC object GUID]] [Naming Context] [/verbose]
          [/nocache] [/repsto] [/conn] [/all] [/errorsonly] [/intersite]
     /showsig [DC_LIST]
     /showtime <DS time value>
     /showtrust [DC_LIST]
     /showutdvec <DC_LIST> <Naming Context> [/nocache] [/latency]
     /showvalue [DC_LIST] <Object DN> [Attribute Name] [Value DN] [/nocache]
     /syncall <DC> [<Naming Context>] [<flags>]
     /viewlist <DC_LIST> [OBJ_LIST]

Note: Most commands take their parameters in the order of "Destination or Target DC_LIST",
      then a "Soure DC_NAME" if required, and finally the NC or Object DN if required. 
      
      DC_LIST or DC_NAME is the proper DNS or NetBios name of a DC, for more options see 
      repadmin /listhelp.
      <Dest DC>, <Source DC>, <DC> : Names of the appropriate servers
      <Naming Context> is the Distinguished Name of the root of the NC
              Example: DC=My-Domain,DC=Microsoft,DC=Com
              
Note: Text (Naming Context names, server names, etc) with International or
      Unicode characters will only display correctly if appropriate fonts and
      language support are loaded

Deprecated Commands:  use repadmin /oldhelp to see these deprecated commands' syntaxes.
      /sync /propcheck /getchanges /getchanges /showreps /showvector /showmeta 	
.

MessageId=
SymbolicName=REPADMIN_EXPERT_HELP
Severity=Informational
Language=English

WARNING:
These commands have the potential to break your Active Directory installation,
and should be used only under the expert guidance of Microsoft PSS.

Expert Help
     /add <Naming Context> <Dest DC> <Source DC> [/asyncrep] [/syncdisable]
        [/dsadn:<Source DC DN>] [/transportdn:<Transport DN>] [/mail]
        [/async] [/readonly]
     /mod <Naming Context> <Dest DC> <Source GUID>
        [/readonly] [/srcdsaaddr:<dns address>]
        [/transportdn:<Transport DN>]
        [+nbrflagoption] [-nbrflagoption]
     /delete <Naming Context> <Dest DC> [<Source DC Address>] [/localonly]
        [/nosource] [/async]

     /removelingeringobjects <Dest_DC_LIST> <Source DC GUID> <NC> [/ADVISORY_MODE] 

     /addrepsto <Naming Context> <DC> <Reps-To DC> <Reps-To DC GUID>
     /updrepsto <Naming Context> <DC> <Reps-To DC> <Reps-To DC GUID>
     /delrepsto <Naming Context> <DC> <Reps-To DC> <Reps-To DC GUID>

     /rehost DC_LIST <Naming Context> <Good Source DC Address> [/application]
     /unhost DC_LIST <Naming Context>
     /removesources DC_LIST <Naming Context>

     /options [DC] [{+|-}IS_GC] [{+|-}DISABLE_INBOUND_REPL]
         [{+|-}DISABLE_OUTBOUND_REPL] [{+|-}DISABLE_NTDSCONN_XLATE]
	 
     /siteoptions [DC] [/site:<Site>] [{+|-}IS_AUTO_TOPOLOGY_DISABLED] 
	 [{+|-}IS_TOPL_CLEANUP_DISABLED] [{+|-}IS_TOPL_MIN_HOPS_DISABLED] 
	 [{+|-}IS_TOPL_DETECT_STALE_DISABLED] 
	 [{+|-}IS_INTER_SITE_AUTO_TOPOLOGY_DISABLED] 
	 [{+|-}IS_GROUP_CACHING_ENABLED] [{+|-}FORCE_KCC_WHISTLER_BEHAVIOR]   
         [{+|-}FORCE_KCC_W2K_ELECTION]   [{+|-}IS_RAND_BH_SELECTION_DISABLED]
         [{+|-}IS_SCHEDULE_HASHING_ENABLED]
         [{+|-}IS_REDUNDANT_SERVER_TOPOLOGY_ENABLED]

     /testhook [DC] [{+|-}lockqueue] [{+|-}link_cleaner]
	       [{+rpctime:<call_name>,<ip or hostname>,<seconds_to_run>|-rpctime}]
	       [{+rpcsync:<call_name>,<ip or hostname>|-rpcsync}]

nbrflagoptions:
        SYNC_ON_STARTUP DO_SCHEDULED_SYNCS TWO_WAY_SYNC
        NEVER_SYNCED IGNORE_CHANGE_NOTIFICATIONS DISABLE_SCHEDULED_SYNC
        COMPRESS_CHANGES NO_CHANGE_NOTIFICATIONS

.

MessageId=
SymbolicName=REPADMIN_PASSWORD_PROMPT
Severity=Informational
Language=English
Password: %0
.


MessageId=
SymbolicName=REPADMIN_SHOWCERT_STATUS_CHECKING_DC_CERT
Severity=Informational
Language=English
Checking for 'Domain Controller' certificate in store '%1'...
.

MessageId=
SymbolicName=REPADMIN_SHOWCERT_DC_V1_CERT_PRESENT
Severity=Informational
Language=English
Domain Controller Certificate V1 is present.
.

MessageId=
SymbolicName=REPADMIN_SHOWCERT_DC_V2_CERT_PRESENT
Severity=Informational
Language=English
Domain Controller Certificate V2 is present.
.
MessageId=
SymbolicName=REPADMIN_BIND_SUCCESS
Severity=Informational
Language=English
Bind to %1 succeeded.
.
MessageId=
SymbolicName=REPADMIN_BIND_EXT_SUPPORTED_HDR
Severity=Informational
Language=English
Extensions supported:
.

MessageId=
SymbolicName=REPADMIN_BIND_EXT_SUPPORTED_LINE
Severity=Informational
Language=English
    %1!-33S!: %2
.

MessageId=
SymbolicName=REPADMIN_BIND_EXT_SUPPORTED_LINE_YES
Severity=Informational
Language=English
    %1!-33S!: Yes
.

MessageId=
SymbolicName=REPADMIN_BIND_EXT_SUPPORTED_LINE_NO
Severity=Informational
Language=English
    %1!-33S!: No
.

MessageId=
SymbolicName=REPADMIN_BIND_SITE_GUID
Severity=Informational
Language=English

Site GUID: %1
.

MessageId=
SymbolicName=REPADMIN_MOD_CUR_REPLICA_FLAGS
Severity=Informational
Language=English
Current Replica flags: %1
.

MessageId=
SymbolicName=REPADMIN_ADD_ENQUEUED_ONE_WAY_REPL
Severity=Informational
Language=English
Successfully enqueued operation to establish one-way replication from source:%1 to dest:%2.
.

MessageId=
SymbolicName=REPADMIN_ADD_ONE_WAY_REPL_ESTABLISHED
Severity=Informational
Language=English
One-way replication from source:%1 to dest:%2 established.
.

MessageId=
SymbolicName=REPADMIN_MOD_CUR_SRC_ADDRESS
Severity=Informational
Language=English
Current source address: %1
.

MessageId=
SymbolicName=REPADMIN_MOD_REPL_LINK_MODIFIED
Severity=Informational
Language=English
Replication link from source:%1 to dest:%2 modified.
.

MessageId=
SymbolicName=REPADMIN_MOD_SRC_ADDR_SET
Severity=Informational
Language=English
Source address set to %1
.

MessageId=
SymbolicName=REPADMIN_MOD_TRANSPORT_DN_SET
Severity=Informational
Language=English
Transport DN set to %1
.

MessageId=
SymbolicName=REPADMIN_MOD_REPLICA_FLAGS_SET
Severity=Informational
Language=English
Replica flags set to %1
.

MessageId=
SymbolicName=REPADMIN_DEL_ENQUEUED_ONE_WAY_REPL_DEL
Severity=Informational
Language=English
Successfully enqueued operation to delete one-way replication from source:%1 to dest:%2.
.

MessageId=
SymbolicName=REPADMIN_DEL_DELETED_REPL_LINK
Severity=Informational
Language=English
Replication link from source:%1 to dest:%2 deleted.
.

MessageId=
SymbolicName=REPADMIN_UPDREFS_ENQUEUED_UPDATE_NOTIFICATIONS
Severity=Informational
Language=English
Successfully enqueued operation to update change notifications from %1 to %2.
.

MessageId=
SymbolicName=REPADMIN_UPDREFS_UPDATED_NOTIFICATIONS
Severity=Informational
Language=English
Successfully updated change notifications from %1 to %2.
.

MessageId=
SymbolicName=REPADMIN_KCC_ENQUEUED_KCC
Severity=Informational
Language=English
Successfully enqueued consistency check for %1.
.

MessageId=
SymbolicName=REPADMIN_KCC_KCC_SUCCESS
Severity=Informational
Language=English
Consistency check on %1 successful.
.

MessageId=
SymbolicName=REPADMIN_SYNC_ENQUEUED_SYNC
Severity=Informational
Language=English
Successfully enqueued sync from %1 to %2.
.

MessageId=
SymbolicName=REPADMIN_SYNC_ENQUEUED_SYNC_ALL_NEIGHBORS
Severity=Informational
Language=English
Successfully enqueued sync from (all neighbors) to %1.
.

MessageId=
SymbolicName=REPADMIN_SYNC_SYNC_SUCCESS
Severity=Informational
Language=English
Sync from %1 to %2 completed successfully.
.

MessageId=
SymbolicName=REPADMIN_SYNC_SYNC_SUCCESS_ALL_NEIGHBORS
Severity=Informational
Language=English
Sync from (all neighbors) to %1 completed successfully.
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_CONNECTING_TO_DCS
Severity=Informational
Language=English
Connecting to the writable DCs:
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_DISABLING_REPL
Severity=Informational
Language=English
Disabling inbound/outbound replication & deleting replication state:
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_DSA_LINE
Severity=Informational
Language=English
    %1...
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_REPL_DISABLED
Severity=Informational
Language=English
        Inbound/outbound replication disabled.
.

MessageId=
SymbolicName=REPADMIN_PRINT_STR
Severity=Informational
Language=English
%1%n%0
.

MessageId=
SymbolicName=REPADMIN_PRINT_SHORT_STR
Severity=Informational
Language=English
%1!S!
.

MessageId=
SymbolicName=REPADMIN_PRINT_SHORT_STR_NO_CR
Severity=Informational
Language=English
%1!S!%0
.

MessageId=
SymbolicName=REPADMIN_PRINT_STR_NO_CR
Severity=Informational
Language=English
%1%0
.

MessageId=
SymbolicName=REPADMIN_PRINT_DOT_NO_CR
Severity=Informational
Language=English
.%0
.

MessageId=
SymbolicName=REPADMIN_PRINT_CR
Severity=Informational
Language=English
%n%0
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_REMOVING_UTD_VEC
Severity=Informational
Language=English
        Removing up-to-date vector...
.

MessageId=
SymbolicName=REPADMIN_NO_INBOUND_REPL_PARTNERS
Severity=Informational
Language=English
        No inbound replication partners.
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_REMOVE_LINK
Severity=Informational
Language=English
        Removing link from %1...
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_RE_ENABLING_REPL
Severity=Informational
Language=English
Re-enabling in/out replication:
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_SUCCESS
Severity=Informational
Language=English

OPERATION SUCCESSFUL!

Note that you must demote all GCs -- you can re-promote them a half
hour later.
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_BUILDING_START_POS
Severity=Informational
Language=English
Building starting position from destination server %1
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_PRINT_STATS_HDR_CUM_TOT
Severity=Informational
Language=English

********* Cumulative packet totals ************
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_PRINT_STATS_HDR_GRD_TOT
Severity=Informational
Language=English

********* Grand total *************************
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_PRINT_STATS_1
Severity=Informational
Language=English
Packets:              %1!d!
Objects:              %2!d!
Object Additions:     %3!d!
Object Modifications: %4!d!
Object Deletions:     %5!d!
Object Moves:         %6!d!
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_PRINT_STATS_2
Severity=Informational
Language=English
Attributes:           %1!d!
Values:               %2!d!
Dn-valued Attributes: %3!d!
MaxDnVals on any attr:%4!d!
ObjectDn with maxattr:%5!S!
Attrname with maxattr:%6!S!
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_PRINT_STATS_3
Severity=Informational
Language=English
#dnvals	1-250	251-500	501-750	751-1000 1000+
add	%1!d!	%2!d!	%3!d!	%4!d!	 %5!d!
mod	%6!d!	%7!d!	%8!d!	%9!d!	 %10!d!
***********************************************
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_BYTE_BLOB_NO_CR
Severity=Informational
Language=English
<%1!d! byte blob>%0
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_NO_CHANGES
Severity=Informational
Language=English

No Changes
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_OBJS_RET
Severity=Informational
Language=English

Objects returned: %1!d!
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_DATA_1
Severity=Informational
Language=English
(%1!d!) %2!S! %3
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_DATA_2_NO_CR
Severity=Informational
Language=English
    %1!d!> %2: %0
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_SEMICOLON_NO_CR
Severity=Informational
Language=English
; %0
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_USING_COOKIE_FILE
Severity=Informational
Language=English
Using cookie from file %1 (%2!d! bytes)
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_EMPTY_COOKIE
Severity=Informational
Language=English
Using empty cookie (full sync).
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_SRC_NEIGHBOR
Severity=Informational
Language=English

Source Neighbor:
%1
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_DST_UTD_VEC
Severity=Informational
Language=English

Destination's up-to-date vector:
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_DST_UTD_VEC_ONE_USN
Severity=Informational
Language=English
%1!-36s! @ USN %2!I64d!
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_SRC_DSA_HDR
Severity=Informational
Language=English

==== SOURCE DC: %1 ====
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_COOKIE_FILE_WRITTEN
Severity=Informational
Language=English
New cookie written to file %1 (%2!d! bytes)
.

MessageId=
SymbolicName=REPADMIN_QUEUE_CONTAINS
Severity=Informational
Language=English
Queue contains %1!d! items.
.

MessageId=
SymbolicName=REPADMIN_QUEUE_MAIL_TH_EXEC
Severity=Informational
Language=English
The mail thread is executing.
.

MessageId=
SymbolicName=REPADMIN_QUEUE_CUR_TASK_EXEC
Severity=Informational
Language=English
Current task began executing at %1!S!.
.

MessageId=
SymbolicName=REPADMIN_QUEUE_CUR_TASK_EXEC_TIME
Severity=Informational
Language=English
Task has been executing for %1!d! minutes, %2!d! seconds.
.

MessageId=
SymbolicName=REPADMIN_QUEUE_ENQUEUED_DATA_ITEM_HDR
Severity=Informational
Language=English

[%1!d!] Enqueued %2!S! at priority %3!d!
.

MessageId=
SymbolicName=REPADMIN_QUEUE_ENQUEUED_DATA_ITEM_DATA
Severity=Informational
Language=English
    %1!S!
    NC %2
    DC %3
    DC object GUID %4
    DC transport addr %5
.

MessageId=
SymbolicName=REPADMIN_FAILCACHE_NONE
Severity=Informational
Language=English
(none)
.

MessageId=
SymbolicName=REPADMIN_PRINT_NO_FAILURES
Severity=Informational
Language=English
No Failures.
.

MessageId=
SymbolicName=REPADMIN_FAILCACHE_FAILURES_LINE
Severity=Informational
Language=English
        %1!d! consecutive failures since %2!S!.
.

MessageId=
SymbolicName=REPADMIN_BIND_REPL_EPOCH
Severity=Informational
Language=English
Repl epoch: %1!d!
.

MessageId=
SymbolicName=REPADMIN_FAILCACHE_LAST_ERR_LINE
Severity=Informational
Language=English
Last error: %0
.

MessageId=
SymbolicName=REPADMIN_FAILCACHE_CONN_HDR
Severity=Informational
Language=English
==== KCC CONNECTION FAILURES ============================
.

MessageId=
SymbolicName=REPADMIN_FAILCACHE_LINK_HDR
Severity=Informational
Language=English
==== KCC LINK FAILURES ==================================
.

MessageId=
SymbolicName=REPADMIN_SHOWREPS_DSA_OPTIONS
Severity=Informational
Language=English
DC Options: %1!S!
.

MessageId=
SymbolicName=REPADMIN_SHOWREPS_SITE_OPTIONS
Severity=Informational
Language=English
Site Options: %1!S!
.

MessageId=
SymbolicName=REPADMIN_PRINT_DSA_OBJ_GUID
Severity=Informational
Language=English
DC object GUID: %1
.

MessageId=
SymbolicName=REPADMIN_PRINT_INVOCATION_ID
Severity=Informational
Language=English
DC invocationID: %1
.

MessageId=
SymbolicName=REPADMIN_PRINT_NAMING_CONTEXT_NO_CR
Severity=Informational
Language=English
Naming Context: %1
.

MessageId=
SymbolicName=REPADMIN_SHOWREPS_IN_NEIGHBORS_HDR
Severity=Informational
Language=English
==== INBOUND NEIGHBORS ======================================
.


MessageId=
SymbolicName=REPADMIN_SHOWREPS_OUT_NEIGHBORS_HDR
Severity=Informational
Language=English
==== OUTBOUND NEIGHBORS FOR CHANGE NOTIFICATIONS ============
.

MessageId=
SymbolicName=REPADMIN_SHOWREPS_KCC_CONN_OBJS_HDR
Severity=Informational
Language=English
==== KCC CONNECTION OBJECTS ============================================
.

MessageId=
SymbolicName=REPADMIN_SHOWNEIGHBOR_DISP_SERVER
Severity=Informational
Language=English
    %1 via %2
.

MessageId=
SymbolicName=REPADMIN_PRINT_INTERSITE_TRANS_OBJ_GUID
Severity=Informational
Language=English
interSiteTransport object GUID: %1
.


MessageId=
SymbolicName=REPADMIN_SHOWNEIGHBOR_USNS
Severity=Informational
Language=English
        USNs: %1!I64d!/OU,%0
.

MessageId=
SymbolicName=REPADMIN_SHOWNEIGHBOR_USNS_HACK2
Severity=Informational
Language=English
 %1!I64d!/PU
.

MessageId=
SymbolicName=REPADMIN_SHOWNEIGHBOR_LAST_ATTEMPT_SUCCESS
Severity=Informational
Language=English
        Last attempt @ %1!S! was successful.
.


MessageId=
SymbolicName=REPADMIN_SHOWNEIGHBOR_LAST_ATTEMPT_DELAYED
Severity=Informational
Language=English
        Last attempt @ %1!S! was delayed for a normal reason, result %0
.

MessageId=
SymbolicName=REPADMIN_SHOWNEIGHBOR_LAST_ATTEMPT_FAILED
Severity=Informational
Language=English
Last attempt @ %1!S! failed, result %0
.

MessageId=
SymbolicName=REPADMIN_SHOWNEIGHBOR_N_CONSECUTIVE_FAILURES
Severity=Informational
Language=English
        %1!u! consecutive failure(s).
.

MessageId=
SymbolicName=REPADMIN_SHOWNEIGHBOR_LAST_SUCCESS
Severity=Informational
Language=English
        Last success @ %1!S!.
.

MessageId=
SymbolicName=REPADMIN_GENERAL_ADDRESS_COLON_STR
Severity=Informational
Language=English
Address: %1
.

MessageId=
SymbolicName=REPADMIN_SHOWNEIGHBOR_ADDED
Severity=Informational
Language=English
        Added @ %1!S!.
.


MessageId=
SymbolicName=REPADMIN_SHOWVECTOR_ONE_USN
Severity=Informational
Language=English
%1!-36s! @ USN %2!9I64d! @ Time %0
.

MessageId=
SymbolicName=REPADMIN_SHOWVECTOR_ONE_USN_HACK2
Severity=Informational
Language=English
%1!S!
.

MessageId=
SymbolicName=REPADMIN_SHOWMETA_N_ENTRIES
Severity=Informational
Language=English
%1!d! entries.
.

MessageId=
SymbolicName=REPADMIN_SHOWMETA_DATA_HDR
Severity=Informational
Language=English
Loc.USN                          Originating DC   Org.USN  Org.Time/Date        Ver Attribute
=======                          =============== ========= =============        === =========
.

MessageId=
SymbolicName=REPADMIN_SHOWMETA_DATA_LINE
Severity=Informational
Language=English
%1!7I64d!%0
.

MessageId=
SymbolicName=REPADMIN_SHOWMETA_DATA_LINE_HACK2
Severity=Informational
Language=English
%1!41s!%2!10I64d!%0
.

MessageId=
SymbolicName=REPADMIN_SHOWMETA_DATA_LINE_HACK3
Severity=Informational
Language=English
%1!20S!%2!5d! %3
.

MessageId=
SymbolicName=REPADMIN_SHOWVALUE_DATA_HDR
Severity=Informational
Language=English
Type    Attribute     Last Mod Time                             Originating DC  Loc.USN Org.USN Ver
======= ============  =============                           ================= ======= ======= ===
        Distinguished Name
        =============================
.

MessageId=
SymbolicName=REPADMIN_SHOWVALUE_DATA_LINE_LEGACY
Severity=Informational
Language=English
LEGACY  %1 %2(%3!d!) %4!S!
.

MessageId=
SymbolicName=REPADMIN_SHOWVALUE_DATA_LINE_PRESENT
Severity=Informational
Language=English
PRESENT %1 %2(%3!d!) %4!S!
.

MessageId=
SymbolicName=REPADMIN_SHOWVALUE_DATA_LINE_ABSENT
Severity=Informational
Language=English
ABSENT  %1 %2(%3!d!) %4!S! %5!S!
.

MessageId=
SymbolicName=REPADMIN_SHOWVALUE_DATA_LEGACY
Severity=Informational
Language=English
LEGACY  %0
.

MessageId=
SymbolicName=REPADMIN_SHOWVALUE_DATA_PRESENT
Severity=Informational
Language=English
PRESENT %0
.

MessageId=
SymbolicName=REPADMIN_SHOWVALUE_DATA_ABSENT
Severity=Informational
Language=English
ABSENT  %0
.

MessageId=
SymbolicName=REPADMIN_SHOWVALUE_DATA_BASIC
Severity=Informational
Language=English
%1!12s!%0
.

MessageId=
SymbolicName=REPADMIN_SHOWVALUE_DATA_VALUE_META_DATA
Severity=Informational
Language=English
%1!20S! %2!38s! %3!7I64d! %0
.

MessageId=
SymbolicName=REPADMIN_SHOWVALUE_DATA_VALUE_META_DATA_HACK2
Severity=Informational
Language=English
%1!7I64d! %0
.
                
MessageId=
SymbolicName=REPADMIN_SHOWVALUE_DATA_VALUE_META_DATA_HACK3
Severity=Informational
Language=English
%1!3d!
.


MessageId=
SymbolicName=REPADMIN_SHOWVALUE_DATA_LINE
Severity=Informational
Language=English
%1!7I64d!%0
.

MessageId=
SymbolicName=REPADMIN_SHOWVALUE_DATA_LINE_HACK2
Severity=Informational
Language=English
 %1!37s! %2!8I64d!%0
.

MessageId=
SymbolicName=REPADMIN_SHOWVALUE_DATA_LINE_HACK3
Severity=Informational
Language=English
 %1!20S! %2!5d!
.

MessageId=
SymbolicName=REPADMIN_SHOWVALUE_NO_MORE_ITEMS
Severity=Informational
Language=English
No more items.
.

MessageId=
SymbolicName=REPADMIN_SHOWCTX_OPEN_CONTEXT_HANDLES
Severity=Informational
Language=English
%1!d! open context handles.
.

MessageId=
SymbolicName=REPADMIN_SHOWCTX_DATA_1
Severity=Informational
Language=English
%1 @ %2!S! (PID %3!d!) (Handle 0x%4!I64x!)
.

MessageId=
SymbolicName=REPADMIN_SHOWCTX_DATA_2
Severity=Informational
Language=English
    bound, refs=%1!d!, last used %2!S!
.

MessageId=
SymbolicName=REPADMIN_SHOWCTX_DATA_2_NOT
Severity=Informational
Language=English
    NOT bound, refs=%1!d!, last used %2!S!
.

MessageId=
SymbolicName=REPADMIN_SHOWISM_ALL_DCS_BRIDGEHEAD_CANDIDATES
Severity=Informational
Language=English
All DCs in site %1 (with trans & hosting NC) are bridgehead candidates.
.

MessageId=
SymbolicName=REPADMIN_SHOWISM_N_SERVERS_ARE_BRIDGEHEADS
Severity=Informational
Language=English
%1!d! server(s) are defined as bridgeheads for transport %2 & site %3:
.

MessageId=
SymbolicName=REPADMIN_SHOWISM_N_SERVERS_ARE_BRIDGEHEADS_DATA
Severity=Informational
Language=English
Server(%1!d!) %2
.

MessageId=
SymbolicName=REPADMIN_SHOWISM_TRANSPORT_CONNECTIVITY_HDR
Severity=Informational
Language=English

==== TRANSPORT %1 CONNECTIVITY INFORMATION FOR %2!d! SITES: ====

.

MessageId=
SymbolicName=REPADMIN_SHOWISM_SITES_HDR
Severity=Informational
Language=English
%1%2!4d!%0
.


MessageId=
SymbolicName=REPADMIN_SHOWISM_SITES_HDR_2
Severity=Informational
Language=English
Site(%1!d!) %2
.

MessageId=
SymbolicName=REPADMIN_SHOWISM_SITES_DATA
Severity=Informational
Language=English
%1%2!d!:%3!d!:%4!x!%0
.

MessageId=
SymbolicName=REPADMIN_SHOWISM_SCHEDULE_DATA
Severity=Informational
Language=English
Schedule between %1 and %2 (cost %3!d!, interval %4!d!):
.

MessageId=
SymbolicName=REPADMIN_SHOWISM_CONN_ALWAYS_AVAIL
Severity=Informational
Language=English
Connection is always available.
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_COOKIE_NULL
Severity=Informational
Language=English
Cookie: null
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_COOKIE_DATA
Severity=Informational
Language=English
Cookie: ver %1!d!,
HighObjUpdate: %2!I64d!,
.
MessageId=
SymbolicName=REPADMIN_GETCHANGES_COOKIE_DATA_HACK2
Severity=Informational
Language=English
HighPropUpdate: %1!I64d!
.

MessageId=
SymbolicName=REPADMIN_SHOWTIME_TIME_DATA
Severity=Informational
Language=English
%1!I64d!%0
.

MessageId=
SymbolicName=REPADMIN_SHOWTIME_TIME_DATA_HACK2
Severity=Informational
Language=English
 = 0x%1!I64x!%0
.

MessageId=
SymbolicName=REPADMIN_SHOWTIME_TIME_DATA_HACK3
Severity=Informational
Language=English
 = %1!S! UTC = %2!S! local
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_REPL_SUPPRESSED
Severity=Informational
Language=English
Replication suppressed by user request:
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_FROM_TO
Severity=Informational
Language=English
    From: %1
    To  : %2
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_CALLBACK_MESSAGE
Severity=Informational
Language=English
CALLBACK MESSAGE: %0
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_REPL_IN_PROGRESS
Severity=Informational
Language=English
The following replication is in progress:
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_REPL_COMPLETED
Severity=Informational
Language=English
The following replication completed successfully:
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_FINISHED
Severity=Informational
Language=English
SyncAll Finished.
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_UNKNOWN
Severity=Informational
Language=English
Unknown.
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_SHOWREPS_CMDLINE
Severity=Informational
Language=English
repadmin /showreps %1 %2 %3
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_HELP
Severity=Informational
Language=English
DsReplicaSyncAll commandline interface.
repadmin /SyncAll [/adehijpPsS] <Dest DC> [<Naming Context>]
    /a: Abort if any server is unavailable
    /A: Perform /SyncAll for all NC's held by <Dest DC> (ignores <Naming Context>)
    /d: ID servers by DN in messages (instead of GUID DNS)
    /e: Enterprise, cross sites (default: only home site)
    /h: Print this help screen
    /i: Iterate indefinitely
    /I: Perform showreps on each server pair in path instead of syncing
    /j: Sync adjacent servers only
    /p: Pause for possible user abort after every message
    /P: Push changes outward from home server (default: pull changes)
    /q: Quiet mode, suppress callback messages
    /Q: Very quiet, report fatal errors only
    /s: Do not sync (just analyze topology and generate messages)
    /S: Skip initial server-response check (assume all servers are available)
If <Naming Context> is omitted DsReplicaSyncAll defaults to the Configuration NC.
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_USER_CANCELED
Severity=Informational
Language=English
SyncAll cancelled by user request.
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_TERMINATED_WITH_NO_ERRORS
Severity=Informational
Language=English
SyncAll terminated with no errors.
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_PRINT_ITER
Severity=Informational
Language=English
 [%1!ld!]
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_Q_OR_CONTINUE_PROMPT
Severity=Informational
Language=English
Q to quit, any other key to continue.
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_ANY_KEY_PROMPT
Severity=Informational
Language=English
Press any key to continue . . .
.

MessageId=
SymbolicName=REPADMIN_PRINT_SPACE
Severity=Informational
Language=English
 %0
.

MessageId=
SymbolicName=REPADMIN_SUN
Severity=Informational
Language=English
        Sun%0
.
MessageId=
SymbolicName=REPADMIN_MON
Severity=Informational
Language=English
        Mon%0
.
MessageId=
SymbolicName=REPADMIN_TUE
Severity=Informational
Language=English
        Tue%0
.
MessageId=
SymbolicName=REPADMIN_WED
Severity=Informational
Language=English
        Wed%0
.
MessageId=
SymbolicName=REPADMIN_THU
Severity=Informational
Language=English
        Thu%0
.
MessageId=
SymbolicName=REPADMIN_FRI
Severity=Informational
Language=English
        Fri%0
.
MessageId=
SymbolicName=REPADMIN_SAT
Severity=Informational
Language=English
        Sat%0
.

MessageId=
SymbolicName=REPADMIN_SCHEDULE_HOUR_HDR
Severity=Informational
Language=English
        day: 0123456789ab0123456789ab
.

MessageId=
SymbolicName=REPADMIN_SCHEDULE_LOADING
Severity=Informational
Language=English

Partition Replication Schedule Loading:
.

MessageId=
SymbolicName=REPADMIN_SCHEDULE_DATA_HOUR
Severity=Informational
Language=English
%1!2.2d!%0 
.

MessageId=
SymbolicName=REPADMIN_SCHEDULE_DATA_QUARTER
Severity=Informational
Language=English
 %1!d!%0
.

MessageId=
SymbolicName=REPADMIN_PRINT_HEX_NO_CR
Severity=Informational
Language=English
%1!x!%0
.

MessageId=
SymbolicName=REPADMIN_GUIDCACHE_CACHING
Severity=Informational
Language=English
Caching GUIDs.
.

MessageId=
SymbolicName=REPADMIN_PROPCHECK_DSA_COLON_NO_CR
Severity=Informational
Language=English
%1: %0
.

MessageId=
SymbolicName=REPADMIN_PRINT_YES
Severity=Informational
Language=English
yes%0
.

MessageId=
SymbolicName=REPADMIN_PRINT_NO_NO
Severity=Informational
Language=English
** NO! **%0
.

MessageId=
SymbolicName=REPADMIN_PROPCHECK_NO_CURSORS_FOUND
Severity=Informational
Language=English
-- No cursor found!
.

MessageId=
SymbolicName=REPADMIN_PROPCHECK_USN
Severity=Informational
Language=English
 (USN %1!I64d!)
.

MessageId=
SymbolicName=REPADMIN_PRINT_CURRENT_NO_CR
Severity=Informational
Language=English
Current %0
.

MessageId=
SymbolicName=REPADMIN_PRINT_NEW_NO_CR
Severity=Informational
Language=English
New %0
.

MessageId=
SymbolicName=REPADMIN_SHOWSIG_RETIRED_INVOC_ID
Severity=Informational
Language=English
%1 retired on %2!S! at USN %3!I64d!
.

MessageId=
SymbolicName=REPADMIN_SHOWSIG_NO_RETIRED_SIGS
Severity=Informational
Language=English
No retired signatures.
.

MessageId=
SymbolicName=REPADMIN_SHOW_MATCH_FAIL_SRC
Severity=Informational
Language=English
Source: %1\%2
.

MessageId=
SymbolicName=REPADMIN_SHOW_MATCH_FAIL_N_CONSECUTIVE_FAILURES
Severity=Informational
Language=English
******* %1!d! CONSECUTIVE FAILURES since %2!S!
.

MessageId=
SymbolicName=REPADMIN_SHOW_MISSING_NEIGHBOR_REPLICA_ADDED
Severity=Informational
Language=English
                Replica link has been added.
.

MessageId=
SymbolicName=REPADMIN_SHOW_BRIDGEHEADS_HDR
Severity=Informational
Language=English
        Naming Context         Attempt Time         Success Time  #Fail  Last Result
      ================  ===================  ===================  =====  ==============
.

MessageId=
SymbolicName=REPADMIN_SHOW_BRIDGEHEADS_DATA_1
Severity=Informational
Language=English
%1!22s!%0
.

MessageId=
SymbolicName=REPADMIN_SHOW_BRIDGEHEADS_DATA_2
Severity=Informational
Language=English
%1!21S!%2!21S! %3!5d!   %4
.

MessageId=
SymbolicName=REPADMIN_SHOWCONN_DATA
Severity=Informational
Language=English
Connection --
    Connection name : %1
    Server DNS name : %2
    Server DN  name : %3
.

MessageId=
SymbolicName=REPADMIN_SHOWCONN_TRANSPORT_TYPE
Severity=Informational
Language=English
        TransportType: %1
.

MessageId=
SymbolicName=REPADMIN_SHOWCONN_TRANSPORT_TYPE_INTRASITE_RPC
Severity=Informational
Language=English
        TransportType: intrasite RPC
.

MessageId=
SymbolicName=REPADMIN_SHOWCONN_OPTIONS
Severity=Informational
Language=English
        options: %0
.

MessageId=
SymbolicName=REPADMIN_SHOWCONN_REASON
Severity=Informational
Language=English
        Reason: %0
.

MessageId=
SymbolicName=REPADMIN_SHOWCONN_ENABLED_CONNECTION
Severity=Informational
Language=English
        enabledConnection: %1
.

MessageId=
SymbolicName=REPADMIN_SHOWCONN_WHEN_CHANGED
Severity=Informational
Language=English
        whenChanged: %0
.

MessageId=
SymbolicName=REPADMIN_SHOWCONN_WHEN_CREATED
Severity=Informational
Language=English
        whenCreated: %0
.

MessageId=
SymbolicName=REPADMIN_SHOWCONN_SCHEDULE
Severity=Informational
Language=English
        Schedule:
.

MessageId=
SymbolicName=REPADMIN_SHOWCONN_N_CONNECTIONS_FOUND
Severity=Informational
Language=English
%1!d! connections found.
.

MessageId=
SymbolicName=REPADMIN_SHOWCONN_BASE_DN
Severity=Informational
Language=English
Base DN: %1
.

MessageId=
SymbolicName=REPADMIN_SHOWCONN_REPLICATES_NC
Severity=Informational
Language=English
ReplicatesNC: %1
.

MessageId=
SymbolicName=REPADMIN_SHOWCONN_KCC_CONN_OBJS_HDR
Severity=Informational
Language=English
==== KCC CONNECTION OBJECTS ============================================
.

MessageId=
SymbolicName=REPADMIN_LATENCY_HDR
Severity=Informational
Language=English
        Originating Site    Ver    Time Local Update    Time Orig. Update   Latency  Since Last
      ==================  =====  ===================  ===================  ========  ==========
.

MessageId=
SymbolicName=REPADMIN_LATENCY_DATA_1
Severity=Informational
Language=English
%1!24s! %2!6d! %3!20S! %4!20S! %0
.

MessageId=
SymbolicName=REPADMIN_PRINT_HH_MM_SS_TIME
Severity=Informational
Language=English
%1!02d!:%2!02d!:%3!02d!%0
.

MessageId=
SymbolicName=REPADMIN_BRIDGEHEADS_HDR
Severity=Informational
Language=English
             Source Site    Local Bridge  Trns         Fail. Time    #    Status
         ===============  ==============  ====  =================   ===  ======== 
.

MessageId=
SymbolicName=REPADMIN_BRIDGEHEADS_DATA_1
Severity=Informational
Language=English
%1!24s!%2!16s!%0
.

MessageId=
SymbolicName=REPADMIN_BRIDGEHEADS_DATA_2
Severity=Informational
Language=English
%1!6s!%0
.

MessageId=
SymbolicName=REPADMIN_BRIDGEHEADS_DATA_3
Severity=Informational
Language=English
%1!20S!%2!4d!   %3!20s!%0
.

MessageId=
SymbolicName=REPADMIN_LATENCY_FOR_SITE
Severity=Informational
Language=English
Replication Latency for site %1 (%2):
.

MessageId=
SymbolicName=REPADMIN_SHOWISTG_BRDIGEHEADS
Severity=Informational
Language=English
Bridgeheads for site %1 (%2):
.

MessageId=
SymbolicName=REPADMIN_SHOWISTG_GATHERING_TOPO
Severity=Informational
Language=English
Gathering topology from site %1 (%2):
.

MessageId=
SymbolicName=REPADMIN_SHOWISTG_HDR
Severity=Informational
Language=English
                   Site                ISTG
      ==================   =================
.

MessageId=
SymbolicName=REPADMIN_SHOWISTG_DATA_1
Severity=Informational
Language=English
%1!24s!%2!20s!
.

MessageId=
SymbolicName=REPADMIN_LATENCY_DISCLAIMER
Severity=Informational
Language=English
Disclaimer:
1. Latency is shown for Configuration NC only.
2. Probes are sent once every half hour. Actual replication may occur more frequently.
3. What is normal Intersite replication frequency depends on many factors: site link schedules, intervals and bridgehead availability.

.

MessageId=
SymbolicName=REPADMIN_TESTHOOK_SUCCESSFULLY_INVOKED
Severity=Informational
Language=English
Successfully invoked test hook "%1".
.

MessageId=
SymbolicName=REPADMIN_DSAGUID_DATA_LINE
Severity=Informational
Language=English
"%1" = %2.
.

MessageId=
SymbolicName=REPADMIN_SHOWPROXY_SEARCHING_NC
Severity=Informational
Language=English
Searching naming context: %1
.

MessageId=
SymbolicName=REPADMIN_SHOWPROXY_OBJECT_DN
Severity=Informational
Language=English
Object DN: %1
.

MessageId=
SymbolicName=REPADMIN_SHOWPROXY_PROXY_DN
Severity=Informational
Language=English
Proxy DN: %1
.

MessageId=
SymbolicName=REPADMIN_SHOWPROXY_OBJECT_GUID
Severity=Informational
Language=English
Object GUID: %1
.

MessageId=
SymbolicName=REPADMIN_SHOWPROXY_MOVED_FROM_NC
Severity=Informational
Language=English
Moved from NC: %1
.

MessageId=
SymbolicName=REPADMIN_SHOWPROXY_MOVED_TO_DN
Severity=Informational
Language=English
Proxied (moved to) DN: %1
.

MessageId=
SymbolicName=REPADMIN_SHOWPROXY_PROXY_TYPE
Severity=Informational
Language=English
        Proxy Type: (%1!d!) %0
.

MessageId=
SymbolicName=REPADMIN_SHOWPROXY_PROXY_TYPE_MOVED_OBJ
Severity=Informational
Language=English
moved object%0
.

MessageId=
SymbolicName=REPADMIN_SHOWPROXY_PROXY_TYPE_PROXY
Severity=Informational
Language=English
proxy%0
.

MessageId=
SymbolicName=REPADMIN_SHOWPROXY_PROXY_TYPE_UNKNOWN
Severity=Informational
Language=English
unknown%0
.

MessageId=
SymbolicName=REPADMIN_SHOWPROXY_PROXY_EPOCH
Severity=Informational
Language=English
        Proxy Epoch: %1!d!
.

MessageId=
SymbolicName=REPADMIN_SHOWMSG_DATA
Severity=Informational
Language=English
%1!d! = 0x%2!x! = "%3"
.


MessageId=
SymbolicName=REPADMIN_NOTIFYOPT_CURRENT
Severity=Informational
Language=English

Current Notification Options:
.

MessageId=
SymbolicName=REPADMIN_NOTIFYOPT_NEWLYSET
Severity=Informational
Language=English

New Notification Options:
.

MessageId=
SymbolicName=REPADMIN_NOTIFYOPT_SHOWFIRST
Severity=Informational
Language=English
Replication-Notify-First-DSA-Delay: %1!d!
.

MessageId=
SymbolicName=REPADMIN_NOTIFYOPT_SHOWFIRST_NOTSET
Severity=Informational
Language=English
Replication-Notify-First-DSA-Delay is not set.
.

MessageId=
SymbolicName=REPADMIN_NOTIFYOPT_SHOWSUBS
Severity=Informational
Language=English
Replication-Notify-Subsequent-DSA-Delay: %1!d!
.

MessageId=
SymbolicName=REPADMIN_NOTIFYOPT_SHOWSUBS_NOTSET
Severity=Informational
Language=English
Replication-Notify-Subsequent-DSA-Delay is not set.
.

MessageId=
SymbolicName=REPADMIN_CERT_FOUND_GUID
Severity=Informational
Language=English
A Domain Controller Certificate was found with Computer Object GUID %1!S!.
.

MessageId=
SymbolicName=REPADMIN_COMPUTER_OBJ_GUID
Severity=Informational
Language=English
The Computer Object Unique ID is %1.
.

MessageId=
SymbolicName=REPADMIN_SHOWNCSIG_RETIRED_NC
Severity=Informational
Language=English
NC %1 retired on %2!S! at USN %3!I64d!
.

MessageId=
SymbolicName=REPADMIN_SHOWNCSIG_INVOCATION_ID
Severity=Informational
Language=English
Retired NC invocationID: %1
.



;
;// Severity=Warning Messages (Range starts at 2000)
;
MessageId=2000
SymbolicName=REPADMIN_WARNING_LEVEL
Severity=Warning
Language=English
Unused
.

MessageId=
SymbolicName=REPADMIN_MOD_FLAGS_NOT_MODABLE
Severity=Warning
Language=English
The following flags are not modifiable: %1
.

MessageId=
SymbolicName=REPADMIN_GENERAL_ASSERT
Severity=Warning
Language=English
Assertion %1 failed in file %2, line %3!d!.
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_NOTE_DISABLED_REPL
Severity=Warning
Language=English
NOTE: Replication on writable DCs has been left disabled.
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_DN_MISSING
Severity=Warning
Language=English
DN is missing!
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_INVALID_DN_2
Severity=Warning
Language=English
(%1!d!) <invalid DN>
.

MessageId=
SymbolicName=REPADMIN_SHOWREPS_WARN_GC_NOT_ADVERTISING
Severity=Warning
Language=English
    WARNING:  Not advertising as a global catalog.
.

MessageId=
SymbolicName=REPADMIN_SHOW_MISSING_NEIGHBOR_WARN_KCC_COULDNT_ADD_REPLICA_LINK
Severity=Warning
Language=English
******* WARNING: KCC could not add this REPLICA LINK due to error.
.

MessageId=
SymbolicName=REPADMIN_SHOWISM_SITE_NOT_CONN
Severity=Warning
Language=English
Site %1 is not connected by this transport.
.

MessageId=
SymbolicName=REPADMIN_REPL_SINGLE_OBJ_SUCCESSFULLY_INVOKED
Severity=Informational
Language=English
Successfully replicated object %1 to %2 from %3.
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_NC
Severity=Informational
Language=English
Syncing partition: %1
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_ALL_NCS
Severity=Informational
Language=English
Syncing all NC's held on %1.
.

MessageId=
SymbolicName=REPADMIN_SHOW_TRUST_INFO
Severity=Informational
Language=English
Domain Trust Info:
.

MessageId=
SymbolicName=REPADMIN_TRUST_DOMAIN
Severity=Informational
Language=English
TRUSTED   : %1
.

MessageId=
SymbolicName=REPADMIN_UNTRUST_DOMAIN
Severity=Informational
Language=English
UNTRUSTED : %1
.

MessageId=
SymbolicName=REPADMIN_QUERYSITES_OUTPUT_HEADER
Severity=Informational
Language=English
From site: %1

To Site Name                                                      Cost
------------                                                      ----
.

MessageId=
SymbolicName=REPADMIN_QUERYSITES_OUTPUT_SITENAME
Severity=Informational
Language=English
%1!-64s!  %0
.

MessageId=
SymbolicName=REPADMIN_QUERYSITES_OUTPUT_COST
Severity=Informational
Language=English
%1!d!
.

MessageId=
SymbolicName=REPADMIN_QUERYSITES_OUTPUT_ERROR
Severity=Informational
Language=English
Error: 0x%1!x!
.

MessageId=
SymbolicName=REPADMIN_CALLS_NO_OUTGOING_CALLS
Severity=Informational
Language=English
%1 is making no outgoing DRS RPC calls at this time.
.

MessageId=
SymbolicName=REPADMIN_CALLS_IN_PROGRESS
Severity=Informational
Language=English
%1 has %2!d! outgoing DRS RPC calls in progress:
.

MessageId=
SymbolicName=REPADMIN_CALLS_CALL_TYPE
Severity=Informational
Language=English
Call type: %1
.

MessageId=
SymbolicName=REPADMIN_CALLS_TARGET_SERVER
Severity=Informational
Language=English
Target server: %1
.

MessageId=
SymbolicName=REPADMIN_CALLS_HANDLE_INFO
Severity=Informational
Language=English
Handle info: bound %1!d! FromCache %2!d! InCache %3!d!
.

MessageId=
SymbolicName=REPADMIN_CALLS_THREAD_ID
Severity=Informational
Language=English
Client thread id: %1!d!
.

MessageId=
SymbolicName=REPADMIN_CALLS_TIME_STARTED
Severity=Informational
Language=English
Time call started: %1!S!
.

MessageId=
SymbolicName=REPADMIN_CALLS_CALL_TIMEOUT
Severity=Informational
Language=English
Call timeout: %1!d! minutes
.

MessageId=
SymbolicName=REPADMIN_CALLS_CALL_DURATION
Severity=Informational
Language=English
Call duration: %1!d! minutes and %2!d! seconds
.

MessageId=
SymbolicName=REPADMIN_DCLIST_RUNNING_SERVER_X
Severity=Informational
Language=English

repadmin running command %1 against server %2

.

MessageId=
SymbolicName=REPADMIN_VIEW_LIST_DC
Severity=Informational
Language=English
DC_LIST[%1!d!] = %2
.

MessageId=
SymbolicName=REPADMIN_XLIST_LDAP_EXTENDED_ERR
Severity=Informational
Language=English
Error: An LDAP lookup operation failed with the following error:%n
    LDAP Error %1!d!(0x%1!x!): %2
    Server Win32 Error %3!u!(0x%3!x!): %4
    Extended Information: %5
.

MessageId=
SymbolicName=REPADMIN_XLIST_WIN32_ERR_MSG
Severity=Informational
Language=English
Error: An error occured:
    Win32 Error %1!u!(0x%1!x!): %2
.

MessageId=
SymbolicName=REPADMIN_XLIST_WIN32_ERR
Severity=Informational
Language=English
Error: An Win32 Error %1!d!(0x%1!x!) occured.
.

MessageId=
SymbolicName=REPADMIN_XLIST_UNPARSEABLE_DC_LIST
Severity=Informational
Language=English
Repadmin couldn't parse the DC_LIST (%1) provided.  Try repadmin /listhelp
.

MessageId=
SymbolicName=REPADMIN_XLIST_LIST_HELP
Severity=Informational
Language=English

DC_LIST = { <DC_NAME> | * | <part_server_name>* | site:<SITE_NAME> | gc: | fsmo_<FSMO_TYPE>:<FSMO_DN> }
    Examples:
    "*" = All DCs in the enterprise.
    "part_server_name*" = would pick "part_server_name_dc_01" and "part_server_name_dc_02"
                but not server "part_server_diff_name".
    "site:east_site1" = All DCs in site "east_site1".
    "gc:" = All GCs in the enterprise.
    "fsmo_pdc:DC=my-corp-dom,DC=com" - repadmin runs against the PDC in the NC "DC=my-corp-dom,DC=com"
    "fsmo_istg:east_site1" would pick the ISTG for the east_site1 site.
    
FSMO_TYPE = dnm | schema | pdc | rid | im | istg
    NOTE: different types of FSMOs require different base DNs/RDNs.
    "fsmo_dnm:" - is an enterprise wide FSMO, and doesn't take any DN.
    "fsmo_schema:" - is an enterprise wide FSMO, and doesn't take any DN.
    "fsmo_pdc:" - is a domain specific FSMO, and takes the DN of the domain the user wants.
    "fsmo_rid:" - is a domain specific FSMO, and takes the DN of the domain the user wants.
    "fsmo_im:" - is a partition/NC specific FSMO, and takes the DN of the NC the user wants.
    "fsmo_istg:" - is a site specific quasi-FSMO, and takes the RDN of the site.
    
DC_NAME = { . | <server_dns> | <dc_dsa_guid> | <server_obj_rdn> | <dsa_dn> }
    . = Tells repadmin to try to pick one for you.
    server_dns = specifies a specific server by DNS.
    dc_dsa_guid = specifies a specific server by it's DSA GUID.
    server_obj_rdn = specifies a server by it's server object rdn (usually the same as it's NetBios name)
    dsa_dn = specifies a server by the DN of it's DSA object.

OBJ_LIST = { ncobj:<NC_NAME> | dsaobj: }
    "ncobj:" = means use the DN of NC Head specified in NC_NAME.
    "dsaobj:" = means use the DN of the DSA we're connected to.

NC_NAME = { config: | schema: | domain: }
    "config:" = Configuration Directory Partition.
    "schema:" = Schema Directory Partition.
    "domain:" = Domain Directory Partition for the Domain of the DC repadmin is running against.
        
OBJ_LIST OPTIONS = { /onelevel | /subtree} /filter:<Ldap_Filter>
    With these options, the showattr and viewlist commands can be used to cover a list of objects,
    instead of just a single object.    

NOTE: The * in wildcards are evaluated by LDAP.
.

MessageId=
SymbolicName=REPADMIN_XLIST_CANT_CONNECT
Severity=Informational
Language=English
Repadmin can't connect to a "home server", because of the following error.  Try specifying a different
home server with /homeserver:[dns name]
.

MessageId=
SymbolicName=REPADMIN_XLIST_CANT_LOCATE
Severity=Informational
Language=English
Repadmin can't locate a "home server" or determine our domain because of the following error.  Try specifying specific
"home server" with /homeserver:[dns name]
.

MessageId=
SymbolicName=REPADMIN_XLIST_CANT_RESOLVE_DC
Severity=Informational
Language=English
Repadmin experienced the following error trying to resolve the DC_NAME: %1
.

MessageId=
SymbolicName=REPADMIN_XLIST_CANT_RESOLVE_SITE
Severity=Informational
Language=English
Repadmin experienced the following error trying to resolve the SITE_NAME: %1
.

MessageId=
SymbolicName=REPADMIN_XLIST_CANT_GET_FSMO
Severity=Informational
Language=English
Repadmin experienced the following error trying get to the FSMO you requested in this subtree: %1
.

MessageId=
SymbolicName=REPADMIN_OLD_HELP
Severity=Informational
Language=English

These are the old deprecated commands, that are being phased out, but
are still presently supported for backwards compatibility.

Usage: repadmin <cmd> <args> [/u:{domain\\user}] [/pw:{password|*}]
                             [/rpc] [/ldap]

Deprecated <cmd>s & args:
     /sync <Naming Context> <Dest DC> <Source DC GUID> [/force] [/async]
          [/full] [/addref] [/allsources] [/readonly]
     /propcheck <Naming Context> <Originating DC Invocation ID>
          <Originating USN> [DC from which to enumerate host DCs]
     /getchanges NamingContext [SourceDC] [/cookie:<file>]
          [/atts:<att1>,<att2>,...]
     /getchanges NamingContext [DestDC] SourceDCObjectGUID
          [/verbose] [/statistics] [/noincremental] [/objectsecurity]
          [/ancestors] [/atts:<att1>,<att2>,...] [/filter:<ldap filter>]
     /showreps [Naming Context] [DC [Source DC object GUID]] [/verbose]
          [/nocache] [/repsto] [/conn] [/all]
     /showvector <Naming Context> [DC] [/nocache] [/latency]
     /showmeta <Object DN> [DC] [/nocache] [/linked]

These deprecated commands are replaced with these improved commands:
     /sync       -> /repl or /replicate
     /propcheck  -> /checkprop
     /getchanges -> /showchanges
     /showreps   -> /showrepl
     /showvector -> /showutdvec
     /showmeta   -> /showobjmeta

Note:- <Dest DC>, <Source DC>, <DC> : Names of the appropriate servers
       <Naming Context> is the Distinguished Name of the root of the NC
              Example: DC=My-Domain,DC=Microsoft,DC=Com
Note: Text (Naming Context names, server names, etc) with International or
      Unicode characters will only display correctly if appropriate fonts and
      language support are loaded
.

MessageId=
SymbolicName=REPADMIN_UNSUPPORTED_CSV_CMD
Severity=Informational
Language=English
This %1 command does not support Comma Seperated Values (/csv) output mode.
.


MessageId=
SymbolicName=REPADMIN_CSV_REPADMIN_C0
Severity=Informational
Language=English
RepadminParserMessage
.

MessageId=
SymbolicName=REPADMIN_CSV_REPADMIN_C1
Severity=Informational
Language=English
Destination DC Site
.

MessageId=
SymbolicName=REPADMIN_CSV_REPADMIN_C2
Severity=Informational
Language=English
Destination DC
.

MessageId=
SymbolicName=REPADMIN_CSV_REPADMIN_C3
Severity=Informational
Language=English
Message
.

MessageId=
SymbolicName=REPADMIN_CSV_SHOWREPL_C3
Severity=Informational
Language=English
Naming Context
.

MessageId=
SymbolicName=REPADMIN_CSV_SHOWREPL_C4
Severity=Informational
Language=English
Source DC Site
.

MessageId=
SymbolicName=REPADMIN_CSV_SHOWREPL_C5
Severity=Informational
Language=English
Source DC
.

MessageId=
SymbolicName=REPADMIN_CSV_SHOWREPL_C6
Severity=Informational
Language=English
Transport Type
.

MessageId=
SymbolicName=REPADMIN_CSV_SHOWREPL_C7
Severity=Informational
Language=English
Number of Failures
.

MessageId=
SymbolicName=REPADMIN_CSV_SHOWREPL_C8
Severity=Informational
Language=English
Last Failure Time
.

MessageId=
SymbolicName=REPADMIN_CSV_SHOWREPL_C9
Severity=Informational
Language=English
Last Success Time
.

MessageId=
SymbolicName=REPADMIN_CSV_SHOWREPL_C10
Severity=Informational
Language=English
Last Failure Status
.

MessageId=
SymbolicName=REPADMIN_CSV_HELP
Severity=Informational
Language=English
The /csv mode makes the /showrepl command print in a Comma Seperated Values (CSV)%0 
format for import into Microsoft Excel or programatic analysis.  The output format%0
(the order of the columns) is rigid and will not change from release to release.
The CSV output is printed to stdout and can be redirected to a file.  Critical
errors will be printed in a CSV friendly format to stdout, and also printed in a 
user friendly format to the screen via stderr.

The repadmin commands supporting /csv output:
    /showrepl

The generic format is:
    column_1  -  test_specifier + printing_type.
    column_2  -  site of the dc the test is running against.
    column_3  -  dc the test is running against.
    column_4+ -  columns_4 and beyond are specific to the test.
    
    test_specifier = repadmin_ | showrepl_
    printing_type  = INFO | ERROR | COLUMNS

Example 1: "repadmin /showrepl dc-22 /csv"
    Output:
    showrepl_COLUMNS,Destination DC Site,Destination DC,Naming Context,Source DC Site,Source DC, ...
    showrepl_INFO,WestCoastSite,DC-22,"DC=myfavoritecorp,DC=com",EastCoastSite,DC-35,...

Example 2: "repadmin /showrepl down-dc /csv > file.csv"
    Output To Screen (note redirect):
    Repadmin experienced the following error trying to resolve the DC_NAME: down-dc
    
    file.csv:
    repadmin_ERROR,-,-,Repadmin experienced the following error trying to resolve the DC_NAME: down-dc

This /csv mode works in conjunction with the lists of DCs as well.  See /listhelp for ways in
which you can specify subsets of servers to target.
.


MessageId=
SymbolicName=REPADMIN_HELP_NO_HELP_IN_CSV_MODE
Severity=Informational
Language=English
There is no help in csv mode.  Do not use the "/csv" option, when listing help.
If you want csv mode help, "repadmin /csvhelp" will give you help.
.

MessageId=
SymbolicName=REPADMIN_VIEW_LIST_OBJ
Severity=Informational
Language=English
    OBJ_LIST[%1!d!] = %2
.

MessageId=
SymbolicName=REPADMIN_OBJ_DUMP_DN
Severity=Informational
Language=English
DN: %1
.

MessageId=
SymbolicName=REPADMIN_OBJ_DUMP_RANGED
Severity=Informational
Language=English
    %1!d!+> %2: %0
.

MessageId=
SymbolicName=REPADMIN_NEVER
Severity=Informational
Language=English
(never)%0
.

MessageId=
SymbolicName=REPADMIN_REPLSUM_START_TIME
Severity=Informational
Language=English
Replication Summary Start Time: %1!S!
.

MessageId=
SymbolicName=REPADMIN_REPLSUM_START_COLLECTING
Severity=Informational
Language=English
Beginning data collection for replication summary, this may take awhile:
  ....%0
.

;//" 123456789012345  >xxd.xxh:xxm:xxs fails/total  %%%%  error "

MessageId=
SymbolicName=REPADMIN_REPLSUM_BY_DEST_HDR
Severity=Informational
Language=English
Destination DC    largest delta    fails/total  %%%%  error
.

MessageId=
SymbolicName=REPADMIN_REPLSUM_BY_SRC_HDR
Severity=Informational
Language=English
Source DC           largest delta  fails/total  %%%%  error
.

MessageId=
SymbolicName=REPADMIN_REPLSUM_OP_ERRORS_HDR
Severity=Informational
Language=English
Experienced the following operational errors trying to retrieve replication information:
.

MessageId=
SymbolicName=REPADMIN_REPLSUM_OP_ERROR
Severity=Informational
Language=English
 %1!11d! - %2
.

MessageId=
SymbolicName=REPADMIN_NONE
Severity=Informational
Language=English
(none)
.

MessageId=
SymbolicName=REPADMIN_OBJ_LIST_BAD_DN
Severity=Informational
Language=English
Can not locate the object for this DN: %1
.

MessageId=
SymbolicName=REPADMIN_OBJ_LIST_BAD_SYNTAX
Severity=Informational
Language=English
User provided a bad syntax for the OBJ_LIST please see repadmin /listhelp.
.

MessageId=
SymbolicName=REPADMIN_OBJ_DUMP_MORE_VALUES
Severity=Informational
Language=English
more values exist...%0
.

MessageId=
SymbolicName=REPADMIN_TEARDOWN_PREEMPTED
Severity=Informational
Language=English
Removal of partition %1 was preempted by higher priority work. Performing the removal
of the partition again.
.

MessageId=
SymbolicName=REPADMIN_REBUILD_ADD_PREEMPTED
Severity=Informational
Language=English
As part of the rebuild of %1, a replica link was added to a new source %2.
Synchronization with that source was preempted due to higher priority work
in another partition. Performing the synchronization again.
.

MessageId=
SymbolicName=REPADMIN_REBUILD_TEARDOWN_IN_PROGRESS
Severity=Informational
Language=English
Removal of partition %1 is in progress...
.

MessageId=
SymbolicName=REPADMIN_REBUILD_FULL_SYNC_IN_PROGRESS
Severity=Informational
Language=English
Full sync of partition %1 is in progress. Please be patient. This step may take
many hours on a large partition. You can monitor the progress of the full sync
using repadmin /showreps /v in another window.
.


;
;// Severity=Error Messages (Range starts at 3000)
;

MessageId=3000
SymbolicName=REPADMIN_PASSWORD_TOO_LONG
Severity=Error
Language=English
The password is too long.
.

MessageId=
SymbolicName=REPADMIN_DOMAIN_BEFORE_USER
Severity=Error
Language=English
User name must be prefixed by domain name.
.

MessageId=
SymbolicName=REPADMIN_PASSWORD_NEEDS_USERNAME
Severity=Error
Language=English
Password must be accompanied by user name.
.

MessageId=
SymbolicName=REPADMIN_FAILED_TO_READ_CONSOLE_MODE
Severity=Error
Language=English
Failed to query the console mode.
.

MessageId=
SymbolicName=REPADMIN_PRINT_STRING_ERROR
Severity=Error
Language=English
%1
.

MessageId=
SymbolicName=REPADMIN_GENERAL_UNKNOWN_OPTION
Severity=Error
Language=English
Unknown option "%1".
.
MessageId=
SymbolicName=REPADMIN_GENERAL_NO_MEMORY
Severity=Error
Language=English
Repadmin failed to allocate memory.
.

MessageId=
SymbolicName=REPADMIN_SHOWCERT_STORE_ACCESS_DENIED
Severity=Error
Language=English
Access to store denied.
Try authenticating (net use) to the system using an administrator account.
.

MessageId=
SymbolicName=REPADMIN_SHOWCERT_CERTOPENSTORE_FAILED
Severity=Error
Language=English
Access to store denied.
CertOpenStore on remote My store failed! Error is %1
.

MessageId=
SymbolicName=REPADMIN_SHOWCERT_DC_CERT_NOT_FOUND
Severity=Error
Language=English
Domain Controller Certificate was not found.
.

MessageId=
SymbolicName=REPADMIN_SHOWCERT_CERT_NO_ALT_SUBJ
Severity=Error
Language=English
Certificate has no alt subject name.
.

MessageId=
SymbolicName=REPADMIN_SHOWCERT_CANT_DECODE_CERT
Severity=Error
Language=English
Can't decode alt subject name, encountered error: 0x%1!x! %2
.

MessageId=
SymbolicName=REPADMIN_SHOWCERT_CAFINDCERTTYPEBYNAME_FAILURE
Severity=Error
Language=English
CAFindCertTypeByName failed, error 0x%1!x!
.

MessageId=
SymbolicName=REPADMIN_SHOWCERT_CAGETCERTTYPEPROPERTY_FAILURE
Severity=Error
Language=English
CAGetCertTypeProperty failed, error 0x%1!x!
.

MessageId=
SymbolicName=REPADMIN_SHOWCERT_CAFREECERTTYPEPROPERTY_FAILURE
Severity=Error
Language=English
CAFreeCertTypeProperty failed, error 0x%1!x!
.

MessageId=
SymbolicName=REPADMIN_SHOWCERT_CACLOSECERTTYPE_FAILURE
Severity=Error
Language=English
CACloseCertType failed, error 0x%1!x!
.

MessageId=
SymbolicName=REPADMIN_BIND_FAILED
Severity=Error
Language=English
DsBindWithCred to %1 failed with status %0
.

MessageId=
SymbolicName=REPADMIN_BIND_FAILED_STATUS
Severity=Error
Language=English
DsBindWithCred to %1 failed with status %2!d!
.

MessageId=
SymbolicName=REPADMIN_GENERAL_ERR
Severity=Error
Language=English
%1!d! (0x%2!x!):
    %3
.

MessageId=
SymbolicName=REPADMIN_GENERAL_ERR_NUM
Severity=Error
Language=English
%1!d! (0x%2!x!):
.

MessageId=
SymbolicName=REPADMIN_BIND_UNBIND_FAILED
Severity=Error
Language=English
DsUnBind() failed with status %0
.

MessageId=
SymbolicName=REPADMIN_MOD_GUID_CONVERT_FAILED
Severity=Error
Language=English
Error converting GUID %1, error %0
.

MessageId=
SymbolicName=REPADMIN_DEL_ONE_REPSTO
Severity=Error
Language=English
Either a single Reps-To DC or "/nosource" must be specified, but not both. 
.

MessageId=
SymbolicName=REPADMIN_GENERAL_FUNC_FAILED
Severity=Error
Language=English
%1() failed with status %0
.

MessageId=
SymbolicName=REPADMIN_GENERAL_FUNC_FAILED_STATUS
Severity=Error
Language=English
%1() failed with status %2!d!
.

MessageId=
SymbolicName=REPADMIN_GENERAL_FUNC_FAILED_ARGS
Severity=Error
Language=English
%1( %2 ) failed with status %0
.

MessageId=
SymbolicName=REPADMIN_SYNC_SRC_GUID_OR_ALLSRC
Severity=Error
Language=English
Either a single source GUID or "/allsources" must be specified.
.

MessageId=
SymbolicName=REPADMIN_GENERAL_LDAP_ERR
Severity=Error
Language=English
[%1!S!, %2!d!] LDAP error %3!d! (%4) Win32 Err %5!d!.
.

MessageId=
SymbolicName=REPADMIN_PRINT_NO_NC
Severity=Error
Language=English
Must specify a Naming Context.
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_NO_DSA
Severity=Error
Language=English
Must specify a target DC.
.

MessageId=
SymbolicName=REPADMIN_GENERAL_LDAP_UNAVAILABLE
Severity=Error
Language=English
Cannot open LDAP connection to %1.
.

MessageId=
SymbolicName=REPADMIN_GENERAL_LDAP_UNAVAILABLE_2
Severity=Error
Language=English
Cannot open LDAP connection to %1 (%2).
.

MessageId=
SymbolicName=REPADMIN_GENERAL_LDAP_UNAVAILABLE_LOCALHOST
Severity=Error
Language=English
Cannot open LDAP connection to localhost.
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_NO_INSTANCES_OF_NC
Severity=Error
Language=English
No instances of this NC found -- please check the NC name and your credentials.
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_REGISTRY_BIND_FAILED
Severity=Error
Language=English
Failed to bind to registry on %1, error %0
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_OPEN_DS_REG_KEY_FAILED
Severity=Error
Language=English
Failed to open DS registry key on %1, error %0
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_COULDNT_SET_REGISTRY
Severity=Error
Language=English
Could not set registry value, error %0
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_DEL_REPLICA_LINK_FAILED
Severity=Error
Language=English
Failed to delete replica link, error %0
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_INVALID_GUID_NO_CR
Severity=Error
Language=English
<invalid GUID>%0
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_INVALID_DN_NO_CR
Severity=Error
Language=English
<invalid DN>%0
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_COULDNT_READ_COOKIE
Severity=Error
Language=English
Couldn't read cookie from file %1
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_COULDNT_WRITE_COOKIE
Severity=Error
Language=English
Couldn't write cookie to file %1
.

MessageId=
SymbolicName=REPADMIN_GETCHANGES_COULDNT_OPEN_COOKIE
Severity=Error
Language=English
Couldn't open cookie file %1
.


MessageId=
SymbolicName=REPADMIN_SHOWMETA_NO_OBJ_SPECIFIED
Severity=Error
Language=English
Must specify an object.
.

MessageId=
SymbolicName=REPADMIN_SHOWISM_SITE_CONNECTIVITY_NULL
Severity=Error
Language=English
I_ISMGetConnectivity() returned NULL for site connectivity!
.

MessageId=
SymbolicName=REPADMIN_SHOWISM_SUPPLY_TRANS_DN_HELP
Severity=Error
Language=English
Must supply zero or one transport DN.
.

MessageId=
SymbolicName=REPADMIN_REPLCTRL_BAIL
Severity=Error
Language=English
Fatal error at line %1!d!, file %2!S!
.

MessageId=
SymbolicName=REPADMIN_REPLCTRL_BAIL_ON_NULL
Severity=Error
Language=English
%1!S! had unexpected null value at line %2!d!, file %3!S!
.

MessageId=
SymbolicName=REPADMIN_REPLCTRL_BAIL_ON_FAILURE
Severity=Error
Language=English
%1!S! had unexpected failure value at line %2!d!, file %3!S!
.

MessageId=
SymbolicName=REPADMIN_REPLCTRL_ERROR_IN_BER_PRINTF
Severity=Error
Language=English
Error in ber_printf.
.

MessageId=
SymbolicName=REPADMIN_REPLCTRL_ERROR_IN_BER_FLATTEN
Severity=Error
Language=English
Error in ber_flatten.
.

MessageId=
SymbolicName=REPADMIN_REPLCTRL_NO_REPL_SERVER_CONTROL_RET
Severity=Error
Language=English
Did not get a replication server control back.
.

MessageId=
SymbolicName=REPADMIN_SHOWMSG_NO_MSG_NUM
Severity=Error
Language=English
Must specify message number.
.

MessageId=
SymbolicName=REPADMIN_SHOWMSG_INVALID_MSG_ID
Severity=Error
Language=English
Invalid message ID, "%1".
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_CONTACTING_SERVER_ERR
Severity=Error
Language=English
Error contacting server %1 (network error): %0
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_ERR_ISSUING_REPL
Severity=Error
Language=English
Error issuing replication: %0
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_SERVER_BAD_TOPO_INCOMPLETE
Severity=Error
Language=English
The following server could not be reached (topology incomplete):
    %1
.

MessageId=
SymbolicName=REPADMIN_GENERAL_UNKNOWN_ERROR
Severity=Error
Language=English
Unknown error.
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_EXITED_FATALLY_ERR
Severity=Error
Language=English
SyncAll exited with fatal Win32 error: %0
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_INVALID_CMDLINE
Severity=Error
Language=English
Invalid commandline; use repadmin /SyncAll /h for help.
.

MessageId=
SymbolicName=REPADMIN_SYNCALL_ERRORS_HDR
Severity=Error
Language=English
SyncAll reported the following errors:
.

MessageId=
SymbolicName=REPADMIN_GENERAL_INVALID_ARGS
Severity=Error
Language=English
Invalid arguments.
.

MessageId=
SymbolicName=REPADMIN_OPTIONS_CANT_ADD_REMOVE_SAME_OPTION
Severity=Error
Language=English
Cannot add & remove same option.
.

MessageId=
SymbolicName=REPADMIN_GENERAL_SITE_NOT_FOUND
Severity=Error
Language=English
The site %1 was not found!
.

MessageId=
SymbolicName=REPADMIN_SHOWSIG_RETIRED_SIGS_UNRECOGNIZED
Severity=Error
Language=English
Format of retiredReplDsaSignatures is unrecognized.
.

MessageId=
SymbolicName=REPADMIN_SHOWCONN_INVALID_DISTNAME_BIN_VAL
Severity=Error
Language=English
invalid distname binary value %1
.

MessageId=
SymbolicName=REPADMIN_SHOWCONN_INVALID_DISTNAME_BIN_LEN
Severity=Error
Language=English
unexpected distname binary length %1!d!
.

MessageId=
SymbolicName=REPADMIN_SHOWCONN_SPECIFY_RDN_DN_OR_GUID
Severity=Error
Language=English
Please specify one of: RDN, DN, or GUID.
.

MessageId=
SymbolicName=REPADMIN_SHOWCONN_AMBIGUOUS_NAME
Severity=Error
Language=English
Ambiguous name: more than one server exists with RDN %1.
.

MessageId=
SymbolicName=REPADMIN_DSAGUID_NEED_INVOC_ID
Severity=Error
Language=English
Must specify invocation ID to translate
.

MessageId=
SymbolicName=REPADMIN_REMOVELINGERINGOBJECTS_SUCCESS
Severity=Error
Language=English
RemoveLingeringObjects sucessfull on %1.
.

MessageId=
SymbolicName=REPADMIN_PLEASE_USE_DOMAIN_NAMING_FSMO
Severity=Error
Language=English
The set operation is allowed on the Domain Naming Master Only.
Your Domain Naming Master is: %1
.

MessageId=
SymbolicName=REPADMIN_INCOMPATIBLE_WRITEABILITY
Severity=Error
Language=English
Partition %1 does not have expected writeability for source %2.
.
