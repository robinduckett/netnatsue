# Natsue Client Modes (Original / Modern) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Send the Natsue "Tower" handshake magic (20240219) by default so the server disables its vanilla-netbabel bug workarounds (enabling live contact adds), with an `original` opt-out via a `Client Mode` key in server.cfg.

**Architecture:** A new `HANDSHAKE_MAGIC_MODERN` constant and an explicit `magic` parameter on the pure `BuildHandshake` function; a static `DSNetManager::SetClientMode` (same pattern as `OverrideHost`) mapped to the magic at the single call site; the engine-side module (c2e-ios `NetHandlers.cpp`) reads `Client Mode` from server.cfg on every connect.

**Tech Stack:** C++ (C++98-style codebase, tab indentation, `my`/`our` member prefixes), CMake, standalone test harness `test/NetNatsueTest.cpp`, Natsue server (Java) for live verification.

**Spec:** `docs/superpowers/specs/2026-07-13-natsue-client-modes-design.md`

## Global Constraints

- The mode controls ONLY the handshake magic field. No other behaviour is gated on it.
- Default is Modern (`20240219`); Original sends `0` (byte-identical to current behaviour).
- server.cfg key: `Client Mode`, values `modern` / `original`, case-insensitive; absent → Modern; unrecognised → warn + Modern.
- Match existing code style: tabs, C++98 idioms (no `auto`, no lambdas, no `nullptr`), comment style of surrounding code.
- Repos: `/Users/robin/dev/netnatsue` (branch off `main`) and `/Users/robin/dev/c2e-ios` (branch off `feature/ce-wave1-fixes`).

---

### Task 1: Protocol layer — magic constant, `BuildHandshake` magic parameter, offline byte tests

**Files:**
- Modify: `NetNatsueProtocol.h:53-56` (handshake constants), `:112-113` (BuildHandshake decl)
- Modify: `NetNatsueProtocol.cpp:112-136` (BuildHandshake)
- Modify: `DSNetManager.cpp:391-392` (call site — pass 0 for now, behaviour unchanged until Task 2)
- Test: `test/NetNatsueTest.cpp`

**Interfaces:**
- Produces: `NetNatsueProtocol::HANDSHAKE_MAGIC_MODERN` (`const int`, 20240219); `BuildHandshake(const BabelUIN&, int ticket, const std::string& nickname, const std::string& password, int magic)`; harness `--offline` flag running only offline checks.

- [ ] **Step 1: Write the failing test.** In `test/NetNatsueTest.cpp`, add below the `PumpBoth` function (line ~89):

```cpp
// Little-endian int at a byte offset, for inspecting built packets
static int GetIntAt(const std::vector<char>& data, size_t offset)
{
	return (unsigned char)data[offset]
		| ((unsigned char)data[offset + 1] << 8)
		| ((unsigned char)data[offset + 2] << 16)
		| ((unsigned char)data[offset + 3] << 24);
}

// Offline: the two client modes must differ only in the handshake
// extension field at offset +40 (tob/Packets/CTOS.md)
static void TestHandshakeBytes()
{
	using namespace NetNatsueProtocol;

	std::cout << "Handshake bytes:" << std::endl;
	BabelUIN uin(42, 1);
	std::vector<char> original = BuildHandshake(uin, 7, "nick", "pass", 0);
	std::vector<char> modern = BuildHandshake(uin, 7, "nick", "pass",
		HANDSHAKE_MAGIC_MODERN);
	Check(original.size() == 52 + 5 + 5, "handshake is 52 bytes plus strings");
	Check(original.size() == modern.size(), "same length in both modes");
	Check(GetIntAt(original, 40) == 0, "original mode sends 0 at +40");
	Check(GetIntAt(modern, 40) == HANDSHAKE_MAGIC_MODERN,
		"modern mode sends the Natsue magic at +40");
	bool sameOtherwise = true;
	for (size_t i = 0; i < original.size() && sameOtherwise; ++i)
		if (i < 40 || i >= 44)
			sameOtherwise = (original[i] == modern[i]);
	Check(sameOtherwise, "modes differ only in the +40 field");
}
```

In `main` (line ~154), add an offline-only entry point and run the byte checks in normal runs too, before `MakeDir`:

```cpp
	if (argc > 1 && std::string(argv[1]) == "--offline")
	{
		TestHandshakeBytes();
		std::cout << theChecks << " checks, " << theFailures
			<< " failures" << std::endl;
		return theFailures ? 1 : 0;
	}
```

and immediately after the existing `std::cout << "NetNatsue protocol test against ..."` line:

```cpp
	TestHandshakeBytes();
```

- [ ] **Step 2: Run the test to verify it fails.**

Run: `cmake -B build && cmake --build build 2>&1 | tail -5`
Expected: compile error — `BuildHandshake` does not take 5 arguments.

- [ ] **Step 3: Minimal implementation.**

`NetNatsueProtocol.h` — extend the constants block (lines 53-56):

```cpp
	// Handshake constants.  The 1 and 2 are believed to be
	// CLIENTVERSION and PRODUCTCODE (2 = Docking Station).
	const int HANDSHAKE_CLIENT_VERSION = 1;
	const int HANDSHAKE_PRODUCT_CODE = 2;
	// Natsue extension (c3ds-projects, tob/Packets/CTOS.md): a value
	// of 20240219 or higher in the third handshake int (offset +40,
	// always 0 in vanilla Babel) marks the client as not-actually-
	// Babel, letting the server drop its workarounds for the vanilla
	// client's networking bugs (e.g. contacts can be added live
	// instead of on the next login).
	const int HANDSHAKE_MAGIC_MODERN = 20240219;
```

`NetNatsueProtocol.h` — declaration (line 112):

```cpp
	std::vector<char> BuildHandshake(const BabelUIN& user, int ticket,
		const std::string& nickname, const std::string& password,
		int magic);
```

`NetNatsueProtocol.cpp` — definition (line 112), signature plus the +40 write:

```cpp
std::vector<char> BuildHandshake(const BabelUIN& user, int ticket,
	const std::string& nickname, const std::string& password,
	int magic)
```

and replace lines 126-128 (`// Zero marks us as ...` + `PutInt(out, 0);`) with:

```cpp
	// 0 marks us as a stock Babel client, which keeps all of
	// Natsue's compatibility workarounds enabled;
	// HANDSHAKE_MAGIC_MODERN claims the not-actually-Babel extension
	PutInt(out, magic);
```

`DSNetManager.cpp:391` — keep behaviour identical for now:

```cpp
			std::vector<char> handshake = BuildHandshake(
				myUserUIN, NextTicket(), myNickname, myPassword, 0);
```

- [ ] **Step 4: Run the test to verify it passes.**

Run: `cmake --build build && ./build/netnatsue-test --offline`
Expected: `5 checks, 0 failures`, exit 0.

- [ ] **Step 5: Commit.**

```bash
git add NetNatsueProtocol.h NetNatsueProtocol.cpp DSNetManager.cpp test/NetNatsueTest.cpp
git commit -m "Give BuildHandshake an explicit Natsue extension-field parameter"
```

---

### Task 2: `DSNetManager` client mode API, Modern default

**Files:**
- Modify: `DSNetManager.h:75-77` (public statics), `:274-276` (static storage)
- Modify: `DSNetManager.cpp:214-215` (static definitions), `:286-291` (near `OverrideHost`), `:391` (call site)
- Test: `./build/netnatsue-test --offline` (compile + existing checks; the mapping itself is exercised live in Task 5)

**Interfaces:**
- Consumes: `NetNatsueProtocol::HANDSHAKE_MAGIC_MODERN`, 5-arg `BuildHandshake` (Task 1). `DSNetManager.cpp` has `using namespace NetNatsueProtocol;` at line 30.
- Produces: `DSNetManager::ClientMode` enum (`CLIENT_MODE_MODERN`, `CLIENT_MODE_ORIGINAL`); `static void DSNetManager::SetClientMode(ClientMode)`. Default is Modern.

- [ ] **Step 1: Implement.**

`DSNetManager.h` — after the `OverrideHost` declaration (line 77):

```cpp
	// Handshake identity, set from the "Client Mode" entry in
	// server.cfg.  Modern (the default) claims Natsue's
	// not-actually-Babel extension, so the server drops its
	// vanilla-bug workarounds and contacts can be added live;
	// Original presents as a stock Babel client.
	enum ClientMode
	{
		CLIENT_MODE_MODERN,
		CLIENT_MODE_ORIGINAL,
	};
	static void SetClientMode(ClientMode mode);
```

`DSNetManager.h` — static storage (after line 276 `static int ourOverridePort;`):

```cpp
	static ClientMode ourClientMode;
```

`DSNetManager.cpp` — definition next to the other statics (line 215):

```cpp
DSNetManager::ClientMode DSNetManager::ourClientMode = DSNetManager::CLIENT_MODE_MODERN;
```

`DSNetManager.cpp` — setter after `OverrideHost` (line 291):

```cpp
// static
void DSNetManager::SetClientMode(ClientMode mode)
{
	ourClientMode = mode;
}
```

`DSNetManager.cpp:391` — map mode to magic:

```cpp
			std::vector<char> handshake = BuildHandshake(
				myUserUIN, NextTicket(), myNickname, myPassword,
				ourClientMode == CLIENT_MODE_ORIGINAL
					? 0 : HANDSHAKE_MAGIC_MODERN);
```

- [ ] **Step 2: Build and run offline checks.**

Run: `cmake --build build && ./build/netnatsue-test --offline`
Expected: `5 checks, 0 failures`.

- [ ] **Step 3: Commit.**

```bash
git add DSNetManager.h DSNetManager.cpp
git commit -m "Add DSNetManager client modes; identify as modern client by default"
```

---

### Task 3: Test harness mode argument

**Files:**
- Modify: `test/NetNatsueTest.cpp` `main` (line ~154)

**Interfaces:**
- Consumes: `DSNetManager::SetClientMode` / `ClientMode` (Task 2).
- Produces: harness usage `./netnatsue-test [host] [port] [modern|original]` (default `modern`).

- [ ] **Step 1: Implement.** In `main`, after the existing `port` parsing (line ~161) and before the `std::cout << "NetNatsue protocol test against ..."` line:

```cpp
	std::string mode = "modern";
	if (argc > 3)
		mode = argv[3];
	if (mode == "original")
		DSNetManager::SetClientMode(DSNetManager::CLIENT_MODE_ORIGINAL);
	else if (mode != "modern")
	{
		std::cout << "Unknown mode '" << mode
			<< "': expected modern or original" << std::endl;
		return 1;
	}
```

and extend the banner line to include the mode:

```cpp
	std::cout << "NetNatsue protocol test against " << host << ":" << port
		<< " in " << mode << " mode" << std::endl;
```

- [ ] **Step 2: Build and check argument handling.**

Run: `cmake --build build && ./build/netnatsue-test localhost 49152 bogus; echo "exit=$?"`
Expected: `Unknown mode 'bogus': expected modern or original`, `exit=1`.

- [ ] **Step 3: Commit.**

```bash
git add test/NetNatsueTest.cpp
git commit -m "Test harness: select client mode from the command line"
```

---

### Task 4: Documentation

**Files:**
- Modify: `README.md` (Configuration §45-61, Design notes bullet at lines 84-86, Testing §123-134)
- Modify: `INTEGRATING.md` (Step 4, lines 93-103)

- [ ] **Step 1: README Configuration.** After the `"Override Port" / "Override Server"` example block (line 51), add:

```markdown
An optional `"Client Mode"` entry selects how the client identifies
itself to Natsue:

    "Client Mode" modern

`modern` (the default when the entry is absent) sends Natsue's
not-actually-Babel handshake extension, so the server disables the
workarounds it keeps for the original client's networking bugs — in
particular, contacts added through the server's `contact` command
appear live instead of at the next login.  `original` presents as a
stock Babel client, exercising Natsue's vanilla-compatible path.
```

- [ ] **Step 2: README Design notes.** Replace the bullet at lines 84-86 (`* The handshake identifies as a stock Babel client ...`) with:

```markdown
* The handshake sends `CLIENTVERSION=1`, `PRODUCTCODE=2` and, by
  default, `20240219` in the extension field — Natsue's
  not-actually-Babel marker (`tob/Packets/CTOS.md`), which turns off
  the server's vanilla-client bug workarounds and enables live
  contact adds.  Setting `"Client Mode" original` in `server.cfg`
  sends 0 there instead, identifying as a stock Babel client.
```

- [ ] **Step 3: README Testing.** Update the run line (line 130) to:

```markdown
    ./build/netnatsue-test [host] [port] [modern|original]

`./build/netnatsue-test --offline` runs just the offline packet
checks; the mode argument (default `modern`) selects the client
mode used for the live run.
```

- [ ] **Step 4: INTEGRATING.md Step 4.** Extend the server.cfg example (lines 97-98) to:

```markdown
    "Override Port" 49152
    "Override Server" eemfoo.org
    "Client Mode" modern

`"Client Mode"` is optional: `modern` (default) sends Natsue's
not-actually-Babel handshake extension so the server can disable its
vanilla-bug workarounds (live contact adds); `original` identifies
as a stock Babel client.  The engine-side module re-reads the key on
every connect, so it can be changed without restarting the game.
```

- [ ] **Step 5: Commit.**

```bash
git add README.md INTEGRATING.md
git commit -m "Document the Client Mode setting"
```

---

### Task 5: Live verification against a local Natsue server

**Files:** none (verification only). Working dir for the server: the session scratchpad clone of `c3ds-projects`.

- [ ] **Step 1: Build Natsue.**

```bash
cd <scratchpad>/c3ds-projects/natsue && ./build.sh
```

Expected: `natsue/cradle/target/natsue-server-cradle-0.666-SNAPSHOT-jar-with-dependencies.jar` exists. (Needs only `java`; `umvn` is bundled. Java 21 is installed.)

- [ ] **Step 2: First run + config.** From `natsue/cradle`, start `java -jar target/natsue-server-cradle-0.666-SNAPSHOT-jar-with-dependencies.jar` in the background, wait for it to settle, stop it, then set `firewallLevel minimal` in the generated `ntsuconf.txt` (per README.md Testing) and restart it in the background.

- [ ] **Step 3: Run the harness in both modes.**

```bash
cd /Users/robin/dev/netnatsue
./build/netnatsue-test localhost 49152 modern
./build/netnatsue-test localhost 49152 original
```

Expected: both runs end `N checks, 0 failures`. If any check fails, stop and debug before proceeding (superpowers:systematic-debugging).

- [ ] **Step 4: Confirm the classification server-side.** In the Natsue log output (cradle stdout / `log/` dir), find the login lines for the harness users and confirm the modern run is classified `Tower` and the original run `Babel` (the classification appears via `CTOSHandshake.clientVersion`; the `!System` `whois` command reports it as `Online (<version>)`). Record the evidence in the final report.

- [ ] **Step 5: Stop the server.** Kill the background Natsue process.

---

### Task 6: netnatsue branch + PR

- [ ] **Step 1:** All work above happens on branch `natsue-client-modes` (created from `main` before Task 1; this task is just the wrap-up).

- [ ] **Step 2: Push and raise the PR.**

```bash
git push -u origin natsue-client-modes
gh pr create --title "Send Natsue's modern-client handshake magic, with Original/Modern client modes" --body "<summary of spec, behaviour, testing evidence>

🤖 Generated with [Claude Code](https://claude.com/claude-code)"
```

Expected: PR URL printed.

---

### Task 7: c2e-ios wiring — read `Client Mode` from server.cfg, bump submodule

**Files:**
- Modify: `/Users/robin/dev/c2e-ios/c2e/modules/netbabel/NetHandlers.cpp:191-199` (beside the Override Server read)
- Modify: submodule pointer `/Users/robin/dev/c2e-ios/Babel/NetNatsue`

**Interfaces:**
- Consumes: `DSNetManager::SetClientMode` / `ClientMode` (Task 2), `Configurator::Get(const std::string&, std::string&)` (leaves value untouched when the key is absent), `NetLogImplementation::Log(const char*)` (already included at NetHandlers.cpp line 6).

- [ ] **Step 1: Branch.** In `/Users/robin/dev/c2e-ios`, create branch `natsue-client-modes` from `feature/ce-wave1-fixes`.

- [ ] **Step 2: Bump the submodule** to the pushed `natsue-client-modes` commit of netnatsue:

```bash
cd /Users/robin/dev/c2e-ios/Babel/NetNatsue
git fetch origin && git checkout <netnatsue branch HEAD sha>
```

- [ ] **Step 3: Implement the config read.** In `NetHandlers.cpp`, directly after the `if (!overrideHost.empty()) DSNetManager::OverrideHost(...)` block (line ~199):

```cpp
		// "Client Mode" selects the handshake identity: modern (the
		// default) claims Natsue's not-actually-Babel extension so
		// contacts can be added live; original presents as a stock
		// Babel client.  Re-read each connect, like the overrides.
		std::string clientMode;
		myServerConfig.Get("Client Mode", clientMode);
		for (std::string::size_type ci = 0; ci < clientMode.size(); ++ci)
			clientMode[ci] = (char)tolower((unsigned char)clientMode[ci]);
		if (clientMode == "original")
			DSNetManager::SetClientMode(DSNetManager::CLIENT_MODE_ORIGINAL);
		else
		{
			if (!clientMode.empty() && clientMode != "modern")
			{
				NetLogImplementation warning;
				warning.Log("Unrecognised \"Client Mode\" in server.cfg; using modern");
			}
			DSNetManager::SetClientMode(DSNetManager::CLIENT_MODE_MODERN);
		}
```

Add `#include <ctype.h>` beside the other includes if `tolower` is not already available.

- [ ] **Step 4: Build.**

Run: `cmake --build /Users/robin/dev/c2e-ios/c2e/build --target netbabel 2>&1 | tail -5` (or the tree's equivalent module target; discover with `cmake --build build --target help | grep -i netbabel` if needed).
Expected: compiles cleanly.

- [ ] **Step 5: Commit (and PR if the repo has a GitHub remote).**

```bash
cd /Users/robin/dev/c2e-ios
git add Babel/NetNatsue c2e/modules/netbabel/NetHandlers.cpp
git commit -m "netbabel: read Client Mode from server.cfg for Natsue modern handshake"
```

If `origin` is a GitHub remote, push the branch and open a PR titled "netbabel: Client Mode (Natsue modern handshake) wiring"; otherwise leave the branch local and report that.
