<?xml version="1.0" encoding="UTF-16"?>
<!DOCTYPE DCARRIER SYSTEM "mantis.dtd">
<DCARRIER CarrierRevision="1">
	<TOOLINFO ToolName="iCat"><![CDATA[<?xml version="1.0"?>
<!DOCTYPE TOOL SYSTEM "tool.dtd">
<TOOL>
	<CREATED><NAME>iCat</NAME><VSGUID>{2c9621d4-253b-4e60-adde-aef1d751c55c}</VSGUID><VERSION>1.0.0.364</VERSION><BUILD>364</BUILD><DATE>9/12/2000</DATE></CREATED><LASTSAVED><NAME>iCat</NAME><VSGUID>{97b86ee0-259c-479f-bc46-6cea7ef4be4d}</VSGUID><VERSION>1.0.0.452</VERSION><BUILD>452</BUILD><DATE>4/27/2001</DATE></LASTSAVED></TOOL>
]]></TOOLINFO><COMPONENT Revision="4" Visibility="1000" MultiInstance="0" Released="1" Editable="1" HTMLFinal="0" ComponentVSGUID="{76971FAD-8973-4BD3-B4E5-557031A93A1A}" ComponentVIGUID="{1FFF5276-DD44-4513-BFB4-64510ED6B7F8}" PlatformGUID="{B784E719-C196-4DDB-B358-D9254426C38D}" RepositoryVSGUID="{8E0BE9ED-7649-47F3-810B-232D36C430B4}"><HELPCONTEXT src="G:\NewNTBug\ds\adsi\nw312\_adsi_nwcompat_provider_component_description.htm">&lt;!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 3.2 Final//EN"&gt;
&lt;HTML DIR="LTR"&gt;&lt;HEAD&gt;
&lt;META HTTP-EQUIV="Content-Type" Content="text/html; charset=Windows-1252"&gt;
&lt;TITLE&gt;ADSI NWCOMPAT Provider Component Description&lt;/TITLE&gt;
&lt;style type="text/css"&gt;@import url(td.css);&lt;/style&gt;&lt;/HEAD&gt;
&lt;BODY TOPMARGIN="0"&gt;
&lt;H1&gt;&lt;A NAME="_adsi_nwcompat_provider_component_description"&gt;&lt;/A&gt;&lt;SUP&gt;&lt;/SUP&gt;ADSI NWCOMPAT Provider Component Description&lt;/H1&gt;

&lt;P&gt;This component contains a provider for Active Directory Service Interfaces (ADSI) that permits accessing Novell NetWare Bindery (NetWare 3.x) directories. By using this component, you can access and modify Bindery directories using Component Object Model (COM) interfaces exported by ADSI. These interfaces are usable from both Automation-compatible languages, such as Microsoft Visual Basic, Scripting Edition (VBScript) and vtable-based languages, such as Microsoft Visual C++.&lt;/P&gt;

&lt;P&gt;This component uses Microsoft Client Services for NetWare to access the NetWare Bindery servers. It is not compatible with third-party NetWare clients.&lt;/P&gt;

&lt;P&gt;This ADSI provider does not expose the &lt;B&gt;IDirectorySearch&lt;/B&gt; interface for searching, and therefore cannot be used with Microsoft ActiveX Data Objects (ADO) or OLE DB.&lt;/P&gt;

&lt;H1&gt;Component Configuration&lt;/H1&gt;

&lt;P&gt;There are no configuration parameters associated with this component.&lt;/P&gt;

&lt;H1&gt;For More Information&lt;/H1&gt;

&lt;P&gt;ADSI is documented in the Platform SDK and the MSDN Library, in the Directory Services section.&lt;/P&gt;

&lt;/BODY&gt;
&lt;/HTML&gt;
</HELPCONTEXT><DISPLAYNAME>ADSI NWCOMPAT Provider Component</DISPLAYNAME><VERSION>1.0</VERSION><DESCRIPTION>This component enables the use of ADSI to communicate with Novell NetWare 3.x Bindery servers</DESCRIPTION><COPYRIGHT>2000 Microsoft Corp.</COPYRIGHT><VENDOR>Microsoft Corp.</VENDOR><OWNERS>mattrim</OWNERS><AUTHORS>mattrim</AUTHORS><DATECREATED>9/12/2000</DATECREATED><DATEREVISED>4/27/2001</DATEREVISED><RESOURCE ResTypeVSGUID="{E66B49F6-4A35-4246-87E8-5C1A468315B5}" BuildTypeMask="819" Name="File:&quot;%11%&quot;,&quot;adsnw.dll&quot;"><PROPERTY Name="DstPath" Format="String">%11%</PROPERTY><PROPERTY Name="DstName" Format="String">adsnw.dll</PROPERTY><PROPERTY Name="NoExpand" Format="Boolean">0</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep:&quot;RawFile&quot;,&quot;MSVCRT.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">MSVCRT.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep:&quot;RawFile&quot;,&quot;ntdll.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">ntdll.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep:&quot;RawFile&quot;,&quot;NWAPI32.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">NWAPI32.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep:&quot;RawFile&quot;,&quot;ADVAPI32.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">ADVAPI32.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep:&quot;RawFile&quot;,&quot;ACTIVEDS.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">ACTIVEDS.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep:&quot;RawFile&quot;,&quot;ole32.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">ole32.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep:&quot;RawFile&quot;,&quot;WINSPOOL.DRV&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">WINSPOOL.DRV</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep:&quot;RawFile&quot;,&quot;KERNEL32.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">KERNEL32.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep:&quot;RawFile&quot;,&quot;USER32.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">USER32.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep:&quot;RawFile&quot;,&quot;OLEAUT32.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">OLEAUT32.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep:&quot;RawFile&quot;,&quot;MPR.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">MPR.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{2C10DB69-39AB-48A4-A83F-9AB3ACBA7C45}" BuildTypeMask="819" Name="RegKey:&quot;HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\ADs\Providers\NWCOMPAT&quot;" Localize="0"><PROPERTY Name="KeyPath" Format="String">HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\ADs\Providers\NWCOMPAT</PROPERTY><PROPERTY Name="RegValue" Format="String">NWCOMPATNamespace</PROPERTY><PROPERTY Name="RegType" Format="Integer">1</PROPERTY><PROPERTY Name="RegOp" Format="Integer">1</PROPERTY><PROPERTY Name="RegCond" Format="Integer">1</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{2C10DB69-39AB-48A4-A83F-9AB3ACBA7C45}" BuildTypeMask="819" Name="RegKey:&quot;HKEY_CLASSES_ROOT\NWCOMPAT\Clsid&quot;" Localize="0"><PROPERTY Name="KeyPath" Format="String">HKEY_CLASSES_ROOT\NWCOMPAT\Clsid</PROPERTY><PROPERTY Name="RegValue" Format="String">{0df68130-4b62-11cf-ae2c-00aa006ebfb9}</PROPERTY><PROPERTY Name="RegType" Format="Integer">1</PROPERTY><PROPERTY Name="RegOp" Format="Integer">1</PROPERTY><PROPERTY Name="RegCond" Format="Integer">1</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{2C10DB69-39AB-48A4-A83F-9AB3ACBA7C45}" BuildTypeMask="819" Name="RegKey:&quot;HKEY_CLASSES_ROOT\NWCOMPATNamespace\Clsid&quot;" Localize="0"><PROPERTY Name="KeyPath" Format="String">HKEY_CLASSES_ROOT\NWCOMPATNamespace\Clsid</PROPERTY><PROPERTY Name="RegValue" Format="String">{0fb32cc0-4b62-11cf-ae2c-00aa006ebfb9}</PROPERTY><PROPERTY Name="RegType" Format="Integer">1</PROPERTY><PROPERTY Name="RegOp" Format="Integer">1</PROPERTY><PROPERTY Name="RegCond" Format="Integer">1</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{2C10DB69-39AB-48A4-A83F-9AB3ACBA7C45}" BuildTypeMask="819" Name="RegKey:&quot;HKEY_CLASSES_ROOT\CLSID\{0df68130-4b62-11cf-ae2c-00aa006ebfb9}&quot;" Localize="0"><PROPERTY Name="KeyPath" Format="String">HKEY_CLASSES_ROOT\CLSID\{0df68130-4b62-11cf-ae2c-00aa006ebfb9}</PROPERTY><PROPERTY Name="RegValue" Format="String">NWCOMPAT Provider Object</PROPERTY><PROPERTY Name="RegType" Format="Integer">1</PROPERTY><PROPERTY Name="RegOp" Format="Integer">1</PROPERTY><PROPERTY Name="RegCond" Format="Integer">1</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{2C10DB69-39AB-48A4-A83F-9AB3ACBA7C45}" BuildTypeMask="819" Name="RegKey:&quot;HKEY_CLASSES_ROOT\CLSID\{0df68130-4b62-11cf-ae2c-00aa006ebfb9}\InprocServer32&quot;" Localize="0"><PROPERTY Name="KeyPath" Format="String">HKEY_CLASSES_ROOT\CLSID\{0df68130-4b62-11cf-ae2c-00aa006ebfb9}\InprocServer32</PROPERTY><PROPERTY Name="RegValue" Format="String">adsnw.dll</PROPERTY><PROPERTY Name="RegType" Format="Integer">1</PROPERTY><PROPERTY Name="RegOp" Format="Integer">1</PROPERTY><PROPERTY Name="RegCond" Format="Integer">1</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{2C10DB69-39AB-48A4-A83F-9AB3ACBA7C45}" BuildTypeMask="819" Name="RegKey:&quot;HKEY_CLASSES_ROOT\CLSID\{0df68130-4b62-11cf-ae2c-00aa006ebfb9}\InprocServer32&quot;\ThreadingModel" Localize="0"><PROPERTY Name="KeyPath" Format="String">HKEY_CLASSES_ROOT\CLSID\{0df68130-4b62-11cf-ae2c-00aa006ebfb9}\InprocServer32</PROPERTY><PROPERTY Name="ValueName" Format="String">ThreadingModel</PROPERTY><PROPERTY Name="RegValue" Format="String">Both</PROPERTY><PROPERTY Name="RegType" Format="Integer">1</PROPERTY><PROPERTY Name="RegOp" Format="Integer">1</PROPERTY><PROPERTY Name="RegCond" Format="Integer">1</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{2C10DB69-39AB-48A4-A83F-9AB3ACBA7C45}" BuildTypeMask="819" Name="RegKey:&quot;HKEY_CLASSES_ROOT\CLSID\{0df68130-4b62-11cf-ae2c-00aa006ebfb9}\ProgID&quot;" Localize="0"><PROPERTY Name="KeyPath" Format="String">HKEY_CLASSES_ROOT\CLSID\{0df68130-4b62-11cf-ae2c-00aa006ebfb9}\ProgID</PROPERTY><PROPERTY Name="RegValue" Format="String">NWCOMPAT</PROPERTY><PROPERTY Name="RegType" Format="Integer">1</PROPERTY><PROPERTY Name="RegOp" Format="Integer">1</PROPERTY><PROPERTY Name="RegCond" Format="Integer">1</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{2C10DB69-39AB-48A4-A83F-9AB3ACBA7C45}" BuildTypeMask="819" Name="RegKey:&quot;HKEY_CLASSES_ROOT\CLSID\{0df68130-4b62-11cf-ae2c-00aa006ebfb9}\TypeLib&quot;" Localize="0"><PROPERTY Name="KeyPath" Format="String">HKEY_CLASSES_ROOT\CLSID\{0df68130-4b62-11cf-ae2c-00aa006ebfb9}\TypeLib</PROPERTY><PROPERTY Name="RegValue" Format="String">{97d25db0-0363-11cf-abc4-02608c9e7553}</PROPERTY><PROPERTY Name="RegType" Format="Integer">1</PROPERTY><PROPERTY Name="RegOp" Format="Integer">1</PROPERTY><PROPERTY Name="RegCond" Format="Integer">1</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{2C10DB69-39AB-48A4-A83F-9AB3ACBA7C45}" BuildTypeMask="819" Name="RegKey:&quot;HKEY_CLASSES_ROOT\CLSID\{0df68130-4b62-11cf-ae2c-00aa006ebfb9}\Version&quot;" Localize="0"><PROPERTY Name="KeyPath" Format="String">HKEY_CLASSES_ROOT\CLSID\{0df68130-4b62-11cf-ae2c-00aa006ebfb9}\Version</PROPERTY><PROPERTY Name="RegValue" Format="String">0.0</PROPERTY><PROPERTY Name="RegType" Format="Integer">1</PROPERTY><PROPERTY Name="RegOp" Format="Integer">1</PROPERTY><PROPERTY Name="RegCond" Format="Integer">1</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{2C10DB69-39AB-48A4-A83F-9AB3ACBA7C45}" BuildTypeMask="819" Name="RegKey:&quot;HKEY_CLASSES_ROOT\CLSID\{0fb32cc0-4b62-11cf-ae2c-00aa006ebfb9}&quot;" Localize="0"><PROPERTY Name="KeyPath" Format="String">HKEY_CLASSES_ROOT\CLSID\{0fb32cc0-4b62-11cf-ae2c-00aa006ebfb9}</PROPERTY><PROPERTY Name="RegValue" Format="String">NWCOMPAT Namespace Object</PROPERTY><PROPERTY Name="RegType" Format="Integer">1</PROPERTY><PROPERTY Name="RegOp" Format="Integer">1</PROPERTY><PROPERTY Name="RegCond" Format="Integer">1</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{2C10DB69-39AB-48A4-A83F-9AB3ACBA7C45}" BuildTypeMask="819" Name="RegKey:&quot;HKEY_CLASSES_ROOT\CLSID\{0fb32cc0-4b62-11cf-ae2c-00aa006ebfb9}\InprocServer32&quot;" Localize="0"><PROPERTY Name="KeyPath" Format="String">HKEY_CLASSES_ROOT\CLSID\{0fb32cc0-4b62-11cf-ae2c-00aa006ebfb9}\InprocServer32</PROPERTY><PROPERTY Name="RegValue" Format="String">adsnw.dll</PROPERTY><PROPERTY Name="RegType" Format="Integer">1</PROPERTY><PROPERTY Name="RegOp" Format="Integer">1</PROPERTY><PROPERTY Name="RegCond" Format="Integer">1</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{2C10DB69-39AB-48A4-A83F-9AB3ACBA7C45}" BuildTypeMask="819" Name="RegKey:&quot;HKEY_CLASSES_ROOT\CLSID\{0fb32cc0-4b62-11cf-ae2c-00aa006ebfb9}\InprocServer32&quot;\ThreadingModel" Localize="0"><PROPERTY Name="KeyPath" Format="String">HKEY_CLASSES_ROOT\CLSID\{0fb32cc0-4b62-11cf-ae2c-00aa006ebfb9}\InprocServer32</PROPERTY><PROPERTY Name="ValueName" Format="String">ThreadingModel</PROPERTY><PROPERTY Name="RegValue" Format="String">Both</PROPERTY><PROPERTY Name="RegType" Format="Integer">1</PROPERTY><PROPERTY Name="RegOp" Format="Integer">1</PROPERTY><PROPERTY Name="RegCond" Format="Integer">1</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{2C10DB69-39AB-48A4-A83F-9AB3ACBA7C45}" BuildTypeMask="819" Name="RegKey:&quot;HKEY_CLASSES_ROOT\CLSID\{0fb32cc0-4b62-11cf-ae2c-00aa006ebfb9}\ProgID&quot;" Localize="0"><PROPERTY Name="KeyPath" Format="String">HKEY_CLASSES_ROOT\CLSID\{0fb32cc0-4b62-11cf-ae2c-00aa006ebfb9}\ProgID</PROPERTY><PROPERTY Name="RegValue" Format="String">NWCOMPATNamespace</PROPERTY><PROPERTY Name="RegType" Format="Integer">1</PROPERTY><PROPERTY Name="RegOp" Format="Integer">1</PROPERTY><PROPERTY Name="RegCond" Format="Integer">1</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{2C10DB69-39AB-48A4-A83F-9AB3ACBA7C45}" BuildTypeMask="819" Name="RegKey:&quot;HKEY_CLASSES_ROOT\CLSID\{0fb32cc0-4b62-11cf-ae2c-00aa006ebfb9}\TypeLib&quot;" Localize="0"><PROPERTY Name="KeyPath" Format="String">HKEY_CLASSES_ROOT\CLSID\{0fb32cc0-4b62-11cf-ae2c-00aa006ebfb9}\TypeLib</PROPERTY><PROPERTY Name="RegValue" Format="String">{97d25db0-0363-11cf-abc4-02608c9e7553}</PROPERTY><PROPERTY Name="RegType" Format="Integer">1</PROPERTY><PROPERTY Name="RegOp" Format="Integer">1</PROPERTY><PROPERTY Name="RegCond" Format="Integer">1</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{2C10DB69-39AB-48A4-A83F-9AB3ACBA7C45}" BuildTypeMask="819" Name="RegKey:&quot;HKEY_CLASSES_ROOT\CLSID\{0fb32cc0-4b62-11cf-ae2c-00aa006ebfb9}\Version&quot;" Localize="0"><PROPERTY Name="KeyPath" Format="String">HKEY_CLASSES_ROOT\CLSID\{0fb32cc0-4b62-11cf-ae2c-00aa006ebfb9}\Version</PROPERTY><PROPERTY Name="RegValue" Format="String">0.0</PROPERTY><PROPERTY Name="RegType" Format="Integer">1</PROPERTY><PROPERTY Name="RegOp" Format="Integer">1</PROPERTY><PROPERTY Name="RegCond" Format="Integer">1</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep:&quot;RawFile&quot;,&quot;adsldpc.dll&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">adsldpc.dll</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{90D8E195-E710-4AF6-B667-B1805FFC9B8F}" BuildTypeMask="819" Name="RawDep:&quot;RawFile&quot;,&quot;activeds.tlb&quot;"><PROPERTY Name="RawType" Format="String">File</PROPERTY><PROPERTY Name="Value" Format="String">activeds.tlb</PROPERTY></RESOURCE><GROUPMEMBER GroupVSGUID="{E01B4103-3883-4FE8-992F-10566E7B796C}"/><GROUPMEMBER GroupVSGUID="{D8142082-243E-4C8C-B98B-3290C50D93C7}"/></COMPONENT></DCARRIER>
