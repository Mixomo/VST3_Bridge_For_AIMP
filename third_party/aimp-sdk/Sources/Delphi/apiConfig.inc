{$IFDEF FPC}
  {$mode delphi}
  {$codepage UTF8}
  {$inline off}
{$ELSE}
  {$IFNDEF UNICODE}
    {$MESSAGE FATAL 'You MUST use unicode version of Delphi to build this library!'}
  {$ENDIF}
{$ENDIF}
