#include <windows.h>
#include <wincrypt.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <zlib.h>
#include "compress.h"

BYTE rgbTestCer [] = {
0x30, 0x82, 0x05, 0x8f, 0x30, 0x82, 0x04, 0xf8, 0xa0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x0a, 0x1e,
0x39, 0xa7, 0x1c, 0x00, 0x01, 0x00, 0x0f, 0xc9, 0x64, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48,
0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, 0x00, 0x30, 0x4b, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03,
0x55, 0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31, 0x12, 0x30, 0x10, 0x06, 0x03, 0x55, 0x04, 0x0a,
0x13, 0x09, 0x4d, 0x69, 0x63, 0x72, 0x6f, 0x73, 0x6f, 0x66, 0x74, 0x31, 0x0e, 0x30, 0x0c, 0x06,
0x03, 0x55, 0x04, 0x0b, 0x13, 0x05, 0x4e, 0x74, 0x64, 0x65, 0x76, 0x31, 0x18, 0x30, 0x16, 0x06,
0x03, 0x55, 0x04, 0x03, 0x13, 0x0f, 0x4e, 0x54, 0x44, 0x45, 0x56, 0x20, 0x49, 0x53, 0x53, 0x55,
0x45, 0x33, 0x20, 0x43, 0x41, 0x30, 0x1e, 0x17, 0x0d, 0x30, 0x31, 0x31, 0x31, 0x31, 0x39, 0x32,
0x31, 0x32, 0x39, 0x31, 0x34, 0x5a, 0x17, 0x0d, 0x30, 0x32, 0x30, 0x39, 0x32, 0x30, 0x32, 0x31,
0x33, 0x33, 0x32, 0x38, 0x5a, 0x30, 0x7b, 0x31, 0x13, 0x30, 0x11, 0x06, 0x0a, 0x09, 0x92, 0x26,
0x89, 0x93, 0xf2, 0x2c, 0x64, 0x01, 0x19, 0x16, 0x03, 0x63, 0x6f, 0x6d, 0x31, 0x19, 0x30, 0x17,
0x06, 0x0a, 0x09, 0x92, 0x26, 0x89, 0x93, 0xf2, 0x2c, 0x64, 0x01, 0x19, 0x16, 0x09, 0x6d, 0x69,
0x63, 0x72, 0x6f, 0x73, 0x6f, 0x66, 0x74, 0x31, 0x15, 0x30, 0x13, 0x06, 0x0a, 0x09, 0x92, 0x26,
0x89, 0x93, 0xf2, 0x2c, 0x64, 0x01, 0x19, 0x16, 0x05, 0x6e, 0x74, 0x64, 0x65, 0x76, 0x31, 0x0c,
0x30, 0x0a, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x03, 0x49, 0x54, 0x47, 0x31, 0x0e, 0x30, 0x0c,
0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x05, 0x55, 0x73, 0x65, 0x72, 0x73, 0x31, 0x14, 0x30, 0x12,
0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x0b, 0x44, 0x61, 0x6e, 0x20, 0x47, 0x72, 0x69, 0x66, 0x66,
0x69, 0x6e, 0x30, 0x81, 0x9f, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01,
0x01, 0x01, 0x05, 0x00, 0x03, 0x81, 0x8d, 0x00, 0x30, 0x81, 0x89, 0x02, 0x81, 0x81, 0x00, 0x89,
0x9f, 0x70, 0xb2, 0x5e, 0xfd, 0x99, 0x31, 0xb8, 0xcd, 0x17, 0xba, 0x2f, 0x7c, 0xb9, 0xed, 0xde,
0x56, 0xff, 0xb3, 0x37, 0x78, 0xd0, 0x51, 0xae, 0x14, 0x7c, 0xae, 0x91, 0x1f, 0xb0, 0x26, 0x87,
0xaf, 0x43, 0x3e, 0xde, 0x59, 0xbb, 0xc8, 0xcf, 0xbe, 0x25, 0x03, 0x0a, 0x1c, 0xd8, 0x18, 0x4d,
0x1a, 0xbd, 0xe3, 0xb0, 0x73, 0xc9, 0x2b, 0x29, 0x0b, 0x0a, 0x12, 0xdd, 0x55, 0x37, 0xcb, 0x2b,
0x8f, 0xf2, 0xe6, 0x2c, 0x2e, 0x7f, 0x8d, 0x71, 0x9a, 0x77, 0xf6, 0x4e, 0x4e, 0x3e, 0x94, 0x2e,
0xdb, 0x3c, 0xd4, 0xde, 0x32, 0x1f, 0xc7, 0xb9, 0x96, 0x72, 0xbb, 0x0d, 0x80, 0xc9, 0xc0, 0x3e,
0x84, 0xee, 0x33, 0x3c, 0x62, 0x46, 0x17, 0x7d, 0x27, 0x83, 0x15, 0xdd, 0x2f, 0x2f, 0x0a, 0xb3,
0xcf, 0x76, 0xf6, 0x9b, 0x0d, 0x70, 0x6d, 0x99, 0x5b, 0xca, 0xba, 0x07, 0x8a, 0x44, 0xd3, 0x02,
0x03, 0x01, 0x00, 0x01, 0xa3, 0x82, 0x03, 0x48, 0x30, 0x82, 0x03, 0x44, 0x30, 0x0b, 0x06, 0x03,
0x55, 0x1d, 0x0f, 0x04, 0x04, 0x03, 0x02, 0x05, 0xa0, 0x30, 0x1d, 0x06, 0x03, 0x55, 0x1d, 0x0e,
0x04, 0x16, 0x04, 0x14, 0x38, 0xef, 0x1a, 0xde, 0x6f, 0x3e, 0xa8, 0x73, 0x86, 0x74, 0xb8, 0x27,
0x4b, 0x9e, 0x8a, 0x98, 0xf7, 0x67, 0x70, 0x47, 0x30, 0x2b, 0x06, 0x09, 0x2b, 0x06, 0x01, 0x04,
0x01, 0x82, 0x37, 0x14, 0x02, 0x04, 0x1e, 0x1e, 0x1c, 0x00, 0x53, 0x00, 0x6d, 0x00, 0x61, 0x00,
0x72, 0x00, 0x74, 0x00, 0x63, 0x00, 0x61, 0x00, 0x72, 0x00, 0x64, 0x00, 0x4c, 0x00, 0x6f, 0x00,
0x67, 0x00, 0x6f, 0x00, 0x6e, 0x30, 0x1f, 0x06, 0x03, 0x55, 0x1d, 0x23, 0x04, 0x18, 0x30, 0x16,
0x80, 0x14, 0xc9, 0x44, 0x56, 0x4a, 0x90, 0x13, 0x7c, 0xa9, 0xf3, 0x33, 0x06, 0x6b, 0xde, 0xd0,
0x99, 0xbb, 0xe7, 0xc8, 0xce, 0xe9, 0x30, 0x82, 0x01, 0x26, 0x06, 0x03, 0x55, 0x1d, 0x1f, 0x04,
0x82, 0x01, 0x1d, 0x30, 0x82, 0x01, 0x19, 0x30, 0x82, 0x01, 0x15, 0xa0, 0x82, 0x01, 0x11, 0xa0,
0x82, 0x01, 0x0d, 0x86, 0x81, 0xc4, 0x6c, 0x64, 0x61, 0x70, 0x3a, 0x2f, 0x2f, 0x2f, 0x43, 0x4e,
0x3d, 0x4e, 0x54, 0x44, 0x45, 0x56, 0x25, 0x32, 0x30, 0x49, 0x53, 0x53, 0x55, 0x45, 0x33, 0x25,
0x32, 0x30, 0x43, 0x41, 0x2c, 0x43, 0x4e, 0x3d, 0x57, 0x48, 0x49, 0x43, 0x41, 0x33, 0x2c, 0x43,
0x4e, 0x3d, 0x43, 0x44, 0x50, 0x2c, 0x43, 0x4e, 0x3d, 0x50, 0x75, 0x62, 0x6c, 0x69, 0x63, 0x25,
0x32, 0x30, 0x4b, 0x65, 0x79, 0x25, 0x32, 0x30, 0x53, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x73,
0x2c, 0x43, 0x4e, 0x3d, 0x53, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x73, 0x2c, 0x43, 0x4e, 0x3d,
0x43, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x75, 0x72, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x2c, 0x44, 0x43,
0x3d, 0x6e, 0x74, 0x64, 0x65, 0x76, 0x2c, 0x44, 0x43, 0x3d, 0x6d, 0x69, 0x63, 0x72, 0x6f, 0x73,
0x6f, 0x66, 0x74, 0x2c, 0x44, 0x43, 0x3d, 0x63, 0x6f, 0x6d, 0x3f, 0x63, 0x65, 0x72, 0x74, 0x69,
0x66, 0x69, 0x63, 0x61, 0x74, 0x65, 0x52, 0x65, 0x76, 0x6f, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e,
0x4c, 0x69, 0x73, 0x74, 0x3f, 0x62, 0x61, 0x73, 0x65, 0x3f, 0x6f, 0x62, 0x6a, 0x65, 0x63, 0x74,
0x43, 0x6c, 0x61, 0x73, 0x73, 0x3d, 0x63, 0x52, 0x4c, 0x44, 0x69, 0x73, 0x74, 0x72, 0x69, 0x62,
0x75, 0x74, 0x69, 0x6f, 0x6e, 0x50, 0x6f, 0x69, 0x6e, 0x74, 0x86, 0x44, 0x68, 0x74, 0x74, 0x70,
0x3a, 0x2f, 0x2f, 0x77, 0x68, 0x69, 0x63, 0x61, 0x33, 0x2e, 0x6e, 0x74, 0x64, 0x65, 0x76, 0x2e,
0x6d, 0x69, 0x63, 0x72, 0x6f, 0x73, 0x6f, 0x66, 0x74, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x43, 0x65,
0x72, 0x74, 0x45, 0x6e, 0x72, 0x6f, 0x6c, 0x6c, 0x2f, 0x4e, 0x54, 0x44, 0x45, 0x56, 0x25, 0x32,
0x30, 0x49, 0x53, 0x53, 0x55, 0x45, 0x33, 0x25, 0x32, 0x30, 0x43, 0x41, 0x2e, 0x63, 0x72, 0x6c,
0x30, 0x82, 0x01, 0x42, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x01, 0x01, 0x04, 0x82,
0x01, 0x34, 0x30, 0x82, 0x01, 0x30, 0x30, 0x81, 0xbd, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05,
0x07, 0x30, 0x02, 0x86, 0x81, 0xb0, 0x6c, 0x64, 0x61, 0x70, 0x3a, 0x2f, 0x2f, 0x2f, 0x43, 0x4e,
0x3d, 0x4e, 0x54, 0x44, 0x45, 0x56, 0x25, 0x32, 0x30, 0x49, 0x53, 0x53, 0x55, 0x45, 0x33, 0x25,
0x32, 0x30, 0x43, 0x41, 0x2c, 0x43, 0x4e, 0x3d, 0x41, 0x49, 0x41, 0x2c, 0x43, 0x4e, 0x3d, 0x50,
0x75, 0x62, 0x6c, 0x69, 0x63, 0x25, 0x32, 0x30, 0x4b, 0x65, 0x79, 0x25, 0x32, 0x30, 0x53, 0x65,
0x72, 0x76, 0x69, 0x63, 0x65, 0x73, 0x2c, 0x43, 0x4e, 0x3d, 0x53, 0x65, 0x72, 0x76, 0x69, 0x63,
0x65, 0x73, 0x2c, 0x43, 0x4e, 0x3d, 0x43, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x75, 0x72, 0x61, 0x74,
0x69, 0x6f, 0x6e, 0x2c, 0x44, 0x43, 0x3d, 0x6e, 0x74, 0x64, 0x65, 0x76, 0x2c, 0x44, 0x43, 0x3d,
0x6d, 0x69, 0x63, 0x72, 0x6f, 0x73, 0x6f, 0x66, 0x74, 0x2c, 0x44, 0x43, 0x3d, 0x63, 0x6f, 0x6d,
0x3f, 0x63, 0x41, 0x43, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x63, 0x61, 0x74, 0x65, 0x3f, 0x62,
0x61, 0x73, 0x65, 0x3f, 0x6f, 0x62, 0x6a, 0x65, 0x63, 0x74, 0x43, 0x6c, 0x61, 0x73, 0x73, 0x3d,
0x63, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x41, 0x75, 0x74,
0x68, 0x6f, 0x72, 0x69, 0x74, 0x79, 0x30, 0x6e, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07,
0x30, 0x02, 0x86, 0x62, 0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x77, 0x68, 0x69, 0x63, 0x61,
0x33, 0x2e, 0x6e, 0x74, 0x64, 0x65, 0x76, 0x2e, 0x6d, 0x69, 0x63, 0x72, 0x6f, 0x73, 0x6f, 0x66,
0x74, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x43, 0x65, 0x72, 0x74, 0x45, 0x6e, 0x72, 0x6f, 0x6c, 0x6c,
0x2f, 0x57, 0x48, 0x49, 0x43, 0x41, 0x33, 0x2e, 0x6e, 0x74, 0x64, 0x65, 0x76, 0x2e, 0x6d, 0x69,
0x63, 0x72, 0x6f, 0x73, 0x6f, 0x66, 0x74, 0x2e, 0x63, 0x6f, 0x6d, 0x5f, 0x4e, 0x54, 0x44, 0x45,
0x56, 0x25, 0x32, 0x30, 0x49, 0x53, 0x53, 0x55, 0x45, 0x33, 0x25, 0x32, 0x30, 0x43, 0x41, 0x28,
0x31, 0x29, 0x2e, 0x63, 0x72, 0x74, 0x30, 0x1f, 0x06, 0x03, 0x55, 0x1d, 0x25, 0x04, 0x18, 0x30,
0x16, 0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x14, 0x02, 0x02, 0x06, 0x08, 0x2b,
0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x02, 0x30, 0x37, 0x06, 0x03, 0x55, 0x1d, 0x11, 0x04, 0x30,
0x30, 0x2e, 0xa0, 0x2c, 0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x14, 0x02, 0x03,
0xa0, 0x1e, 0x0c, 0x1c, 0x64, 0x61, 0x6e, 0x67, 0x72, 0x69, 0x66, 0x66, 0x40, 0x6e, 0x74, 0x64,
0x65, 0x76, 0x2e, 0x6d, 0x69, 0x63, 0x72, 0x6f, 0x73, 0x6f, 0x66, 0x74, 0x2e, 0x63, 0x6f, 0x6d,
0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, 0x00, 0x03,
0x81, 0x81, 0x00, 0x92, 0xe6, 0x34, 0x6a, 0x1b, 0x71, 0xe6, 0x91, 0x4a, 0x92, 0x35, 0x00, 0x2d,
0xe3, 0x20, 0x50, 0x68, 0x01, 0x7d, 0x92, 0xe7, 0xc1, 0x5c, 0xfd, 0x13, 0xb5, 0x49, 0x31, 0xc5,
0xc5, 0x0d, 0x5f, 0xa5, 0xf3, 0xa6, 0xd1, 0xb4, 0x28, 0x7b, 0x70, 0xfd, 0x16, 0xd2, 0x60, 0x3a,
0xa9, 0xa5, 0x39, 0x08, 0xed, 0x36, 0x76, 0xa5, 0x44, 0xf3, 0x45, 0x8e, 0x56, 0x63, 0xd6, 0xfe,
0x0e, 0xbd, 0x41, 0xf0, 0xdf, 0x2c, 0xa7, 0xdf, 0x03, 0xda, 0xf0, 0x35, 0x2f, 0x51, 0xab, 0xa3,
0x0d, 0x94, 0xb2, 0x89, 0x12, 0xe0, 0x30, 0x6f, 0xee, 0x1f, 0x09, 0x21, 0xe4, 0x3e, 0x51, 0x4f,
0xf0, 0x4a, 0xb3, 0x30, 0x87, 0xef, 0x7a, 0x49, 0x2f, 0x0e, 0x30, 0x4d, 0xd0, 0xd5, 0x4b, 0xfc,
0x77, 0xac, 0x81, 0xb8, 0xf1, 0x36, 0xfa, 0x9e, 0xbb, 0x35, 0x5b, 0xf7, 0x4a, 0x5f, 0x81, 0x16,
0x98, 0x27, 0xd7
};

#define cbTestCer sizeof(rgbTestCer)

int _cdecl main(int argc, char * argv[])
{
    DWORD dwSts = ERROR_SUCCESS;
    DWORD cbIn = 0;
    DWORD cbOut = 0;
    DWORD cbOut2 = 0;
    PBYTE pbOut = NULL;
    PBYTE pbOut2 = NULL;

    dwSts = CompressData(
        cbTestCer,
        rgbTestCer,
        &cbOut,
        NULL);

    if (ERROR_SUCCESS != dwSts)
        goto Ret;

    pbOut = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cbOut);

    if (NULL == pbOut)
    {
        dwSts = ERROR_NOT_ENOUGH_MEMORY;
        goto Ret;
    }

    dwSts = CompressData(
        cbTestCer,
        rgbTestCer,
        &cbOut,
        pbOut);

    if (ERROR_SUCCESS != dwSts)
        goto Ret;

    printf("Compression ratio = %f\n", 
        (float) cbTestCer / (float) cbOut);

    dwSts = UncompressData(
        cbOut,
        pbOut,
        &cbOut2,
        NULL);

    if (ERROR_SUCCESS != dwSts)
        goto Ret;

    pbOut2 = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cbOut2);

    if (NULL == pbOut2)
    {
        dwSts = ERROR_NOT_ENOUGH_MEMORY;
        goto Ret;
    }

    dwSts = UncompressData(
        cbOut,
        pbOut,
        &cbOut2,
        pbOut2);

    if (ERROR_SUCCESS != dwSts)
        goto Ret;

    if (cbOut2 != cbTestCer)
    {
        printf("ERROR: data lengths don't match\n");
        dwSts = ERROR_INTERNAL_ERROR;
        goto Ret;
    }

    if (0 != memcmp(pbOut2, rgbTestCer, cbOut2))
    {
        printf("ERROR: data doesn't match\n");
        dwSts = ERROR_INTERNAL_ERROR;
        goto Ret;
    }

Ret:
    
    if (ERROR_SUCCESS != dwSts)
        printf(" failed, 0x%x\n", dwSts);
    else
        printf("Success.\n");

    if (pbOut)
        HeapFree(GetProcessHeap(), 0, pbOut);
    if (pbOut2)
        HeapFree(GetProcessHeap(), 0, pbOut2);

    return 0;
}

