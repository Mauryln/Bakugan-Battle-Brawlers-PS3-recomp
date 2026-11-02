#include <cstdio>
#include <cstring>
static const char* value;
static unsigned calls;
static const char* option(const char* name)
{
    if (strcmp(name, "FP_CONSTBUF") && strcmp(name, "VTX_DUMP") &&
        strcmp(name, "TEXFMTDBG") && strcmp(name, "MOVIE_BIND")) return nullptr;
    ++calls;
    return value;
}
#define getenv option
#include "hot_options.inc"
int main(int argc, char** argv)
{
    if (argc != 2) return 1;
    value = strcmp(argv[1], "absent") ? argv[1] : nullptr;
    int expected = !value || value[0] != '0';
    for (int draw = 0; draw < 100000; ++draw)
        if (fp_constbuf_enabled() != expected) return 2;
    if (calls != 1) return 3;
    for (int call = 0; call < 100000; ++call) {
        const auto* opts = vp_diagnostic_options();
        if (opts->vertex_dump != (value != nullptr) ||
            opts->texture_format != (value != nullptr) ||
            opts->movie_bind != (value != nullptr)) return 4;
    }
    if (calls != 4) return 5;
    puts("PASS: FP option semantics preserved with one environment lookup");
}
