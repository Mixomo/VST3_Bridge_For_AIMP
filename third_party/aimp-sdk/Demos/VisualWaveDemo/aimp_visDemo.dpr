library aimp_visDemo;

uses
  apiPlugin,
  apiVisuals,
  aimp_visDemoMain in 'aimp_visDemoMain.pas';

{$R *.res}

  function AIMPPluginGetHeader(out Header: IAIMPPlugin): HRESULT; stdcall;
  begin
    try
      Header := TVisualPlugin.Create;
      Result := S_OK;
    except
      Result := E_UNEXPECTED;
    end;
  end;

exports
  AIMPPluginGetHeader;
begin
end.
