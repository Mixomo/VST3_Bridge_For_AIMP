unit uOptionFrameDemo;

interface

uses
  Windows, SysUtils, AIMPCustomPlugin, apiOptions, apiObjects, apiCore, apiGUI;

type

  { TAIMPDemoPluginOptionForm }

  TAIMPDemoPluginOptionForm = class
  strict private
    FCheckbox: IAIMPUICheckBox;
    FForm: IAIMPUIForm;

    procedure HandlerChanged(const Sender: IUnknown);
  protected
    procedure CreateControls(const AService: IAIMPServiceUI);
  public
    OnModified: TProc;

    constructor Create(AParentWnd: HWND);
    destructor Destroy; override;
    function GetHandle: HWND;
    // External Events
    procedure ApplyLocalization;
    procedure ConfigLoad;
    procedure ConfigSave;
  end;

  { TAIMPDemoPluginOptionFrame }

  TAIMPDemoPluginOptionFrame = class(TInterfacedObject, IAIMPOptionsDialogFrame)
  strict private
    FForm: TAIMPDemoPluginOptionForm;

    procedure HandlerModified;
  protected
    // IAIMPOptionsDialogFrame
    function CreateFrame(ParentWnd: HWND): HWND; stdcall;
    procedure DestroyFrame; stdcall;
    function GetName(out S: IAIMPString): HRESULT; stdcall;
    procedure Notification(ID: Integer); stdcall;
  end;

  { TAIMPDemoPlugin }

  TAIMPDemoPlugin = class(TAIMPCustomPlugin)
  protected
    function InfoGet(Index: Integer): PWideChar; override; stdcall;
    function InfoGetCategories: Cardinal; override; stdcall;
    function Initialize(Core: IAIMPCore): HRESULT; override; stdcall;
  end;

var
  GlobalSettingsOption1: Boolean = False;

implementation

uses
  apiWrappers, apiPlugin, apiWrappersGUI, apiMUI;

{ TAIMPDemoPluginOptionFrame }

function TAIMPDemoPluginOptionFrame.CreateFrame(ParentWnd: HWND): HWND;
begin
  FForm := TAIMPDemoPluginOptionForm.Create(ParentWnd);
  FForm.OnModified := HandlerModified;
  Result := FForm.GetHandle;
end;

procedure TAIMPDemoPluginOptionFrame.DestroyFrame;
begin
  FreeAndNil(FForm);
end;

function TAIMPDemoPluginOptionFrame.GetName(out S: IAIMPString): HRESULT;
begin
  try
    S := MakeString('Custom Frame');
    Result := S_OK;
  except
    Result := E_UNEXPECTED;
  end;
end;

procedure TAIMPDemoPluginOptionFrame.Notification(ID: Integer);
begin
  if FForm <> nil then
    case ID of
      AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_LOCALIZATION:
        FForm.ApplyLocalization;
      AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_LOAD:
        FForm.ConfigLoad;
      AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_SAVE:
        FForm.ConfigSave;
    end;
end;

procedure TAIMPDemoPluginOptionFrame.HandlerModified;
var
  AServiceOptions: IAIMPServiceOptionsDialog;
begin
  if Supports(CoreIntf, IAIMPServiceOptionsDialog, AServiceOptions) then
    AServiceOptions.FrameModified(Self);
end;

{ TAIMPDemoPlugin }

function TAIMPDemoPlugin.InfoGet(Index: Integer): PWideChar;
begin
  case Index of
    AIMP_PLUGIN_INFO_NAME:
      Result := 'OptionFrame Demo';
    AIMP_PLUGIN_INFO_AUTHOR:
      Result := 'Artem Izmaylov';
    AIMP_PLUGIN_INFO_SHORT_DESCRIPTION:
      Result := 'This plugin show how to use both Options and GUI api';
  else
    Result := nil;
  end;
end;

function TAIMPDemoPlugin.InfoGetCategories: Cardinal;
begin
  Result := AIMP_PLUGIN_CATEGORY_ADDONS;
end;

function TAIMPDemoPlugin.Initialize(Core: IAIMPCore): HRESULT;
begin
  Result := inherited Initialize(Core);
  if Succeeded(Result) then
    Core.RegisterExtension(IID_IAIMPServiceOptionsDialog, TAIMPDemoPluginOptionFrame.Create);
end;

{ TAIMPDemoPluginOptionForm }

constructor TAIMPDemoPluginOptionForm.Create(AParentWnd: HWND);
var
  AService: IAIMPServiceUI;
begin
  CoreGetService(IAIMPServiceUI, AService);
  CheckResult(AService.CreateForm(AParentWnd, AIMPUI_SERVICE_CREATEFORM_FLAGS_CHILD, MakeString('DemoForm'), nil, FForm));
  CheckResult(FForm.SetValueAsInt32(AIMPUI_FORM_PROPID_BORDERSTYLE, AIMPUI_FLAGS_BORDERSTYLE_NONE));
  CreateControls(AService);
end;

destructor TAIMPDemoPluginOptionForm.Destroy;
begin
  FForm.Release(False);
  FForm := nil;
  inherited;
end;

function TAIMPDemoPluginOptionForm.GetHandle: HWND;
begin
  Result := FForm.GetHandle;
end;

procedure TAIMPDemoPluginOptionForm.ApplyLocalization;
begin
  FCheckbox.SetValueAsObject(AIMPUI_CHECKBOX_PROPID_CAPTION, LangLoadStringEx('AIMP\acPlaylistCopySelectedToClipboard'));
end;

procedure TAIMPDemoPluginOptionForm.ConfigLoad;
begin
  PropListSetInt32(FCheckbox, AIMPUI_CHECKBOX_PROPID_STATE, Ord(GlobalSettingsOption1));
end;

procedure TAIMPDemoPluginOptionForm.ConfigSave;
begin
  GlobalSettingsOption1 := PropListGetInt32(FCheckbox, AIMPUI_CHECKBOX_PROPID_STATE) <> 0;
end;

procedure TAIMPDemoPluginOptionForm.CreateControls(const AService: IAIMPServiceUI);
begin
  AService.CreateControl(FForm, FForm, nil, TAIMPUINotifyEventAdapter.Create(HandlerChanged), IAIMPUICheckBox, FCheckbox);
end;

procedure TAIMPDemoPluginOptionForm.HandlerChanged(const Sender: IInterface);
begin
  if Assigned(OnModified) then OnModified();
end;

end.
