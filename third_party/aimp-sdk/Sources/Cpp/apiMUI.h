////////////////////////////////////////////////////////////////////////////////
//
//  Project:   AIMP
//             Programming Interface
//
//  Target:    v5.40 build 2650
//
//  Purpose:   MUI API
//
//  Author:    Artem Izmaylov
//             © 2006-2025
//             www.aimp.ru
//
#ifndef apiMUIH
#define apiMUIH

#include <unknwn.h>
#include "apiObjects.h"
#include "apiTypes.h"

static const GUID IID_IAIMPServiceMUI = {0x41494D50, 0x5372, 0x764D, 0x55, 0x49, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/* IAIMPServiceMUI */

class IAIMPServiceMUI: public IUnknown
{
	public:
 		virtual HRESULT WINAPI GetName(IAIMPString **Value) = 0;
		virtual HRESULT WINAPI GetValue(IAIMPString *KeyPath, IAIMPString **Value) = 0;
		virtual HRESULT WINAPI GetValuePart(IAIMPString *KeyPath, int Part, IAIMPString **Value) = 0;
};

#endif // !apiMUIH