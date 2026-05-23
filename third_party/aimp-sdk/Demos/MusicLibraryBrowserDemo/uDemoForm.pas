unit uDemoForm;

interface

{$R uDemoForm.res}

uses
  Windows, apiGUI, apiObjects, apiWrappersGUI, uDataProvider;

const
  NullRect: TRect = (Left: 0; Top: 0; Right: 0; Bottom: 0);

type

  TAIMPUITreeListNodeValueEvent = procedure (Sender: IAIMPUITreeList; NodeValue: IAIMPString) of object;

  { TAIMPUITreeListNodeSelectedEventAdapter }

  TAIMPUITreeListNodeSelectedEventAdapter = class(TInterfacedObject,
    IAIMPUITreeListEvents)
  strict private
    FEvent: TAIMPUITreeListNodeValueEvent;
  public
    constructor Create(AEvent: TAIMPUITreeListNodeValueEvent);
    // IAIMPUITreeListEvents
    procedure OnColumnClick(Sender: IAIMPUITreeList; ColumnIndex: Integer); stdcall;
    procedure OnFocusedColumnChanged(Sender: IAIMPUITreeList); stdcall;
    procedure OnFocusedNodeChanged(Sender: IAIMPUITreeList); stdcall;
    procedure OnNodeChecked(Sender: IAIMPUITreeList; Node: IAIMPUITreeListNode); stdcall;
    procedure OnNodeDblClicked(Sender: IAIMPUITreeList; Node: IAIMPUITreeListNode); stdcall;
    procedure OnSelectionChanged(Sender: IAIMPUITreeList); stdcall;
    procedure OnSorted(Sender: IAIMPUITreeList); stdcall;
    procedure OnStructChanged(Sender: IAIMPUITreeList); stdcall;
  end;

  { TDemoForm }

  TDemoForm = class(TInterfacedObject,
    IAIMPUIPlacementEvents,
    IAIMPUIFormEvents)
  strict private
    // IAIMPUIPlacementEvents
    procedure OnBoundsChanged(Sender: IInterface); stdcall;
    // IAIMPUIFormEvents
    procedure OnActivated(Sender: IAIMPUIForm); stdcall;
    procedure OnDeactivated(Sender: IAIMPUIForm); stdcall;
    procedure OnCreated(Sender: IAIMPUIForm); stdcall;
    procedure OnDestroyed(Sender: IAIMPUIForm); stdcall;
    procedure OnCloseQuery(Sender: IAIMPUIForm; var CanClose: LongBool); stdcall;
    procedure OnLocalize(Sender: IAIMPUIForm); stdcall;
    procedure OnShortCut(Sender: IAIMPUIForm; Key, Modifiers: Word; var Handled: LongBool); stdcall;
    // TAIMPUITreeListNodeSelectedEventAdapter
    procedure OnSelectAlbum(Sender: IAIMPUITreeList; NodeValue: IAIMPString);
    procedure OnSelectArtist(Sender: IAIMPUITreeList; NodeValue: IAIMPString);
  protected
    FControlAlbumList: IAIMPUITreeList;
    FControlArtistList: IAIMPUITreeList;
    FControlTopPanel: IAIMPUIWinControl;
    FControlTrackList: IAIMPUITreeList;
    FDataProvider: TMLDataProvider;
    FForm: IAIMPUIForm;
    FService: IAIMPServiceUI;

    FSelectedAlbum: IAIMPString;
    FSelectedArtist: IAIMPString;

    procedure CreateControls;
    procedure FetchAlbums;
    procedure FetchArtists;
    procedure FetchTracks;
    procedure PopulateTreeList(ATreeList: IAIMPUITreeList; AData: THashSet<string>);
  public
    constructor Create(AService: IAIMPServiceUI; ADataProvider: TMLDataProvider);
    function ShowModal: Integer;
  end;

implementation

uses
  apiWrappers;

function CenterRect(const ABounds: TRect; AWidth, AHeight: Integer): TRect;
begin
  Result.Left := (ABounds.Left + ABounds.Right - AWidth) div 2;
  Result.Top := (ABounds.Top + ABounds.Bottom - AHeight) div 2;
  Result.Right := Result.Left + AWidth;
  Result.Bottom := Result.Top + AHeight;
end;

{ TAIMPUITreeListNodeSelectedEventAdapter }

constructor TAIMPUITreeListNodeSelectedEventAdapter.Create(AEvent: TAIMPUITreeListNodeValueEvent);
begin
  FEvent := AEvent;
end;

procedure TAIMPUITreeListNodeSelectedEventAdapter.OnFocusedColumnChanged(Sender: IAIMPUITreeList);
begin
  // do nothing
end;

procedure TAIMPUITreeListNodeSelectedEventAdapter.OnFocusedNodeChanged(Sender: IAIMPUITreeList);
var
  ANode: IAIMPUITreeListNode;
  AValue: IAIMPString;
begin
  if Succeeded(Sender.GetFocused(IAIMPUITreeListNode, ANode)) then
  begin
    if Succeeded(ANode.GetValue(0, AValue)) then
      FEvent(Sender, AValue);
  end;
end;

procedure TAIMPUITreeListNodeSelectedEventAdapter.OnColumnClick(Sender: IAIMPUITreeList; ColumnIndex: Integer);
begin
  // do nothing
end;

procedure TAIMPUITreeListNodeSelectedEventAdapter.OnNodeChecked(Sender: IAIMPUITreeList; Node: IAIMPUITreeListNode);
begin
  // do nothing
end;

procedure TAIMPUITreeListNodeSelectedEventAdapter.OnNodeDblClicked(Sender: IAIMPUITreeList; Node: IAIMPUITreeListNode);
begin
  // do nothing
end;

procedure TAIMPUITreeListNodeSelectedEventAdapter.OnSelectionChanged(Sender: IAIMPUITreeList);
begin
  // do nothing
end;

procedure TAIMPUITreeListNodeSelectedEventAdapter.OnSorted(Sender: IAIMPUITreeList);
begin
  // do nothing
end;

procedure TAIMPUITreeListNodeSelectedEventAdapter.OnStructChanged(Sender: IAIMPUITreeList);
begin
  // do nothing
end;

{ TDemoForm }

constructor TDemoForm.Create(AService: IAIMPServiceUI; ADataProvider: TMLDataProvider);
var
  ABounds: TRect;
begin
  FService := AService;
  FDataProvider := ADataProvider;

  CheckResult(AService.CreateForm(0, 0, MakeString('DemoForm'), Self, FForm));

  // Center the Form on screen
  SystemParametersInfo(SPI_GETWORKAREA, 0, ABounds, 0);
  CheckResult(FForm.SetPlacement(TAIMPUIControlPlacement.Create(CenterRect(ABounds, 1024, 600))));

  // Create children controls
  CreateControls;

  // Show the data
  FetchArtists;
end;

procedure TDemoForm.CreateControls;
var
  AColumn: IAIMPUITreeListColumn;
begin
  // Create a top panel
  CheckResult(FService.CreateControl(FForm, FForm, nil, nil, IID_IAIMPUIPanel, FControlTopPanel));
  CheckResult(FControlTopPanel.SetPlacement(TAIMPUIControlPlacement.Create(ualTop, 200)));
  CheckResult(FControlTopPanel.SetValueAsInt32(AIMPUI_PANEL_PROPID_BORDERS, 0));

  // Create an artist view
  CheckResult(FService.CreateControl(FForm, FControlTopPanel, nil,
    TAIMPUITreeListNodeSelectedEventAdapter.Create(OnSelectArtist), IID_IAIMPUITreeList, FControlArtistList));
  CheckResult(FControlArtistList.SetPlacement(TAIMPUIControlPlacement.Create(ualNone, NullRect)));
  CheckResult(FControlArtistList.AddColumn(IID_IAIMPUITreeListColumn, AColumn));
  PropListSetStr(AColumn, AIMPUI_TL_COLUMN_PROPID_CAPTION, 'Artists');

  // Create an album view
  CheckResult(FService.CreateControl(FForm, FControlTopPanel, nil,
    TAIMPUITreeListNodeSelectedEventAdapter.Create(OnSelectAlbum), IID_IAIMPUITreeList, FControlAlbumList));
  CheckResult(FControlAlbumList.SetPlacement(TAIMPUIControlPlacement.Create(ualClient, NullRect)));
  CheckResult(FControlAlbumList.AddColumn(IID_IAIMPUITreeListColumn, AColumn));
  PropListSetStr(AColumn, AIMPUI_TL_COLUMN_PROPID_CAPTION, 'Albums');

  // Create a tracks view
  CheckResult(FService.CreateControl(FForm, FForm, nil, nil, IID_IAIMPUITreeList, FControlTrackList));
  CheckResult(FControlTrackList.SetPlacement(TAIMPUIControlPlacement.Create(ualClient, NullRect)));
end;

procedure TDemoForm.FetchAlbums;
begin
  FDataProvider.FetchAlbums(FSelectedArtist,
    procedure (AStringSet: THashSet<string>)
    begin
      PopulateTreeList(FControlAlbumList, AStringSet);
    end);
end;

procedure TDemoForm.FetchArtists;
begin
  FDataProvider.FetchArtists(
    procedure (AStringSet: THashSet<string>)
    begin
      PopulateTreeList(FControlArtistList, AStringSet);
    end);
end;

procedure TDemoForm.FetchTracks;
begin
  FDataProvider.FetchTracks(FSelectedArtist, FSelectedAlbum,
    procedure (AStringSet: THashSet<string>)
    begin
      PopulateTreeList(FControlTrackList, AStringSet);
    end);
end;

procedure TDemoForm.PopulateTreeList(ATreeList: IAIMPUITreeList; AData: THashSet<string>);
var
  ARootNode: IAIMPUITreeListNode;
  ANode: IAIMPUITreeListNode;
  AValue: string;
begin
  ATreeList.BeginUpdate;
  try
    CheckResult(ATreeList.GetRootNode(IID_IAIMPUITreeListNode, ARootNode));
    CheckResult(ARootNode.ClearChildren);
    for AValue in AData do
    begin
      CheckResult(ARootNode.Add(ANode));
      CheckResult(ANode.SetValue(0, MakeString(AValue)));
    end;
  finally
    ATreeList.EndUpdate;
  end;
end;

function TDemoForm.ShowModal: Integer;
begin
  Result := FForm.ShowModal;
end;

// IAIMPUIPlacementEvents
procedure TDemoForm.OnBoundsChanged(Sender: IInterface);
var
  APlacement: TAIMPUIControlPlacement;
begin
  CheckResult(FControlTopPanel.GetPlacement(APlacement));
  CheckResult(FControlArtistList.SetPlacement(TAIMPUIControlPlacement.Create(ualLeft, APlacement.Bounds.Width div 2)));
end;

// IAIMPUIFormEvents
procedure TDemoForm.OnActivated(Sender: IAIMPUIForm);
begin
  // do nothing
end;

procedure TDemoForm.OnDeactivated(Sender: IAIMPUIForm); stdcall;
begin
  // do nothing
end;

procedure TDemoForm.OnCreated(Sender: IAIMPUIForm); stdcall;
begin
  // do nothing
end;

procedure TDemoForm.OnDestroyed(Sender: IAIMPUIForm); stdcall;
begin
  {$MESSAGE 'TODO - stop all requests in FDataProvider'}
  // Release all variables
  FControlTopPanel := nil;
  FControlArtistList := nil;
  FControlAlbumList := nil;
  FControlTrackList := nil;
  FForm := nil;
end;

procedure TDemoForm.OnCloseQuery(Sender: IAIMPUIForm; var CanClose: LongBool); stdcall;
begin
  // do nothing
end;

procedure TDemoForm.OnLocalize(Sender: IAIMPUIForm); stdcall;
begin
  // do nothing
end;

procedure TDemoForm.OnShortCut(Sender: IAIMPUIForm; Key, Modifiers: Word; var Handled: LongBool); stdcall;
begin
  // do nothing
end;

procedure TDemoForm.OnSelectAlbum(Sender: IAIMPUITreeList; NodeValue: IAIMPString);
begin
  FControlTrackList.Clear;
  FSelectedAlbum := NodeValue;
  FetchTracks;
end;

procedure TDemoForm.OnSelectArtist(Sender: IAIMPUITreeList; NodeValue: IAIMPString);
begin
  FControlAlbumList.Clear;
  FControlTrackList.Clear;
  FSelectedArtist := NodeValue;
  FSelectedAlbum := nil;
  FetchAlbums;
end;

end.

