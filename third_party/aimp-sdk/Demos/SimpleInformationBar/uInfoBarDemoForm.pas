unit uInfoBarDemoForm;

interface

uses
  Winapi.Windows, Winapi.Messages, System.SysUtils, System.Variants, System.Classes, Vcl.Graphics,
  Vcl.Controls, Vcl.Forms, Vcl.Dialogs, Vcl.ExtCtrls, apiFileManager, Vcl.StdCtrls, apiObjects;

type
  TfrmCard = class(TForm)
    pmAlbumArt: TPaintBox;
    lbTitle: TLabel;
    lbAlbum: TLabel;
    lbArtist: TLabel;
    procedure pmAlbumArtPaint(Sender: TObject);
  private
    FAlbumArt: IAIMPImage;
  public
    procedure UpdateInfo(Info: IAIMPFileInfo);
  end;

implementation

uses
  apiWrappers;

{$R *.dfm}

procedure TfrmCard.pmAlbumArtPaint(Sender: TObject);
begin
  if FAlbumArt <> nil then
    FAlbumArt.Draw(pmAlbumArt.Canvas.Handle, pmAlbumArt.ClientRect, AIMP_IMAGE_DRAW_STRETCHMODE_FIT, nil);
end;

procedure TfrmCard.UpdateInfo(Info: IAIMPFileInfo);
begin
  lbAlbum.Caption := PropListGetStr(Info, AIMP_FILEINFO_PROPID_ALBUM);
  lbArtist.Caption := PropListGetStr(Info, AIMP_FILEINFO_PROPID_ARTIST);
  lbTitle.Caption := PropListGetStr(Info, AIMP_FILEINFO_PROPID_TITLE);
  if Info <> nil then
    Info.GetValueAsObject(AIMP_FILEINFO_PROPID_ALBUMART, IAIMPImage, FAlbumArt)
  else
    FAlbumArt := nil;

  pmAlbumArt.Invalidate;
end;

end.
