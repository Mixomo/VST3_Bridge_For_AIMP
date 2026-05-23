unit uInfoBarDemo;

interface

uses
  Windows,
  // api
  apiFileManager,
  apiOptions,
  apiObjects,
  apiCore,
  apiPlugin,
  apiPlayer,
  apiMessages,
  apiWrappers,
  //
  AIMPCustomPlugin,
  uInfoBarDemoForm;

type

  { TAIMPDemoPlugin }

  TAIMPDemoPlugin = class(TAIMPCustomPlugin, IAIMPMessageHook)
  strict private
    FCard: TfrmCard;

    procedure ShowPlayingFileInfo;
    // IAIMPMessageHook
    procedure CoreMessage(Message: Cardinal; Param1: Integer; Param2: Pointer; var Result: HRESULT); stdcall;
  protected
    procedure Finalize; override; stdcall;
    function InfoGet(Index: Integer): PWideChar; override; stdcall;
    function InfoGetCategories: Cardinal; override; stdcall;
    function Initialize(Core: IAIMPCore): HRESULT; override; stdcall;
  end;

implementation

uses
  SysUtils;

{ TAIMPDemoPlugin }

procedure TAIMPDemoPlugin.CoreMessage(Message: Cardinal; Param1: Integer; Param2: Pointer; var Result: HRESULT);
begin
  case Message of
    AIMP_MSG_EVENT_STREAM_START,
    AIMP_MSG_EVENT_STREAM_START_SUBTRACK,
    AIMP_MSG_EVENT_STREAM_END,
    AIMP_MSG_EVENT_PLAYING_FILE_INFO:
      ShowPlayingFileInfo;
  end;
end;

procedure TAIMPDemoPlugin.Finalize;
var
  LService: IAIMPServiceMessageDispatcher;
begin
  if CoreGetService(IAIMPServiceMessageDispatcher, LService) then
    LService.Unhook(Self);
  FreeAndNil(FCard);
  inherited;
end;

function TAIMPDemoPlugin.InfoGet(Index: Integer): PWideChar;
begin
  case Index of
    AIMP_PLUGIN_INFO_NAME:
      Result := 'InfoBar Demo';
    AIMP_PLUGIN_INFO_AUTHOR:
      Result := 'Artem Izmaylov';
    AIMP_PLUGIN_INFO_SHORT_DESCRIPTION:
      Result := 'This plugin show how to fetch info about playing track';
  else
    Result := nil;
  end;
end;

function TAIMPDemoPlugin.InfoGetCategories: Cardinal;
begin
  Result := AIMP_PLUGIN_CATEGORY_ADDONS;
end;

function TAIMPDemoPlugin.Initialize(Core: IAIMPCore): HRESULT;
var
  LService: IAIMPServiceMessageDispatcher;
begin
  Result := inherited Initialize(Core);
  if CoreGetService(IAIMPServiceMessageDispatcher, LService) then
    LService.Hook(Self);
end;

procedure TAIMPDemoPlugin.ShowPlayingFileInfo;
var
  LService: IAIMPServicePlayer;
  LInfo: IAIMPFileInfo;
begin
  if CoreGetService(IAIMPServicePlayer, LService) and Succeeded(LService.GetInfo(LInfo)) then
  begin
    if FCard = nil then
      FCard := TfrmCard.Create(nil);
    FCard.UpdateInfo(LInfo);
    FCard.Show;
  end
  else
    if FCard <> nil then
      FCard.UpdateInfo(nil);
end;

end.
