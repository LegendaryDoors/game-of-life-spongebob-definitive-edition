/* d3d9.dll proxy: upscales the game's 800x600 frame. */
#include <windows.h>
#include <mmsystem.h>
#include <psapi.h>
#include <stdio.h>
#include <stdarg.h>

#include "version.h"

static char   g_exedir[MAX_PATH];
static char   g_inipath[MAX_PATH];
static HANDLE g_log = INVALID_HANDLE_VALUE;
static LARGE_INTEGER g_logFreq, g_logT0;

static void log_open(void)
{
    char path[MAX_PATH];
    char *slash;

    QueryPerformanceFrequency(&g_logFreq);
    QueryPerformanceCounter(&g_logT0);
    GetModuleFileNameA(NULL, g_exedir, sizeof(g_exedir));
    slash = strrchr(g_exedir, '\\');
    if (slash)
        *slash = 0;
    _snprintf(path, sizeof(path) - 1, "%s\\%s", g_exedir, GOLDE_LOG);
    path[sizeof(path) - 1] = 0;
    _snprintf(g_inipath, sizeof(g_inipath) - 1, "%s\\%s", g_exedir, GOLDE_INI);
    g_inipath[sizeof(g_inipath) - 1] = 0;
    g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

static void LOG(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    LARGE_INTEGER now;
    int n, m;

    QueryPerformanceCounter(&now);
    m = _snprintf(buf, 16, "[%8.3f] ", g_logFreq.QuadPart
                  ? (double)(now.QuadPart - g_logT0.QuadPart) / (double)g_logFreq.QuadPart
                  : 0.0);
    if (m < 0 || m > 15)
        m = 0;
    va_start(ap, fmt);
    n = _vsnprintf(buf + m, sizeof(buf) - 3 - m, fmt, ap);
    va_end(ap);
    if (n < 0)
        n = sizeof(buf) - 3 - m;
    n += m;
    buf[n] = '\r';
    buf[n + 1] = '\n';
    OutputDebugStringA(buf);
    if (g_log != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(g_log, buf, (DWORD)n + 2, &written, NULL);
        FlushFileBuffers(g_log);
    }
}

enum { FILTER_SHARP = 0, FILTER_LINEAR = 1, FILTER_POINT = 2 };

static int  g_cfgWidth, g_cfgHeight;
static int  g_cfgFilter = FILTER_SHARP;
static BOOL g_cfgEnabled = TRUE;
static BOOL g_cfgStretch = FALSE;
static BOOL g_cfgWindowed = FALSE;
static int  g_cfgFrameCap = 0;
static BOOL g_cfgDiag = FALSE;
static BOOL g_cfgRuntime = FALSE;
static BOOL g_cfgBackground = FALSE;

static void config_load(void)
{
    char buf[32];

    g_cfgEnabled = GetPrivateProfileIntA("Display", "Enabled", 1, g_inipath) != 0;
    g_cfgWidth   = GetPrivateProfileIntA("Display", "Width", 0, g_inipath);
    g_cfgHeight  = GetPrivateProfileIntA("Display", "Height", 0, g_inipath);
    g_cfgStretch = GetPrivateProfileIntA("Display", "Stretch", 0, g_inipath) != 0;

    g_cfgDiag = FALSE;
    g_cfgFrameCap = GetPrivateProfileIntA("Display", "FrameCap", 0, g_inipath);
    if (g_cfgFrameCap < 0 || g_cfgFrameCap > 1000)
        g_cfgFrameCap = 0;

    g_cfgBackground = GetPrivateProfileIntA("Display", "RunInBackground", 0,
                                            g_inipath) != 0;

    GetPrivateProfileStringA("Display", "Method", "proxy", buf, sizeof(buf), g_inipath);
    g_cfgRuntime = (!lstrcmpiA(buf, "runtime") || !lstrcmpiA(buf, "native"));

    GetPrivateProfileStringA("Display", "Mode", "fullscreen", buf, sizeof(buf), g_inipath);
    g_cfgWindowed = (!lstrcmpiA(buf, "windowed") || !lstrcmpiA(buf, "window"));

    GetPrivateProfileStringA("Display", "Filter", "sharp", buf, sizeof(buf), g_inipath);
    if (!lstrcmpiA(buf, "linear"))
        g_cfgFilter = FILTER_LINEAR;
    else if (!lstrcmpiA(buf, "point") || !lstrcmpiA(buf, "nearest"))
        g_cfgFilter = FILTER_POINT;
    else
        g_cfgFilter = FILTER_SHARP;
}

/* Order must match the FORWARD slots and d3d9.def. */
static const char *const kExports[] = {
    "Direct3DCreate9",
    "Direct3DCreate9Ex",
    "Direct3DShaderValidatorCreate9",
    "PSGPError",
    "PSGPSampleTexture",
    "D3DPERF_BeginEvent",
    "D3DPERF_EndEvent",
    "D3DPERF_GetStatus",
    "D3DPERF_QueryRepeatFrame",
    "D3DPERF_SetMarker",
    "D3DPERF_SetOptions",
    "D3DPERF_SetRegion",
    "DebugSetLevel",
    "DebugSetMute",
};
#define NEXPORTS (sizeof(kExports) / sizeof(kExports[0]))

static FARPROC g_fn[NEXPORTS];
static HMODULE g_real;

typedef void *(WINAPI *PFN_Direct3DCreate9)(UINT);

static void load_real(void)
{
    char path[MAX_PATH];
    UINT n;

    if (g_real)
        return;
    n = GetSystemDirectoryA(path, sizeof(path) - 16);
    if (!n)
        return;
    lstrcpynA(path + n, "\\d3d9.dll", (int)(sizeof(path) - n));
    g_real = LoadLibraryA(path);
    if (!g_real) {
        LOG("FATAL: cannot load %s (err %lu)", path, GetLastError());
        return;
    }
    for (size_t i = 0; i < NEXPORTS; i++)
        g_fn[i] = GetProcAddress(g_real, kExports[i]);
    LOG("real d3d9: %s (base %p)", path, (void *)g_real);
}

#define FORWARD(name, slot)                                  \
    extern "C" __declspec(naked) void name(void)             \
    {                                                        \
        __asm { jmp dword ptr [g_fn + slot * 4] }            \
    }

FORWARD(Direct3DCreate9Ex,              1)
FORWARD(Direct3DShaderValidatorCreate9, 2)
FORWARD(PSGPError,                      3)
FORWARD(PSGPSampleTexture,              4)
FORWARD(D3DPERF_BeginEvent,             5)
FORWARD(D3DPERF_EndEvent,               6)
FORWARD(D3DPERF_GetStatus,              7)
FORWARD(D3DPERF_QueryRepeatFrame,       8)
FORWARD(D3DPERF_SetMarker,              9)
FORWARD(D3DPERF_SetOptions,            10)
FORWARD(D3DPERF_SetRegion,             11)
FORWARD(DebugSetLevel,                 12)
FORWARD(DebugSetMute,                  13)

typedef struct {
    UINT  BackBufferWidth, BackBufferHeight, BackBufferFormat, BackBufferCount;
    DWORD MultiSampleType, MultiSampleQuality, SwapEffect;
    HWND  hDeviceWindow;
    BOOL  Windowed, EnableAutoDepthStencil;
    DWORD AutoDepthStencilFormat, Flags;
    UINT  FullScreen_RefreshRateInHz, PresentationInterval;
} PRESENT_PARAMS;

typedef struct { DWORD X, Y, Width, Height; float MinZ, MaxZ; } VIEWPORT9;
typedef struct { INT Pitch; void *pBits; } LOCKED_RECT;

#define D3DPOOL_SYSTEMMEM  2
#define D3DLOCK_READONLY   0x10

#define D3DCLEAR_TARGET 0x1
#define D3DTEXF_POINT   1
#define D3DTEXF_LINEAR  2

#define VT_D3D9_CREATEDEVICE 16

#define VT_DEV_SETCURSORPROPS  10
#define VT_DEV_SETCURSORPOS    11
#define VT_DEV_SHOWCURSOR      12
#define VT_DEV_RESET           16
#define VT_DEV_PRESENT         17
#define VT_DEV_GETBACKBUFFER   18
#define VT_DEV_CREATERT        28
#define VT_DEV_GETRTDATA       32
#define VT_DEV_STRETCHRECT     34
#define VT_DEV_CREATEOFFSCREEN 36

#define VT_SURF_LOCKRECT       13
#define VT_SURF_UNLOCKRECT     14
#define VT_DEV_SETRENDERTARGET 37
#define VT_DEV_GETRENDERTARGET 38
#define VT_DEV_CLEAR           43
#define VT_DEV_SETVIEWPORT     47
#define VT_DEV_GETVIEWPORT     48

typedef ULONG   (WINAPI *PFN_Release)(void *);
typedef HRESULT (WINAPI *PFN_CreateDevice)(void *, UINT, DWORD, HWND, DWORD,
                                           PRESENT_PARAMS *, void **);
typedef HRESULT (WINAPI *PFN_Present)(void *, const RECT *, const RECT *, HWND,
                                      const void *);
typedef HRESULT (WINAPI *PFN_Reset)(void *, PRESENT_PARAMS *);
typedef HRESULT (WINAPI *PFN_GetBackBuffer)(void *, UINT, UINT, DWORD, void **);
typedef HRESULT (WINAPI *PFN_CreateRenderTarget)(void *, UINT, UINT, DWORD, DWORD,
                                                 DWORD, BOOL, void **, HANDLE *);
typedef HRESULT (WINAPI *PFN_StretchRect)(void *, void *, const RECT *, void *,
                                          const RECT *, DWORD);
typedef HRESULT (WINAPI *PFN_SetRenderTarget)(void *, DWORD, void *);
typedef HRESULT (WINAPI *PFN_GetRenderTarget)(void *, DWORD, void **);
typedef HRESULT (WINAPI *PFN_Clear)(void *, DWORD, const void *, DWORD, DWORD,
                                    float, DWORD);
typedef HRESULT (WINAPI *PFN_SetViewport)(void *, const VIEWPORT9 *);
typedef HRESULT (WINAPI *PFN_GetViewport)(void *, VIEWPORT9 *);
typedef HRESULT (WINAPI *PFN_CreateOffscreen)(void *, UINT, UINT, DWORD, DWORD,
                                              void **, HANDLE *);
typedef HRESULT (WINAPI *PFN_GetRenderTargetData)(void *, void *, void *);
typedef HRESULT (WINAPI *PFN_LockRect)(void *, LOCKED_RECT *, const RECT *, DWORD);
typedef HRESULT (WINAPI *PFN_UnlockRect)(void *);

static void *vt(void *obj, int slot)
{
    return (*(void ***)obj)[slot];
}

#define CALLV(obj, slot, type) ((type)vt((obj), (slot)))

static PFN_CreateDevice g_origCreateDevice;
static PFN_Present      g_origPresent;
static PFN_Reset        g_origReset;

static HWND    g_hwnd;
static HWND    g_root;
static WNDPROC g_origRootProc;
static WNDPROC g_origChildProc;

static UINT g_srcW = 800, g_srcH = 600;
static UINT g_bbW,  g_bbH;
static void *g_mid;
static UINT g_midW, g_midH;
static DWORD g_dumpFormat;
static void *g_probe, *g_probeSys;
static RECT g_dest;
static int  g_frames, g_tick;
static BOOL g_active;

static void monitor_rects(HWND hwnd, RECT *mon, RECT *work)
{
    HMONITOR h = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;

    mi.cbSize = sizeof(mi);
    if (h && GetMonitorInfoA(h, &mi)) {
        if (mon)
            *mon = mi.rcMonitor;
        if (work)
            *work = mi.rcWork;
        return;
    }
    if (mon) {
        mon->left = mon->top = 0;
        mon->right = GetSystemMetrics(SM_CXSCREEN);
        mon->bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    if (work) {
        work->left = work->top = 0;
        work->right = GetSystemMetrics(SM_CXSCREEN);
        work->bottom = GetSystemMetrics(SM_CYSCREEN);
    }
}

static void monitor_size(HWND hwnd, RECT *rc)
{
    monitor_rects(hwnd, rc, NULL);
}

static void windowed_size(HWND hwnd, UINT *w, UINT *h)
{
    RECT work;
    UINT availW, availH, sx, sy, s;

    if (g_cfgWidth > 0 && g_cfgHeight > 0) {
        *w = (UINT)g_cfgWidth;
        *h = (UINT)g_cfgHeight;
        return;
    }
    monitor_rects(hwnd, NULL, &work);
    availW = (UINT)((work.right - work.left) * 92 / 100);
    availH = (UINT)((work.bottom - work.top) * 92 / 100);

    sx = availW / g_srcW;
    sy = availH / g_srcH;
    s = sx < sy ? sx : sy;
    if (s < 1)
        s = 1;
    *w = g_srcW * s;
    *h = g_srcH * s;
}

static void compute_dest(UINT w, UINT h, RECT *dst)
{
    UINT dw, dh;

    if (g_cfgStretch) {
        dst->left = dst->top = 0;
        dst->right = (LONG)w;
        dst->bottom = (LONG)h;
        return;
    }
    if ((UINT64)w * g_srcH >= (UINT64)h * g_srcW) {
        dh = h;
        dw = (UINT)((UINT64)h * g_srcW / g_srcH);
    } else {
        dw = w;
        dh = (UINT)((UINT64)w * g_srcH / g_srcW);
    }
    dst->left = (LONG)((w - dw) / 2);
    dst->top = (LONG)((h - dh) / 2);
    dst->right = dst->left + (LONG)dw;
    dst->bottom = dst->top + (LONG)dh;
}

typedef BOOL (WINAPI *PFN_ScreenToClient)(HWND, LPPOINT);
static PFN_ScreenToClient g_origScreenToClient;

typedef BOOL (WINAPI *PFN_GetCursorPos)(LPPOINT);
static PFN_GetCursorPos g_origGetCursorPos;
static LONG g_nGetCursor, g_nScreenToClient;

static BOOL WINAPI My_GetCursorPos(LPPOINT pt)
{
    BOOL ok = g_origGetCursorPos(pt);
    POINT c, org = { 0, 0 };
    LONG dw, dh, x, y;

    InterlockedIncrement(&g_nGetCursor);
    if (!ok || !pt || !g_active || !g_hwnd)
        return ok;

    dw = g_dest.right - g_dest.left;
    dh = g_dest.bottom - g_dest.top;
    if (dw <= 0 || dh <= 0)
        return ok;

    c = *pt;
    g_origScreenToClient(g_hwnd, &c);
    x = ((c.x - g_dest.left) * (LONG)g_srcW) / dw;
    y = ((c.y - g_dest.top) * (LONG)g_srcH) / dh;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > (LONG)g_srcW - 1) x = (LONG)g_srcW - 1;
    if (y > (LONG)g_srcH - 1) y = (LONG)g_srcH - 1;

    ClientToScreen(g_hwnd, &org);
    pt->x = org.x + x;
    pt->y = org.y + y;
    return ok;
}

static void note_hwnd(HWND hwnd, BOOL remapped)
{
    static HWND seen[8];
    int i;

    if (!g_cfgDiag)
        return;
    for (i = 0; i < 8; i++) {
        if (seen[i] == hwnd)
            return;
        if (!seen[i]) {
            seen[i] = hwnd;
            LOG("ScreenToClient on %p (%s) -> %s", (void *)hwnd,
                hwnd == g_hwnd ? "device child" :
                hwnd == g_root ? "FRAME" : "unknown window",
                remapped ? "remapped" : "LEFT ALONE");
            return;
        }
    }
}

static BOOL WINAPI My_ScreenToClient(HWND hwnd, LPPOINT pt)
{
    BOOL ok = g_origScreenToClient(hwnd, pt);

    InterlockedIncrement(&g_nScreenToClient);
    note_hwnd(hwnd, FALSE);

    return ok;
}

static void patch_iat(HMODULE mod, const char *dll, const void *target,
                      void *replacement)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    DWORD rva;

    if (IsBadReadPtr(base, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return;
    rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva)
        return;

    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + rva); imp->Name; imp++) {
        const char *name = (const char *)(base + imp->Name);
        void **thunk;

        if (lstrcmpiA(name, dll))
            continue;
        thunk = (void **)(base + imp->FirstThunk);
        for (; *thunk; thunk++) {
            DWORD prot;

            if (*thunk != target)
                continue;

            /* EXECUTE_READWRITE: the IAT may be in an executable section. */
            if (!VirtualProtect(thunk, sizeof(void *), PAGE_EXECUTE_READWRITE, &prot))
                continue;
            *thunk = replacement;
            VirtualProtect(thunk, sizeof(void *), prot, &prot);
        }
    }
}

typedef BOOL (WINAPI *PFN_ClipCursor)(const RECT *);
static PFN_ClipCursor g_origClipCursor;

static BOOL WINAPI My_ClipCursor(const RECT *r)
{
    RECT mapped;

    if (r && g_active) {
        if (g_cfgDiag)
            LOG("ClipCursor(%ld,%ld,%ld,%ld) -> remapped to dest rect",
                r->left, r->top, r->right, r->bottom);
        GetClientRect(g_hwnd, &mapped);
        MapWindowPoints(g_hwnd, NULL, (POINT *)&mapped, 2);
        return g_origClipCursor(&mapped);
    }
    if (g_cfgDiag)
        LOG("ClipCursor(%s)", r ? "rect" : "NULL - released");
    return g_origClipCursor(r);
}

typedef int (WINAPI *PFN_ShowCursor)(BOOL);
static PFN_ShowCursor g_origShowCursor;

static int WINAPI My_ShowCursor(BOOL show)
{
    int count = g_origShowCursor(show);

    if (g_cfgDiag)
        LOG("ShowCursor(%s) -> display count %d", show ? "TRUE" : "FALSE", count);
    return count;
}

#define CK_GETFOCUSLOSTBEHAVIOR "?GetFocusLostBehavior@CKContext@@QAEIXZ"

/* 0 = never pause. __fastcall matches CK2's argument-less __thiscall. */
static unsigned int __fastcall My_GetFocusLostBehavior(void *self, void *edx)
{
    (void)self;
    (void)edx;
    return 0;
}

static BOOL g_focusPatched;

static void hook_focus(HMODULE *mods, DWORD count, HMODULE self)
{
    HMODULE ck2;
    void *fn;
    DWORD i;

    if (!g_cfgBackground || g_focusPatched)
        return;
    ck2 = GetModuleHandleA("CK2.dll");
    if (!ck2) {
        LOG("RunInBackground: CK2.dll not loaded yet, will retry");
        return;
    }
    fn = (void *)GetProcAddress(ck2, CK_GETFOCUSLOSTBEHAVIOR);
    if (!fn) {
        LOG("RunInBackground: CK2.dll has no " CK_GETFOCUSLOSTBEHAVIOR
            " - leaving the pause alone");
        g_focusPatched = TRUE;
        return;
    }
    for (i = 0; i < count; i++) {
        if (mods[i] == ck2 || mods[i] == self)
            continue;
        patch_iat(mods[i], "CK2.dll", fn, (void *)My_GetFocusLostBehavior);
    }
    g_focusPatched = TRUE;
    LOG("RunInBackground: GetFocusLostBehavior pinned to 0, the game keeps "
        "running unfocused");
}

static void hook_cursor(void)
{
    HMODULE user32 = GetModuleHandleA("user32.dll");
    HMODULE self = GetModuleHandleA("d3d9.dll");
    HMODULE mods[512];
    DWORD needed = 0, i;
    void *stc, *clip, *gcp, *shc;

    if (!user32)
        return;
    stc = (void *)GetProcAddress(user32, "ScreenToClient");
    clip = (void *)GetProcAddress(user32, "ClipCursor");
    gcp = (void *)GetProcAddress(user32, "GetCursorPos");
    shc = (void *)GetProcAddress(user32, "ShowCursor");
    if (!stc)
        return;
    if (!g_origScreenToClient)
        g_origScreenToClient = (PFN_ScreenToClient)stc;
    if (clip && !g_origClipCursor)
        g_origClipCursor = (PFN_ClipCursor)clip;
    if (gcp && !g_origGetCursorPos)
        g_origGetCursorPos = (PFN_GetCursorPos)gcp;
    if (shc && !g_origShowCursor)
        g_origShowCursor = (PFN_ShowCursor)shc;

    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed))
        return;
    if (needed > sizeof(mods))
        needed = sizeof(mods);
    for (i = 0; i < needed / sizeof(HMODULE); i++) {
        if (mods[i] == user32 || mods[i] == self)
            continue;
        patch_iat(mods[i], "USER32.dll", stc, (void *)My_ScreenToClient);
        if (clip)
            patch_iat(mods[i], "USER32.dll", clip, (void *)My_ClipCursor);
        if (gcp)
            patch_iat(mods[i], "USER32.dll", gcp, (void *)My_GetCursorPos);
        if (shc && g_cfgDiag)
            patch_iat(mods[i], "USER32.dll", shc, (void *)My_ShowCursor);
    }
    hook_focus(mods, needed / sizeof(HMODULE), self);
}

static LRESULT CALLBACK Root_WndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_WINDOWPOSCHANGING: {
        WINDOWPOS *wp = (WINDOWPOS *)l;
        RECT mr;

        if (IsIconic(h) || (wp->flags & SWP_HIDEWINDOW))
            break;

        if (g_cfgWindowed) {
            RECT want = { 0, 0, (LONG)g_bbW, (LONG)g_bbH };

            AdjustWindowRect(&want, (DWORD)GetWindowLongA(h, GWL_STYLE), FALSE);
            wp->cx = want.right - want.left;
            wp->cy = want.bottom - want.top;
            wp->flags &= ~SWP_NOSIZE;
        } else {
            monitor_size(h, &mr);
            wp->x = mr.left;
            wp->y = mr.top;
            wp->cx = mr.right - mr.left;
            wp->cy = mr.bottom - mr.top;
            wp->flags &= ~(SWP_NOSIZE | SWP_NOMOVE);
        }
        break;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)l;
        RECT mr;

        if (g_cfgWindowed)
            break;
        monitor_size(h, &mr);
        mmi->ptMaxSize.x = mr.right - mr.left;
        mmi->ptMaxSize.y = mr.bottom - mr.top;
        mmi->ptMaxTrackSize.x = mmi->ptMaxSize.x + 64;
        mmi->ptMaxTrackSize.y = mmi->ptMaxSize.y + 64;
        break;
    }
    case WM_ERASEBKGND: {
        RECT rc;

        GetClientRect(h, &rc);
        FillRect((HDC)w, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return 1;
    }
    case WM_ACTIVATEAPP:

        if (g_cfgDiag)
            LOG("WM_ACTIVATEAPP %s", w ? "activated" : "DEACTIVATED (player pauses)");
        break;
    }
    return CallWindowProcA(g_origRootProc, h, msg, w, l);
}

static LRESULT CALLBACK Child_WndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_WINDOWPOSCHANGING && g_root && !IsIconic(g_root)) {
        WINDOWPOS *wp = (WINDOWPOS *)l;
        RECT rc;

        GetClientRect(g_root, &rc);
        if (rc.right > 0 && rc.bottom > 0) {
            wp->x = 0;
            wp->y = 0;
            wp->cx = rc.right;
            wp->cy = rc.bottom;
            wp->flags &= ~(SWP_NOSIZE | SWP_NOMOVE);
        }
    }
    return CallWindowProcA(g_origChildProc, h, msg, w, l);
}

static void apply_window_mode(void)
{
    RECT mr, work, rc;
    LONG style, ex;

    if (!g_root)
        return;
    monitor_rects(g_root, &mr, &work);

    if (!g_origRootProc) {
        g_origRootProc = (WNDPROC)SetWindowLongPtrA(g_root, GWLP_WNDPROC,
                                                    (LONG_PTR)Root_WndProc);
        LOG("subclassed frame %p (wndproc %p)", (void *)g_root,
            (void *)g_origRootProc);
    }

    style = GetWindowLongA(g_root, GWL_STYLE);
    ex = GetWindowLongA(g_root, GWL_EXSTYLE);

    if (g_cfgWindowed) {
        RECT want = { 0, 0, (LONG)g_bbW, (LONG)g_bbH };
        int x, y;

        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX | WS_POPUP);
        style |= WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                 WS_VISIBLE;
        SetWindowLongA(g_root, GWL_STYLE, style);
        SetWindowLongA(g_root, GWL_EXSTYLE, ex);

        AdjustWindowRect(&want, (DWORD)style, FALSE);
        x = work.left + ((work.right - work.left) - (want.right - want.left)) / 2;
        y = work.top + ((work.bottom - work.top) - (want.bottom - want.top)) / 2;
        SetWindowPos(g_root, HWND_TOP, x, y, want.right - want.left,
                     want.bottom - want.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
                   WS_SYSMENU | WS_BORDER | WS_DLGFRAME);
        style |= WS_POPUP | WS_VISIBLE;
        SetWindowLongA(g_root, GWL_STYLE, style);

        ex &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE |
                WS_EX_WINDOWEDGE);
        SetWindowLongA(g_root, GWL_EXSTYLE, ex);

        SetWindowPos(g_root, HWND_TOP, mr.left, mr.top, mr.right - mr.left,
                     mr.bottom - mr.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }

    if (g_hwnd && g_hwnd != g_root) {
        if (!g_origChildProc)
            g_origChildProc = (WNDPROC)SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC,
                                                         (LONG_PTR)Child_WndProc);
        GetClientRect(g_root, &rc);
        SetWindowPos(g_hwnd, NULL, 0, 0, rc.right, rc.bottom,
                     SWP_NOZORDER | SWP_SHOWWINDOW);
    }
}

static void release_surfaces(void)
{
    if (g_mid) {
        CALLV(g_mid, 2, PFN_Release)(g_mid);
        g_mid = NULL;
    }
    if (g_probe) {
        CALLV(g_probe, 2, PFN_Release)(g_probe);
        g_probe = NULL;
    }
    if (g_probeSys) {
        CALLV(g_probeSys, 2, PFN_Release)(g_probeSys);
        g_probeSys = NULL;
    }
}

/* StretchRect cannot go surface-to-itself, so an intermediate is required. */
static void create_surfaces(void *dev, DWORD format)
{
    UINT sx, sy, s;
    HRESULT hr;

    release_surfaces();
    if (g_cfgRuntime)
        return;

    sx = (UINT)(g_dest.right - g_dest.left) / g_srcW;
    sy = (UINT)(g_dest.bottom - g_dest.top) / g_srcH;
    s = sx < sy ? sx : sy;
    if (g_cfgFilter != FILTER_SHARP || s < 2)
        s = 1;

    g_midW = g_srcW * s;
    g_midH = g_srcH * s;
    hr = CALLV(dev, VT_DEV_CREATERT, PFN_CreateRenderTarget)(
             dev, g_midW, g_midH, format, 0, 0, FALSE, &g_mid, NULL);
    if (FAILED(hr)) {
        g_mid = NULL;
        LOG("intermediate %ux%u failed (0x%08lX); letting the runtime stretch",
            g_midW, g_midH, (unsigned long)hr);
        return;
    }
    LOG("intermediate %ux%u (%ux point step, then %s)", g_midW, g_midH, s,
        g_cfgFilter == FILTER_POINT ? "point" : "linear");
}

static void measure_rate(void)
{
    static LARGE_INTEGER freq, mark;
    static int frames, reports;
    LARGE_INTEGER now;
    double secs;

    if (reports >= 6)
        return;
    if (!freq.QuadPart) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&mark);
        return;
    }
    frames++;
    QueryPerformanceCounter(&now);
    secs = (double)(now.QuadPart - mark.QuadPart) / (double)freq.QuadPart;
    if (secs < 5.0)
        return;
    LOG("frame rate: %.1f fps over %.1fs   (GetCursorPos %ld, ScreenToClient %ld)",
        frames / secs, secs, g_nGetCursor, g_nScreenToClient);
    frames = 0;
    mark = now;
    reports++;
}

static void limit_frames(void)
{
    static LARGE_INTEGER freq, next;
    LARGE_INTEGER now;
    LONGLONG period;

    if (g_cfgFrameCap <= 0)
        return;
    if (!freq.QuadPart) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&next);
    }

    period = freq.QuadPart / g_cfgFrameCap;
    next.QuadPart += period;
    QueryPerformanceCounter(&now);

    if (now.QuadPart > next.QuadPart + period) {
        next = now;
        return;
    }

    while (now.QuadPart < next.QuadPart) {
        LONGLONG left = ((next.QuadPart - now.QuadPart) * 1000) / freq.QuadPart;

        if (left > 1)
            Sleep((DWORD)(left - 1));
        else
            Sleep(0);
        QueryPerformanceCounter(&now);
    }
}

#define PROBE 8

static DWORD g_probeSig;
static LARGE_INTEGER g_probeFreq, g_probeStart;
static int g_probeEvents;

static void probe_init(void *dev, DWORD format)
{
    if (!g_cfgDiag || g_probe)
        return;
    if (FAILED(CALLV(dev, VT_DEV_CREATERT, PFN_CreateRenderTarget)(
                   dev, PROBE, PROBE, format, 0, 0, FALSE, &g_probe, NULL)))
        g_probe = NULL;
    if (g_probe &&
        FAILED(CALLV(dev, VT_DEV_CREATEOFFSCREEN, PFN_CreateOffscreen)(
                   dev, PROBE, PROBE, format, D3DPOOL_SYSTEMMEM, &g_probeSys, NULL)))
        g_probeSys = NULL;
    QueryPerformanceFrequency(&g_probeFreq);
    QueryPerformanceCounter(&g_probeStart);
    LOG("diagnostic: frame probe %dx%d %s", PROBE, PROBE,
        (g_probe && g_probeSys) ? "ready" : "FAILED");
}

static BOOL WINAPI My_GetCursorPos(LPPOINT pt);

static int g_dumps;

static BOOL dump_requested(BOOL *full)
{
    static int check;
    char path[MAX_PATH];

    if ((++check & 7) != 0)
        return FALSE;
    _snprintf(path, sizeof(path) - 1, "%s\\de_dumpfull.req", g_exedir);
    path[sizeof(path) - 1] = 0;
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        DeleteFileA(path);
        *full = TRUE;
        return TRUE;
    }
    _snprintf(path, sizeof(path) - 1, "%s\\de_dump.req", g_exedir);
    path[sizeof(path) - 1] = 0;
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        DeleteFileA(path);
        *full = FALSE;
        return TRUE;
    }
    return FALSE;
}

static void dump_frame(void *dev, void *back, const RECT *srcRect, UINT w, UINT h,
                       DWORD format)
{
    LOCKED_RECT lr;
    char path[MAX_PATH];
    POINT raw = { 0, 0 }, mapped = { 0, 0 };
    void *rt = NULL, *sys = NULL;
    HANDLE f;
    DWORD wrote;
    HRESULT hr;
    BOOL full = FALSE;
    int y;

    if (!g_cfgDiag || g_dumps >= 64 || !back)
        return;
    if (!dump_requested(&full))
        return;
    if (full) {
        srcRect = NULL;
        w = g_bbW ? g_bbW : w;
        h = g_bbH ? g_bbH : h;
    }

    hr = CALLV(dev, VT_DEV_CREATERT, PFN_CreateRenderTarget)(
             dev, w, h, format, 0, 0, FALSE, &rt, NULL);
    if (FAILED(hr)) {
        LOG("dump: CreateRenderTarget %ux%u failed 0x%08lX", w, h, (unsigned long)hr);
        return;
    }
    hr = CALLV(dev, VT_DEV_CREATEOFFSCREEN, PFN_CreateOffscreen)(
             dev, w, h, format, D3DPOOL_SYSTEMMEM, &sys, NULL);
    if (FAILED(hr)) {
        LOG("dump: CreateOffscreenPlainSurface failed 0x%08lX", (unsigned long)hr);
        CALLV(rt, 2, PFN_Release)(rt);
        return;
    }

    hr = CALLV(dev, VT_DEV_STRETCHRECT, PFN_StretchRect)(
             dev, back, srcRect, rt, NULL, D3DTEXF_POINT);
    if (SUCCEEDED(hr))
        hr = CALLV(dev, VT_DEV_GETRTDATA, PFN_GetRenderTargetData)(dev, rt, sys);
    if (SUCCEEDED(hr))
        hr = CALLV(sys, VT_SURF_LOCKRECT, PFN_LockRect)(sys, &lr, NULL, D3DLOCK_READONLY);
    if (FAILED(hr)) {
        LOG("dump: copy/readback failed 0x%08lX", (unsigned long)hr);
        CALLV(sys, 2, PFN_Release)(sys);
        CALLV(rt, 2, PFN_Release)(rt);
        return;
    }

    _snprintf(path, sizeof(path) - 1, "%s\\de_frame_%d_%ux%u.raw", g_exedir,
              g_dumps, w, h);
    path[sizeof(path) - 1] = 0;
    f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        for (y = 0; y < (int)h; y++)
            WriteFile(f, (const BYTE *)lr.pBits + (size_t)y * lr.Pitch,
                      w * 4, &wrote, NULL);
        CloseHandle(f);
    }
    CALLV(sys, VT_SURF_UNLOCKRECT, PFN_UnlockRect)(sys);
    CALLV(sys, 2, PFN_Release)(sys);
    CALLV(rt, 2, PFN_Release)(rt);

    if (g_origGetCursorPos) {
        g_origGetCursorPos(&raw);
        mapped = raw;
        My_GetCursorPos(&mapped);
    }
    LOG("dumped de_frame_%d_%ux%u.raw (%s)  cursor screen=(%ld,%ld) "
        "engine sees=(%ld,%ld)", g_dumps, w, h, full ? "whole back buffer" :
        "engine region", raw.x, raw.y, mapped.x, mapped.y);
    g_dumps++;
}

static void probe_frame(void *dev, void *source)
{
    LOCKED_RECT lr;
    DWORD sig = 0;
    LARGE_INTEGER now;
    const BYTE *p;
    int x, y;

    if (!g_probe || !g_probeSys || !source || g_probeEvents >= 60)
        return;
    if ((g_tick & 3) != 0)
        return;

    if (FAILED(CALLV(dev, VT_DEV_STRETCHRECT, PFN_StretchRect)(
                   dev, source, NULL, g_probe, NULL, D3DTEXF_LINEAR)))
        return;
    if (FAILED(CALLV(dev, VT_DEV_GETRTDATA, PFN_GetRenderTargetData)(
                   dev, g_probe, g_probeSys)))
        return;
    if (FAILED(CALLV(g_probeSys, VT_SURF_LOCKRECT, PFN_LockRect)(
                   g_probeSys, &lr, NULL, D3DLOCK_READONLY)))
        return;

    for (y = 0; y < PROBE; y++) {
        p = (const BYTE *)lr.pBits + (size_t)y * lr.Pitch;
        for (x = 0; x < PROBE * 4; x++)
            sig += p[x];
    }
    CALLV(g_probeSys, VT_SURF_UNLOCKRECT, PFN_UnlockRect)(g_probeSys);

    if (g_probeSig && (sig > g_probeSig ? sig - g_probeSig : g_probeSig - sig) > 600) {
        QueryPerformanceCounter(&now);
        LOG("  picture changed: t=%.3fs frame=%d (sig %lu -> %lu)",
            (double)(now.QuadPart - g_probeStart.QuadPart) / (double)g_probeFreq.QuadPart,
            g_tick, g_probeSig, sig);
        g_probeEvents++;
    }
    g_probeSig = sig;
}

static HRESULT present_frame(void *dev, const RECT *src, const RECT *dst,
                             HWND wnd, const void *dirty)
{
    void *back = NULL, *oldRT = NULL;
    VIEWPORT9 oldVP, fullVP;
    RECT srcRect;
    BOOL haveVP;

    if (!g_active)
        return g_origPresent(dev, src, dst, wnd, dirty);

    srcRect.left = srcRect.top = 0;
    srcRect.right = (LONG)g_srcW;
    srcRect.bottom = (LONG)g_srcH;

    if (g_cfgRuntime || !g_mid) {
        if (g_cfgDiag && SUCCEEDED(CALLV(dev, VT_DEV_GETBACKBUFFER, PFN_GetBackBuffer)(
                                       dev, 0, 0, 0, &back)) && back) {
            dump_frame(dev, back, NULL, g_srcW, g_srcH, g_dumpFormat);
            CALLV(back, 2, PFN_Release)(back);
        }
        return g_origPresent(dev, NULL, &g_dest, NULL, NULL);
    }

    if (FAILED(CALLV(dev, VT_DEV_GETBACKBUFFER, PFN_GetBackBuffer)(
                   dev, 0, 0, 0, &back)) || !back)
        return g_origPresent(dev, &srcRect, &g_dest, NULL, NULL);

    CALLV(dev, VT_DEV_STRETCHRECT, PFN_StretchRect)(
        dev, back, &srcRect, g_mid, NULL, D3DTEXF_POINT);
    probe_frame(dev, g_mid);
    dump_frame(dev, back, &srcRect, g_srcW, g_srcH, g_dumpFormat);

    CALLV(dev, VT_DEV_GETRENDERTARGET, PFN_GetRenderTarget)(dev, 0, &oldRT);
    haveVP = SUCCEEDED(CALLV(dev, VT_DEV_GETVIEWPORT, PFN_GetViewport)(dev, &oldVP));
    CALLV(dev, VT_DEV_SETRENDERTARGET, PFN_SetRenderTarget)(dev, 0, back);

    /* Clear only affects the current viewport; widen it for the bars. */
    fullVP.X = fullVP.Y = 0;
    fullVP.Width = g_bbW;
    fullVP.Height = g_bbH;
    fullVP.MinZ = 0.0f;
    fullVP.MaxZ = 1.0f;
    CALLV(dev, VT_DEV_SETVIEWPORT, PFN_SetViewport)(dev, &fullVP);
    CALLV(dev, VT_DEV_CLEAR, PFN_Clear)(dev, 0, NULL, D3DCLEAR_TARGET,
                                        0xFF000000, 1.0f, 0);

    CALLV(dev, VT_DEV_STRETCHRECT, PFN_StretchRect)(
        dev, g_mid, NULL, back, &g_dest,
        g_cfgFilter == FILTER_POINT ? D3DTEXF_POINT : D3DTEXF_LINEAR);

    if (haveVP)
        CALLV(dev, VT_DEV_SETVIEWPORT, PFN_SetViewport)(dev, &oldVP);
    if (oldRT) {
        CALLV(dev, VT_DEV_SETRENDERTARGET, PFN_SetRenderTarget)(dev, 0, oldRT);
        CALLV(oldRT, 2, PFN_Release)(oldRT);
    }
    CALLV(back, 2, PFN_Release)(back);

    if (g_frames < 3) {
        LOG("frame %d: engine asked src=%s dst=%s; we present %ux%u into "
            "(%ld,%ld)-(%ld,%ld) of %ux%u",
            g_frames,
            src ? "rect" : "full", dst ? "rect" : "full",
            g_srcW, g_srcH, g_dest.left, g_dest.top, g_dest.right,
            g_dest.bottom, g_bbW, g_bbH);
        g_frames++;
    }

    if (g_tick < 600 && (g_tick % 60) == 0)
        hook_cursor();
    g_tick++;

    return g_origPresent(dev, NULL, NULL, NULL, NULL);
}

static HRESULT WINAPI My_Present(void *dev, const RECT *src, const RECT *dst,
                                 HWND wnd, const void *dirty)
{
    HRESULT hr = present_frame(dev, src, dst, wnd, dirty);

    limit_frames();
    measure_rate();
    return hr;
}

static void setup(void *dev, PRESENT_PARAMS *pp)
{
    apply_window_mode();
    compute_dest(g_bbW, g_bbH, &g_dest);
    g_dumpFormat = pp->BackBufferFormat;
    create_surfaces(dev, pp->BackBufferFormat);
    probe_init(dev, pp->BackBufferFormat);
    hook_cursor();
    LOG("%s, %s: engine renders %ux%u; presenting into (%ld,%ld)-(%ld,%ld) of %ux%u",
        g_cfgWindowed ? "windowed" : "borderless fullscreen",
        g_cfgRuntime ? "runtime stretch" : "proxy blit",
        g_srcW, g_srcH, g_dest.left, g_dest.top, g_dest.right, g_dest.bottom,
        g_bbW, g_bbH);
}

static void enlarge(PRESENT_PARAMS *pp, HWND hwnd)
{
    RECT mr;
    UINT w, h;

    g_srcW = pp->BackBufferWidth;
    g_srcH = pp->BackBufferHeight;

    if (g_cfgWindowed) {
        windowed_size(hwnd, &w, &h);
    } else {
        monitor_size(hwnd, &mr);
        w = (UINT)(mr.right - mr.left);
        h = (UINT)(mr.bottom - mr.top);
        if (g_cfgWidth > 0 && g_cfgHeight > 0) {
            w = (UINT)g_cfgWidth;
            h = (UINT)g_cfgHeight;
        }
    }
    if (w <= pp->BackBufferWidth || h <= pp->BackBufferHeight) {
        LOG("target %ux%u is no larger than the game's %ux%u - left alone",
            w, h, pp->BackBufferWidth, pp->BackBufferHeight);
        g_active = FALSE;
        return;
    }

    g_bbW = w;
    g_bbH = h;
    g_active = TRUE;

    if (g_cfgRuntime) {
        LOG("method: runtime stretch (back buffer left at %ux%u)", g_srcW, g_srcH);
        return;
    }

    pp->BackBufferWidth = w;
    pp->BackBufferHeight = h;
    pp->Windowed = TRUE;
}

static HRESULT WINAPI My_Reset(void *dev, PRESENT_PARAMS *pp)
{
    HRESULT hr;

    release_surfaces();
    if (pp && g_cfgEnabled)
        enlarge(pp, g_hwnd);
    hr = g_origReset(dev, pp);
    LOG("Reset -> 0x%08lX", (unsigned long)hr);
    if (SUCCEEDED(hr) && pp && g_active && g_hwnd)
        setup(dev, pp);
    return hr;
}

typedef void    (WINAPI *PFN_SetCursorPosition)(void *, INT, INT, DWORD);
typedef BOOL    (WINAPI *PFN_ShowCursorD3D)(void *, BOOL);
typedef HRESULT (WINAPI *PFN_SetCursorProperties)(void *, UINT, UINT, void *);

static PFN_SetCursorPosition   g_origSetCursorPosition;
static PFN_ShowCursorD3D       g_origShowCursorD3D;
static PFN_SetCursorProperties g_origSetCursorProperties;

static void WINAPI My_SetCursorPosition(void *dev, INT x, INT y, DWORD flags)
{
    INT mx = x, my = y;

    if (g_active && g_srcW && g_srcH) {
        LONG dw = g_dest.right - g_dest.left;
        LONG dh = g_dest.bottom - g_dest.top;

        mx = (INT)(g_dest.left + ((LONGLONG)x * dw) / (LONG)g_srcW);
        my = (INT)(g_dest.top + ((LONGLONG)y * dh) / (LONG)g_srcH);
    }
    if (g_cfgDiag)
        LOG("SetCursorPosition(%d,%d) -> (%d,%d)", x, y, mx, my);
    g_origSetCursorPosition(dev, mx, my, flags);
}

static BOOL WINAPI My_ShowCursorD3D(void *dev, BOOL show)
{
    if (g_cfgDiag)
        LOG("device ShowCursor(%d)", show);
    return g_origShowCursorD3D(dev, show);
}

static HRESULT WINAPI My_SetCursorProperties(void *dev, UINT hx, UINT hy, void *bmp)
{
    if (g_cfgDiag)
        LOG("SetCursorProperties(hotspot %u,%u, surface %p) - game is using the "
            "D3D hardware cursor", hx, hy, bmp);
    return g_origSetCursorProperties(dev, hx, hy, bmp);
}

static void patch_vtable(void *obj, int slot, void *fn, void **orig)
{
    void **vtbl = *(void ***)obj;
    DWORD prot;

    if (!VirtualProtect(&vtbl[slot], sizeof(void *), PAGE_EXECUTE_READWRITE, &prot))
        return;
    if (orig && !*orig)
        *orig = vtbl[slot];
    vtbl[slot] = fn;
    VirtualProtect(&vtbl[slot], sizeof(void *), prot, &prot);
}

static HRESULT WINAPI My_CreateDevice(void *self, UINT adapter, DWORD devtype,
                                      HWND hFocus, DWORD flags,
                                      PRESENT_PARAMS *pp, void **ppdev)
{
    HRESULT hr;

    if (pp) {
        RECT rr = { 0, 0, 0, 0 }, cr = { 0, 0, 0, 0 };

        g_hwnd = pp->hDeviceWindow ? pp->hDeviceWindow : hFocus;
        g_root = g_hwnd ? GetAncestor(g_hwnd, GA_ROOT) : NULL;
        if (!g_root)
            g_root = g_hwnd;
        if (g_hwnd)
            GetClientRect(g_hwnd, &cr);
        if (g_root)
            GetWindowRect(g_root, &rr);
        LOG("CreateDevice: %ux%u windowed=%d swap=%lu interval=%u",
            pp->BackBufferWidth, pp->BackBufferHeight, pp->Windowed,
            pp->SwapEffect, pp->PresentationInterval);
        LOG("  device window %p (client %ldx%ld), frame %p (%ldx%ld)%s",
            (void *)g_hwnd, cr.right, cr.bottom, (void *)g_root,
            rr.right - rr.left, rr.bottom - rr.top,
            g_hwnd == g_root ? "" : " - device is a child");
        if (g_cfgEnabled && g_hwnd)
            enlarge(pp, g_hwnd);
    }

    hr = g_origCreateDevice(self, adapter, devtype, hFocus, flags, pp, ppdev);
    LOG("  -> 0x%08lX device=%p", (unsigned long)hr,
        (ppdev && *ppdev) ? *ppdev : NULL);

    if (SUCCEEDED(hr) && ppdev && *ppdev) {
        patch_vtable(*ppdev, VT_DEV_PRESENT, (void *)My_Present, (void **)&g_origPresent);
        patch_vtable(*ppdev, VT_DEV_RESET,   (void *)My_Reset,   (void **)&g_origReset);
        patch_vtable(*ppdev, VT_DEV_SETCURSORPROPS, (void *)My_SetCursorProperties,
                     (void **)&g_origSetCursorProperties);
        patch_vtable(*ppdev, VT_DEV_SETCURSORPOS, (void *)My_SetCursorPosition,
                     (void **)&g_origSetCursorPosition);
        patch_vtable(*ppdev, VT_DEV_SHOWCURSOR, (void *)My_ShowCursorD3D,
                     (void **)&g_origShowCursorD3D);
        if (g_active && g_hwnd && pp)
            setup(*ppdev, pp);
    }
    return hr;
}

extern "C" __declspec(dllexport) void *WINAPI Direct3DCreate9(UINT sdkVersion)
{
    void *d3d;

    load_real();
    if (!g_fn[0]) {
        LOG("FATAL: real Direct3DCreate9 missing");
        return NULL;
    }
    d3d = ((PFN_Direct3DCreate9)g_fn[0])(sdkVersion);
    if (d3d)
        patch_vtable(d3d, VT_D3D9_CREATEDEVICE, (void *)My_CreateDevice,
                     (void **)&g_origCreateDevice);
    return d3d;
}

/* Volatile and read from DllMain, or the linker strips it. */
static volatile const char g_golde_marker[] = GOLDE_DLL_MARKER;

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        log_open();
        config_load();
        LOG("%s v%s", GOLDE_NAME, GOLDE_VERSION);
        LOG("build: %s", (const char *)g_golde_marker);
        LOG("config: enabled=%d method=%s mode=%s size=%dx%d filter=%d "
            "stretch=%d cap=%d bg=%d",
            g_cfgEnabled, g_cfgRuntime ? "runtime" : "proxy",
            g_cfgWindowed ? "windowed" : "fullscreen",
            g_cfgWidth, g_cfgHeight, g_cfgFilter, g_cfgStretch, g_cfgFrameCap,
            g_cfgBackground);

        if (g_cfgFrameCap > 0)
            timeBeginPeriod(1);
        load_real();
    }
    return TRUE;
}
