////////////////////////////////////////////////////////////////////////////////
//
//  Project:   AIMP
//             Programming Interface
//
//  Target:    v5.40 build 2650
//
//  Purpose:   Audio Decoders API
//
//  Author:    Artem Izmaylov
//             © 2006-2025
//             www.aimp.ru
//
//  FPC:       OK
//
unit apiDecoders;

{$I apiConfig.inc}

interface

uses
  apiFileManager,
  apiObjects;

const
  SID_IAIMPAudioDecoder = '{41494D50-4175-6469-6F44-656300000000}';
  IID_IAIMPAudioDecoder: TGUID = SID_IAIMPAudioDecoder;

  SID_IAIMPAudioDecoderBufferingProgress = '{41494D50-4175-6469-6F44-656342756666}';
  IID_IAIMPAudioDecoderBufferingProgress: TGUID = SID_IAIMPAudioDecoderBufferingProgress;

  SID_IAIMPAudioDecoderListener = '{41494D50-4175-6469-6F44-65634C737400}';
  IID_IAIMPAudioDecoderListener: TGUID = SID_IAIMPAudioDecoderListener;

  SID_IAIMPAudioDecoderNotifications = '{41494D50-4175-6469-6F44-65634E746679}';
  IID_IAIMPAudioDecoderNotifications: TGUID = SID_IAIMPAudioDecoderNotifications;

  SID_IAIMPExtensionAudioDecoder = '{41494D50-4578-7441-7564-696F44656300}';
  IID_IAIMPExtensionAudioDecoder: TGUID = SID_IAIMPExtensionAudioDecoder;

  SID_IAIMPExtensionAudioDecoderOld = '{41494D50-4578-7441-7564-696F4465634F}';
  IID_IAIMPExtensionAudioDecoderOld: TGUID = SID_IAIMPExtensionAudioDecoderOld;

  SID_IAIMPExtensionAudioDecoderPriority = '{41494D50-4578-7444-6563-5072696F7200}';
  IID_IAIMPExtensionAudioDecoderPriority: TGUID = SID_IAIMPExtensionAudioDecoderPriority;

  SID_IAIMPServiceAudioDecoders = '{41494D50-5372-7641-7564-696F44656300}';
  IID_IAIMPServiceAudioDecoders: TGUID = SID_IAIMPServiceAudioDecoders;

const
  AIMP_DECODER_SAMPLEFORMAT_08BIT      = 1;
  AIMP_DECODER_SAMPLEFORMAT_16BIT      = 2;
  AIMP_DECODER_SAMPLEFORMAT_24BIT      = 3;
  AIMP_DECODER_SAMPLEFORMAT_32BIT      = 4;
  AIMP_DECODER_SAMPLEFORMAT_32BITFLOAT = 5;
  AIMP_DECODER_SAMPLEFORMAT_64BITFLOAT = 6; // v5.40

  // Flags for IAIMPExtensionAudioDecoder / IAIMPExtensionAudioDecoderOld
  AIMP_DECODER_FLAGS_FORCE_CREATE_INSTANCE = $1000;

  // Flags for IAIMPAudioDecoderListener.Changed
  AIMP_DECODER_CHANGE_INPUTFORMAT = 1;

type

  { IAIMPAudioDecoder }

  IAIMPAudioDecoder = interface(IUnknown)
  [SID_IAIMPAudioDecoder]
    function GetFileInfo(FileInfo: IAIMPFileInfo): LongBool; stdcall;
    function GetStreamInfo(out SampleRate, Channels, SampleFormat: Integer): LongBool; stdcall; 

    function IsSeekable: LongBool; stdcall;
    function IsRealTimeStream: LongBool; stdcall;

    function GetAvailableData: Int64; stdcall;
    function GetSize: Int64; stdcall;
    function GetPosition: Int64; stdcall;
    function SetPosition(const Value: Int64): LongBool; stdcall;

    function Read(Buffer: PByte; Count: Integer): Integer; stdcall;
  end;

  { IAIMPAudioDecoderBufferingProgress }

  IAIMPAudioDecoderBufferingProgress = interface
  [SID_IAIMPAudioDecoderBufferingProgress]
    function Get(out Value: Double): LongBool; stdcall;
  end;

  { IAIMPAudioDecoderListener }

  IAIMPAudioDecoderListener = interface
  [SID_IAIMPAudioDecoderListener]
    procedure Changed(Changes: Integer);
  end;

  { IAIMPAudioDecoderNotifications }

  IAIMPAudioDecoderNotifications = interface
  [SID_IAIMPAudioDecoderNotifications]
    procedure ListenerAdd(Listener: IAIMPAudioDecoderListener); stdcall;
    procedure ListenerRemove(Listener: IAIMPAudioDecoderListener); stdcall;
  end;

  { IAIMPExtensionAudioDecoder }

  IAIMPExtensionAudioDecoder = interface(IUnknown)
  [SID_IAIMPExtensionAudioDecoder]
    function CreateDecoder(Stream: IAIMPStream; Flags: LongWord;
      ErrorInfo: IAIMPErrorInfo; out Decoder: IAIMPAudioDecoder): HRESULT; stdcall;
  end;

  { IAIMPExtensionAudioDecoderOld }

  IAIMPExtensionAudioDecoderOld = interface(IUnknown)
  [SID_IAIMPExtensionAudioDecoderOld]
    function CreateDecoder(FileName: IAIMPString; Flags: LongWord;
      ErrorInfo: IAIMPErrorInfo; out Decoder: IAIMPAudioDecoder): HRESULT; stdcall;
  end;

  { IAIMPExtensionAudioDecoderPriority }

  IAIMPExtensionAudioDecoderPriority = interface(IUnknown)
  [SID_IAIMPExtensionAudioDecoderPriority]
    function GetPriority: Integer; stdcall;
  end;

  { IAIMPServiceAudioDecoders }

  IAIMPServiceAudioDecoders = interface
  [SID_IAIMPServiceAudioDecoders]
    function CreateDecoderForStream(Stream: IAIMPStream; Flags: LongWord;
      ErrorInfo: IAIMPErrorInfo; out Decoder: IAIMPAudioDecoder): HRESULT; stdcall;
    function CreateDecoderForFileURI(FileURI: IAIMPString; Flags: LongWord;
      ErrorInfo: IAIMPErrorInfo; out Decoder: IAIMPAudioDecoder): HRESULT; stdcall;
  end;

implementation

end.
