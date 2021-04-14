#include "pch.h"

#pragma hdrstop

#include "bmcommon.h"


PWCHAR g_aszSd[] =
{
    L"O:BAG:BAD:(OA;;GA;;;WD)S:(AU;FASA;GA;;;WD)",

    L"O:BAG:DUD:(A;;0x40;;;s-1-2-2)(A;;0x1;;;BA)(OA;;0x2;6da8a4ff-0e52-11d0-a286-00aa00304900;;BA)(OA;;0x4;6da8a4ff-0e52-11d0-a286-00aa00304901;;BA)(OA;;0x8;6da8a4ff-0e52-11d0-a286-00aa00304903;;AU)(OA;;0x10;6da8a4ff-0e52-11d0-a286-00aa00304904;;BU)(OA;;0x20;6da8a4ff-0e52-11d0-a286-00aa00304905;;AU)(A;;0x40;;;PS)S:(AU;IDSAFA;0xFFFFFF;;;WD)",

    //
    // mkdit.ini : [User]
    //
    L"O:BAG:DUD:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;WD)(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;SY)(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;AO)(A;;RPLCLORC;;;PS)(OA;;CR;ab721a53-1e2f-11d0-9819-00aa0040529b;;PS)(OA;;CR;ab721a54-1e2f-11d0-9819-00aa0040529b;;PS)(OA;;CR;ab721a56-1e2f-11d0-9819-00aa0040529b;;PS)(OA;;RPWP;77B5B886-944A-11d1-AEBD-0000F80367C1;;PS)(OA;;RPWP;E45795B2-9455-11d1-AEBD-0000F80367C1;;PS)(OA;;RPWP;E45795B3-9455-11d1-AEBD-0000F80367C1;;PS)(OA;;RP;037088f8-0ae1-11d2-b422-00a0c968f939;;RS)(OA;;RP;4c164200-20c0-11d0-a768-00aa006e0529;;RS)(OA;;RP;bc0ac240-79a9-11d0-9020-00c04fc2d4cf;;RS)(A;;RC;;;AU)(OA;;RP;59ba2f42-79a2-11d0-9020-00c04fc2d3cf;;AU)(OA;;RP;77B5B886-944A-11d1-AEBD-0000F80367C1;;AU)(OA;;RP;E45795B3-9455-11d1-AEBD-0000F80367C1;;AU)(OA;;RP;e48d0154-bcf8-11d1-8702-00c04fb96050;;AU)(OA;;CR;ab721a53-1e2f-11d0-9819-00aa0040529b;;WD)(OA;;RP;5f202010-79a5-11d0-9020-00c04fc2d4cf;;RS)(OA;;RPWP;bf967a7f-0de6-11d0-a285-00aa003049e2;;CA)S:(AU;IDSAFA;0xFFFFFF;;;WD)",

    //
    // mkdit.ini : [Computer]
    //
    L"O:BAG:DUD:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA)(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;AO)(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;SY)(A;;RPCRLCLORCSDDT;;;CO)(OA;;WP;4c164200-20c0-11d0-a768-00aa006e0529;;CO)(A;;RPLCLORC;;;AU)(OA;;CR;ab721a53-1e2f-11d0-9819-00aa0040529b;;WD)(OA;;CCDC;;;PS)(OA;;CCDC;bf967aa8-0de6-11d0-a285-00aa003049e2;;PO)(OA;;RPWP;bf967a7f-0de6-11d0-a285-00aa003049e2;;CA)(OA;;SW;f3a64788-5306-11d1-a9c5-0000f80367c1;;PS)(OA;;RPWP;77B5B886-944A-11d1-AEBD-0000F80367C1;;PS)(OA;;SW;72e39547-7b18-11d1-adef-00c04fd8d5cd;;PS)(OA;;SW;72e39547-7b18-11d1-adef-00c04fd8d5cd;;CO)(OA;;SW;f3a64788-5306-11d1-a9c5-0000f80367c1;;CO)"
    
    //
    // Domain-DNS (from Praerit)
    //
    //L"D:(A;;RP;;;WD)(OA;;CR;1131f6aa-9c07-11d1-f79f-00c04fc2dcd2;;ED)(OA;;CR;1131f6ab-9c07-11d1-f79f-00c04fc2dcd2;;ED)(OA;;CR;1131f6ac-9c07-11d1-f79f-00c04fc2dcd2;;ED)(OA;;CR;1131f6aa-9c07-11d1-f79f-00c04fc2dcd2;;BA)(OA;;CR;1131f6ab-9c07-11d1-f79f-00c04fc2dcd2;;BA)(OA;;CR;1131f6ac-9c07-11d1-f79f-00c04fc2dcd2;;BA)(A;;RPLCLORC;;;AU)(A;;RPWPCRLCLOCCRCWDWOSW;;;DA)(A;CI;RPWPCRLCLOCCRCWDWOSDSW;;;BA)(A;;RPWPCRLCLOCCDCRCWDWOSDDTSW;;;SY)(A;CI;RPWPCRLCLOCCDCRCWDWOSDDTSW;;;EA)(A;CI;LC;;;RU)(OA;CIIO;RP;037088f8-0ae1-11d2-b422-00a0c968f939;bf967aba-0de6-11d0-a285-00aa003049e2;RU)(OA;CIIO;RP;59ba2f42-79a2-11d0-9020-00c04fc2d3cf;bf967aba-0de6-11d0-a285-00aa003049e2;RU)(OA;CIIO;RP;bc0ac240-79a9-11d0-9020-00c04fc2d4cf;bf967aba-0de6-11d0-a285-00aa003049e2;RU)(OA;CIIO;RP;4c164200-20c0-11d0-a768-00aa006e0529;bf967aba-0de6-11d0-a285-00aa003049e2;RU)(OA;CIIO;RP;5f202010-79a5-11d0-9020-00c04fc2d4cf;bf967aba-0de6-11d0-a285-00aa003049e2;RU)(OA;CIIO;RPLCLORC;;bf967a9c-0de6-11d0-a285-00aa003049e2;RU)(A;;RC;;;RU)(OA;CIIO;RPLCLORC;;bf967aba-0de6-11d0-a285-00aa003049e2;RU)S:(AU;CISAFA;WDWOSDDTWPCRCCDCSW;;;WD)"

};

PWCHAR g_szSd;

GUID Guid0 = {0x6da8a4ff, 0xe52, 0x11d0, {0xa2, 0x86, 0x00, 0xaa, 0x00, 0x30, 0x49, 0x00}};
GUID Guid1 = {0x6da8a4ff, 0xe52, 0x11d0, {0xa2, 0x86, 0x00, 0xaa, 0x00, 0x30, 0x49, 0x01}};
GUID Guid2 = {0x6da8a4ff, 0xe52, 0x11d0, {0xa2, 0x86, 0x00, 0xaa, 0x00, 0x30, 0x49, 0x02}};
GUID Guid3 = {0x6da8a4ff, 0xe52, 0x11d0, {0xa2, 0x86, 0x00, 0xaa, 0x00, 0x30, 0x49, 0x03}};
GUID Guid4 = {0x6da8a4ff, 0xe52, 0x11d0, {0xa2, 0x86, 0x00, 0xaa, 0x00, 0x30, 0x49, 0x04}};
GUID Guid5 = {0x6da8a4ff, 0xe52, 0x11d0, {0xa2, 0x86, 0x00, 0xaa, 0x00, 0x30, 0x49, 0x05}};
GUID Guid6 = {0x6da8a4ff, 0xe52, 0x11d0, {0xa2, 0x86, 0x00, 0xaa, 0x00, 0x30, 0x49, 0x06}};
GUID Guid7 = {0x6da8a4ff, 0xe52, 0x11d0, {0xa2, 0x86, 0x00, 0xaa, 0x00, 0x30, 0x49, 0x07}};
GUID Guid8 = {0x6da8a4ff, 0xe52, 0x11d0, {0xa2, 0x86, 0x00, 0xaa, 0x00, 0x30, 0x49, 0x08}};


DWORD ObjectTypeListLength= 8;
OBJECT_TYPE_LIST ObjectTypeList[] =
{
    { 0, 0, &Guid0 },
    { 1, 0, &Guid1 },
    { 2, 0, &Guid2 },
    { 2, 0, &Guid3 },
    { 1, 0, &Guid4 },
    { 2, 0, &Guid5 },
    { 3, 0, &Guid6 },
    { 2, 0, &Guid7 }
};

DWORD fNtAccessCheckResult[200] = { 2 };
BOOL fAzAccessCheckResult[200];

DWORD dwNtGrantedAccess[200] = { 0xaabbccdd };
DWORD dwAzGrantedAccess[200];

// S-1-5-21-397955417-626881126-188441444-2101332
ULONG _Sid1[] = {0x00000501, 0x05000000, 0x00000015, 0x17b85159, 0x255d7266, 0x0b3b6364, 0x00201054};
PSID g_Sid1 = (PSID) _Sid1;

ACCESS_MASK g_DesiredAccess;