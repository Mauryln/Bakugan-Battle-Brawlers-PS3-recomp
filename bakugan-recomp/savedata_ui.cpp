/*
 * Host save-data applet for the port.
 *
 * The firmware save manager is a system process that draws the save list,
 * asks the player which directory to use and then hands the answer back to
 * cellSaveData. This port has no such process, so the HLE layer exposes a
 * chooser hook (ps3_savedata_set_chooser) and this file implements it with a
 * small native window. It runs on its own thread and blocks the caller until
 * the player closes it, which is exactly the firmware's behaviour: the title
 * is stuck inside cellSaveData while the applet is open.
 *
 * The window uses only system window classes (LISTBOX, BUTTON, STATIC), so it
 * needs no common-controls initialization and cannot fail because a manifest
 * was missing. Set PS3_SAVEDATA_UI=0 to fall back to the headless defaults.
 */

#include <Windows.h>
#include <process.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cellSaveData.h"

#define UI_CLASS    "bakugan_savedata_picker"
#define UI_WIDTH    760
#define UI_HEIGHT   460
#define ID_LIST     1001
#define ID_OK       1002
#define ID_CANCEL   1003

struct UiRequest {
    int is_save;
    int count;
    int focus;
    const CellSaveDataSlotInfo* slots;
    const char* new_name;
    char out_dir[CELL_SAVEDATA_DIRNAME_SIZE];
    int result;                 /* 1 picked, 0 cancelled, -1 failed */
    HWND hwnd;
    HWND list;
    HWND detail;
};

static void format_date(s64 mtime, char* out, size_t n)
{
    time_t t = (time_t)mtime;
    struct tm tmv;
    if (mtime <= 0 || localtime_s(&tmv, &t) != 0) {
        snprintf(out, n, "unknown date");
        return;
    }
    strftime(out, n, "%Y-%m-%d %H:%M", &tmv);
}

/* PARAM.SFO strings are UTF-8 (the title's own subtitle carries a trademark
 * sign), while the window classes here are the ANSI ones. Convert once so the
 * list shows text instead of mojibake; fall back to the raw bytes if the
 * runtime refuses the conversion. */
static void to_ansi(const char* in, char* out, size_t n)
{
    wchar_t wide[512];
    int wl = MultiByteToWideChar(CP_UTF8, 0, in, -1, wide, (int)(sizeof wide / sizeof wide[0]));
    if (wl > 0 && WideCharToMultiByte(CP_ACP, 0, wide, -1, out, (int)n, NULL, NULL) > 0)
        return;
    snprintf(out, n, "%s", in);
}

static void list_label(const CellSaveDataSlotInfo* s, char* out, size_t n)
{
    char date[48], title[sizeof s->subTitle];
    format_date(s->mtime, date, sizeof date);
    const char* raw = s->subTitle[0] ? s->subTitle : (s->title[0] ? s->title : s->dirName);
    to_ansi(raw, title, sizeof title);
    snprintf(out, n, "%s   (%s)   %s", title, s->dirName, date);
}

static void show_details(UiRequest* req, int index)
{
    char text[768];
    if (index < 0 || index >= req->count) {
        SetWindowTextA(req->detail, "Use the arrow keys and Enter, or press Esc to cancel.");
        return;
    }
    const CellSaveDataSlotInfo* s = &req->slots[index];
    char title[sizeof s->title], sub[sizeof s->subTitle], detail[320];
    size_t i;
    to_ansi(s->title, title, sizeof title);
    to_ansi(s->subTitle, sub, sizeof sub);
    for (i = 0; i + 1 < sizeof detail && s->detail[i]; i++)
        detail[i] = (s->detail[i] == '\n' || s->detail[i] == '\r') ? ' ' : s->detail[i];
    detail[i] = 0;
    snprintf(text, sizeof text, "%s\n%s\n%s\n%s  -  %u KB",
             title, sub, detail, s->dirName, s->sizeKB);
    SetWindowTextA(req->detail, text);
}

static void accept_choice(UiRequest* req)
{
    int sel = (int)SendMessageA(req->list, LB_GETCURSEL, 0, 0);
    if (sel < 0 && req->count > 0) sel = 0;
    if (sel < 0 || sel >= req->count) {
        req->result = 0;
    } else {
        snprintf(req->out_dir, sizeof req->out_dir, "%s", req->slots[sel].dirName);
        req->result = 1;
    }
    PostMessageA(req->hwnd, WM_CLOSE, 0, 0);
}

static void centre_over_game(HWND hwnd)
{
    RECT area = {0, 0, 0, 0};
    HWND game = FindWindowA("ps3recomp_d3d12", NULL);
    RECT target;
    if (game && GetWindowRect(game, &target)) {
        area = target;
    } else {
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0);
    }
    RECT frame = {0, 0, UI_WIDTH, UI_HEIGHT};
    AdjustWindowRect(&frame, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    int w = frame.right - frame.left, h = frame.bottom - frame.top;
    int x = area.left + ((area.right - area.left) - w) / 2;
    int y = area.top + ((area.bottom - area.top) - h) / 2;
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
}

static LRESULT CALLBACK picker_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    UiRequest* req = (UiRequest*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == ID_LIST && code == LBN_SELCHANGE && req) {
            show_details(req, (int)SendMessageA(req->list, LB_GETCURSEL, 0, 0));
            return 0;
        }
        if ((id == ID_LIST && code == LBN_DBLCLK) || id == ID_OK) {
            if (req) accept_choice(req);
            return 0;
        }
        if (id == ID_CANCEL) {
            if (req) req->result = 0;
            PostMessageA(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    }
    case WM_KEYDOWN:
        if (wp == VK_RETURN) {
            if (req) accept_choice(req);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            if (req) req->result = 0;
            PostMessageA(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void register_picker_class(void)
{
    static int registered;
    if (registered) return;
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = picker_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = UI_CLASS;
    RegisterClassExA(&wc);
    registered = 1;
}

static unsigned __stdcall picker_thread(void* param)
{
    UiRequest* req = (UiRequest*)param;
    register_picker_class();

    const char* heading = req->is_save ? "Choose a slot to save to"
                                       : "Choose a save data slot";
    req->hwnd = CreateWindowExA(
        WS_EX_TOPMOST, UI_CLASS, "Bakugan: Battle Brawlers - Save Data",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, UI_WIDTH, UI_HEIGHT,
        NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!req->hwnd) {
        req->result = -1;
        return 0;
    }
    SetWindowLongPtrA(req->hwnd, GWLP_USERDATA, (LONG_PTR)req);

    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HWND label = CreateWindowExA(0, "STATIC", heading, WS_CHILD | WS_VISIBLE,
                                 16, 12, UI_WIDTH - 32, 20, req->hwnd, NULL, NULL, NULL);
    req->list = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP |
                                    LBS_NOTIFY | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
                                16, 38, UI_WIDTH - 32, 260,
                                req->hwnd, (HMENU)ID_LIST, NULL, NULL);
    req->detail = CreateWindowExA(WS_EX_CLIENTEDGE, "STATIC", "",
                                  WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                                  16, 306, UI_WIDTH - 32, 76,
                                  req->hwnd, NULL, NULL, NULL);
    HWND ok = CreateWindowExA(0, "BUTTON", req->is_save ? "Save" : "Load",
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                              UI_WIDTH - 208, 394, 90, 30,
                              req->hwnd, (HMENU)ID_OK, NULL, NULL);
    HWND cancel = CreateWindowExA(0, "BUTTON", "Cancel",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                  UI_WIDTH - 110, 394, 90, 30,
                                  req->hwnd, (HMENU)ID_CANCEL, NULL, NULL);

    HWND children[] = {label, req->list, req->detail, ok, cancel};
    for (size_t i = 0; i < sizeof children / sizeof children[0]; i++)
        SendMessageA(children[i], WM_SETFONT, (WPARAM)font, TRUE);

    for (int i = 0; i < req->count; i++) {
        char text[256];
        list_label(&req->slots[i], text, sizeof text);
        SendMessageA(req->list, LB_ADDSTRING, 0, (LPARAM)text);
    }
    int focus = req->focus;
    if (focus < 0 || focus >= req->count) focus = req->count ? 0 : -1;
    if (focus >= 0) SendMessageA(req->list, LB_SETCURSEL, focus, 0);
    show_details(req, focus);

    centre_over_game(req->hwnd);
    SetForegroundWindow(req->hwnd);
    SetFocus(req->list);

    /* The title kept the cursor hidden/clipped for gameplay; give it back so
     * the player can click the list, then put it the way it was. */
    ClipCursor(NULL);
    int cursor_visible = ShowCursor(TRUE) > 0;
    while (ShowCursor(TRUE) < 0) {}

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    while (ShowCursor(FALSE) >= 0) {}
    if (cursor_visible) while (ShowCursor(TRUE) < 0) {}

    HWND game = FindWindowA("ps3recomp_d3d12", NULL);
    if (game) SetForegroundWindow(game);

    if (req->result != 1) req->result = 0;
    return 0;
}

static int pick_slot(int is_save, const CellSaveDataSlotInfo* slots, int count,
                     int focus, const char* new_name, char* out_dir, size_t out_size)
{
    (void)new_name;
    UiRequest req;
    memset(&req, 0, sizeof req);
    req.is_save = is_save;
    req.count = count;
    req.focus = focus;
    req.slots = slots;
    req.result = 0;

    unsigned id = 0;
    HANDLE thread = (HANDLE)_beginthreadex(NULL, 0, picker_thread, &req, 0, &id);
    if (!thread) {
        printf("[savedata-ui] could not start the picker thread\n");
        return -1;
    }
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);

    if (req.result != 1) {
        printf("[savedata-ui] selection cancelled\n");
        return req.result;
    }
    printf("[savedata-ui] %s -> '%s'\n", is_save ? "save" : "load", req.out_dir);
    snprintf(out_dir, out_size, "%s", req.out_dir);
    return 1;
}

void bakugan_savedata_ui_install(void)
{
    const char* off = getenv("PS3_SAVEDATA_UI");
    if (off && *off == '0') {
        printf("[savedata-ui] disabled by PS3_SAVEDATA_UI=0\n");
        return;
    }
    ps3_savedata_set_chooser(pick_slot);
    printf("[savedata-ui] native save picker installed\n");
}
