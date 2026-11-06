// External x64 sampler. Suspend only for GetThreadContext; resolve local PDBs
// after sampling, on this helper's single DbgHelp thread. No target writes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <timeapi.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

struct module_info { DWORD64 base; DWORD size; std::string path, name; };
struct sample {
    double end_ms; DWORD64 rip;
    CONTEXT context{};
    unsigned char stack[4096]{};
    SIZE_T stack_bytes = 0;
};
struct read_range { DWORD64 start, end; };
static std::vector<read_range> immutable_ranges;
static const sample* frozen_sample;
static BOOL CALLBACK frozen_read(HANDLE process, DWORD64 address, PVOID dst, DWORD size, LPDWORD got)
{
    DWORD local_count = 0;
    if (!got) got = &local_count;
    *got = 0;
    if (!size) return TRUE;
    if (frozen_sample && address >= frozen_sample->context.Rsp &&
        address - frozen_sample->context.Rsp < frozen_sample->stack_bytes) {
        SIZE_T offset = SIZE_T(address - frozen_sample->context.Rsp);
        DWORD n = DWORD(std::min<SIZE_T>(size, frozen_sample->stack_bytes - offset));
        std::memcpy(dst, frozen_sample->stack + offset, n); *got = n;
        return TRUE;
    }
    for (const auto& range : immutable_ranges) {
        if (address >= range.start && address <= range.end && size <= range.end - address) {
            SIZE_T n = 0;
            BOOL ok = ReadProcessMemory(process, reinterpret_cast<void*>(address), dst, size, &n);
            *got = DWORD(n); return ok;
        }
    }
    // Never fall back to a resumed live stack or mutable module data.
    return FALSE;
}
static BOOL CALLBACK symbol_callback(HANDLE process, ULONG action, ULONG64 data, ULONG64)
{
    if (action != CBA_READ_MEMORY) return FALSE;
    auto* request = reinterpret_cast<IMAGEHLP_CBA_READ_MEMORY*>(data);
    return frozen_read(process, request->addr, request->buf, request->bytes, request->bytesread);
}
static unsigned long long file_time(FILETIME t)
{
    return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}
static double now_ms()
{
    LARGE_INTEGER n, f;
    QueryPerformanceCounter(&n); QueryPerformanceFrequency(&f);
    return static_cast<double>(n.QuadPart) * 1000.0 / f.QuadPart;
}
static void fail(const char* operation)
{
    std::fprintf(stderr, "%s failed: Windows error %lu\n", operation, GetLastError());
    std::exit(1);
}
int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "stamp") == 0) {
        std::printf("end_ms=%.6f\n", now_ms());
        return 0;
    }
    if (argc == 4 && std::strcmp(argv[1], "event") == 0) {
        if (std::strcmp(argv[3], "set") && std::strcmp(argv[3], "reset")) return 2;
        HANDLE event = OpenEventA(EVENT_MODIFY_STATE, FALSE, argv[2]);
        if (!event) fail("OpenEventA");
        BOOL ok = std::strcmp(argv[3], "set") == 0 ? SetEvent(event) : ResetEvent(event);
        DWORD error = GetLastError();
        CloseHandle(event);
        if (!ok) { SetLastError(error); fail("Event control"); }
        std::printf("event=%s mode=%s end_ms=%.6f\n", argv[2], argv[3], now_ms());
        return 0;
    }
    if (argc != 7) {
        std::fprintf(stderr, "Usage: sampler PID TID seconds interval_ms symbol_dir output_prefix\n");
        return 2;
    }
    DWORD pid = std::strtoul(argv[1], nullptr, 10), tid = std::strtoul(argv[2], nullptr, 10);
    unsigned seconds = std::strtoul(argv[3], nullptr, 10), interval = std::strtoul(argv[4], nullptr, 10);
    if (!pid || !tid || seconds > 120 || interval < 1 || interval > 100) return 2;
    std::string prefix = argv[6];
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) fail("OpenProcess");
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                              THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
    if (!thread) fail("OpenThread");
    HANDLE snapshot = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
        if (snapshot != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH) break;
    }
    if (snapshot == INVALID_HANDLE_VALUE) fail("CreateToolhelp32Snapshot");
    MODULEENTRY32 entry{}; entry.dwSize = sizeof(entry);
    std::vector<module_info> modules;
    if (!Module32First(snapshot, &entry)) fail("Module32First");
    do { modules.push_back({reinterpret_cast<DWORD64>(entry.modBaseAddr), entry.modBaseSize,
                            entry.szExePath, entry.szModule}); } while (Module32Next(snapshot, &entry));
    CloseHandle(snapshot);
    for (const auto& m : modules) {
        IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{}; SIZE_T got = 0;
        if (!ReadProcessMemory(process, reinterpret_cast<void*>(m.base), &dos, sizeof(dos), &got) ||
            dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) continue;
        if (!ReadProcessMemory(process, reinterpret_cast<void*>(m.base + dos.e_lfanew), &nt, sizeof(nt), &got) ||
            nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt.FileHeader.NumberOfSections > 96) continue;
        immutable_ranges.push_back({m.base, m.base + std::min(nt.OptionalHeader.SizeOfHeaders, m.size)});
        DWORD64 section_start = m.base + dos.e_lfanew + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
        for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
            IMAGE_SECTION_HEADER section{};
            if (!ReadProcessMemory(process, reinterpret_cast<void*>(section_start + i * sizeof(section)), &section, sizeof(section), &got)) break;
            if (!(section.Characteristics & IMAGE_SCN_MEM_WRITE) && section.VirtualAddress < m.size) {
                DWORD length = std::min(section.Misc.VirtualSize, m.size - section.VirtualAddress);
                immutable_ranges.push_back({m.base + section.VirtualAddress, m.base + section.VirtualAddress + length});
            }
        }
    }
    std::vector<sample> samples;
    samples.reserve(seconds * 1000 / interval + 1);
    FILETIME created{}, exited{}, kernel0{}, user0{}, kernel1{}, user1{};
    if (!GetThreadTimes(thread, &created, &exited, &kernel0, &user0)) fail("GetThreadTimes start");
    unsigned errors = 0, already_suspended = 0;
    double suspended_ms = 0, max_suspended_ms = 0, start = now_ms();
    if (timeBeginPeriod(1) != 0) { std::fprintf(stderr, "timeBeginPeriod failed\n"); return 1; }
    while (now_ms() - start < seconds * 1000.0) {
        CONTEXT context{};
        context.ContextFlags = CONTEXT_FULL;
        unsigned char captured_stack[4096]{};
        SIZE_T stack_bytes = 0;
        double before = now_ms();
        DWORD previous = SuspendThread(thread);
        if (previous == DWORD(-1)) { ++errors; break; }
        BOOL obtained = GetThreadContext(thread, &context);
        if (obtained) {
            for (const auto& m : modules) {
                if (context.Rip >= m.base && context.Rip - m.base < m.size &&
                    (m.name == "VCRUNTIME140.dll" || m.name == "ucrtbase.dll" || m.name == "ntdll.dll")) {
                    const SIZE_T requested = m.name == "ntdll.dll" ? sizeof(captured_stack) : 512;
                    ReadProcessMemory(process, reinterpret_cast<void*>(context.Rsp), captured_stack, requested, &stack_bytes);
                    break;
                }
            }
        }
        // Always balance our one successful suspension, including failed reads.
        DWORD resumed = ResumeThread(thread);
        if (resumed == DWORD(-1)) {
            std::fprintf(stderr, "CRITICAL: ResumeThread failed, pid=%lu tid=%lu error=%lu\n", pid, tid, GetLastError());
            return 3;
        }
        double after = now_ms(), duration = after - before;
        suspended_ms += duration; max_suspended_ms = std::max(max_suspended_ms, duration);
        if (previous) ++already_suspended;
        else if (obtained) {
            sample record{}; record.end_ms = after; record.rip = context.Rip;
            record.context = context; record.stack_bytes = stack_bytes;
            std::memcpy(record.stack, captured_stack, stack_bytes);
            samples.push_back(record);
        }
        else ++errors;
        Sleep(interval + (samples.size() & 1u));
    }
    timeEndPeriod(1);
    double wall_ms = now_ms() - start;
    if (!GetThreadTimes(thread, &created, &exited, &kernel1, &user1)) fail("GetThreadTimes end");
    double cpu_ms = (file_time(kernel1) + file_time(user1) - file_time(kernel0) - file_time(user0)) / 10000.0;
    FILE* raw = std::fopen((prefix + ".samples.tsv").c_str(), "wb");
    if (!raw) { std::perror("samples output"); return 1; }
    std::fprintf(raw, "end_ms\trip\n");
    std::map<DWORD64, unsigned> pcs;
    for (const auto& s : samples) { std::fprintf(raw, "%.6f\t0x%llX\n", s.end_ms, s.rip); ++pcs[s.rip]; }
    std::fclose(raw);
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS |
                  SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS | SYMOPT_EXACT_SYMBOLS);
    if (!SymInitialize(process, argv[5], FALSE)) fail("SymInitialize");
    if (!SymRegisterCallback64(process, symbol_callback, 0)) fail("SymRegisterCallback64");
    FILE* meta = std::fopen((prefix + ".metadata.txt").c_str(), "wb");
    if (!meta) { std::perror("metadata output"); return 1; }
    std::fprintf(meta, "pid=%lu tid=%lu samples=%zu unique_pcs=%zu wall_ms=%.3f cpu_ms=%.3f cpu_core_pct=%.2f errors=%u already_suspended=%u suspended_total_ms=%.3f max_suspend_ms=%.3f start_ms=%.6f end_ms=%.6f\n",
                 pid, tid, samples.size(), pcs.size(), wall_ms, cpu_ms, 100 * cpu_ms / wall_ms,
                 errors, already_suspended, suspended_ms, max_suspended_ms, start, start + wall_ms);
    for (const auto& m : modules) {
        SetLastError(ERROR_SUCCESS);
        DWORD64 loaded = SymLoadModuleEx(process, nullptr, m.path.c_str(), nullptr, m.base, m.size, nullptr, 0);
        DWORD error = GetLastError();
        IMAGEHLP_MODULE64 info{}; info.SizeOfStruct = sizeof(info);
        BOOL known = SymGetModuleInfo64(process, m.base, &info);
        std::fprintf(meta, "module=%s base=0x%llX size=%lu loaded=%llu error=%lu symtype=%d pdb=%s\n",
                     m.name.c_str(), m.base, m.size, loaded, error,
                     known ? int(info.SymType) : -1, known ? info.LoadedPdbName : "");
    }
    // With seconds=0 this also provides an outside-the-loop counter snapshot.
    // PERF must be off: its reporting resets these counters. All listed writers
    // are frame_clock, so suspend that one writer only for the actual reads.
    struct counter { const char* name; unsigned width; DWORD64 address; unsigned long long value; bool valid; };
    counter counters[] = {{"s_perf_dup_total",4}, {"s_perf_vpc_hits",4}, {"s_perf_vpc_saved",4},
                          {"s_m_stores",4}, {"s_m_nokey",4}, {"s_m_nogen",4}, {"s_m_stale",4},
                          {"g_arrays_calls",8}};
    for (auto& c : counters) {
        alignas(SYMBOL_INFO) unsigned char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO); symbol->MaxNameLen = MAX_SYM_NAME;
        std::string name = "BakuganRecomp!" + std::string(c.name);
        if (SymFromName(process, name.c_str(), symbol) && symbol->Size == c.width &&
            !(symbol->Flags & (SYMFLAG_CONSTANT | SYMFLAG_REGISTER | SYMFLAG_FRAMEREL |
                               SYMFLAG_REGREL | SYMFLAG_TLSREL | SYMFLAG_FUNCTION))) {
            for (const auto& m : modules) if (symbol->Address >= m.base &&
                symbol->Address - m.base + c.width <= m.size) c.address = symbol->Address;
        }
    }
    DWORD counter_previous = SuspendThread(thread);
    if (counter_previous == DWORD(-1)) fail("SuspendThread counters");
    for (auto& c : counters) if (c.address) {
        SIZE_T got = 0;
        c.valid = ReadProcessMemory(process, reinterpret_cast<void*>(c.address), &c.value, c.width, &got) && got == c.width;
    }
    if (ResumeThread(thread) == DWORD(-1)) {
        std::fprintf(stderr, "CRITICAL: counter ResumeThread failed pid=%lu tid=%lu error=%lu\n", pid, tid, GetLastError());
        return 3;
    }
    double counter_time = now_ms();
    FILE* counter_file = std::fopen((prefix + ".counters.tsv").c_str(), "wb");
    if (!counter_file) { std::perror("counter output"); return 1; }
    std::fprintf(counter_file, "end_ms\tname\twidth\tvalue\tvalid\n");
    for (const auto& c : counters)
        std::fprintf(counter_file, "%.6f\t%s\t%u\t%llu\t%d\n", counter_time, c.name, c.width, c.value, c.valid);
    std::fclose(counter_file);
    std::map<std::string, unsigned> functions, lines, callers, caller_lines, caller_pcs;
    for (const auto& [rip, count] : pcs) {
        const module_info* owner = nullptr;
        for (const auto& m : modules) if (rip >= m.base && rip - m.base < m.size) { owner = &m; break; }
        std::string key = owner ? owner->name : "[unknown module]";
        alignas(SYMBOL_INFO) unsigned char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO); symbol->MaxNameLen = MAX_SYM_NAME;
        DWORD64 displacement = 0;
        // DLLs without local PDBs are module buckets, not nearest-export guesses.
        if (owner && owner->name == "BakuganRecomp.exe" && SymFromAddr(process, rip, &displacement, symbol)) {
            if (!symbol->Size || displacement < symbol->Size) key += "!" + std::string(symbol->Name);
        }
        functions[key] += count;
        IMAGEHLP_LINE64 line{}; line.SizeOfStruct = sizeof(line);
        DWORD offset = 0;
        if (owner && owner->name == "BakuganRecomp.exe" && SymGetLineFromAddr64(process, rip, &offset, &line))
            lines[std::string(line.FileName) + ":" + std::to_string(line.LineNumber)] += count;
    }
    for (const auto& m : modules) if (m.name == "BakuganRecomp.exe") {
        IMAGEHLP_MODULE64 info{}; info.SizeOfStruct = sizeof(info);
        if (SymGetModuleInfo64(process, m.base, &info))
            std::fprintf(meta, "resolved_exe_symtype=%d pdb=%s\n", int(info.SymType), info.LoadedPdbName);
    }
    unsigned stack_captured = 0, caller_found = 0;
    for (const auto& record : samples) {
        if (!record.stack_bytes) continue;
        ++stack_captured;
        frozen_sample = &record;
        CONTEXT context = record.context;
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = context.Rip; frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp; frame.AddrStack.Mode = AddrModeFlat;
        std::string callee = "unknown";
        DWORD64 callee_base = 0;
        for (const auto& m : modules) if (record.rip >= m.base && record.rip - m.base < m.size) {
            callee = m.name; callee_base = m.base;
        }
        bool found = false;
        for (unsigned depth = 0; depth < 24; ++depth) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context,
                             frozen_read, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
            if (!frame.AddrPC.Offset || frame.AddrPC.Offset == record.rip) continue;
            const module_info* owner = nullptr;
            for (const auto& m : modules) if (frame.AddrPC.Offset >= m.base && frame.AddrPC.Offset - m.base < m.size) { owner = &m; break; }
            if (!owner || owner->name != "BakuganRecomp.exe") continue;
            DWORD64 address = frame.AddrPC.Offset - 1, displacement = 0;
            alignas(SYMBOL_INFO) unsigned char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO); symbol->MaxNameLen = MAX_SYM_NAME;
            std::string name = "[unresolved exe caller]";
            if (SymFromAddr(process, address, &displacement, symbol) && symbol->Size && displacement < symbol->Size)
                name = symbol->Name;
            // Import/linker thunks can be inside the EXE without a function
            // symbol. Continue on the frozen stack to reach the real caller.
            if (name == "[unresolved exe caller]") continue;
            callers[callee + " <- " + name]++;
            char detail[200];
            std::snprintf(detail, sizeof(detail), "+0x%llX r8_raw=%llu rdx_align64=%llu rcx_align64=%llu",
                          record.rip - callee_base, record.context.R8,
                          record.context.Rdx & 63u, record.context.Rcx & 63u);
            caller_pcs[callee + detail + " <- " + name]++;
            IMAGEHLP_LINE64 line{}; line.SizeOfStruct = sizeof(line); DWORD delta = 0;
            if (SymGetLineFromAddr64(process, address, &delta, &line))
                caller_lines[callee + " <- " + std::string(line.FileName) + ":" + std::to_string(line.LineNumber)]++;
            found = true; ++caller_found; break;
        }
        if (!found) callers[callee + " <- [unwind unavailable]"]++;
    }
    frozen_sample = nullptr;
    std::fprintf(meta, "frozen_stack_samples=%u caller_recovered=%u stack_bytes_limit=4096_crt512 inline_icf_tail_calls_may_obscure_ownership=1\n", stack_captured, caller_found);
    std::fclose(meta);
    auto write_table = [&](const char* suffix, const std::map<std::string, unsigned>& table) {
        std::vector<std::pair<std::string, unsigned>> rows(table.begin(), table.end());
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        FILE* file = std::fopen((prefix + suffix).c_str(), "wb");
        if (!file) { std::perror("table output"); std::exit(1); }
        std::fprintf(file, "samples\twall_pct\test_wall_ms\tregion\n");
        for (const auto& [name, count] : rows)
            std::fprintf(file, "%u\t%.4f\t%.3f\t%s\n", count, samples.empty() ? 0 : count * 100.0 / samples.size(),
                         samples.empty() ? 0 : count * wall_ms / samples.size(), name.c_str());
        std::fclose(file);
    };
    write_table(".functions.tsv", functions); write_table(".lines.tsv", lines);
    write_table(".callers.tsv", callers); write_table(".caller-lines.tsv", caller_lines);
    write_table(".caller-pcs.tsv", caller_pcs);
    SymCleanup(process); CloseHandle(thread); CloseHandle(process);
    std::printf("samples=%zu wall_ms=%.1f cpu_core=%.1f%% suspend_total_ms=%.3f errors=%u; output=%s\n",
                samples.size(), wall_ms, 100 * cpu_ms / wall_ms, suspended_ms, errors, prefix.c_str());
    return errors ? 1 : 0;
}
