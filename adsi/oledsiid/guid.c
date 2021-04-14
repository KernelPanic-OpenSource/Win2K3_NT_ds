#define INITGUID

#include "dswarn.h"
#include <ole2.h>

//--------------------------------------------------------------------------
//
//  ADs CLSIDs
//
//--------------------------------------------------------------------------

DEFINE_GUID(LIBID_ADs,0x97D25DB0L,0x0363,0x11CF,0xAB,0xC4,0x02,0x60,0x8C,0x9E,0x75,0x53);

DEFINE_GUID(IID_IADs,0xFD8256D0L,0xFD15,0x11CE,0xAB,0xC4,0x02,0x60,0x8C,0x9E,0x75,0x53);

DEFINE_GUID(IID_IADsContainer,0x001677D0L,0xFD16,0x11CE,0xAB,0xC4,0x02,0x60,0x8C,0x9E,0x75,0x53);

DEFINE_GUID(IID_IADsNamespaces,0x28B96BA0L,0xB330,0x11CF,0xA9,0xAD,0x00,0xAA,0x00,0x6B,0xC1,0x49);

DEFINE_GUID(IID_IADsDomain,0x00E4C220L,0xFD16,0x11CE,0xAB,0xC4,0x02,0x60,0x8C,0x9E,0x75,0x53);


DEFINE_GUID(IID_IADsUser,0x3E37E320L,0x17E2,0x11CF,0xAB,0xC4,0x02,0x60,0x8C,0x9E,0x75,0x53);



DEFINE_GUID(IID_IADsComputerOperations,0xEF497680L,0x1D9F,0x11CF,0xB1,0xF3,0x02,0x60,0x8C,0x9E,0x75,0x53);

DEFINE_GUID(IID_IADsComputer,0xEFE3CC70L,0x1D9F,0x11CF,0xB1,0xF3,0x02,0x60,0x8C,0x9E,0x75,0x53);


DEFINE_GUID(IID_IADsGroup,0x27636B00L,0x410F,0x11CF,0xB1,0xFF,0x02,0x60,0x8C,0x9E,0x75,0x53);

DEFINE_GUID(IID_IADsMembers,0x451A0030L,0x72EC,0x11CF,0xB0,0x3B,0x00,0xAA,0x00,0x6E,0x09,0x75);


DEFINE_GUID(IID_IADsPrintQueue,0xB15160D0L,0x1226,0x11CF,0xA9,0x85,0x00,0xAA,0x00,0x6B,0xC1,0x49);

DEFINE_GUID(IID_IADsPrintQueueOperations,0x124BE5C0L,0x156E,0x11CF,0xA9,0x86,0x00,0xAA,0x00,0x6B,0xC1,0x49);



DEFINE_GUID(IID_IADsPrintJobOperations,0x9A52DB30L,0x1ECF,0x11CF,0xA9,0x88,0x00,0xAA,0x00,0x6B,0xC1,0x49);

DEFINE_GUID(IID_IADsPrintJob,0x32FB6780L,0x1ED0,0x11CF,0xA9,0x88,0x00,0xAA,0x00,0x6B,0xC1,0x49);


DEFINE_GUID(IID_IADsCollection,0x72B945E0L,0x253B,0x11CF,0xA9,0x88,0x00,0xAA,0x00,0x6B,0xC1,0x49);



DEFINE_GUID(IID_IADsServiceOperations,0x5D7B33F0L,0x31CA,0x11CF,0xA9,0x8A,0x00,0xAA,0x00,0x6B,0xC1,0x49);

DEFINE_GUID(IID_IADsService,0x68AF66E0L,0x31CA,0x11CF,0xA9,0x8A,0x00,0xAA,0x00,0x6B,0xC1,0x49);



DEFINE_GUID(IID_IADsFileServiceOperations,0xA02DED10L,0x31CA,0x11CF,0xA9,0x8A,0x00,0xAA,0x00,0x6B,0xC1,0x49);

DEFINE_GUID(IID_IADsFileService,0xA89D1900L,0x31CA,0x11CF,0xA9,0x8A,0x00,0xAA,0x00,0x6B,0xC1,0x49);



DEFINE_GUID(IID_IADsResource,0x34A05B20L,0x4AAB,0x11CF,0xAE,0x2C,0x00,0xAA,0x00,0x6E,0xBF,0xB9);



DEFINE_GUID(IID_IADsSession,0x398B7DA0L,0x4AAB,0x11CF,0xAE,0x2C,0x00,0xAA,0x00,0x6E,0xBF,0xB9);


DEFINE_GUID(IID_IADsFileShare,0xEB6DCAF0L,0x4B83,0x11CF,0xA9,0x95,0x00,0xAA,0x00,0x6B,0xC1,0x49);


DEFINE_GUID(IID_IADsClass, 0xC8F93DD0L, 0x4AE0, 0x11CF, 0x9E, 0x73, 0x00, 0xAA, 0x00, 0x4A, 0x56, 0x91);

DEFINE_GUID(IID_IADsSyntax, 0xC8F93DD2L, 0x4AE0, 0x11CF, 0x9E, 0x73, 0x00, 0xAA, 0x00, 0x4A, 0x56, 0x91);

DEFINE_GUID(IID_IADsProperty, 0xC8F93DD3L, 0x4AE0, 0x11CF, 0x9E, 0x73, 0x00, 0xAA, 0x00, 0x4A, 0x56, 0x91);

DEFINE_GUID(IID_IADsLocality,0xA05E03A2L,0xEFFE,0x11CF,0x8A,0xBC,0x00,0xC0,0x4F,0xD8,0xD5,0x03);

DEFINE_GUID(IID_IADsO,0xA1CD2DC6L,0xEFFE,0x11CF,0x8A,0xBC,0x00,0xC0,0x4F,0xD8,0xD5,0x03);

DEFINE_GUID(IID_IADsOU,0xA2F733B8L,0xEFFE,0x11CF,0x8A,0xBC,0x00,0xC0,0x4F,0xD8,0xD5,0x03);

DEFINE_GUID(IID_IADsOpenDSObject,0xddf2891e,0x0f9c,0x11d0,0x8a,0xd4,0x00,0xc0,0x4f,0xd8,0xd5,0x03);

DEFINE_GUID(IID_IDirectoryObject,0xe798de2c,0x22e4,0x11d0,0x84,0xfe,0x00,0xc0,0x4f,0xd8,0xd5,0x03);

DEFINE_GUID(IID_IDirectorySearch,0x109ba8ec,0x92f0,0x11d0,0xa7,0x90,0x00,0xc0,0x4f,0xd8,0xd5,0xa8);


DEFINE_GUID(IID_IDirectorySchemaMgmt, 0x75db3b9c, 0xa4d8, 0x11d0, 0xa7, 0x9c, 0x00, 0xc0, 0x4f, 0xd8, 0xd5, 0xa8);


DEFINE_GUID(IID_IADsSearch, 0xC69F7780L, 0x4008, 0x11D0, 0xB9, 0x4C, 0x00, 0xC0, 0x4F, 0xD8, 0xD5, 0xA8);

DEFINE_GUID(IID_IADsPropertyList, 0xc6f602b6,0x8f69,0x11d0, 0x85, 0x28, 0x00, 0xc0, 0x4f, 0xd8, 0xd5, 0x03);

DEFINE_GUID(IID_IADsObjectOptions, 0x46f14fda,0x232b,0x11d1, 0xa8, 0x08, 0x00, 0xc0, 0x4f, 0xd8, 0xd5, 0xa8);

DEFINE_GUID(IID_IADsObjOptPrivate, 0x81cbb829,0x1867,0x11d2, 0x92, 0x20, 0x00, 0xc0, 0x4f, 0xb6, 0xd0, 0xd1);

const IID IID_IADsPropertyEntry = {0x05792c8e,0x941f,0x11d0,{0x85,0x29,0x00,0xc0,0x4f,0xd8,0xd5,0x03}};


const CLSID CLSID_PropertyEntry = {0x72d3edc2,0xa4c4,0x11d0,{0x85,0x33,0x00,0xc0,0x4f,0xd8,0xd5,0x03}};


const IID IID_IADsAccessControlEntry = {0xb4f3a14c,0x9bdd,0x11d0,{0x85,0x2c,0x00,0xc0,0x4f,0xd8,0xd5,0x03}};


const CLSID CLSID_AccessControlEntry = {0xb75ac000,0x9bdd,0x11d0,{0x85,0x2c,0x00,0xc0,0x4f,0xd8,0xd5,0x03}};


const IID IID_IADsAccessControlList = {0xb7ee91cc,0x9bdd,0x11d0,{0x85,0x2c,0x00,0xc0,0x4f,0xd8,0xd5,0x03}};


const CLSID CLSID_AccessControlList = {0xb85ea052,0x9bdd,0x11d0,{0x85,0x2c,0x00,0xc0,0x4f,0xd8,0xd5,0x03}};


const IID IID_IADsSecurityDescriptor = {0xb8c787ca,0x9bdd,0x11d0,{0x85,0x2c,0x00,0xc0,0x4f,0xd8,0xd5,0x03}};


const CLSID CLSID_SecurityDescriptor = {0xb958f73c,0x9bdd,0x11d0,{0x85,0x2c,0x00,0xc0,0x4f,0xd8,0xd5,0x03}};

const IID IID_IADsPropertyValue = {0x79fa9ad0,0xa97c,0x11d0,{0x85,0x34,0x00,0xc0,0x4f,0xd8,0xd5,0x03}};

const CLSID CLSID_PropertyValue = {0x7b9e38b0,0xa97c,0x11d0,{0x85,0x34,0x00,0xc0,0x4f,0xd8,0xd5,0x03}};

const IID IID_IADsPropertyValue2 = {0x306e831c,0x5bc7,0x11d1,{0xa3,0xb8,0x00,0xc0,0x4f,0xb9,0x50,0xdc}};

const IID IID_IADsValue = {0x1e3ef0aa,0xaef5,0x11d0,{0x85,0x37,0x00,0xc0,0x4f,0xd8,0xd5,0x03}};

const IID IID_IADsPathname = {0xd592aed4,0xf420,0x11d0,{0xa3,0x6e,0x00,0xc0,0x4f,0xb9,0x50,0xdc}};
const IID IID_IADsPathnameProvider = {0xaacd1d30,0x8bd0,0x11d2,{0x92, 0xa9, 0x00, 0xc0, 0x4f, 0x79, 0xf8, 0x34}};


const CLSID CLSID_Pathname = {0x080d0d78,0xf421,0x11d0,{0xa3,0x6e,0x00,0xc0,0x4f,0xb9,0x50,0xdc}};

const IID IID_IADsADSystemInfo = {0x5BB11929, 0xAFD1, 0x11d2, {0x9C, 0xB9, 0x00, 0x00, 0xF8, 0x7A, 0x36, 0x9E}};
const CLSID CLSID_ADSystemInfo = {0x50B6327F, 0xAFD1, 0x11d2, {0x9C, 0xB9, 0x00, 0x00, 0xF8, 0x7A, 0x36, 0x9E}};

const IID IID_IADsWinNTSystemInfo = {0x6C6D65DC, 0xAFD1, 0x11d2, {0x9C, 0xB9, 0x00, 0x00, 0xF8, 0x7A, 0x36, 0x9E}};
const CLSID CLSID_WinNTSystemInfo = {0x66182EC4, 0xAFD1, 0x11d2, {0x9C, 0xB9, 0x00, 0x00, 0xF8, 0x7A, 0x36, 0x9E}};

const IID IID_IADsDNWithString = {0x370df02e, 0xf934, 0x11d2, {0xba, 0x96, 0x00, 0xc0, 0x4f, 0xb6, 0xd0, 0xd1}};
const CLSID CLSID_DNWithString = {0x334857cc, 0xf934, 0x11d2, {0xba, 0x96, 0x00, 0xc0, 0x4f, 0xb6, 0xd0, 0xd1}};

const IID IID_IADsDNWithBinary = {0x7e99c0a2, 0xf935, 0x11d2, {0xba, 0x96, 0x00, 0xc0, 0x4f, 0xb6, 0xd0, 0xd1}};
const CLSID CLSID_DNWithBinary = {0x7e99c0a3, 0xf935, 0x11d2, {0xba, 0x96, 0x00, 0xc0, 0x4f, 0xb6, 0xd0, 0xd1}};

const IID IID_IADsNameTranslate = {0xb1b272a3,0x3625,0x11d1,{0xa3,0xa4,0x00,0xc0,0x4f,0xb9,0x50,0xdc}};
const CLSID CLSID_NameTranslate = {0x274fae1f,0x3626,0x11d1,{0xa3,0xa4,0x00,0xc0,0x4f,0xb9,0x50,0xdc}};

const IID IID_IADsLargeInteger = {0x9068270b,0x0939,0x11d1,{0x8b,0xe1,0x00,0xc0,0x4f,0xd8,0xd5,0x03}};

const CLSID CLSID_LargeInteger = {0x927971f5,0x0939,0x11d1,{0x8b,0xe1,0x00,0xc0,0x4f,0xd8,0xd5,0x03}};

const IID IID_IADsAcl = {0x8452d3ab,0x0869,0x11d1,{0xa3,0x77,0x00,0xc0,0x4f,0xb9,0x50,0xdc}};

DEFINE_GUID(CLSID_NetAddress,0xb0b71247L,0x4080,0x11D1,0xA3,0xAC,0x00,0xC0,0x4F,0xB9,0x50,0xDC);
const IID IID_IADsNetAddress = {0xb21a50a9,0x4080,0x11d1,{0xa3,0xac,0x00,0xc0,0x4f,0xb9,0x50,0xdc}};

DEFINE_GUID(CLSID_Path,0xb2538919L,0x4080,0x11D1,0xA3,0xAC,0x00,0xC0,0x4F,0xB9,0x50,0xDC);
const IID IID_IADsPath = {0xb287fcd5,0x4080,0x11d1,{0xa3,0xac,0x00,0xc0,0x4f,0xb9,0x50,0xdc}};

DEFINE_GUID(CLSID_Timestamp,0xb2bed2ebL,0x4080,0x11D1,0xA3,0xAC,0x00,0xC0,0x4F,0xB9,0x50,0xDC);
const IID IID_IADsTimestamp = {0xb2f5a901,0x4080,0x11d1,{0xa3,0xac,0x00,0xc0,0x4f,0xb9,0x50,0xdc}};

DEFINE_GUID(CLSID_TypedName,0xb33143cbL,0x4080,0x11D1,0xA3,0xAC,0x00,0xC0,0x4F,0xB9,0x50,0xDC);
const IID IID_IADsTypedName = {0xb371a349,0x4080,0x11d1,{0xa3,0xac,0x00,0xc0,0x4f,0xb9,0x50,0xdc}};

DEFINE_GUID(CLSID_Hold,0xb3ad3e13L,0x4080,0x11D1,0xA3,0xAC,0x00,0xC0,0x4F,0xB9,0x50,0xDC);
const IID IID_IADsHold = {0xb3eb3b37,0x4080,0x11d1,{0xa3,0xac,0x00,0xc0,0x4f,0xb9,0x50,0xdc}};

DEFINE_GUID(CLSID_ReplicaPointer,0xf5d1badfL,0x4080,0x11D1,0xA3,0xAC,0x00,0xC0,0x4F,0xB9,0x50,0xDC);
const IID IID_IADsReplicaPointer= {0xf60fb803,0x4080,0x11d1,{0xa3,0xac,0x00,0xc0,0x4f,0xb9,0x50,0xdc}};

DEFINE_GUID(CLSID_BackLink,0xfcbf906fL,0x4080,0x11D1,0xA3,0xAC,0x00,0xC0,0x4F,0xB9,0x50,0xDC);
const IID IID_IADsBackLink = {0xfd1302bd,0x4080,0x11d1,{0xa3,0xac,0x00,0xc0,0x4f,0xb9,0x50,0xdc}};

DEFINE_GUID(CLSID_PostalAddress,0x0a75afcdL,0x4680,0x11D1,0xA3,0xB4,0x00,0xC0,0x4F,0xB9,0x50,0xDC);
const IID IID_IADsPostalAddress = {0x7adecf29,0x4680,0x11d1,{0xa3,0xb4,0x00,0xc0,0x4f,0xb9,0x50,0xdc}};

DEFINE_GUID(CLSID_OctetList,0x1241400fL,0x4680,0x11D1,0xA3,0xB4,0x00,0xC0,0x4F,0xB9,0x50,0xDC);
const IID IID_IADsOctetList= {0x7b28b80f,0x4680,0x11d1,{0xa3,0xb4,0x00,0xc0,0x4f,0xb9,0x50,0xdc}};

DEFINE_GUID(CLSID_CaseIgnoreList,0x15f88a55L,0x4680,0x11D1,0xA3,0xB4,0x00,0xC0,0x4F,0xB9,0x50,0xDC);
const IID IID_IADsCaseIgnoreList = {0x7b66b533,0x4680,0x11d1,{0xa3,0xb4,0x00,0xc0,0x4f,0xb9,0x50,0xdc}};

DEFINE_GUID(CLSID_FaxNumber,0xa5062215L,0x4681,0x11D1,0xA3,0xB4,0x00,0xC0,0x4F,0xB9,0x50,0xDC);
const IID IID_IADsFaxNumber= {0xa910dea9,0x4680,0x11d1,{0xa3,0xb4,0x00,0xc0,0x4f,0xb9,0x50,0xdc}};

DEFINE_GUID(CLSID_Email,0x8f92a857L,0x478e,0x11D1,0xA3,0xB4,0x00,0xC0,0x4F,0xB9,0x50,0xDC);

const IID IID_IADsEmail= {0x97af011a,0x478e,0x11d1,{0xa3,0xb4,0x00,0xc0,0x4f,0xb9,0x50,0xdc}};

const IID IID_IPrivateDispatch = {0x86ab4bbe,0x65f6,0x11d1,{0x8c,0x13,0x00,0xc0,0x4f,0xd8,0xd5,0x03}};

const IID IID_IPrivateUnknown = {0x89126bab,0x6ead,0x11d1,{0x8c,0x18,0x00,0xc0,0x4f,0xd8,0xd5,0x03}};

DEFINE_GUID(IID_IADsExtension, 0x3d35553c, 0xd2b0, 0x11d1, 0xb1, 0x7b, 0x0, 0x0, 0xf8, 0x75, 0x93, 0xa0);

const IID IID_IADsDeleteOps = {0xb2bd0902,0x8878,0x11d1,{0x8c,0x21,0x00,0xc0,0x4f,0xd8,0xd5,0x03}};


//
// Umi specific GUIDS
//
/*
DEFINE_GUID(IID_IUmiPropList,0x12575a7b,0xd9db,0x11d3,0xa1,0x1f,0x00,0x10,0x5a,0x1f,0x51,0x5a);


DEFINE_GUID(IID_IUmiBaseObject,0x12575a7c,0xd9db,0x11d3,0xa1,0x1f,0x00,0x10,0x5a,0x1f,0x51,0x5a);


DEFINE_GUID(IID_IUmiConnection,0x5ed7ee20,0x64a4,0x11d3,0xa0,0xda,0x00,0x10,0x5a,0x1f,0x51,0x5a);


DEFINE_GUID(IID_IUmiContainer,0x5ed7ee21,0x64a4,0x11d3,0xa0,0xda,0x00,0x10,0x5a,0x1f,0x51,0x5a);


DEFINE_GUID(IID_IUmiObject,0x5ed7ee23,0x64a4,0x11d3,0xa0,0xda,0x00,0x10,0x5a,0x1f,0x51,0x5a);


DEFINE_GUID(IID_IUmiCursor,0x5ed7ee26,0x64a4,0x11d3,0xa0,0xda,0x00,0x10,0x5a,0x1f,0x51,0x5a);


DEFINE_GUID(IID_IUmiObjectSink,0x5ed7ee24,0x64a4,0x11d3,0xa0,0xda,0x00,0x10,0x5a,0x1f,0x51,0x5a);


DEFINE_GUID(IID_IUmiURL,0x12575a7d,0xd9db,0x11d3,0xa1,0x1f,0x00,0x10,0x5a,0x1f,0x51,0x5a);

DEFINE_GUID(IID_IUmiPathKeyList,0xcf779c98,0x4739,0x4fd4,0xa4,0x15,0xda,0x93,0x7a,0x59,0x9f,0x2f);

DEFINE_GUID(IID_IUmiQuery,0x12575a7e,0xd9db,0x11d3,0xa1,0x1f,0x00,0x10,0x5a,0x1f,0x51,0x5a);
*/

//
// LDAP Connection object GUID - uuid(7da2a9c4-0c46-43bd-b04e-d92b1be27c45)
//
DEFINE_GUID(CLSID_LDAPConnectionObject,0x7da2a9c4,0x0c46,0x43bd,0xb0,0x4e,0xd9,0x2b,0x1b,0xe2,0x7c,0x45);

// 
// WinNT Connection object GUID - uuid(7992c6eb-d142-4332-831e-3154c50a8316)
//
DEFINE_GUID(CLSID_WinNTConnectionObject,0x7992c6eb,0xd142,0x4332,0x83,0x1e,0x31,0x54,0xc5,0x0a,0x83,0x16);

//
// CLSID to represent non-extension objects for IUmiCustomInterface calls
//
DEFINE_GUID(CLSID_WinNTObject,0xb8324185,0x4050,0x4220,0x98,0x0a,0xab,0x14,0x62,0x3e,0x06,0x3a);

//
// CLSID to represent non-extension interfaces for LDAP objects - 05709878-5195-466c-9e64-487ce3ca20bf
//
DEFINE_GUID(CLSID_LDAPObject,0x05709878,0x5195,0x466c,0x9e,0x64,0x48,0x7c,0xe3,0xca,0x20,0xbf);

//
// LDAP Umi Query object GUID - uuid(cd5d4d76-a818-4f95-b958-7970fd9412ca)
//
DEFINE_GUID(CLSID_UmiLDAPQueryObject, 0xcd5d4d76,0xa818,0x4f95,0xb9,0x58,0x79,0x70,0xfd,0x94,0x12,0xca);

//
// Private Umi helper routine GUID - 4fe243f0-ad89-4cbc-9b14-486126446ae0.
//
DEFINE_GUID(IID_IADsUmiHelperPrivate, 0x4fe243f0,0xad89,0x4cbc,0x9b,0x14,0x48,0x61,0x26,0x44,0x6a,0xe0);

DEFINE_GUID(IID_IUmiADSIPrivate,0xcfcecb01,0x3123,0x4926,0xb5,0xe3,0x62,0x78,0x08,0x27,0x26,0x43);

//
// Definition for private ACE helper interface GUID - fd145df2-fd96-4135-9b22-68ff0f6bf5bb
//
DEFINE_GUID(IID_IADsAcePrivate, 0xfd145df2, 0xfd96, 0x4135,0x9b, 0x22, 0x68, 0xff, 0x0f, 0x6b, 0xf5, 0xbb);

//
// ADS Util related GUIDs.
//
DEFINE_GUID(CLSID_ADsSecurityUtility, 0xf270c64a, 0xffb8, 0x4ae4, 0x85, 0xfe, 0x3a, 0x75, 0xe5, 0x34, 0x79, 0x66);
DEFINE_GUID(IID_IADsSecurityUtility, 0xa63251b2, 0x5f21, 0x474b, 0xab, 0x52, 0x4a, 0x8e, 0xfa, 0xd1, 0x08, 0x95);

//
// Define the OLE DB specific GUIDs
//

DEFINE_GUID(CLSID_ADsDSOObject,0x549365D0L,0xEC26,0x11CF,0x83,0x10,0x00,0xAA,0x00,0xB5,0x05,0xDB);

DEFINE_GUID(DBGUID_LDAPDialect, 0xEFF65380L, 0x9C98, 0x11CF, 0xB9, 0x63, 0x00, 0xAA, 0x00, 0x44, 0x77, 0x3D);

DEFINE_GUID(DBPROPSET_ADSISEARCH, 0xcfcfc928, 0x9aa2, 0x11d0, 0xa7, 0x9a, 0x00, 0xc0, 0x4f, 0xd8, 0xd5, 0xa8);

DEFINE_GUID(DBPROPSET_ADSIBIND, 0x6da66dc8, 0xb7e8, 0x11d2, 0x9d, 0x60, 0x0, 0xc0, 0x4f, 0x68, 0x93, 0x45);

#define DBINITCONSTANTS
#include "oledb.h"
#include "oledberr.h"
#include "msdaguid.h"
#include "msdatt.h"
#include "msdadc.h"


