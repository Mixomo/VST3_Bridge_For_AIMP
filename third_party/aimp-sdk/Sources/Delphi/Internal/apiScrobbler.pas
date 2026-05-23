////////////////////////////////////////////////////////////////////////////////
//
//  Project:   AIMP
//             Programming Interface
//
//  Target:    v5.40 build 2650
//
//  Purpose:   Scrobbler Plugin Internal API
//
//  Author:    Artem Izmaylov
//             © 2006-2025
//             www.aimp.ru
//
//  FPC:       OK
//
unit apiScrobbler;

{$I apiConfig.inc}

interface

uses
  apiObjects;

const
  SID_IAIMPServiceScrobbler = '{41494D50-5372-7653-6372-6F62626C6572}';
  IID_IAIMPServiceScrobbler: TGUID = SID_IAIMPServiceScrobbler;

  SID_IAIMPScrobblerListenerTrackInfo = '{41494D50-5363-7262-6C72-54726B496E66}';
  IID_IAIMPScrobblerListenerTrackInfo: TGUID = SID_IAIMPScrobblerListenerTrackInfo;

  SID_IAIMPScrobblerListenerState = '{41494D50-5363-7262-6C72-537461746500}';
  IID_IAIMPScrobblerListenerState: TGUID = SID_IAIMPScrobblerListenerState;

//  static const GUID IID_IAIMPScrobblerListenerState = {0x41494D50, 0x5363, 0x7262, 0x6C, 0x72, 0x53, 0x74, 0x61, 0x74, 0x65, 0x00};
//  static const GUID IID_IAIMPScrobblerListenerTrackInfo = {0x41494D50, 0x5363, 0x7262, 0x6C, 0x72, 0x54, 0x72, 0x6B, 0x49, 0x6E, 0x66};
//  static const GUID IID_IAIMPServiceScrobbler = {0x41494D50, 0x5372, 0x7653, 0x63, 0x72, 0x6F, 0x62, 0x62, 0x6C, 0x65, 0x72};

  AIMP_SCROBBLER_WIKIINFO_IMAGE   = 0;
  AIMP_SCROBBLER_WIKIINFO_TITLE   = 1;
  AIMP_SCROBBLER_WIKIINFO_CONTENT = 2;
  AIMP_SCROBBLER_WIKIINFO_TAGS    = 3;
  AIMP_SCROBBLER_WIKIINFO_URL     = 4;

  AIMP_SCROBBLER_PROPID_SCROBBLING = 1;
  AIMP_SCROBBLER_PROPID_LIKED      = 2;

type

  { IAIMPScrobblerWikiInfo }

  IAIMPScrobblerWikiInfo = IAIMPPropertyList2;

  { IAIMPScrobblerListenerTrackInfo }

  IAIMPScrobblerListenerTrackInfo = interface
  [SID_IAIMPScrobblerListenerTrackInfo]
    procedure OnTrackInfo({nullable}Info: IAIMPScrobblerWikiInfo); stdcall;
  end;

  { IAIMPScrobblerListenerState }

  IAIMPScrobblerListenerState = interface
  [SID_IAIMPScrobblerListenerState]
    procedure OnOptionsChanged; stdcall;
    procedure OnTrackScrobbled; stdcall;
    procedure OnTrackStateChanged; stdcall;
  end;

  { IAIMPServiceScrobbler }

  IAIMPServiceScrobbler = interface
  [SID_IAIMPServiceScrobbler]
    function ListenerAdd(Listener: IUnknown): HRESULT; stdcall;
    function ListenerRemove(Listener: IUnknown): HRESULT; stdcall;
  end;

implementation

end.
