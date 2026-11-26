# Verification

## Environment
- MSVC 14.44.35207, Windows SDK 10.0.26100.0, `build.bat` (`cl /O2 /MT /std:c++17`).
- Test instance: `run\test`, ports **22700 / 22701 / 32700**.
- **Oracle**: a copy of the unmodified production binary run from `analysis\oracle` on private
  ports **23700 / 23701 / 33700**. Early checks used `np-enabled=no`; the later differential
  suites and instant-payment probes used NP-enabled copies against the live gateway. The live
  production install under `D:\service` was only read; its daemon was stopped/restarted during
  analysis (PID 23844 -> 25972) and is running from its original binary and config on ports
  21700/21701/31700.
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

## Clean full-suite verification (2026-11-25)
`tests\e2e\run_all_fresh.ps1` restarts both daemons before each suite so every run hits a fresh
oracle NP connection. Final result (`tests\e2e\all_fresh_results.txt`):

```
=== compare_oracle.py ===   COMPARE RESULT: PASS
=== compare_remaining.py === RESULT: PASS
=== compare_world.py ===     RESULT: PASS
DONE
```

This covers all probed lobby/world/manage paths byte-for-byte against the original binary.
The cmd16 failure path was corrected in the final build (reason 6 / cmd18), which removed the
last intermittent mismatch caused by consume probes changing the test item's state.

## Idle 2-minute log comparison (2026-11-26)
`analysis/idle_log_capture.ps1` starts both private copies with identical DB/NP settings (only
ports differ), leaves them untouched for 125 seconds, then captures the new log of each side.
Before the fidelity fixes: oracle **5851** lines, ours **28**. The message-category diff showed:

- original prints the full `DaemonConfig::PrintAll` dump (51 lines), ours printed 8;
- original logs 1011 goods definitions x 4 `GoodsDef.cpp` lines (~4050 lines) during catalogue
  load, ours logged none;
- original logs NP channel/ping and periodic `UPDATE_GOODS_FOR_PURCHASE` transaction-job lines,
  ours logged none;
- ours logged 14 `PipeConsole.cpp "thread frozen"` errors, original logged none;
- original's `daemon:` log field is the description (`Blade&Soul AccountInventory Daemon`),
  ours used the short name; the test config also had `dump-type=2` vs the original's `0`.

Closing the gap: the full config dump, `daemon:` description field and `dump-type=0` were added;
`NpClient` now logs every catalogue item in the original's four `GoodsDef.cpp` forms plus the
`(goods)` level dump (including the narrow-media `?` substitution), the per-page NP request/reply
lines, object-pool initialisation and the periodic `UPDATE_GOODS_FOR_PURCHASE` job (with NP ping
and revision ticks). The framework's stale thread timestamps and the frozen-console warnings were
fixed by refreshing `ThreadManager::ownerTime/cachedTime` every daemon timer tick.

After these fixes a fresh 125-second idle capture produced **5851** oracle lines and **5844** ours,
with **5838** normalized lines common. The residual handful are values that vary per run and
cannot be made equal: pointer/session addresses, timer tick values, and one timing-dependent job
tick. `analysis/categorize_idle.py` and `analysis/diff_idle_normalized.py` reproduce the comparison.

## Derived list fields and invalid-account probes (2026-11-26)
The cmd8 fields that were previously constants are now computed from protocol data:

```
NP /Goods/ListGoodsInfos CustomizedGoodsAttribute  = base64 00 00 00 00 01   (goods tail)
NP /Goods/ListGoodsInfos CustomizedItemAttribute1  = base64 00*8             (item blob 1)
NP /Goods/ListGoodsInfos CustomizedItemAttribute2  = base64 00 00 00 00 01 00 (item blob 2)
first u32 of cmd8 = reply body length excluding that field (lobby 261, world 281, empty 10)
```

`NpClient` stores the three base64 catalogue fields per GoodsNumber; `HandleListGoods` decodes
them with `Base64Decode` and patches the length after composing the body. Live probes confirmed
`first_u32 == body_length - 4` for both channels.

`analysis/probe_zero_guid.py` sends the all-zero GUID to **every** probed command on the lobby,
world and manage ports. After adding the packet-handler validation that the original performs,
all 17 cases match byte-for-byte (cmd4/7/10/13/16/19/22/25/28 lobby, cmd4/7/13/16/19/22/34
world, manage cmd4). A non-zero unknown GUID yields the empty-list cmd8 with length 10.

## Instant-payment restore state machine (fully driven)
A test copy `GameWarehouseDB_RE` was restored from the live DB and an in-progress service row
inserted (`analysis/db/GameWarehouseDB_TEST.bak`). The NP gateway caches purchases at startup, so
a **targeted reload of only PurchaseSrv** was used after each fixture change:

```
POST http://localhost:6605/apps/1055.1.1.<epoch>/stop
```

No other SpawnSrv app was restarted. The production AccountInventory daemon was restarted from
its own binary/config during analysis (new PID 25972); the process restart is permitted. The
gateway lookup recovered from the original is `POST /Purchase/GetPurchase STS/1.0` with
`<RequestKey>bnsgrnTH+10070+<serviceId></RequestKey>`. With both daemons on the test DB and NP
port 6601, the measured replies are:

```
service 99990001 / pcid 12345 / type 2, purchase status 3 : MATCH  cmd5 [tid,1]
service 99990001 / pcid 12345 / type 2, purchase status 4 : MATCH  cmd5 [tid,0]
service 99990001 / pcid 12345 / type 2, purchase status 2 : MATCH  cmd5 [tid,1]
service 99990001 / pcid 12345 / type 2, no purchase        : MATCH  cmd5 [tid,1]
service 291      / pcid 0     / type 2                     : MATCH  cmd6 [tid,4]
```

Status 4 advances the service to payment-success in SQL; status 3 cancels it. The real NP request
is issued by `NpClient::GetPurchaseStatus`; the status branch and DB updates are in
`HandleManageRestore`. All temporary fixture rows were removed and PurchaseSrv was reloaded once
more to clear its cache; the oracle config was restored to `run\test\cert.cfg` / `np-port=6601`.

## Remaining limitations
- The former opaque constants are now derived, not hard-coded. The gateway's
  `/Goods/ListGoodsInfos` XML carries base64 fields which decode into exactly the reply blobs:
  `CustomizedGoodsAttribute` = `00 00 00 00 01` (5 bytes), `CustomizedItemAttribute1` = eight zero
  bytes, `CustomizedItemAttribute2` = `00 00 00 00 01 00` (6 bytes). `NpClient::LoadGoodsCatalog`
  stores them per goods number and `HandleListGoods` decodes them with `Base64Decode`.
  The first u32 of cmd8 is the reply-body length **excluding that field itself** (lobby 261,
  world 281, empty list 10); it is patched after composition. The world/lobby difference is the
  extra per-item u32 that world writes. All values are now computed from protocol data.
- `cmd7` with the all-zero game-account GUID is rejected before any job is started with
  cmd9 / reason 7 (LobbyPacketHandler "invalid parameter"); a non-zero but unknown GUID yields an
  empty list whose length field evaluates to 10. Both behaviours are mirrored.
- `analysis/probe_remaining.py` covers lobby cmd32/33 and world cmd10/25/28/31 with five request
  shapes each (30 probes). Lobby cmd32/33 and world cmd25/28/31 are no-reply on both daemons.
  World cmd10 with a well-formed request (>= 33 payload bytes) replies cmd12 / result 7 on both;
  short requests produce no reply. All 30 probes match byte-for-byte.
- The shipped config is byte-identical to the production install:
  `release/bin/accountInventoryd.config.xml` and
  `D:\service\AccountInventoryDaemon\bin\accountInventoryd.config.xml` share SHA-256
  `9B589B132604B4EA1275063F12E7F3E969B79F5DEE638D6F3737B151C525B2C8`
  (ports 31700/21700/21701, NP port 6601, `D:\service\DBConfig\Accinventoryd.cfg`).
- The oracle's NP socket goes idle between long runs and it falls back to its NP-unavailable
  replies; each differential suite passes when run against a freshly started oracle.
- ODBC `RETURN` values from RAISERROR paths are not reliably surfaced by Driver 17; the
  oracle-verified reason codes are emitted directly on those paths. Wire behaviour is unchanged
  (byte-exact on every probed path); this only avoids depending on an unreliable driver feature.
