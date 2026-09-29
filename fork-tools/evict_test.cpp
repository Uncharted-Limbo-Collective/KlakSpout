// Standalone check of one question:
//   does a receiver lookup evict a LIVE sender from the Spout name list when
//   the sender's info block is briefly unreadable?
//
// Simulates a busy sender by holding its info mutex for 200 ms from another
// thread, then looks the sender up the old way (CheckSender) and the new way
// (FindSender). No Unity, no GPU, no D3D device needed. Build and run from Plugin/:
//   cl /nologo /O2 /MT /EHsc /std:c++17 /DMINI_SPOUTUTILS evict_test.cpp ^
//      Spout\SpoutSenderNames.cpp Spout\SpoutSharedMemory.cpp Spout\SpoutUtils.cpp ^
//      /Fe:evict_test.exe /link ole32.lib
//
// Registers a sender named "EvictTest_LiveSender" in the real, machine-wide
// list for the duration of the run.
#include <cstdio>
#include <thread>
#include <chrono>
#include "Spout/SpoutSenderNames.h"
#include "Spout/SpoutSharedMemory.h"

static void holdInfoLock(const char* name, int ms)
{
    SpoutSharedMemory mem;
    mem.Open(name);
    mem.Lock();
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    mem.Unlock();
}

static void run(const char* label, bool useCheckSender)
{
    spoutSenderNames sender, receiver;
    char name[SpoutMaxSenderNameLen] = "EvictTest_LiveSender";
    sender.CreateSender(name, 512, 512, (HANDLE)0x1234, 87);
    std::printf("%s\n  before: in list = %d\n", label, receiver.FindSenderName(name));

    std::thread busy(holdInfoLock, name, 200);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    unsigned int w = 0, h = 0; HANDLE hd = nullptr; DWORD f = 0;
    bool ok = useCheckSender ? receiver.CheckSender(name, w, h, hd, f)
                             : receiver.FindSender(name, w, h, hd, f);
    busy.join();

    std::printf("  lookup during busy sender -> %d\n", ok);
    std::printf("  after:  in list = %d   (sender still alive)\n", receiver.FindSenderName(name));
    ok = receiver.FindSender(name, w, h, hd, f);
    std::printf("  next lookup -> %d  %ux%u\n\n", ok, w, h);
    sender.ReleaseSenderName(name);
}

int main()
{
    run("OLD receiver path: CheckSender", true);
    run("NEW receiver path: FindSender", false);
}
