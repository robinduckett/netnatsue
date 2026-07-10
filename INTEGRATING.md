# Integrating NetNatsue into an engine source tree

NetNatsue stands in for the proprietary `Babel/` libraries
(`BabelCloak`, `BabelClient`, `BabelCommon`) that the engine's
networking module linked against.  The engine-side module wrapper is
in the released source (`c2e/modules/netbabel/`: `NetHandlers.cpp`,
`NetworkImplementation.cpp`, `NetLogImplementation.cpp`) and compiles
against NetNatsue unchanged.

The expected layout matches the include paths in the original project
files: this repository checked out as `Babel/NetNatsue` next to
`c2e`.

    <your tree>/
        c2e/                      (from your engine source dump)
            modules/netbabel/     (the surviving module wrapper)
            server/HistoryFeed/   (empty in known dumps; see step 2)
            ...
        Babel/
            NetNatsue/            (this repository)

From `<your tree>`:

    git clone https://github.com/robinduckett/netnatsue Babel/NetNatsue

## Step 1: the library

The library is five `.cpp` files with no dependencies outside the
platform socket API (`ws2_32` on Windows).  Build them with whatever
builds the rest of your tree, and put `Babel/NetNatsue` on the
include path so `<DSNetManager.h>` resolves.

## Step 2: HistoryTransferOut

The module wrapper includes
`../../server/HistoryFeed/HistoryTransferOut.h`, which is missing
from the `c2e` tree in the known source dumps.  Copy the
reimplementation from this repository:

    cp Babel/NetNatsue/compat/HistoryTransferOut.h   c2e/server/HistoryFeed/
    cp Babel/NetNatsue/compat/HistoryTransferOut.cpp c2e/server/HistoryFeed/

If your dump also contains the "Creatures 3" server sources, the
original `src/server/HistoryFeed/HistoryTransferOut.{h,cpp}` works
too, after fixing its HistoryStore.h include path for the new
location.  The two are wire-compatible.

## Step 3: build wiring

### Windows / Visual C++ 6 (original Docking Station Build 195 dump)

The original `netbabel.dsp` lists the lost Babel sources and cannot
build.  Use the replacement project:

    copy Babel\NetNatsue\integration\vc6\netnatsue.dsp c2e\modules\netbabel\

Add it to the workspace next to `engine.dsw`.  Build the engine
first (the module links `engine.lib`), then build `netnatsue`.  The
output is `engine-netbabel.dll`, which the engine discovers at
startup by filename.  Put the DLL in the Docking Station directory.

### Unix makefile build

`integration/module.mk` replaces `c2e/modules/netbabel/module.mk`
and points the existing `lc2e-netbabel.so` rules at the NetNatsue
sources.

### CMake builds (exodus and other newer ports)

Either `add_subdirectory(../Babel/NetNatsue netnatsue)` and link the
`netnatsue` target, or add these sources to the engine build:

    modules/netbabel/NetHandlers.cpp
    modules/netbabel/NetLogImplementation.cpp
    modules/netbabel/NetworkImplementation.cpp
    server/HistoryFeed/HistoryTransferOut.cpp
    ../Babel/NetNatsue/DSNetManager.cpp
    ../Babel/NetNatsue/NetMemoryPack.cpp
    ../Babel/NetNatsue/NetMemoryUnpack.cpp
    ../Babel/NetNatsue/NetNatsueProtocol.cpp
    ../Babel/NetNatsue/NetNatsueSocket.cpp

with `../Babel/NetNatsue` on the include path.  Define
`MODULE_EMBEDDED` so `ModuleImporter.cpp` links the module statically
instead of scanning for DLLs.  The engine's embedded-module code path
is only compiled on non-Windows platforms; Windows builds use the DLL
route above.

The `NET:` commands also require the engine's PRAY subsystem
(`common/PRAYFiles/*.cpp`, which needs zlib).  Most engine builds
include it already; some experimental CMake trees do not list it.

## Step 4: server configuration

`server.cfg` beside the engine, as in the original client:

    "Override Port" 49152
    "Override Server" eemfoo.org

Setup instructions for the eemfoo.org server, including a ready-made
copy of this file, are at https://eem.foo/eem-foo-warp-natsue.  To
run your own server, see the `natsue` directory of
https://github.com/20kdc/c3ds-projects.

## Verifying without the engine

`test/NetNatsueTest.cpp` exercises the client against a Natsue server
using the call sequences the engine module makes.  See the Testing
section of README.md.
