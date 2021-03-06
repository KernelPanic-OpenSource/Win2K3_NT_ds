#pragma code_page(936)

#include    <msprivs2.h>

LANGUAGE LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED
STRINGTABLE MOVEABLE DISCARDABLE
BEGIN
	IDS_SeCreateTokenPrivilege,		"创建标记对象"
	IDS_SeAssignPrimaryTokenPrivilege,	"替换进程级别标记"
	IDS_SeLockMemoryPrivilege,		"内存中锁定页面"
	IDS_SeIncreaseQuotaPrivilege,		"调整进程的内存配额"
	IDS_SeMachineAccountPrivilege,		"域中添加工作站"
	IDS_SeTcbPrivilege,			"以操作系统方式操作"
	IDS_SeSecurityPrivilege,		"管理审核和安全日志"
	IDS_SeTakeOwnershipPrivilege,		"取得文件或其他对象的所有权"
	IDS_SeLoadDriverPrivilege,		"装载和卸载设备驱动程序"
	IDS_SeSystemProfilePrivilege,		"配置系统性能"
	IDS_SeSystemtimePrivilege,		"更改系统时间"
	IDS_SeProfileSingleProcessPrivilege,	"配置单一进程"
	IDS_SeIncreaseBasePriorityPrivilege,	"增加计划优先级"
	IDS_SeCreatePagefilePrivilege,		"创建页面文件"
	IDS_SeCreatePermanentPrivilege,		"创建永久共享对象"
	IDS_SeBackupPrivilege,			"备份文件和目录"
	IDS_SeRestorePrivilege,			"还原文件和目录"
	IDS_SeShutdownPrivilege,		"关闭系统"
	IDS_SeDebugPrivilege,			"调试程序"
	IDS_SeAuditPrivilege,			"生成安全审核"
	IDS_SeSystemEnvironmentPrivilege,	"修改固件环境值"
	IDS_SeChangeNotifyPrivilege,		"跳过遍历检查"
	IDS_SeRemoteShutdownPrivilege,		"从远程系统强制关机"
// new in Windows 2000
	IDS_SeUndockPrivilege,			"从扩展坞中取出计算机"
	IDS_SeSyncAgentPrivilege,		"同步目录服务数据"
	IDS_SeEnableDelegationPrivilege,	"允许计算机和用户帐户被信任以便用于委任"
// new in windows 2000 + 1
	IDS_SeManageVolumePrivilege,		"执行卷维护任务"
        IDS_SeImpersonatePrivilege,             "身份验证后模拟客户端"
        IDS_SeCreateGlobalPrivilege,            "创建全局对象"
END
