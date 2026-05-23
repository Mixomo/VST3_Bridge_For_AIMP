////////////////////////////////////////////////////////////////////////////////
//
//  Project:   AIMP
//             Programming Interface
//
//  Target:    v5.40 build 2650
//
//  Purpose:   Player API
//
//  Author:    Artem Izmaylov
//             © 2006-2025
//             www.aimp.ru
//
//  FPC:       OK
//
unit apiPlayer;

{$I apiConfig.inc}

interface

uses
  apiFileManager,
  apiObjects,
  apiPlaylists,
  apiThreading;

const
  SID_IAIMPEqualizerBands = '{41494D50-4571-4261-6E64-730000000000}';
  IID_IAIMPEqualizerBands: TGUID = SID_IAIMPEqualizerBands;

  SID_IAIMPEqualizerPreset = '{41494D50-4571-5072-7374-320000000000}';
  IID_IAIMPEqualizerPreset: TGUID = SID_IAIMPEqualizerPreset;

  SID_IAIMPPlaybackQueueItem = '{41494D50-506C-6179-6261-636B5149746D}';
  IID_IAIMPPlaybackQueueItem: TGUID = SID_IAIMPPlaybackQueueItem;

  SID_IAIMPExtensionPlaybackQueue = '{41494D50-4578-7450-6C61-796261636B51}';
  IID_IAIMPExtensionPlaybackQueue: TGUID = SID_IAIMPExtensionPlaybackQueue;

  SID_IAIMPExtensionPlaybackQueue2 = '{41494D50-4578-7450-6C61-7962636B5132}';
  IID_IAIMPExtensionPlaybackQueue2: TGUID = SID_IAIMPExtensionPlaybackQueue2;

  SID_IAIMPServicePlaybackQueue = '{41494D50-5372-7650-6C62-61636B510000}';
  IID_IAIMPServicePlaybackQueue: TGUID = SID_IAIMPServicePlaybackQueue;

  SID_IAIMPServicePlaybackQueue2 = '{41494D50-5372-7650-6C62-61636B513200}';
  IID_IAIMPServicePlaybackQueue2: TGUID = SID_IAIMPServicePlaybackQueue2;

  SID_IAIMPServicePlayer = '{41494D50-5372-7650-6C61-796572000000}';
  IID_IAIMPServicePlayer: TGUID = SID_IAIMPServicePlayer;

  SID_IAIMPServicePlayer2 = '{41494D50-5372-7650-6C61-796572320000}';
  IID_IAIMPServicePlayer2: TGUID = SID_IAIMPServicePlayer2;

  SID_IAIMPServicePlayerEqualizer = '{41494D50-5372-7645-5132-000000000000}';
  IID_IAIMPServicePlayerEqualizer: TGUID = SID_IAIMPServicePlayerEqualizer;

  SID_IAIMPServicePlayerEqualizerPresets = '{41494D50-5372-7645-5150-727374733200}';
  IID_IAIMPServicePlayerEqualizerPresets: TGUID = SID_IAIMPServicePlayerEqualizerPresets;

  SID_IAIMPExtensionPlayerHook = '{41494D50-4578-7450-6C72-486F6F6B0000}';
  IID_IAIMPExtensionPlayerHook: TGUID = SID_IAIMPExtensionPlayerHook;

  SID_IAIMPExtensionWaveformProvider = '{41494D50-4578-7457-6176-507276000000}';
  IID_IAIMPExtensionWaveformProvider: TGUID = SID_IAIMPExtensionWaveformProvider;

  SID_IAIMPServiceWaveform = '{41494D50-5372-7657-6176-650000000000}';
  IID_IAIMPServiceWaveform: TGUID = SID_IAIMPServiceWaveform;

  // PropIDs for IAIMPPlaybackQueueItem
  AIMP_PLAYBACKQUEUEITEM_PROPID_CUSTOM       = 0;
  AIMP_PLAYBACKQUEUEITEM_PROPID_PLAYLISTITEM = 1;
  AIMP_PLAYBACKQUEUEITEM_PROPID_OFFSET       = 2; // v5.01

  // Flags for IAIMPExtensionPlaybackQueue.GetNext / GetPrev
  AIMP_PLAYBACKQUEUE_FLAGS_START_FROM_BEGINNING = 1;
  AIMP_PLAYBACKQUEUE_FLAGS_START_FROM_CURSOR    = 2;
  AIMP_PLAYBACKQUEUE_FLAGS_START_FROM_ITEM      = 3;

  // Flags for IAIMPServicePlayer.Play2 / Play4
  AIMP_SERVICE_PLAYER_FLAGS_PLAY_FROM_PLAYLIST              = 1;
  AIMP_SERVICE_PLAYER_FLAGS_PLAY_FROM_PLAYLIST_CAN_ADD      = 2;
  AIMP_SERVICE_PLAYER_FLAGS_PLAY_WITHOUT_ADDING_TO_PLAYLIST = 4;
  AIMP_SERVICE_PLAYER_FLAGS_PLAY_SUSPENDED                  = 8;

  // PropIDs for IAIMPPropertyList from IAIMPServicePlayer
  AIMP_PLAYER_PROPID_ACTION_ON_END_OF_TRACK               = 1;
  AIMP_PLAYER_PROPID_AUTO_JUMP_TO_NEXT_TRACK              = 2;
  AIMP_PLAYER_PROPID_AUTOSWITCHING                        = 10;
  AIMP_PLAYER_PROPID_AUTOSWITCHING_CROSSFADE              = 11; // msec
  AIMP_PLAYER_PROPID_AUTOSWITCHING_FADEIN                 = 12; // msec
  AIMP_PLAYER_PROPID_AUTOSWITCHING_FADEOUT                = 13; // msec
  AIMP_PLAYER_PROPID_AUTOSWITCHING_PAUSE_BETWEEN_TRACKS   = 14; // msec
  AIMP_PLAYER_PROPID_MANUALSWITCHING                      = 20;
  AIMP_PLAYER_PROPID_MANUALSWITCHING_CROSSFADE            = 21; // msec
  AIMP_PLAYER_PROPID_MANUALSWITCHING_FADEIN               = 22; // msec
  AIMP_PLAYER_PROPID_MANUALSWITCHING_FADEOUT              = 23; // msec
  AIMP_PLAYER_PROPID_OUTPUT                               = 30; // IAIMPString or IAIMPObjectList

  // IAIMPServicePlayer.GetState
  AIMP_PLAYER_STATE_STOPPED = 0;
  AIMP_PLAYER_STATE_PAUSED  = 1;
  AIMP_PLAYER_STATE_PLAYING = 2;

type
  PAIMPWaveformPeakInfo = ^TAIMPWaveformPeakInfo;
  TAIMPWaveformPeakInfo = packed record
    MaxNegative: Word;
    MaxPositive: Word;

    function Max: Word; inline;
  end;

  PAIMPWaveformPeakInfoArray = ^TAIMPWaveformPeakInfoArray;
  TAIMPWaveformPeakInfoArray = array [0..0] of TAIMPWaveformPeakInfo;

  { IAIMPEqualizerBands }

  IAIMPEqualizerBands = interface(IUnknown)
  [SID_IAIMPEqualizerBands]
    function GetBandCount: Integer; stdcall;
    function GetBandFrequency(BandIndex: Integer; out Value: Double): HRESULT; stdcall;
    function GetBandGain(BandIndex: Integer; out Value: Double): HRESULT; stdcall;
    function SetBandGain(BandIndex: Integer; const Value: Double): HRESULT; stdcall;
  end;

  { IAIMPEqualizerPreset }

  IAIMPEqualizerPreset = interface(IAIMPEqualizerBands)
  [SID_IAIMPEqualizerPreset]
    function GetName(out S: IAIMPString): HRESULT; stdcall;
    function SetName(S: IAIMPString): HRESULT; stdcall;
  end;

  { IAIMPPlaybackQueueItem }

  IAIMPPlaybackQueueItem = interface(IAIMPPropertyList)
  [SID_IAIMPPlaybackQueueItem]
  end;

//----------------------------------------------------------------------------------------------------------------------
// Extensions
//----------------------------------------------------------------------------------------------------------------------

  { IAIMPExtensionPlayerHook }

  IAIMPExtensionPlayerHook = interface(IUnknown)
  [SID_IAIMPExtensionPlayerHook]
    procedure OnCheckURL(URL: IAIMPString; var Handled: LongBool); stdcall;
  end;

  { IAIMPExtensionPlaybackQueue }

  IAIMPExtensionPlaybackQueue = interface(IUnknown)
  [SID_IAIMPExtensionPlaybackQueue]
    function GetNext(Current: IUnknown; Flags: LongWord; QueueItem: IAIMPPlaybackQueueItem): LongBool; stdcall;
    function GetPrev(Current: IUnknown; Flags: LongWord; QueueItem: IAIMPPlaybackQueueItem): LongBool; stdcall;
    procedure OnSelect(Item: IAIMPPlaylistItem; QueueItem: IAIMPPlaybackQueueItem); stdcall;
  end;

  { IAIMPExtensionPlaybackQueue2 }

  IAIMPExtensionPlaybackQueue2 = interface(IAIMPExtensionPlaybackQueue)
  [SID_IAIMPExtensionPlaybackQueue2]
    function GetInfo(Current: IUnknown; out Position, Size: Integer): LongBool; stdcall;
  end;

  { IAIMPExtensionWaveformProvider }

  IAIMPExtensionWaveformProvider = interface
  [SID_IAIMPExtensionWaveformProvider]
    function Calculate(FileURI: IAIMPString; TaskOwner: IAIMPTaskOwner;
      Peaks: PAIMPWaveformPeakInfoArray; PeakCount: Integer): HRESULT; stdcall;
  end;

//----------------------------------------------------------------------------------------------------------------------
// Services
//----------------------------------------------------------------------------------------------------------------------

  { IAIMPServicePlayer }

  IAIMPServicePlayer = interface(IUnknown) // + IAIMPPropertyList
  [SID_IAIMPServicePlayer]
    // Start Playback
    function Play(Item: IAIMPPlaybackQueueItem): HRESULT; stdcall;
    function Play2(Item: IAIMPPlaylistItem): HRESULT; stdcall;
    function Play3(Playlist: IAIMPPlaylist): HRESULT; stdcall;
    function Play4(FileURI: IAIMPString; Flags: LongWord): HRESULT; stdcall;

    // Navigation
    function GoToNext: HRESULT; stdcall;
    function GoToPrev: HRESULT; stdcall;

    // Playing File Control
    function GetDuration(out Seconds: Double): HRESULT; stdcall;
    function GetPosition(out Seconds: Double): HRESULT; stdcall;
    function SetPosition(const Seconds: Double): HRESULT; stdcall;
    function GetMute(out Value: LongBool): HRESULT; stdcall;
    function SetMute(const Value: LongBool): HRESULT; stdcall;
    function GetVolume(out Level: Single): HRESULT; stdcall;
    function SetVolume(const Level: Single): HRESULT; stdcall;
    function GetInfo(out FileInfo: IAIMPFileInfo): HRESULT; stdcall;
    function GetPlaylistItem(out Item: IAIMPPlaylistItem): HRESULT; stdcall;
    function GetState: Integer; stdcall; // AIMP_PLAYER_STATE_XXX
    function Pause: HRESULT; stdcall;
    function Resume: HRESULT; stdcall;
    function Stop: HRESULT; stdcall;
    function StopAfterTrack: HRESULT; stdcall;
  end;

  { IAIMPServicePlayer2 }

  IAIMPServicePlayer2 = interface(IAIMPServicePlayer)
  [SID_IAIMPServicePlayer2]
    function Play(Item: IAIMPPlaybackQueueItem; Offset: Single; Flags: LongWord): HRESULT; overload; stdcall;
    function Play2(Item: IAIMPPlaylistItem; Offset: Single; Flags: LongWord): HRESULT; overload; stdcall;
    function Play4(FileURI: IAIMPString; Offset: Single; Flags: LongWord): HRESULT; overload; stdcall;
  end;

  { IAIMPServicePlayerEqualizer }

  IAIMPServicePlayerEqualizer = interface(IAIMPEqualizerBands)
  [SID_IAIMPServicePlayerEqualizer]
    function GetActive: LongBool;
    function SetActive(Value: LongBool): HRESULT; stdcall;

    function GetPreamp(out Value: Double): HRESULT; stdcall;
    function SetPreamp(const Value: Double): HRESULT; stdcall;

    function GetPreset(const IID: TGUID; out Preset): HRESULT; stdcall;
    function SetPreset(Preset: IAIMPEqualizerPreset): HRESULT; stdcall;
  end;

  { IAIMPServicePlayerEqualizerPresets }

  IAIMPServicePlayerEqualizerPresets = interface(IUnknown)
  [SID_IAIMPServicePlayerEqualizerPresets]
    function Add(Name: IAIMPString; const IID: TGUID; out Preset): HRESULT; stdcall;
    function FindByName(Name: IAIMPString; const IID: TGUID; out Preset): HRESULT; stdcall;
    function Delete(Preset: IAIMPEqualizerPreset): HRESULT; stdcall;
    function Delete2(Index: Integer): HRESULT; stdcall;

    function GetPreset(Index: Integer; const IID: TGUID; out Preset): HRESULT; stdcall;
    function GetPresetCount: Integer; stdcall;
  end;

  { IAIMPServicePlaybackQueue }

  IAIMPServicePlaybackQueue = interface(IUnknown)
  [SID_IAIMPServicePlaybackQueue]
    function GetNextTrack(out Item: IAIMPPlaybackQueueItem): HRESULT; stdcall;
    function GetPrevTrack(out Item: IAIMPPlaybackQueueItem): HRESULT; stdcall;
  end;

  { IAIMPServicePlaybackQueue2 }

  IAIMPServicePlaybackQueue2 = interface(IAIMPServicePlaybackQueue)
  [SID_IAIMPServicePlaybackQueue2]
    procedure NotifyChanged(Sender: IAIMPExtensionPlaybackQueue); stdcall;
  end;

  { IAIMPServiceWaveform }

  IAIMPServiceWaveform = interface
  [SID_IAIMPServiceWaveform]
  end;

implementation

{ TAIMPWaveformPeakInfo }

function TAIMPWaveformPeakInfo.Max: Word;
begin
  Result := MaxPositive;
  if MaxNegative > Result then
    Result := MaxNegative;
end;

end.
