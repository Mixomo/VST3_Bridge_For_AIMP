////////////////////////////////////////////////////////////////////////////////
//
//  Project:   AIMP
//             Programming Interface
//
//  Target:    v5.40 build 2650
//
//  Purpose:   Lyrics API
//
//  Author:    Artem Izmaylov
//             © 2006-2025
//             www.aimp.ru
//
//  FPC:       OK
//
unit apiLyrics;

{$I apiConfig.inc}

interface

uses
  apiObjects,
  apiFileManager,
  apiThreading,
  apiTypes;

const
  SID_IAIMPServiceLyrics = '{41494D50-5372-764C-7972-697800000000}';
  IID_IAIMPServiceLyrics: TGUID = SID_IAIMPServiceLyrics;

  SID_IAIMPExtensionLyricsProvider = '{41494D50-4578-744C-7972-697850727600}';
  IID_IAIMPExtensionLyricsProvider: TGUID = SID_IAIMPExtensionLyricsProvider;

  SID_IAIMPLyrics = '{41494D50-4C79-7269-6373-46696C650000}';
  IID_IAIMPLyrics: TGUID = SID_IAIMPLyrics;

  // PropertyID for the IAIMPLyrics
  AIMP_LYRICS_PROPID_TEXT     = 1;
  AIMP_LYRICS_PROPID_TYPE     = 2;
  AIMP_LYRICS_PROPID_LYRICIST = 3;
  AIMP_LYRICS_PROPID_OFFSET   = 4;
  AIMP_LYRICS_PROPID_ALBUM    = 5;
  AIMP_LYRICS_PROPID_TITLE    = 6;
  AIMP_LYRICS_PROPID_CREATOR  = 7;
  AIMP_LYRICS_PROPID_APP      = 8;
  AIMP_LYRICS_PROPID_APPVER   = 9;

  // Lyrics Type
  AIMP_LYRICS_TYPE_UNKNOWN   = 0;
  AIMP_LYRICS_TYPE_UNSYNCED  = 1;
  AIMP_LYRICS_TYPE_SYNCED    = 2;

  // IAIMPLyrics's File Format
  AIMP_LYRICS_FORMAT_TXT = 0;
  AIMP_LYRICS_FORMAT_LRC = 1;
  AIMP_LYRICS_FORMAT_SRT = 2;

  // Flags for IAIMPServiceLyrics.Get
  AIMP_SERVICE_LYRICS_FLAGS_NOCACHE = 1;
  AIMP_SERVICE_LYRICS_FLAGS_WAITFOR = 4;

  // IAIMPExtensionLyricsProvider.GetCategory
  AIMP_LYRICS_PROVIDER_CATEGORY_FILE     = 1;
  AIMP_LYRICS_PROVIDER_CATEGORY_INTERNET = 2;

type

  { IAIMPLyrics }

  IAIMPLyrics = interface(IAIMPPropertyList)
  [SID_IAIMPLyrics]
    function Assign(Source: IAIMPLyrics): HRESULT; stdcall;
	  function Clone(out Target: IAIMPLyrics): HRESULT; stdcall;
    //
    function Add(TimeStart, TimeFinish: Integer; Text: IAIMPString): HRESULT; stdcall;
    function Delete(Index: Integer): HRESULT; stdcall;
    function Find(Time: Integer; out Index: Integer; out Text: IAIMPString): HRESULT; stdcall;
    function Get(Index: Integer; out TimeStart, TimeFinish: Integer; out Text: IAIMPString): HRESULT; stdcall;
    function GetCount(out Value: Integer): HRESULT; stdcall;
    // I/O
    function LoadFromFile(FileURI: IAIMPString): HRESULT; stdcall;
    function LoadFromStream(Stream: IAIMPStream; Format: Integer): HRESULT; stdcall;
    function LoadFromString(&String: IAIMPString; Format: Integer): HRESULT; stdcall;
    function SaveToFile(FileURI: IAIMPString): HRESULT; stdcall;
    function SaveToStream(Stream: IAIMPStream; Format: Integer): HRESULT; stdcall;
    function SaveToString(out &String: IAIMPString; Format: Integer): HRESULT; stdcall;
  end;

  { IAIMPExtensionLyricsProvider }

  IAIMPExtensionLyricsProvider = interface
  [SID_IAIMPExtensionLyricsProvider]
    function Get(Owner: IAIMPTaskOwner; FileInfo: IAIMPFileInfo;
      Flags: LongWord; Lyrics: IAIMPLyrics): HRESULT; stdcall;
    function GetCategory: LongWord; stdcall;
  end;

  { IAIMPServiceLyrics }

  TAIMPServiceLyricsReceiveProc = procedure (Lyrics: IAIMPLyrics; UserData: Pointer); stdcall;

  IAIMPServiceLyrics = interface
  [SID_IAIMPServiceLyrics]
    function Get(FileInfo: IAIMPFileInfo; Flags: LongWord;
      CallbackProc: TAIMPServiceLyricsReceiveProc;
      UserData: Pointer; out TaskID: TTaskHandle): HRESULT; stdcall;
    function Cancel(TaskID: TTaskHandle; Flags: LongWord): HRESULT; stdcall;
  end;

implementation

end.
