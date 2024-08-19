#pragma once

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <windows.h>
#include <windows.media.h>
#include <winsdkver.h>
#include <systemmediatransportcontrolsinterop.h>
#include <atlbase.h>
#include <Windows.Foundation.h>
#include <wrl\wrappers\corewrappers.h>
#include <wrl\client.h>
#include <winstring.h>
#include <Windows.Media.h>

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Media;
using ISMTC = ABI::Windows::Media::ISystemMediaTransportControls;
using ISMTCDisplayUpdater = ABI::Windows::Media::ISystemMediaTransportControlsDisplayUpdater;