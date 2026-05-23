object frmCard: TfrmCard
  Left = 0
  Top = 0
  BorderStyle = bsToolWindow
  Caption = 'Now Playing'
  ClientHeight = 122
  ClientWidth = 568
  Color = clBtnFace
  Font.Charset = DEFAULT_CHARSET
  Font.Color = clWindowText
  Font.Height = -12
  Font.Name = 'Segoe UI'
  Font.Style = []
  FormStyle = fsStayOnTop
  TextHeight = 15
  object pmAlbumArt: TPaintBox
    Left = 8
    Top = 8
    Width = 105
    Height = 105
    OnPaint = pmAlbumArtPaint
  end
  object lbTitle: TLabel
    Left = 119
    Top = 8
    Width = 32
    Height = 15
    Caption = 'lbTitle'
  end
  object lbAlbum: TLabel
    Left = 119
    Top = 29
    Width = 34
    Height = 15
    Caption = 'Label1'
  end
  object lbArtist: TLabel
    Left = 119
    Top = 50
    Width = 34
    Height = 15
    Caption = 'Label1'
  end
end
