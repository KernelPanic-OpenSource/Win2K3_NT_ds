#define INITGUID

#include <ole2.h>

//--------------------------------------------------------------------------
//
//  ADS CLSIDs
//
//--------------------------------------------------------------------------


DEFINE_GUID(CLSID_ADsNamespaces,0x233664B0L,0x0367,0x11CF,0xAB,0xC4,0x02,0x60,0x8C,0x9E,0x75,0x53);

DEFINE_GUID(CLSID_ADsProvider,0x4753DA60L,0x5B71,0x11CF,0xB0,0x35,0x00,0xAA,0x00,0x6E,0x09,0x75);

// {E0FA581D-2188-11d2-A739-00C04FA377A1}
DEFINE_GUID(CLSID_ADSI_BINDER,0xe0fa581d, 0x2188, 0x11d2, 0xa7, 0x39, 0x0, 0xc0, 0x4f, 0xa3, 0x77, 0xa1);

// {04EE4CBB-21B6-11d2-A739-00C04FA377A1}
DEFINE_GUID(CLSID_Row, 0x4ee4cbb, 0x21b6, 0x11d2, 0xa7, 0x39, 0x0, 0xc0, 0x4f, 0xa3, 0x77, 0xa1);

//------------------------------------------------------------------------
//  GUIDS that come out of oleds.tlb.
//------------------------------------------------------------------------

DEFINE_GUID(IID_IProvideDBService, 0xEFF65380L,0x9C98,0x11CF,0xB9,0x63,0x00,0xAA,0x00,0x44,0x77,0x3D);

DEFINE_GUID( IID_IRowProvider, 0x36576d80, 0xe5bc, 0x11cf, 0xa4, 0x8, 0x0, 0xc0, 0x4f, 0xd6, 0x11, 0xd0);

DEFINE_GUID( DBPROPSET_TEMPTABLE, 0x4e4c0950L,0xe5a8,0x11cf,0xa4,0x08,0x00,0xc0,0x4f,0xd6,0x11,0xd0);


