/**********************************************************************/
/**                       Microsoft Passport                         **/
/**                Copyright(c) Microsoft Corporation, 1999 - 2001   **/
/**********************************************************************/

/*
    manager.h
        Define CManager class for passport manager interface


    FILE HISTORY:

*/
// Manager.h : Declaration of the CManager

#ifndef __MANAGER_H_
#define __MANAGER_H_

#include "resource.h"       // main symbols
#include "Passport.h"
#include "Ticket.h"
#include "Profile.h"
#include "passportservice.h"
#include <httpext.h>
#include <httpfilt.h>

using namespace ATL;

inline bool IsEmptyString(LPCWSTR str)
{
   if (!str) return true;
   if (*str == 0) return true;
   return false;
};

/////////////////////////////////////////////////////////////////////////////
// CManager
class ATL_NO_VTABLE CManager :
        public CComObjectRootEx<CComMultiThreadModel>,
        public CComCoClass<CManager, &CLSID_Manager>,
        public ISupportErrorInfo,
        public IPassportService,
        public IDispatchImpl<IPassportManager3, &IID_IPassportManager3, &LIBID_PASSPORTLib>,
        public IDomainMap
{
public:
    CManager();
    ~CManager();

public:

DECLARE_REGISTRY_RESOURCEID(IDR_MANAGER)
DECLARE_GET_CONTROLLING_UNKNOWN()
DECLARE_PROTECT_FINAL_CONSTRUCT()

BEGIN_COM_MAP(CManager)
  COM_INTERFACE_ENTRY(IPassportManager)
  COM_INTERFACE_ENTRY(IPassportManager2)
  COM_INTERFACE_ENTRY(IPassportManager3)
  COM_INTERFACE_ENTRY(IDispatch)
  COM_INTERFACE_ENTRY(ISupportErrorInfo)
  COM_INTERFACE_ENTRY(IPassportService)
  COM_INTERFACE_ENTRY(IDomainMap)
  COM_INTERFACE_ENTRY_AGGREGATE(IID_IMarshal, m_pUnkMarshaler.p)
END_COM_MAP()

        HRESULT FinalConstruct()
        {
            if(m_pUnkMarshaler.p != NULL)
                return S_OK;

            return CoCreateFreeThreadedMarshaler(
                        GetControllingUnknown(), &m_pUnkMarshaler.p);
        }

        void FinalRelease()
        {
                m_pUnkMarshaler.Release();
        }

        CComPtr<IUnknown> m_pUnkMarshaler;

// ISupportsErrorInfo
  STDMETHOD(InterfaceSupportsErrorInfo)(REFIID riid);

// IPassportManager
public:
        STDMETHOD(HaveConsent)(/*[in]*/ VARIANT_BOOL bNeedFullConsent, /*[in]*/ VARIANT_BOOL bNeedBirthdate, /*[out,retval]*/ VARIANT_BOOL* pbHaveConsent);
        STDMETHOD(GetServerInfo)(/*[out,retval]*/ BSTR *pbstrOut);
  STDMETHOD(Commit)(BSTR *newProf);
  STDMETHOD(get_HasSavedPassword)(/*[out, retval]*/ VARIANT_BOOL *pVal);
  STDMETHOD(get_ProfileByIndex)(/*[in]*/ int index, /*[out, retval]*/ VARIANT *pVal);
  STDMETHOD(put_ProfileByIndex)(/*[in]*/ int index, /*[in]*/ VARIANT newVal);
  STDMETHOD(get_Profile)(/*[in]*/ BSTR attributeName, /*[out, retval]*/ VARIANT *pVal);
  STDMETHOD(put_Profile)(/*[in]*/ BSTR attributeName, /*[in]*/ VARIANT newVal);
  STDMETHOD(DomainFromMemberName)(/*[in,optional]*/ VARIANT memberName, /*[out,retval]*/ BSTR *pDomainName);
  STDMETHOD(GetDomainAttribute)(/*[in]*/ BSTR attributeName, /*[in,optional]*/ VARIANT lcid, /*[in,optional]*/ VARIANT domain, /*[out,retval]*/ BSTR *pAttrVal);
  STDMETHOD(get_TimeSinceSignIn)(/*[out, retval]*/ int *pVal);
  STDMETHOD(get_TicketAge)(/*[out, retval]*/ int *pVal);
  STDMETHOD(get_SignInTime)(/*[out, retval]*/ long *pVal);
  STDMETHOD(get_TicketTime)(/*[out, retval]*/ long *pVal);
  STDMETHOD(HasFlag)(/*[in]*/ VARIANT flagMask, /*[out, retval]*/ VARIANT_BOOL *pVal);
  STDMETHOD(get_FromNetworkServer)(/*[out, retval]*/ VARIANT_BOOL *pVal);
  STDMETHOD(get_HasTicket)(/*[out, retval]*/ VARIANT_BOOL *pVal);
  STDMETHOD(HasProfile)(/*[in]*/ VARIANT profileName, /*[out, retval]*/ VARIANT_BOOL *pVal);
  STDMETHOD(LogoTag)(/*[in]*/ VARIANT returnUrl, /*[in]*/ VARIANT TimeWindow, /*[in]*/ VARIANT ForceLogin, VARIANT coBrandTemplate, VARIANT lang_id, VARIANT bSecure, VARIANT NameSpace, /*[in, optional*/ VARIANT KPP, /*[in, optional]*/ VARIANT SecureLevel, /*[out, retval]*/ BSTR *pVal);
  STDMETHOD(LogoTag2)(/*[in]*/ VARIANT returnUrl, /*[in]*/ VARIANT TimeWindow, /*[in]*/ VARIANT ForceLogin, VARIANT coBrandTemplate, VARIANT lang_id, VARIANT bSecure, VARIANT NameSpace, /*[in, optional*/ VARIANT KPP, /*[in, optional]*/ VARIANT SecureLevel, /*[out, retval]*/ BSTR *pVal);
  STDMETHOD(IsAuthenticated)(/*[in]*/ VARIANT TimeWindow, /*[in]*/ VARIANT ForceLogin, /*[in,optional]*/ VARIANT SecureLevel, /*[out, retval]*/ VARIANT_BOOL *pVal);
  STDMETHOD(AuthURL)(/*[in]*/ VARIANT returnUrl, /*[in]*/ VARIANT TimeWindow, /*[in]*/ VARIANT ForceLogin, VARIANT coBrandTemplate, VARIANT lang_id, VARIANT NameSpace, /*[in, optional]*/ VARIANT KPP, /*[in, optional]*/ VARIANT SecureLevel, /*[out,retval]*/ BSTR *pAuthUrl);
  STDMETHOD(AuthURL2)(/*[in]*/ VARIANT returnUrl, /*[in]*/ VARIANT TimeWindow, /*[in]*/ VARIANT ForceLogin, VARIANT coBrandTemplate, VARIANT lang_id, VARIANT NameSpace, /*[in, optional]*/ VARIANT KPP, /*[in, optional]*/ VARIANT SecureLevel, /*[out,retval]*/ BSTR *pAuthUrl);
  //    New API. call it to generate user logon. ASP caller will get a redirect.
  //    isapi callers should not do any more work after this
  STDMETHOD(LoginUser)(/*[in]*/ VARIANT returnUrl,
                       /*[in]*/ VARIANT TimeWindow,
                       /*[in]*/ VARIANT ForceLogin,
                       /*[in]*/ VARIANT coBrandTemplate,
                       /*[in]*/ VARIANT lang_id,
                       /*[in]*/ VARIANT NameSpace,
             /*[in, optional]*/ VARIANT KPP,
             /*[in, optional]*/ VARIANT SecureLevel,
             /*[in, optional]*/ VARIANT ExtraParams);
  //Active Server Pages Methods
  STDMETHOD(OnStartPage)(IUnknown* IUnk);
  STDMETHOD(OnStartPageManual)(BSTR qsT, BSTR qsP, BSTR mspauth, BSTR mspprof, BSTR mspconsent, VARIANT vmspsec, VARIANT *pCookies);
  STDMETHOD(OnStartPageECB)(/*[in]*/ LPBYTE pECB, /*[in,out]*/ DWORD *pBufSize, /*[out]*/ LPSTR pCookieHeader);
  STDMETHOD(OnStartPageFilter)(/*[in]*/ LPBYTE pPFC, /*[in,out]*/ DWORD *pBufSize, /*[out]*/ LPSTR pCookieHeader);
  STDMETHOD(OnStartPageASP)(/*[in]*/ IDispatch* pdispRequest, /*[in]*/ IDispatch* pdispResponse);
  STDMETHOD(OnEndPage)();
  STDMETHOD(_Ticket)(IPassportTicket** piTicket);
  STDMETHOD(_Profile)(IPassportProfile** piProfile);
  STDMETHOD(get_Domains)(VARIANT* pArrayVal);
  STDMETHOD(get_Error)(long* pErrorVal);

  // IPassportManager3
  STDMETHOD(get_Ticket)(/*[in]*/ BSTR attributeName, /*[out, retval]*/ VARIANT *pVal);


   STDMETHOD(GetCurrentConfig)(/*[in]*/ BSTR name, /*[out, retval]*/ VARIANT *pVal)
   {
      if (!m_pRegistryConfig)
      {
         AtlReportError(CLSID_Manager, PP_E_NOT_INITIALIZEDSTR,
                       IID_IPassportManager, PP_E_NOT_INITIALIZED);
         return PP_E_NOT_INITIALIZED;
      }
      else
         return m_pRegistryConfig->GetCurrentConfig(name, pVal);
   };

   STDMETHOD(LogoutURL)(
            /* [optional][in] */ VARIANT returnUrl,
            /* [optional][in] */ VARIANT coBrandArgs,
            /* [optional][in] */ VARIANT lang_id,
            /* [optional][in] */ VARIANT NameSpace,
            /* [optional][in] */ VARIANT bSecure,
            /* [retval][out] */  BSTR *pVal);

  STDMETHOD(GetLoginChallenge)(/*[in]*/ VARIANT returnUrl,
                       /*[in]*/ VARIANT TimeWindow,
                       /*[in]*/ VARIANT ForceLogin,
                       /*[in]*/ VARIANT coBrandTemplate,
                       /*[in]*/ VARIANT lang_id,
                       /*[in]*/ VARIANT NameSpace,
             /*[in, optional]*/ VARIANT KPP,
             /*[in, optional]*/ VARIANT SecureLevel,
             /*[in, optional]*/ VARIANT ExtraParams,
//             /*[out, optional]*/ VARIANT *pAuthHeader,
             /*[out, retval]*/ BSTR* pAuthHeader
             );

STDMETHOD(get_HexPUID)(/*[out, retval]*/ BSTR *pVal);
STDMETHOD(get_PUID)(/*[out, retval]*/ BSTR *pVal);

STDMETHOD(OnStartPageHTTPRawEx)(
            /* [in] */          LPCSTR method,
            /* [in] */          LPCSTR path,
            /* [in] */          LPCSTR QS,
            /* [in] */          LPCSTR HTTPVer,
            /* [string][in] */  LPCSTR headers,
            /* [in] */          DWORD  flags,
            /* [out][in] */     DWORD  *bufSize,
            /* [size_is][out]*/ LPSTR  pCookieHeader);

STDMETHOD(OnStartPageHTTPRaw)(
            /* [string][in] */ LPCSTR request_line,
            /* [string][in] */ LPCSTR headers,
            /* [in] */ DWORD flags,
            /* [out][in] */ DWORD *pBufSize,
            /* [size_is][out] */ LPSTR pCookieHeader);
        
        
STDMETHOD(ContinueStartPageHTTPRaw)(
            /* [in] */ DWORD bodyLen,
            /* [size_is][in] */ byte *body,
            /* [out][in] */ DWORD *pBufSize,
            /* [size_is][out] */ LPSTR pRespHeaders,
            /* [out][in] */ DWORD *pRespBodyLen,
            /* [size_is][out] */ byte *pRespBody);

STDMETHOD(get_Option)( 
            /* [in] */ BSTR name,
            /* [retval][out] */ VARIANT *pVal);
        
STDMETHOD(put_Option)( 
            /* [in] */ BSTR name,
            /* [in] */ VARIANT newVal);


// IDomainMap
public:
  // GetDomainAttribute and get_Domains declared above.
  STDMETHOD(DomainExists)(BSTR bstrDomainName, VARIANT_BOOL* pbExists);

// IPassportService
public:
    STDMETHOD(Initialize)(BSTR, IServiceProvider*);
    STDMETHOD(Shutdown)();
    STDMETHOD(ReloadState)(IServiceProvider*);
    STDMETHOD(CommitState)(IServiceProvider*);
    STDMETHOD(DumpState)( BSTR* );

protected:
  void wipeState();

  // return S_OK -- altered, should use two returned output params for MSPAuth and MSPSecAuth as cookies
  HRESULT   IfAlterAuthCookie(BSTR* pMSPAuth, BSTR* pMSPSecAuth);

  // return S_OK -- should use the generated MSPConsent cookie
  HRESULT   IfConsentCookie(BSTR* pMSPConsent);

private:
  STDMETHOD(GetLoginChallengeInternal)(/*[in]*/ VARIANT returnUrl,
                       /*[in]*/ VARIANT TimeWindow,
                       /*[in]*/ VARIANT ForceLogin,
                       /*[in]*/ VARIANT coBrandTemplate,
                       /*[in]*/ VARIANT lang_id,
                       /*[in]*/ VARIANT NameSpace,
             /*[in, optional]*/ VARIANT KPP,
             /*[in, optional]*/ VARIANT SecureLevel,
             /*[in, optional]*/ VARIANT ExtraParams,
             /*[out, optional]*/ VARIANT *pAuthHeader,
             /*[out, retval]*/ BSTR* pAuthVal
             );

  STDMETHOD(CommonAuthURL)(VARIANT returnUrl,
                           VARIANT TimeWindow,
                           VARIANT ForceLogin,
                           VARIANT coBrandTemplate,
                           VARIANT lang_id,
                           VARIANT NameSpace,
                           VARIANT KPP,
                           VARIANT SecureLevel,
                           BOOL    fRedirToSelf,
                           VARIANT functionArea,
                           BSTR *pAuthUrl);
  BOOL handleQueryStringData(BSTR a, BSTR p);
  BOOL handleCookieData(BSTR a, BSTR p, BSTR c, BSTR s);
  BOOL checkForPassportChallenge(IRequestDictionaryPtr piServerVariables);
  BOOL HeaderFromQS(PWSTR   pszQS, _bstr_t& bstrHeader);
  STDMETHODIMP FormatAuthHeaderFromParams(PCWSTR    pszLoginUrl,    // unused for now
                                          PCWSTR    pszRetUrl,
                                          ULONG     ulTimeWindow,
                                          BOOL      fForceLogin,
                                          time_t    ct,
                                          PCWSTR    pszCBT,         // unused for now
                                          PCWSTR    pszNamespace,
                                          int       nKpp,
                                          PWSTR     pszlcid,
                                          ULONG     ulSecLevel,
                                          _bstr_t&  strHeader   //  return result
                                          );
  STDMETHOD(CommonLogoTag)(VARIANT returnUrl,
                     VARIANT TimeWindow,
                     VARIANT ForceLogin,
                     VARIANT coBrandTemplate,
                     VARIANT lang_id,
                     VARIANT bSecure,
                     VARIANT NameSpace,
                     VARIANT KPP,
                     VARIANT SecureLevel,
                     BOOL    fRedirToSelf,
                     BSTR *pVal);
  //    helper for coming up with login paramers based on
  //    what the site passed in and registry configs
  //    someone should put all these in a class, so the number of
  //    params stays manageable
  STDMETHOD(GetLoginParams)(//  this is what the caller passed in
                      VARIANT vRU,
                      VARIANT vTimeWindow,
                      VARIANT vForceLogin,
                      VARIANT vCoBrand,
                      VARIANT vLCID,
                      VARIANT vNameSpace,
                      VARIANT vKPP,
                      VARIANT vSecureLevel,
                      //    these are the processed values
                      _bstr_t&  url,
                      _bstr_t&  returnUrl,
                      UINT&     TimeWindow,
                      VARIANT_BOOL& ForceLogin,
                      time_t&   ct,
                      _bstr_t&  strCBT,
                      _bstr_t&  strNameSpace,
                      int&      nKpp,
                      ULONG&    ulSecureLevel,
                      PWSTR     pszlcid);

  VARIANT_BOOL m_profileValid;
  VARIANT_BOOL m_ticketValid;

  CComObject<CTicket>  *m_piTicket;
  CComObject<CProfile> *m_piProfile;

  CRegistryConfig*      m_pRegistryConfig;

  IRequestPtr m_piRequest;                              //Request Object
  IResponsePtr m_piResponse;                            //Response Object
  bool m_bOnStartPageCalled;                            //OnStartPage successful?

  bool m_fromQueryString;
  //    for ISAPI ....
  EXTENSION_CONTROL_BLOCK   *m_pECB;
  PHTTP_FILTER_CONTEXT      m_pFC;
  //    is it 1.4 capable client?
  BOOL  m_bIsTweenerCapable;

  long m_lNetworkError;

  // secure sign in
  bool m_bSecureTransported;

  CComVariant   m_iModeOption;
  BOOL IfCreateTPF()
  {
      if (V_VT(&m_iModeOption) == VT_BOOL && V_BOOL(&m_iModeOption) == VARIANT_TRUE)
        return FALSE;
      return TRUE;

  }
};

#endif //__MANAGER_H_
