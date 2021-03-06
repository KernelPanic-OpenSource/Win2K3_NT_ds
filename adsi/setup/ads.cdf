#define quote(X) #X
[Version]
Class=IEXPRESS
CDFVersion=2.0
[Options]
Quantum=7
ExtractOnly=0
ShowInstallProgramWindow=0
HideExtractAnimation=0
UseLongFileName=0
RebootMode=I
InstallPrompt=%InstallPrompt%
DisplayLicense=%DisplayLicense%
FinishMessage=%FinishMessage%
TargetName=%TargetName%
FriendlyName=%FriendlyName%
AppLaunched=%AppLaunched%
PostInstallCmd=%PostInstallCmd%
SourceFiles=SourceFiles
[Strings]
InstallPrompt="Welcome to Active Directory Service Interfaces (ADSI) Version 2.5 Setup. This will install the binaries and providers required for ADSI on your system. If you have Site Server Administration Tools then please upgrade to Site Server SP2. Are you sure you want to continue?"
DisplayLicense=LICENSEFILE
FinishMessage="Active Directory Service Interfaces installation done!"
TargetName=TARGETNAME
FriendlyName="Active Directory Service Interfaces Version 2.5"
AppLaunched="activeds.inf"
PostInstallCmd="<None>"
FILE0="nwapi32.dll"
FILE1="activeds.dll"
FILE2="adsnw.dll"
FILE3="adsnt.dll"
FILE4="adsnds.dll"
FILE5="adsldp.dll"
FILE6="adsldpc.dll"
FILE7="wldap32.dll"
FILE8="activeds.tlb"
FILE9="activeds.inf"
FILE10="adsmsext.dll"
[SourceFiles]
SourceFiles0=SOURCEFILES
[SourceFiles0]
%FILE0%
%FILE1%
%FILE2%
%FILE3%
%FILE4%
%FILE5%
%FILE6%
%FILE7%
%FILE8%
%FILE9%
%FILE10%
