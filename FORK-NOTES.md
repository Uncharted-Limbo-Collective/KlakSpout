# ULC fork of KlakSpout

Fork of [keijiro/KlakSpout](https://github.com/keijiro/KlakSpout) at **2.0.6**
(`849e7bc`). It exists to carry fixes for two native crashes found while
running Spout between TouchDesigner and Unity 6000.4.1f1 on Direct3D12, and
a receiver bug that disconnected live TouchDesigner senders mid-show.

`main` tracks upstream unmodified apart from this file. Each fix lives on its
own branch so it can be offered upstream independently.

| Branch | Fixes | Status |
| --- | --- | --- |
| `fix-sender-null-resource` | Null `ID3D12Resource` passed to `CreateWrappedResource` | Root cause proven by instrumented build. Verified sufficient **on its own**, with the receiver bug still present |
| `fix-receiver-uninitialized-handle` | Indeterminate share handle passed to `OpenSharedHandle` | Root cause proven. Real but non-fatal here: logged 400-500 times in a session without crashing |
| `fix-receiver-list-eviction` (on top of the branch above) | Receivers delete live senders from the Spout name list, then cannot reconnect | Reproduced with `fork-tools/evict_test.cpp`; observed live with TouchDesigner |

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

## `fix-receiver-list-eviction`

**Symptom.** A TouchDesigner Spout Out TOP stays active and keeps sending, but
its name disappears from the Spout sender list. Unity stops receiving it and
never recovers. Recreating the TOP in TD (cut/paste, or toggling Active)
re-registers the name and brings Unity back.

**Cause.** `Receiver::update()` runs every frame and called
`spoutSenderNames::CheckSender()`, which

1. only finds senders whose name is in the shared `SpoutSenderNames` list, and
2. calls `ReleaseSenderName()` when a listed sender's info block cannot be
   read - removing the name **for every Spout app on the machine**.

The read can fail while the sender is alive:

- **Lock timeout.** `SpoutSharedMemory::Lock()` gives up after 67 ms. Anyone
  holding the sender's info mutex that long - the sender, another receiver, a
  stalled thread - makes the lookup fail.
- **Re-registration.** A sender that is released and registered again (node
  reset, Active toggled, restarted) has no info block for a moment. A lookup
  that saw the name in the list just before can delete the fresh entry.

A resize does not open this window: `UpdateSender()` rewrites the info block in
place. Which trigger caused the eviction we saw is not established - see the
probe results below.

Either way (1) then locks the receiver out: the sender is alive, but no longer
listed, so `CheckSender()` never finds it again.

**Evidence.**

`fork-tools/evict_test.cpp` (no GPU needed) holds a live sender's info mutex
for 200 ms and looks it up both ways:

```
OLD receiver path: CheckSender
  before: in list = 1
  lookup during busy sender -> 0
  after:  in list = 0   (sender still alive)
  next lookup -> 1  512x512

NEW receiver path: FindSender
  before: in list = 1
  lookup during busy sender -> 0
  after:  in list = 1   (sender still alive)
  next lookup -> 1  512x512
```

`fork-tools/spout_lock_probe.cpp` against a live TD session, 45 s at ~2 ms
per poll, after an eviction had happened: `TDSpoutSDF`'s info block and mutex
were perfectly healthy (max wait 0.4 ms, no failed opens) while its name was
absent from the list. Worst wait across all four TD senders was 13.4 ms. So
the sender was alive and reachable; only the list entry was gone - exactly the
state in which the old receiver cannot reconnect and the new one can.

**Fix.** Use `FindSender()`, which reads the info block directly by name and
never writes the list - the lookup upstream Spout itself recommends for
receivers (09.03.18 note in `SpoutSenderNames.cpp`). Also:

- keep the current texture through up to 60 consecutive failed lookups, so a
  briefly unreadable sender is not treated as gone;
- reopen the shared texture when the share handle changes, not only the size.
  A sender that recreated its texture at the same size previously left the
  receiver holding the old resource - a frozen frame.

**Consequences.**

- Receivers no longer prune dead names from the list. Nothing is lost: a
  crashed sender's info block dies with its process, `RegisterSenderName()`
  already prunes dead entries on a name clash, and SpoutPanel prunes too.
- Receivers connect to a sender whether or not it is listed. The list is only
  used for discovery (`SpoutManager.GetSourceNames()`, the inspector
  dropdown).
- When a sender really goes away, the receiver keeps showing its last frame
  for up to 60 frames (about a second) before releasing it, and keeps a
  reference to the sender's texture for that long.
- One mutex acquisition per frame per receiver instead of two (the list
  mutex is no longer taken), so less contention for every Spout app.
- No ABI change: same exports, same interop struct.

Other Spout apps can still evict names the same way. A sender that must stay
listed needs a watchdog on the sending side that re-registers it.

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
