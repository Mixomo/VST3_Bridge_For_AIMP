////////////////////////////////////////////////////////////////////////////////
//
//  Project:   AIMP
//             Programming Interface
//
//  Target:    v5.40 build 2650
//
//  Purpose:   MUI API
//
//  Author:    Artem Izmaylov
//             © 2006-2025
//             www.aimp.ru
//
//  FPC:       OK
//
unit apiMUI;

{$I apiConfig.inc}

interface

uses
  apiObjects;

const
  SID_IAIMPServiceMUI = '{41494D50-5372-764D-5549-000000000000}';
  IID_IAIMPServiceMUI: TGUID = SID_IAIMPServiceMUI;

type
 
  { IAIMPServiceMUI }

  IAIMPServiceMUI = interface(IUnknown)
  [SID_IAIMPServiceMUI]
    function GetName(out Value: IAIMPString): HRESULT; stdcall;
    function GetValue(KeyPath: IAIMPString; out Value: IAIMPString): HRESULT; stdcall;
    function GetValuePart(KeyPath: IAIMPString; Part: Integer; out Value: IAIMPString): HRESULT; stdcall;
  end;

implementation

end.
