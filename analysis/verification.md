# Verification

## Environment
- MSVC 14.44.35207, Windows SDK 10.0.26100.0, `build.bat` (`cl /O2 /MT /std:c++17`).
- Test instance: `run\test`, ports **22700 / 22701 / 32700**.
- **Oracle**: a copy of the unmodified production binary run from `analysis\oracle` on private
  ports **23700 / 23701 / 33700** (`np-enabled=no`, `enable-instant-payment=no`). The live
  production process (PID 23844, ports 21700/21701/31700) was never touched.
- DB: `localhost\SQLEXPRESS`, `GameWarehouseDB`, ODBC Driver 17, trusted connection.

## Test table

| # | Check | Evidence | Result |
|---|---|---|---|
| 1 | Build | `build.bat` -> `BUILD OK` | PASS |
| 2 | Version | `--version` = `0.0.210.314`, packet versions `8/7/1` | PASS |
| 3 | Startup | log: config PrintAll, `Starting ... 0.0.210.314`, three `open ... port ok` | PASS |
| 4 | DB | `usp_CheckWorldId(10070)` succeeds; daemon continues | PASS |
| 5 | Listen ports | 22700 / 22701 / 32700 LISTENING under our test PID | PASS |
| 6 | Hello, all 3 ports (valid + invalid version) | `tests/e2e/e2e_client.py`: 6/6 | PASS |
| 7 | **Oracle differential: cmd4 get-goods-count** | `[6] 55443322110000000500` both | PASS |
| 8 | **Oracle differential: cmd7 list-goods** | `[9] 55443322110000000500` both | PASS |
| 9 | **Oracle differential: cmd13 service-item-list** | `[14] 0e0000005544332211000000640000000000` both | PASS |
| 10 | **Oracle differential: cmd16 item-info** | `[17] 5544...396001` both | PASS |
| 11 | **Oracle differential: cmd19 try-consume** | `[21] ab080000000056270700` both | PASS |
| 12 | **Oracle differential: cmd25 cancel-consume** | `[27] ab080000000056270700` both | PASS |
| 13 | Release binary | SHA256 of release exe == SHA256 of build exe | PASS |
| 14 | Companion DLLs + config | byte-identical to original install | PASS |

`python tests\e2e\compare_oracle.py` output:

```
cmd4 get-goods-count   MATCH  oracle=[(6, '55443322110000000500')]  ours=[(6, '55443322110000000500')]
cmd7 list-goods        MATCH  oracle=[(9, '55443322110000000500')]  ours=[(9, '55443322110000000500')]
cmd13 service-list     MATCH  oracle=[(14, '0e0000005544332211000000640000000000')]  ours=[(14, '0e0000005544332211000000640000000000')]
cmd16 item-info        MATCH  oracle=[(17, '55443322110000000000000000000000ab08000000005627b2030000000000006039010001')]  ours=[(17, '...')]
cmd19 try-consume      MATCH  oracle=[(21, 'ab080000000056270700')]  ours=[(21, 'ab080000000056270700')]
cmd25 cancel-consume   MATCH  oracle=[(27, 'ab080000000056270700')]  ours=[(27, 'ab080000000056270700')]
COMPARE RESULT: PASS
```

## DB business logic now implemented

All 43 stored procedures of `GameWarehouseDB` were dumped (signatures:
`analysis/db/proc_signatures.txt`, definitions `analysis/db/procs/`). The C++ facade
`AccountInventoryDB.{h,cpp}` implements typed calls and result parsing for the goods
registration, warehouse listing/search, item acquisition, item consumption, lobby
service-item and instant-payment procedures, with per-binding ODBC indicator storage.

Commands 4/7/13/16/19/22/25/28 on the lobby port and 4/7 on the world port now call the real
DB and build their replies from the live rows; their wire bytes were validated against the
original binary as an oracle (table above).

## NP gateway implementation (second pass)

Reversed the NP transport by running a private copy of the original with `np-enabled=yes`
against the live gateway through a logging TCP proxy (`tests/e2e/np_proxy.py`). The gateway
speaks HTTP-like STS/1.0 text frames (see `analysis/NP_PROTOCOL.md`). Implemented
`src/AccountInventory/NpClient.cpp`: connect / register / GetGoodsCount / List, sequence
numbers, XML parsing, reconnect-on-demand, wired into the daemon (`onStart`) and handlers.

Differential test with the NP-enabled oracle (both daemons talking to the same live gateway):

```
cmd4 get-goods-count   MATCH  cmd5 [554433221100000001000000]
cmd7 list-goods        MATCH  cmd8 [265-byte goods-list frame]
cmd10 take-goods       MATCH  cmd12 [tid,15]
cmd13 service-list     MATCH  cmd14
cmd16 item-info        MATCH  cmd17
cmd19 try-consume      MATCH  cmd21
cmd22 finish-consume   MATCH  cmd24
cmd25 cancel-consume   MATCH  cmd27
cmd28 consume          MATCH  cmd30
manage cmd4 restore    MATCH  cmd6 [tid,4]
COMPARE RESULT: PASS (11/11 probed paths)

World channel (compare_world.py, NP-enabled oracle) — all MATCH:
  cmd4 cmd5 / cmd7 cmd8 (281 variant + extra per-item field) / cmd13 cmd15 /
  cmd16 cmd17 / cmd19 cmd21 / cmd22 cmd24 / cmd34 cmd36.
  cmd10/25/28/31: oracle sends no reply; our handlers mirror that.
```

## Remaining limitations
- The instant-payment job state machine beyond the restore-request command (cmd4) is represented
  by the recovered dispatcher and DB facade but not exercised end-to-end; the test database has
  no active instant-payment service to drive a full purchase/restore cycle.
- Lobby cmd32 (retry-take) and cmd33 (logout) and world cmd10/25/28/31 produce no reply on the
  oracle with the recovered request formats; our handlers mirror that behaviour.
- The oracle's NP socket goes idle between long runs and it falls back to its NP-unavailable
  replies; each differential suite passes when run against a freshly started oracle.
- ODBC `RETURN` values from RAISERROR paths are not reliably surfaced by Driver 17; the
  oracle-verified reason codes are used on those paths instead.
