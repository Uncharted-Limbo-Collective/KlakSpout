// Standalone check of one question:
//   does spoutSenderNames::CheckSender() assign its out-params when it fails?
//
// No Unity, no GPU, no D3D device needed. Build and run from Plugin/:
//   cl /nologo /O2 /MT /EHsc /std:c++17 /DMINI_SPOUTUTILS checksender_test.cpp ^
//      Spout\SpoutSenderNames.cpp Spout\SpoutSharedMemory.cpp Spout\SpoutUtils.cpp ^
//      /Fe:checksender_test.exe /link ole32.lib

#include <cstdio>
#include "Spout/SpoutSenderNames.h"

int main()
{
    spoutSenderNames spout;

    // Sentinels, so we can see exactly which of these CheckSender writes.
    // Receiver::update() leaves these genuinely uninitialized; using sentinels
    // keeps this test deterministic instead of relying on undefined behaviour.
    unsigned int width  = 0xAAAAAAAA;
    unsigned int height = 0xBBBBBBBB;
    HANDLE       handle = (HANDLE)0xDEADBEEFDEADBEEF;
    DWORD        format = 0xCCCCCCCC;

    const char* name = "ThisSenderDoesNotExist_KlakSpoutCheck";

    bool res = spout.CheckSender(name, width, height, handle, format);

    std::printf("CheckSender(\"%s\") -> %s\n", name, res ? "true" : "false");
    std::printf("  width  = 0x%08X %s\n", width,
                width  == 0xAAAAAAAA ? "(UNTOUCHED)" : "(assigned)");
    std::printf("  height = 0x%08X %s\n", height,
                height == 0xBBBBBBBB ? "(UNTOUCHED)" : "(assigned)");
    std::printf("  handle = 0x%016llX %s\n", (unsigned long long)handle,
                handle == (HANDLE)0xDEADBEEFDEADBEEF ? "(UNTOUCHED)" : "(assigned)");
    std::printf("  format = 0x%08X %s\n", format,
                format == 0xCCCCCCCC ? "(UNTOUCHED)" : "(assigned)");

    std::printf("\nReceiver::update() passes this handle to OpenSharedHandle/\n"
                "OpenSharedResource whenever res == false.\n");
    return 0;
}
