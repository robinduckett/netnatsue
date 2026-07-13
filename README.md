# NetNatsue

A cross-platform recreation of the lost "Babel" client libraries used
by Docking Station's networking module (`engine-netbabel.dll` /
`lc2e-netbabel.so`).  It implements the NetBabel protocol, as
documented by 20kdc's c3ds-projects, and connects to Natsue servers
such as the one at `eemfoo.org` (https://eem.foo/eem-foo-warp-natsue).

The released Creatures Evolution Engine source contains the
engine-side module wrapper (`c2e/modules/netbabel/`: `NetHandlers.cpp`
with the `NET:` CAOS commands, `NetworkImplementation.cpp`,
`NetLogImplementation.cpp`) but not the proprietary `Babel/` libraries
it linked against (`BabelCloak`, `BabelClient`, `BabelCommon`).
NetNatsue provides the headers those wrapper files include
(`DSNetManager.h`, `NetMessages.h`, `NetMemoryPack.h`,
`NetMemoryUnpack.h`, `QueuedMessage.h`, `NetLogInterface.h`), so the
wrapper compiles unchanged.

Checked out as `Babel/NetNatsue` next to a `c2e` tree, it is a
drop-in for the original Docking Station Build 195 source and for
newer ports of it.  See [INTEGRATING.md](INTEGRATING.md).  The code
builds with Visual C++ 6.0 and with current GCC/Clang.  The library
depends on nothing beyond the platform socket API.

## Features

Verified against a Natsue server by the test suite in
`test/NetNatsueTest.cpp`:

* Login, first-login account registration, `NET: ERRA` error codes
  for refused logins
* Presence: WWR add/remove, user online/offline notifications
  (`NET: WHON` / `NET: WHOF` / `NET: ULIN`)
* `NET: UNIK` (nickname lookup), `NET: RUSO` (random online user),
  `NET: STAT` (connection statistics)
* Warp transfer in both directions: outbox files (`NET: EXPO` /
  `NET: MAKE`) are sent as Packed Babel Messages; incoming PRAY
  messages are spooled to the inbox for `PRAY REFR`; `NET: FROM`
  reports the sender
* Acknowledgement of the server's virtual circuit pings, which Natsue
  uses to confirm delivery of creatures and mail
* Creature history upload
* `NET: WRIT` / `NET: HEAR` CAOS messages between machines

## Configuration

`server.cfg` in the game directory, as in the original client.  For
the eemfoo.org server:

    "Override Port" 49152
    "Override Server" eemfoo.org

An optional `"Client Mode"` entry selects how the client identifies
itself to Natsue:

    "Client Mode" modern

`modern` (the default when the entry is absent) sends Natsue's
not-actually-Babel handshake extension, so the server disables the
workarounds it keeps for the original client's networking bugs — in
particular, contacts added through the server's `contact` command
appear live instead of at the next login.  `original` presents as a
stock Babel client, exercising Natsue's vanilla-compatible path.

Setup instructions for that server are at
https://eem.foo/eem-foo-warp-natsue, including a ready-made copy of
this file.  With no
configuration the client falls back to the original
`heart.creatures.net:49152`, which no longer exists.

Nickname and password are set by the game via `NET: PASS`.  Passwords
are stored in `password.cfg` only if the
`engine_netbabel_save_passwords` game variable is 1, as before.

## Design notes

The public `DSNetManager` API matches the original library; the
signatures were reconstructed from the module wrapper's call sites
and the `tools/nettest` sources.  Internals differ:

* Single threaded.  All socket work happens on the caller's thread
  using a non-blocking socket, pumped once per engine tick from
  `SendOrdinaryMessages()`.  Blocking CAOS commands (`NET: LINE`,
  `NET: WHON`, `NET: UNIK`, `NET: RUSO`, `NET: STAT`) use the
  engine's block-and-poll pattern (the `bool& block` parameters).
  Two calls wait synchronously with a timeout, because their callers
  need an immediate result: `DSFeedHistory` (8s) and `IsUserOnline`
  for users outside the whose-wanted register (5s).  Both normally
  complete in one round trip.
* `NET: WRIT` messages go down the ordinary message path instead of
  virtual circuits.  The original virtual circuit transport could
  freeze the game; Natsue routes writs sent as ordinary messages.
* Incoming virtual circuit connects are acknowledged
  (`C_TID_CLIENT_COMMAND`, subcommand 0xE) and otherwise ignored.
  Natsue uses these circuits as delivery confirmation pings.
* The handshake sends `CLIENTVERSION=1`, `PRODUCTCODE=2` and, by
  default, `20240219` in the extension field — Natsue's
  not-actually-Babel marker (`tob/Packets/CTOS.md`), which turns off
  the server's vanilla-client bug workarounds and enables live
  contact adds.  Setting `"Client Mode" original` in `server.cfg`
  sends 0 there instead, identifying as a stock Babel client.
* `NET: STAT` reports locally measured time online and byte counts;
  the users-online figure comes from the server.

Spool files are named `<serial>T-<uid>+<hid>.warp` in both
directions.  The outbox filename encodes the destination; the inbox
filename encodes the sender, which is what `NET: FROM` reads.

`compat/HistoryTransferOut.{h,cpp}` is a reimplementation of the
engine-side history packer, which is absent from the known `c2e`
dumps.  It installs into `c2e/server/HistoryFeed/`.

## Wire protocol

The framing lives in `NetNatsueProtocol.{h,cpp}`: 32-byte
little-endian packet headers; transactions matched by
client-allocated ticket numbers, checked before the packet type
(Natsue sends dummy responses with a zero type field); Packed Babel
Messages (24-byte header, sender HID before UID) carrying a 12-byte
C2E message header (type 0 for PRAY files, 1 for writs) and the
payload.  The protocol reference is the `tob` book in c3ds-projects;
where the documents were ambiguous, the Natsue server source was
treated as ground truth.

Strings are byte-transparent (the game's native Windows-1252).  The
codec is explicitly little-endian, independent of host byte order.

## Building

    cmake -B build && cmake --build build

builds the static library and the test harness.  Alternatively,
compile the five `.cpp` files with the engine; they need a C++
compiler and sockets, nothing else.  Engine build wiring (VC6 `.dsp`,
makefile module, CMake) is described in
[INTEGRATING.md](INTEGRATING.md).

## Testing

`test/NetNatsueTest.cpp` is a standalone harness; no engine needed.
Run a Natsue server locally (the `natsue` directory of c3ds-projects;
set `firewallLevel minimal` in `ntsuconf.txt` so the harness's
hand-rolled PRAY files pass the content filters), then:

    ./build/netnatsue-test [host] [port] [modern|original]

`./build/netnatsue-test --offline` runs just the offline packet
checks; the mode argument (default `modern`) selects the client
mode used for the live run.

It logs in two users and runs them through the command surface the
engine module uses, including a warp file round trip, and reports a
check/failure count.

## Credits

* 20kdc and the c3ds-projects contributors: the NetBabel protocol
  documentation ("tob") and the Natsue server.
  https://github.com/20kdc/c3ds-projects
* Creature Labs: the Creatures Evolution Engine and the original
  Babel design.

NetNatsue is a fan project and is not affiliated with the rights
holders of Creatures.  This repository contains no Creature Labs
source code.

## License

CC0 1.0 Universal.  See [COPYING.txt](COPYING.txt).
