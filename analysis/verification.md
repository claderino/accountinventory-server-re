# Verification

## Environment
- MSVC 14.44.35207, Windows SDK 10.0.26100.0, `build.bat` (`cl /O2 /MT /std:c++17`).
- Test instance: `C:\serverside_re\accountinventory\run\test`, ports **22700 / 22701 / 32700**.
- DB: `localhost\SQLEXPRESS`, `GameWarehouseDB`, ODBC Driver 17, trusted connection.
- Production install `D:\service\...` was read-only; production PID 23844 untouched.

## Test table

| # | Check | Command / evidence | Result |
|---|---|---|---|
| 1 | Build | `build.bat` -> `BUILD OK: ...\AccountInventoryDaemon.exe` | PASS |
| 2 | Version | `--version` prints `AccountInventoryDaemon service 0.0.210.314 / PlatformBinary server 0.0.210.359` and packet versions `8/7/1` | PASS |
| 3 | Startup | log: config PrintAll, `Starting ... version: 0.0.210.314`, `Network Configuration`, `open ... port ok` | PASS |
| 4 | DB | `usp_CheckWorldId(10070)` succeeded (no `failed to check world id` log, daemon continued) | PASS |
| 5 | Ports | `netstat -ano`: LISTENING on 22700 (pid 15272/…), 22701, 32700 | PASS |
| 6 | Manage hello bad version | frame cmd1 ver=99 -> `[len=6][cmd=3][reason=2]` | PASS |
| 7 | Lobby hello bad version | frame cmd1 ver=99 -> `[len=6][cmd=3][reason=2]` | PASS |
| 8 | World hello bad version | frame cmd1 ver=99 -> `[len=6][cmd=3][reason=2]` | PASS |
| 9 | Manage hello | frame cmd1 ver=1 -> `[len=4][cmd=2]` | PASS |
| 10 | Lobby hello | frame cmd1 ver=7,wid=7801,port=20100 -> `[len=4][cmd=2]` | PASS |
| 11 | World hello | frame cmd1 ver=8,wid=7801,port=20100 -> `[len=4][cmd=2]` | PASS |
| 12 | Release binary | SHA256 of release exe == SHA256 of build exe | PASS |
| 13 | Companion DLLs | byte-identical copies of original `XmlReader_sv64.dll`, `bsConsole_sv64.dll`, `dbghelp.dll`, `GeoData_sv64.dll`, `StsMsgSrv64.dll` | PASS |

`python tests\e2e\e2e_client.py` output:

```
manage bad-version     PASS  len=6 cmd=3 body=0200
lobby  bad-version     PASS  len=6 cmd=3 body=0200
world  bad-version     PASS  len=6 cmd=3 body=0200
manage hello           PASS  len=4 cmd=2 body=
lobby  hello           PASS  len=4 cmd=2 body=
world  hello           PASS  len=4 cmd=2 body=
E2E RESULT: PASS
```

## Known issues / scope
- Non-hello commands are recognised from the recovered ID tables and answered with a valid
  ack, but the original's asynchronous job/NP/DB business logic (goods list/take, warehouse
  search/acquire/consume, instant payment) is not reproduced in this pass.
- The NP gateway (`StsMsgSrv64.dll`) is shipped with the release but not driven by our code.
- Only `usp_CheckWorldId` is called at startup; the remaining 42 stored procedures in
  `GameWarehouseDB` are outside the implemented request paths.
