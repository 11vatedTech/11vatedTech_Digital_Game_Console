// guids.cpp — GUID definitions for the Windows host layer.
// MSVC requires exactly one definition TU per binary (MinGW headers self-define
// via INITGUID); keep MSVC builds explicit and version-controlled.
#include <initguid.h>

#include <dxgi.h>
#include <d3d12.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <guiddef.h>

// DXGIDE/D3D12 IIDs ship predefined in dxgi.lib/d3d12.lib; MMDevice CLSIDs/IIDs
// need local definition with initguid.h semantics.
