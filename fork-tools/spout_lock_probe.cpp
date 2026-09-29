// Diagnostic: watch live Spout senders from outside any Spout app.
//
// For each sender name given, polls every ~2 ms: does its info map exist, how
// long does taking its info mutex take, and is the name still in the shared
// SpoutSenderNames list. Prints list additions/removals as they happen and a
// wait-time histogram at the end. Useful to tell a busy sender (long waits)
// from a recreated one (map missing) from an evicted one (in no list, map fine).
//
//   cl /nologo /O2 /EHsc /std:c++17 spout_lock_probe.cpp /Fe:spout_lock_probe.exe
//   spout_lock_probe.exe 60 TDSpoutSDF TDSpoutStamp
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <chrono>
using clk = std::chrono::steady_clock;

static std::set<std::string> readList() {
    std::set<std::string> s;
    HANDLE h = OpenFileMappingA(FILE_MAP_READ, FALSE, "SpoutSenderNames");
    if (!h) return s;
    auto* p = (const char*)MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
    if (p) { for (int i = 0; i < 64 && p[i*256]; i++) s.insert(std::string(p + i*256, strnlen(p + i*256, 256))); UnmapViewOfFile(p); }
    CloseHandle(h);
    return s;
}

struct Stat { long n=0, openFail=0, mutexFail=0, over67=0, abandoned=0; double maxMs=0; std::map<int,long> hist; unsigned w=0,h=0; };

int main(int argc, char** argv) {
    int secs = argc > 1 ? atoi(argv[1]) : 60;
    std::vector<std::string> names(argv + (argc > 2 ? 2 : argc), argv + argc);
    if (names.empty()) { for (auto& n : readList()) names.push_back(n); }
    std::map<std::string, Stat> st;
    auto list = readList();
    auto t0 = clk::now();
    while (clk::now() - t0 < std::chrono::seconds(secs)) {
        for (auto& n : names) {
            auto& s = st[n]; s.n++;
            HANDLE map = OpenFileMappingA(FILE_MAP_READ, FALSE, n.c_str());
            if (!map) { s.openFail++; continue; }
            HANDLE mx = OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (n + "_mutex").c_str());
            if (!mx) { s.mutexFail++; CloseHandle(map); continue; }
            auto a = clk::now();
            DWORD r = WaitForSingleObject(mx, 2000);
            double ms = std::chrono::duration<double, std::milli>(clk::now() - a).count();
            if (r == WAIT_ABANDONED) s.abandoned++;
            if (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED) {
                auto* p = (const unsigned*)MapViewOfFile(map, FILE_MAP_READ, 0, 0, 16);
                if (p) { s.w = p[1]; s.h = p[2]; UnmapViewOfFile(p); }   // shareHandle, width, height
                ReleaseMutex(mx);
            }
            if (ms > s.maxMs) s.maxMs = ms;
            if (ms > 67) s.over67++;
            int b = ms < 1 ? 0 : ms < 5 ? 1 : ms < 17 ? 5 : ms < 67 ? 17 : 67; s.hist[b]++;
            CloseHandle(mx); CloseHandle(map);
        }
        auto now = readList();
        if (now != list) {
            double t = std::chrono::duration<double>(clk::now() - t0).count();
            for (auto& x : list) if (!now.count(x)) printf("[%6.2fs] LIST: removed %s\n", t, x.c_str());
            for (auto& x : now) if (!list.count(x)) printf("[%6.2fs] LIST: added   %s\n", t, x.c_str());
            list = now;
        }
        Sleep(2);
    }
    printf("\n%-18s %7s %8s %9s %7s %9s  wait histogram (<1 / 1-5 / 5-17 / 17-67 / >67 ms)   size\n","sender","polls","mapFail","mutexFail","max ms","abandoned");
    for (auto& [n, s] : st)
        printf("%-18s %7ld %8ld %9ld %7.1f %9ld  %ld / %ld / %ld / %ld / %ld   %ux%u\n", n.c_str(), s.n, s.openFail, s.mutexFail, s.maxMs, s.abandoned,
               s.hist[0], s.hist[1], s.hist[5], s.hist[17], s.hist[67], s.w, s.h);
    printf("\nin list at end:"); for (auto& x : list) printf(" %s", x.c_str()); printf("\n");
}
