#include <stdio.h>
#include <string.h>
#include "savedata_dir_matches.inc"

int main(void)
{
    const char* backup = "BLES00758-PLAYER-DATA-00.bak-20261108T150150191Z";
    if (savedata_dir_matches(backup, "BLES00758") || savedata_dir_matches(backup, NULL)) {
        fputs("FAIL: unrepresentable slot name would be truncated before loading\n", stderr);
        return 1;
    }
    if (!savedata_dir_matches("BLES00758-PLAYER-DATA-00", "OTHER|BLES00758") ||
        !savedata_dir_matches("1234567890123456789012345678901", NULL) ||
        savedata_dir_matches("BLES00758-PLAYER-DATA-00", "OTHER")) {
        fputs("FAIL: valid slot filtering changed\n", stderr);
        return 1;
    }
    puts("PASS: save-slot names round trip without truncation");
    return 0;
}
