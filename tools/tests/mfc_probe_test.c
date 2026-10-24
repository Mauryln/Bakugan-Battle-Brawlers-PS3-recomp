#include "spu_dma.h"
#include <assert.h>
#include <stdio.h>
static unsigned environment_reads;
static char* probe_environment(const char* name) { (void)name; environment_reads++; return NULL; }
static DWORD WINAPI spu_pcsample_thread(LPVOID arg) { (void)arg; abort(); }
#define getenv probe_environment
#include "mfc_probe_entry.inc"
int main(void)
{
    static spu_context first, second;
    mfc_engine* a = mfc_for(&first);
    mfc_engine* b = mfc_for(&second);
    assert(a && b && a != b);
    for (unsigned i = 0; i < 10000; i++) {
        assert(mfc_for(&first) == a);
        assert(mfc_for(&second) == b);
    }
    assert(environment_reads == 1);
    puts("PASS: disabled MFC diagnostic reads configuration once, preserving private engines");
}
