
extern HRESULT SamHelp(CArgs *pArgs);
extern HRESULT SamQuit(CArgs *pArgs);
extern HRESULT SamSpecifyLogFile(CArgs *pArgs);
extern HRESULT SamConnectToServer(CArgs *pArgs);
extern HRESULT SamDuplicateSidCheckOnly(CArgs *pArgs);
extern HRESULT SamDuplicateSidCheckAndCleanup(CArgs *pArgs);
extern VOID    SamCleanupGlobals();

extern HRESULT SetPwdHelp(CArgs *pArgs);
extern HRESULT SetPwdQuit(CArgs *pArgs);
extern HRESULT SetDSRMPwdWorker(CArgs *pArgs);
