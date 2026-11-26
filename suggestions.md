# Suggestions for reverse-engineering a sibling daemon

Lessons learned while rebuilding `AccountInventoryDaemon` (C++). Most apply to the other
`D:\service\<Daemon>\bin` modules that share the same BloodLib-style framework, console DLL and
NP gateway. Treat this as a checklist, not a guarantee — verify each item against your own oracle.

## 1. Start from a sibling rewrite, not from scratch

The framework code (packet compose/decompose, logging, console, listener/session, ODBC wrapper,
config reader) is duplicated across siblings. Copy the closest already-rewritten module first and
change only the application layer. In this project reusing the sibling `BloodLib` saved writing
the entire network/log/packet stack; the remaining work was the protocol and handler semantics.

Watch for **version skew in the copied framework**: a sibling may be built against a newer
`bsConsole_sv64.dll` or a different daemon version. Compare file hashes of the DLLs you ship, not
just their names.

## 2. Keep production read-only; use a private oracle

- Never patch, replace or reconfigure files under `D:\service`. Take a private copy of the binary
  and config into `analysis\oracle\` and run that as the oracle.
- Give the test daemons **offset ports** (we used 22700/22701/32700 for ours and
  23700/23701/33700 for the oracle). The production ports (31700/21700/21701 here) must not be
  bound by a test instance.
- Use a logging TCP proxy between the oracle and the real gateway to recover wire formats. Our NP
  protocol notes (`analysis/NP_PROTOCOL.md`) came entirely from that.
- When the gateway caches data, do **not** restart the whole SpawnSrv stack. The management
  endpoint reloads a single app:
  `POST http://localhost:6605/apps/<ProductType>.<AppIndex>.<Deployment>.<Epoch>/stop`.
  Get the current epoch from `http://localhost:6605/apps-state`. We used this to make a fixture
  purchase visible to `PurchaseSrv` without touching other services.

## 3. Config and double-click behaviour are part of the deliverable

- The shipped `release\bin\accountInventoryd.config.xml` was made **byte-identical** to the
  production config; hashing it against `D:\service\...` is cheap, strong evidence.
- Check the no-argument launch, not just `--conf=...`. A plain double-click resolves the config
  from the executable's directory; a UTF-8 BOM and absolute production paths (`root-path`,
  `log-path`, `cert-file`) must all work.
- Verify the window console too: capture both windows with `PrintWindow`
  (`analysis/shot_window.ps1`) and compare title, log pane, counters row, toolbar buttons and
  status bar. A GUI that opens but shows no log rows is not equivalent.

## 4. Derive constants from protocol data, not from observed bytes

Three values we initially hard-coded were later derived:

- Goods-list blobs are base64 fields in `/Goods/ListGoodsInfos`
  (`CustomizedGoodsAttribute`, `CustomizedItemAttribute1/2`); `Base64Decode` reproduces the exact
  byte fields the original writes.
- The first u32 of the cmd8 reply is the **body length excluding that field itself** (lobby 261,
  world 281, empty 10), not a state enum.
- Per-goods counters in the goods-def logs come from `GameItemKey` (1..999 lobby, 999 game-cash),
  not from item numbers.

When you see a mysterious constant, look for a base64/XML field or a length computed after
composition before accepting it as a dataset artifact.

## 5. Differential testing must cover *all* commands and both payload directions

We ran three suites (`compare_oracle.py`, `compare_remaining.py`, `compare_world.py`) after every
fresh daemon restart, but they did not cover enough. Additional probes that found real gaps:

- **All-zero GUID on every command**: the original rejects it in the packet handler with a
  failure ack (reason 7, manage reason 4); our handlers initially went to NP or DB and timed out.
  `analysis/probe_zero_guid.py` now checks 17 paths, 0 DIFF.
- **"No-reply" commands** (lobby 32/33, world 10/25/28/31): probe with several payload shapes and
  lengths. World cmd10 actually replies `cmd12 [first-qword,7]` once the payload is >=33 bytes;
  calling it a no-op would have been wrong.
- **Empty/unknown account**: a random non-zero GUID produces an empty-list reply whose length
  field evaluates to 10; it is not the same as an invalid GUID.
- **Async gateway states**: fixture data in the gateway DB is not visible until that app is
  reloaded; status 3/4/2 and "no purchase" branches were verified only after a targeted
  `PurchaseSrv` reload.

Keep a probe script per discovered class in `analysis/`; they are cheap to rerun.

## 6. Log fidelity is doable and worth doing

If the objective says "runs the same way", log output counts. The original writes a full
`DaemonConfig::PrintAll` dump, per-item goods-def lines, NP request/reply and periodic job lines.
To match:

- Mirror the original's file/line/level/tag exactly, including small quirks (e.g. one message has
  no comma after the tag).
- The log media is narrow: one Unicode codepoint becomes one `?`; full-width punctuation folds to
  ASCII first. Replacing every UTF-8 *byte* with `?` gives the wrong number of characters.
- Rejecting the fixture "frozen thread" warnings required refreshing
  `ThreadManager::ownerTime/cachedTime` on every daemon timer tick.
- Compare normalized logs (strip timestamps, PIDs, pointers, ports, timing values) with a multiset
  diff; we reached 5838 of 5851 normalized lines common. The remainder is inherently
  nondeterministic (pointer values, timer ticks).

`analysis/idle_log_capture.ps1`, `categorize_idle.py` and `diff_idle_normalized.py` reproduce it.

## 7. The console DLL ABI is a trap

This cost the most time. Production's `bsConsole_sv64.dll` (121856 bytes) is **not** drop-in
compatible with the newer sibling rebuild (182784 bytes). Symptoms: window opens, first log line
is written to the log file, then `0xC0000005` with fault address 0.

What to check:

- Compare DLL hashes/sizes; do not assume "same filename, same ABI".
- Production's DLL calls methods on the **log record's object vtable** at +0x00 (enabled), +0x10
  (level) and +0x18 (format the whole line). Our object vtable had null/reserved slots at those
  offsets. Fill them with real thunks; +0x18 must emit `timestamp,file,line,(level),message`, not
  just the clock.
- A temporary vectored exception handler that logs `ExceptionAddress`, `Rsp` and a few stack words
  pinpointed the faulting DLL function via the return address. Remove the instrumentation before
  committing.
- Ghidra headless + a PE vtable reader (`analysis/run_ghidra_dll.ps1`, formerly
  `analysis/read_vtable.py`) is enough to recover these slots; you do not need a live debugger.

## 8. Process and database safety

- A broad filter like `ProcessName -eq 'console_launch'` can also kill the wrapper of the
  production daemon. Use path-based filters and explicitly exclude `D:\service*`. The user may be
  fine with restarts, but be explicit about it.
- Restoring attached `.mdf/.ldf` files with `git checkout` while SQL Server has them attached
  caused `RECOVERY_PENDING`. Fix by `DROP DATABASE` + `RESTORE` from the backup, and don't revert
  those binaries mid-experiment.
- SQL Server Driver 17 does not reliably surface ODBC `RETURN` values from RAISERROR paths. Emit the
  oracle-verified reason codes directly; wire behavior stays byte-exact.

## 9. Finish with reproducible evidence

Before saying "done", show command output for:

- build `BUILD OK`, `--version` banner, three listen ports;
- `release==build` hash and `config==production` hash (and DLL hash if you replaced one);
- every differential suite PASS and every extra probe 0 DIFF;
- clean git tree with local HEAD == `origin/master`.

Keep `analysis/verification.md` current — it is the map of what was actually proven versus what is
still a known limitation. Commit and push after each verified increment; a clean tree makes the
next investigation much easier.
