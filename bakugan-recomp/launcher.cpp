/*
 * Bakugan launcher.
 *
 * The title renders 1280x720; the launcher chooses the presentation size and
 * the frame cap, then starts BakuganRecomp.exe with the matching environment.
 * No game data lives here: the ELF is found relative to the launcher or from
 * PS3_VFS_ROOT.
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ID_PRESET  101
#define ID_OUTPUT  102
#define ID_CAP     103
#define ID_LAUNCH  104
#define ID_STATUS  105

struct Preset { const char* label; const char* output; const char* cap; };
static const Preset kPresets[] = {
    {"720p / 30 FPS",              "720p",  "30"},
    {"1080p / 30 FPS (default)",   "1080p", "30"},
    {"1080p / 60 FPS",             "1080p", "60"},
    {"1080p / monitor",            "1080p", "monitor"},
    {"1440p / 60 FPS",             "1440p", "60"},
    {"Personalizado",              "1080p", "0"},
};
static const char* kOutputs[] = {"auto", "native", "1080p", "1440p", "2160p"};
static const char* kCaps[] = {"30", "60", "90", "120", "monitor", "0"};

static char s_dir[MAX_PATH];
static HWND s_preset, s_output, s_cap, s_status;

static void game_paths(char* exe, size_t nexe, char* elf, size_t nelf)
{
    exe[0] = elf[0] = 0;
    snprintf(exe, nexe, "%s\\BakuganRecomp.exe", s_dir);

    const char* vfs = getenv("PS3_VFS_ROOT");
    if (vfs && *vfs) {
        snprintf(elf, nelf, "%s\\PS3_GAME\\USRDIR\\EBOOT.elf", vfs);
        if (GetFileAttributesA(elf) != INVALID_FILE_ATTRIBUTES) return;
    }
    snprintf(elf, nelf, "%s\\..\\..\\game\\PS3_GAME\\USRDIR\\EBOOT.elf", s_dir);
    if (GetFileAttributesA(elf) != INVALID_FILE_ATTRIBUTES) return;
    snprintf(elf, nelf, "%s\\game\\PS3_GAME\\USRDIR\\EBOOT.elf", s_dir);
    if (GetFileAttributesA(elf) != INVALID_FILE_ATTRIBUTES) return;

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = "EBOOT.elf\0EBOOT.elf\0Todos\0*.*\0";
    ofn.lpstrFile = elf;
    ofn.nMaxFile = (DWORD)nelf;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameA(&ofn)) elf[0] = 0;
}

static void launch(int preset_index, const char* output, const char* cap)
{
    char exe[MAX_PATH * 2], elf[MAX_PATH * 2];
    game_paths(exe, sizeof exe, elf, sizeof elf);
    if (!elf[0]) { SetWindowTextA(s_status, "Falta EBOOT.elf."); return; }

    char vfs[MAX_PATH * 2];
    snprintf(vfs, sizeof vfs, "%s", elf);
    char* at = strstr(vfs, "\\PS3_GAME\\");
    if (at) *at = 0; else { at = strrchr(vfs, '\\'); if (at) *at = 0; }

    SetEnvironmentVariableA("PS3_VFS_ROOT", vfs);
    SetEnvironmentVariableA("PS3_TITLE", "Bakugan: Battle Brawlers");
    SetEnvironmentVariableA("PS3_VERBOSE", "0");
    SetEnvironmentVariableA("PS3_INTERNAL", "720");
    SetEnvironmentVariableA("PS3_OUTPUT", output);
    SetEnvironmentVariableA("PS3_FSR", "auto");
    SetEnvironmentVariableA("PS3_FSR_SHARPNESS", "0.5");
    SetEnvironmentVariableA("PS3_FPS_CAP", strcmp(cap, "0") ? cap : NULL);
    SetEnvironmentVariableA("PS3_TICK_HZ", atoi(cap) > 60 ? cap : NULL);
    SetEnvironmentVariableA("PS3_WINDOW_MODE", "borderless");

    char cmd[MAX_PATH * 4];
    snprintf(cmd, sizeof cmd, "\"%s\" \"%s\"", exe, elf);
    STARTUPINFOA si{}; si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, s_dir, &si, &pi)) {
        char msg[512];
        snprintf(msg, sizeof msg, "No se pudo iniciar el juego (error %lu).\n%s",
                 GetLastError(), exe);
        SetWindowTextA(s_status, msg);
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    (void)preset_index;
    SetWindowTextA(s_status, "Juego lanzado. Esta ventana se puede cerrar.");
}

static void sync_preset(void)
{
    int i = (int)SendMessageA(s_preset, CB_GETCURSEL, 0, 0);
    if (i < 0) i = 1;
    if (i < 5) {
        for (int o = 0; o < 4; ++o)
            if (!strcmp(kOutputs[o], kPresets[i].output))
                SendMessageA(s_output, CB_SETCURSEL, o, 0);
        for (int c = 0; c < 6; ++c)
            if (!strcmp(kCaps[c], kPresets[i].cap))
                SendMessageA(s_cap, CB_SETCURSEL, c, 0);
    }
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        CreateWindowA("STATIC", "Preset", WS_CHILD | WS_VISIBLE, 16, 14, 100, 20,
                      hwnd, NULL, NULL, NULL);
        s_preset = CreateWindowA("COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                 16, 34, 300, 200, hwnd, (HMENU)ID_PRESET, NULL, NULL);
        for (const Preset& p : kPresets)
            SendMessageA(s_preset, CB_ADDSTRING, 0, (LPARAM)p.label);
        SendMessageA(s_preset, CB_SETCURSEL, 1, 0);

        CreateWindowA("STATIC", "Salida", WS_CHILD | WS_VISIBLE, 16, 72, 100, 20,
                      hwnd, NULL, NULL, NULL);
        s_output = CreateWindowA("COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                 16, 92, 140, 160, hwnd, (HMENU)ID_OUTPUT, NULL, NULL);
        for (const char* o : kOutputs) SendMessageA(s_output, CB_ADDSTRING, 0, (LPARAM)o);
        SendMessageA(s_output, CB_SETCURSEL, 1, 0);

        CreateWindowA("STATIC", "FPS", WS_CHILD | WS_VISIBLE, 180, 72, 60, 20,
                      hwnd, NULL, NULL, NULL);
        s_cap = CreateWindowA("COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                              180, 92, 80, 120, hwnd, (HMENU)ID_CAP, NULL, NULL);
        for (const char* c : kCaps) SendMessageA(s_cap, CB_ADDSTRING, 0, (LPARAM)c);
        SendMessageA(s_cap, CB_SETCURSEL, 0, 0);

        CreateWindowA("BUTTON", "Jugar", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                      16, 140, 120, 34, hwnd, (HMENU)ID_LAUNCH, NULL, NULL);
        s_status = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE,
                                 16, 186, 400, 44, hwnd, (HMENU)ID_STATUS, NULL, NULL);
        SendMessageA(s_status, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        SetWindowTextA(s_status,
            "El juego renderiza 1280x720; la salida usa FSR1. 1080p/30 por defecto.");
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_PRESET && HIWORD(wp) == CBN_SELCHANGE) sync_preset();
        if (LOWORD(wp) == ID_LAUNCH) {
            char out[32] = "1080p", cap[8] = "30";
            int oi = (int)SendMessageA(s_output, CB_GETCURSEL, 0, 0);
            int ci = (int)SendMessageA(s_cap, CB_GETCURSEL, 0, 0);
            if (oi >= 0 && oi < 5) snprintf(out, sizeof out, "%s", kOutputs[oi]);
            if (ci >= 0 && ci < 6) snprintf(cap, sizeof cap, "%s", kCaps[ci]);
            launch((int)SendMessageA(s_preset, CB_GETCURSEL, 0, 0), out, cap);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show)
{
    GetModuleFileNameA(NULL, s_dir, MAX_PATH);
    char* slash = strrchr(s_dir, '\\');
    if (slash) *slash = 0;

    const char* cli = GetCommandLineA();
    if (cli && strstr(cli, "--preset 720p30")) { launch(0, "720p", "30"); return 0; }
    if (cli && strstr(cli, "--preset 1080p60")) { launch(2, "1080p", "60"); return 0; }
    if (cli && strstr(cli, "--preset 1080pmonitor")) { launch(3, "1080p", "monitor"); return 0; }
    if (cli && strstr(cli, "--preset 1440p60")) { launch(4, "1440p", "60"); return 0; }
    if (cli && strstr(cli, "--preset 1080p30")) { launch(1, "1080p", "30"); return 0; }

    WNDCLASSA wc{};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "bakugan_launcher";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("bakugan_launcher", "Bakugan: Battle Brawlers",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                              CW_USEDEFAULT, CW_USEDEFAULT, 460, 280,
                              NULL, NULL, instance, NULL);
    if (!hwnd) return 1;
    ShowWindow(hwnd, show);
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
