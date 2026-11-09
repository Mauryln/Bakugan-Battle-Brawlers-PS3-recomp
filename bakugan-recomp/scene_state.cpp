/*
 * Reproducible scene checkpoints for testers and agents.
 *
 * A checkpoint is not a host-memory image: the PPU/SPU are native code and a
 * session owns D3D12, WASAPI and file handles that cannot be recreated in
 * another process. What is saved is the pair that makes the scene reachable
 * again: the complete pad timeline since boot and a copy of the save data.
 * Loading replays that timeline deterministically until the recorded point.
 *
 * F5 writes a checkpoint; F9 asks the launcher to restart and load the newest
 * one. PS3_SCENE_DIR enables both keys.
 */

#include "scene_state.h"

#if !defined(_WIN32)
void scene_state_tick(void) {}
#else

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" int cellAudioQuit(void);
extern "C" int cellPad_replay_cursor(void);
void bakugan_input_shutdown(void);
extern "C" void rsx_d3d12_backend_flush_shader_cache(void);

#define SCENE_PATH_MAX 1024
#define SCENE_MAX_BYTES (64u * 1024u * 1024u)

static char s_scene_dir[SCENE_PATH_MAX];
static int  s_initialized = 0;
static int  s_f5_down = 0;
static int  s_f9_down = 0;
static volatile LONG s_save_busy = 0;

static void scene_log(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);
}

static int path_exists(const char* path)
{
    return path && *path && GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static void make_dirs(const char* path)
{
    char tmp[SCENE_PATH_MAX];
    strncpy(tmp, path, sizeof tmp - 1);
    tmp[sizeof tmp - 1] = 0;
    for (char* p = tmp + 1; *p; ++p) {
        if (*p == '\\' || *p == '/') {
            char saved = *p;
            *p = 0;
            CreateDirectoryA(tmp, NULL);
            *p = saved;
        }
    }
    CreateDirectoryA(tmp, NULL);
}

static void stamp_local(char* out, size_t n)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(out, n, "%04u%02u%02u-%02u%02u%02u",
             (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
             (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);
}

static char* read_file(const char* path, size_t* out_n)
{
    if (out_n) *out_n = 0;
    HANDLE h = CreateFileA(path, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;

    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 ||
        (uint64_t)size.QuadPart > SCENE_MAX_BYTES) {
        CloseHandle(h);
        return NULL;
    }

    size_t n = (size_t)size.QuadPart;
    char* data = (char*)malloc(n + 1);
    if (!data) { CloseHandle(h); return NULL; }

    size_t got = 0;
    while (got < n) {
        DWORD chunk = (DWORD)((n - got) > 0x40000000u ? 0x40000000u : (n - got));
        DWORD read = 0;
        if (!ReadFile(h, data + got, chunk, &read, NULL) || read == 0) break;
        got += read;
    }
    CloseHandle(h);
    data[got] = 0;
    if (out_n) *out_n = got;
    return data;
}

static int write_file(const char* path, const char* data, size_t n)
{
    HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ,
                           NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int ok = 1;
    size_t put = 0;
    while (put < n) {
        DWORD chunk = (DWORD)((n - put) > 0x40000000u ? 0x40000000u : (n - put));
        DWORD wrote = 0;
        if (!WriteFile(h, data + put, chunk, &wrote, NULL) || wrote == 0) {
            ok = 0;
            break;
        }
        put += wrote;
    }
    CloseHandle(h);
    return ok;
}

/* Copy a pad recording without an incomplete trailing line. The recorder
 * flushes after every sample, so the file is usually clean; this also makes a
 * checkpoint taken in the middle of a write safe. */
static int copy_padrec_clean(const char* src, const char* dst)
{
    size_t n = 0;
    char* text = read_file(src, &n);
    if (!text) return 0;

    size_t keep = n;
    if (keep && text[keep - 1] != '\n') {
        while (keep > 0 && text[keep - 1] != '\n') --keep;
    }
    int ok = write_file(dst, text, keep);
    free(text);
    return ok;
}

/* Keep the first `cursor + 1` samples of a replay and add a heartbeat just
 * after it. That turns "the replay is at the current screen" into a checkpoint
 * that ends at the current screen instead of continuing to its old end. */
static int copy_replay_truncated(const char* src, const char* dst, int cursor)
{
    size_t n = 0;
    char* text = read_file(src, &n);
    if (!text) return 0;

    FILE* out = fopen(dst, "wb");
    if (!out) { free(text); return 0; }
    fputs("# ps3recomp pad recording v1\n", out);
    fputs("# t_ms flips mask lx ly rx ry\n", out);

    int index = -1;
    unsigned last_t = 0, last_f = 0;
    char* line = text;
    while (line && *line) {
        char* next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (line[0] != '#' && line[0] != 0) {
            unsigned t, f, m, lx, ly, rx, ry;
            if (sscanf(line, "%u %u %u %u %u %u %u",
                       &t, &f, &m, &lx, &ly, &rx, &ry) == 7) {
                ++index;
                if (cursor >= 0 && index > cursor) break;
                fprintf(out, "%u %u %u %u %u %u %u\n", t, f, m, lx, ly, rx, ry);
                last_t = t; last_f = f;
            }
        }
        line = next;
    }
    if (cursor >= 0)
        fprintf(out, "%u %u 0 128 128 128 128\n", last_t + 250u, last_f + 30u);
    fclose(out);
    free(text);
    return 1;
}

static int copy_tree(const char* src, const char* dst)
{
    if (!path_exists(src)) return 0;
    make_dirs(dst);

    char pattern[SCENE_PATH_MAX];
    snprintf(pattern, sizeof pattern, "%s\\*", src);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 1;

    int ok = 1;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char s[SCENE_PATH_MAX], d[SCENE_PATH_MAX];
        snprintf(s, sizeof s, "%s\\%s", src, fd.cFileName);
        snprintf(d, sizeof d, "%s\\%s", dst, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!copy_tree(s, d)) ok = 0;
        } else if (!CopyFileA(s, d, FALSE)) {
            ok = 0;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return ok;
}

static void savedata_root(char* out, size_t n)
{
    const char* root = getenv("PS3_GAMEDATA_ROOT");
    if (root && *root) {
        snprintf(out, n, "%s\\dev_hdd0\\home\\00000001\\savedata", root);
        return;
    }
    char cwd[SCENE_PATH_MAX];
    if (!GetCurrentDirectoryA(sizeof cwd, cwd)) cwd[0] = 0;
    snprintf(out, n, "%s\\gamedata\\dev_hdd0\\home\\00000001\\savedata", cwd);
}

static void last_sample(const char* path, unsigned* t_ms, unsigned* flips)
{
    *t_ms = 0; *flips = 0;
    size_t n = 0;
    char* text = read_file(path, &n);
    if (!text) return;
    char* line = text;
    while (line && *line) {
        char* next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (line[0] != '#' && line[0] != 0) {
            unsigned t, f, m, lx, ly, rx, ry;
            if (sscanf(line, "%u %u %u %u %u %u %u",
                       &t, &f, &m, &lx, &ly, &rx, &ry) == 7) {
                *t_ms = t; *flips = f;
            }
        }
        line = next;
    }
    free(text);
}

static int write_ini(const char* dir, const char* name, const char* note,
                     const char* padrec, const char* replay)
{
    char path[SCENE_PATH_MAX];
    snprintf(path, sizeof path, "%s\\scene.ini", dir);

    unsigned t_ms = 0, flips = 0;
    last_sample(padrec, &t_ms, &flips);

    char exe[SCENE_PATH_MAX];
    if (!GetModuleFileNameA(NULL, exe, sizeof exe)) exe[0] = 0;

    char created[32];
    stamp_local(created, sizeof created);

    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "version=1\n");
    fprintf(f, "name=%s\n", name ? name : "scene");
    fprintf(f, "created=%s\n", created);
    fprintf(f, "source=%s\n", replay ? "replay" : "record");
    fprintf(f, "recording=%s\n", padrec);
    fprintf(f, "target_ms=%u\n", t_ms);
    fprintf(f, "target_flip=%u\n", flips);
    fprintf(f, "savedata=savedata\n");
    fprintf(f, "exe=%s\n", exe);
    fprintf(f, "note=%s\n", note ? note : "");
    fclose(f);
    return 1;
}

static int save_checkpoint(char* out_dir, size_t out_size, char* err, size_t err_size)
{
    if (out_dir && out_size) out_dir[0] = 0;
    if (err && err_size) err[0] = 0;

    const char* base = getenv("PS3_SCENE_NAME");
    if (!base || !*base) base = "quick";
    const char* note = getenv("PS3_SCENE_NOTE");

    char stamp[32];
    stamp_local(stamp, sizeof stamp);
    snprintf(out_dir, out_size, "%s\\%s-quick-%s", s_scene_dir, base, stamp);
    make_dirs(out_dir);

    char padrec[SCENE_PATH_MAX];
    snprintf(padrec, sizeof padrec, "%s\\scene.padrec", out_dir);

    const char* record = getenv("PAD_RECORD");
    const char* replay = getenv("PAD_REPLAY");
    if (record && *record && path_exists(record)) {
        if (!copy_padrec_clean(record, padrec)) {
            snprintf(err, err_size, "no se pudo copiar la grabacion %s", record);
            return 0;
        }
    } else if (replay && *replay && path_exists(replay)) {
        int cursor = cellPad_replay_cursor();
        if (!copy_replay_truncated(replay, padrec, cursor)) {
            snprintf(err, err_size, "no se pudo copiar el replay %s", replay);
            return 0;
        }
    } else {
        snprintf(err, err_size, "no hay PAD_RECORD ni PAD_REPLAY activo");
        return 0;
    }

    char save_src[SCENE_PATH_MAX];
    savedata_root(save_src, sizeof save_src);
    char save_dst[SCENE_PATH_MAX];
    snprintf(save_dst, sizeof save_dst, "%s\\savedata", out_dir);
    if (path_exists(save_src)) {
        if (!copy_tree(save_src, save_dst)) {
            snprintf(err, err_size, "no se pudo copiar el savedata %s", save_src);
            return 0;
        }
    } else {
        make_dirs(save_dst);
    }

    if (!write_ini(out_dir, base, note, padrec, replay)) {
        snprintf(err, err_size, "no se pudo escribir scene.ini");
        return 0;
    }
    return 1;
}

static DWORD WINAPI save_thread(LPVOID)
{
    char dir[SCENE_PATH_MAX], err[512];
    if (save_checkpoint(dir, sizeof dir, err, sizeof err))
        scene_log("[scene] checkpoint guardado en %s\n", dir);
    else
        scene_log("[scene] no se pudo guardar el checkpoint: %s\n", err);
    InterlockedExchange(&s_save_busy, 0);
    return 0;
}

/* Newest scene directory under PS3_SCENE_DIR. */
static int newest_scene(char* out, size_t n)
{
    char pattern[SCENE_PATH_MAX];
    snprintf(pattern, sizeof pattern, "%s\\*", s_scene_dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    ULARGE_INTEGER best = {0, 0};
    int found = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;

        char ini[SCENE_PATH_MAX];
        snprintf(ini, sizeof ini, "%s\\%s\\scene.ini", s_scene_dir, fd.cFileName);
        WIN32_FIND_DATAA fi;
        HANDLE hi = FindFirstFileA(ini, &fi);
        if (hi == INVALID_HANDLE_VALUE) continue;
        FindClose(hi);

        ULARGE_INTEGER t;
        t.LowPart = fi.ftLastWriteTime.dwLowDateTime;
        t.HighPart = fi.ftLastWriteTime.dwHighDateTime;
        if (!found || t.QuadPart > best.QuadPart) {
            best = t;
            snprintf(out, n, "%s\\%s", s_scene_dir, fd.cFileName);
            found = 1;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return found;
}

static int reload_latest(void)
{
    char dir[SCENE_PATH_MAX];
    if (!newest_scene(dir, sizeof dir)) {
        scene_log("[scene] no hay checkpoints en %s\n", s_scene_dir);
        return 0;
    }

    char reload[SCENE_PATH_MAX];
    snprintf(reload, sizeof reload, "%s\\.reload", s_scene_dir);
    if (!write_file(reload, dir, strlen(dir))) {
        scene_log("[scene] no se pudo escribir %s\n", reload);
        return 0;
    }

    scene_log("[scene] recargando %s; cerrando para que el lanzador lo reproduzca\n", dir);
    bakugan_input_shutdown();
    cellAudioQuit();
    rsx_d3d12_backend_flush_shader_cache();
    fflush(NULL);
    ExitProcess(9);
    return 1;
}

static int foreground_is_ours(void)
{
    HWND fg = GetForegroundWindow();
    if (!fg) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

void scene_state_tick(void)
{
    if (!s_initialized) {
        s_initialized = 1;
        const char* dir = getenv("PS3_SCENE_DIR");
        if (dir && *dir) {
            strncpy(s_scene_dir, dir, sizeof s_scene_dir - 1);
            s_scene_dir[sizeof s_scene_dir - 1] = 0;
            CreateDirectoryA(s_scene_dir, NULL);
            scene_log("[scene] F5 guarda checkpoint; F9 recarga el ultimo (%s)\n", s_scene_dir);
        }
    }
    if (!s_scene_dir[0]) return;
    if (!foreground_is_ours()) {
        s_f5_down = s_f9_down = 0;
        return;
    }

    int f5 = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
    int f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;

    if (f5 && !s_f5_down && InterlockedCompareExchange(&s_save_busy, 1, 0) == 0) {
        scene_log("[scene] guardando checkpoint...\n");
        HANDLE th = CreateThread(NULL, 0, save_thread, NULL, 0, NULL);
        if (!th) InterlockedExchange(&s_save_busy, 0);
    }
    if (f9 && !s_f9_down) {
        if (s_save_busy) scene_log("[scene] espera a que termine el checkpoint antes de recargar\n");
        else reload_latest();
    }

    s_f5_down = f5;
    s_f9_down = f9;
}

#endif  /* _WIN32 */
