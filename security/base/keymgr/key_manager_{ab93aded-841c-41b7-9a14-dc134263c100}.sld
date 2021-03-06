<?xml version="1.0" encoding="UTF-16"?>
<!DOCTYPE DCARRIER SYSTEM "mantis.dtd">
<DCARRIER CarrierRevision="1">
	<TOOLINFO ToolName="iCat"><![CDATA[<?xml version="1.0"?>
<!DOCTYPE TOOL SYSTEM "tool.dtd">
<TOOL>
	<CREATED><NAME>iCat</NAME><VSGUID>{97b86ee0-259c-479f-bc46-6cea7ef4be4d}</VSGUID><VERSION>1.0.0.452</VERSION><BUILD>452</BUILD><DATE>3/2/2001</DATE></CREATED><LASTSAVED><NAME>iCat</NAME><VSGUID>{97b86ee0-259c-479f-bc46-6cea7ef4be4d}</VSGUID><VERSION>1.0.0.452</VERSION><BUILD>452</BUILD><DATE>7/31/2001</DATE></LASTSAVED></TOOL>
]]></TOOLINFO><COMPONENT Revision="8" Visibility="1000" MultiInstance="0" Released="1" Editable="1" HTMLFinal="0" ComponentVSGUID="{AB93ADED-841C-41B7-9A14-DC134263C100}" ComponentVIGUID="{D2DEECD4-16D6-4FF5-9502-FC65E3354344}" PlatformGUID="{B784E719-C196-4DDB-B358-D9254426C38D}" RepositoryVSGUID="{8E0BE9ED-7649-47F3-810B-232D36C430B4}"><HELPCONTEXT src="X:\nt\ds\security\base\keymgr\keymgr_component_description.htm">&lt;!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 3.2 Final//EN"&gt;
&lt;HTML DIR="LTR"&gt;&lt;HEAD&gt;
&lt;META HTTP-EQUIV="Content-Type" Content="text/html; charset=Windows-1252"&gt;
&lt;TITLE&gt;Keyring Component Description&lt;/TITLE&gt;
&lt;style type="text/css"&gt;@import url(td.css);&lt;/style&gt;&lt;/HEAD&gt;
&lt;BODY TOPMARGIN="0"&gt;
&lt;H1&gt;&lt;A NAME="_keyring_component_description"&gt;&lt;/A&gt;&lt;SUP&gt;&lt;/SUP&gt;Keyring Component Description&lt;/H1&gt;

&lt;P&gt;Microsoft Windows NT provides a single sign-on experience for users by allowing network providers to take a user’s credentials at login and authenticate the user to other targets. This approach might not be sufficient in every case, for example, if a user connects to an untrusted domain or uses alternate credentials to access a specific resource. Windows XP addresses this problem through the Windows Stored User Names and Passwords component, sometimes referred to as Key Manager or Keyring. This component provides credential storage and management functionality. &lt;/P&gt;

&lt;P&gt;The Store User Names and Passwords component provides the user with a secure roamable store for credentials. Roamable implies that if the user is part of a domain with roaming profiles the credentials can be saved as part of that roaming profile. This mechanism enables users to use the Stored User Names and Passwords feature anywhere they can access their profiles. &lt;/P&gt;

&lt;H1&gt;Configuring the Component&lt;/H1&gt;

&lt;P&gt;This component requires no configuration.&lt;/P&gt;

&lt;P&gt;The Credential Manager uses two registry values to control per-machine policy. &lt;/P&gt;

&lt;P&gt;The following table shows the registry values under the &lt;code class="ce"&gt;HKLM\System\CurrentControlSet\Control\Lsa&lt;/code&gt; registry key:&lt;/P&gt;

&lt;P class="fineprint"&gt;&lt;/P&gt;

&lt;TABLE&gt;

&lt;TR VALIGN="top"&gt;
&lt;TH width=29%&gt;Registry Value&lt;/TH&gt;
&lt;TH width=22%&gt;Type&lt;/TH&gt;
&lt;TH width=49%&gt;Description&lt;/TH&gt;
&lt;/TR&gt;

&lt;TR VALIGN="top"&gt;
&lt;TD width=29%&gt;&lt;B&gt;TargetInfoCacheSize&lt;/B&gt;&lt;/TD&gt;
&lt;TD width=22%&gt;&lt;B&gt;REG_DWORD&lt;/B&gt;&lt;/TD&gt;
&lt;TD width=49%&gt;Specifies the number of entries in the target information cache. The credential manager manages a per-logon session cache of mappings from target name to target info. The &lt;B&gt;CredGetTargetInfo&lt;/B&gt; function obtains its information from the cache. If this value is set too small, other applications running under the logon session can flush a cache entry (by adding their own) before a cache entry can be used. If this value is set too large, an excessive amount of memory will be consumed. The default value is 1000 entries. The minimum value is 1.&lt;/TD&gt;
&lt;/TR&gt;

&lt;TR VALIGN="top"&gt;
&lt;TD width=29%&gt;&lt;B&gt;DisableDomainCreds&lt;/B&gt;&lt;/TD&gt;
&lt;TD width=22%&gt;&lt;B&gt;REG_DWORD&lt;/B&gt;&lt;/TD&gt;
&lt;TD width=49%&gt;Specifies whether domain credentials CRED_TYPE_DOMAIN_* may be read or written on this machine. If this value is set to 0, domain credentials function normally. If this value is set to 1, domain credentials cannot be written (a STATUS_NO_SUCH_LOGON_SESSION error message is returned to any API that attempts to write such a credential) or read (any such credential is silently ignored).&lt;/TD&gt;
&lt;/TR&gt;
&lt;/TABLE&gt;&lt;BR&gt;

&lt;P class="fineprint"&gt;&lt;/P&gt;

&lt;H1&gt;For More Information&lt;/H1&gt;

&lt;P&gt;Additional information about this component can be found in the product online Help.&lt;/P&gt;

&lt;/BODY&gt;
&lt;/HTML&gt;
</HELPCONTEXT><DISPLAYNAME>Key Manager</DISPLAYNAME><VERSION>1.0</VERSION><DESCRIPTION>User interface for manipulating the credential manager stored credentials</DESCRIPTION><COPYRIGHT>2000 Microsoft Corp.</COPYRIGHT><VENDOR>Microsoft Corp.</VENDOR><OWNERS>georgema</OWNERS><AUTHORS>georgema</AUTHORS><DATECREATED>3/2/2001</DATECREATED><DATEREVISED>7/31/2001</DATEREVISED><RESOURCE ResTypeVSGUID="{E66B49F6-4A35-4246-87E8-5C1A468315B5}" BuildTypeMask="819" Name="File(819):&quot;%11%&quot;,&quot;keymgr.dll&quot;"><PROPERTY Name="DstPath" Format="String">%11%</PROPERTY><PROPERTY Name="DstName" Format="String">keymgr.dll</PROPERTY><PROPERTY Name="NoExpand" Format="Boolean">0</PROPERTY><DISPLAYNAME>keymgr.dll</DISPLAYNAME><DESCRIPTION>Credential Manager Tool</DESCRIPTION></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep(819):&quot;File&quot;,&quot;msvcrt.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">msvcrt.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep(819):&quot;File&quot;,&quot;netapi32.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">netapi32.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep(819):&quot;File&quot;,&quot;advapi32.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">advapi32.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep(819):&quot;File&quot;,&quot;dnsapi.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">dnsapi.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep(819):&quot;File&quot;,&quot;kernel32.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">kernel32.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep(819):&quot;File&quot;,&quot;user32.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">user32.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep(819):&quot;File&quot;,&quot;shell32.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">shell32.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep(819):&quot;File&quot;,&quot;gdi32.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">gdi32.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep(819):&quot;File&quot;,&quot;shlwapi.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">shlwapi.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep(819):&quot;File&quot;,&quot;crypt32.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">crypt32.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep(819):&quot;File&quot;,&quot;rpcrt4.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">rpcrt4.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep(819):&quot;File&quot;,&quot;credui.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">credui.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep(819):&quot;File&quot;,&quot;comctl32.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">comctl32.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{2C10DB69-39AB-48A4-A83F-9AB3ACBA7C45}" BuildTypeMask="819" Name="RegKey(819):&quot;HKEY_CLASSES_ROOT\.psw&quot;,&quot;&quot;" Localize="0"><PROPERTY Name="KeyPath" Format="String">HKEY_CLASSES_ROOT\.psw</PROPERTY><PROPERTY Name="ValueName" Format="String"></PROPERTY><PROPERTY Name="RegValue" Format="String">PSWFile</PROPERTY><PROPERTY Name="RegType" Format="Integer">1</PROPERTY><PROPERTY Name="RegOp" Format="Integer">1</PROPERTY><PROPERTY Name="RegCond" Format="Integer">1</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{2C10DB69-39AB-48A4-A83F-9AB3ACBA7C45}" BuildTypeMask="819" Name="RegKey(819):&quot;HKEY_CLASSES_ROOT\PSWFile&quot;,&quot;&quot;" Localize="0"><PROPERTY Name="KeyPath" Format="String">HKEY_CLASSES_ROOT\PSWFile</PROPERTY><PROPERTY Name="ValueName" Format="String"></PROPERTY><PROPERTY Name="RegValue" Format="String">Password Backup</PROPERTY><PROPERTY Name="RegType" Format="Integer">1</PROPERTY><PROPERTY Name="RegOp" Format="Integer">1</PROPERTY><PROPERTY Name="RegCond" Format="Integer">1</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{2C10DB69-39AB-48A4-A83F-9AB3ACBA7C45}" BuildTypeMask="819" Name="RegKey(819):&quot;HKEY_CLASSES_ROOT\PSWFile&quot;,&quot;NoOpen&quot;" Localize="0"><PROPERTY Name="KeyPath" Format="String">HKEY_CLASSES_ROOT\PSWFile</PROPERTY><PROPERTY Name="ValueName" Format="String">NoOpen</PROPERTY><PROPERTY Name="RegValue" Format="String"></PROPERTY><PROPERTY Name="RegType" Format="Integer">1</PROPERTY><PROPERTY Name="RegOp" Format="Integer">1</PROPERTY><PROPERTY Name="RegCond" Format="Integer">1</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep(819):&quot;File&quot;,&quot;hhctrl.ocx&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">hhctrl.ocx</PROPERTY></RESOURCE><GROUPMEMBER GroupVSGUID="{C39397C0-E383-46D3-B99A-3F464054C480}"/></COMPONENT></DCARRIER>
