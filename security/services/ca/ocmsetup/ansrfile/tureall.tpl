[Version]
Signature = "$Windows NT$"

[Global]
FreshMode = Minimal | Typical | Custom
MaintenanceMode = AddRmove | reinstallFile | reinstallComplete | RemoveAll
UpgradeMode = UpgradeOnly | AddExtraComps

[Components]
certsrv = ON
certsrv_client = ON
certsrv_server = ON

[certsrv_client]

[certsrv_server]
UseExistingCert=YES
PreserveDB=YES
