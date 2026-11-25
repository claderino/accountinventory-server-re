# SUMMARY — AccountInventoryDaemon reconstruction

## Reverse engineering method

1. Read-only, in-memory dump of the running packed binary via `analysis/dump_process.py`
   (`OpenProcess(PROCESS_QUERY_INFORMATION|VM_READ)` + `ReadProcessMemory`, no writes/suspension).
   The dump reconstructed a loadable PE (`analysis/mem/AccountInventoryDaemon.exe.mem.exe`)
   with `.text`/`.rdata` restored from committed pages.
2. Ghidra 12.2 headless import + full decompile (`analysis/run_ghidra.ps1`) -> 1534 functions
   in `analysis/decomp/unknown.c`.
3. String/RVA extraction (`analysis/extract_strings_rva.py`) and function-scoped string mining
   (`analysis/fnstrings.py`) recovered config keys, log formats, SQL and packet format strings.
4. Function-pointer tables in `.data` were dumped directly from the image:
   * manage table `0xCC1D0`: commands **1, 4**
   * lobby table `0xCC210`: commands **1, 4, 7, 10, 13, 16, 19, 22, 25, 28, 32, 33**
   * world/gamed table `0xCC320`: commands **1, 4, 7, 10, 13, 16, 19, 22, 25, 28, 31, 34**
5. Hello handlers were read from the decompilation to recover exact versions and ack formats.

## Implementation

Reused the proven `BloodLib` framework from the sibling `achievement` reconstruction
(IOCP networking, packet field engine, logging, config, timers, crash handling) and wrote a
new application layer: `Config.cpp`, `Db.cpp`, `AccountInventoryDaemon.cpp`, `Handlers.cpp`,
`main.cpp`. One framework fix was needed: `Network::InitWorkers` must not re-initialise an
already-sized `ThreadManager` (the daemon base pre-sizes it to 128).

## Verification

* `build.bat` compiles cleanly with MSVC 14.44 / SDK 10.0.26100 (`/O2 /MT /std:c++17`).
* `--version` output is byte-for-byte the expected original text (0.0.210.314).
* Test instance on 22700/22701/32700 starts, connects to `GameWarehouseDB` and validates
  world id 10070 through `usp_CheckWorldId`; log lines use the original file/line/tag strings.
* `tests/e2e/e2e_client.py`: 6/6 checks pass (valid + invalid hello on all three ports).
* `release\bin\AccountInventoryDaemon.exe` is byte-identical to the build output.

## Safety

`D:\service\` was treated as read-only throughout. The production daemon (PID 23844) was never
stopped, restarted or reconfigured. Tests used offset ports only and a local ODBC connection.

## Known limitation

Goods acquisition/consumption, warehouse search, NP gateway and instant-payment transactions
are served by the recovered command dispatcher with well-formed acks, but the full job/STS/DB
business logic of those commands was not reproduced in this pass. See `analysis/verification.md`.
