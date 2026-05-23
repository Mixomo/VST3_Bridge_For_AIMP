////////////////////////////////////////////////////////////////////////////////
//
//  Project:   AIMP
//             Programming Interface
//
//  Target:    v5.40 build 2650
//
//  Purpose:   General Wrappers
//
//  Author:    Artem Izmaylov
//             © 2006-2025
//             www.aimp.ru
//
//  FPC:       OK
//
unit apiWrappers;

{$I apiConfig.inc}

interface

uses
{$IFDEF FPC}
  LCLIntf,
  LCLType,
{$ELSE}
  Windows,
{$ENDIF}
  // System
  Classes,
  Math,
  SysUtils,
  Types,
  // API
  apiCore,
  apiObjects,
  apiFileManager,
  apiMUI,
  apiTypes;

type

  { TInterfacedObjectEx }

  TInterfacedObjectEx = class(TObject, IUnknown)
  strict private
    FRefCount: Integer;
  protected
    // IUnknown
    function QueryInterface({$IFDEF FPC}constref{$ELSE}const{$ENDIF}
      IID: TGUID; out Obj): HRESULT; virtual; {$IFDEF MSWINDOWS}stdcall{$ELSE}cdecl{$ENDIF};
    function _AddRef: Integer; {$IFDEF MSWINDOWS}stdcall{$ELSE}cdecl{$ENDIF};
    function _Release: Integer; {$IFDEF MSWINDOWS}stdcall{$ELSE}cdecl{$ENDIF};
  public
    procedure AfterConstruction; override;
    procedure BeforeDestruction; override;
    class function NewInstance: TObject; override;
    property RefCount: Integer read FRefCount;
  end;

  { TAIMPAPIWrappers }

  TAIMPAPIWrappers = class
  public
    class procedure Finalize;
    class procedure Initialize(Core: IAIMPCore);
  end;

  { TAIMPExtensionFileFormat }

  TAIMPExtensionFileFormat = class(TInterfacedObjectEx, IAIMPExtensionFileFormat)
  private
    FDescription: string;
    FExtList: string;
    FFlags: Cardinal;
  public
    constructor Create(const ADescription, AExtList: string;
      AFlags: Cardinal = AIMP_SERVICE_FILEFORMATS_CATEGORY_AUDIO);
    // IAIMPExtensionFileFormat
    function GetDescription(out S: IAIMPString): HRESULT; stdcall;
    function GetExtList(out S: IAIMPString): HRESULT; stdcall;
    function GetFlags(out Flags: Cardinal): HRESULT; stdcall;
  end;

  { TAIMPPropertyList }

  TAIMPPropertyList = class(TInterfacedObjectEx,
    IAIMPPropertyList,
    IAIMPPropertyList2)
  protected
    FCustomObject: IUnknown;

    function CheckAccess(out AResult: HRESULT): Boolean; virtual;
    // IAIMPPropertyList
    procedure DoBeginUpdate; virtual;
    procedure DoEndUpdate; virtual;
    procedure DoGetValueAsFloat(PropertyID: Integer; out Value: Double; var Result: HRESULT); virtual;
    procedure DoGetValueAsInt32(PropertyID: Integer; out Value: Integer; var Result: HRESULT); virtual;
    procedure DoGetValueAsInt64(PropertyID: Integer; out Value: Int64; var Result: HRESULT); virtual;
    function DoGetValueAsObject(PropertyID: Integer): IUnknown; virtual;
    procedure DoGetValueAsVariant(PropertyID: Integer; out Value: OleVariant; var Result: HRESULT); virtual;
    procedure DoSetValueAsFloat(PropertyID: Integer; const Value: Double; var Result: HRESULT); virtual;
    procedure DoSetValueAsInt32(PropertyID: Integer; const Value: Integer; var Result: HRESULT); virtual;
    procedure DoSetValueAsInt64(PropertyID: Integer; const Value: Int64; var Result: HRESULT); virtual;
    procedure DoSetValueAsObject(PropertyID: Integer; const Value: IUnknown; var Result: HRESULT); virtual;
    procedure DoSetValueAsVariant(PropertyID: Integer; const Value: OleVariant; var Result: HRESULT); virtual;
    function DoReset: HRESULT; virtual;
  public
    // IAIMPPropertyList
    procedure BeginUpdate; stdcall;
    procedure EndUpdate; stdcall;
    function GetValueAsFloat(PropertyID: Integer; out Value: Double): HRESULT; stdcall;
    function GetValueAsInt32(PropertyID: Integer; out Value: Integer): HRESULT; stdcall;
    function GetValueAsInt64(PropertyID: Integer; out Value: Int64): HRESULT; stdcall;
    function GetValueAsObject(PropertyID: Integer; const IID: TGUID; out Value): HRESULT; stdcall;
    function SetValueAsFloat(PropertyID: Integer; const Value: Double): HRESULT; stdcall;
    function SetValueAsInt32(PropertyID: Integer; Value: Integer): HRESULT; stdcall;
    function SetValueAsInt64(PropertyID: Integer; const Value: Int64): HRESULT; stdcall;
    function SetValueAsObject(PropertyID: Integer; Value: IInterface): HRESULT; stdcall;
    function Reset: HRESULT; stdcall;
    // IAIMPPropertyList2
    function GetValueAsVariant(PropertyID: Integer; out Value: OleVariant): HRESULT; stdcall;
    function SetValueAsVariant(PropertyID: Integer; const Value: OleVariant): HRESULT; stdcall;
  end;

  { TAIMPServiceConfig }

  TAIMPServiceConfig = class(TObject)
  private
    FService: IAIMPServiceConfig;
  public
    constructor Create;
    // Deleting
    procedure Delete(const AKeyPath: string);
    // Reading
    function ReadBool(const AKeyPath: string; const ADefault: Boolean = False): Boolean;
    function ReadFloat(const AKeyPath: string; const ADefault: Double = 0): Double;
    function ReadInt64(const AKeyPath: string; const ADefault: Int64 = 0): Int64;
    function ReadInteger(const AKeyPath: string; const ADefault: Integer = 0): Integer;
    function ReadString(const AKeyPath: string; const ADefault: string = ''): string;
    // Writing
    procedure WriteBool(const AKeyPath: string; const AValue: Boolean);
    procedure WriteFloat(const AKeyPath: string; const AValue: Double);
    procedure WriteInt64(const AKeyPath: string; const AValue: Int64);
    procedure WriteInteger(const AKeyPath: string; const AValue: Integer);
    procedure WriteString(const AKeyPath: string; const AValue: string);
    // Properties
    property Service: IAIMPServiceConfig read FService;
  end;

  { TAIMPStreamWrapper }

  TAIMPStreamWrapper = class(TStream)
  private
    FSource: IAIMPStream;
  protected
    function GetSize: Int64; override;
    procedure SetSize(const NewSize: Int64); override;
  public
    constructor Create(ASource: IAIMPStream); virtual;
    function Read(var Buffer; Count: Longint): Longint; override;
    function Seek(const Offset: Int64; Origin: TSeekOrigin): Int64; override;
    function Write(const Buffer; Count: Longint): Longint; override;
  end;

  { TAIMPStreamAdapter }

  TAIMPStreamAdapter = class(TInterfacedObjectEx, IAIMPStream)
  private
    FOwnership: TStreamOwnership;
  protected
    FSource: TStream;
    FSourceIsReadOnly: Boolean;

    function TestSource(ASource: TStream): Boolean; virtual;
    // IAIMPStream
    function GetSize: Int64; stdcall;
    function SetSize(const Value: Int64): HRESULT; stdcall;
    function GetPosition: Int64; stdcall;
    function Seek(const Offset: Int64; Mode: Integer): HRESULT; stdcall;
    function Read(Buffer: PByte; Count: LongWord): Integer; virtual; stdcall;
    function Write(Buffer: PByte; Count: LongWord; Written: PLongWord = nil): HRESULT; stdcall;
  public
    constructor Create(ASource: TStream; AOwnership: TStreamOwnership = soOwned);
    constructor CreateFromResource(const AName: string; AType: PChar);
    destructor Destroy; override;
  end;

  { TAIMPMemoryStreamAdapter }

  TAIMPMemoryStreamAdapter = class(TAIMPStreamAdapter, IAIMPMemoryStream)
  protected
    function TestSource(ASource: TStream): Boolean; override;
    // IAIMPMemoryStream
    function GetData: PByte; stdcall;
  public
    constructor Create; overload;
  end;

  { TAIMPFileStreamAdapter }

  TAIMPFileStreamAdapter = class(TAIMPStreamAdapter, IAIMPFileStream)
  protected
    function CreateStream(const AFileName: string; AMode: Integer): TStream; virtual;
    function Read(Buffer: PByte; Count: DWORD): Integer; override;
    function TestSource(ASource: TStream): Boolean; override;
    // IAIMPFileStream
    function GetClipping(out Offset, Size: Int64): HRESULT; virtual; stdcall;
    function GetFileName(out S: IAIMPString): HRESULT; virtual; stdcall;
  public
    constructor Create(const AFileName: IAIMPString; AMode: Integer); overload;
    constructor Create(const AFileName: string; AMode: Integer); overload;
  end;

var
  FInternalConverter: function (const AString: IAIMPString): string = nil;

procedure CheckResult(R: HRESULT; const AMessage: string = '%d');

// Core
procedure CoreCreateObject(const IID: TGUID; out Obj);
function CoreGetProfilePath: string;
function CoreGetService(const IID: TGUID; out Obj): LongBool;
function CoreIntf: IAIMPCore;
function CoreProcessException(const E: Exception; ErrorInfo: IAIMPErrorInfo): HRESULT;

// Window Handles
function MainWindowGetHandle: HWND;

// Localizations
function LangGetName: string;
function LangLoadString(const KeyPath: string): string; overload;
function LangLoadString(const KeyPath: string; APartIndex: Integer): string; overload;
function LangLoadString(const KeyPath: string; APartIndex: Integer; out AValue: IAIMPString): HRESULT; overload;
function LangLoadString(const KeyPath: string; out AValue: IAIMPString): HRESULT; overload;
function LangLoadStringEx(const KeyPath: string): IAIMPString; overload;
function LangLoadStringEx(const KeyPath: string; APartIndex: Integer): IAIMPString; overload;
function LangLoadStringEx(const KeyPath: string; const Args: array of const): IAIMPString; overload;

// Strings
function IAIMPStringToString(const S: IAIMPString): string; inline;
function MakeString(const S: string): IAIMPString;
function StrCompare(const S1: IAIMPString; const S2: IAIMPString; IgnoreCase: Boolean = False): Integer; overload;
function StrCompare(const S1: IAIMPString; const S2: string; IgnoreCase: Boolean = False): Integer; overload;

function PropListGetBool(const List: IAIMPPropertyList; ID: Integer; const ADefault: LongBool = False): LongBool;
function PropListGetFloat(const List: IAIMPPropertyList; ID: Integer; const ADefault: Double = 0): Double;
function PropListGetInt32(const List: IAIMPPropertyList; ID: Integer; const ADefault: Integer = 0): Integer;
function PropListGetInt64(const List: IAIMPPropertyList; ID: Integer; const ADefault: Int64 = 0): Int64;
function PropListGetObj(const List: IAIMPPropertyList; ID: Integer): IUnknown;
function PropListGetStr(const List: IAIMPPropertyList; ID: Integer): string; overload;
function PropListGetStr(const List: IAIMPPropertyList; ID: Integer; out S: IAIMPString): LongBool; overload;
function PropListGetStr(const List: IAIMPPropertyList; ID: Integer; out S: string): LongBool; overload;
procedure PropListSetBool(const List: IAIMPPropertyList; ID: Integer; const Value: LongBool);
procedure PropListSetFloat(const List: IAIMPPropertyList; ID: Integer; const Value: Double);
procedure PropListSetInt32(const List: IAIMPPropertyList; ID: Integer; const Value: Integer);
procedure PropListSetInt64(const List: IAIMPPropertyList; ID: Integer; const Value: Int64);
procedure PropListSetObj(const List: IAIMPPropertyList; ID: Integer; S: IUnknown);
procedure PropListSetStr(const List: IAIMPPropertyList; ID: Integer; const S: string);

// Message Dispatcher
function ApplicationIsLoaded: LongBool;
function MessageDispatcherSend(Message: Integer; Param1: Integer = 0; Param2: Pointer = nil): HRESULT;
function MessageDispatcherGetPropValue(PropertyID: Integer; ValueBuffer: Pointer): HRESULT;
function MessageDispatcherSetPropValue(PropertyID: Integer; ValueBuffer: Pointer): HRESULT;
implementation

uses
  apiMessages;

const
  sErrorCannotCreateObject = 'Cannot create object (%d)';
  sErrorNoCore = 'Plugin was not initialized';

var
  FCore: IAIMPCore = nil;

procedure CheckResult(R: HRESULT; const AMessage: string = '%d');
begin
  if Failed(R) then
    raise Exception.CreateFmt(AMessage, [R]);
end;

//----------------------------------------------------------------------------------------------------------------------
// Core
//----------------------------------------------------------------------------------------------------------------------

procedure CoreCreateObject(const IID: TGUID; out Obj);
begin
  CheckResult(CoreIntf.CreateObject(IID, Obj), sErrorCannotCreateObject);
end;

function CoreCreateObjectEx(const IID: TGUID; out Obj): HRESULT;
begin
  Result := CoreIntf.CreateObject(IID, Obj);
end;

function CoreGetProfilePath: string;
var
  S: IAIMPString;
begin
  if Failed(CoreIntf.GetPath(AIMP_CORE_PATH_PROFILE, S)) then
    raise Exception.Create('Profile path is not found');
  Result := IAIMPStringToString(S);
end;

function CoreGetService(const IID: TGUID; out Obj): LongBool;
begin
  Result := (FCore <> nil) and Succeeded(FCore.QueryInterface(IID, Obj));
end;

function CoreIntf: IAIMPCore;
begin
  Result := FCore;
  if Result = nil then
    raise Exception.Create(sErrorNoCore);
end;

function CoreProcessException(const E: Exception; ErrorInfo: IAIMPErrorInfo): HRESULT;
begin
  if E is EAbort then
    Exit(E_FAIL);
  if ErrorInfo <> nil then
    ErrorInfo.SetInfo(-1, MakeString(E.Message), nil);
  Result := E_UNEXPECTED;
end;

//----------------------------------------------------------------------------------------------------------------------
// Window Handles
//----------------------------------------------------------------------------------------------------------------------

function MainWindowGetHandle: HWND;
begin
  if Failed(MessageDispatcherGetPropValue(AIMP_MSG_PROPERTY_HWND, @Result)) then
    Result := 0;
end;

//----------------------------------------------------------------------------------------------------------------------
// Localization
//----------------------------------------------------------------------------------------------------------------------

function LangGetName: string;
var
  AService: IAIMPServiceMUI;
  AValue: IAIMPString;
begin
  if CoreGetService(IAIMPServiceMUI, AService) and Succeeded(AService.GetName(AValue)) then
    Result := IAIMPStringToString(AValue)
  else
    Result := '';
end;

function LangLoadString(const KeyPath: string): string;
var
  AValue: IAIMPString;
begin
  if Succeeded(LangLoadString(KeyPath, AValue)) then
    Result := IAIMPStringToString(AValue)
  else
    Result := '';
end;

function LangLoadString(const KeyPath: string; out AValue: IAIMPString): HRESULT;
var
  AService: IAIMPServiceMUI;
begin
  if CoreGetService(IAIMPServiceMUI, AService) then
    Result := AService.GetValue(MakeString(KeyPath), AValue)
  else
    Result := E_NOINTERFACE;
end;

function LangLoadStringEx(const KeyPath: string): IAIMPString;
begin
  if Failed(LangLoadString(KeyPath, Result)) then
    Result := MakeString('');
end;

function LangLoadStringEx(const KeyPath: string; APartIndex: Integer): IAIMPString;
begin
  if Failed(LangLoadString(KeyPath, APartIndex, Result)) then
    Result := MakeString('');
end;

function LangLoadStringEx(const KeyPath: string; const Args: array of const): IAIMPString;
begin
  Result := MakeString(Format(LangLoadString(KeyPath), Args));
end;

function LangLoadString(const KeyPath: string; APartIndex: Integer): string;
var
  AValue: IAIMPString;
begin
  if Succeeded(LangLoadString(KeyPath, APartIndex, AValue)) then
    Result := IAIMPStringToString(AValue)
  else
    Result := '';
end;

function LangLoadString(const KeyPath: string; APartIndex: Integer; out AValue: IAIMPString): HRESULT;
var
  AService: IAIMPServiceMUI;
begin
  if CoreGetService(IAIMPServiceMUI, AService) then
    Result := AService.GetValuePart(MakeString(KeyPath), APartIndex, AValue)
  else
    Result := E_NOINTERFACE;
end;

//----------------------------------------------------------------------------------------------------------------------
// Strings
//----------------------------------------------------------------------------------------------------------------------

function IAIMPStringToString(const S: IAIMPString): string;
begin
  if Assigned(FInternalConverter) then
    Result := FInternalConverter(S)
  else
    if S <> nil then
      SetString(Result, S.GetData, S.GetLength)
    else
      Result := '';
end;

function MakeString(const S: string): IAIMPString;
begin
  CoreCreateObject(IID_IAIMPString, Result);
  Result.SetData(PChar(S), Length(S));
end;

function StrCompare(const S1: IAIMPString; const S2: IAIMPString; IgnoreCase: Boolean): Integer;
begin
  if (S1 <> nil) and (S2 <> nil) then
    S1.Compare(S2, Result, IgnoreCase)
  else if S1 <> nil then
    Result := 1
  else if S2 <> nil then
    Result := -1
  else
    Result := 0
end;

function StrCompare(const S1: IAIMPString; const S2: string; IgnoreCase: Boolean): Integer;
begin
  if S1 <> nil then
    S1.Compare2(PChar(S2), Length(S2), Result, IgnoreCase)
  else if S2 <> '' then
    Result := -1
  else
    Result := 0;
end;

//----------------------------------------------------------------------------------------------------------------------
// PropList
//----------------------------------------------------------------------------------------------------------------------

function EnsurePropListNotNil(const List: IAIMPPropertyList): IAIMPPropertyList; inline;
begin
  if List = nil then
    raise Exception.Create('PropList is nil');
  Result := List;
end;

function PropListGetBool(const List: IAIMPPropertyList; ID: Integer; const ADefault: LongBool): LongBool;
var
  AValue: Integer;
begin
  if (List <> nil) and Succeeded(List.GetValueAsInt32(ID, AValue)) then
    Result := AValue <> 0
  else
    Result := ADefault;
end;

function PropListGetFloat(const List: IAIMPPropertyList; ID: Integer; const ADefault: Double = 0): Double;
begin
  if (List = nil) or Failed(List.GetValueAsFloat(ID, Result)) then
    Result := ADefault;
end;

function PropListGetInt64(const List: IAIMPPropertyList; ID: Integer; const ADefault: Int64 = 0): Int64;
begin
  if (List = nil) or Failed(List.GetValueAsInt64(ID, Result)) then
    Result := ADefault;
end;

function PropListGetInt32(const List: IAIMPPropertyList; ID: Integer; const ADefault: Integer = 0): Integer;
begin
  if (List = nil) or Failed(List.GetValueAsInt32(ID, Result)) then
    Result := ADefault;
end;

function PropListGetObj(const List: IAIMPPropertyList; ID: Integer): IUnknown;
begin
  if (List = nil) or Failed(List.GetValueAsObject(ID, IUnknown, Result)) then
    Result := nil;
end;

function PropListGetStr(const List: IAIMPPropertyList; ID: Integer): string;
begin
  if not PropListGetStr(List, ID, Result) then
    Result := '';
end;

function PropListGetStr(const List: IAIMPPropertyList; ID: Integer; out S: IAIMPString): LongBool;
begin
  Result := (List <> nil) and Succeeded(List.GetValueAsObject(ID, IAIMPString, S));
end;

function PropListGetStr(const List: IAIMPPropertyList; ID: Integer; out S: string): LongBool;
var
  AStrIntf: IAIMPString;
begin
  Result := PropListGetStr(List, ID, AStrIntf);
  if Result then
    S := IAIMPStringToString(AStrIntf);
end;

procedure PropListSetBool(const List: IAIMPPropertyList; ID: Integer; const Value: LongBool);
begin
  PropListSetInt32(List, ID, IfThen(Value, 1, 0));
end;

procedure PropListSetFloat(const List: IAIMPPropertyList; ID: Integer; const Value: Double);
begin
  CheckResult(EnsurePropListNotNil(List).SetValueAsFloat(ID, Value));
end;

procedure PropListSetInt32(const List: IAIMPPropertyList; ID: Integer; const Value: Integer);
begin
  CheckResult(EnsurePropListNotNil(List).SetValueAsInt32(ID, Value));
end;

procedure PropListSetInt64(const List: IAIMPPropertyList; ID: Integer; const Value: Int64);
begin
  CheckResult(EnsurePropListNotNil(List).SetValueAsInt64(ID, Value));
end;

procedure PropListSetObj(const List: IAIMPPropertyList; ID: Integer; S: IUnknown);
begin
  CheckResult(EnsurePropListNotNil(List).SetValueAsObject(ID, S));
end;

procedure PropListSetStr(const List: IAIMPPropertyList; ID: Integer; const S: string);
begin
  CheckResult(EnsurePropListNotNil(List).SetValueAsObject(ID, MakeString(S)));
end;

//----------------------------------------------------------------------------------------------------------------------
// Message Dispatcher
//----------------------------------------------------------------------------------------------------------------------

function ApplicationIsLoaded: LongBool;
begin
  if Failed(MessageDispatcherGetPropValue(AIMP_MSG_PROPERTY_LOADED, @Result)) then
    Result := True; //todo
end;

function MessageDispatcherSend(Message: Integer; Param1: Integer = 0; Param2: Pointer = nil): HRESULT;
var
  AService: IAIMPServiceMessageDispatcher;
begin
  if CoreGetService(IID_IAIMPServiceMessageDispatcher, AService) then
    Result := AService.Send(Message, Param1, Param2)
  else
    Result := E_NOINTERFACE;
end;

function MessageDispatcherGetPropValue(PropertyID: Integer; ValueBuffer: Pointer): HRESULT;
begin
  Result := MessageDispatcherSend(PropertyID, AIMP_MSG_PROPVALUE_GET, ValueBuffer);
end;

function MessageDispatcherSetPropValue(PropertyID: Integer; ValueBuffer: Pointer): HRESULT;
begin
  Result := MessageDispatcherSend(PropertyID, AIMP_MSG_PROPVALUE_SET, ValueBuffer);
end;

{ TInterfacedObjectEx }

procedure TInterfacedObjectEx.AfterConstruction;
begin
  // Release the constructor's implicit refcount
  InterlockedDecrement(FRefCount);
end;

procedure TInterfacedObjectEx.BeforeDestruction;
begin
  if RefCount <> 0 then
    raise Exception.Create('Invalid Pointer');
  inherited BeforeDestruction;
end;

// Set an implicit refcount so that refcounting
// during construction won't destroy the object.
class function TInterfacedObjectEx.NewInstance: TObject;
begin
  Result := inherited NewInstance;
  TInterfacedObjectEx(Result).FRefCount := 1;
end;

function TInterfacedObjectEx.QueryInterface;
begin
  if GetInterface(IID, Obj) then
    Result := 0
  else
    Result := E_NOINTERFACE;
end;

function TInterfacedObjectEx._AddRef: Integer;
begin
  Result := InterlockedIncrement(FRefCount);
end;

function TInterfacedObjectEx._Release: Integer;
begin
  Result := InterlockedDecrement(FRefCount);
  if Result = 0 then
    Free;
end;

{ TAIMPAPIWrappers }

class procedure TAIMPAPIWrappers.Finalize;
begin
  FCore := nil;
end;

class procedure TAIMPAPIWrappers.Initialize(Core: IAIMPCore);
begin
  FCore := Core;
end;

{ TAIMPExtensionFileFormat }

constructor TAIMPExtensionFileFormat.Create(const ADescription, AExtList: string;
  AFlags: Cardinal = AIMP_SERVICE_FILEFORMATS_CATEGORY_AUDIO);
begin
  inherited Create;
  FFlags := AFlags;
  FExtList := AExtList;
  FDescription := ADescription;
end;

function TAIMPExtensionFileFormat.GetDescription(out S: IAIMPString): HRESULT;
begin
  try
    S := MakeString(FDescription);
    Result := S_OK;
  except
    Result := E_UNEXPECTED;
  end;
end;

function TAIMPExtensionFileFormat.GetExtList(out S: IAIMPString): HRESULT;
begin
  try
    S := MakeString(FExtList);
    Result := S_OK;
  except
    Result := E_UNEXPECTED;
  end;
end;

function TAIMPExtensionFileFormat.GetFlags(out Flags: Cardinal): HRESULT;
begin
  try
    Flags := FFlags;
    Result := S_OK;
  except
    Result := E_UNEXPECTED;
  end;
end;

{ TAIMPPropertyList }

function TAIMPPropertyList.CheckAccess(out AResult: HRESULT): Boolean;
begin
  Result := True;
  AResult := S_OK;
end;

procedure TAIMPPropertyList.DoBeginUpdate;
begin
  // do nothing
end;

procedure TAIMPPropertyList.DoEndUpdate;
begin
  // do nothing
end;

procedure TAIMPPropertyList.DoGetValueAsFloat(PropertyID: Integer; out Value: Double; var Result: HRESULT);
var
  AVarValue: OleVariant;
begin
  DoGetValueAsVariant(PropertyID, AVarValue, Result);
  if Succeeded(Result) then
    Value := AVarValue;
end;

procedure TAIMPPropertyList.DoGetValueAsInt32(PropertyID: Integer; out Value: Integer; var Result: HRESULT);
var
  AVarValue: OleVariant;
begin
  DoGetValueAsVariant(PropertyID, AVarValue, Result);
  if Succeeded(Result) then
    Value := AVarValue;
end;

procedure TAIMPPropertyList.DoGetValueAsInt64(PropertyID: Integer; out Value: Int64; var Result: HRESULT);
var
  AVarValue: OleVariant;
begin
  DoGetValueAsVariant(PropertyID, AVarValue, Result);
  if Succeeded(Result) then
    Value := AVarValue;
end;

function TAIMPPropertyList.DoGetValueAsObject(PropertyID: Integer): IUnknown;
var
  AValue: OleVariant;
begin
  if PropertyID = 0 then
    Result := FCustomObject
  else
    if Succeeded(GetValueAsVariant(PropertyID, AValue)) then
      Result := MakeString(AValue)
    else
      Result := nil;
end;

procedure TAIMPPropertyList.DoGetValueAsVariant(PropertyID: Integer; out Value: OleVariant; var Result: HRESULT);
begin
  Result := E_NOTIMPL;
end;

function TAIMPPropertyList.DoReset: HRESULT;
begin
  Result := E_NOTIMPL;
end;

procedure TAIMPPropertyList.DoSetValueAsFloat(PropertyID: Integer; const Value: Double; var Result: HRESULT);
begin
  DoSetValueAsVariant(PropertyID, Value, Result);
end;

procedure TAIMPPropertyList.DoSetValueAsInt32(PropertyID: Integer; const Value: Integer; var Result: HRESULT);
begin
  DoSetValueAsVariant(PropertyID, Value, Result);
end;

procedure TAIMPPropertyList.DoSetValueAsInt64(PropertyID: Integer; const Value: Int64; var Result: HRESULT);
begin
  DoSetValueAsVariant(PropertyID, Value, Result);
end;

procedure TAIMPPropertyList.DoSetValueAsObject(PropertyID: Integer; const Value: IInterface; var Result: HRESULT);
var
  AStrIntf: IAIMPString;
begin
  if PropertyID = 0 then
    FCustomObject := Value
  else
    if Supports(Value, IAIMPString, AStrIntf) then
      DoSetValueAsVariant(PropertyID, IAIMPStringToString(AStrIntf), Result)
    else
      Result := E_NOTIMPL;
end;

procedure TAIMPPropertyList.DoSetValueAsVariant(PropertyID: Integer; const Value: OleVariant; var Result: HRESULT);
begin
  Result := E_NOTIMPL;
end;

procedure TAIMPPropertyList.BeginUpdate;
var
  X: HRESULT;
begin
  if CheckAccess(X) then
    DoBeginUpdate;
end;

procedure TAIMPPropertyList.EndUpdate;
var
  X: HRESULT;
begin
  if CheckAccess(X) then
    DoEndUpdate;
end;

function TAIMPPropertyList.GetValueAsFloat(PropertyID: Integer; out Value: Double): HRESULT;
begin
  if CheckAccess(Result) then
  try
    Result := S_OK;
    DoGetValueAsFloat(PropertyID, Value, Result);
  except
    Result := E_UNEXPECTED;
  end;
end;

function TAIMPPropertyList.GetValueAsInt32(PropertyID: Integer; out Value: Integer): HRESULT;
begin
  if CheckAccess(Result) then
  try
    Result := S_OK;
    DoGetValueAsInt32(PropertyID, Value, Result);
  except
    Result := E_UNEXPECTED;
  end;
end;

function TAIMPPropertyList.GetValueAsInt64(PropertyID: Integer; out Value: Int64): HRESULT;
begin
  if CheckAccess(Result) then
  try
    Result := S_OK;
    DoGetValueAsInt64(PropertyID, Value, Result);
  except
    Result := E_UNEXPECTED;
  end;
end;

function TAIMPPropertyList.GetValueAsObject(PropertyID: Integer; const IID: TGUID; out Value): HRESULT;
var
  AIntf: IUnknown;
begin
  if CheckAccess(Result) then
  try
    AIntf := DoGetValueAsObject(PropertyID);
    if AIntf <> nil then
      Result := AIntf.QueryInterface(IID, Value)
    else
      Result := E_INVALIDARG;
  except
    Result := E_UNEXPECTED;
  end;
end;

function TAIMPPropertyList.GetValueAsVariant(PropertyID: Integer; out Value: OleVariant): HRESULT;
begin
  if CheckAccess(Result) then
  try
    Result := S_OK;
    DoGetValueAsVariant(PropertyID, Value, Result);
  except
    Result := E_UNEXPECTED;
  end;
end;

function TAIMPPropertyList.SetValueAsFloat(PropertyID: Integer; const Value: Double): HRESULT;
begin
  if CheckAccess(Result) then
  try
    Result := S_OK;
    DoSetValueAsFloat(PropertyID, Value, Result);
  except
    Result := E_UNEXPECTED;
  end;
end;

function TAIMPPropertyList.SetValueAsInt32(PropertyID, Value: Integer): HRESULT;
begin
  if CheckAccess(Result) then
  try
    Result := S_OK;
    DoSetValueAsInt32(PropertyID, Value, Result);
  except
    Result := E_UNEXPECTED;
  end;
end;

function TAIMPPropertyList.SetValueAsInt64(PropertyID: Integer; const Value: Int64): HRESULT;
begin
  if CheckAccess(Result) then
  try
    Result := S_OK;
    DoSetValueAsInt64(PropertyID, Value, Result);
  except
    Result := E_UNEXPECTED;
  end;
end;

function TAIMPPropertyList.SetValueAsObject(PropertyID: Integer; Value: IInterface): HRESULT;
begin
  if CheckAccess(Result) then
  try
    Result := S_OK;
    DoSetValueAsObject(PropertyID, Value, Result);
  except
    Result := E_UNEXPECTED;
  end;
end;

function TAIMPPropertyList.SetValueAsVariant(PropertyID: Integer; const Value: OleVariant): HRESULT;
begin
  if CheckAccess(Result) then
  try
    Result := S_OK;
    DoSetValueAsVariant(PropertyID, Value, Result);
  except
    Result := E_UNEXPECTED;
  end;
end;

function TAIMPPropertyList.Reset: HRESULT;
begin
  if CheckAccess(Result) then
  try
    Result := DoReset;
  except
    Result := E_UNEXPECTED;
  end;
end;

{ TAIMPServiceConfig }

constructor TAIMPServiceConfig.Create;
begin
  inherited Create;
  if not CoreGetService(IAIMPServiceConfig, FService) then
    raise Exception.Create('The IAIMPServiceConfig is not supported');
end;

procedure TAIMPServiceConfig.Delete(const AKeyPath: string);
begin
  FService.Delete(MakeString(AKeyPath));
end;

function TAIMPServiceConfig.ReadBool(const AKeyPath: string; const ADefault: Boolean): Boolean;
begin
  Result := ReadInteger(AKeyPath, Ord(ADefault)) <> 0;
end;

function TAIMPServiceConfig.ReadFloat(const AKeyPath: string; const ADefault: Double = 0): Double;
begin
  if Failed(FService.GetValueAsFloat(MakeString(AKeyPath), Result)) then
    Result := ADefault;
end;

function TAIMPServiceConfig.ReadInt64(const AKeyPath: string; const ADefault: Int64 = 0): Int64;
begin
  if Failed(FService.GetValueAsInt64(MakeString(AKeyPath), Result)) then
    Result := ADefault;
end;

function TAIMPServiceConfig.ReadInteger(const AKeyPath: string; const ADefault: Integer = 0): Integer;
begin
  if Failed(FService.GetValueAsInt32(MakeString(AKeyPath), Result)) then
    Result := ADefault;
end;

function TAIMPServiceConfig.ReadString(const AKeyPath: string; const ADefault: string = ''): string;
var
  AValue: IAIMPString;
begin
  if Succeeded(FService.GetValueAsString(MakeString(AKeyPath), AValue)) then
    Result := IAIMPStringToString(AValue)
  else
    Result := ADefault;
end;

procedure TAIMPServiceConfig.WriteBool(const AKeyPath: string; const AValue: Boolean);
begin
  WriteInteger(AKeyPath, Ord(AValue));
end;

procedure TAIMPServiceConfig.WriteFloat(const AKeyPath: string; const AValue: Double);
begin
  FService.SetValueAsFloat(MakeString(AKeyPath), AValue);
end;

procedure TAIMPServiceConfig.WriteInt64(const AKeyPath: string; const AValue: Int64);
begin
  FService.SetValueAsInt64(MakeString(AKeyPath), AValue);
end;

procedure TAIMPServiceConfig.WriteInteger(const AKeyPath: string; const AValue: Integer);
begin
  FService.SetValueAsInt32(MakeString(AKeyPath), AValue);
end;

procedure TAIMPServiceConfig.WriteString(const AKeyPath: string; const AValue: string);
begin
  FService.SetValueAsString(MakeString(AKeyPath), MakeString(AValue));
end;

{ TAIMPStreamWrapper }

constructor TAIMPStreamWrapper.Create(ASource: IAIMPStream);
begin
  inherited Create;
  FSource := ASource;
end;

function TAIMPStreamWrapper.Read(var Buffer; Count: Integer): Longint;
begin
  Result := FSource.Read(@Buffer, Count);
end;

function TAIMPStreamWrapper.Seek(const Offset: Int64; Origin: TSeekOrigin): Int64;
begin
  case Origin of
    soBeginning:
      FSource.Seek(Offset, AIMP_STREAM_SEEKMODE_FROM_BEGINNING);
    soCurrent:
      FSource.Seek(Offset, AIMP_STREAM_SEEKMODE_FROM_CURRENT);
    soEnd:
      FSource.Seek(Offset, AIMP_STREAM_SEEKMODE_FROM_END);
  end;
  Result := FSource.GetPosition;
end;

function TAIMPStreamWrapper.Write(const Buffer; Count: Integer): Longint;
var
  AWritten: DWORD;
begin
  if Succeeded(FSource.Write(@Buffer, Count, @AWritten)) then
    Result := AWritten
  else
    Result := 0
end;

function TAIMPStreamWrapper.GetSize: Int64;
begin
  Result := FSource.GetSize;
end;

procedure TAIMPStreamWrapper.SetSize(const NewSize: Int64);
begin
  if Failed(FSource.SetSize(NewSize)) then
    Abort;
end;

{ TAIMPStreamAdapter }

constructor TAIMPStreamAdapter.Create(ASource: TStream; AOwnership: TStreamOwnership = soOwned);
begin
  inherited Create;
  if not TestSource(ASource) then
    raise EStreamError.Create('Unsupported stream class');
  FSource := ASource;
  FOwnership := AOwnership;
end;

constructor TAIMPStreamAdapter.CreateFromResource(const AName: string; AType: PChar);
begin
  Create(TResourceStream.Create(HINSTANCE, AName, AType));
end;

destructor TAIMPStreamAdapter.Destroy;
begin
  if FOwnership = soOwned then
    FreeAndNil(FSource);
  inherited Destroy;
end;

function TAIMPStreamAdapter.TestSource(ASource: TStream): Boolean;
begin
  Result := True;
end;

function TAIMPStreamAdapter.GetPosition: Int64;
begin
  Result := FSource.Position;
end;

function TAIMPStreamAdapter.GetSize: Int64;
begin
  Result := FSource.Size;
end;

function TAIMPStreamAdapter.Read(Buffer: PByte; Count: DWORD): Integer;
begin
  try
    Result := FSource.Read(Buffer^, Count);
  except
    Result := -1;
  end;
end;

function TAIMPStreamAdapter.Seek(const Offset: Int64; Mode: Integer): HRESULT;
var
  ASeekOrigin: TSeekOrigin;
begin
  case Mode of
    AIMP_STREAM_SEEKMODE_FROM_BEGINNING:
      ASeekOrigin := soBeginning;
    AIMP_STREAM_SEEKMODE_FROM_CURRENT:
      ASeekOrigin := soCurrent;
    AIMP_STREAM_SEEKMODE_FROM_END:
      ASeekOrigin := soEnd;
  else
    Exit(E_INVALIDARG);
  end;

  try
    if FSource.Seek(Offset, ASeekOrigin) = Offset then
      Result := S_OK
    else
      Result := E_FAIL;
  except
    Result := E_UNEXPECTED;
  end;
end;

function TAIMPStreamAdapter.SetSize(const Value: Int64): HRESULT;
begin
  if FSourceIsReadOnly then
    Result := E_NOTIMPL
  else
    try
      FSource.Size := Value;
      Result := S_OK;
    except
      Result := E_UNEXPECTED;
    end;
end;

function TAIMPStreamAdapter.Write(Buffer: PByte; Count: LongWord; Written: PLongWord = nil): HRESULT;
begin
  if FSourceIsReadOnly then
    Result := E_NOTIMPL
  else
    try
      Count := FSource.Write(Buffer^, Count);
      if Written <> nil then
        Written^ := Count;
      Result := S_OK;
    except
      Result := E_UNEXPECTED;
    end;
end;

{ TAIMPMemoryStreamAdapter }

constructor TAIMPMemoryStreamAdapter.Create;
begin
  inherited Create(TMemoryStream.Create);
end;

function TAIMPMemoryStreamAdapter.TestSource(ASource: TStream): Boolean;
begin
  Result := ASource is TMemoryStream;
end;

function TAIMPMemoryStreamAdapter.GetData: PByte;
begin
  Result := TMemoryStream(FSource).Memory;
end;

{ TAIMPFileStreamAdapter }

constructor TAIMPFileStreamAdapter.Create(const AFileName: string; AMode: Integer);
begin
  Create(CreateStream(AFileName, AMode));
end;

constructor TAIMPFileStreamAdapter.Create(const AFileName: IAIMPString; AMode: Integer);
begin
  Create(IAIMPStringToString(AFileName), AMode);
end;

function TAIMPFileStreamAdapter.TestSource(ASource: TStream): Boolean;
begin
  Result := ASource is TFileStream;
end;

function TAIMPFileStreamAdapter.GetClipping(out Offset, Size: Int64): HRESULT;
begin
  Result := E_FAIL;
end;

function TAIMPFileStreamAdapter.GetFileName(out S: IAIMPString): HRESULT;
begin
  try
    S := MakeString(TFileStream(FSource).FileName);
    Result := S_OK;
  except
    Result := E_UNEXPECTED;
  end;
end;

function TAIMPFileStreamAdapter.Read(Buffer: PByte; Count: DWORD): Integer;
begin
  try
    Result := FSource.Read(Buffer^, Count);
    if (Result = 0) and (Count > 0){$IFDEF MSWINDOWS}and (GetLastError <> ERROR_SUCCESS){$ENDIF} then
    begin
      if FSource.Position <> FSource.Size then
        Result := -1;
        //RaiseLastOSError;
    end;
  except
    Result := -1;
  end;
end;

function TAIMPFileStreamAdapter.CreateStream(const AFileName: string; AMode: Integer): TStream;
begin
  Result := TFileStream.Create(AFileName, AMode);
end;

end.
