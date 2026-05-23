////////////////////////////////////////////////////////////////////////////////
//
//  Project:   AIMP
//             Programming Interface
//
//  Target:    v5.40 build 2650
//
//  Purpose:   Visualization API
//
//  Author:    Artem Izmaylov
//             © 2006-2025
//             www.aimp.ru
//
//  FPC:       OK
//
unit apiVisuals;

{$I apiConfig.inc}

interface

uses
  apiObjects,
  apiTypes;

const
  SID_IAIMPExtensionEmbeddedVisualization = '{41494D50-4578-7445-6D62-645669730000}';
  IID_IAIMPExtensionEmbeddedVisualization: TGUID = SID_IAIMPExtensionEmbeddedVisualization;

  SID_IAIMPExtensionCustomVisualization = '{41494D50-4578-7443-7374-6D5669730000}';
  IID_IAIMPExtensionCustomVisualization: TGUID = SID_IAIMPExtensionCustomVisualization;

  SID_IAIMPServiceVisualizations = '{41494D50-5372-7656-6973-75616C000000}';
  IID_IAIMPServiceVisualizations: TGUID = SID_IAIMPServiceVisualizations;

  // Button ID for IAIMPExtensionEmbeddedVisualization.Click
  AIMP_VISUAL_CLICK_BUTTON_LEFT   = 0;
  AIMP_VISUAL_CLICK_BUTTON_MIDDLE = 1;

  // flags for IAIMPExtensionEmbeddedVisualization.GetFlags and IAIMPExtensionCustomVisualization.GetFlags
  AIMP_VISUAL_FLAGS_RQD_DATA_WAVEFORM             = 1;
  AIMP_VISUAL_FLAGS_RQD_DATA_SPECTRUM             = 2;
  AIMP_VISUAL_FLAGS_NOT_SUSPEND                   = 4;
  AIMP_VISUAL_FLAGS_RQD_DATA_LOGARITHMIC_SPECTRUM = 8; // for internal use

  AIMP_VISUAL_SPECTRUM_SIZE = 256;
  AIMP_VISUAL_WAVEFORM_SIZE = 512;

type
  TAIMPVisualDataLogarithmicSpectrum = array[0..AIMP_VISUAL_SPECTRUM_SIZE - 1] of Word;
  TAIMPVisualDataWaveform = array[0..AIMP_VISUAL_WAVEFORM_SIZE - 1] of Single;

  TAIMPVisualDataSpectrum = array[0..AIMP_VISUAL_SPECTRUM_SIZE - 1] of Single;
  (*
    0  ~ 20 Hz
    10 ~ 200 Hz
    20 ~ 400 Hz
    30 ~ 630 Hz
    40 ~ 870 Hz
    50 ~ 1.1 KHz
    60 ~ 1.4 KHz
    70 ~ 1.7 KHz
    80 ~ 2.1 KHz
    90 ~ 2.5 KHz
    100 ~ 3.0 KHz
    110 ~ 3.5 KHz
    120 ~ 4.0 KHz
    130 ~ 4.5 KHz
    140 ~ 5.2 KHz
    150 ~ 6.0 KHz
    160 ~ 6.7 KHz
    170 ~ 7.6 KHz
    180 ~ 8.5 KHz
    190 ~ 9.6 KHz
    200 ~ 11.0 KHz
    210 ~ 12.0 KHz
    220 ~ 13.5 KHz
    230 ~ 15.0 KHz
    240 ~ 16.8 KHz
    255 ~ 20.0 KHz
  *)

  PAIMPVisualData = ^TAIMPVisualData;
  TAIMPVisualData = packed record
    Peaks: array[0..1] of Single;
    Spectrum: array[0..2] of TAIMPVisualDataSpectrum;
    Waveform: array[0..1] of TAIMPVisualDataWaveform;
    Reserved: Integer;
    LogarithmicSpectrum: array[0..2] of TAIMPVisualDataLogarithmicSpectrum; // for internal use
  end;

  { IAIMPExtensionCustomVisualization }

  IAIMPExtensionCustomVisualization = interface(IUnknown)
  [SID_IAIMPExtensionCustomVisualization]
    // Common information
    function GetFlags: Integer; stdcall;
    // Basic functionality
    procedure Draw(Data: PAIMPVisualData); stdcall;
  end;

  { IAIMPExtensionEmbeddedVisualization }

  IAIMPExtensionEmbeddedVisualization = interface(IUnknown)
  [SID_IAIMPExtensionEmbeddedVisualization]
    // Common information
    function GetFlags: Integer; stdcall;
    function GetMaxDisplaySize(out Width, Height: Integer): HRESULT; stdcall;
    function GetName(out S: IAIMPString): HRESULT; stdcall;
    // Initialization / Finalization
    function Initialize(Width, Height: Integer): HRESULT; stdcall;
    procedure Finalize; stdcall;
    // Basic functionality
    procedure Click(X, Y: Integer; Button: Integer); stdcall;
    procedure Draw(Canvas: HCANVAS; Data: PAIMPVisualData); stdcall;
    procedure Resize(NewWidth, NewHeight: Integer); stdcall;
  end;

  { IAIMPServiceVisualizations }

  IAIMPServiceVisualizations = interface(IUnknown)
  [SID_IAIMPServiceVisualizations]
  end;

implementation

end.

