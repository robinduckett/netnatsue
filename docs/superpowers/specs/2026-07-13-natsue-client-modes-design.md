# Natsue Client Modes (Original / Modern) — Design

Date: 2026-07-13
Status: Approved

## Problem

NetNatsue currently identifies itself to the Natsue server as a stock
(vanilla) Babel client.  Natsue therefore keeps all of its
vanilla-netbabel bug workarounds enabled for us — most visibly the
"reconnect" contact-add strategy, where a contact added via the
server's `contact` command only appears in the game at the next login
(vanilla clients crash on live contact adds due to the
`NET: ULIN`/GetConnectionInfo seize-up bug).

Natsue defines a protocol extension for this: in the CTOS handshake
packet (type 0x25), the int at offset +40 — always 0 in vanilla
Babel — marks the client as **Tower** ("not actually Babel") when it
is `20240219` or higher (`c3ds-projects/tob/Packets/CTOS.md`).  For
Tower clients Natsue may disable networking-layer workarounds; today
that means live ("loud") contact adds via
`contactAddLoudForTowerUsers` (default on).  The reference client
from Natsue's creator (`reference/babelclient/BabelClient.cpp`)
sends exactly `20240219`.

NetNatsue is a clean reimplementation without the vanilla bugs, so it
should be able to claim Tower status — while retaining a way to
present as vanilla Babel to exercise Natsue's compatibility path.

## Decisions (made during brainstorming)

- The mode controls **only** the handshake magic field.  No other
  client behaviour is gated on it.
- **Modern is the default.**  Original is an opt-out via server.cfg.
- Both repos are in scope: this library, plus the server.cfg wiring
  in c2e-ios's engine-side netbabel module and a submodule bump.
- Plumbing follows the existing static `OverrideHost` pattern
  (Approach A).

## 1. Protocol layer (`NetNatsueProtocol`)

- Add `const int HANDSHAKE_MAGIC_MODERN = 20240219;` beside the
  existing handshake constants in `NetNatsueProtocol.h`, with a
  comment citing the Natsue extension (`tob/Packets/CTOS.md`): a
  value >= 20240219 at handshake offset +40 marks the client as
  not-actually-Babel and lets Natsue disable networking-layer bug
  workarounds.
- `BuildHandshake` gains a final `int magic` parameter, written
  where the hardcoded `PutInt(out, 0)` sits today
  (`NetNatsueProtocol.cpp:128`).  Callers pass 0 for Original or
  `HANDSHAKE_MAGIC_MODERN` for Modern.  The function stays pure and
  directly testable.

## 2. Mode API (`DSNetManager`)

- New enum on `DSNetManager`:
  `ClientMode { CLIENT_MODE_MODERN, CLIENT_MODE_ORIGINAL }`.
- New static `SetClientMode(ClientMode)` with static storage
  (`ourClientMode`), defaulting to `CLIENT_MODE_MODERN` — same
  pattern as the existing static `OverrideHost`.
- `PumpConnectPhase` (the single `BuildHandshake` call site,
  `DSNetManager.cpp:391`) maps the mode to the magic value:
  Modern -> `HANDSHAKE_MAGIC_MODERN`, Original -> 0.
- Nothing else consults the mode.

## 3. server.cfg key + c2e-ios wiring

- New server.cfg key **`Client Mode`** with values `modern`
  (default when the key is absent) and `original`, matched
  case-insensitively.  An unrecognised value logs a warning and
  falls back to Modern.
- In `c2e-ios/c2e/modules/netbabel/NetHandlers.cpp`, read the key
  immediately beside the existing `Override Server` /
  `Override Port` reads (after `myServerConfig.Reload()`), so the
  mode re-reads on every connect and the shell's Online settings can
  flip it without restarting the game.  Map the string to the enum
  there and call `DSNetManager::SetClientMode`.
- Bump the `Babel/NetNatsue` submodule pointer in c2e-ios once this
  repo's change lands.

## 4. Documentation

- `README.md` and `INTEGRATING.md`: document the `Client Mode` key,
  what the magic changes on Natsue's side (Tower classification,
  live contact adds instead of add-on-reconnect), and that Original
  mode exists to exercise Natsue's vanilla-compatible path.

## 5. Testing

- **Offline byte checks** (in `test/NetNatsueTest.cpp`, run before
  any connection): build handshakes in both modes; `Check` that the
  int at offset +40 is `20240219` / `0` respectively, and that all
  other bytes of the two packets are identical.
- **Live harness**: the test binary gains an optional mode argument
  (`modern` default, `original` opt-out) applied via
  `DSNetManager::SetClientMode` before connecting, so the full
  two-user protocol test can run against a Natsue instance in both
  modes.  A Modern-mode run proves Natsue accepts the extension; an
  Original-mode run proves the vanilla path still works end-to-end.

## Error handling

- Absent `Client Mode` key: silently default to Modern.
- Unrecognised value: log a warning, use Modern.
- No wire-level error handling changes: the handshake response path
  is untouched, and servers other than Natsue ignore the field (it
  was reserved/zero in the original protocol; the original servers
  are long gone).

## Out of scope

- Replicating vanilla netbabel bugs in Original mode (Original only
  changes the advertised magic, not behaviour).
- Any use of the mode outside `BuildHandshake`.
- Natsue-side configuration changes.
