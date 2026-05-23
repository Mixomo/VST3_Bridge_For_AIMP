////////////////////////////////////////////////////////////////////////////////
//
//  Project:   AIMP
//             Programming Interface
//
//  Target:    v5.40 build 2650
//
//  Purpose:   Actions API
//
//  Author:    Artem Izmaylov
//             © 2006-2025
//             www.aimp.ru
//
//  FPC:       OK
//
unit apiActions;

{$I apiConfig.inc}

interface

uses
  apiObjects;

const
  SID_IAIMPAction = '{41494D50-4163-7469-6F6E-000000000000}';
  IID_IAIMPAction: TGUID = SID_IAIMPAction;

  SID_IAIMPActionEvent = '{41494D50-4163-7469-6F6E-4576656E7400}';
  IID_IAIMPActionEvent: TGUID = SID_IAIMPActionEvent;

  SID_IAIMPServiceActionManager = '{41494D50-5372-7641-6374-696F6E4D616E}';
  IID_IAIMPServiceActionManager: TGUID = SID_IAIMPServiceActionManager;

  // IAIMPAction Properties
  AIMP_ACTION_PROPID_CUSTOM				        = 0;
  AIMP_ACTION_PROPID_ID                   = 1;
  AIMP_ACTION_PROPID_NAME                 = 2;
  AIMP_ACTION_PROPID_GROUPNAME            = 3;
  AIMP_ACTION_PROPID_ENABLED              = 4;
  AIMP_ACTION_PROPID_DEFAULTLOCALHOTKEY   = 5;
  AIMP_ACTION_PROPID_DEFAULTGLOBALHOTKEY  = 6;
  AIMP_ACTION_PROPID_DEFAULTGLOBALHOTKEY2 = 7;
  AIMP_ACTION_PROPID_EVENT                = 8;

  // Flags for IAIMPServiceActionManager.MakeHotkey function
  AIMP_ACTION_HOTKEY_MODIFIER_CTRL  = 1;
  AIMP_ACTION_HOTKEY_MODIFIER_ALT   = 2;
  AIMP_ACTION_HOTKEY_MODIFIER_SHIFT = 4;
  AIMP_ACTION_HOTKEY_MODIFIER_WIN   = 8;

type

  { IAIMPAction }

  IAIMPAction = interface(IAIMPPropertyList)
  [SID_IAIMPAction]
  end;

  { IAIMPActionEvent }

  IAIMPActionEvent = interface
  [SID_IAIMPActionEvent]
    procedure OnExecute(Data: IUnknown); stdcall;
  end;

  { IAIMPServiceActionManager }

  IAIMPServiceActionManager = interface(IUnknown)
  [SID_IAIMPServiceActionManager]
    function GetByID(ID: IAIMPString; out Action: IAIMPAction): HRESULT; stdcall;
    function MakeHotkey(Modifiers: Word; Key: Word): Integer; stdcall;
  end;

implementation

end.
