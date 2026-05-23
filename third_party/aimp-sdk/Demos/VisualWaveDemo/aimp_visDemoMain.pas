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
    procedure DrawWave(const Wave: TAIMPVisualDataWaveform; const Area: TRect);
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
      Result := 'Embedded wave-based visualization demo';
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
    // draw waves
    FCanvas.Pen.Color := clWhite;
    DrawWave(Data^.Waveform[0], Rect(0, 0, FWidth, FHeight div 2));
    DrawWave(Data^.Waveform[1], Rect(0, FHeight div 2, FWidth, FHeight));
  finally
    FCanvas.Handle := 0;
  end;
end;

procedure TVisualization.DrawWave(const Wave: TAIMPVisualDataWaveform; const Area: TRect);
var
  LMiddle: Integer;
  LHeight: Integer;
  I: Integer;
begin
  LHeight := Area.Height div 2;
  LMiddle := (Area.Top + Area.Bottom) div 2;
  FCanvas.MoveTo(Area.Left, LMiddle);
  for I := 0 to AIMP_VISUAL_WAVEFORM_SIZE - 1 do
  begin
    FCanvas.LineTo(
      Area.Left + MulDiv(Area.Width, I + 1, AIMP_VISUAL_WAVEFORM_SIZE),
      LMiddle + Round(Wave[I] * LHeight));
  end;
end;

procedure TVisualization.Finalize;
begin
  FreeAndNil(FCanvas);
end;

function TVisualization.GetFlags: Integer;
begin
  Result := AIMP_VISUAL_FLAGS_RQD_DATA_WAVEFORM; // this visualization requires wave data
end;

function TVisualization.GetMaxDisplaySize(out Width, Height: Integer): HRESULT;
begin
  Result := E_FAIL; // Our plugin have no limitation
end;

function TVisualization.GetName(out S: IAIMPString): HRESULT;
begin
  S := MakeString('Simple Wave Visualization');
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
