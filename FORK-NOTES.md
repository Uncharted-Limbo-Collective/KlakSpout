# ULC fork of KlakSpout

Fork of [keijiro/KlakSpout](https://github.com/keijiro/KlakSpout) at **2.0.6**
(`849e7bc`). It exists to carry fixes for two native crashes found while
running Spout between TouchDesigner and Unity 6000.4.1f1 on Direct3D12.

`main` tracks upstream unmodified apart from this file. Each fix lives on its
own branch so it can be offered upstream independently.

| Branch | Fixes | Status |
| --- | --- | --- |
| `fix-sender-null-resource` | Null `ID3D12Resource` passed to `CreateWrappedResource` | Root cause proven by instrumented build. Verified sufficient **on its own**, with the receiver bug still present |
| `fix-receiver-uninitialized-handle` | Indeterminate share handle passed to `OpenSharedHandle` | Root cause proven. Real but non-fatal here: logged 400-500 times in a session without crashing |

## `fix-sender-null-resource`

**Symptom.** Editor dies on the render thread with no logged error. Stack:

```
GfxTaskExecutorD3D12::RunTask
  OnRenderEvent                Plugin.cpp:32
    Sender::update             Sender.h:37
      Sender::updateTexture    Sender.h:103   <- CreateWrappedResource
        d3d11 -> d3d11on12            <- fault
```

**Cause.** `Sender::update()` discards the `HRESULT` from `ComPtr::As()` and
passes the result to `updateTexture()` unchecked. A null texture pointer in
the interop block therefore reaches
`ID3D11On12Device::CreateWrappedResource` as a null `ID3D12Resource`.

**Evidence.** A build instrumented to log `source` per call, staged so each
probe announces itself before running:

```
[1] enter source=000001CC39225180 changed=1 ... refcount=3 ... GetDesc ok w=1024 h=1024 ... hr=0x00000000
[2] enter source=000001CC39225180 changed=0 ... refcount=4 ... hr=0x00000000
[3] enter source=000001CC39222430 changed=1 ... GetDesc ok w=1080 h=1920 ... hr=0x00000000
[4] enter source=000001CC39222430 changed=0 ... hr=0x00000000
[5] enter source=0000000000000000 changed=1
[5] calling CreateWrappedResource...      <- last line written
```

Four healthy calls, then one with a null source. The probes between `enter`
and `CreateWrappedResource` are absent on call 5 because they sit behind a
null check, which is what identifies the pointer as null rather than stale.

Verified in isolation. Built from this branch alone, with `Receiver.h`
byte-identical to upstream, the editor started cleanly over repeated launches
in the configuration that previously crashed every time. The receiver bug was
still live during those runs and logged itself 400-500 times per session, which
confirms the build really was receiver-unpatched and that the sender fix alone
accounts for the crash going away.

## `fix-receiver-uninitialized-handle`

**Cause.** `spoutSenderNames::CheckSender()` assigns the share handle only
when it finds a live sender; on failure it zeroes width and height and leaves
the handle untouched. `Receiver::update()` returned early only on success, so
a failed lookup opened an indeterminate stack value as a shared handle.

**Evidence.** `fork-tools/checksender_test.cpp` (on `main`) verifies the
out-parameter contract with no GPU, Unity or D3D device required:

```
CheckSender("ThisSenderDoesNotExist_KlakSpoutCheck") -> false
  width  = 0x00000000 (assigned)
  height = 0x00000000 (assigned)
  handle = 0xDEADBEEFDEADBEEF (UNTOUCHED)
  format = 0xCCCCCCCC (UNTOUCHED)
```

This is a real latent bug but was **not** the crash being chased. Reported
separately from the sender fix for that reason.

## Known remaining issue (observed once, not reproducible)

With the sender fix applied, the startup crash is gone. A **separate** fault
was seen once afterwards, late in a session during asset GC rather than during
project load:

```
OnRenderEvent      +0x56
  Sender::update   +0x3D      <- faults before reaching any D3D call
```

No D3D frames below it, so it is not the null wrap fixed above.

Status: **not reproducible so far.** A symbolized build (`/Zi /Od`) was
installed afterwards and the editor ran a normal working session without
recurrence, so there is no file and line for it and no root cause. It is
recorded here only so the observation is not lost. Treat it as a single
sighting rather than a known defect, and do not read it as a reason to
distrust the sender fix.

If it recurs, capture the crash folder under the Unity editor crash directory
(`Temp/Unity/Editor/Crashes` inside LOCALAPPDATA). With a matched symbolized
DLL and PDB pair installed, the stack resolves to `Sender.h:NN` instead of a
raw offset.

## Building the plugin

Upstream builds with MinGW-w64 (`Plugin/Makefile`). These binaries were built
with MSVC instead:

```
cl /nologo /LD /O2 /MT /EHsc /std:c++20 /DMINI_SPOUTUTILS /DNDEBUG ^
   Plugin.cpp Spout\SpoutSenderNames.cpp Spout\SpoutSharedMemory.cpp Spout\SpoutUtils.cpp ^
   /Fe:KlakSpout.dll ^
   /link dxgi.lib d3d12.lib d3d11.lib ole32.lib
```

`/std:c++20` is required (`Receiver::getInteropData` uses designated
initializers). Produces the same 7 undecorated exports as upstream; `/MT`
avoids a VC runtime dependency. Add `/Zi /Od` and `/link /DEBUG` for a build
whose crashes resolve to file and line.

Do not commit a rebuilt `Packages/jp.keijiro.klak.spout/Plugin/KlakSpout.dll`
to a pull request — that binary is tracked, `make copy` overwrites it, and
upstream builds it with a different toolchain.

## Upstream

Sender crash reported at <https://github.com/keijiro/KlakSpout/issues/114>.
