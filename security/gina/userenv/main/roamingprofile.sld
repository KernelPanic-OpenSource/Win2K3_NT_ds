<?xml version="1.0" encoding="UTF-16"?>
<!DOCTYPE DCARRIER SYSTEM "mantis.dtd">
<DCARRIER CarrierRevision="1">
	<TOOLINFO ToolName="iCat"><![CDATA[<?xml version="1.0"?>
<!DOCTYPE TOOL SYSTEM "tool.dtd">
<TOOL>
	<CREATED><NAME>iCat</NAME><VSGUID>{592269d9-a8a0-4005-97da-70b92ee4ed5f}</VSGUID><VERSION>1.0.0.438</VERSION><BUILD>438</BUILD><DATE>12/7/2000</DATE></CREATED><LASTSAVED><NAME>iCat</NAME><VSGUID>{97b86ee0-259c-479f-bc46-6cea7ef4be4d}</VSGUID><VERSION>1.0.0.452</VERSION><BUILD>452</BUILD><DATE>5/11/2001</DATE></LASTSAVED></TOOL>
]]></TOOLINFO><COMPONENT Revision="5" Visibility="1000" MultiInstance="0" Released="1" Editable="1" HTMLFinal="0" ComponentVSGUID="{019893D6-0D9D-4893-8B52-9630E85858A3}" ComponentVIGUID="{97A04B78-8787-4E7F-B4D0-555FE1EF1426}" PlatformGUID="{B784E719-C196-4DDB-B358-D9254426C38D}" RepositoryVSGUID="{8E0BE9ED-7649-47F3-810B-232D36C430B4}"><HELPCONTEXT src="\\mantisqa\ovrdaily\mantis\hlp\santanuc\_roaming_user_component_description.htm">&lt;!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 3.2 Final//EN"&gt;
&lt;HTML DIR="LTR"&gt;&lt;HEAD&gt;
&lt;META HTTP-EQUIV="Content-Type" Content="text/html; charset=Windows-1252"&gt;
&lt;TITLE&gt;Roaming User Component Description&lt;/TITLE&gt;
&lt;style type="text/css"&gt;@import url(td.css);&lt;/style&gt;&lt;/HEAD&gt;
&lt;BODY TOPMARGIN="0"&gt;
&lt;H1&gt;&lt;A NAME="_roaming_user_component_description"&gt;&lt;/A&gt;&lt;SUP&gt;&lt;/SUP&gt;Roaming User Component Description&lt;/H1&gt;

&lt;P&gt;A roaming user profile is a type of profile that is stored centrally on a server. This profile is available every time a user logs on to any computer on the network and any changes made to a roaming user profile are updated on the server.&lt;/P&gt;

&lt;P&gt;A roaming profile is created the first time a user logs on to a computer, in the same way as a local user profile. When the user logs off, the local profile is copied to the location specified in the profile path on the user object. If a profile already exists on the server, the local profile is merged with the server copy. For subsequent logons, the contents of the local cached profile are compared with the copy of the profile on the server, and the two profiles are merged.&lt;/P&gt;

&lt;P&gt;Roaming user profiles are merged at the file level. The merged profile contains the superset of files that are in the local computer and server copies of the user’s profile. In the case where the same file is in both the local and server copy of the profile, the file that was modified most recently is used. This implies that new files are not deleted, and updated versions of existing files are not overwritten.&lt;/P&gt;

&lt;H1&gt;Component Configuration&lt;/H1&gt;

&lt;P&gt;There are no configuration requirements associated with this component. &lt;/P&gt;

&lt;H1&gt;For More Information&lt;/H1&gt;

&lt;P&gt;Fore more information on user profile, visit this &lt;A HREF="http://msdn.microsoft.com/library/psdk/sysmgmt/profile_3pkj.htm"&gt;Microsoft Web site&lt;/A&gt;.&lt;/P&gt;

&lt;/BODY&gt;
&lt;/HTML&gt;
</HELPCONTEXT><DISPLAYNAME>Roaming Profile</DISPLAYNAME><VERSION>1.0</VERSION><DESCRIPTION>Manage Roaming/Local Profile For User</DESCRIPTION><COPYRIGHT>2000 Microsoft Corp.</COPYRIGHT><VENDOR>Microsoft Corp.</VENDOR><OWNERS>profdev</OWNERS><AUTHORS>santanuc</AUTHORS><DATECREATED>12/7/2000</DATECREATED><DATEREVISED>5/11/2001</DATEREVISED><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep(819):&quot;File&quot;,&quot;userenv.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">userenv.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{2C10DB69-39AB-48A4-A83F-9AB3ACBA7C45}" BuildTypeMask="819" Name="RegKey(819):&quot;HKEY_LOCAL_MACHINE\Software\Policies\Microsoft\Windows\System&quot;,&quot;LocalProfile&quot;" Localize="0"><PROPERTY Name="KeyPath" Format="String">HKEY_LOCAL_MACHINE\Software\Policies\Microsoft\Windows\System</PROPERTY><PROPERTY Name="ValueName" Format="String">LocalProfile</PROPERTY><PROPERTY Name="RegValue" Format="Integer">0</PROPERTY><PROPERTY Name="RegType" Format="Integer">4</PROPERTY><PROPERTY Name="RegOp" Format="Integer">1</PROPERTY><PROPERTY Name="RegCond" Format="Integer">1</PROPERTY><DESCRIPTION>Allows Local/Roaming Profile in the system</DESCRIPTION></RESOURCE><GROUPMEMBER GroupVSGUID="{E01B4103-3883-4FE8-992F-10566E7B796C}"/><GROUPMEMBER GroupVSGUID="{D8142082-243E-4C8C-B98B-3290C50D93C7}"/></COMPONENT></DCARRIER>
