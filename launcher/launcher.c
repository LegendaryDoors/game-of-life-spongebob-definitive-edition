/* Setup and settings launcher. Keep strings ASCII - the API calls are ANSI. */
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "resource.h"
#include "version.h"

#define PATCH_DLL   GOLDE_PATCH_DLL
#define INI_NAME    GOLDE_INI
#define APP_VERSION GOLDE_VERSION

#define COL_BG_TOP    RGB(203, 233, 242)
#define COL_BG_BOT    RGB(170, 214, 232)

#define COL_HAZE      RGB(150, 208, 224)

#define COL_EDGE      RGB(126, 176, 198)

#define COL_INK       RGB( 34,  44,  54)
#define COL_INK_SOFT  RGB(112, 126, 138)
#define COL_INK_PAGE  RGB( 40,  86, 108)
#define COL_INK_FAINT RGB(176, 186, 194)

#define COL_CARD      RGB(255, 255, 255)
#define COL_RULE      RGB(223, 230, 236)
#define COL_TRACK     RGB(236, 241, 245)
#define COL_TRACK_HOV RGB(223, 235, 244)

#define COL_ACCENT    RGB(  0, 158, 224)
#define COL_ACCENT_DK RGB(  0, 118, 174)
#define COL_ACCENT_LT RGB( 56, 186, 240)

#define COL_GO        RGB(252, 190,  38)
#define COL_GO_DK     RGB(226, 162,  16)
#define COL_GO_LT     RGB(255, 208,  84)
#define COL_GO_INK    RGB( 74,  46,   4)

#define COL_OK        RGB( 22, 148,  88)
#define COL_OK_PAGE   RGB( 14, 112,  66)
#define COL_WARN      RGB(176,  44,  24)

static HFONT  g_h1_font,
              g_label_font,
              g_seg_font,
              g_cap_font,
              g_btn_font,
              g_play_font,
              g_body_font;
static HBRUSH g_edit_brush, g_track_brush;
static int    g_hover_id;
static int    g_hover_seg = -1;

#define HERO_H(w) (((w) * ART_SRC_H) / ART_SRC_W)

static HBITMAP g_bg_bmp;
static int     g_bg_w, g_bg_h;
static void ensure_bg(HWND dlg, HDC ref, int w, int h);

static double g_scale = 1.0;

#define SC(v) ((int)((v) * g_scale + ((v) < 0 ? -0.5 : 0.5)))

enum { MODE_SETUP = 0, MODE_LAUNCH = 1 };
static int  g_mode;
static char g_gamedir[MAX_PATH];
static char g_pending_msg[160];

#define RES_QUIT      0
#define RES_LAUNCHER  1
#define RES_SETUP     2

static const int g_check_ids[] = {
    IDC_AUTORES, IDC_STRETCH, IDC_CAP60, IDC_ENABLED, IDC_BACKGROUND
};
#define NCHECK ((int)(sizeof(g_check_ids) / sizeof(g_check_ids[0])))
static BOOL g_check_state[NCHECK];

static int check_index(int id)
{
    int i;
    for (i = 0; i < NCHECK; i++)
        if (g_check_ids[i] == id)
            return i;
    return -1;
}

static BOOL is_checked(int id)
{
    int i = check_index(id);
    return i >= 0 && g_check_state[i];
}

static void set_checked(HWND dlg, int id, BOOL on)
{
    int i = check_index(id);
    HWND c;
    if (i < 0)
        return;
    g_check_state[i] = on;
    c = GetDlgItem(dlg, id);
    if (c)
        InvalidateRect(c, NULL, FALSE);
}

static const int g_mode_ids[] = { IDC_MODE_FULL, IDC_MODE_WIND };
static const int g_filt_ids[] = { IDC_FILT_SHARP, IDC_FILT_LINEAR, IDC_FILT_POINT };
#define NMODE ((int)(sizeof(g_mode_ids) / sizeof(g_mode_ids[0])))
#define NFILT ((int)(sizeof(g_filt_ids) / sizeof(g_filt_ids[0])))
static int g_mode_sel = IDC_MODE_FULL;
static int g_filt_sel = IDC_FILT_SHARP;

static const int *radio_group(int id, int *count, int **sel)
{
    int i;
    for (i = 0; i < NMODE; i++)
        if (g_mode_ids[i] == id) { *count = NMODE; *sel = &g_mode_sel; return g_mode_ids; }
    for (i = 0; i < NFILT; i++)
        if (g_filt_ids[i] == id) { *count = NFILT; *sel = &g_filt_sel; return g_filt_ids; }
    return NULL;
}

static BOOL is_radio(int id)
{
    int n; int *sel;
    return radio_group(id, &n, &sel) != NULL;
}

static BOOL radio_on(int id)
{
    int n; int *sel;
    return radio_group(id, &n, &sel) != NULL && *sel == id;
}

static void set_radio(HWND dlg, int id)
{
    int n, i; int *sel;
    const int *ids = radio_group(id, &n, &sel);
    if (!ids)
        return;
    *sel = id;
    for (i = 0; i < n; i++) {
        HWND c = GetDlgItem(dlg, ids[i]);
        if (c)
            InvalidateRect(c, NULL, FALSE);
    }
}

static const int g_btn_ids[] = {
    IDC_BROWSE, IDC_INSTALL, IDC_UNINSTALL, IDC_HELPBTN, IDC_ABOUT,
    IDC_PLAY, IDC_SETUPBTN,
    IDC_ROW_MODE, IDC_ROW_RES,
    IDC_ROW_SHAPE, IDC_ROW_CAP, IDC_ROW_BG, IDC_ROW_PATCH
};
#define NBTN ((int)(sizeof(g_btn_ids) / sizeof(g_btn_ids[0])))

static void join_path(char *dst, size_t n, const char *dir, const char *name);
static BOOL file_exists(const char *path);

#define ART_TITLE_TEX "game\\textures\\tex_menu_titlebg.jpg"

#define ART_SRC_X     0
#define ART_SRC_Y     8
#define ART_SRC_W     800
#define ART_SRC_H     131
#define ART_FADE_FROM 133

typedef struct {
    UINT32 GdiplusVersion;
    void  *DebugEventCallback;
    BOOL   SuppressBackgroundThread;
    BOOL   SuppressExternalCodecs;
} GpStartupInput;

typedef int  (WINAPI *PFN_Startup)(ULONG_PTR *, const GpStartupInput *, void *);
typedef void (WINAPI *PFN_Shutdown)(ULONG_PTR);
typedef int  (WINAPI *PFN_FromFile)(const WCHAR *, void **);
typedef int  (WINAPI *PFN_ToHBITMAP)(void *, HBITMAP *, DWORD);
typedef int  (WINAPI *PFN_Dispose)(void *);
typedef int  (WINAPI *PFN_GFromHDC)(HDC, void **);
typedef int  (WINAPI *PFN_GDelete)(void *);
typedef int  (WINAPI *PFN_SetSmooth)(void *, int);
typedef int  (WINAPI *PFN_PathNew)(int, void **);
typedef int  (WINAPI *PFN_PathDelete)(void *);
typedef int  (WINAPI *PFN_PathArcI)(void *, int, int, int, int, float, float);
typedef int  (WINAPI *PFN_PathClose)(void *);
typedef int  (WINAPI *PFN_BrushNew)(DWORD, void **);
typedef int  (WINAPI *PFN_BrushDelete)(void *);
typedef int  (WINAPI *PFN_FillPath)(void *, void *, void *);
typedef int  (WINAPI *PFN_PenNew)(DWORD, float, int, void **);
typedef int  (WINAPI *PFN_PenDelete)(void *);
typedef int  (WINAPI *PFN_DrawPath)(void *, void *, void *);

typedef struct { int X, Y, Width, Height; } GpRectI;

typedef int  (WINAPI *PFN_LineBrush)(const GpRectI *, DWORD, DWORD, int, int, void **);
typedef int  (WINAPI *PFN_FillRectI)(void *, void *, int, int, int, int);

static struct {
    HMODULE        mod;
    ULONG_PTR      token;
    BOOL           ok;
    PFN_Startup    Startup;
    PFN_Shutdown   Shutdown;
    PFN_FromFile   FromFile;
    PFN_ToHBITMAP  ToHBITMAP;
    PFN_Dispose    Dispose;
    PFN_GFromHDC   GFromHDC;
    PFN_GDelete    GDelete;
    PFN_SetSmooth  SetSmooth;
    PFN_PathNew    PathNew;
    PFN_PathDelete PathDelete;
    PFN_PathArcI   PathArcI;
    PFN_PathClose  PathClose;
    PFN_BrushNew   BrushNew;
    PFN_BrushDelete BrushDelete;
    PFN_FillPath   FillPath;
    PFN_PenNew     PenNew;
    PFN_PenDelete  PenDelete;
    PFN_DrawPath   DrawPath;
    PFN_LineBrush  LineBrush;
    PFN_FillRectI  FillRectI;
} G;

#define GP_SYM(name) \
    G.name = (PFN_##name)GetProcAddress(G.mod, "Gdip" #name)

static BOOL gdip_init(void)
{
    GpStartupInput si;

    if (G.ok) return TRUE;
    if (G.mod) return FALSE;

    G.mod = LoadLibraryA("gdiplus.dll");
    if (!G.mod) return FALSE;

    G.Startup     = (PFN_Startup)    GetProcAddress(G.mod, "GdiplusStartup");
    G.Shutdown    = (PFN_Shutdown)   GetProcAddress(G.mod, "GdiplusShutdown");
    G.FromFile    = (PFN_FromFile)   GetProcAddress(G.mod, "GdipCreateBitmapFromFile");
    G.ToHBITMAP   = (PFN_ToHBITMAP)  GetProcAddress(G.mod, "GdipCreateHBITMAPFromBitmap");
    G.Dispose     = (PFN_Dispose)    GetProcAddress(G.mod, "GdipDisposeImage");
    G.GFromHDC    = (PFN_GFromHDC)   GetProcAddress(G.mod, "GdipCreateFromHDC");
    G.GDelete     = (PFN_GDelete)    GetProcAddress(G.mod, "GdipDeleteGraphics");
    G.SetSmooth   = (PFN_SetSmooth)  GetProcAddress(G.mod, "GdipSetSmoothingMode");
    G.PathNew     = (PFN_PathNew)    GetProcAddress(G.mod, "GdipCreatePath");
    G.PathDelete  = (PFN_PathDelete) GetProcAddress(G.mod, "GdipDeletePath");
    G.PathArcI    = (PFN_PathArcI)   GetProcAddress(G.mod, "GdipAddPathArcI");
    G.PathClose   = (PFN_PathClose)  GetProcAddress(G.mod, "GdipClosePathFigure");
    G.BrushNew    = (PFN_BrushNew)   GetProcAddress(G.mod, "GdipCreateSolidFill");
    G.BrushDelete = (PFN_BrushDelete)GetProcAddress(G.mod, "GdipDeleteBrush");
    G.FillPath    = (PFN_FillPath)   GetProcAddress(G.mod, "GdipFillPath");
    G.PenNew      = (PFN_PenNew)     GetProcAddress(G.mod, "GdipCreatePen1");
    G.PenDelete   = (PFN_PenDelete)  GetProcAddress(G.mod, "GdipDeletePen");
    G.DrawPath    = (PFN_DrawPath)   GetProcAddress(G.mod, "GdipDrawPath");
    G.LineBrush   = (PFN_LineBrush)  GetProcAddress(G.mod, "GdipCreateLineBrushFromRectI");
    G.FillRectI   = (PFN_FillRectI)  GetProcAddress(G.mod, "GdipFillRectangleI");

    if (!G.Startup || !G.Shutdown || !G.GFromHDC || !G.GDelete || !G.SetSmooth ||
        !G.PathNew || !G.PathDelete || !G.PathArcI || !G.PathClose ||
        !G.BrushNew || !G.BrushDelete || !G.FillPath || !G.PenNew ||
        !G.PenDelete || !G.DrawPath)
        return FALSE;

    memset(&si, 0, sizeof(si));
    si.GdiplusVersion = 1;
    if (G.Startup(&G.token, &si, NULL) != 0)
        return FALSE;

    G.ok = TRUE;
    return TRUE;
}

static void gdip_shutdown(void)
{
    if (G.ok && G.Shutdown) G.Shutdown(G.token);
    G.ok = FALSE;
    G.token = 0;
}

static DWORD gp_argb(COLORREF c, int alpha)
{
    return ((DWORD)(alpha & 0xFF) << 24) | ((DWORD)GetRValue(c) << 16) |
           ((DWORD)GetGValue(c) << 8) | (DWORD)GetBValue(c);
}

static void gp_round_path(void *path, int x, int y, int w, int h, int r)
{
    int d;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r < 1) r = 1;
    d = r * 2;
    G.PathArcI(path, x,         y,         d, d, 180.0f, 90.0f);
    G.PathArcI(path, x + w - d, y,         d, d, 270.0f, 90.0f);
    G.PathArcI(path, x + w - d, y + h - d, d, d,   0.0f, 90.0f);
    G.PathArcI(path, x,         y + h - d, d, d,  90.0f, 90.0f);
    G.PathClose(path);
}

static HBITMAP g_art;
static int     g_art_w, g_art_h;
static char    g_art_dir[MAX_PATH];
static BOOL    g_art_tried;

static void art_free(void)
{
    if (g_art) { DeleteObject(g_art); g_art = NULL; }
    g_art_w = g_art_h = 0;
    g_art_dir[0] = 0;
    g_art_tried = FALSE;
}

static BOOL art_load(const char *dir)
{
    void *bmp = NULL;
    WCHAR wpath[MAX_PATH];
    char path[MAX_PATH];
    HBITMAP hb = NULL;

    if (!dir || !dir[0])
        return FALSE;
    if (g_art && _stricmp(dir, g_art_dir) == 0)
        return TRUE;
    if (g_art_tried && _stricmp(dir, g_art_dir) == 0)
        return FALSE;

    art_free();
    _snprintf(g_art_dir, sizeof(g_art_dir), "%s", dir);
    g_art_dir[sizeof(g_art_dir) - 1] = 0;
    g_art_tried = TRUE;

    join_path(path, sizeof(path), dir, ART_TITLE_TEX);
    if (!file_exists(path))
        return FALSE;

    if (!gdip_init() || !G.FromFile || !G.ToHBITMAP || !G.Dispose)
        return FALSE;

    MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH);
    if (G.FromFile(wpath, &bmp) == 0 && bmp) {
        if (G.ToHBITMAP(bmp, &hb, 0xFF000000) != 0)
            hb = NULL;
        G.Dispose(bmp);
    }

    if (!hb)
        return FALSE;
    {
        BITMAP bi;
        if (!GetObjectA(hb, sizeof(bi), &bi) || bi.bmWidth < ART_SRC_W ||
            bi.bmHeight < ART_SRC_Y + ART_SRC_H) {
            DeleteObject(hb);
            return FALSE;
        }
        g_art = hb;
        g_art_w = bi.bmWidth;
        g_art_h = bi.bmHeight;
    }
    return TRUE;
}

static void join_path(char *dst, size_t n, const char *dir, const char *name)
{
    size_t len = strlen(dir);
    if (len && (dir[len - 1] == '\\' || dir[len - 1] == '/'))
        _snprintf(dst, n, "%s%s", dir, name);
    else
        _snprintf(dst, n, "%s\\%s", dir, name);
    dst[n - 1] = 0;
}

static BOOL file_exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void launcher_dir(char *out, size_t n)
{
    char *slash;
    GetModuleFileNameA(NULL, out, (DWORD)n);
    out[n - 1] = 0;
    slash = strrchr(out, '\\');
    if (slash)
        *slash = 0;
}

static BOOL dir_has_game(const char *dir)
{
    char probe[MAX_PATH];

    join_path(probe, sizeof(probe), dir, GOLDE_GAME_EXE);
    if (!file_exists(probe))
        return FALSE;
    join_path(probe, sizeof(probe), dir, GOLDE_GAME_DATA);
    return file_exists(probe);
}

#define SEARCH_MAX_DEPTH 4
#define SEARCH_MAX_DIRS  600

static BOOL find_game_dir(const char *root, char *out, size_t n,
                          int depth, int *budget)
{
    char probe[MAX_PATH], pattern[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    BOOL found = FALSE;

    if (dir_has_game(root)) {
        _snprintf(out, n, "%s", root);
        out[n - 1] = 0;
        return TRUE;
    }
    if (depth <= 0 || (*budget)-- <= 0)
        return FALSE;

    join_path(pattern, sizeof(pattern), root, "*");
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.')
            continue;
        join_path(probe, sizeof(probe), root, fd.cFileName);
        found = find_game_dir(probe, out, n, depth - 1, budget);
    } while (!found && FindNextFileA(h, &fd));
    FindClose(h);
    return found;
}

static BOOL locate_game(const char *start, char *out, size_t n)
{
    int budget = SEARCH_MAX_DIRS;
    return find_game_dir(start, out, n, SEARCH_MAX_DEPTH, &budget);
}

#define PREF_KEY "Software\\Game of Life SpongeBob Definitive Edition"
#define PREF_VAL "GameDir"

static void save_gamedir_pref(const char *dir)
{
    HKEY k;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, PREF_KEY, 0, NULL, 0, KEY_SET_VALUE,
                        NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(k, PREF_VAL, 0, REG_SZ,
                       (const BYTE *)dir, (DWORD)strlen(dir) + 1);
        RegCloseKey(k);
    }
}

static BOOL load_gamedir_pref(char *out, DWORD n)
{
    HKEY k;
    DWORD type = REG_SZ, sz = n;
    BOOL ok = FALSE;

    out[0] = 0;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, PREF_KEY, 0, KEY_QUERY_VALUE, &k)
            == ERROR_SUCCESS) {
        ok = RegQueryValueExA(k, PREF_VAL, NULL, &type,
                              (BYTE *)out, &sz) == ERROR_SUCCESS;
        RegCloseKey(k);
    }
    out[n - 1] = 0;
    return ok && out[0] != 0;
}

static void forget_gamedir_pref(void)
{
    HKEY k;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, PREF_KEY, 0, KEY_SET_VALUE, &k)
            == ERROR_SUCCESS) {
        RegDeleteValueA(k, PREF_VAL);
        RegCloseKey(k);
    }

    RegDeleteKeyA(HKEY_CURRENT_USER, PREF_KEY);
}

static void get_gamedir(HWND dlg, char *out, size_t n)
{
    if (GetDlgItem(dlg, IDC_GAMEDIR)) {
        GetDlgItemTextA(dlg, IDC_GAMEDIR, out, (int)n);
        return;
    }
    _snprintf(out, n, "%s", g_gamedir);
    out[n - 1] = 0;
}

static BOOL is_wine(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version") != NULL;
}

static BOOL set_wine_override(void)
{
    char key[MAX_PATH];
    HKEY h;
    LONG r;

    _snprintf(key, sizeof(key),
              "Software\\Wine\\AppDefaults\\%s\\DllOverrides", GOLDE_GAME_EXE);
    key[sizeof(key) - 1] = 0;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_SET_VALUE,
                        NULL, &h, NULL) != ERROR_SUCCESS)
        return FALSE;
    r = RegSetValueExA(h, "d3d9", 0, REG_SZ, (const BYTE *)"native,builtin", 15);
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

/* d3d9.dll is a contended name, so identify by marker, never by filename. */
static BOOL file_has_marker(const char *path, const char *marker)
{
    HANDLE f;
    DWORD size, got = 0;
    char *buf;
    BOOL hit = FALSE;
    size_t mlen = strlen(marker), i;

    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size > 8u * 1024 * 1024) {
        CloseHandle(f);
        return FALSE;
    }
    buf = (char *)malloc(size + 1);
    if (!buf) {
        CloseHandle(f);
        return FALSE;
    }
    ReadFile(f, buf, size, &got, NULL);
    CloseHandle(f);

    if (got >= mlen)
        for (i = 0; i + mlen <= got; i++)
            if (memcmp(buf + i, marker, mlen) == 0) { hit = TRUE; break; }
    free(buf);
    return hit;
}

static BOOL write_resource(int id, const char *path)
{
    HRSRC res = FindResourceA(NULL, MAKEINTRESOURCEA(id), RT_RCDATA);
    HGLOBAL mem;
    const void *data;
    DWORD size, written = 0;
    HANDLE h;
    BOOL ok;

    if (!res)
        return FALSE;
    size = SizeofResource(NULL, res);
    mem  = LoadResource(NULL, res);
    if (!size || !mem)
        return FALSE;
    data = LockResource(mem);
    if (!data)
        return FALSE;

    h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;
    ok = WriteFile(h, data, size, &written, NULL) && written == size;
    CloseHandle(h);
    if (!ok)
        DeleteFileA(path);
    return ok;
}

static BOOL patch_installed_in(const char *dir)
{
    char path[MAX_PATH];
    join_path(path, sizeof(path), dir, PATCH_DLL);
    return file_exists(path) && file_has_marker(path, GOLDE_DLL_MARKER);
}

static BOOL foreign_dll_in(const char *dir)
{
    char path[MAX_PATH];
    join_path(path, sizeof(path), dir, PATCH_DLL);
    return file_exists(path) && !file_has_marker(path, GOLDE_DLL_MARKER);
}

static BOOL install_path_fits(const char *dir)
{
    return strlen(dir) + 1 + strlen(GOLDE_LAUNCHER_EXE) + 1 <= MAX_PATH;
}

static BOOL install_launcher_copy(const char *dir)
{
    char src[MAX_PATH], dst[MAX_PATH];

    GetModuleFileNameA(NULL, src, sizeof(src));
    src[sizeof(src) - 1] = 0;
    join_path(dst, sizeof(dst), dir, GOLDE_LAUNCHER_EXE);
    if (_stricmp(src, dst) == 0)
        return TRUE;
    return CopyFileA(src, dst, FALSE);
}

static void set_label(HWND dlg, int id, const char *s)
{
    HWND c = GetDlgItem(dlg, id);
    RECT r;
    if (!c) return;
    SetWindowTextA(c, s);
    GetWindowRect(c, &r);
    MapWindowPoints(NULL, dlg, (POINT *)&r, 2);
    RedrawWindow(dlg, &r, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

#define TIMER_HOVER  1
#define TIMER_MSG    2
#define MSG_HOLD_MS  5000

static char g_sticky_msg[256];

static void set_msg(HWND dlg, const char *s)
{
    _snprintf(g_sticky_msg, sizeof(g_sticky_msg), "%s", s ? s : "");
    g_sticky_msg[sizeof(g_sticky_msg) - 1] = 0;
    KillTimer(dlg, TIMER_MSG);
    set_label(dlg, IDC_MSG, g_sticky_msg);
}

static void set_msg_timed(HWND dlg, const char *s)
{
    set_label(dlg, IDC_MSG, s ? s : "");
    KillTimer(dlg, TIMER_MSG);
    SetTimer(dlg, TIMER_MSG, MSG_HOLD_MS, NULL);
}

static void msg_expire(HWND dlg)
{
    KillTimer(dlg, TIMER_MSG);
    set_label(dlg, IDC_MSG, g_sticky_msg);
}

static void desktop_res(int *w, int *h)
{
    DEVMODEA dm;
    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm) &&
        dm.dmPelsWidth >= 640 && dm.dmPelsHeight >= 480) {
        *w = (int)dm.dmPelsWidth;
        *h = (int)dm.dmPelsHeight;
        return;
    }
    { int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
      if (sx >= 640 && sy >= 480) { *w = sx; *h = sy; return; } }
    *w = 1920; *h = 1080;
}

static BOOL g_loading;

static void load_settings(HWND dlg, const char *dir)
{
    char ini[MAX_PATH], buf[32];
    UINT w, h, en, stretch, cap, bg;

    g_loading = TRUE;
    join_path(ini, sizeof(ini), dir, INI_NAME);

    w       = GetPrivateProfileIntA("Display", "Width", 0, ini);
    h       = GetPrivateProfileIntA("Display", "Height", 0, ini);
    en      = GetPrivateProfileIntA("Display", "Enabled", 1, ini);
    stretch = GetPrivateProfileIntA("Display", "Stretch", 0, ini);
    cap     = GetPrivateProfileIntA("Display", "FrameCap", 0, ini);
    bg      = GetPrivateProfileIntA("Display", "RunInBackground", 0, ini);

    GetPrivateProfileStringA("Display", "Mode", "fullscreen", buf, sizeof(buf), ini);
    g_mode_sel = (_stricmp(buf, "windowed") == 0) ? IDC_MODE_WIND : IDC_MODE_FULL;

    GetPrivateProfileStringA("Display", "Filter", "sharp", buf, sizeof(buf), ini);
    g_filt_sel = (_stricmp(buf, "point") == 0) ? IDC_FILT_POINT : IDC_FILT_SHARP;

    set_checked(dlg, IDC_AUTORES, (w == 0 || h == 0));
    {   int dw, dh; desktop_res(&dw, &dh);
        SetDlgItemInt(dlg, IDC_WIDTH,  w ? w : (UINT)dw, FALSE);
        SetDlgItemInt(dlg, IDC_HEIGHT, h ? h : (UINT)dh, FALSE); }
    set_checked(dlg, IDC_STRETCH, stretch != 0);
    set_checked(dlg, IDC_CAP60,   cap != 0);
    set_checked(dlg, IDC_ENABLED, en != 0);
    set_checked(dlg, IDC_BACKGROUND, bg != 0);

    InvalidateRect(dlg, NULL, TRUE);
    g_loading = FALSE;
}

static BOOL save_settings(HWND dlg, const char *dir)
{
    char ini[MAX_PATH], buf[32];
    UINT w = 0, h = 0;
    BOOL ok = TRUE;

    if (!dir[0]) { set_msg(dlg, "Pick your game folder first."); return FALSE; }

    if (!GetDlgItem(dlg, IDC_ROW_MODE))
        return TRUE;
    join_path(ini, sizeof(ini), dir, INI_NAME);

    if (!is_checked(IDC_AUTORES)) {
        w = GetDlgItemInt(dlg, IDC_WIDTH,  NULL, FALSE);
        h = GetDlgItemInt(dlg, IDC_HEIGHT, NULL, FALSE);
    }

    /* Edits keys in place, so the ini's comments survive a save. */
    _snprintf(buf, sizeof(buf), "%u", w);
    ok &= (BOOL)WritePrivateProfileStringA("Display", "Width", buf, ini);
    _snprintf(buf, sizeof(buf), "%u", h);
    WritePrivateProfileStringA("Display", "Height", buf, ini);

    WritePrivateProfileStringA("Display", "Mode",
        (g_mode_sel == IDC_MODE_WIND) ? "windowed" : "fullscreen", ini);
    WritePrivateProfileStringA("Display", "Filter",
        (g_filt_sel == IDC_FILT_LINEAR) ? "linear" :
        (g_filt_sel == IDC_FILT_POINT)  ? "point"  : "sharp", ini);
    WritePrivateProfileStringA("Display", "Stretch",
        is_checked(IDC_STRETCH) ? "1" : "0", ini);
    WritePrivateProfileStringA("Display", "FrameCap",
        is_checked(IDC_CAP60) ? "60" : "0", ini);
    WritePrivateProfileStringA("Display", "RunInBackground",
        is_checked(IDC_BACKGROUND) ? "1" : "0", ini);
    WritePrivateProfileStringA("Display", "Enabled",
        is_checked(IDC_ENABLED) ? "1" : "0", ini);
    return ok;
}

static const int g_flow_ids[] = {
    IDC_ROW_SHAPE, IDC_ROW_CAP, IDC_ROW_BG, IDC_ROW_PATCH,
    IDC_SETUPBTN, IDC_ABOUT, IDC_PLAY, IDC_MSG
};
#define NFLOW ((int)(sizeof(g_flow_ids) / sizeof(g_flow_ids[0])))
static POINT g_flow_base[NFLOW];
static int   g_flow_ready, g_row_px, g_flow_applied;

static void flow_capture(HWND dlg)
{
    int i;

    g_flow_ready = 0;

    if (!GetDlgItem(dlg, IDC_WIDTH))
        return;

    /* Measure two real rows: MapDialogRect ignores the zoom. */
    {   HWND r1 = GetDlgItem(dlg, IDC_ROW_SHAPE);
        HWND r2 = GetDlgItem(dlg, IDC_ROW_CAP);
        g_row_px = 0;
        if (r1 && r2) {
            RECT a, b;
            GetWindowRect(r1, &a);
            GetWindowRect(r2, &b);
            g_row_px = b.top - a.top;
        }
        if (g_row_px <= 0) {
            RECT unit = { 0, 0, 0, 20 };
            MapDialogRect(dlg, &unit);
            g_row_px = unit.bottom;
        }
    }

    for (i = 0; i < NFLOW; i++) {
        HWND c = GetDlgItem(dlg, g_flow_ids[i]);
        RECT wr;
        POINT tl;
        g_flow_base[i].x = g_flow_base[i].y = -1;
        if (!c)
            continue;
        GetWindowRect(c, &wr);
        tl.x = wr.left; tl.y = wr.top;
        ScreenToClient(dlg, &tl);
        g_flow_base[i] = tl;
    }
    g_flow_applied = 0;
    g_flow_ready = 1;
}

static void flow_apply(HWND dlg)
{
    int shift, i;

    if (!g_flow_ready)
        return;
    shift = (GetDlgItem(dlg, IDC_WIDTH) && is_checked(IDC_AUTORES)) ? g_row_px : 0;

    for (i = 0; i < NFLOW; i++) {
        HWND c = GetDlgItem(dlg, g_flow_ids[i]);
        if (!c || g_flow_base[i].y < 0)
            continue;
        SetWindowPos(c, NULL, g_flow_base[i].x, g_flow_base[i].y - shift, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (shift != g_flow_applied) {
        RECT wr;
        GetWindowRect(dlg, &wr);
        SetWindowPos(dlg, NULL, 0, 0, wr.right - wr.left,
                     (wr.bottom - wr.top) - (shift - g_flow_applied),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        g_flow_applied = shift;
        if (g_bg_bmp) {
            DeleteObject(g_bg_bmp);
            g_bg_bmp = NULL;
            g_bg_w = g_bg_h = 0;
        }
        InvalidateRect(dlg, NULL, TRUE);
    }
}

static void show_ctl(HWND dlg, int id, BOOL show)
{
    HWND c = GetDlgItem(dlg, id);
    if (c)
        ShowWindow(c, show ? SW_SHOW : SW_HIDE);
}

static void update_enable_states(HWND dlg)
{
    BOOL autores = is_checked(IDC_AUTORES);

    if (autores) {
        int dw, dh;
        desktop_res(&dw, &dh);
        SetDlgItemInt(dlg, IDC_WIDTH,  (UINT)dw, FALSE);
        SetDlgItemInt(dlg, IDC_HEIGHT, (UINT)dh, FALSE);
    }
    show_ctl(dlg, IDC_WIDTH,      !autores);
    show_ctl(dlg, IDC_HEIGHT,     !autores);
    show_ctl(dlg, IDC_LBL_WIDTH,  !autores);
    show_ctl(dlg, IDC_LBL_HEIGHT, !autores);
    flow_apply(dlg);

    {   HWND c = GetDlgItem(dlg, IDC_ROW_RES);
        if (c) InvalidateRect(c, NULL, FALSE); }
}

static void autosave(HWND dlg)
{
    char dir[MAX_PATH];

    if (g_loading || !GetDlgItem(dlg, IDC_ROW_MODE))
        return;
    get_gamedir(dlg, dir, sizeof(dir));
    if (!dir[0])
        return;

    if (save_settings(dlg, dir))
        set_msg_timed(dlg, "Settings saved.");
    else
        set_msg(dlg, "Could not write " INI_NAME " - settings not saved.");
}

static void refresh_status(HWND dlg)
{
    char dir[MAX_PATH];
    BOOL game, patched, foreign;

    get_gamedir(dlg, dir, sizeof(dir));
    if (!dir[0]) {
        set_label(dlg, IDC_STATUS_EXE, "Game folder: (none selected)");
        set_label(dlg, IDC_STATUS_PATCH, "");
        show_ctl(dlg, IDC_UNINSTALL, FALSE);
        show_ctl(dlg, IDC_PLAY, FALSE);
        return;
    }

    game    = dir_has_game(dir);
    patched = patch_installed_in(dir);
    foreign = foreign_dll_in(dir);

    set_label(dlg, IDC_STATUS_EXE,
        game ? "Game: FOUND (" GOLDE_GAME_EXE ")"
             : "No Game of Life: SpongeBob SquarePants Edition in this folder.");

    set_label(dlg, IDC_STATUS_PATCH,
        patched ? "Patch: INSTALLED (" PATCH_DLL " v" APP_VERSION ")"
        : foreign ? "Patch: not installed - and another " PATCH_DLL " is here."
        : "Patch: not installed.");

    show_ctl(dlg, IDC_UNINSTALL, patched);

    show_ctl(dlg, IDC_PLAY, game);

    if (!GetDlgItem(dlg, IDC_STATUS_PATCH) && game) {
        if (foreign)
            set_msg(dlg, "Warning: another " PATCH_DLL " is here - the patch is "
                         "NOT active. Open Setup.");
        else if (!patched)
            set_msg(dlg, "The patch is not installed. Open Setup to install it.");
    }
}

static int  g_quiet;
static int  g_quiet_confirm;
static char g_last_error[256];

static void report_error(HWND dlg, const char *brief, const char *detail)
{
    set_msg(dlg, brief);
    _snprintf(g_last_error, sizeof(g_last_error), "%s", brief);
    g_last_error[sizeof(g_last_error) - 1] = 0;
    if (g_quiet)
        return;
    MessageBoxA(dlg, detail ? detail : brief, GOLDE_SHORT,
                MB_OK | MB_ICONWARNING);
}

static void apply_gamedir(HWND dlg, const char *dir)
{
    _snprintf(g_gamedir, sizeof(g_gamedir), "%s", dir);
    g_gamedir[sizeof(g_gamedir) - 1] = 0;
    if (GetDlgItem(dlg, IDC_GAMEDIR))
        SetDlgItemTextA(dlg, IDC_GAMEDIR, dir);

    if (dir_has_game(dir))
        save_gamedir_pref(dir);

    if (art_load(dir) && g_bg_bmp) {
        DeleteObject(g_bg_bmp);
        g_bg_bmp = NULL;
        g_bg_w = g_bg_h = 0;
        InvalidateRect(dlg, NULL, TRUE);
    }

    load_settings(dlg, dir);
    update_enable_states(dlg);
    refresh_status(dlg);
}

static BOOL resolve_gamedir(HWND dlg, char *dir, size_t n)
{
    char found[MAX_PATH];

    if (!dir[0]) {
        report_error(dlg, "Pick your game folder first.",
            "No folder is selected yet.\n\n"
            "Click Browse and pick the folder your game is installed in - the "
            "one containing " GOLDE_GAME_EXE ".");
        return FALSE;
    }
    if (locate_game(dir, found, sizeof(found))) {
        if (strcmp(found, dir) != 0) {
            _snprintf(dir, n, "%s", found);
            dir[n - 1] = 0;
            apply_gamedir(dlg, dir);
        }
        return TRUE;
    }
    report_error(dlg, "No game found in that folder.",
        "The Game of Life: SpongeBob SquarePants Edition was not found in that "
        "folder.\n\n"
        "Pick the folder that contains " GOLDE_GAME_EXE ". A folder a few levels "
        "above it is fine too - this setup looks inside subfolders.\n\n"
        "If you have not installed the game yet, click \"Where do I get the "
        "game?\".");
    return FALSE;
}

static BOOL confirm_foreign_overwrite(HWND dlg, const char *dir)
{
    if (!foreign_dll_in(dir))
        return TRUE;
    if (g_quiet)
        return g_quiet_confirm ? TRUE : FALSE;
    return MessageBoxA(dlg,
        "This folder already has a " PATCH_DLL " that did not come from this "
        "patch.\n\n"
        "That name is used by ReShade, dgVoodoo2, SpecialK and ENB among "
        "others. Installing will REPLACE it, and whatever it does for you will "
        "stop working.\n\n"
        "Replace it?",
        "Another " PATCH_DLL " is already here",
        MB_OKCANCEL | MB_ICONWARNING) == IDOK;
}

static void do_install(HWND dlg)
{
    char dir[MAX_PATH], dst[MAX_PATH];

    get_gamedir(dlg, dir, sizeof(dir));
    if (!resolve_gamedir(dlg, dir, sizeof(dir)))
        return;
    if (!install_path_fits(dir)) {
        report_error(dlg, "That folder's path is too long to install into.",
            "The game folder's path is so long that the files this patch "
            "installs would not fit within Windows' path limit.\n\n"
            "Move the game somewhere shorter - close to the root of a drive, "
            "for example - and install again.");
        return;
    }
    if (!confirm_foreign_overwrite(dlg, dir)) {
        set_msg_timed(dlg, "Install cancelled - the existing " PATCH_DLL " was kept.");
        return;
    }

    join_path(dst, sizeof(dst), dir, INI_NAME);
    if (!file_exists(dst))
        write_resource(IDR_INI_TEMPLATE, dst);

    join_path(dst, sizeof(dst), dir, PATCH_DLL);
    if (!write_resource(IDR_PATCH_DLL, dst)) {
        report_error(dlg, "Could not write " PATCH_DLL " into the game folder.",
            "Could not write " PATCH_DLL " into the game folder.\n\n"
            "Close the game if it is running. If the game is installed under "
            "Program Files, run this setup as administrator.");
        return;
    }

    if (!save_settings(dlg, dir)) {
        report_error(dlg, "Patch copied, but writing settings failed.",
            "The patch was copied in, but " INI_NAME " could not be "
            "written.\n\nThe game will still run using default settings.");
        refresh_status(dlg);
        return;
    }

    if (is_wine())
        set_wine_override();
    install_launcher_copy(dir);
    refresh_status(dlg);

    _snprintf(g_pending_msg, sizeof(g_pending_msg),
              "Installed. Press PLAY to start the game.");
    g_pending_msg[sizeof(g_pending_msg) - 1] = 0;
    EndDialog(dlg, RES_LAUNCHER);
}

static void do_play(HWND dlg)
{
    char dir[MAX_PATH], exe[MAX_PATH];

    get_gamedir(dlg, dir, sizeof(dir));
    if (!resolve_gamedir(dlg, dir, sizeof(dir)))
        return;
    autosave(dlg);

    if (foreign_dll_in(dir)) {
        if (MessageBoxA(dlg,
                "The game folder has a " PATCH_DLL " that did not come from "
                "this patch, so the game will not be running the Definitive "
                "Edition.\n\n"
                "Replace it with the patch and start the game?",
                "Another " PATCH_DLL " is in the way",
                MB_OKCANCEL | MB_ICONWARNING) == IDOK) {
            char probe[MAX_PATH];
            join_path(probe, sizeof(probe), dir, PATCH_DLL);
            if (!write_resource(IDR_PATCH_DLL, probe)) {
                report_error(dlg, "Could not replace " PATCH_DLL ".",
                    "Could not write the patch into the game folder.\n\n"
                    "Close the game if it is running. If the game is installed "
                    "under Program Files, run this app as administrator.");
                return;
            }
            refresh_status(dlg);
        }
    }

    join_path(exe, sizeof(exe), dir, GOLDE_GAME_EXE);
    if ((INT_PTR)ShellExecuteA(dlg, "open", exe, NULL, dir, SW_SHOWNORMAL) <= 32) {
        report_error(dlg, "Could not start the game.",
            "Windows would not start the game.\n\n"
            "Check that the file is still there and that no security software "
            "is blocking it.");
        return;
    }
    EndDialog(dlg, RES_QUIT);
}

static void do_uninstall(HWND dlg)
{
    char dir[MAX_PATH], path[MAX_PATH];

    get_gamedir(dlg, dir, sizeof(dir));
    if (!resolve_gamedir(dlg, dir, sizeof(dir)))
        return;
    join_path(path, sizeof(path), dir, PATCH_DLL);
    if (!patch_installed_in(dir)) {
        if (file_exists(path))
            report_error(dlg, "That " PATCH_DLL " is not this patch, so it was left alone.",
                "The " PATCH_DLL " in that folder came from something else - "
                "ReShade, dgVoodoo2 or similar - not from this patch, so it has "
                "not been touched.\n\n"
                "Removing it is up to whatever put it there.");
        else
            report_error(dlg, "No patch is installed in this folder - nothing to remove.",
                "There is no Definitive Edition patch installed in that folder, "
                "so there is nothing to remove.");
        return;
    }
    if (!DeleteFileA(path)) {
        report_error(dlg, "Could not remove " PATCH_DLL " (is the game running?).",
            "Could not remove " PATCH_DLL ".\n\n"
            "Close the game if it is running, then try again.");
        refresh_status(dlg);
        return;
    }

    {
        char self_exe[MAX_PATH], copy[MAX_PATH];
        GetModuleFileNameA(NULL, self_exe, sizeof(self_exe));
        self_exe[sizeof(self_exe) - 1] = 0;
        join_path(copy, sizeof(copy), dir, GOLDE_LAUNCHER_EXE);
        if (file_exists(copy) && _stricmp(self_exe, copy) == 0) {
            set_msg(dlg, "Patch removed. Delete this launcher yourself to finish.");
            MessageBoxA(dlg,
                "The patch is removed and the game will now run exactly as it "
                "shipped.\n\n"
                "This launcher is running from inside the game folder, so "
                "Windows will not let it delete itself. Delete \""
                GOLDE_LAUNCHER_EXE "\" by hand if you want it gone.",
                GOLDE_SHORT, MB_OK | MB_ICONINFORMATION);
            refresh_status(dlg);
            return;
        }
        if (file_exists(copy))
            DeleteFileA(copy);
    }
    forget_gamedir_pref();
    set_msg_timed(dlg, "Patch removed. The game will run unmodified.");
    refresh_status(dlg);
}

static void do_browse(HWND dlg)
{
    BROWSEINFOA bi;
    LPITEMIDLIST pidl;
    char path[MAX_PATH], found[MAX_PATH];

    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = dlg;
    bi.lpszTitle = "Select the folder your game is installed in";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        if (SHGetPathFromIDListA(pidl, path)) {
            if (locate_game(path, found, sizeof(found)) && strcmp(found, path) != 0) {
                apply_gamedir(dlg, found);
                set_msg_timed(dlg, "Found the game in a subfolder - selected it for you.");
            } else {
                apply_gamedir(dlg, path);
            }
        }
        CoTaskMemFree(pidl);
    }
}

#define GOLDE_ARCHIVE_URL "https://archive.org/details/game-of-life-sb"

static void do_help(HWND dlg)
{
    if (MessageBoxA(dlg,
            "The Game of Life: SpongeBob SquarePants Edition was sold on CD in "
            "2007 and is long out of print. This patch does not include it.\n\n"
            "A disc image is preserved on the Internet Archive:\n\n"
            "archive.org/details/game-of-life-sb\n\n"
            "That image is the exact copy this patch was developed and tested "
            "against - the README lists its checksums so you can verify what "
            "you downloaded.\n\n"
            "Mount or extract the ISO, run the installer inside it, then point "
            "this setup at the folder holding " GOLDE_GAME_EXE ".\n\n"
            "Open that page in your browser now?",
            "Where do I get the game?", MB_YESNO | MB_ICONINFORMATION) == IDYES)
        ShellExecuteA(dlg, "open", GOLDE_ARCHIVE_URL, NULL, NULL, SW_SHOWNORMAL);
}

static void do_about(HWND dlg)
{
    char msg[768];

    _snprintf(msg, sizeof(msg),
        GOLDE_SHORT "  -  v" APP_VERSION "\n\n"
        "A fan-made patch that runs The Game of Life: SpongeBob SquarePants "
        "Edition (2007) full-screen at your monitor's resolution.\n\n"
        "The game draws at 800x600 and all of its artwork was made at that "
        "size, so the patch does not render larger - it scales the finished "
        "picture up to your screen, keeping the original 4:3 shape.\n\n"
        "No game file is modified. Everything it installs can be removed from "
        "the Setup screen.\n\n"
        "The Game of Life: SpongeBob SquarePants Edition (c) 2007 THQ / "
        "Nickelodeon / Hasbro. No game code or artwork is included here.\n\n"
        "%s", GOLDE_URL);
    msg[sizeof(msg) - 1] = 0;
    MessageBoxA(dlg, msg, "About", MB_OK | MB_ICONINFORMATION);
}

static int clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static COLORREF mix(COLORREF a, COLORREF b, double t)
{
    return RGB(clamp8((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t + 0.5)),
               clamp8((int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t + 0.5)),
               clamp8((int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t + 0.5)));
}

static void fill_rect(HDC dc, const RECT *r, COLORREF col)
{
    HBRUSH b = CreateSolidBrush(col);
    FillRect(dc, (RECT *)r, b);
    DeleteObject(b);
}

static void fill_vgradient(HDC dc, const RECT *rc, COLORREF top, COLORREF bot)
{
    int y, hgt = rc->bottom - rc->top;
    if (hgt <= 0) return;
    for (y = 0; y < hgt; y++) {
        COLORREF c = mix(top, bot, (double)y / (double)hgt);
        HPEN pen = CreatePen(PS_SOLID, 1, c);
        HPEN old = (HPEN)SelectObject(dc, pen);
        MoveToEx(dc, rc->left, rc->top + y, NULL);
        LineTo(dc, rc->right, rc->top + y);
        SelectObject(dc, old);
        DeleteObject(pen);
    }
}

static void round_fill_a(HDC dc, RECT r, int rad, COLORREF fill, int alpha)
{
    void *gfx = NULL, *path = NULL, *brush = NULL;
    int w = r.right - r.left, h = r.bottom - r.top;

    if (w <= 0 || h <= 0) return;

    if (G.ok && G.GFromHDC(dc, &gfx) == 0 && gfx) {
        if (G.PathNew(0, &path) == 0 && path) {
            gp_round_path(path, r.left, r.top, w, h, rad);
            if (G.BrushNew(gp_argb(fill, alpha), &brush) == 0 && brush) {
                G.SetSmooth(gfx, 4);
                G.FillPath(gfx, brush, path);
                G.BrushDelete(brush);
            }
            G.PathDelete(path);
        }
        G.GDelete(gfx);
        return;
    }

    {   HRGN rgn = CreateRoundRectRgn(r.left, r.top, r.right + 1, r.bottom + 1,
                                      rad * 2, rad * 2);
        HBRUSH br = CreateSolidBrush(fill);
        FillRgn(dc, rgn, br);
        DeleteObject(br);
        DeleteObject(rgn); }
}

static void round_fill(HDC dc, RECT r, int rad, COLORREF fill)
{
    round_fill_a(dc, r, rad, fill, 255);
}

static void round_frame(HDC dc, RECT r, int rad, COLORREF col, int wid)
{
    void *gfx = NULL, *path = NULL, *pen = NULL;
    int w = r.right - r.left, h = r.bottom - r.top;

    if (wid < 1) wid = 1;
    if (w <= 0 || h <= 0) return;

    if (G.ok && G.GFromHDC(dc, &gfx) == 0 && gfx) {
        if (G.PathNew(0, &path) == 0 && path) {
            float half = wid / 2.0f;
            gp_round_path(path, r.left + (int)half, r.top + (int)half,
                          w - wid, h - wid, rad);
            if (G.PenNew(gp_argb(col, 255), (float)wid, 2 , &pen) == 0 && pen) {
                G.SetSmooth(gfx, 4);
                G.DrawPath(gfx, pen, path);
                G.PenDelete(pen);
            }
            G.PathDelete(path);
        }
        G.GDelete(gfx);
        return;
    }

    {   HRGN rgn = CreateRoundRectRgn(r.left, r.top, r.right + 1, r.bottom + 1,
                                      rad * 2, rad * 2);
        HBRUSH br = CreateSolidBrush(col);
        FrameRgn(dc, rgn, br, wid, wid);
        DeleteObject(br);
        DeleteObject(rgn); }
}

static void round_grad(HDC dc, RECT r, int rad, COLORREF top, COLORREF bot)
{
    void *gfx = NULL, *path = NULL, *brush = NULL;
    int w = r.right - r.left, h = r.bottom - r.top;
    GpRectI gr;

    if (w <= 0 || h <= 0) return;

    if (G.ok && G.LineBrush && G.GFromHDC(dc, &gfx) == 0 && gfx) {
        if (G.PathNew(0, &path) == 0 && path) {
            gp_round_path(path, r.left, r.top, w, h, rad);

            gr.X = r.left; gr.Y = r.top - 1; gr.Width = w; gr.Height = h + 2;
            if (G.LineBrush(&gr, gp_argb(top, 255), gp_argb(bot, 255), 1, 0,
                            &brush) == 0 && brush) {
                G.SetSmooth(gfx, 4);
                G.FillPath(gfx, brush, path);
                G.BrushDelete(brush);
            }
            G.PathDelete(path);
        }
        G.GDelete(gfx);
        return;
    }
    round_fill(dc, r, rad, mix(top, bot, 0.5));
}

static void fade_into(HDC dc, RECT r, COLORREF col)
{
    void *gfx = NULL, *brush = NULL;
    GpRectI gr;
    int w = r.right - r.left, h = r.bottom - r.top;

    if (w <= 0 || h <= 0) return;
    if (!G.ok || !G.LineBrush || !G.FillRectI) return;
    if (G.GFromHDC(dc, &gfx) != 0 || !gfx) return;

    gr.X = r.left; gr.Y = r.top - 1; gr.Width = w; gr.Height = h + 2;
    if (G.LineBrush(&gr, gp_argb(col, 0), gp_argb(col, 255), 1, 0, &brush) == 0
        && brush) {
        G.FillRectI(gfx, brush, r.left, r.top, w, h);
        G.BrushDelete(brush);
    }
    G.GDelete(gfx);
}

static void round_shadow(HDC dc, RECT r, int rad, COLORREF page, int depth)
{
    int i;
    (void)page;

    if (!G.ok) return;
    for (i = depth; i >= 1; i--) {
        RECT s = r;
        InflateRect(&s, i, i);
        OffsetRect(&s, 0, (i + 1) / 2);
        round_fill_a(dc, s, rad + i, RGB(22, 62, 84), 10);
    }
}

static int CALLBACK face_probe(const LOGFONTA *lf, const TEXTMETRICA *tm,
                               DWORD type, LPARAM lp)
{
    (void)lf; (void)tm; (void)type;
    *(int *)lp = 1;
    return 0;
}

static const char *pick_face(const char *const *names, int n)
{
    HDC dc = GetDC(NULL);
    int i;

    for (i = 0; i < n; i++) {
        LOGFONTA lf;
        int found = 0;
        memset(&lf, 0, sizeof(lf));
        lf.lfCharSet = DEFAULT_CHARSET;
        _snprintf(lf.lfFaceName, LF_FACESIZE, "%s", names[i]);
        lf.lfFaceName[LF_FACESIZE - 1] = 0;
        EnumFontFamiliesExA(dc, &lf, face_probe, (LPARAM)&found, 0);
        if (found) { ReleaseDC(NULL, dc); return names[i]; }
    }
    ReleaseDC(NULL, dc);
    return names[n - 1];
}

static const char *const FACE_VOICE[] = {
    "Comic Sans MS", "Trebuchet MS", "Verdana", "Segoe UI"
};

static const char *const FACE_TEXT[] = {
    "Trebuchet MS", "Segoe UI", "Tahoma"
};
#define NFACE(a) ((int)(sizeof(a) / sizeof((a)[0])))

static void draw_text_in(HDC dc, RECT r, const char *s, HFONT f,
                         COLORREF col, UINT flags)
{
    HFONT of = (HFONT)SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, col);
    DrawTextA(dc, s, -1, &r, flags);
    SelectObject(dc, of);
}

static int text_width(HDC dc, const char *s, HFONT f)
{
    HFONT of = (HFONT)SelectObject(dc, f);
    SIZE sz;
    GetTextExtentPoint32A(dc, s, (int)strlen(s), &sz);
    SelectObject(dc, of);
    return sz.cx;
}

#define MAX_SEG 3

typedef struct { int key; const char *text; } Seg;

static int row_segs(int id, Seg *out, const char **label)
{
    switch (id) {
    case IDC_ROW_MODE:
        *label = "Screen";
        out[0].key = IDC_MODE_FULL; out[0].text = "Fullscreen";
        out[1].key = IDC_MODE_WIND; out[1].text = "Window";
        return 2;
    case IDC_ROW_RES:
        *label = "Resolution";
        out[0].key = 1; out[0].text = "Auto";
        out[1].key = 0; out[1].text = "Custom";
        return 2;

    case IDC_ROW_SHAPE:
        *label = "Picture shape";
        out[0].key = 0; out[0].text = "Keep 4:3";
        out[1].key = 1; out[1].text = "Stretch";
        return 2;
    case IDC_ROW_CAP:
        *label = "Frame limit";
        out[0].key = 0; out[0].text = "Uncapped";
        out[1].key = 1; out[1].text = "60 fps";
        return 2;

    case IDC_ROW_BG:
        *label = "When unfocused";
        out[0].key = 0; out[0].text = "Pause";
        out[1].key = 1; out[1].text = "Keep running";
        return 2;
    case IDC_ROW_PATCH:
        *label = "Patch";
        out[0].key = 1; out[0].text = "On";
        out[1].key = 0; out[1].text = "Off";
        return 2;
    }
    *label = "";
    return 0;
}

static int row_current(int id)
{
    switch (id) {
    case IDC_ROW_MODE:   return (g_mode_sel == IDC_MODE_WIND) ? 1 : 0;
    case IDC_ROW_RES:    return is_checked(IDC_AUTORES) ? 0 : 1;
    case IDC_ROW_SHAPE:  return is_checked(IDC_STRETCH) ? 1 : 0;
    case IDC_ROW_CAP:    return is_checked(IDC_CAP60)   ? 1 : 0;
    case IDC_ROW_BG:     return is_checked(IDC_BACKGROUND) ? 1 : 0;
    case IDC_ROW_PATCH:  return is_checked(IDC_ENABLED) ? 0 : 1;
    }
    return 0;
}

static void row_select(HWND dlg, int id, int idx)
{
    switch (id) {
    case IDC_ROW_MODE:
        set_radio(dlg, idx ? IDC_MODE_WIND : IDC_MODE_FULL);
        break;
    case IDC_ROW_RES:
        set_checked(dlg, IDC_AUTORES, idx == 0);
        update_enable_states(dlg);
        break;
    case IDC_ROW_SHAPE:  set_checked(dlg, IDC_STRETCH, idx == 1); break;
    case IDC_ROW_CAP:    set_checked(dlg, IDC_CAP60,   idx == 1); break;
    case IDC_ROW_BG:     set_checked(dlg, IDC_BACKGROUND, idx == 1); break;
    case IDC_ROW_PATCH:  set_checked(dlg, IDC_ENABLED, idx == 0); break;
    default: return;
    }
    {   HWND c = GetDlgItem(dlg, id);
        if (c) InvalidateRect(c, NULL, FALSE); }

    if (id == IDC_ROW_MODE || id == IDC_ROW_RES) {
        HWND c = GetDlgItem(dlg, IDC_ROW_SHAPE);
        if (c) InvalidateRect(c, NULL, FALSE);
    }
}

static BOOL windowed_exact(void)
{
    return g_mode_sel == IDC_MODE_WIND && is_checked(IDC_AUTORES);
}

static void row_note(int id, char *out, size_t n)
{
    out[0] = 0;
    switch (id) {
    case IDC_ROW_RES:
        if (is_checked(IDC_AUTORES)) {
            int dw, dh;
            desktop_res(&dw, &dh);
            _snprintf(out, n, "%d x %d", dw, dh);
        }
        break;

    case IDC_ROW_SHAPE:
        if (windowed_exact())
            _snprintf(out, n, "no effect in Window");
        break;
    case IDC_ROW_PATCH:
        if (!is_checked(IDC_ENABLED))
            _snprintf(out, n, "runs unmodified");
        break;
    }
    out[n - 1] = 0;
}

static BOOL is_row(int id)
{
    return id >= IDC_ROW_FIRST && id <= IDC_ROW_LAST;
}

static void row_pill_rect(HDC dc, const RECT *rc, const Seg *segs, int n,
                          RECT *pill, int *segw)
{
    HFONT of = (HFONT)SelectObject(dc, g_seg_font);
    int i, widest = 0;

    for (i = 0; i < n; i++) {
        SIZE sz;
        GetTextExtentPoint32A(dc, segs[i].text, (int)strlen(segs[i].text), &sz);
        if (sz.cx > widest) widest = sz.cx;
    }
    SelectObject(dc, of);

    *segw = widest + SC(14);
    if (*segw < SC(26)) *segw = SC(26);

    pill->right  = rc->right - SC(2);
    pill->left   = pill->right - (*segw * n) - SC(5);

    pill->top    = rc->top + SC(2.5);
    pill->bottom = rc->bottom - SC(2.5);
}

#define PILL_RAD (SC(3) < 3 ? 3 : SC(3))

static void blit_parent_bg(LPDRAWITEMSTRUCT d, RECT rc)
{
    HWND parent = GetParent(d->hwndItem);
    RECT wr, pcr;
    POINT tl;
    HDC mem;
    HBITMAP ob;

    GetWindowRect(d->hwndItem, &wr);
    tl.x = wr.left; tl.y = wr.top;
    ScreenToClient(parent, &tl);
    GetClientRect(parent, &pcr);
    ensure_bg(parent, d->hDC, pcr.right, pcr.bottom);
    mem = CreateCompatibleDC(d->hDC);
    ob = (HBITMAP)SelectObject(mem, g_bg_bmp);
    BitBlt(d->hDC, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
           mem, tl.x, tl.y, SRCCOPY);
    SelectObject(mem, ob);
    DeleteDC(mem);
}

static void draw_row(LPDRAWITEMSTRUCT d)
{
    RECT rc = d->rcItem, pill, lab;
    int id = (int)d->CtlID;
    Seg segs[MAX_SEG];
    const char *label = "";
    char note[48];
    int n = row_segs(id, segs, &label);
    int cur = row_current(id);
    int i, sw;
    POINT cur_pt;
    int hot = -1;

    blit_parent_bg(d, rc);
    if (n <= 0)
        return;

    GetCursorPos(&cur_pt);
    ScreenToClient(d->hwndItem, &cur_pt);

    row_pill_rect(d->hDC, &rc, segs, n, &pill, &sw);

    lab = rc;
    lab.left  = rc.left + SC(4);
    lab.right = pill.left - SC(6);
    draw_text_in(d->hDC, lab, label, g_label_font, COL_INK,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    row_note(id, note, sizeof(note));
    if (note[0]) {
        RECT nr = rc;
        nr.right = pill.left - SC(8);
        nr.left  = nr.right - SC(70);
        draw_text_in(d->hDC, nr, note, g_body_font, COL_INK_SOFT,
                     DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    round_fill(d->hDC, pill, PILL_RAD, COL_TRACK);

    for (i = 0; i < n; i++) {
        RECT s = pill;
        s.left  = pill.left + i * sw;
        s.right = (i == n - 1) ? pill.right : s.left + sw;
        if (PtInRect(&s, cur_pt)) hot = i;
    }

    for (i = 0; i < n; i++) {
        RECT s = pill;
        COLORREF fg;
        s.left  = pill.left + i * sw;
        s.right = (i == n - 1) ? pill.right : s.left + sw;

        if (i == cur) {
            RECT f = s;
            InflateRect(&f, -SC(1), -SC(1));
            round_fill(d->hDC, f, PILL_RAD,
                       hot == i ? COL_ACCENT_LT : COL_ACCENT);
            fg = RGB(255, 255, 255);
        } else if (hot == i) {
            RECT f = s;
            InflateRect(&f, -SC(1), -SC(1));
            round_fill(d->hDC, f, PILL_RAD, COL_TRACK_HOV);
            fg = COL_INK;
        } else {
            fg = COL_INK_SOFT;
        }
        draw_text_in(d->hDC, s, segs[i].text, g_seg_font, fg,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

static int row_hit(HWND ctl, int id)
{
    RECT rc, pill;
    Seg segs[MAX_SEG];
    const char *label;
    int n = row_segs(id, segs, &label);
    POINT pt;
    int sw, i;

    if (n <= 0) return -1;
    GetClientRect(ctl, &rc);
    {   HDC dc = GetDC(ctl);
        row_pill_rect(dc, &rc, segs, n, &pill, &sw);
        ReleaseDC(ctl, dc); }
    GetCursorPos(&pt);
    ScreenToClient(ctl, &pt);
    if (!PtInRect(&pill, pt)) return -1;
    for (i = 0; i < n; i++) {
        RECT s = pill;
        s.left  = pill.left + i * sw;
        s.right = (i == n - 1) ? pill.right : s.left + sw;
        if (PtInRect(&s, pt)) return i;
    }
    return -1;
}

static void draw_button(LPDRAWITEMSTRUCT d)
{
    char text[128];
    RECT rc = d->rcItem, tr;
    int id = (int)d->CtlID;
    BOOL primary  = (id == IDC_PLAY || id == IDC_INSTALL);
    BOOL pressed  = (d->itemState & ODS_SELECTED) != 0;
    BOOL disabled = (d->itemState & ODS_DISABLED) != 0;
    BOOL hover    = (id == g_hover_id);
    HFONT f = (id == IDC_PLAY) ? g_play_font : g_btn_font;

    int rad = SC(3) < 3 ? 3 : SC(3);

    blit_parent_bg(d, rc);
    GetWindowTextA(d->hwndItem, text, sizeof(text));
    tr = rc;
    if (pressed) OffsetRect(&tr, 0, 1);

    if (primary) {
        COLORREF base = disabled ? RGB(206, 212, 218)
                      : pressed  ? COL_GO_DK
                      : hover    ? COL_GO_LT : COL_GO;
        RECT body = rc;

        if (!disabled)
            round_shadow(d->hDC, rc, rad, COL_BG_TOP, SC(2) < 2 ? 2 : SC(2));
        if (pressed) OffsetRect(&body, 0, 1);

        round_grad(d->hDC, body, rad,
                   mix(base, RGB(255, 255, 255), 0.30),
                   mix(base, RGB(150, 96, 0), 0.16));
        if (!disabled)
            round_frame(d->hDC, body, rad, mix(base, RGB(140, 88, 0), 0.34),
                        SC(1.2));
        draw_text_in(d->hDC, tr, text, f,
                     disabled ? RGB(140, 146, 152) : COL_GO_INK,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }

    {
        COLORREF fill = disabled ? mix(COL_BG_TOP, COL_CARD, 0.4)
                      : (hover || pressed) ? COL_TRACK_HOV : COL_CARD;
        COLORREF brd  = disabled ? RGB(178, 202, 214)
                      : (hover || pressed) ? COL_ACCENT : COL_EDGE;
        COLORREF fg   = disabled ? COL_INK_FAINT
                      : (hover || pressed) ? COL_ACCENT_DK : COL_INK;
        if (!disabled)
            round_shadow(d->hDC, rc, rad, COL_BG_TOP, 1);
        round_fill(d->hDC, rc, rad, fill);
        round_frame(d->hDC, rc, rad, brd, SC(1.2));
        draw_text_in(d->hDC, tr, text, f, fg,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

static void draw_static_ctl(LPDRAWITEMSTRUCT d)
{
    char text[256];
    int id = (int)d->CtlID;
    RECT rc = d->rcItem;

    blit_parent_bg(d, rc);
    GetWindowTextA(d->hwndItem, text, sizeof(text));
    if (!text[0])
        return;

    if (id == IDC_HDR_FOLDER) {
        draw_text_in(d->hDC, rc, text, g_h1_font, COL_INK,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }
    if (id == IDC_LBL_WIDTH || id == IDC_LBL_HEIGHT) {
        draw_text_in(d->hDC, rc, text, g_label_font, COL_INK_SOFT,
                     ((id == IDC_LBL_HEIGHT) ? DT_CENTER : DT_LEFT)
                     | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }
    if (id == IDC_STATUS_EXE || id == IDC_STATUS_PATCH) {
        const char *colon = strchr(text, ':');
        COLORREF val = COL_INK;
        if (strstr(text, "FOUND") || strstr(text, "INSTALLED"))
            val = COL_OK_PAGE;
        else if (strstr(text, "another") || strstr(text, "No "))
            val = COL_WARN;
        if (colon) {
            char head[64];
            int n = (int)(colon - text) + 1;
            RECT r = rc;
            if (n > (int)sizeof(head) - 1) n = (int)sizeof(head) - 1;
            memcpy(head, text, n);
            head[n] = 0;
            draw_text_in(d->hDC, r, head, g_body_font, COL_INK_PAGE,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            r.left += text_width(d->hDC, head, g_body_font) + SC(4);
            draw_text_in(d->hDC, r, colon + 1, g_label_font, val,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            return;
        }
    }
    if (id == IDC_SUBHEAD) {
        draw_text_in(d->hDC, rc, text, g_body_font, COL_INK_PAGE,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }

    draw_text_in(d->hDC, rc, text, g_body_font,
                 (id == IDC_MSG) ? COL_INK_PAGE : COL_INK_SOFT,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static void draw_scene(HWND dlg, HDC dc, int w, int h)
{
    RECT rc, card;
    int hero_h = HERO_H(w);

    rc.left = 0; rc.top = 0; rc.right = w; rc.bottom = h;

    fill_vgradient(dc, &rc, COL_BG_TOP, COL_BG_BOT);

    if (g_art) {
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP ob = (HBITMAP)SelectObject(mem, g_art);
        int sw = ART_SRC_W;
        int sh = (hero_h * sw) / w;
        int sy = ART_SRC_Y + (ART_SRC_H - sh) / 2;

        if (sy < 0) sy = 0;
        if (sy + sh > g_art_h) sh = g_art_h - sy;
        if (sh < 1) sh = 1;

        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, NULL);
        StretchBlt(dc, 0, 0, w, hero_h, mem, ART_SRC_X, sy, sw, sh, SRCCOPY);
        SelectObject(mem, ob);
        DeleteDC(mem);
    } else {
        RECT t = rc;
        t.bottom = hero_h;
        fill_vgradient(dc, &t, RGB(222, 240, 250), RGB(186, 222, 242));
        t.left = SC(26); t.top = hero_h / 2 - SC(15);
        t.bottom = t.top + SC(17); t.right = w - SC(26);
        draw_text_in(dc, t, "The Game of Life", g_h1_font, RGB(24, 68, 104),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        t.top += SC(15); t.bottom = t.top + SC(13);
        draw_text_in(dc, t, "SpongeBob SquarePants Edition", g_label_font,
                     RGB(56, 116, 152), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    {
        RECT f, s;
        f.left = 0; f.right = w;
        f.bottom = hero_h;
        f.top = ((ART_FADE_FROM - ART_SRC_Y) * hero_h) / ART_SRC_H;
        if (f.top > hero_h - 3) f.top = hero_h - 3;
        if (f.top < 0) f.top = 0;
        fade_into(dc, f, COL_HAZE);

        s.left = 0; s.right = w;
        s.top = hero_h; s.bottom = hero_h + SC(12);
        fill_vgradient(dc, &s, COL_HAZE, COL_BG_TOP);
    }

    if (dlg && GetDlgItem(dlg, IDC_ROW_MODE)) {
        HWND first = GetDlgItem(dlg, IDC_ROW_MODE);
        HWND last  = GetDlgItem(dlg, IDC_ROW_PATCH);
        RECT a, b;

        GetWindowRect(first, &a); MapWindowPoints(NULL, dlg, (POINT *)&a, 2);
        GetWindowRect(last,  &b); MapWindowPoints(NULL, dlg, (POINT *)&b, 2);
        card.left = a.left - SC(10); card.right = b.right + SC(10);
        card.top  = a.top  - SC(8);
        card.bottom = b.bottom + SC(8);
        round_shadow(dc, card, SC(8), COL_BG_TOP, SC(3));
        round_fill(dc, card, SC(8), COL_CARD);

        {
            int i;
            for (i = 1; i < IDC_ROW_COUNT; i++) {
                RECT ln;
                HWND c = GetDlgItem(dlg, IDC_ROW_FIRST + i);
                RECT r;
                if (!c) continue;
                GetWindowRect(c, &r); MapWindowPoints(NULL, dlg, (POINT *)&r, 2);
                ln.left = card.left + SC(10); ln.right = card.right - SC(10);
                ln.top = r.top - SC(1); ln.bottom = ln.top + 1;
                fill_rect(dc, &ln, COL_RULE);
            }
        }
    }

    if (dlg && GetDlgItem(dlg, IDC_GAMEDIR)) {
        HWND e = GetDlgItem(dlg, IDC_GAMEDIR);
        RECT er;
        GetWindowRect(e, &er); MapWindowPoints(NULL, dlg, (POINT *)&er, 2);
        InflateRect(&er, SC(3), SC(3));
        round_fill(dc, er, SC(4), COL_CARD);
        round_frame(dc, er, SC(4), COL_RULE, SC(1.2));
    }

    if (dlg) {
        HWND m = GetDlgItem(dlg, IDC_MSG);
        if (m) {
            RECT mr, vr;
            GetWindowRect(m, &mr);
            MapWindowPoints(NULL, dlg, (POINT *)&mr, 2);
            vr.top = mr.top; vr.bottom = mr.bottom;
            vr.right = w - SC(26); vr.left = vr.right - SC(150);
            draw_text_in(dc, vr, "Definitive Edition  v" GOLDE_VERSION,
                         g_body_font, mix(COL_INK_PAGE, COL_BG_BOT, 0.42),
                         DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    if (dlg && GetDlgItem(dlg, IDC_ROW_PATCH)) {
        static const int edits[] = { IDC_WIDTH, IDC_HEIGHT };
        HWND rowc = GetDlgItem(dlg, IDC_ROW_PATCH);
        int i, target_h = 0;

        {
            RECT rr, local, pill;
            Seg segs[MAX_SEG];
            const char *lab;
            int n, sw;
            HDC probe = GetDC(dlg);

            GetWindowRect(rowc, &rr);
            MapWindowPoints(NULL, dlg, (POINT *)&rr, 2);
            local.left = 0; local.top = 0;
            local.right = rr.right - rr.left;
            local.bottom = rr.bottom - rr.top;
            n = row_segs(IDC_ROW_PATCH, segs, &lab);
            row_pill_rect(probe, &local, segs, n, &pill, &sw);
            ReleaseDC(dlg, probe);
            target_h = pill.bottom - pill.top;
        }

        for (i = 0; i < 2; i++) {
            HWND c = GetDlgItem(dlg, edits[i]);
            RECT er;
            int cy;
            if (!c || !(GetWindowLongA(c, GWL_STYLE) & WS_VISIBLE))
                continue;
            GetWindowRect(c, &er);
            MapWindowPoints(NULL, dlg, (POINT *)&er, 2);
            cy = (er.top + er.bottom) / 2;
            er.top = cy - target_h / 2;
            er.bottom = er.top + target_h;
            InflateRect(&er, SC(5), 0);
            round_fill(dc, er, PILL_RAD, COL_TRACK);
            round_frame(dc, er, PILL_RAD, COL_RULE, SC(1.2));
        }
    }
}

static void ensure_bg(HWND dlg, HDC ref, int w, int h)
{
    HDC mem;
    HBITMAP ob;
    if (g_bg_bmp && g_bg_w == w && g_bg_h == h) return;
    if (g_bg_bmp) DeleteObject(g_bg_bmp);
    g_bg_bmp = CreateCompatibleBitmap(ref, w, h);
    g_bg_w = w; g_bg_h = h;
    mem = CreateCompatibleDC(ref);
    ob = (HBITMAP)SelectObject(mem, g_bg_bmp);
    draw_scene(dlg, mem, w, h);
    SelectObject(mem, ob);
    DeleteDC(mem);
}

static double window_zoom(HWND dlg, int base_w, int base_h)
{
    HMONITOR mon;
    MONITORINFO mi;
    int aw, ah;
    double z, zh;

    mon = MonitorFromWindow(dlg, MONITOR_DEFAULTTONEAREST);
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!mon || !GetMonitorInfoA(mon, &mi) || base_w <= 0 || base_h <= 0)
        return 1.0;

    aw = mi.rcWork.right - mi.rcWork.left;
    ah = mi.rcWork.bottom - mi.rcWork.top;
    if (aw <= 0 || ah <= 0)
        return 1.0;

    z = (aw * 0.30) / base_w;

    zh = (ah * 0.80) / base_h;
    if (z > zh) z = zh;

    {
        double floor_z = (ah * 0.92) / base_h;
        if (floor_z > 1.0) floor_z = 1.0;
        if (z < floor_z) z = floor_z;
    }
    if (z > 1.9) z = 1.9;
    return z;
}

static void scale_dialog(HWND dlg, double z)
{
    RECT cr, want;
    HWND c;
    int cw, ch;

    if (z > 0.999 && z < 1.001)
        return;

    for (c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        RECT r;
        GetWindowRect(c, &r);
        MapWindowPoints(NULL, dlg, (POINT *)&r, 2);
        SetWindowPos(c, NULL,
                     (int)(r.left * z + 0.5), (int)(r.top * z + 0.5),
                     (int)((r.right - r.left) * z + 0.5),
                     (int)((r.bottom - r.top) * z + 0.5),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    GetClientRect(dlg, &cr);
    cw = (int)(cr.right * z + 0.5);
    ch = (int)(cr.bottom * z + 0.5);
    want.left = want.top = 0; want.right = cw; want.bottom = ch;
    AdjustWindowRectEx(&want, (DWORD)GetWindowLongA(dlg, GWL_STYLE), FALSE,
                       (DWORD)GetWindowLongA(dlg, GWL_EXSTYLE));

    {
        HMONITOR mon = MonitorFromWindow(dlg, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        int ww = want.right - want.left, wh = want.bottom - want.top;
        int x = 0, y = 0;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        if (mon && GetMonitorInfoA(mon, &mi)) {
            x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - ww) / 2;
            y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - wh) / 2;
        }
        SetWindowPos(dlg, NULL, x, y, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

static INT_PTR CALLBACK dlg_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        char self[MAX_PATH];
        double zoom = 1.0;

        g_mode = (int)lp;

        {   RECT u = { 0, 0, 400, 0 };
            MapDialogRect(dlg, &u);
            g_scale = u.right > 0 ? u.right / 400.0 : 1.0; }

        {   RECT cr;
            GetClientRect(dlg, &cr);
            /* g_scale must take the zoom before the fonts are built. */
            zoom = window_zoom(dlg, cr.right, cr.bottom);
            g_scale *= zoom; }

        gdip_init();

        {
            const char *voice = pick_face(FACE_VOICE, NFACE(FACE_VOICE));
            const char *text  = pick_face(FACE_TEXT,  NFACE(FACE_TEXT));

            g_h1_font    = CreateFontA(SC(-11.5),0,0,0, FW_BOLD, 0,0,0, DEFAULT_CHARSET,0,0,
                                       CLEARTYPE_QUALITY, 0, voice);
            g_label_font = CreateFontA(SC(-7.2), 0,0,0, FW_BOLD, 0,0,0, DEFAULT_CHARSET,0,0,
                                       CLEARTYPE_QUALITY, 0, voice);
            g_seg_font   = CreateFontA(SC(-6.9), 0,0,0, FW_BOLD, 0,0,0, DEFAULT_CHARSET,0,0,
                                       CLEARTYPE_QUALITY, 0, voice);
            g_play_font  = CreateFontA(SC(-9.5), 0,0,0, FW_BOLD, 0,0,0, DEFAULT_CHARSET,0,0,
                                       CLEARTYPE_QUALITY, 0, voice);
            g_btn_font   = CreateFontA(SC(-7),   0,0,0, FW_BOLD, 0,0,0, DEFAULT_CHARSET,0,0,
                                       CLEARTYPE_QUALITY, 0, voice);
            g_cap_font   = CreateFontA(SC(-6.6), 0,0,0, FW_BOLD, 0,0,0, DEFAULT_CHARSET,0,0,
                                       CLEARTYPE_QUALITY, 0, text);
            g_body_font  = CreateFontA(SC(-7),   0,0,0, FW_NORMAL, 0,0,0, DEFAULT_CHARSET,0,0,
                                       CLEARTYPE_QUALITY, 0, text);
        }
        g_edit_brush  = CreateSolidBrush(COL_CARD);
        g_track_brush = CreateSolidBrush(COL_TRACK);

        scale_dialog(dlg, zoom);

        {
            /* EDITs are Windows-drawn and do not inherit the fonts above. */
            static const int edits[] = { IDC_GAMEDIR, IDC_WIDTH, IDC_HEIGHT };
            int i;
            for (i = 0; i < (int)(sizeof(edits) / sizeof(edits[0])); i++) {
                HWND e = GetDlgItem(dlg, edits[i]);
                if (e)
                    SendMessageA(e, WM_SETFONT, (WPARAM)g_seg_font, TRUE);
            }
        }

        flow_capture(dlg);

        {
            HINSTANCE hinst = GetModuleHandleA(NULL);
            HICON t = (HICON)LoadImageA(hinst, MAKEINTRESOURCEA(IDI_APPICON),
                          IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
            if (!t) t = LoadIconA(hinst, MAKEINTRESOURCEA(IDI_APPICON));
            if (t) { SendMessageA(dlg, WM_SETICON, ICON_BIG,   (LPARAM)t);
                     SendMessageA(dlg, WM_SETICON, ICON_SMALL, (LPARAM)t); }
        }

        launcher_dir(self, sizeof(self));
        if (dir_has_game(self))
            apply_gamedir(dlg, self);
        else if (g_gamedir[0])
            apply_gamedir(dlg, g_gamedir);
        else {
            char dir[MAX_PATH];
            get_gamedir(dlg, dir, sizeof(dir));
            load_settings(dlg, dir);
            update_enable_states(dlg);
            refresh_status(dlg);
        }

        if (g_pending_msg[0]) {
            set_msg_timed(dlg, g_pending_msg);
            g_pending_msg[0] = 0;
        }
        SetTimer(dlg, TIMER_HOVER, 40, NULL);
        return TRUE;
    }

    case WM_TIMER: {
        POINT pt; HWND under; int i, hov = 0, seg = -1;

        if (wp == TIMER_MSG) {
            msg_expire(dlg);
            return TRUE;
        }
        if (wp != TIMER_HOVER)
            break;

        GetCursorPos(&pt);
        under = WindowFromPoint(pt);
        for (i = 0; i < NBTN; i++)
            if (GetDlgItem(dlg, g_btn_ids[i]) == under) { hov = g_btn_ids[i]; break; }

        if (hov && is_row(hov))
            seg = row_hit(under, hov);

        if (hov != g_hover_id || seg != g_hover_seg) {
            int old = g_hover_id;
            g_hover_id = hov;
            g_hover_seg = seg;
            if (old) InvalidateRect(GetDlgItem(dlg, old), NULL, FALSE);
            if (hov) InvalidateRect(GetDlgItem(dlg, hov), NULL, FALSE);
        }
        return TRUE;
    }

    case WM_CTLCOLORDLG:
        return (INT_PTR)GetStockObject(HOLLOW_BRUSH);

    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        int id = GetDlgCtrlID((HWND)lp);
        if (id == IDC_WIDTH || id == IDC_HEIGHT) {
            SetBkColor(dc, COL_TRACK);
            SetTextColor(dc, COL_INK_SOFT);
            return (INT_PTR)g_track_brush;
        }
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, COL_INK_SOFT);
        return (INT_PTR)GetStockObject(HOLLOW_BRUSH);
    }

    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        int id = GetDlgCtrlID((HWND)lp);
        BOOL on_track = (id == IDC_WIDTH || id == IDC_HEIGHT);
        SetBkColor(dc, on_track ? COL_TRACK : COL_CARD);
        SetTextColor(dc, COL_INK);
        return (INT_PTR)(on_track ? g_track_brush : g_edit_brush);
    }

    case WM_ERASEBKGND: {
        HDC dc = (HDC)wp, mem;
        HBITMAP ob;
        RECT crc;
        GetClientRect(dlg, &crc);
        ensure_bg(dlg, dc, crc.right, crc.bottom);
        mem = CreateCompatibleDC(dc);
        ob = (HBITMAP)SelectObject(mem, g_bg_bmp);
        BitBlt(dc, 0, 0, crc.right, crc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteDC(mem);
        return TRUE;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT d = (LPDRAWITEMSTRUCT)lp;
        int id = (int)d->CtlID;
        int w, h;
        HDC mem;
        HBITMAP bmp, ob;
        DRAWITEMSTRUCT b;

        if (d->CtlType != ODT_BUTTON && d->CtlType != ODT_STATIC)
            break;

        w = d->rcItem.right - d->rcItem.left;
        h = d->rcItem.bottom - d->rcItem.top;
        if (w <= 0 || h <= 0)
            return TRUE;

        mem = CreateCompatibleDC(d->hDC);
        bmp = CreateCompatibleBitmap(d->hDC, w, h);
        if (!mem || !bmp) {
            if (bmp) DeleteObject(bmp);
            if (mem) DeleteDC(mem);
            break;
        }
        ob = (HBITMAP)SelectObject(mem, bmp);

        b = *d;
        b.hDC = mem;
        b.rcItem.left = 0; b.rcItem.top = 0;
        b.rcItem.right = w; b.rcItem.bottom = h;

        if (b.CtlType == ODT_STATIC) draw_static_ctl(&b);
        else if (is_row(id))         draw_row(&b);
        else                         draw_button(&b);

        BitBlt(d->hDC, d->rcItem.left, d->rcItem.top, w, h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        return TRUE;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);

        if (is_row(id)) {
            int seg = row_hit(GetDlgItem(dlg, id), id);
            if (seg >= 0) {
                row_select(dlg, id, seg);
                autosave(dlg);
            }
            return TRUE;
        }

        switch (id) {
        case IDC_WIDTH:
        case IDC_HEIGHT:
            if (HIWORD(wp) == EN_KILLFOCUS) autosave(dlg);
            return TRUE;

        case IDC_BROWSE:     do_browse(dlg);    return TRUE;
        case IDC_INSTALL:    do_install(dlg);   return TRUE;
        case IDC_UNINSTALL:  do_uninstall(dlg); return TRUE;
        case IDC_PLAY:       do_play(dlg);      return TRUE;
        case IDC_SETUPBTN:   EndDialog(dlg, RES_SETUP); return TRUE;
        case IDC_HELPBTN:    do_help(dlg);      return TRUE;
        case IDC_ABOUT:      do_about(dlg);     return TRUE;

        case IDCANCEL:       autosave(dlg); EndDialog(dlg, RES_QUIT); return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        autosave(dlg);
        EndDialog(dlg, RES_QUIT);
        return TRUE;

    case WM_DESTROY:
        KillTimer(dlg, TIMER_HOVER);
        KillTimer(dlg, TIMER_MSG);
        DeleteObject(g_h1_font);    DeleteObject(g_label_font);
        DeleteObject(g_seg_font);   DeleteObject(g_cap_font);
        DeleteObject(g_btn_font);   DeleteObject(g_play_font);
        DeleteObject(g_body_font);  DeleteObject(g_edit_brush);
        DeleteObject(g_track_brush);
        if (g_bg_bmp) { DeleteObject(g_bg_bmp); g_bg_bmp = NULL; g_bg_w = g_bg_h = 0; }
        art_free();
        return TRUE;
    }
    return FALSE;
}

#define GOLDE_DLG_CLASS "GameOfLifeDefinitiveEditionSetup"
static void register_dlg_class(HINSTANCE inst)
{
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = DefDlgProcA;
    wc.cbWndExtra    = DLGWINDOWEXTRA;
    wc.hInstance     = inst;
    wc.hIcon   = (HICON)LoadImageA(inst, MAKEINTRESOURCEA(IDI_APPICON), IMAGE_ICON,
                                   64, 64, LR_DEFAULTCOLOR);
    wc.hIconSm = (HICON)LoadImageA(inst, MAKEINTRESOURCEA(IDI_APPICON), IMAGE_ICON,
                                   32, 32, LR_DEFAULTCOLOR);
    if (!wc.hIcon)   wc.hIcon   = LoadIconA(inst, MAKEINTRESOURCEA(IDI_APPICON));
    if (!wc.hIconSm) wc.hIconSm = wc.hIcon;
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = GOLDE_DLG_CLASS;
    RegisterClassExA(&wc);
}

#ifndef GOLDE_PREVIEW
int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    INITCOMMONCONTROLSEX icc;
    (void)prev; (void)cmd; (void)show;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);
    CoInitialize(NULL);
    register_dlg_class(inst);

    {
        char self[MAX_PATH], pref[MAX_PATH];
        int mode = MODE_SETUP;

        launcher_dir(self, sizeof(self));
        if (dir_has_game(self)) {
            _snprintf(g_gamedir, sizeof(g_gamedir), "%s", self);
            g_gamedir[sizeof(g_gamedir) - 1] = 0;
            mode = MODE_LAUNCH;
        } else if (load_gamedir_pref(pref, sizeof(pref))) {
            if (dir_has_game(pref)) {
                _snprintf(g_gamedir, sizeof(g_gamedir), "%s", pref);
                g_gamedir[sizeof(g_gamedir) - 1] = 0;

                if (patch_installed_in(pref))
                    mode = MODE_LAUNCH;
            }
        }

        for (;;) {
            INT_PTR r = DialogBoxParamA(inst,
                MAKEINTRESOURCEA(mode == MODE_LAUNCH ? IDD_LAUNCH : IDD_MAIN),
                NULL, dlg_proc, (LPARAM)mode);
            if (r == RES_LAUNCHER)   { mode = MODE_LAUNCH; continue; }
            if (r == RES_SETUP)      { mode = MODE_SETUP;  continue; }
            break;
        }
    }

    art_free();
    gdip_shutdown();
    CoUninitialize();
    return 0;
}
#endif
