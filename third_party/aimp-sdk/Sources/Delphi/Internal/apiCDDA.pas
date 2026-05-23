////////////////////////////////////////////////////////////////////////////////
//
//  Project:   AIMP
//             Programming Interface
//
//  Target:    v5.40 build 2650
//
//  Purpose:   CDDA Plugin Internal API
//
//  Author:    Artem Izmaylov
//             © 2006-2025
//             www.aimp.ru
//
//  FPC:       OK
//
unit apiCDDA;

{$I apiConfig.inc}

interface

uses
  apiObjects;

const
  SID_IAIMPServiceCompactDiskAudio = '{41494D50-5372-7643-4444-410000000000}';
  IID_IAIMPServiceCompactDiskAudio: TGUID = SID_IAIMPServiceCompactDiskAudio;
  {0x504D4941, 0x7253, 0x4376, 0x44, 0x44, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00}

  SID_IAIMPCompactDiskAudioDriveInfo = '{41494D50-4344-4441-4472-76496E660000}';
  IID_IAIMPCompactDiskAudioDriveInfo: TGUID = SID_IAIMPCompactDiskAudioDriveInfo;
  {0x41494D50, 0x4344, 0x4441, 0x44, 0x72, 0x76, 0x49, 0x6E, 0x66, 0x00, 0x00}

type

  { IAIMPCompactDiskAudioDriveInfo }

  IAIMPCompactDiskAudioDriveInfo = interface
  [SID_IAIMPCompactDiskAudioDriveInfo]
    function GetDriveFriendlyName(out S: IAIMPString): HRESULT; stdcall;
    function GetDriveIndex: Integer; stdcall;
    function GetDriveLetter: Char; stdcall;
  end;

  { IAIMPServiceCompactDiskAudio }

  IAIMPServiceCompactDiskAudio = interface
  [SID_IAIMPServiceCompactDiskAudio]
    function PopulateDrives(out List: IAIMPObjectList): HRESULT; stdcall;
    function PopulateFiles(DriveIndex: Integer; out List: IAIMPObjectList): HRESULT; stdcall;
    function PopulateFiles2(Drive: Char; out List: IAIMPObjectList): HRESULT; stdcall;
  end;

implementation

end.
