/*
 * Windows Update Blocker
 * Author  : Ari Sohandri Putra
 * Version : 2.0.0
 * Original: 15 May 2019
 * Build   : Visual Studio 2010
 * Compiler: MSVC 2010 (v100)
 *
 * Light & Classic Modern UI Theme
 * Auto-elevates to Administrator via UAC manifest.
 */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0501  
#include <windows.h>
#include <winsvc.h>
#include <shellapi.h>
#include <tchar.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#define IDC_TOGGLE_BTN   1001
#define IDC_STATUS_LBL   1002
#define IDC_INFO_LBL     1003
#define IDC_REFRESH_BTN  1004
#define IDC_FOOTER_LBL   1005
#define WM_TRAYICON      (WM_USER + 1)
#define ID_TRAY_EXIT     2001
#define ID_TRAY_SHOW     2002
#define ANIM_TIMER_ID    500
static const TCHAR* g_services[] = {
    _T("wuauserv"),    // Windows Update
    _T("bits"),        // Background Intelligent Transfer Service
    _T("UsoSvc"),      // Update Orchestrator Service (Win10+)
    _T("WaaSMedicSvc") // Windows Update Medic Service (best-effort)
};
#define SERVICE_COUNT 4


//  UI Color
//  Made with https://imagecolorpicker.com/
#define CLR_BG           RGB(245, 247, 250)
#define CLR_PANEL        RGB(255, 255, 255)
#define CLR_PANEL_HEADER RGB(235, 240, 248)
#define CLR_BORDER       RGB(210, 218, 230)
#define CLR_BORDER_DARK  RGB(180, 195, 215)
#define CLR_ON           RGB(22,  163, 74)
#define CLR_OFF          RGB(220,  38, 38)
#define CLR_TRACK_OFF    RGB(200, 210, 225)
#define CLR_TRACK_ON     RGB(134, 239, 172)
#define CLR_THUMB        RGB(255, 255, 255)
#define CLR_THUMB_SHADOW RGB(180, 195, 215)
#define CLR_TEXT_DARK    RGB( 30,  40,  60)
#define CLR_TEXT_MED     RGB( 80,  95, 120)
#define CLR_TEXT_LIGHT   RGB(130, 148, 170)
#define CLR_ACCENT       RGB( 37, 99, 235)
#define CLR_BTN_BG       RGB(235, 240, 248)
#define CLR_BTN_HOVER    RGB(210, 222, 240)
#define CLR_WARN_BG      RGB(255, 247, 235)
#define CLR_WARN_BORDER  RGB(251, 191,  36)

#define SLIDER_W   86
#define SLIDER_H   40
#define THUMB_D    32

HWND  g_hWnd        = NULL;
HWND  g_hToggleBtn  = NULL;
HWND  g_hStatusLbl  = NULL;
HWND  g_hInfoLbl    = NULL;
HWND  g_hRefreshBtn = NULL;
HWND  g_hFooterLbl  = NULL;
BOOL  g_bEnabled    = TRUE;
BOOL  g_bAnimating  = FALSE;
int   g_sliderPos   = 1;    
int   g_animTarget  = 1;
BOOL  g_bRefreshHover = FALSE;

NOTIFYICONDATA g_nid;
HFONT g_hFontTitle   = NULL;
HFONT g_hFontSub     = NULL;
HFONT g_hFontNormal  = NULL;
HFONT g_hFontSmall   = NULL;
HFONT g_hFontFooter  = NULL;
HBRUSH g_hBrushBg    = NULL;
HBRUSH g_hBrushPanel = NULL;
HBRUSH g_hBrushHeader = NULL;

static BOOL IsRunAsAdmin()
{
    BOOL bAdmin = FALSE;
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return FALSE;
    TOKEN_ELEVATION te;
    DWORD dwSize = 0;
    if (GetTokenInformation(hToken, TokenElevation, &te, sizeof(te), &dwSize))
        bAdmin = te.TokenIsElevated;
    CloseHandle(hToken);
    return bAdmin;
}

static BOOL SetServiceStartType(const TCHAR* svcName, DWORD dwStartType)
{
    SC_HANDLE hScm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hScm) return FALSE;

    SC_HANDLE hSvc = OpenService(hScm, svcName,
        SERVICE_CHANGE_CONFIG | SERVICE_STOP | SERVICE_START | SERVICE_QUERY_STATUS);
    if (!hSvc) {
        DWORD err = GetLastError();
        CloseServiceHandle(hScm);
        return (err == ERROR_SERVICE_DOES_NOT_EXIST) ? TRUE : FALSE;
    }

    BOOL ok = ChangeServiceConfig(
        hSvc, SERVICE_NO_CHANGE, dwStartType, SERVICE_NO_CHANGE,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL);

    if (dwStartType == SERVICE_DISABLED) {
        SERVICE_STATUS ss;
        ControlService(hSvc, SERVICE_CONTROL_STOP, &ss);
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hScm);
    return ok;
}

static BOOL GetServiceStartType(const TCHAR* svcName, DWORD* pStartType)
{
    SC_HANDLE hScm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hScm) return FALSE;

    SC_HANDLE hSvc = OpenService(hScm, svcName, SERVICE_QUERY_CONFIG);
    if (!hSvc) {
        CloseServiceHandle(hScm);
        return FALSE;
    }

    BYTE buf[4096];
    DWORD needed = 0;
    BOOL ok = QueryServiceConfig(hSvc, (LPQUERY_SERVICE_CONFIG)buf, sizeof(buf), &needed);
    if (ok)
        *pStartType = ((LPQUERY_SERVICE_CONFIG)buf)->dwStartType;

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hScm);
    return ok;
}

static BOOL CheckUpdateEnabled()
{
    DWORD st = 0;
    if (!GetServiceStartType(g_services[0], &st))
        return TRUE;
    return (st != SERVICE_DISABLED);
}

static BOOL DisableWindowsUpdate()
{
    BOOL anyOk = FALSE;
    for (int i = 0; i < SERVICE_COUNT; i++) {
        BOOL ok = SetServiceStartType(g_services[i], SERVICE_DISABLED);
        if (ok && i == 0) anyOk = TRUE; 
        if (i == 0 && !ok) return FALSE; 
    }

    HKEY hKey;
    if (RegCreateKeyEx(
            HKEY_LOCAL_MACHINE,
            _T("SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU"),
            0, NULL, REG_OPTION_NON_VOLATILE,
            KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        DWORD val1 = 1;
        DWORD val2 = 1;
        RegSetValueEx(hKey, _T("NoAutoUpdate"), 0, REG_DWORD, (BYTE*)&val1, sizeof(val1));
        RegSetValueEx(hKey, _T("AUOptions"),    0, REG_DWORD, (BYTE*)&val2, sizeof(val2));
        RegCloseKey(hKey);
    }

    HKEY hKey2;
    if (RegCreateKeyEx(
            HKEY_LOCAL_MACHINE,
            _T("SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate"),
            0, NULL, REG_OPTION_NON_VOLATILE,
            KEY_WRITE, NULL, &hKey2, NULL) == ERROR_SUCCESS)
    {
        DWORD val = 1;
        RegSetValueEx(hKey2, _T("DisableWindowsUpdateAccess"), 0, REG_DWORD, (BYTE*)&val, sizeof(val));
        RegCloseKey(hKey2);
    }

    return TRUE;
}

static BOOL EnableWindowsUpdate()
{
    for (int i = 0; i < SERVICE_COUNT; i++) {
        BOOL ok = SetServiceStartType(g_services[i], SERVICE_DEMAND_START);
        if (i == 0 && !ok) return FALSE; 
    }

    RegDeleteKey(
        HKEY_LOCAL_MACHINE,
        _T("SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU"));

    HKEY hKey;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
            _T("SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate"),
            0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
    {
        RegDeleteValue(hKey, _T("DisableWindowsUpdateAccess"));
        RegCloseKey(hKey);
    }

    return TRUE;
}

static void DrawToggle(HWND hWnd, int animPos)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);

    RECT rc;
    GetClientRect(hWnd, &rc);
    int W = rc.right;
    int H = rc.bottom;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, W, H);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    HBRUSH bgBr = CreateSolidBrush(CLR_PANEL);
    FillRect(memDC, &rc, bgBr);
    DeleteObject(bgBr);

    float t = animPos / 100.0f;
    BYTE r = (BYTE)(GetRValue(CLR_TRACK_OFF) + t * (GetRValue(CLR_TRACK_ON)  - GetRValue(CLR_TRACK_OFF)));
    BYTE g = (BYTE)(GetGValue(CLR_TRACK_OFF) + t * (GetGValue(CLR_TRACK_ON)  - GetGValue(CLR_TRACK_OFF)));
    BYTE b = (BYTE)(GetBValue(CLR_TRACK_OFF) + t * (GetBValue(CLR_TRACK_ON)  - GetBValue(CLR_TRACK_OFF)));
    COLORREF trackClr = RGB(r, g, b);

    int tx = (W - SLIDER_W) / 2;
    int ty = (H - SLIDER_H) / 2;

    HBRUSH trackBr = CreateSolidBrush(trackClr);
    HPEN borderPen = CreatePen(PS_SOLID, 1, CLR_BORDER_DARK);
    SelectObject(memDC, trackBr);
    SelectObject(memDC, borderPen);
    RoundRect(memDC, tx, ty, tx + SLIDER_W, ty + SLIDER_H, SLIDER_H, SLIDER_H);
    DeleteObject(trackBr);
    DeleteObject(borderPen);

    int thumbTravel = SLIDER_W - THUMB_D - 4;
    int thumbX = tx + 2 + (int)(thumbTravel * t);
    int thumbY = ty + (SLIDER_H - THUMB_D) / 2;

    HBRUSH shadowBr = CreateSolidBrush(CLR_THUMB_SHADOW);
    HPEN nullPen = CreatePen(PS_NULL, 0, 0);
    SelectObject(memDC, shadowBr);
    SelectObject(memDC, nullPen);
    Ellipse(memDC, thumbX + 1, thumbY + 2, thumbX + THUMB_D + 1, thumbY + THUMB_D + 2);
    DeleteObject(shadowBr);

    HBRUSH thumbBr = CreateSolidBrush(CLR_THUMB);
    HPEN thumbBorderPen = CreatePen(PS_SOLID, 1, CLR_BORDER_DARK);
    SelectObject(memDC, thumbBr);
    SelectObject(memDC, thumbBorderPen);
    Ellipse(memDC, thumbX, thumbY, thumbX + THUMB_D, thumbY + THUMB_D);
    DeleteObject(thumbBr);
    DeleteObject(thumbBorderPen);
    DeleteObject(nullPen);

    SetBkMode(memDC, TRANSPARENT);
    HFONT hF = CreateFont(11, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, _T("Segoe UI"));
    SelectObject(memDC, hF);

    BOOL bOn = (animPos > 50);
    if (bOn) {
        SetTextColor(memDC, RGB(22, 101, 52));
        RECT rtxt = {tx + 4, ty, tx + THUMB_D + 6, ty + SLIDER_H};
        DrawText(memDC, _T("ON"), -1, &rtxt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        SetTextColor(memDC, RGB(100, 116, 139));
        RECT rtxt = {tx + SLIDER_W - THUMB_D - 8, ty, tx + SLIDER_W - 2, ty + SLIDER_H};
        DrawText(memDC, _T("OFF"), -1, &rtxt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    DeleteObject(hF);

    BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    EndPaint(hWnd, &ps);
}

static LRESULT CALLBACK ToggleProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        int pos = (int)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        DrawToggle(hWnd, pos);
        return 0;
    }
    case WM_LBUTTONUP:
        SendMessage(GetParent(hWnd), WM_COMMAND,
            MAKEWPARAM(IDC_TOGGLE_BTN, BN_CLICKED), (LPARAM)hWnd);
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursor(NULL, IDC_HAND));
        return TRUE;
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

static void PaintMainWindow(HWND hWnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    RECT rc;
    GetClientRect(hWnd, &rc);
    int W = rc.right;
    int H = rc.bottom;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, W, H);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    HBRUSH bgBr = CreateSolidBrush(CLR_BG);
    FillRect(memDC, &rc, bgBr);
    DeleteObject(bgBr);

    RECT hdrRc = {0, 0, W, 72};
    HBRUSH hdrBr = CreateSolidBrush(CLR_PANEL_HEADER);
    FillRect(memDC, &hdrRc, hdrBr);
    DeleteObject(hdrBr);

    HPEN hdrLinePen = CreatePen(PS_SOLID, 1, CLR_BORDER);
    HPEN oldPen = (HPEN)SelectObject(memDC, hdrLinePen);
    MoveToEx(memDC, 0, 72, NULL);
    LineTo(memDC, W, 72);
    DeleteObject(hdrLinePen);

    int sx = 18, sy = 16;
    HBRUSH shieldBr = CreateSolidBrush(CLR_ACCENT);
    HPEN shieldPen = CreatePen(PS_NULL, 0, 0);
    SelectObject(memDC, shieldBr);
    SelectObject(memDC, shieldPen);
    RECT shieldTop = {sx, sy, sx+30, sy+26};
    HRGN hTopRgn = CreateRoundRectRgn(sx, sy, sx+30, sy+26, 8, 8);
    FillRgn(memDC, hTopRgn, shieldBr);
    DeleteObject(hTopRgn);
    POINT tri[3] = {{sx, sy+20}, {sx+30, sy+20}, {sx+15, sy+38}};
    HRGN hTriRgn = CreatePolygonRgn(tri, 3, WINDING);
    FillRgn(memDC, hTriRgn, shieldBr);
    DeleteObject(hTriRgn);
    DeleteObject(shieldBr);
    DeleteObject(shieldPen);

    if (!g_bEnabled) {
        HPEN chkPen = CreatePen(PS_SOLID, 3, RGB(255,255,255));
        SelectObject(memDC, chkPen);
        MoveToEx(memDC, sx+9, sy+11, NULL); LineTo(memDC, sx+21, sy+23);
        MoveToEx(memDC, sx+21, sy+11, NULL); LineTo(memDC, sx+9, sy+23);
        DeleteObject(chkPen);
    } else {
        HPEN chkPen = CreatePen(PS_SOLID, 3, RGB(255,255,255));
        SelectObject(memDC, chkPen);
        MoveToEx(memDC, sx+7, sy+18, NULL); LineTo(memDC, sx+13, sy+25);
        LineTo(memDC, sx+23, sy+12);
        DeleteObject(chkPen);
    }

    SetBkMode(memDC, TRANSPARENT);
    SelectObject(memDC, g_hFontTitle);
    SetTextColor(memDC, CLR_TEXT_DARK);
    RECT titleRc = {56, 14, W - 10, 45};
    DrawText(memDC, _T("Windows Update Blocker"), -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(memDC, g_hFontSmall);
    SetTextColor(memDC, CLR_TEXT_LIGHT);
    RECT subRc = {57, 44, W - 10, 68};
    DrawText(memDC, _T("by Ari Sohandri Putra  \xB7  v2.0"), -1, &subRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT panel = {16, 84, W - 16, H - 52};
    HBRUSH panelBr = CreateSolidBrush(CLR_PANEL);
    HPEN panelPen = CreatePen(PS_SOLID, 1, CLR_BORDER);
    SelectObject(memDC, panelBr);
    SelectObject(memDC, panelPen);
    RoundRect(memDC, panel.left, panel.top, panel.right, panel.bottom, 12, 12);
    DeleteObject(panelBr);
    DeleteObject(panelPen);

    COLORREF dotClr = g_bEnabled ? CLR_ON : CLR_OFF;
    HBRUSH dotBr = CreateSolidBrush(dotClr);
    HPEN dotPen = CreatePen(PS_NULL, 0, 0);
    SelectObject(memDC, dotBr);
    SelectObject(memDC, dotPen);
    int dotCx = W/2 - 62;
    Ellipse(memDC, dotCx, 101, dotCx+10, 111);
    DeleteObject(dotBr);
    DeleteObject(dotPen);

    HPEN divPen = CreatePen(PS_SOLID, 1, CLR_BORDER);
    SelectObject(memDC, divPen);
    MoveToEx(memDC, 32, 174, NULL);
    LineTo(memDC, W - 32, 174);
    DeleteObject(divPen);

    RECT footerRc = {0, H - 46, W, H};
    HBRUSH footerBr = CreateSolidBrush(CLR_PANEL_HEADER);
    FillRect(memDC, &footerRc, footerBr);
    DeleteObject(footerBr);
    HPEN footerPen = CreatePen(PS_SOLID, 1, CLR_BORDER);
    SelectObject(memDC, footerPen);
    MoveToEx(memDC, 0, H - 46, NULL);
    LineTo(memDC, W, H - 46);
    DeleteObject(footerPen);

    BOOL bAdmin = IsRunAsAdmin();
    RECT adminRc = {16, H - 36, W/2, H - 10};
    SelectObject(memDC, g_hFontSmall);
    SetTextColor(memDC, bAdmin ? CLR_ON : CLR_OFF);
    DrawText(memDC, bAdmin ? _T("\u2713 Running as Administrator") : _T("\u2717 No Admin Rights"),
        -1, &adminRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(memDC, oldPen);
    BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    EndPaint(hWnd, &ps);
}

static void UpdateToggleAnimation()
{
    if (!g_bAnimating) return;
    int cur = (int)GetWindowLongPtr(g_hToggleBtn, GWLP_USERDATA);
    int target = (g_animTarget == 1) ? 100 : 0;
    int step = target - cur;
    if (step == 0) {
        g_bAnimating = FALSE;
        KillTimer(g_hWnd, ANIM_TIMER_ID);
        return;
    }
    int delta = step / 3;
    if (delta == 0) delta = (step > 0) ? 1 : -1;
    cur += delta;
    if ((g_animTarget == 1 && cur >= 100) || (g_animTarget == 0 && cur <= 0)) {
        cur = target;
        g_bAnimating = FALSE;
        KillTimer(g_hWnd, ANIM_TIMER_ID);
    }
    SetWindowLongPtr(g_hToggleBtn, GWLP_USERDATA, (LONG_PTR)cur);
    InvalidateRect(g_hToggleBtn, NULL, FALSE);
}

static void UpdateStatusUI()
{
    if (g_bEnabled) {
        SetWindowText(g_hStatusLbl, _T("Windows Update: ACTIVE"));
        SetWindowText(g_hInfoLbl,
            _T("Automatic updates are running normally.\r\n")
            _T("Click the toggle to block Windows Update."));
    } else {
        SetWindowText(g_hStatusLbl, _T("Windows Update: BLOCKED"));
        SetWindowText(g_hInfoLbl,
            _T("Automatic updates have been disabled.\r\n")
            _T("Services and registry policies are locked."));
    }
    InvalidateRect(g_hWnd, NULL, FALSE);
}

static void DoToggle()
{
    if (!IsRunAsAdmin()) {
        MessageBox(g_hWnd,
            _T("This program requires Administrator privileges.\r\n\r\n")
            _T("Please right-click the .exe and select 'Run as administrator'."),
            _T("Administrator Required"), MB_ICONWARNING | MB_OK);
        return;
    }

    BOOL newState = !g_bEnabled;
    BOOL ok = FALSE;

    if (newState == FALSE) {
        ok = DisableWindowsUpdate();
    } else {
        ok = EnableWindowsUpdate();
    }

    if (ok) {
        g_bEnabled = newState;
        g_sliderPos  = g_bEnabled ? 1 : 0;
        g_animTarget = g_sliderPos;
        g_bAnimating = TRUE;
        SetTimer(g_hWnd, ANIM_TIMER_ID, 16, NULL);
        UpdateStatusUI();
    } else {
        DWORD err = GetLastError();
        TCHAR msg[512];
        wsprintf(msg,
            _T("Failed to change service status.\r\n")
            _T("Error code: %lu\r\n\r\n")
            _T("Note: Some services (e.g. WaaSMedicSvc) are\r\n")
            _T("protected by Windows and may not be modified.\r\n")
            _T("Core Windows Update service has been updated."),
            err);
        MessageBox(g_hWnd, msg, _T("Warning"), MB_ICONWARNING | MB_OK);
        // Even partial success — update the state based on actual service status
        g_bEnabled = CheckUpdateEnabled();
        g_sliderPos  = g_bEnabled ? 1 : 0;
        g_animTarget = g_sliderPos;
        g_bAnimating = TRUE;
        SetTimer(g_hWnd, ANIM_TIMER_ID, 16, NULL);
        UpdateStatusUI();
    }
}

static void AddTrayIcon(HWND hWnd)
{
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(NOTIFYICONDATA);
    g_nid.hWnd             = hWnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = LoadIcon(NULL, IDI_APPLICATION);
    lstrcpy(g_nid.szTip, _T("Windows Update Blocker"));
    Shell_NotifyIcon(NIM_ADD, &g_nid);
}

static void RemoveTrayIcon()
{
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
}

static void ShowTrayMenu(HWND hWnd)
{
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, ID_TRAY_SHOW, _T("Show Window"));
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, _T("Exit"));
    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
}

static WNDPROC g_oldBtnProc = NULL;
static BOOL g_bBtnHover = FALSE;

static LRESULT CALLBACK RefreshBtnProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!g_bBtnHover) {
            g_bBtnHover = TRUE;
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hWnd, 0};
            TrackMouseEvent(&tme);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        g_bBtnHover = FALSE;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        COLORREF bgClr = g_bBtnHover ? CLR_BTN_HOVER : CLR_BTN_BG;
        HBRUSH br = CreateSolidBrush(bgClr);
        HPEN pen = CreatePen(PS_SOLID, 1, CLR_BORDER_DARK);
        SelectObject(hdc, br);
        SelectObject(hdc, pen);
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
        DeleteObject(br);
        DeleteObject(pen);
        SetBkMode(hdc, TRANSPARENT);
        SelectObject(hdc, g_hFontSmall);
        SetTextColor(hdc, CLR_ACCENT);
        DrawText(hdc, _T("\u21BA  Refresh Status"), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        SetCursor(LoadCursor(NULL, IDC_HAND));
        return TRUE;
    }
    return CallWindowProc(g_oldBtnProc, hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HINSTANCE hInst = GetModuleHandle(NULL);

        g_hFontTitle  = CreateFont(19, 0, 0, 0, FW_BOLD,   0,0,0, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, _T("Segoe UI"));
        g_hFontSub    = CreateFont(16, 0, 0, 0, FW_SEMIBOLD,0,0,0, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, _T("Segoe UI"));
        g_hFontNormal = CreateFont(15, 0, 0, 0, FW_SEMIBOLD,0,0,0, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, _T("Segoe UI"));
        g_hFontSmall  = CreateFont(13, 0, 0, 0, FW_NORMAL,  0,0,0, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, _T("Segoe UI"));
        g_hFontFooter = CreateFont(12, 0, 0, 0, FW_NORMAL,  0,0,0, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, _T("Segoe UI"));
        g_hStatusLbl = CreateWindow(_T("STATIC"), _T(""),
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            30, 95, 300, 24, hWnd, (HMENU)IDC_STATUS_LBL, hInst, NULL);
        SendMessage(g_hStatusLbl, WM_SETFONT, (WPARAM)g_hFontSub, TRUE);
        g_hToggleBtn = CreateWindow(_T("WUBToggle"), _T(""),
            WS_CHILD | WS_VISIBLE,
            (360 - SLIDER_W - 16) / 2, 128, SLIDER_W + 16, SLIDER_H + 8,
            hWnd, (HMENU)IDC_TOGGLE_BTN, hInst, NULL);
        SetWindowLongPtr(g_hToggleBtn, GWLP_USERDATA, (LONG_PTR)100);

        g_hInfoLbl = CreateWindow(_T("STATIC"), _T(""),
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            28, 182, 304, 48, hWnd, (HMENU)IDC_INFO_LBL, hInst, NULL);
        SendMessage(g_hInfoLbl, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);
        g_hRefreshBtn = CreateWindow(_T("BUTTON"), _T(""),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            116, 238, 128, 30, hWnd, (HMENU)IDC_REFRESH_BTN, hInst, NULL);
        g_oldBtnProc = (WNDPROC)SetWindowLongPtr(g_hRefreshBtn, GWLP_WNDPROC, (LONG_PTR)RefreshBtnProc);
        g_bEnabled = CheckUpdateEnabled();
        g_sliderPos = g_bEnabled ? 1 : 0;
        SetWindowLongPtr(g_hToggleBtn, GWLP_USERDATA, (LONG_PTR)(g_bEnabled ? 100 : 0));
        UpdateStatusUI();

        AddTrayIcon(hWnd);
        return 0;
    }

    case WM_PAINT:
        PaintMainWindow(hWnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLORSTATIC:
    {
        HDC hdcS = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        SetBkMode(hdcS, TRANSPARENT);
        if (hCtrl == g_hStatusLbl) {
            SetTextColor(hdcS, g_bEnabled ? CLR_ON : CLR_OFF);
        } else {
            SetTextColor(hdcS, CLR_TEXT_MED);
        }
        return (LRESULT)CreateSolidBrush(CLR_PANEL); // White bg for statics inside panel
    }

    case WM_TIMER:
        if (wParam == ANIM_TIMER_ID)
            UpdateToggleAnimation();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_TOGGLE_BTN:
            if (HIWORD(wParam) == BN_CLICKED) DoToggle();
            break;
        case IDC_REFRESH_BTN:
            g_bEnabled = CheckUpdateEnabled();
            g_sliderPos = g_bEnabled ? 1 : 0;
            SetWindowLongPtr(g_hToggleBtn, GWLP_USERDATA, (LONG_PTR)(g_bEnabled ? 100 : 0));
            UpdateStatusUI();
            InvalidateRect(g_hToggleBtn, NULL, FALSE);
            break;
        case ID_TRAY_SHOW:
            ShowWindow(hWnd, SW_SHOW);
            SetForegroundWindow(hWnd);
            break;
        case ID_TRAY_EXIT:
            DestroyWindow(hWnd);
            break;
        }
        return 0;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) ShowTrayMenu(hWnd);
        else if (lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(hWnd, SW_SHOW);
            SetForegroundWindow(hWnd);
        }
        return 0;

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) ShowWindow(hWnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        RemoveTrayIcon();
        if (g_hFontTitle)  DeleteObject(g_hFontTitle);
        if (g_hFontSub)    DeleteObject(g_hFontSub);
        if (g_hFontNormal) DeleteObject(g_hFontNormal);
        if (g_hFontSmall)  DeleteObject(g_hFontSmall);
        if (g_hFontFooter) DeleteObject(g_hFontFooter);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI _tWinMain(HINSTANCE hInst, HINSTANCE, LPTSTR, int nCmdShow)
{
    HANDLE hMutex = CreateMutex(NULL, TRUE, _T("WindowsUpdateBlocker_SingleInstance_AriSP"));
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBox(NULL,
            _T("Windows Update Blocker is already running."),
            _T("Already Running"), MB_ICONINFORMATION | MB_OK);
        return 0;
    }

    if (!IsRunAsAdmin()) {
        TCHAR path[MAX_PATH];
        GetModuleFileName(NULL, path, MAX_PATH);
        HINSTANCE hRet = ShellExecute(NULL, _T("runas"), path, NULL, NULL, SW_SHOWNORMAL);
        if ((INT_PTR)hRet > 32) {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return 0;
        }
    }

    WNDCLASSEX wcT;
    ZeroMemory(&wcT, sizeof(wcT));
    wcT.cbSize        = sizeof(WNDCLASSEX);
    wcT.lpfnWndProc   = ToggleProc;
    wcT.hInstance     = hInst;
    wcT.hCursor       = LoadCursor(NULL, IDC_HAND);
    wcT.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcT.lpszClassName = _T("WUBToggle");
    RegisterClassEx(&wcT);

    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wc.lpszClassName = _T("WUBlockerWnd");
    RegisterClassEx(&wc);

    int wW = 360, wH = 330;
    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);

    g_hWnd = CreateWindowEx(
        WS_EX_APPWINDOW,
        _T("WUBlockerWnd"),
        _T("Windows Update Blocker"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (scrW - wW) / 2, (scrH - wH) / 2, wW, wH,
        NULL, NULL, hInst, NULL);

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    return (int)msg.wParam;
}
