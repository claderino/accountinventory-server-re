# AccountInventoryDaemon reverse-engineering reconstruction

Clean C++17 reconstruction of NCSoft Blade & Soul `AccountInventoryDaemon.exe` 0.0.210.314
(Garena branch, x64, originally packed `.bns0/.bns1`), reverse engineered from the live
production install at `D:\service\AccountInventoryDaemon\bin\` (read-only; never modified).

## What the original does

A three-port IOCP/ODBC Windows service/console daemon for the account "warehouse" (cash-shop
goods) system:

| port id | config key | production port | purpose |
|---|---|---|---|
| 2 | `lobby-listen-port` | 21700 | lobby / client-facing goods & inventory requests |
| 3 | `world-listen-port` | 21701 | game-server ("gamed") registration & goods requests |
| 4 | `manage-listen-port` | 31700 | management / instant-payment restore |

At startup it parses `accountinventoryd.config.xml`, validates `service-world-id` against
`GameWarehouseDB.dbo.usp_CheckWorldId`, opens the three ports, and (in the full product)
bridges to the Next Platform (NP) gateway via `StsMsgSrv64.dll`.

## Layout

```
accountinventory/
├── AccountInventoryDaemon/          the reconstructed codebase
│   ├── build.bat                    MSVC x64 build -> build\AccountInventoryDaemon.exe
│   ├── config/                      placeholder config + gamed-list + manifest
│   └── src/
│       ├── BloodLib/                framework port: IOCP Network/Port/Session/Listener,
│       │                            packet field engine, Log, Config, Timer, CrashHandler
│       ├── AccountInventory/        app: config reader, ODBC Db, daemon lifecycle, wire handlers
│       └── DataReader/              schema-driven XML record reader
├── analysis/                        RE artifacts: read-only memory dump of the packed image,
│   │                                Ghidra decomp (decomp/unknown.c), string tables with RVAs,
│   │                                command-handler tables, tools, build logs
├── run/test/                        private test instance (ports 22700/22701/32700)
├── tests/e2e/e2e_client.py          wire-level e2e (6 checks)
└── release/                         drop-in release folder (see below)
```

## Build & test

```powershell
cd C:\serverside_re\accountinventory\AccountInventoryDaemon
.\build.bat
# start private test instance (never touches production ports):
Start-Process ..\run\test\console_launch.exe -ArgumentList `
  '"...\AccountInventoryDaemon\build\AccountInventoryDaemon.exe" --text --conf=...\run\test' -WindowStyle Hidden
python ..\tests\e2e\e2e_client.py
```

Test DB: `GameWarehouseDB` on `localhost\SQLEXPRESS` (read-only usage: `usp_CheckWorldId`).
`--version` output matches the original exactly (0.0.210.314, packet versions 8/7/1).

## Release

`release\bin\` contains the rebuilt `AccountInventoryDaemon.exe`, byte-identical copies of the
original companion DLLs (`XmlReader_sv64.dll`, `bsConsole_sv64.dll`, `dbghelp.dll`,
`GeoData_sv64.dll`, `StsMsgSrv64.dll`), a byte-identical copy of the original
`accountInventoryd.config.xml`, the manifest, `config\gamed-list.config.xml`, a sample cert,
`start.bat` and a console launcher. The config uses absolute production paths and production
ports, so it self-locates like the original; deploy only into the real install location.

## Fidelity / scope

Implemented and verified: CLI/run-mode behaviour, config parsing of every shipped key,
log format and original file/line/tag strings, ODBC connection + `usp_CheckWorldId` world-id
validation, all three listen ports with the recovered command tables, the exact hello
handshake (manage version 1, lobby 7, world 8), and a typed DB facade over all 43 stored
procedures of `GameWarehouseDB`.

Lobby commands 4/7/13/16/19/22/25/28 and world commands 4/7 now query the real database and
compose real replies. Their response bytes were validated **against a private copy of the
original binary as an oracle** — 6/6 command probes match byte-for-byte
(`python tests\e2e\compare_oracle.py`, see `analysis/verification.md`).

The Next Platform gateway (`StsMsgSrv64.dll`) is shipped but not driven; commands that need it
reply with the same reason codes the original emits when NP is unavailable. The companion
DLLs are the original binaries, byte for byte.
