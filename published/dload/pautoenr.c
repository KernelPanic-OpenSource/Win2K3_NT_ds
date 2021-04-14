#include "dspch.h"
#pragma hdrstop

#define _PAUTOENR_

static
HANDLE 
WINAPI
CertAutoEnrollment(IN HWND     hwndParent,
                   IN DWORD    dwStatus)
{
    return NULL;
}


static
BOOL 
WINAPI
CertAutoRemove(IN DWORD    dwFlags)
{
    return FALSE;
}


//
// !! WARNING !! The entries below must be in alphabetical order
// and are CASE SENSITIVE (i.e., lower case comes last!)
//
DEFINE_PROCNAME_ENTRIES(pautoenr)
{
    DLPENTRY(CertAutoEnrollment)
    DLPENTRY(CertAutoRemove)
};

DEFINE_PROCNAME_MAP(pautoenr)

