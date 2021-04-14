//+--------------------------------------------------------------------------
//
// Microsoft Windows
// Copyright (C) Microsoft Corporation, 1996 - 1999
//
// File:        pch.cpp
//
// Contents:    Cert Server precompiled header
//
// History:     25-Jul-96       vich created
//
//---------------------------------------------------------------------------

#define __DIR__		"certcli"

#include <windows.h>
#include <atlbase.h>

//You may derive a class from CComModule and use it if you want to override
//something, but do not change the name of _Module
extern CComModule _Module;

#include <atlcom.h>

#include <certsrv.h>
#include "certlib.h"

#pragma hdrstop
