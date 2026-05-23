////////////////////////////////////////////////////////////////////////////////
//
//  Project:   AIMP
//             Programming Interface
//
//  Target:    v5.40 build 2650
//
//  Purpose:   General Types
//
//  Author:    Artem Izmaylov
//             © 2006-2025
//             www.aimp.ru
//
//  FPC:       OK
//
unit apiTypes;

{$I apiConfig.inc}

interface

uses
{$IFDEF MSWINDOWS}
  Windows;
{$ELSE}
  Cairo, LCLType;
{$ENDIF}

{$IFNDEF MSWINDOWS}
const
  E_ABORT = HRESULT($80004004);
  E_ACCESSDENIED = HRESULT($80070005);
  E_FAIL = HRESULT($80004005);
  E_HANDLE = HRESULT($80070006);
  E_INVALIDARG = HRESULT($80070057);
  E_OUTOFMEMORY = HRESULT($8007000E);
  E_PENDING = HRESULT($8000000A);
{$ENDIF}

type
  HWND     = {$IFDEF FPC}LCLType{$ELSE}Windows{$ENDIF}.HWND;
  HBITMAP  = {$IFDEF FPC}LCLType{$ELSE}Windows{$ENDIF}.HBITMAP;
  PRGBQuad = {$IFDEF FPC}LCLType{$ELSE}Windows{$ENDIF}.PRGBQuad;
  TRGBQuad = {$IFDEF FPC}LCLType{$ELSE}Windows{$ENDIF}.TRGBQuad;

{$IFDEF MSWINDOWS}
  HCANVAS = HDC;
{$ELSE}
  HCANVAS = Pcairo_t; {$MESSAGE WARN 'HCANVAS не описан'}
{$ENDIF}

  TTaskHandle = NativeUInt;

function Failed(Status: HRESULT): Boolean;
function Succeeded(Status: HRESULT): Boolean;
implementation

function Failed(Status: HRESULT): Boolean;
begin
  Result := Status and HRESULT($80000000) <> 0;
end;

function Succeeded(Status: HRESULT): Boolean;
begin
  Result := Status and HRESULT($80000000) = 0;
end;

end.
