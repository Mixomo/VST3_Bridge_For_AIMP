library aimp_infobar_demo;

uses
  apiPlugin,
  uInfoBarDemo in 'uInfoBarDemo.pas',
  uInfoBarDemoForm in 'uInfoBarDemoForm.pas' {frmCard};

{$R *.res}

function AIMPPluginGetHeader(out Header: IAIMPPlugin): HRESULT; stdcall;
begin
  try
    Header := TAIMPDemoPlugin.Create;
    Result := S_OK;
  except
    Result := E_UNEXPECTED;
  end;
end;

exports
  AIMPPluginGetHeader;
begin
end.
