////////////////////////////////////////////////////////////////////////////////
//
//  Project:   AIMP
//             Programming Interface
//
//  Target:    v5.40 build 2650
//
//  Purpose:   UpdateChecker Internal API
//
//  Author:    Artem Izmaylov
//             © 2006-2025
//             www.aimp.ru
//
//  FPC:       OK
//
unit apiUpdater;

{$I apiConfig.inc}

interface

const
  SID_IAIMPServiceUpdater = '{41494D50-5372-7655-7064-617465720000}';
  IID_IAIMPServiceUpdater: TGUID = SID_IAIMPServiceUpdater;

type

  { IAIMPServiceUpdater }

  IAIMPServiceUpdater = interface
  [SID_IAIMPServiceUpdater]
    function CheckForUpdates: HRESULT; stdcall;
  end;

implementation
end.
