// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//      are changed infrequently
//
#if !defined(SLBCSP_STDAFX_H)
#define SLBCSP_STDAFX_H

#include "NoWarning.h"

// Avoid the compiler's redefine warning message when afxv_w32.h is included
#if defined(_WIN32_WINDOWS)
#undef _WIN32_WINDOWS
#endif

#include <afxwin.h>         // MFC core and standard components
#include <afxext.h>         // MFC extensions

// Include the template class(s)
#include <afxtempl.h>
#include <afxmt.h>

#ifdef ISOLATION_AWARE_ENABLED
#include <shfusion.h>

class CThemeContextActivator
{
public:
    CThemeContextActivator() : m_ulActivationCookie(0)
        { SHActivateContext (&m_ulActivationCookie); }

    ~CThemeContextActivator()
        { SHDeactivateContext (m_ulActivationCookie); }

private:
    ULONG_PTR m_ulActivationCookie;
};

#endif

#endif // !defined(SLBCSP_STDAFX_H)
