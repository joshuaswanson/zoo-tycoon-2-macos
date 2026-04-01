/*
 * D3D9 Proxy DLL for Zoo Tycoon 2
 *
 * Wraps d3d9_real.dll (DXVK) and spoofs GPU identity as NVIDIA
 * to pass Zoo Tycoon 2's GPU vendor check.
 */

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <string.h>

/* Real D3D9 */
typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT SDKVersion);
static PFN_Direct3DCreate9 real_Direct3DCreate9 = NULL;
static HMODULE real_d3d9 = NULL;

static char g_logPath[MAX_PATH] = {0};
static char g_dllDir[MAX_PATH] = {0};

static void InitLogPath(HINSTANCE hDll) {
    char *last;
    GetModuleFileNameA(hDll, g_dllDir, MAX_PATH);
    last = strrchr(g_dllDir, '\\');
    if (last) *(last + 1) = '\0';
    strcpy(g_logPath, g_dllDir);
    strcat(g_logPath, "d3d9_proxy.log");
}

static void DebugLog(const char *msg) {
    HANDLE f = CreateFileA(g_logPath[0] ? g_logPath : "C:\\d3d9_proxy.log",
        FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(f, msg, strlen(msg), &written, NULL);
        WriteFile(f, "\r\n", 2, &written, NULL);
        CloseHandle(f);
    }
}

static void EnsureRealLoaded(void) {
    char path[MAX_PATH];
    char *last_slash;

    if (real_d3d9) return;

    DebugLog("EnsureRealLoaded: starting");

    /* Get the directory of the current DLL/EXE */
    GetModuleFileNameA(NULL, path, MAX_PATH);
    last_slash = strrchr(path, '\\');
    if (last_slash) {
        *(last_slash + 1) = '\0';
        strcat(path, "d3d9_real.dll");
    } else {
        strcpy(path, "d3d9_real.dll");
    }

    DebugLog(path);
    real_d3d9 = LoadLibraryA(path);

    if (!real_d3d9) {
        DebugLog("ERROR: Failed to load d3d9_real.dll");
        return;
    }

    DebugLog("Loaded d3d9_real.dll successfully");
    real_Direct3DCreate9 = (PFN_Direct3DCreate9)GetProcAddress(real_d3d9, "Direct3DCreate9");
    if (!real_Direct3DCreate9) {
        DebugLog("ERROR: Direct3DCreate9 not found in d3d9_real.dll");
    } else {
        DebugLog("Got Direct3DCreate9 pointer");
    }
}

/* Wrapper struct */
typedef struct {
    IDirect3D9Vtbl *lpVtbl;
    IDirect3D9 *real;
    LONG refcount;
} WrappedD3D9;

#define W(iface) ((WrappedD3D9*)(iface))

/* === Wrapped methods === */

static HRESULT WINAPI W_QueryInterface(IDirect3D9 *s, REFIID riid, void **out) {
    HRESULT hr = IDirect3D9_QueryInterface(W(s)->real, riid, out);
    if (SUCCEEDED(hr)) *out = s;
    return hr;
}

static ULONG WINAPI W_AddRef(IDirect3D9 *s) {
    return InterlockedIncrement(&W(s)->refcount);
}

static ULONG WINAPI W_Release(IDirect3D9 *s) {
    ULONG ref = InterlockedDecrement(&W(s)->refcount);
    if (ref == 0) {
        IDirect3D9_Release(W(s)->real);
        HeapFree(GetProcessHeap(), 0, W(s));
    }
    return ref;
}

static HRESULT WINAPI W_RegisterSoftwareDevice(IDirect3D9 *s, void *init) {
    DebugLog("RegisterSoftwareDevice called");
    return IDirect3D9_RegisterSoftwareDevice(W(s)->real, init);
}

static UINT WINAPI W_GetAdapterCount(IDirect3D9 *s) {
    UINT count = IDirect3D9_GetAdapterCount(W(s)->real);
    {
        char buf[64];
        wsprintfA(buf, "GetAdapterCount() = %u", count);
        DebugLog(buf);
    }
    return count;
}

/* SPOOFED: Return NVIDIA GPU identity */
static HRESULT WINAPI W_GetAdapterIdentifier(IDirect3D9 *s, UINT a, DWORD f, D3DADAPTER_IDENTIFIER9 *id) {
    HRESULT hr = IDirect3D9_GetAdapterIdentifier(W(s)->real, a, f, id);
    DebugLog("GetAdapterIdentifier called");
    if (SUCCEEDED(hr)) {
        char buf[512];
        wsprintfA(buf, "  Original: vendor=0x%X device=0x%X desc=%s driver=%s whql=%u",
            id->VendorId, id->DeviceId, id->Description, id->Driver, id->WHQLLevel);
        DebugLog(buf);
        wsprintfA(buf, "  DriverVer: 0x%08X%08X subsys=0x%X rev=0x%X",
            id->DriverVersion.HighPart, id->DriverVersion.LowPart, id->SubSysId, id->Revision);
        DebugLog(buf);
        id->VendorId = 0x10DE;
        id->DeviceId = 0x0290;
        id->SubSysId = 0x04561043;
        id->Revision = 0xA1;
        strncpy(id->Driver, "nv4_disp.dll", sizeof(id->Driver) - 1);
        strncpy(id->Description, "NVIDIA GeForce 7900 GTX", sizeof(id->Description) - 1);
        /* Set a realistic NVIDIA driver version: 6.14.10.9371 */
        id->DriverVersion.HighPart = 0x0006000E;
        id->DriverVersion.LowPart = 0x000A249B;
        id->WHQLLevel = 1;
        wsprintfA(buf, "  Spoofed: vendor=0x%X device=0x%X desc=%s", id->VendorId, id->DeviceId, id->Description);
        DebugLog(buf);
    } else {
        char buf[64];
        wsprintfA(buf, "  GetAdapterIdentifier FAILED: 0x%X", hr);
        DebugLog(buf);
    }
    return hr;
}

/* Standard modes to inject for adapters that lack them */
typedef struct { UINT w, h, hz; } StdMode;
static const StdMode g_stdModes[] = {
    {640, 480, 60}, {640, 480, 75},
    {800, 600, 60}, {800, 600, 75},
    {1024, 768, 60}, {1024, 768, 75},
    {1152, 864, 60}, {1152, 864, 75},
    {1280, 720, 60},
    {1280, 960, 60},
    {1280, 1024, 60}, {1280, 1024, 75},
    {1600, 900, 60},
    {1600, 1200, 60},
    {1920, 1080, 60},
    {1920, 1200, 60},
};
#define NUM_STD_MODES (sizeof(g_stdModes) / sizeof(g_stdModes[0]))

/* Check if adapter needs mode injection (lacks standard modes) */
static int g_needsInjection[4] = {-1, -1, -1, -1}; /* -1 = unknown, 0 = no, 1 = yes */

static int NeedsInjection(IDirect3D9 *real, UINT a, D3DFORMAT f) {
    D3DDISPLAYMODE mode;
    UINT i, count;
    if (a >= 4) return 0;
    if (g_needsInjection[a] >= 0) return g_needsInjection[a];
    /* Check if 800x600 or 1024x768 exists */
    count = IDirect3D9_GetAdapterModeCount(real, a, f);
    for (i = 0; i < count; i++) {
        if (SUCCEEDED(IDirect3D9_EnumAdapterModes(real, a, f, i, &mode))) {
            if ((mode.Width == 800 && mode.Height == 600) ||
                (mode.Width == 1024 && mode.Height == 768)) {
                g_needsInjection[a] = 0;
                return 0;
            }
        }
    }
    g_needsInjection[a] = 1;
    DebugLog("MODE INJECTION: Adapter 0 lacks standard modes, injecting");
    return 1;
}

static UINT WINAPI W_GetAdapterModeCount(IDirect3D9 *s, UINT a, D3DFORMAT f) {
    UINT count = IDirect3D9_GetAdapterModeCount(W(s)->real, a, f);
    if ((f == 22 || f == 23) && NeedsInjection(W(s)->real, a, f)) {
        count += NUM_STD_MODES;
    }
    {
        char buf[128];
        wsprintfA(buf, "GetAdapterModeCount(a=%u, fmt=%d) = %u", a, (int)f, count);
        DebugLog(buf);
    }
    return count;
}

static HRESULT WINAPI W_EnumAdapterModes(IDirect3D9 *s, UINT a, D3DFORMAT f, UINT m, D3DDISPLAYMODE *d) {
    HRESULT hr;
    if ((f == 22 || f == 23) && NeedsInjection(W(s)->real, a, f) && m < NUM_STD_MODES) {
        /* Return injected standard mode */
        if (d) {
            d->Width = g_stdModes[m].w;
            d->Height = g_stdModes[m].h;
            d->RefreshRate = g_stdModes[m].hz;
            d->Format = f;
        }
        hr = D3D_OK;
    } else {
        UINT realIdx = m;
        if ((f == 22 || f == 23) && NeedsInjection(W(s)->real, a, f)) {
            realIdx = m - NUM_STD_MODES;
        }
        hr = IDirect3D9_EnumAdapterModes(W(s)->real, a, f, realIdx, d);
    }
    {
        char buf[192];
        if (SUCCEEDED(hr) && d) {
            wsprintfA(buf, "EnumAdapterModes(a=%u, fmt=%d, mode=%u) = OK: %ux%u@%uHz fmt=%d",
                a, (int)f, m, d->Width, d->Height, d->RefreshRate, (int)d->Format);
        } else {
            wsprintfA(buf, "EnumAdapterModes(a=%u, fmt=%d, mode=%u) = 0x%X", a, (int)f, m, hr);
        }
        DebugLog(buf);
    }
    return hr;
}

static HRESULT WINAPI W_GetAdapterDisplayMode(IDirect3D9 *s, UINT a, D3DDISPLAYMODE *d) {
    HRESULT hr = IDirect3D9_GetAdapterDisplayMode(W(s)->real, a, d);
    {
        char buf[128];
        if (SUCCEEDED(hr) && d) {
            wsprintfA(buf, "GetAdapterDisplayMode(a=%u) = OK: %ux%u@%uHz fmt=%d",
                a, d->Width, d->Height, d->RefreshRate, (int)d->Format);
        } else {
            wsprintfA(buf, "GetAdapterDisplayMode(a=%u) = 0x%X", a, hr);
        }
        DebugLog(buf);
    }
    return hr;
}

static HRESULT WINAPI W_CheckDeviceType(IDirect3D9 *s, UINT a, D3DDEVTYPE dt, D3DFORMAT af, D3DFORMAT bf, BOOL w) {
    HRESULT hr = IDirect3D9_CheckDeviceType(W(s)->real, a, dt, af, bf, w);
    char buf[128];
    wsprintfA(buf, "CheckDeviceType(a=%u,dt=%d,af=%d,bf=%d,w=%d) = 0x%X", a, dt, af, bf, w, hr);
    DebugLog(buf);
    return hr;
}

static HRESULT WINAPI W_CheckDeviceFormat(IDirect3D9 *s, UINT a, D3DDEVTYPE dt, D3DFORMAT af, DWORD u, D3DRESOURCETYPE rt, D3DFORMAT f) {
    HRESULT hr = IDirect3D9_CheckDeviceFormat(W(s)->real, a, dt, af, u, rt, f);
    /* Zoo Tycoon 2 requires certain format checks to pass for initialization.
       Force success for render target and depth stencil format checks. */
    if (FAILED(hr)) {
        hr = D3D_OK;
    }
    return hr;
}

static HRESULT WINAPI W_CheckDeviceMultiSampleType(IDirect3D9 *s, UINT a, D3DDEVTYPE dt, D3DFORMAT f, BOOL w, D3DMULTISAMPLE_TYPE mt, DWORD *q) {
    HRESULT hr = IDirect3D9_CheckDeviceMultiSampleType(W(s)->real, a, dt, f, w, mt, q);
    {
        char buf[128];
        wsprintfA(buf, "CheckDeviceMultiSampleType(a=%u,dt=%d,fmt=%d,w=%d,ms=%d) = 0x%X", a, dt, (int)f, w, (int)mt, hr);
        DebugLog(buf);
    }
    return hr;
}

static HRESULT WINAPI W_CheckDepthStencilMatch(IDirect3D9 *s, UINT a, D3DDEVTYPE dt, D3DFORMAT af, D3DFORMAT rf, D3DFORMAT df) {
    HRESULT hr = IDirect3D9_CheckDepthStencilMatch(W(s)->real, a, dt, af, rf, df);
    if (FAILED(hr)) {
        char buf[128];
        wsprintfA(buf, "CheckDepthStencilMatch FAIL: a=%u dt=%d af=%d rf=%d df=%d hr=0x%X", a, dt, af, rf, df, hr);
        DebugLog(buf);
        hr = D3D_OK; /* Force success */
    }
    return hr;
}

static HRESULT WINAPI W_CheckDeviceFormatConversion(IDirect3D9 *s, UINT a, D3DDEVTYPE dt, D3DFORMAT src, D3DFORMAT dst) {
    HRESULT hr = IDirect3D9_CheckDeviceFormatConversion(W(s)->real, a, dt, src, dst);
    {
        char buf[128];
        wsprintfA(buf, "CheckDeviceFormatConversion(a=%u,dt=%d,src=%d,dst=%d) = 0x%X", a, dt, (int)src, (int)dst, hr);
        DebugLog(buf);
    }
    if (FAILED(hr)) hr = D3D_OK;
    return hr;
}

/* SPOOFED: Ensure hardware T&L support */
static HRESULT WINAPI W_GetDeviceCaps(IDirect3D9 *s, UINT a, D3DDEVTYPE dt, D3DCAPS9 *caps) {
    HRESULT hr = IDirect3D9_GetDeviceCaps(W(s)->real, a, dt, caps);
    {
        char buf[256];
        wsprintfA(buf, "GetDeviceCaps(a=%u dt=%d) = 0x%X", a, dt, hr);
        DebugLog(buf);
        if (SUCCEEDED(hr)) {
            wsprintfA(buf, "  DevCaps=0x%X VS=%d.%d PS=%d.%d MaxTex=%ux%u",
                caps->DevCaps,
                (caps->VertexShaderVersion >> 8) & 0xFF, caps->VertexShaderVersion & 0xFF,
                (caps->PixelShaderVersion >> 8) & 0xFF, caps->PixelShaderVersion & 0xFF,
                caps->MaxTextureWidth, caps->MaxTextureHeight);
            DebugLog(buf);
        }
    }
    if (SUCCEEDED(hr)) {
        caps->DevCaps |= D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_PUREDEVICE;
        if (caps->VertexShaderVersion < D3DVS_VERSION(1,1))
            caps->VertexShaderVersion = D3DVS_VERSION(1,1);
        if (caps->PixelShaderVersion < D3DPS_VERSION(1,4))
            caps->PixelShaderVersion = D3DPS_VERSION(1,4);
        if (caps->MaxTextureWidth < 2048) caps->MaxTextureWidth = 2048;
        if (caps->MaxTextureHeight < 2048) caps->MaxTextureHeight = 2048;
    }
    return hr;
}

static HMONITOR WINAPI W_GetAdapterMonitor(IDirect3D9 *s, UINT a) {
    HMONITOR mon = IDirect3D9_GetAdapterMonitor(W(s)->real, a);
    {
        char buf[64];
        wsprintfA(buf, "GetAdapterMonitor(a=%u) = %p", a, mon);
        DebugLog(buf);
    }
    return mon;
}

/* ======== IDirect3DDevice9 Wrapper for tracing ======== */

typedef struct {
    IDirect3DDevice9Vtbl *lpVtbl;
    IDirect3DDevice9 *real;
    LONG refcount;
} WrappedDevice9;

#define WD(iface) ((WrappedDevice9*)(iface))

/* We'll use a vtable-copy approach: copy the real vtable, replace key methods */
static IDirect3DDevice9Vtbl wrapped_dev_vtbl;
static int dev_vtbl_initialized = 0;

/* Device method logging wrappers */
static HRESULT WINAPI WD_QueryInterface(IDirect3DDevice9 *s, REFIID riid, void **out) {
    return IDirect3DDevice9_QueryInterface(WD(s)->real, riid, out);
}
static ULONG WINAPI WD_AddRef(IDirect3DDevice9 *s) {
    return InterlockedIncrement(&WD(s)->refcount);
}
static ULONG WINAPI WD_Release(IDirect3DDevice9 *s) {
    ULONG ref = InterlockedDecrement(&WD(s)->refcount);
    if (ref == 0) {
        DebugLog("Device9::Release -> destroying wrapped device");
        IDirect3DDevice9_Release(WD(s)->real);
        HeapFree(GetProcessHeap(), 0, WD(s));
    }
    return ref;
}
static HRESULT WINAPI WD_TestCooperativeLevel(IDirect3DDevice9 *s) {
    HRESULT hr = IDirect3DDevice9_TestCooperativeLevel(WD(s)->real);
    { char buf[64]; wsprintfA(buf, "Device9::TestCooperativeLevel = 0x%X", hr); DebugLog(buf); }
    return hr;
}
static HRESULT WINAPI WD_GetDeviceCaps(IDirect3DDevice9 *s, D3DCAPS9 *caps) {
    HRESULT hr = IDirect3DDevice9_GetDeviceCaps(WD(s)->real, caps);
    {
        char buf[256];
        wsprintfA(buf, "Device9::GetDeviceCaps = 0x%X", hr);
        DebugLog(buf);
        if (SUCCEEDED(hr) && caps) {
            wsprintfA(buf, "  DevCaps=0x%X VS=%d.%d PS=%d.%d MaxTex=%ux%u Caps2=0x%X",
                caps->DevCaps,
                (caps->VertexShaderVersion >> 8) & 0xFF, caps->VertexShaderVersion & 0xFF,
                (caps->PixelShaderVersion >> 8) & 0xFF, caps->PixelShaderVersion & 0xFF,
                caps->MaxTextureWidth, caps->MaxTextureHeight, caps->Caps2);
            DebugLog(buf);
            /* Spoof caps on device too */
            caps->DevCaps |= D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_PUREDEVICE;
            if (caps->VertexShaderVersion < D3DVS_VERSION(1,1))
                caps->VertexShaderVersion = D3DVS_VERSION(1,1);
            if (caps->PixelShaderVersion < D3DPS_VERSION(1,4))
                caps->PixelShaderVersion = D3DPS_VERSION(1,4);
            if (caps->MaxTextureWidth < 2048) caps->MaxTextureWidth = 2048;
            if (caps->MaxTextureHeight < 2048) caps->MaxTextureHeight = 2048;
        }
    }
    return hr;
}
static HRESULT WINAPI WD_GetDisplayMode(IDirect3DDevice9 *s, UINT sw, D3DDISPLAYMODE *m) {
    HRESULT hr = IDirect3DDevice9_GetDisplayMode(WD(s)->real, sw, m);
    {
        char buf[128];
        if (SUCCEEDED(hr) && m) {
            wsprintfA(buf, "Device9::GetDisplayMode(sw=%u) = OK: %ux%u@%uHz fmt=%d", sw, m->Width, m->Height, m->RefreshRate, (int)m->Format);
        } else {
            wsprintfA(buf, "Device9::GetDisplayMode(sw=%u) = 0x%X", sw, hr);
        }
        DebugLog(buf);
    }
    return hr;
}
static HRESULT WINAPI WD_Clear(IDirect3DDevice9 *s, DWORD c, const D3DRECT *r, DWORD f, D3DCOLOR col, float z, DWORD st) {
    HRESULT hr = IDirect3DDevice9_Clear(WD(s)->real, c, r, f, col, z, st);
    { char buf[128]; wsprintfA(buf, "Device9::Clear(count=%u,flags=0x%X,color=0x%08X) = 0x%X", c, f, col, hr); DebugLog(buf); }
    return hr;
}
static HRESULT WINAPI WD_Present(IDirect3DDevice9 *s, const RECT *sr, const RECT *dr, HWND hw, const RGNDATA *dd) {
    HRESULT hr = IDirect3DDevice9_Present(WD(s)->real, sr, dr, hw, dd);
    { char buf[64]; wsprintfA(buf, "Device9::Present = 0x%X", hr); DebugLog(buf); }
    return hr;
}
static HRESULT WINAPI WD_Reset(IDirect3DDevice9 *s, D3DPRESENT_PARAMETERS *pp) {
    HRESULT hr = IDirect3DDevice9_Reset(WD(s)->real, pp);
    { char buf[64]; wsprintfA(buf, "Device9::Reset = 0x%X", hr); DebugLog(buf); }
    return hr;
}
static HRESULT WINAPI WD_GetBackBuffer(IDirect3DDevice9 *s, UINT sw, UINT bb, D3DBACKBUFFER_TYPE t, IDirect3DSurface9 **surf) {
    HRESULT hr = IDirect3DDevice9_GetBackBuffer(WD(s)->real, sw, bb, t, surf);
    { char buf[128]; wsprintfA(buf, "Device9::GetBackBuffer(sw=%u,bb=%u,t=%d) = 0x%X surf=%p", sw, bb, t, hr, surf ? *surf : NULL); DebugLog(buf); }
    return hr;
}
static HRESULT WINAPI WD_CreateVertexShader(IDirect3DDevice9 *s, const DWORD *func, IDirect3DVertexShader9 **sh) {
    HRESULT hr = IDirect3DDevice9_CreateVertexShader(WD(s)->real, func, sh);
    { char buf[64]; wsprintfA(buf, "Device9::CreateVertexShader = 0x%X", hr); DebugLog(buf); }
    return hr;
}
static HRESULT WINAPI WD_CreatePixelShader(IDirect3DDevice9 *s, const DWORD *func, IDirect3DPixelShader9 **sh) {
    HRESULT hr = IDirect3DDevice9_CreatePixelShader(WD(s)->real, func, sh);
    { char buf[64]; wsprintfA(buf, "Device9::CreatePixelShader = 0x%X", hr); DebugLog(buf); }
    return hr;
}
static HRESULT WINAPI WD_CreateTexture(IDirect3DDevice9 *s, UINT w, UINT h, UINT lvl, DWORD use, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture9 **tex, HANDLE *sh) {
    HRESULT hr = IDirect3DDevice9_CreateTexture(WD(s)->real, w, h, lvl, use, fmt, pool, tex, sh);
    { char buf[128]; wsprintfA(buf, "Device9::CreateTexture(%ux%u,lvl=%u,use=0x%X,fmt=%d,pool=%d) = 0x%X", w, h, lvl, use, (int)fmt, (int)pool, hr); DebugLog(buf); }
    return hr;
}
static HRESULT WINAPI WD_CreateRenderTarget(IDirect3DDevice9 *s, UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms, DWORD msq, BOOL lock, IDirect3DSurface9 **surf, HANDLE *sh) {
    HRESULT hr = IDirect3DDevice9_CreateRenderTarget(WD(s)->real, w, h, fmt, ms, msq, lock, surf, sh);
    { char buf[128]; wsprintfA(buf, "Device9::CreateRenderTarget(%ux%u,fmt=%d,ms=%d) = 0x%X", w, h, (int)fmt, (int)ms, hr); DebugLog(buf); }
    return hr;
}
static HRESULT WINAPI WD_CreateDepthStencilSurface(IDirect3DDevice9 *s, UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms, DWORD msq, BOOL dis, IDirect3DSurface9 **surf, HANDLE *sh) {
    HRESULT hr = IDirect3DDevice9_CreateDepthStencilSurface(WD(s)->real, w, h, fmt, ms, msq, dis, surf, sh);
    { char buf[128]; wsprintfA(buf, "Device9::CreateDepthStencilSurface(%ux%u,fmt=%d,ms=%d) = 0x%X", w, h, (int)fmt, (int)ms, hr); DebugLog(buf); }
    return hr;
}
static HRESULT WINAPI WD_SetRenderState(IDirect3DDevice9 *s, D3DRENDERSTATETYPE st, DWORD val) {
    HRESULT hr = IDirect3DDevice9_SetRenderState(WD(s)->real, st, val);
    /* Only log failures - render states are called too frequently */
    if (FAILED(hr)) {
        char buf[96]; wsprintfA(buf, "Device9::SetRenderState(%d, 0x%X) FAILED = 0x%X", (int)st, val, hr); DebugLog(buf);
    }
    return hr;
}
static HRESULT WINAPI WD_BeginScene(IDirect3DDevice9 *s) {
    HRESULT hr = IDirect3DDevice9_BeginScene(WD(s)->real);
    { char buf[64]; wsprintfA(buf, "Device9::BeginScene = 0x%X", hr); DebugLog(buf); }
    return hr;
}
static HRESULT WINAPI WD_EndScene(IDirect3DDevice9 *s) {
    HRESULT hr = IDirect3DDevice9_EndScene(WD(s)->real);
    { char buf[64]; wsprintfA(buf, "Device9::EndScene = 0x%X", hr); DebugLog(buf); }
    return hr;
}
static HRESULT WINAPI WD_SetTexture(IDirect3DDevice9 *s, DWORD stage, IDirect3DBaseTexture9 *tex) {
    HRESULT hr = IDirect3DDevice9_SetTexture(WD(s)->real, stage, tex);
    if (FAILED(hr)) {
        char buf[96]; wsprintfA(buf, "Device9::SetTexture(%u) FAILED = 0x%X", stage, hr); DebugLog(buf);
    }
    return hr;
}

static IDirect3DDevice9* WrapDevice9(IDirect3DDevice9 *real) {
    WrappedDevice9 *wd;
    if (!real) return NULL;

    wd = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*wd));
    if (!wd) return real;

    /* Copy the real vtable as baseline, then override key methods */
    if (!dev_vtbl_initialized) {
        memcpy(&wrapped_dev_vtbl, real->lpVtbl, sizeof(IDirect3DDevice9Vtbl));
        wrapped_dev_vtbl.QueryInterface = WD_QueryInterface;
        wrapped_dev_vtbl.AddRef = WD_AddRef;
        wrapped_dev_vtbl.Release = WD_Release;
        wrapped_dev_vtbl.TestCooperativeLevel = WD_TestCooperativeLevel;
        wrapped_dev_vtbl.GetDeviceCaps = WD_GetDeviceCaps;
        wrapped_dev_vtbl.GetDisplayMode = WD_GetDisplayMode;
        wrapped_dev_vtbl.Clear = WD_Clear;
        wrapped_dev_vtbl.Present = WD_Present;
        wrapped_dev_vtbl.Reset = WD_Reset;
        wrapped_dev_vtbl.GetBackBuffer = WD_GetBackBuffer;
        wrapped_dev_vtbl.CreateVertexShader = WD_CreateVertexShader;
        wrapped_dev_vtbl.CreatePixelShader = WD_CreatePixelShader;
        wrapped_dev_vtbl.CreateTexture = WD_CreateTexture;
        wrapped_dev_vtbl.CreateRenderTarget = WD_CreateRenderTarget;
        wrapped_dev_vtbl.CreateDepthStencilSurface = WD_CreateDepthStencilSurface;
        wrapped_dev_vtbl.SetRenderState = WD_SetRenderState;
        wrapped_dev_vtbl.BeginScene = WD_BeginScene;
        wrapped_dev_vtbl.EndScene = WD_EndScene;
        wrapped_dev_vtbl.SetTexture = WD_SetTexture;
        dev_vtbl_initialized = 1;
    }

    wd->lpVtbl = &wrapped_dev_vtbl;
    wd->real = real;
    wd->refcount = 1;
    DebugLog("Wrapped IDirect3DDevice9 with tracing");
    return (IDirect3DDevice9*)wd;
}

/* Device vtable hooks for window resize scaling */
static IDirect3DDevice9Vtbl *g_origDevVtbl = NULL;
static IDirect3DDevice9Vtbl g_hookedDevVtbl;
static IDirect3DDevice9 *g_device = NULL;
static HWND g_deviceHwnd = NULL;
static UINT g_bbWidth = 0, g_bbHeight = 0;
static WNDPROC g_origWndProc = NULL;

/* Window proc subclass to lock 4:3 aspect ratio during resize */
static LRESULT CALLBACK AspectWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_SIZING && lp) {
        RECT *r = (RECT*)lp;
        LONG w = r->right - r->left;
        LONG h = r->bottom - r->top;
        /* Get border sizes */
        LONG style = GetWindowLongA(hwnd, GWL_STYLE);
        RECT borders = {0, 0, 0, 0};
        AdjustWindowRect(&borders, style, FALSE);
        LONG bw = (borders.right - borders.left);
        LONG bh = (borders.bottom - borders.top);
        LONG clientW = w - bw;
        LONG clientH = h - bh;
        /* Enforce 4:3 based on which edge is being dragged */
        LONG newClientH = (clientW * 3) / 4;
        LONG newH = newClientH + bh;
        /* Adjust bottom edge to match */
        if (wp == WMSZ_LEFT || wp == WMSZ_RIGHT || wp == WMSZ_TOPLEFT || wp == WMSZ_TOPRIGHT ||
            wp == WMSZ_BOTTOMLEFT || wp == WMSZ_BOTTOMRIGHT) {
            if (wp == WMSZ_TOPLEFT || wp == WMSZ_TOPRIGHT)
                r->top = r->bottom - newH;
            else
                r->bottom = r->top + newH;
        } else {
            /* Dragging top or bottom edge: compute width from height */
            LONG newClientW = (clientH * 4) / 3;
            LONG newW = newClientW + bw;
            r->right = r->left + newW;
        }
        return TRUE;
    }
    if (msg == WM_SIZE && g_device) {
        UINT newW = LOWORD(lp);
        UINT newH = HIWORD(lp);
        if (newW > 0 && newH > 0 && (newW != g_bbWidth || newH != g_bbHeight)) {
            /* Try to Reset the device to the new size */
            D3DPRESENT_PARAMETERS pp;
            memset(&pp, 0, sizeof(pp));
            pp.BackBufferWidth = newW;
            pp.BackBufferHeight = newH;
            pp.BackBufferFormat = D3DFMT_UNKNOWN;
            pp.BackBufferCount = 1;
            pp.SwapEffect = 1; /* D3DSWAPEFFECT_DISCARD */
            pp.Windowed = TRUE;
            pp.EnableAutoDepthStencil = TRUE;
            pp.AutoDepthStencilFormat = 75; /* D3DFMT_D24S8 */
            pp.PresentationInterval = 1;
            {
                HRESULT hr = g_origDevVtbl->Reset(g_device, &pp);
                if (SUCCEEDED(hr)) {
                    g_bbWidth = newW;
                    g_bbHeight = newH;
                }
            }
        }
    }
    return CallWindowProcA(g_origWndProc, hwnd, msg, wp, lp);
}

static HRESULT WINAPI Hooked_Present(IDirect3DDevice9 *dev, const RECT *src, const RECT *dst, HWND hwOver, const RGNDATA *rgn) {
    return g_origDevVtbl->Present(dev, src, dst, hwOver, rgn);
}

/* Clamp SetViewport to backbuffer size to prevent zoom when window is larger */
static HRESULT WINAPI Hooked_SetViewport(IDirect3DDevice9 *dev, const D3DVIEWPORT9 *vp) {
    if (vp && g_bbWidth > 0 && g_bbHeight > 0) {
        D3DVIEWPORT9 clamped = *vp;
        if (clamped.Width > g_bbWidth) clamped.Width = g_bbWidth;
        if (clamped.Height > g_bbHeight) clamped.Height = g_bbHeight;
        return g_origDevVtbl->SetViewport(dev, &clamped);
    }
    return g_origDevVtbl->SetViewport(dev, vp);
}

/* Clamp SetScissorRect to backbuffer size */
static HRESULT WINAPI Hooked_SetScissorRect(IDirect3DDevice9 *dev, const RECT *r) {
    if (r && g_bbWidth > 0 && g_bbHeight > 0) {
        RECT clamped = *r;
        if (clamped.right > (LONG)g_bbWidth) clamped.right = g_bbWidth;
        if (clamped.bottom > (LONG)g_bbHeight) clamped.bottom = g_bbHeight;
        return g_origDevVtbl->SetScissorRect(dev, &clamped);
    }
    return g_origDevVtbl->SetScissorRect(dev, r);
}

static HRESULT WINAPI Hooked_Reset(IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pp) {
    if (pp) {
        pp->Windowed = TRUE;
        pp->BackBufferFormat = D3DFMT_UNKNOWN;
        pp->FullScreen_RefreshRateInHz = 0;
        if (pp->EnableAutoDepthStencil && pp->AutoDepthStencilFormat == 71)
            pp->AutoDepthStencilFormat = 75;
    }
    {
        HRESULT hr = g_origDevVtbl->Reset(dev, pp);
        if (SUCCEEDED(hr) && pp) {
            g_bbWidth = pp->BackBufferWidth;
            g_bbHeight = pp->BackBufferHeight;
            { char buf[64]; wsprintfA(buf, "Reset OK: %ux%u", g_bbWidth, g_bbHeight); DebugLog(buf); }
        }
        return hr;
    }
}

static HRESULT WINAPI W_CreateDevice(IDirect3D9 *s, UINT a, D3DDEVTYPE dt, HWND hw, DWORD fl, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **dev) {
    HRESULT hr;
    char buf[256];
    DebugLog("CreateDevice called!");
    wsprintfA(buf, "  adapter=%u devtype=%d flags=0x%X hwnd=%p", a, dt, fl, hw);
    DebugLog(buf);
    if (pp) {
        wsprintfA(buf, "  PP: %ux%u fmt=%d bbcount=%u swap=%d windowed=%d",
            pp->BackBufferWidth, pp->BackBufferHeight, (int)pp->BackBufferFormat,
            pp->BackBufferCount, (int)pp->SwapEffect, pp->Windowed);
        DebugLog(buf);
        wsprintfA(buf, "  PP: autoDepth=%d depthFmt=%d flags=0x%X refresh=%u interval=%u",
            pp->EnableAutoDepthStencil, (int)pp->AutoDepthStencilFormat,
            pp->Flags, pp->FullScreen_RefreshRateInHz, pp->PresentationInterval);
        DebugLog(buf);
    }

    /* Remove PUREDEVICE flag if real device doesn't support it */
    if (fl & D3DCREATE_PUREDEVICE) {
        D3DCAPS9 caps;
        if (SUCCEEDED(IDirect3D9_GetDeviceCaps(W(s)->real, a, dt, &caps))) {
            if (!(caps.DevCaps & D3DDEVCAPS_PUREDEVICE)) {
                fl &= ~D3DCREATE_PUREDEVICE;
                DebugLog("  Removed PUREDEVICE flag (not supported)");
            }
        }
    }
    /* Force windowed mode at a good resolution */
    if (pp && !pp->Windowed) {
        pp->Windowed = TRUE;
        pp->FullScreen_RefreshRateInHz = 0;
        pp->BackBufferFormat = D3DFMT_UNKNOWN;
        /* Render at 1024x768 (4:3, crisp on most displays) */
        
        
        DebugLog("  Forced windowed mode");
    }
    /* Fix depth format: D32(71) → D24S8(75) */
    if (pp && pp->EnableAutoDepthStencil && pp->AutoDepthStencilFormat == 71) {
        pp->AutoDepthStencilFormat = 75; /* D24S8 */
        DebugLog("  Changed depth D32 -> D24S8");
    }
    hr = IDirect3D9_CreateDevice(W(s)->real, a, dt, hw, fl, pp, dev);
    wsprintfA(buf, "  CreateDevice result: 0x%X dev=%p", hr, (dev && SUCCEEDED(hr)) ? *dev : NULL);
    DebugLog(buf);

    if (FAILED(hr) && (fl & D3DCREATE_HARDWARE_VERTEXPROCESSING)) {
        DebugLog("  Retrying with SOFTWARE_VERTEXPROCESSING");
        fl = (fl & ~D3DCREATE_HARDWARE_VERTEXPROCESSING) | D3DCREATE_SOFTWARE_VERTEXPROCESSING;
        hr = IDirect3D9_CreateDevice(W(s)->real, a, dt, hw, fl, pp, dev);
        wsprintfA(buf, "  Retry SW result: 0x%X", hr);
        DebugLog(buf);
    }

    /* If windowed creation failed, try different depth formats */
    if (FAILED(hr) && pp && pp->Windowed) {
        D3DFORMAT depthFmts[] = {75, 77, 80, 0};
        int di;
        for (di = 0; depthFmts[di] && FAILED(hr); di++) {
            pp->AutoDepthStencilFormat = depthFmts[di];
            hr = IDirect3D9_CreateDevice(W(s)->real, a, dt, hw, fl, pp, dev);
            wsprintfA(buf, "  Retry depth=%d: 0x%X", (int)depthFmts[di], hr);
            DebugLog(buf);
        }
    }

    if (SUCCEEDED(hr) && pp && pp->Windowed && hw) {
        /* Add title bar, window controls, and RESIZABLE border */
        LONG style = GetWindowLongA(hw, GWL_STYLE);
        style |= WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;
        style &= ~WS_POPUP;
        SetWindowLongA(hw, GWL_STYLE, style);
        {
            RECT rc = {0, 0, (LONG)pp->BackBufferWidth, (LONG)pp->BackBufferHeight};
            AdjustWindowRect(&rc, style, FALSE);
            SetWindowPos(hw, HWND_TOP, 50, 50,
                rc.right - rc.left, rc.bottom - rc.top,
                SWP_SHOWWINDOW | SWP_FRAMECHANGED);
        }
        ShowWindow(hw, SW_SHOW);

        /* Hook device + window for resize support */
        if (dev && *dev) {
            IDirect3DDevice9 *device = *dev;
            g_device = device;
            g_origDevVtbl = (IDirect3DDevice9Vtbl*)device->lpVtbl;
            g_deviceHwnd = hw;
            g_bbWidth = pp->BackBufferWidth;
            g_bbHeight = pp->BackBufferHeight;
            /* Hook vtable */
            memcpy(&g_hookedDevVtbl, g_origDevVtbl, sizeof(g_hookedDevVtbl));
            g_hookedDevVtbl.Present = Hooked_Present;
            g_hookedDevVtbl.Reset = Hooked_Reset;
            g_hookedDevVtbl.SetViewport = Hooked_SetViewport;
            g_hookedDevVtbl.SetScissorRect = Hooked_SetScissorRect;
            *(void**)device = &g_hookedDevVtbl;
            /* Subclass window for smooth 4:3 aspect ratio locking */
            if (hw) {
                g_origWndProc = (WNDPROC)SetWindowLongA(hw, GWL_WNDPROC, (LONG)AspectWndProc);
                DebugLog("  Subclassed window for 4:3 aspect lock + resize");
            }
        }
    }

    /* Device wrapper disabled - vtable copy breaks non-overridden methods.
       The real device is returned directly. */
    if (SUCCEEDED(hr) && dev && *dev) {
        DebugLog("CreateDevice SUCCEEDED! Returning real device (no wrapper).");
    }
    return hr;
}

static IDirect3D9Vtbl wrapped_vtbl = {
    W_QueryInterface, W_AddRef, W_Release,
    W_RegisterSoftwareDevice, W_GetAdapterCount,
    W_GetAdapterIdentifier, W_GetAdapterModeCount,
    W_EnumAdapterModes, W_GetAdapterDisplayMode,
    W_CheckDeviceType, W_CheckDeviceFormat,
    W_CheckDeviceMultiSampleType, W_CheckDepthStencilMatch,
    W_CheckDeviceFormatConversion, W_GetDeviceCaps,
    W_GetAdapterMonitor, W_CreateDevice,
};

/* The exported function */
__declspec(dllexport) IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion) {
    WrappedD3D9 *w;
    IDirect3D9 *real;

    EnsureRealLoaded();
    if (!real_Direct3DCreate9) return NULL;

    real = real_Direct3DCreate9(SDKVersion);
    if (!real) return NULL;

    w = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*w));
    if (!w) { IDirect3D9_Release(real); return NULL; }

    w->lpVtbl = &wrapped_vtbl;
    w->real = real;
    w->refcount = 1;

    /* Runtime binary patches - run ONCE on first Direct3DCreate9 call */
    {
        static int patchesApplied = 0;
        HMODULE gameModule = GetModuleHandleA(NULL);
        if (gameModule && !patchesApplied) {
            patchesApplied = 1;
            BYTE *base = (BYTE*)gameModule;
            IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER*)base;
            IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
            IMAGE_SECTION_HEADER *sections = (IMAGE_SECTION_HEADER*)((BYTE*)nt + sizeof(*nt));
            DWORD i;
            char tbuf[256];

            DebugLog("=== TARGETED PATCHES + Mode injection ===");

            /* Dump vidCheck ORIGINAL bytes for analysis */
            {
                BYTE *vidCheck = base + (0x7F1AF6 - 0x400000);
                wsprintfA(tbuf, "Video check at %p: %02X %02X %02X %02X %02X",
                    vidCheck, vidCheck[0], vidCheck[1], vidCheck[2], vidCheck[3], vidCheck[4]);
                DebugLog(tbuf);

                /* Dump 4KB of ORIGINAL function bytes */
                {
                    char dumpPath[MAX_PATH];
                    strcpy(dumpPath, g_dllDir);
                    strcat(dumpPath, "vidcheck_original.txt");
                    HANDLE hDump = CreateFileA(dumpPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
                    if (hDump != INVALID_HANDLE_VALUE) {
                        char hexline[128];
                        DWORD written;
                        int di;
                        for (di = 0; di < 4096; di += 16) {
                            int p = wsprintfA(hexline, "%08X:", 0x7F1AF6 + di);
                            int dj;
                            for (dj = 0; dj < 16; dj++)
                                p += wsprintfA(hexline + p, " %02X", vidCheck[di+dj]);
                            p += wsprintfA(hexline + p, "\r\n");
                            WriteFile(hDump, hexline, p, &written, NULL);
                        }
                        CloseHandle(hDump);
                        DebugLog("Dumped vidCheck to vidcheck_original.txt");
                    }
                }

                /* Also dump the renderer vtable at 0x907338 */
                {
                    DWORD *vtable = (DWORD*)(base + (0x907338 - 0x400000));
                    int vi;
                    DebugLog("=== Renderer vtable at 0x907338 ===");
                    for (vi = 0; vi < 20; vi++) {
                        wsprintfA(tbuf, "  vtable[%d] (offset 0x%X) = 0x%08X", vi, vi*4, vtable[vi]);
                        DebugLog(tbuf);
                    }
                }

                /* Dump the format name table at 0x919D50 (34 entries of string pointers) */
                {
                    DWORD *fmtTable = (DWORD*)(base + (0x919D50 - 0x400000));
                    int fi;
                    DebugLog("=== Format name table at 0x919D50 ===");
                    for (fi = 0; fi < 34; fi++) {
                        char *fmtStr = (char*)(DWORD_PTR)fmtTable[fi];
                        if (fmtStr && fmtStr > (char*)base && fmtStr < (char*)base + nt->OptionalHeader.SizeOfImage) {
                            wsprintfA(tbuf, "  fmt[%d] = 0x%08X -> \"%s\"", fi, fmtTable[fi], fmtStr);
                        } else {
                            wsprintfA(tbuf, "  fmt[%d] = 0x%08X (bad ptr)", fi, fmtTable[fi]);
                        }
                        DebugLog(tbuf);
                    }
                }

                /* Dump format comparison strings at 0x906F84, 0x906F90, etc. */
                {
                    DWORD fmtAddrs[] = {0x906F04, 0x906F84, 0x906F90, 0x906F98, 0x906FA4, 0x906FAC, 0x906FB8, 0};
                    int fi;
                    DebugLog("=== Format comparison strings ===");
                    for (fi = 0; fmtAddrs[fi]; fi++) {
                        char *str = (char*)(base + (fmtAddrs[fi] - 0x400000));
                        wsprintfA(tbuf, "  0x%08X = \"%s\"", fmtAddrs[fi], str);
                        DebugLog(tbuf);
                    }
                }
            }

            /* === TARGETED PATCHES === */
            /* Patch 1: vidCheck JBE at 007F1E13 - the renderer count check.
               vidCheck runs normally (queries D3D9, creates renderer object, initializes globals)
               but this final check on vtable[7] count fails. NOP the JBE so execution
               falls through instead of jumping to the destructor+return-0 path.
               Also set the count to 1 so the mode processing loop runs once. */
            {
                BYTE *jbe = base + (0x007F1E13 - 0x400000);
                char pbuf[128];
                /* Verify the bytes are: 0F 86 xx xx xx xx (JBE rel32) */
                if (jbe[0] == 0x0F && jbe[1] == 0x86) {
                    DWORD oldProt;
                    wsprintfA(pbuf, "PATCH vidCheck JBE at %p: %02X %02X -> NOP", jbe, jbe[0], jbe[1]);
                    DebugLog(pbuf);
                    VirtualProtect(jbe - 2, 8, PAGE_EXECUTE_READWRITE, &oldProt);
                    /* Replace: test eax,eax; JBE rel32 (8 bytes total at 007F1E11)
                       With:    mov eax, 1; NOP; NOP; NOP (force count = 1) */
                    jbe[-2] = 0xB8; /* mov eax, imm32 */
                    jbe[-1] = 0x01;
                    jbe[0]  = 0x00;
                    jbe[1]  = 0x00;
                    jbe[2]  = 0x00;
                    jbe[3]  = 0x90; /* NOP */
                    jbe[4]  = 0x90; /* NOP */
                    jbe[5]  = 0x90; /* NOP */
                    VirtualProtect(jbe - 2, 8, oldProt, &oldProt);
                    DebugLog("  -> mov eax,1; NOP NOP NOP (force 1 mode)");
                    /* Also need to update [ebp-30] which stores the count */
                    /* The mov [ebp-30],eax at 007F1E09 already ran with the real count,
                       so we need to patch that too to store 1 */
                    {
                        BYTE *movCount = base + (0x007F1E09 - 0x400000);
                        /* Original: 89 45 D0 = mov [ebp-30], eax
                           Replace with: C7 45 D0 01 00 00 00 = mov [ebp-30], 1
                           But that's 7 bytes and we only have 3. Instead, let's patch
                           AFTER our mov eax,1: it will store eax(=1) to [ebp-30] naturally
                           since the original mov [ebp-30], eax at 007F1E09 is BEFORE our patch */
                    }
                } else {
                    wsprintfA(pbuf, "WARNING: vidCheck JBE not found at %p: %02X %02X",
                        jbe, jbe[0], jbe[1]);
                    DebugLog(pbuf);
                }
            }

            /* Patch 2: ALL E_FAIL returns in vidCheck function.
               Scan the 4KB vidCheck function for all 'B8 05 40 00 80' (mov eax, 0x80004005)
               and change them to 'B8 00 00 00 00' (mov eax, 0 = S_OK). */
            {
                BYTE *vidFunc = base + (0x7F1AF6 - 0x400000);
                int patchCount = 0;
                DWORD fi;
                for (fi = 0; fi < 4096 - 5; fi++) {
                    if (vidFunc[fi] == 0xB8 && vidFunc[fi+1] == 0x05 &&
                        vidFunc[fi+2] == 0x40 && vidFunc[fi+3] == 0x00 &&
                        vidFunc[fi+4] == 0x80) {
                        DWORD oldProt;
                        char pbuf2[128];
                        wsprintfA(pbuf2, "PATCH E_FAIL at 0x%08X -> S_OK",
                            0x7F1AF6 + fi);
                        DebugLog(pbuf2);
                        VirtualProtect(vidFunc + fi, 5, PAGE_EXECUTE_READWRITE, &oldProt);
                        vidFunc[fi+1] = 0x00; /* change 0x80004005 to 0x00000000 */
                        vidFunc[fi+2] = 0x00;
                        vidFunc[fi+3] = 0x00;
                        vidFunc[fi+4] = 0x00;
                        VirtualProtect(vidFunc + fi, 5, oldProt, &oldProt);
                        patchCount++;
                    }
                }
                {
                    char pbuf2[64];
                    wsprintfA(pbuf2, "Patched %d E_FAIL returns in vidCheck", patchCount);
                    DebugLog(pbuf2);
                }
            }

            /* Patch 3: Force pre-factory and post-vidCheck JGEs to succeed */
            {
                /* 005BBC67: JGE after capCheck → force to JMP */
                BYTE *jge1 = base + (0x005BBC67 - 0x400000);
                if (jge1[0] == 0x7D) {
                    DWORD oldProt;
                    DebugLog("PATCH JGE at 005BBC67 (capCheck) -> JMP");
                    VirtualProtect(jge1, 1, PAGE_EXECUTE_READWRITE, &oldProt);
                    jge1[0] = 0xEB;
                    VirtualProtect(jge1, 1, oldProt, &oldProt);
                }

                /* 005BBCBF: JGE after vidCheck → force to JMP */
                {
                    BYTE *jge2 = base + (0x005BBCBF - 0x400000);
                    if (jge2[0] == 0x7D) {
                        DWORD oldProt;
                        DebugLog("PATCH JGE at 005BBCBF (vidCheck) -> JMP");
                        VirtualProtect(jge2, 1, PAGE_EXECUTE_READWRITE, &oldProt);
                        jge2[0] = 0xEB;
                        VirtualProtect(jge2, 1, oldProt, &oldProt);
                    }
                }

                /* 005BBCD1: JGE after renderer creation (005BBA69) → force to JMP */
                {
                    BYTE *jge3 = base + (0x005BBCD1 - 0x400000);
                    if (jge3[0] == 0x7D) {
                        DWORD oldProt;
                        DebugLog("PATCH JGE at 005BBCD1 (renderer create) -> JMP");
                        VirtualProtect(jge3, 1, PAGE_EXECUTE_READWRITE, &oldProt);
                        jge3[0] = 0xEB;
                        VirtualProtect(jge3, 1, oldProt, &oldProt);
                    }
                }

                /* 005BBE90: JE for bit-flag check - NOT patched (it jumps past renderer creation) */
                DebugLog("Skipping JE at 005BBE90 (would skip renderer creation)");

            /* Patch 4: JNZ at 005BBD38 -> NOP (prevent jump to error handler)
               NOTE: This code path may have already executed before Direct3DCreate9.
               Only apply if the bytes match (they won't if already patched/executed). */
            {
                BYTE *jnz = base + (0x005BBD38 - 0x400000);
                if (jnz[0] == 0x0F && jnz[1] == 0x85) {
                    DWORD oldProt;
                    DebugLog("PATCH: JNZ at 005BBD38 -> NOP (skip error jump)");
                    VirtualProtect(jnz, 6, PAGE_EXECUTE_READWRITE, &oldProt);
                    jnz[0]=0x90; jnz[1]=0x90; jnz[2]=0x90;
                    jnz[3]=0x90; jnz[4]=0x90; jnz[5]=0x90;
                    VirtualProtect(jnz, 6, oldProt, &oldProt);
                }
            }

            /* Patch 5: Error string scanner - find PUSH instructions referencing
               error strings and patch their guard Jcc instructions */
            {
                BYTE *imgBase = base;
                DWORD imageSize = nt->OptionalHeader.SizeOfImage;
                const char *needles[] = {
                    "error:d3d_create_failed",
                    "error:unsupported_graphics_adapter",
                    "error:prototype_graphics_adapter",
                    "error:invalid_graphics_driver",
                    "error:old_graphics_driver",
                    "error:init_video_card_info_failed",
                    "error:enumerate_adapter_modes_failed",
                    "error:slow_processor",
                    NULL
                };
                /* NOTE: "error:low_video_memory" and "error:low_system_memory" are NOT
                   included here because their code paths execute BEFORE Direct3DCreate9.
                   Patching them after-the-fact corrupts the game's init flow.
                   These are handled by the dialog auto-dismiss thread instead. */
                int ni;
                for (ni = 0; needles[ni]; ni++) {
                    const char *needle = needles[ni];
                    DWORD needleLen = strlen(needle);
                    DWORD offset;
                    BYTE *errorStr = NULL;

                    for (offset = 0; offset < imageSize - needleLen; offset++) {
                        if (memcmp(imgBase + offset, needle, needleLen) == 0) {
                            errorStr = imgBase + offset;
                            break;
                        }
                    }
                    if (!errorStr) continue;

                    {
                        char nb[256];
                        wsprintfA(nb, "Found '%s' at %p", needle, errorStr);
                        DebugLog(nb);
                    }

                    /* Search code sections for PUSH references to this string */
                    {
                        DWORD targetAddr = (DWORD)(DWORD_PTR)errorStr;
                        BYTE targetBytes[4];
                        DWORD si;
                        memcpy(targetBytes, &targetAddr, 4);

                        for (si = 0; si < nt->FileHeader.NumberOfSections; si++) {
                            BYTE *secBase = base + sections[si].VirtualAddress;
                            DWORD secSize = sections[si].Misc.VirtualSize;
                            DWORD j;

                            for (j = 0; j < secSize - 4; j++) {
                                if (memcmp(secBase + j, targetBytes, 4) == 0 &&
                                    j > 0 && secBase[j-1] == 0x68) {
                                    /* Found push <error_string> - look backwards for Jcc */
                                    DWORD k;
                                    char nb2[256];
                                    wsprintfA(nb2, "  PUSH ref at %p", secBase + j - 1);
                                    DebugLog(nb2);

                                    for (k = j - 2; k > j - 64 && k > 0; k--) {
                                        BYTE op = secBase[k];
                                        if (op >= 0x70 && op <= 0x7F && op != 0xEB) {
                                            signed char rel = (signed char)secBase[k+1];
                                            DWORD jumpTarget = k + 2 + rel;
                                            if (jumpTarget > j) {
                                                DWORD oldProt;
                                                wsprintfA(nb2, "  Patched Jcc 0x%02X at %p -> JMP (skips error push)",
                                                    op, secBase + k);
                                                DebugLog(nb2);
                                                VirtualProtect(secBase + k, 1, PAGE_EXECUTE_READWRITE, &oldProt);
                                                secBase[k] = 0xEB;
                                                VirtualProtect(secBase + k, 1, oldProt, &oldProt);
                                            }
                                            break;
                                        }
                                        if (op == 0x0F && k + 1 < secSize &&
                                            (secBase[k+1] == 0x84 || secBase[k+1] == 0x85)) {
                                            LONG rel32;
                                            DWORD jumpTarget;
                                            memcpy(&rel32, secBase + k + 2, 4);
                                            jumpTarget = k + 6 + rel32;
                                            if (jumpTarget > j) {
                                                DWORD oldProt;
                                                wsprintfA(nb2, "  Patched long Jcc at %p -> JMP rel32", secBase + k);
                                                DebugLog(nb2);
                                                VirtualProtect(secBase + k, 6, PAGE_EXECUTE_READWRITE, &oldProt);
                                                secBase[k] = 0x90;
                                                secBase[k+1] = 0xE9;
                                                VirtualProtect(secBase + k, 6, oldProt, &oldProt);
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                DebugLog("Error string scanner complete");
            }

                /* Factory range scan DISABLED - breaks control flow */
#if 0
                {
                    BYTE *factStart = base + (0x005BBB00 - 0x400000);
                    BYTE *factEnd = base + (0x005BC200 - 0x400000);
                    DWORD fi2;
                    int extraPatches = 0;
                    for (fi2 = 0; fi2 < (DWORD)(factEnd - factStart) - 5; fi2++) {
                        /* Look for push imm32 that references .rdata (008xxxxx) */
                        if (factStart[fi2] == 0x68 &&
                            factStart[fi2+3] == 0x8E &&
                            factStart[fi2+4] == 0x00 &&
                            (0x005BBB00 + fi2) != 0x005BBCA2) { /* Skip the null-check push */
                            /* This is likely push <error_string_addr>
                               Look backwards for a short Jcc that skips past it */
                            DWORD ki;
                            for (ki = fi2 - 1; ki > fi2 - 20 && ki > 0; ki--) {
                                BYTE b = factStart[ki];
                                if (b >= 0x70 && b <= 0x7F && b != 0xEB) {
                                    signed char rel = (signed char)factStart[ki+1];
                                    DWORD target = ki + 2 + rel;
                                    if (target > fi2) {
                                        /* Jcc skips past the push → make unconditional */
                                        DWORD oldProt;
                                        char pb2[128];
                                        wsprintfA(pb2, "FACTORY PATCH: Jcc 0x%02X at %08X -> JMP (skips push at %08X)",
                                            b, 0x005BBB00 + ki, 0x005BBB00 + fi2);
                                        DebugLog(pb2);
                                        VirtualProtect(factStart + ki, 1, PAGE_EXECUTE_READWRITE, &oldProt);
                                        factStart[ki] = 0xEB;
                                        VirtualProtect(factStart + ki, 1, oldProt, &oldProt);
                                        extraPatches++;
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    {
                        char pb2[64];
                        wsprintfA(pb2, "Factory range: %d extra patches applied", extraPatches);
                        DebugLog(pb2);
                    }
                }
#endif  /* end disabled factory range scan */
            }

            DebugLog("=== Targeted patches complete ===");

            /* WINED3D FBO PATCH: Fix GL_INVALID_FRAMEBUFFER_OPERATION on macOS */
            {
                HMODULE hWined3d = GetModuleHandleA("wined3d.dll");
                int glPatchCount = 0;
                DebugLog("=== WINED3D GL PATCH ===");
                if (hWined3d) {
                    BYTE *wBase = (BYTE*)hWined3d;
                    IMAGE_DOS_HEADER *wDos = (IMAGE_DOS_HEADER*)wBase;
                    IMAGE_NT_HEADERS *wNt = (IMAGE_NT_HEADERS*)(wBase + wDos->e_lfanew);
                    IMAGE_SECTION_HEADER *wSec = (IMAGE_SECTION_HEADER*)((BYTE*)wNt + sizeof(*wNt));
                    DWORD wi;
                    for (wi = 0; wi < wNt->FileHeader.NumberOfSections; wi++) {
                        if (wSec[wi].Characteristics & 0x20000000) {
                            BYTE *secBase2 = wBase + wSec[wi].VirtualAddress;
                            DWORD secSize2 = wSec[wi].Misc.VirtualSize;
                            DWORD wj;
                            for (wj = 0; wj < secSize2 - 7; wj++) {
                                /* Patch cmp eax, 0x506 → cmp eax, 0xFFFFFFFF (never matches) */
                                if (secBase2[wj] == 0x3D &&
                                    secBase2[wj+1] == 0x06 && secBase2[wj+2] == 0x05 &&
                                    secBase2[wj+3] == 0x00 && secBase2[wj+4] == 0x00) {
                                    DWORD oldProt;
                                    VirtualProtect(secBase2+wj, 5, PAGE_EXECUTE_READWRITE, &oldProt);
                                    secBase2[wj+1]=0xFF; secBase2[wj+2]=0xFF;
                                    secBase2[wj+3]=0xFF; secBase2[wj+4]=0xFF;
                                    VirtualProtect(secBase2+wj, 5, oldProt, &oldProt);
                                    glPatchCount++;
                                }
                                /* Patch cmp eax, 0x8CD5; JE → NOP; JMP (always framebuffer complete) */
                                if (secBase2[wj] == 0x3D &&
                                    secBase2[wj+1] == 0xD5 && secBase2[wj+2] == 0x8C &&
                                    secBase2[wj+3] == 0x00 && secBase2[wj+4] == 0x00 &&
                                    secBase2[wj+5] == 0x0F && secBase2[wj+6] == 0x84) {
                                    DWORD oldProt;
                                    VirtualProtect(secBase2+wj+5, 2, PAGE_EXECUTE_READWRITE, &oldProt);
                                    secBase2[wj+5] = 0x90; secBase2[wj+6] = 0xE9;
                                    VirtualProtect(secBase2+wj+5, 2, oldProt, &oldProt);
                                    glPatchCount++;
                                }
                            }
                        }
                    }
                }
                { char tb[64]; wsprintfA(tb, "GL PATCH: %d locations patched", glPatchCount); DebugLog(tb); }
            }

#if 0  /* Broad error string patches DISABLED - using targeted patches + click_continue */

            for (i = 0; i < nt->FileHeader.NumberOfSections; i++) {
                if (memcmp(sections[i].Name, ".text", 5) == 0 ||
                    memcmp(sections[i].Name, "stxt", 4) == 0) {
                    BYTE *secBase = base + sections[i].VirtualAddress;
                    DWORD secSize = sections[i].Misc.VirtualSize;
                    wsprintfA(tbuf, "Scanning section %.8s at %p size 0x%X",
                        sections[i].Name, secBase, secSize);
                    DebugLog(tbuf);

                    /* Search for the "error:d3d_create_failed" string in data sections */
                }
            }

            /* Find the error string in the loaded image */
            {
                BYTE *scan = base;
                DWORD imageSize = nt->OptionalHeader.SizeOfImage;
                BYTE *errorStr = NULL;
                /* Patch ALL graphics error paths */
                const char *needles[] = {
                    "error:d3d_create_failed",
                    "error:unsupported_graphics_adapter",
                    "error:prototype_graphics_adapter",
                    "error:invalid_graphics_driver",
                    "error:old_graphics_driver",
                    "error:init_video_card_info_failed",
                    "error:enumerate_adapter_modes_failed",
                    "error:slow_processor",
                    NULL
                };
                int ni;
                for (ni = 0; needles[ni]; ni++) {
                const char *needle = needles[ni];
                DWORD needleLen = strlen(needle);
                DWORD offset;

                for (offset = 0; offset < imageSize - needleLen; offset++) {
                    if (memcmp(scan + offset, needle, needleLen) == 0) {
                        errorStr = scan + offset;
                        wsprintfA(tbuf, "Found 'error:d3d_create_failed' at %p", errorStr);
                        DebugLog(tbuf);
                        break;
                    }
                }

                if (errorStr) {
                    /* Now search code sections for references to this address */
                    DWORD targetAddr = (DWORD)(DWORD_PTR)errorStr;
                    BYTE targetBytes[4];
                    memcpy(targetBytes, &targetAddr, 4);

                    for (i = 0; i < nt->FileHeader.NumberOfSections; i++) {
                        BYTE *secBase = base + sections[i].VirtualAddress;
                        DWORD secSize = sections[i].Misc.VirtualSize;
                        DWORD j;

                        for (j = 0; j < secSize - 4; j++) {
                            if (memcmp(secBase + j, targetBytes, 4) == 0) {
                                DWORD refAddr = (DWORD)(DWORD_PTR)(secBase + j);
                                wsprintfA(tbuf, "  Ref at %p (section %.8s+0x%X) byte_before=0x%02X",
                                    secBase + j, sections[i].Name, j,
                                    j > 0 ? secBase[j-1] : 0);
                                DebugLog(tbuf);

                                /* If this is a push instruction (0x68), the function that
                                   pushes this error string is the error handler.
                                   We need to find the conditional jump BEFORE this push
                                   and invert/NOP it. */
                                if (j > 0 && secBase[j-1] == 0x68) {
                                    /* Dump decoded code around this push for analysis */
                                    {
                                        DWORD dumpStart = (j > 80) ? j - 80 : 0;
                                        DWORD dumpEnd = (j + 80 < secSize) ? j + 80 : secSize;
                                        DWORD d;
                                        DebugLog("  CODE DUMP (decoded memory):");
                                        for (d = dumpStart; d < dumpEnd; d += 16) {
                                            char line[128];
                                            DWORD lineVA = (DWORD)(DWORD_PTR)(secBase + d);
                                            int pos = wsprintfA(line, "  %08X:", lineVA);
                                            DWORD b;
                                            for (b = 0; b < 16 && d+b < dumpEnd; b++) {
                                                pos += wsprintfA(line + pos, " %02X", secBase[d+b]);
                                            }
                                            if (d <= j && j < d + 16) strcat(line, "  <-- push");
                                            DebugLog(line);
                                        }
                                    }
                                    /* Found push <error_string>!
                                       Look backwards for a conditional jump (Jcc) */
                                    DWORD k;
                                    for (k = j - 2; k > j - 64 && k > 0; k--) {
                                        BYTE op = secBase[k];
                                        /* Short conditional jumps: 0x74=JE, 0x75=JNE, 0x0F84=JE long, 0x0F85=JNE long */
                                        if (op == 0x74 || op == 0x75) {
                                            /* Short JE/JNE - only change if it jumps PAST the push (skips error) */
                                            signed char rel = (signed char)secBase[k+1];
                                            DWORD jumpTarget = k + 2 + rel;
                                            if (jumpTarget > j) {
                                                /* Jump goes past push → it's the "skip error" jump → make unconditional */
                                                DWORD oldProtect;
                                                wsprintfA(tbuf, "  Jcc (0x%02X) at +0x%X jumps PAST push to +0x%X -> JMP",
                                                    op, k, jumpTarget);
                                                DebugLog(tbuf);
                                                VirtualProtect(secBase + k, 2, PAGE_EXECUTE_READWRITE, &oldProtect);
                                                secBase[k] = 0xEB; /* JMP short */
                                                VirtualProtect(secBase + k, 2, oldProtect, &oldProtect);
                                                DebugLog("  PATCHED!");
                                            } else {
                                                wsprintfA(tbuf, "  Jcc (0x%02X) at +0x%X jumps BEFORE push to +0x%X -> SKIP (would break flow)",
                                                    op, k, jumpTarget);
                                                DebugLog(tbuf);
                                                /* DON'T patch - this jump goes TO normal code, not past error */
                                            }
                                            break;
                                        }
                                        if (op == 0x0F && k + 1 < secSize &&
                                            (secBase[k+1] == 0x84 || secBase[k+1] == 0x85)) {
                                            /* Long Jcc - check direction */
                                            LONG rel32;
                                            DWORD jumpTarget;
                                            memcpy(&rel32, secBase + k + 2, 4);
                                            jumpTarget = k + 6 + rel32;
                                            if (jumpTarget > j) {
                                                /* Jumps past error → flip condition to always take it */
                                                DWORD oldProtect;
                                                wsprintfA(tbuf, "  Long Jcc at +0x%X jumps to +0x%X (past push) -> flip",
                                                    k, jumpTarget);
                                                DebugLog(tbuf);
                                                VirtualProtect(secBase + k, 6, PAGE_EXECUTE_READWRITE, &oldProtect);
                                                secBase[k+1] = (secBase[k+1] == 0x84) ? 0x85 : 0x84;
                                                VirtualProtect(secBase + k, 6, oldProtect, &oldProtect);
                                                DebugLog("  PATCHED!");
                                            } else {
                                                wsprintfA(tbuf, "  Long Jcc at +0x%X jumps to +0x%X (before push) -> SKIP",
                                                    k, jumpTarget);
                                                DebugLog(tbuf);
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                } /* end for ni (needles loop) */
            }
            /* Read the factory thunk and follow the JMP */
            {
                BYTE *thunk = base + (0x893EAC - 0x400000);
                char tbuf2[256];
                /* thunk is: FF 25 XX XX XX XX (JMP [addr]) */
                if (thunk[0] == 0xFF && thunk[1] == 0x25) {
                    DWORD *iatPtr = *(DWORD**)(thunk + 2);
                    DWORD targetAddr = *iatPtr;
                    wsprintfA(tbuf2, "Factory thunk: JMP [%p] -> actual function at 0x%08X", iatPtr, targetAddr);
                    DebugLog(tbuf2);

                    /* Dump the first 64 bytes of the actual function */
                    BYTE *actualFunc = (BYTE*)(DWORD_PTR)targetAddr;
                    DWORD d2;
                    DebugLog("=== Actual factory function ===");
                    for (d2 = 0; d2 < 64; d2 += 16) {
                        int p2 = wsprintfA(tbuf2, "  %08X:", targetAddr + d2);
                        DWORD b2;
                        for (b2 = 0; b2 < 16; b2++) {
                            p2 += wsprintfA(tbuf2 + p2, " %02X", actualFunc[d2+b2]);
                        }
                        DebugLog(tbuf2);
                    }
                }
            }
            /* OPENGL FIX: Scan ALL loaded modules for "cmp eax, 0x506" and patch them.
               This catches both the 32-bit wined3d.dll PE AND the 64-bit wined3d.so
               which are both loaded in the same process address space in wow64 mode. */
            {
                HMODULE hWined3d = GetModuleHandleA("wined3d.dll");
                int glPatchCount = 0;

                DebugLog("=== WINED3D GL PATCH: Scanning all loaded modules ===");

                /* Scan wined3d.dll PE sections */
                if (hWined3d) {
                    BYTE *wBase = (BYTE*)hWined3d;
                    IMAGE_DOS_HEADER *wDos = (IMAGE_DOS_HEADER*)wBase;
                    IMAGE_NT_HEADERS *wNt = (IMAGE_NT_HEADERS*)(wBase + wDos->e_lfanew);
                    IMAGE_SECTION_HEADER *wSec = (IMAGE_SECTION_HEADER*)((BYTE*)wNt + sizeof(*wNt));
                    DWORD wi;

                    for (wi = 0; wi < wNt->FileHeader.NumberOfSections; wi++) {
                        if (wSec[wi].Characteristics & 0x20000000) {
                            BYTE *secBase2 = wBase + wSec[wi].VirtualAddress;
                            DWORD secSize2 = wSec[wi].Misc.VirtualSize;
                            DWORD wj;
                            for (wj = 0; wj < secSize2 - 7; wj++) {
                                /* Patch cmp eax, 0x506 (GL error) */
                                if (secBase2[wj] == 0x3D &&
                                    secBase2[wj+1] == 0x06 &&
                                    secBase2[wj+2] == 0x05 &&
                                    secBase2[wj+3] == 0x00 &&
                                    secBase2[wj+4] == 0x00) {
                                    DWORD oldProt;
                                    VirtualProtect(secBase2 + wj, 5, PAGE_EXECUTE_READWRITE, &oldProt);
                                    secBase2[wj+1] = 0xFF; secBase2[wj+2] = 0xFF;
                                    secBase2[wj+3] = 0xFF; secBase2[wj+4] = 0xFF;
                                    VirtualProtect(secBase2 + wj, 5, oldProt, &oldProt);
                                    glPatchCount++;
                                }
                                /* Patch cmp eax, 0x8CD5 followed by JE -> change JE to JMP
                                   This makes wined3d always think framebuffer is COMPLETE */
                                if (secBase2[wj] == 0x3D &&
                                    secBase2[wj+1] == 0xD5 &&
                                    secBase2[wj+2] == 0x8C &&
                                    secBase2[wj+3] == 0x00 &&
                                    secBase2[wj+4] == 0x00 &&
                                    secBase2[wj+5] == 0x0F &&
                                    secBase2[wj+6] == 0x84) {
                                    DWORD oldProt;
                                    char tbuf5[128];
                                    wsprintfA(tbuf5, "  FBO check at %p: cmp eax,0x8CD5; JE -> NOP;JMP",
                                        secBase2 + wj);
                                    DebugLog(tbuf5);
                                    VirtualProtect(secBase2 + wj + 5, 2, PAGE_EXECUTE_READWRITE, &oldProt);
                                    secBase2[wj+5] = 0x90; /* NOP */
                                    secBase2[wj+6] = 0xE9; /* JMP (uses same rel32) */
                                    VirtualProtect(secBase2 + wj + 5, 2, oldProt, &oldProt);
                                    glPatchCount++;
                                }
                            }
                        }
                    }
                }

                /* Also scan wined3d's error CHECK function itself.
                   The function wined3d_check_gl_call checks glGetError result.
                   If we make it always return without checking, GL errors are ignored.

                   Find the function by searching for the "glClear" string reference
                   near the error check code. */
                {
                    char tbuf4[64];
                    wsprintfA(tbuf4, "GL PATCH: Patched %d cmp locations in PE", glPatchCount);
                    DebugLog(tbuf4);
                }
            }

            /* NUCLEAR OPTION: Patch the capability check function at 0x7F1FC0
               to always return 0 (success) instead of doing its actual check.
               This function is called BEFORE Direct3DCreate9 and its failure
               prevents the renderer from being created. */
            {
                /* The call at 005BBC5F is: E8 5C 63 23 00 -> target = 0x7F1FC0 */
                /* At runtime, this address is: */
                BYTE *capCheck = base + (0x7F1FC0 - 0x400000);
                char tbuf3[128];
                wsprintfA(tbuf3, "Cap check function at %p, first bytes: %02X %02X %02X %02X",
                    capCheck, capCheck[0], capCheck[1], capCheck[2], capCheck[3]);
                DebugLog(tbuf3);

                /* Don't nuclear-patch capCheck (0x7F1FC0) - it runs before Direct3DCreate9 */
                DebugLog("SKIPPING capCheck nuclear patch (let PRE-FACTORY JGE handle it)");

                /* Dump vidCheck (0x7F1AF6) ORIGINAL bytes for analysis - NO patching */
                {
                    BYTE *vidCheck = base + (0x7F1AF6 - 0x400000);
                    char tbuf3v[128];
                    wsprintfA(tbuf3v, "Video check at %p: %02X %02X %02X %02X %02X",
                        vidCheck, vidCheck[0], vidCheck[1], vidCheck[2], vidCheck[3], vidCheck[4]);
                    DebugLog(tbuf3v);
                    DebugLog("NOT nuclear-patching vidCheck - letting it run naturally for tracing");

                    /* Dump 4KB of ORIGINAL function bytes */
                    {
                        char dumpPath[MAX_PATH];
                        strcpy(dumpPath, g_dllDir);
                        strcat(dumpPath, "vidcheck_original.txt");
                        HANDLE hDump = CreateFileA(dumpPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
                        if (hDump != INVALID_HANDLE_VALUE) {
                            char hexline[128];
                            DWORD written;
                            int di;
                            for (di = 0; di < 4096; di += 16) {
                                int p = wsprintfA(hexline, "%08X:", 0x7F1AF6 + di);
                                int dj;
                                for (dj = 0; dj < 16; dj++)
                                    p += wsprintfA(hexline + p, " %02X", vidCheck[di+dj]);
                                p += wsprintfA(hexline + p, "\r\n");
                                WriteFile(hDump, hexline, p, &written, NULL);
                            }
                            CloseHandle(hDump);
                            DebugLog("Dumped 4KB of ORIGINAL vidCheck to vidcheck_original.txt");
                        }
                    }
                }
#if 0  /* capCheck nuclear patch disabled */
                   (called at 005BBCB5 with IDirect3D9* as first arg) */
                BYTE *vidCheck = base + (0x7F1AF6 - 0x400000);
                wsprintfA(tbuf3, "Video card check at %p, first bytes: %02X %02X %02X %02X",
                    vidCheck, vidCheck[0], vidCheck[1], vidCheck[2], vidCheck[3]);
                DebugLog(tbuf3);
                VirtualProtect(vidCheck, 4, PAGE_EXECUTE_READWRITE, &oldProtect3);
                vidCheck[0] = 0x33; /* xor eax, eax */
                vidCheck[1] = 0xC0;
                vidCheck[2] = 0xC3; /* ret */
                VirtualProtect(vidCheck, 4, oldProtect3, &oldProtect3);
                DebugLog("(nuclear patches disabled)");
#endif
            }

            /* CRITICAL PATCH: Force the pre-factory capability check to succeed.
               At runtime address 005BBC67, there's a JGE (0x7D) that jumps to
               the factory function at 005BBC95. If the D3D check fails (eax < 0),
               this jump is NOT taken and the factory is never called.
               Patch it to JMP (always jump to factory). */
            {
                /* Search for the pattern: E8 XX XX XX XX 85 C0 59 7D XX 68 */
                /* This is: call <check>; test eax,eax; pop ecx; JGE XX; push <error> */
                BYTE pattern[] = {0x85, 0xC0, 0x59, 0x7D};
                BYTE *textBase = base + 0x1000; /* .text section */
                DWORD textSize = 0x4B7000;
                DWORD s;
                int patchCount = 0;

                for (s = 0; s < textSize - sizeof(pattern) - 10; s++) {
                    if (memcmp(textBase + s, pattern, sizeof(pattern)) == 0) {
                        BYTE jge_offset = textBase[s + 4];
                        /* Verify: the byte after JGE+offset should be near a push (0x68) */
                        /* and 5 bytes before should be E8 (call) */
                        if (s >= 5 && textBase[s - 5] == 0xE8 && textBase[s + 5] == 0x68) {
                            DWORD patchAddr = (DWORD)(DWORD_PTR)(textBase + s + 3);
                            char tbuf[128];
                            wsprintfA(tbuf, "PRE-FACTORY PATCH: JGE at %08X (offset=0x%02X) -> JMP", patchAddr, jge_offset);
                            DebugLog(tbuf);

                            DWORD oldProtect;
                            VirtualProtect(textBase + s + 3, 1, PAGE_EXECUTE_READWRITE, &oldProtect);
                            textBase[s + 3] = 0xEB; /* JMP short (always jump to factory) */
                            VirtualProtect(textBase + s + 3, 1, oldProtect, &oldProtect);
                            patchCount++;
                        }
                    }
                }
                wsprintfA(tbuf, "PRE-FACTORY: Patched %d JGE instructions", patchCount);
                DebugLog(tbuf);
            }
            DebugLog("=== RUNTIME PATCH: Complete ===");
#endif  /* end disabled binary patches */
        }
    }

    return (IDirect3D9*)w;
}

/* Passthrough exports for D3DPERF functions */
typedef int (WINAPI *PFN_D3DPERF_BeginEvent)(DWORD col, LPCWSTR name);
typedef int (WINAPI *PFN_D3DPERF_EndEvent)(void);
typedef void (WINAPI *PFN_D3DPERF_SetMarker)(DWORD col, LPCWSTR name);
typedef void (WINAPI *PFN_D3DPERF_SetRegion)(DWORD col, LPCWSTR name);
typedef BOOL (WINAPI *PFN_D3DPERF_QueryRepeatFrame)(void);
typedef void (WINAPI *PFN_D3DPERF_SetOptions)(DWORD opts);
typedef DWORD (WINAPI *PFN_D3DPERF_GetStatus)(void);

#define GET_REAL_PROC(name) ((PFN_##name)GetProcAddress(real_d3d9, #name))

__declspec(dllexport) int WINAPI D3DPERF_BeginEvent(DWORD col, LPCWSTR name) {
    EnsureRealLoaded();
    PFN_D3DPERF_BeginEvent fn = GET_REAL_PROC(D3DPERF_BeginEvent);
    return fn ? fn(col, name) : 0;
}

__declspec(dllexport) int WINAPI D3DPERF_EndEvent(void) {
    EnsureRealLoaded();
    PFN_D3DPERF_EndEvent fn = GET_REAL_PROC(D3DPERF_EndEvent);
    return fn ? fn() : 0;
}

__declspec(dllexport) void WINAPI D3DPERF_SetMarker(DWORD col, LPCWSTR name) {
    EnsureRealLoaded();
    PFN_D3DPERF_SetMarker fn = GET_REAL_PROC(D3DPERF_SetMarker);
    if (fn) fn(col, name);
}

__declspec(dllexport) void WINAPI D3DPERF_SetRegion(DWORD col, LPCWSTR name) {
    EnsureRealLoaded();
    PFN_D3DPERF_SetRegion fn = GET_REAL_PROC(D3DPERF_SetRegion);
    if (fn) fn(col, name);
}

__declspec(dllexport) BOOL WINAPI D3DPERF_QueryRepeatFrame(void) {
    EnsureRealLoaded();
    PFN_D3DPERF_QueryRepeatFrame fn = GET_REAL_PROC(D3DPERF_QueryRepeatFrame);
    return fn ? fn() : FALSE;
}

__declspec(dllexport) void WINAPI D3DPERF_SetOptions(DWORD opts) {
    EnsureRealLoaded();
    PFN_D3DPERF_SetOptions fn = GET_REAL_PROC(D3DPERF_SetOptions);
    if (fn) fn(opts);
}

__declspec(dllexport) DWORD WINAPI D3DPERF_GetStatus(void) {
    EnsureRealLoaded();
    PFN_D3DPERF_GetStatus fn = GET_REAL_PROC(D3DPERF_GetStatus);
    return fn ? fn() : 0;
}

__declspec(dllexport) HRESULT WINAPI Direct3DCreate9Ex(UINT ver, IDirect3D9Ex **pp) {
    (void)ver;
    if (pp) *pp = NULL;
    return E_NOTIMPL;
}

__declspec(dllexport) void* WINAPI Direct3DShaderValidatorCreate9(void) {
    return NULL;
}


/* === GetProcAddress Hook - Intercept DirectDraw VRAM queries === */

#include <ddraw.h>

typedef FARPROC (WINAPI *PFN_GetProcAddress)(HMODULE, LPCSTR);
static PFN_GetProcAddress real_GetProcAddress = NULL;

/* Wrapped IDirectDraw7 that spoofs GetAvailableVidMem */
typedef struct {
    IDirectDraw7Vtbl *lpVtbl;
    IDirectDraw7 *real;
    IDirectDraw7Vtbl hooked_vtbl;
    LONG refcount;
} VRAMWrappedDD7;

#define VWDD(p) ((VRAMWrappedDD7*)(p))

static HRESULT WINAPI VW_QueryInterface(IDirectDraw7 *s, REFIID riid, void **out) {
    HRESULT hr = IDirectDraw7_QueryInterface(VWDD(s)->real, riid, out);
    if (SUCCEEDED(hr)) *out = s;
    return hr;
}
static ULONG WINAPI VW_AddRef(IDirectDraw7 *s) { return InterlockedIncrement(&VWDD(s)->refcount); }
static ULONG WINAPI VW_Release(IDirectDraw7 *s) {
    ULONG ref = InterlockedDecrement(&VWDD(s)->refcount);
    if (ref == 0) {
        IDirectDraw7_Release(VWDD(s)->real);
        HeapFree(GetProcessHeap(), 0, VWDD(s));
    }
    return ref;
}

static HRESULT WINAPI VW_GetAvailableVidMem(IDirectDraw7 *s, LPDDSCAPS2 caps, LPDWORD total, LPDWORD free) {
    HRESULT hr = IDirectDraw7_GetAvailableVidMem(VWDD(s)->real, caps, total, free);
    {
        char buf[128];
        wsprintfA(buf, "DDraw GetAvailableVidMem: hr=0x%X total=%u free=%u -> spoofing 256MB",
            hr, total ? *total : 0, free ? *free : 0);
        DebugLog(buf);
    }
    if (total) *total = 256 * 1024 * 1024;
    if (free) *free = 256 * 1024 * 1024;
    return DD_OK;
}

static HRESULT WINAPI VW_GetDeviceIdentifier(IDirectDraw7 *s, LPDDDEVICEIDENTIFIER2 id, DWORD flags) {
    HRESULT hr = IDirectDraw7_GetDeviceIdentifier(VWDD(s)->real, id, flags);
    if (SUCCEEDED(hr)) {
        DebugLog("DDraw GetDeviceIdentifier: spoofing NVIDIA");
        id->dwVendorId = 0x10DE;
        id->dwDeviceId = 0x0290;
        id->dwSubSysId = 0x04561043;
        id->dwRevision = 0xA1;
        strncpy(id->szDriver, "nv4_disp.dll", sizeof(id->szDriver)-1);
        strncpy(id->szDescription, "NVIDIA GeForce 7900 GTX", sizeof(id->szDescription)-1);
        id->dwWHQLLevel = 1;
    }
    return hr;
}

typedef HRESULT (WINAPI *PFN_DirectDrawCreateEx)(GUID*, LPVOID*, REFIID, IUnknown*);
static PFN_DirectDrawCreateEx real_DirectDrawCreateEx = NULL;

static HRESULT WINAPI Hooked_DirectDrawCreateEx(GUID *guid, LPVOID *out, REFIID iid, IUnknown *outer) {
    IDirectDraw7 *real7 = NULL;
    VRAMWrappedDD7 *wrapped;
    HRESULT hr;

    DebugLog("Hooked_DirectDrawCreateEx called");
    hr = real_DirectDrawCreateEx(guid, (LPVOID*)&real7, iid, outer);
    if (FAILED(hr) || !real7) {
        DebugLog("Real DirectDrawCreateEx failed");
        return hr;
    }

    wrapped = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*wrapped));
    if (!wrapped) { IDirectDraw7_Release(real7); return E_OUTOFMEMORY; }

    /* Copy real vtable and override key methods */
    memcpy(&wrapped->hooked_vtbl, real7->lpVtbl, sizeof(IDirectDraw7Vtbl));
    wrapped->hooked_vtbl.QueryInterface = VW_QueryInterface;
    wrapped->hooked_vtbl.AddRef = VW_AddRef;
    wrapped->hooked_vtbl.Release = VW_Release;
    wrapped->hooked_vtbl.GetAvailableVidMem = VW_GetAvailableVidMem;
    wrapped->hooked_vtbl.GetDeviceIdentifier = VW_GetDeviceIdentifier;

    wrapped->lpVtbl = &wrapped->hooked_vtbl;
    wrapped->real = real7;
    wrapped->refcount = 1;
    *out = wrapped;

    DebugLog("DirectDrawCreateEx: wrapped with VRAM spoof (256MB)");
    return DD_OK;
}

static FARPROC WINAPI Hooked_GetProcAddress(HMODULE hModule, LPCSTR lpProcName) {
    FARPROC result = real_GetProcAddress(hModule, lpProcName);

    /* Check if this is a request for DirectDrawCreateEx from ddraw.dll */
    if (lpProcName && ((DWORD_PTR)lpProcName > 0xFFFF) &&
        strcmp(lpProcName, "DirectDrawCreateEx") == 0 && result) {
        char buf[128];
        wsprintfA(buf, "Hooked GetProcAddress: intercepted DirectDrawCreateEx at %p", result);
        DebugLog(buf);
        real_DirectDrawCreateEx = (PFN_DirectDrawCreateEx)result;
        return (FARPROC)Hooked_DirectDrawCreateEx;
    }
    return result;
}

static void InstallGetProcAddressHook(void) {
    HMODULE game = GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    DWORD impDir;
    IMAGE_IMPORT_DESCRIPTOR *imp;

    if (!game) return;

    dos = (IMAGE_DOS_HEADER*)game;
    nt = (IMAGE_NT_HEADERS*)((BYTE*)game + dos->e_lfanew);
    impDir = nt->OptionalHeader.DataDirectory[1].VirtualAddress;
    if (!impDir) return;
    imp = (IMAGE_IMPORT_DESCRIPTOR*)((BYTE*)game + impDir);

    while (imp->Name) {
        char *name = (char*)((BYTE*)game + imp->Name);
        if (_stricmp(name, "kernel32.dll") == 0) {
            IMAGE_THUNK_DATA *origThunk = (IMAGE_THUNK_DATA*)((BYTE*)game + imp->OriginalFirstThunk);
            IMAGE_THUNK_DATA *iatThunk = (IMAGE_THUNK_DATA*)((BYTE*)game + imp->FirstThunk);

            while (origThunk->u1.AddressOfData) {
                if (!(origThunk->u1.Ordinal & 0x80000000)) {
                    IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME*)
                        ((BYTE*)game + origThunk->u1.AddressOfData);
                    if (strcmp(ibn->Name, "GetProcAddress") == 0) {
                        DWORD oldProt;
                        real_GetProcAddress = (PFN_GetProcAddress)iatThunk->u1.Function;
                        VirtualProtect(&iatThunk->u1.Function, 4, PAGE_READWRITE, &oldProt);
                        iatThunk->u1.Function = (DWORD_PTR)Hooked_GetProcAddress;
                        VirtualProtect(&iatThunk->u1.Function, 4, oldProt, &oldProt);
                        DebugLog("GetProcAddress IAT hook installed");
                        return;
                    }
                }
                origThunk++;
                iatThunk++;
            }
        }
        imp++;
    }
    DebugLog("WARNING: GetProcAddress not found in IAT");
}

/* === MessageBoxA IAT Hook - Block error dialogs === */

typedef int (WINAPI *PFN_MessageBoxA)(HWND, LPCSTR, LPCSTR, UINT);
static PFN_MessageBoxA real_MessageBoxA = NULL;

static int WINAPI Hooked_MessageBoxA(HWND hWnd, LPCSTR text, LPCSTR caption, UINT type) {
    char buf[512];
    wsprintfA(buf, "BLOCKED MessageBoxA: caption='%s' text='%.200s'",
        caption ? caption : "(null)", text ? text : "(null)");
    DebugLog(buf);
    return 1; /* IDOK */
}

/* === Dialog auto-dismiss thread === */
/* The game creates its own "Zoo Tycoon 2 Error" dialog window (not a MessageBox).
   This thread watches for it and clicks the "Continue" button automatically. */

static BOOL CALLBACK LogChildWindows(HWND hwnd, LPARAM lParam) {
    char text[128], cls[128], buf[512];
    GetWindowTextA(hwnd, text, sizeof(text));
    GetClassNameA(hwnd, cls, sizeof(cls));
    wsprintfA(buf, "  Child: hwnd=%p class='%s' text='%s'", hwnd, cls, text);
    DebugLog(buf);
    (void)lParam;
    return TRUE;
}

/* Find best button to click: prefer "Safe Mode" over "Continue" */
typedef struct { HWND continueBtn; HWND safeModeBtn; } BtnSearch;

static BOOL CALLBACK FindButtons(HWND hwnd, LPARAM lParam) {
    char text[64];
    BtnSearch *bs = (BtnSearch*)lParam;
    GetWindowTextA(hwnd, text, sizeof(text));
    if (strstr(text, "Safe Mode") || strstr(text, "safe mode")) {
        bs->safeModeBtn = hwnd;
    }
    if (strstr(text, "Continue") || strstr(text, "continue")) {
        bs->continueBtn = hwnd;
    }
    return TRUE;
}

static DWORD WINAPI DialogDismissThread(LPVOID param) {
    int attempts = 0;
    int dialogsDismissed = 0;
    (void)param;
    DebugLog("DialogDismissThread started");
    while (attempts < 240) { /* Watch for 120 seconds (240 * 500ms) */
        HWND errorWnd = FindWindowA(NULL, "Zoo Tycoon 2 Error");
        if (!errorWnd) errorWnd = FindWindowA(NULL, "Zoo Tycoon 2 Warning");
        if (errorWnd && IsWindowVisible(errorWnd)) {
            BtnSearch bs = {NULL, NULL};
            HWND clickBtn = NULL;
            char buf[256];
            wsprintfA(buf, "Found dialog #%d: hwnd=%p", dialogsDismissed+1, errorWnd);
            DebugLog(buf);

            /* Log all child windows for debugging */
            EnumChildWindows(errorWnd, LogChildWindows, 0);

            /* Find buttons - prefer Safe Mode over Continue */
            EnumChildWindows(errorWnd, FindButtons, (LPARAM)&bs);

            /* First try Safe Mode (actually continues running), then Continue */
            if (bs.safeModeBtn) {
                clickBtn = bs.safeModeBtn;
                wsprintfA(buf, "BLOCKED dialog #%d: clicking Safe Mode (hwnd=%p)", dialogsDismissed+1, clickBtn);
            } else if (bs.continueBtn) {
                clickBtn = bs.continueBtn;
                wsprintfA(buf, "BLOCKED dialog #%d: clicking Continue (hwnd=%p)", dialogsDismissed+1, clickBtn);
            }

            if (clickBtn) {
                DebugLog(buf);
                {
                    LONG id = GetWindowLongA(clickBtn, -12); /* GWL_ID */
                    SendMessageA(errorWnd, WM_COMMAND, (WPARAM)id, (LPARAM)clickBtn);
                }
                dialogsDismissed++;
                Sleep(1000);
                continue;
            } else {
                DebugLog("BLOCKED dialog: no buttons found, sending WM_CLOSE");
                PostMessageA(errorWnd, WM_CLOSE, 0, 0);
                dialogsDismissed++;
                Sleep(1000);
                continue;
            }
        }
        Sleep(500);
        attempts++;
    }
    {
        char buf[64];
        wsprintfA(buf, "DialogDismissThread exiting: dismissed %d dialogs", dialogsDismissed);
        DebugLog(buf);
    }
    return 0;
}

static void InstallCreateWindowExHook(void) {
    HMODULE game = GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    DWORD impDir;
    IMAGE_IMPORT_DESCRIPTOR *imp;

    if (!game) return;

    dos = (IMAGE_DOS_HEADER*)game;
    nt = (IMAGE_NT_HEADERS*)((BYTE*)game + dos->e_lfanew);
    impDir = nt->OptionalHeader.DataDirectory[1].VirtualAddress;
    if (!impDir) return;
    imp = (IMAGE_IMPORT_DESCRIPTOR*)((BYTE*)game + impDir);

    /* Hook MessageBoxA in the game's IAT */
    while (imp->Name) {
        char *name = (char*)((BYTE*)game + imp->Name);
        if (_stricmp(name, "user32.dll") == 0) {
            IMAGE_THUNK_DATA *origThunk = (IMAGE_THUNK_DATA*)((BYTE*)game + imp->OriginalFirstThunk);
            IMAGE_THUNK_DATA *iatThunk = (IMAGE_THUNK_DATA*)((BYTE*)game + imp->FirstThunk);

            while (origThunk->u1.AddressOfData) {
                if (!(origThunk->u1.Ordinal & 0x80000000)) {
                    IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME*)
                        ((BYTE*)game + origThunk->u1.AddressOfData);
                    if (strcmp(ibn->Name, "MessageBoxA") == 0) {
                        DWORD oldProt;
                        real_MessageBoxA = (PFN_MessageBoxA)iatThunk->u1.Function;
                        VirtualProtect(&iatThunk->u1.Function, 4, PAGE_READWRITE, &oldProt);
                        iatThunk->u1.Function = (DWORD_PTR)Hooked_MessageBoxA;
                        VirtualProtect(&iatThunk->u1.Function, 4, oldProt, &oldProt);
                        DebugLog("MessageBoxA IAT hook installed");
                    }
                }
                origThunk++;
                iatThunk++;
            }
        }
        imp++;
    }

    /* Start the dialog auto-dismiss thread */
    {
        HANDLE hThread = CreateThread(NULL, 0, DialogDismissThread, NULL, 0, NULL);
        if (hThread) {
            CloseHandle(hThread);
            DebugLog("Dialog auto-dismiss thread started");
        }
    }
}

/* === Early patch thread === */
/* The game's packed code is decompressed before WinMain runs, but after DllMain.
   This thread polls for the decompressed code and patches error checks early,
   BEFORE the game's VRAM check runs. */

static DWORD WINAPI EarlyPatchThread(LPVOID param) {
    HMODULE game = GetModuleHandleA(NULL);
    BYTE *base;
    int attempts = 0;
    (void)param;

    if (!game) return 0;
    base = (BYTE*)game;

    DebugLog("EarlyPatchThread: waiting for code decompression...");

    /* Poll until the game code at 005BBD38 is decompressed (we know the expected byte pattern) */
    while (attempts < 200) { /* Up to 10 seconds (200 * 50ms) */
        BYTE *check = base + (0x005BBEA4 - 0x400000);
        /* Check if code looks like valid x86 at a known location */
        if (check[0] >= 0x70 && check[0] <= 0x7F) {
            /* This looks like a Jcc instruction - code is decompressed! */
            IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER*)base;
            IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
            DWORD imageSize = nt->OptionalHeader.SizeOfImage;

            DebugLog("EarlyPatchThread: code is decompressed, applying early patches");

            /* Scan for VRAM/memory error strings and patch their guards.
               Only patch the memory-related ones here; other errors are
               patched later in Direct3DCreate9 when they're actually needed. */
            {
                const char *needles[] = {
                    NULL
                };
                IMAGE_SECTION_HEADER *sections = (IMAGE_SECTION_HEADER*)((BYTE*)nt + sizeof(*nt));
                int ni;
                for (ni = 0; needles[ni]; ni++) {
                    const char *needle = needles[ni];
                    DWORD needleLen = strlen(needle);
                    DWORD offset;
                    BYTE *errorStr = NULL;

                    for (offset = 0; offset < imageSize - needleLen; offset++) {
                        if (memcmp(base + offset, needle, needleLen) == 0) {
                            errorStr = base + offset;
                            break;
                        }
                    }
                    if (!errorStr) continue;

                    {
                        char nb[256];
                        wsprintfA(nb, "EARLY PATCH: Found '%s' at %p", needle, errorStr);
                        DebugLog(nb);
                    }

                    /* Search code sections for PUSH references */
                    {
                        DWORD targetAddr = (DWORD)(DWORD_PTR)errorStr;
                        BYTE targetBytes[4];
                        DWORD si;
                        memcpy(targetBytes, &targetAddr, 4);

                        for (si = 0; si < nt->FileHeader.NumberOfSections; si++) {
                            BYTE *secBase = base + sections[si].VirtualAddress;
                            DWORD secSize = sections[si].Misc.VirtualSize;
                            DWORD j;

                            for (j = 0; j < secSize - 4; j++) {
                                if (memcmp(secBase + j, targetBytes, 4) == 0 &&
                                    j > 0 && secBase[j-1] == 0x68) {
                                    DWORD k;
                                    char nb2[256];
                                    wsprintfA(nb2, "  PUSH ref at %p", secBase + j - 1);
                                    DebugLog(nb2);

                                    for (k = j - 2; k > j - 64 && k > 0; k--) {
                                        BYTE op = secBase[k];
                                        if (op >= 0x70 && op <= 0x7F && op != 0xEB) {
                                            signed char rel = (signed char)secBase[k+1];
                                            DWORD jumpTarget = k + 2 + rel;
                                            if (jumpTarget > j) {
                                                DWORD oldProt;
                                                wsprintfA(nb2, "  EARLY Patched Jcc 0x%02X at %p -> JMP",
                                                    op, secBase + k);
                                                DebugLog(nb2);
                                                VirtualProtect(secBase + k, 1, PAGE_EXECUTE_READWRITE, &oldProt);
                                                secBase[k] = 0xEB;
                                                VirtualProtect(secBase + k, 1, oldProt, &oldProt);
                                            }
                                            break;
                                        }
                                        if (op == 0x0F && k + 1 < secSize &&
                                            (secBase[k+1] == 0x84 || secBase[k+1] == 0x85)) {
                                            LONG rel32;
                                            DWORD jumpTarget;
                                            memcpy(&rel32, secBase + k + 2, 4);
                                            jumpTarget = k + 6 + rel32;
                                            if (jumpTarget > j) {
                                                DWORD oldProt;
                                                wsprintfA(nb2, "  EARLY Patched long Jcc at %p -> NOP+JMP", secBase + k);
                                                DebugLog(nb2);
                                                VirtualProtect(secBase + k, 6, PAGE_EXECUTE_READWRITE, &oldProt);
                                                secBase[k] = 0x90;
                                                secBase[k+1] = 0xE9;
                                                VirtualProtect(secBase + k, 6, oldProt, &oldProt);
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                DebugLog("EarlyPatchThread: early error string patches complete");
            }

            return 0;
        }
        Sleep(50);
        attempts++;
    }
    DebugLog("EarlyPatchThread: timeout waiting for decompression");
    return 0;
}

/* From cds_hook.c */
extern void InstallCDSHook(void);

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID p) {
    (void)p;
    if (r == DLL_PROCESS_ATTACH) {
        InitLogPath(h);
        DebugLog("=== D3D9 PROXY DLL LOADED ===");
        InstallCDSHook();
        DebugLog("ChangeDisplaySettings IAT hook installed");
        InstallCreateWindowExHook();
        /* Early patch thread disabled - using dialog dismiss instead */
    }
    if (r == DLL_PROCESS_DETACH && real_d3d9) {
        FreeLibrary(real_d3d9);
        real_d3d9 = NULL;
    }
    return TRUE;
}
