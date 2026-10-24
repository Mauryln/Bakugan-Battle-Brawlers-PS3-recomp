#include <windows.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
static int fail_high_resolution, requested_high, closed, waited;
static long long elapsed, due_value;
static LARGE_INTEGER s_qpc_freq = {.QuadPart = 1000000};
static void ensure_qpc_init(void) {}
static HANDLE timer_ex(void* attr, const wchar_t* name, DWORD flags, DWORD access)
{
    (void)attr; (void)name; (void)access;
    requested_high = (flags & 2) != 0;
    return fail_high_resolution ? NULL : (HANDLE)2;
}
static HANDLE timer_legacy(void* attr, BOOL manual, const wchar_t* name)
{
    (void)attr; (void)manual; (void)name; return (HANDLE)1;
}
static BOOL timer_set(HANDLE handle, LARGE_INTEGER* due, LONG period, void* fn, void* data, BOOL resume)
{
    (void)handle; (void)period; (void)fn; (void)data; (void)resume;
    due_value = due->QuadPart; return TRUE;
}
static DWORD timer_wait(HANDLE handle, DWORD timeout)
{
    (void)timeout; waited++;
    elapsed += handle == (HANDLE)2 ? -due_value / 10 : 16000;
    return WAIT_OBJECT_0;
}
static BOOL timer_close(HANDLE handle) { (void)handle; closed++; return TRUE; }
#define CreateWaitableTimerExW timer_ex
#define CreateWaitableTimerW timer_legacy
#define SetWaitableTimer timer_set
#define WaitForSingleObject timer_wait
#define CloseHandle timer_close
static void guest_sleep(uint64_t usec)
{
    #include "ppu_sleep_entry.inc"
}
int main(void)
{
    guest_sleep(1000);
    assert(requested_high && elapsed == 1000 && closed == 1 && waited == 1);
    fail_high_resolution = 1;
    guest_sleep(1000);
    assert(closed == 2 && waited == 2); /* Older Windows retains the fallback. */
    puts("PASS: 1ms PPU sleeps request a high-resolution timer and close both success/fallback handles");
}
