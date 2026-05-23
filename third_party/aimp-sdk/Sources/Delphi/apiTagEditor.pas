////////////////////////////////////////////////////////////////////////////////
//
//  Project:   AIMP
//             Programming Interface
//
//  Target:    v5.40 build 2650
//
//  Purpose:   Tags Library API
//
//  Author:    Artem Izmaylov
//             © 2006-2025
//             www.aimp.ru
//
//  FPC:       OK
//
unit apiTagEditor;

{$I apiConfig.inc}

interface

uses
  apiObjects,
  apiFileManager,
  apiThreading;

const
  SID_IAIMPFileTag = '{41494D50-4669-6C65-5461-670000000000}';
  IID_IAIMPFileTag: TGUID = SID_IAIMPFileTag;

  SID_IAIMPFileTagEditor = '{41494D50-4669-6C65-5461-674564697400}';
  IID_IAIMPFileTagEditor: TGUID = SID_IAIMPFileTagEditor;

  SID_IAIMPServiceFileTagEditor = '{41494D50-5372-7654-6167-456469740000}';
  IID_IAIMPServiceFileTagEditor: TGUID = SID_IAIMPServiceFileTagEditor;

  SID_IAIMPExtensionTagsProvider = '{41494D50-4578-7446-696E-645461677332}';
  IID_IAIMPExtensionTagsProvider: TGUID = SID_IAIMPExtensionTagsProvider;

  SID_IAIMPServiceFindTagsOnline = '{41494D50-5372-7646-696E-645461677300}';
  IID_IAIMPServiceFindTagsOnline: TGUID = SID_IAIMPServiceFindTagsOnline;

  // PropertyID for the IAIMPFileTag
  AIMP_FILETAG_PROPID_BASE             = 100;
  AIMP_FILETAG_PROPID_TAG_ID           = AIMP_FILETAG_PROPID_BASE + 1;
  AIMP_FILETAG_PROPID_DELETE_ON_SAVING = AIMP_FILETAG_PROPID_BASE + 2;

  // IDs for IAIMPFileTag.AIMP_FILETAG_PROPID_TAG_ID
  AIMP_FILETAG_ID_CUSTOM = 0;
  AIMP_FILETAG_ID_APEv2  = 1;
  AIMP_FILETAG_ID_ID3v1  = 2;
  AIMP_FILETAG_ID_ID3v2  = 3;
  AIMP_FILETAG_ID_MP4    = 4;
  AIMP_FILETAG_ID_VORBIS = 5;
  AIMP_FILETAG_ID_WMA    = 6;

type

  { IAIMPFileTag }

  IAIMPFileTag = interface(IAIMPFileInfo)
  [SID_IAIMPFileTag]
  end;

  { IAIMPFileTagEditor }

  IAIMPFileTagEditor = interface(IUnknown)
  [SID_IAIMPFileTagEditor]
    // Info
    function GetMixedInfo(out Info: IAIMPFileInfo): HRESULT; stdcall;
    function GetTag(Index: Integer; const IID: TGUID; out Obj): HRESULT; stdcall;
    function GetTagCount: Integer; stdcall;
    function SetToAll(Info: IAIMPFileInfo): HRESULT; stdcall;
    // Save
    function Save: HRESULT; stdcall;
  end;

  { IAIMPExtensionTagsProvider }

  TAIMPServiceFindTagsOnlineAlbumInfoReceiveProc = procedure (AlbumInfo: IAIMPFileInfo; Data: Pointer); stdcall;

  IAIMPExtensionTagsProvider = interface
  [SID_IAIMPExtensionTagsProvider]
    function GetName(out S: IAIMPString): HRESULT; stdcall;
    function GetSupportedFields(out Fields: PInteger; out Count: Integer): HRESULT; stdcall;

    function FindAlbums(Query: IAIMPString; Owner: IAIMPTaskOwner; ErrorInfo: IAIMPErrorInfo;
      ReceiveProc: TAIMPServiceFindTagsOnlineAlbumInfoReceiveProc; ReceiveProcData: Pointer): HRESULT; stdcall;
    function FindTracks(AlbumInfo: IAIMPFileInfo; Owner: IAIMPTaskOwner;
      ErrorInfo: IAIMPErrorInfo; out TracksInfo: IAIMPObjectList): HRESULT; stdcall;
  end;

  { IAIMPServiceFindTagsOnline }

  IAIMPServiceFindTagsOnline = interface
  [SID_IAIMPServiceFindTagsOnline]
  end;

  { IAIMPServiceFileTagEditor }

  IAIMPServiceFileTagEditor = interface(IUnknown)
  [SID_IAIMPServiceFileTagEditor]
    function EditFile(Source: IUnknown; const IID: TGUID; out Obj): HRESULT; stdcall;
    function EditTag(Source: IUnknown; TagID: Integer; const IID: TGUID; out Obj): HRESULT; stdcall;
  end;

implementation

end.
