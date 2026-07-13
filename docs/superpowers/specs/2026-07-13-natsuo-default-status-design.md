# Default Status "Natsuo <build date>" — Design

Date: 2026-07-13
Status: Approved

## Problem

`NET: WHAT` reports the client's current action via
`DSNetManager::DebugGetCurrentAction()`.  When the client is idle,
`myCurrentAction` is an empty string, which the game presents as the
default "Nothing" status.  The user wants the idle status to identify
this client and its build date instead: `Natsuo YYYY-MM-DD`
("Natsuo" spelling is intentional).

## Decisions (made during brainstorming)

- Lazy fallback at the single read site
  (`DebugGetCurrentAction`), NOT re-initialising the ~14 places that
  clear `myCurrentAction`.
- Build date is generated at build time by CMake: an
  `add_custom_target` runs a `cmake -P` script every build that
  writes `NetBuildDate.h` (defining `NETNATSUE_BUILD_DATE
  "YYYY-MM-DD"`, UTC) into the build tree, using copy-if-different
  so nothing recompiles unless the date actually changed.  CMake
  builds define `NETNATSUE_BUILD_DATE_HEADER` to opt into including
  it.
- Non-CMake builds (INTEGRATING.md supports compiling the five
  `.cpp` files directly) fall back to converting the `__DATE__`
  macro to `YYYY-MM-DD` in `DSNetManager.cpp`; that path refreshes
  only when `DSNetManager.cpp` recompiles, which is acceptable
  there.
- Deliberate deviation from the original client (empty string when
  idle) — that is the point of the request.
- Ships on the existing `natsue-client-modes` branch / PR
  (robinduckett/netnatsue#1), per user instruction.

## Change

- `cmake/BuildDate.cmake`: script writing the generated header via
  `string(TIMESTAMP ... UTC)` + `copy_if_different`.
- `CMakeLists.txt`: custom target generating
  `${CMAKE_CURRENT_BINARY_DIR}/generated/NetBuildDate.h` each build;
  `netnatsue` depends on it, gets the generated dir on its private
  include path and the `NETNATSUE_BUILD_DATE_HEADER` define.
- `DSNetManager.cpp`: includes `NetBuildDate.h` when
  `NETNATSUE_BUILD_DATE_HEADER` is defined; a static helper builds
  the default status string once — `"Natsuo "` +
  (`NETNATSUE_BUILD_DATE` if defined, else `__DATE__` converted to
  `YYYY-MM-DD`).
- `DebugGetCurrentAction()` returns that default when
  `myCurrentAction` is empty, otherwise `myCurrentAction` unchanged.
- Busy states ("Logging in as …", "Fetching …") are untouched.
- No wire-protocol change, no engine-side (c2e-ios) change; NET: WHAT
  already routes through `DebugGetCurrentAction`.

## Testing

Offline check in `test/NetNatsueTest.cpp` (runs under `--offline`
and at the start of live runs): a fresh, never-connected
`DSNetManager` reports a current action of exactly `"Natsuo "`
followed by 10 characters shaped `dddd-dd-dd`.  The exact date is
not asserted (the library may be compiled on a different day than
the test run).  Existing offline checks keep passing.

## Out of scope

- Changing any of the transient action strings.
- Engine-side or server-side changes.
