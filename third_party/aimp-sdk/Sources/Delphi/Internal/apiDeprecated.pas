////////////////////////////////////////////////////////////////////////////////
//
//  Project:   AIMP
//             Programming Interface
//
//  Target:    v5.40 build 2650
//
//  Purpose:   For compatibility reasons only
//
//  Author:    Artem Izmaylov
//             © 2006-2025
//             www.aimp.ru
//
//  FPC:       OK
//
unit apiDeprecated;

{$I apiConfig.inc}

interface

uses
  apiObjects, apiTypes;

const
  AIMP_SCHEDULER_MSG_EVENT_NEARESTTASK = 'AIMP.Scheduler.MSG.NearestTask';

type
  PAIMPSchedulerTaskInfo = ^TAIMPSchedulerTaskInfo;
  TAIMPSchedulerTaskInfo = record
    cbSize: Cardinal;
    TaskName: PChar;
    TimeRemaining: UInt64;
  end;

  IAIMPDeprecatedTaskOwner = interface(IUnknown)
  ['{41494D50-5461-736B-4F77-6E6572000000}']
    function IsCanceled: LongBool;
  end;

  IAIMPDeprecatedTask = interface(IUnknown)
  ['{41494D50-5461-736B-0000-000000000000}']
    procedure Execute(Owner: IAIMPDeprecatedTaskOwner); stdcall;
  end;

  { IAIMPDeprecatedServiceSynchronizer }

  IAIMPDeprecatedServiceSynchronizer = interface(IUnknown)
  ['{41494D50-5372-7653-796E-637200000000}']
    function ExecuteInMainThread(Task: IAIMPDeprecatedTask; ExecuteNow: LongBool): HRESULT; stdcall;
  end;

  { IAIMPDeprecatedServiceThreadPool }

  IAIMPDeprecatedServiceThreadPool = interface(IUnknown)
  ['{41494D50-5372-7654-6872-64506F6F6C00}']
    function Cancel(TaskHandle: TTaskHandle; Flags: Cardinal): HRESULT; stdcall;
    function Execute(Task: IAIMPDeprecatedTask; out TaskHandle: TTaskHandle): HRESULT; stdcall;
    function WaitFor(TaskHandle: TTaskHandle): HRESULT; stdcall;
  end;

  { IAIMPDeprecatedEqualizerPreset }

  IAIMPDeprecatedEqualizerPreset = interface(IUnknown)
  ['{41494D50-4571-5072-7374-000000000000}']
    function GetName(out S: IAIMPString): HRESULT; stdcall;
    function SetName(S: IAIMPString): HRESULT; stdcall;
    function GetBandValue(BandIndex: Integer; out Value: Double): HRESULT; stdcall;
    function SetBandValue(BandIndex: Integer; const Value: Double): HRESULT; stdcall;
  end;

  { IAIMPDeprecatedServicePlayerEqualizer }

  IAIMPDeprecatedServicePlayerEqualizer = interface(IUnknown)
  ['{41494D50-5372-7645-5100-000000000000}']
    function GetActive: LongBool;
    function SetActive(Value: LongBool): HRESULT; stdcall;

    function GetBandValue(BandIndex: Integer; out Value: Double): HRESULT; stdcall;
    function SetBandValue(BandIndex: Integer; const Value: Double): HRESULT; stdcall;

    function GetPreset(out Preset: IAIMPDeprecatedEqualizerPreset): HRESULT; stdcall;
    function SetPreset(Preset: IAIMPDeprecatedEqualizerPreset): HRESULT; stdcall;
  end;

  { IAIMPDeprecatedServicePlayerEqualizerPresets }

  IAIMPDeprecatedServicePlayerEqualizerPresets = interface(IUnknown)
  ['{41494D50-5372-7645-5150-727374730000}']
    function Add(Name: IAIMPString; out Preset: IAIMPDeprecatedEqualizerPreset): HRESULT; stdcall;
    function FindByName(Name: IAIMPString; out Preset: IAIMPDeprecatedEqualizerPreset): HRESULT; stdcall;
    function Delete(Preset: IAIMPDeprecatedEqualizerPreset): HRESULT; stdcall;
    function Delete2(Index: Integer): HRESULT; stdcall;

    function GetPreset(Index: Integer; out Preset: IAIMPDeprecatedEqualizerPreset): HRESULT; stdcall;
    function GetPresetCount: Integer; stdcall;
  end;

implementation

end.
