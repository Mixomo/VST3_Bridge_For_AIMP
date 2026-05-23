unit aimp_visDemoMain;

interface

uses
  Windows,
  // System
  System.Classes,
  System.SysUtils,
  // VCL
  Vcl.Graphics,
  // SDK
  apiCore,
  apiObjects,
  apiPlugin,
  apiWrappers,
  apiVisuals,
  AIMPCustomPlugin;

type

  { TVisualPlugin }

  TVisualPlugin = class(TAIMPCustomPlugin)
  protected
    function InfoGet(Index: Integer): PWideChar; override;
    function InfoGetCategories: Cardinal; override;
    function Initialize(Core: IAIMPCore): HRESULT; override;
  end;

  { TVisualization }

  TVisualization = class(TInterfacedObject,
    IAIMPExtensionEmbeddedVisualization)
  strict private
    FCanvas: TCanvas;
    FWidth, FHeight: Integer;
  public
    // IAIMPExtensionEmbeddedVisualization
    function GetFlags: Integer; stdcall;
    function GetMaxDisplaySize(out Width, Height: Integer): HRESULT; stdcall;
    function GetName(out S: IAIMPString): HRESULT; stdcall;
    // Initialization / Finalization
    function Initialize(Width, Height: Integer): HRESULT; stdcall;
    procedure Finalize; stdcall;
    // Basic functionality
    procedure Click(X, Y: Integer; Button: Integer); stdcall;
    procedure Draw(DC: HDC; Data: PAIMPVisualData); stdcall;
    procedure Resize(NewWidth, NewHeight: Integer); stdcall;
  end;

implementation

{ TVisualPlugin }

function TVisualPlugin.InfoGet(Index: Integer): PWideChar;
begin
  case Index of
    AIMP_PLUGIN_INFO_NAME:
      Result := 'Embedded peak-based visualization demo';
    AIMP_PLUGIN_INFO_AUTHOR:
      Result := 'Artem Izmaylov';
  else
    Result := '';
  end;
end;

function TVisualPlugin.InfoGetCategories: Cardinal;
begin
  Result := AIMP_PLUGIN_CATEGORY_VISUALS;
end;

function TVisualPlugin.Initialize(Core: IAIMPCore): HRESULT;
begin
  Result := inherited;
  Core.RegisterExtension(IID_IAIMPServiceVisualizations, TVisualization.Create)
end;

{ TVisualization }

procedure TVisualization.Click(X, Y, Button: Integer);
begin
  // do nothing
end;

procedure TVisualization.Draw(DC: HDC; Data: PAIMPVisualData);
begin
  FCanvas.Handle := DC;
  try
    // fill the background
    FCanvas.Brush.Color := clBlack;
    FCanvas.FillRect(Rect(0, 0, FWidth, FHeight));
    // draw left peak
    FCanvas.Brush.Color := clLime;
    FCanvas.FillRect(Rect(0, 10, Round(FWidth * Data^.Peaks[0]), FHeight div 2 - 5));
    // draw right peak
    FCanvas.Brush.Color := clLime;
    FCanvas.FillRect(Rect(0, FHeight div 2 + 5, Round(FWidth * Data^.Peaks[0]), FHeight - 10));
  finally
    FCanvas.Handle := 0;
  end;
end;

procedure TVisualization.Finalize;
begin
  FreeAndNil(FCanvas);
end;

function TVisualization.GetFlags: Integer;
begin
  Result := 0; // this visualization uses peak data only
end;

function TVisualization.GetMaxDisplaySize(out Width, Height: Integer): HRESULT;
begin
  Result := E_FAIL; // Our plugin have no limitation
end;

function TVisualization.GetName(out S: IAIMPString): HRESULT;
begin
  S := MakeString('Simple Peak Visualization');
  Result := S_OK;
end;

function TVisualization.Initialize(Width, Height: Integer): HRESULT;
begin
  FCanvas := TCanvas.Create;
  Resize(Width, Height);
  Result := S_OK;
end;

procedure TVisualization.Resize(NewWidth, NewHeight: Integer);
begin
  FHeight := NewHeight;
  FWidth := NewWidth;
end;

end.
