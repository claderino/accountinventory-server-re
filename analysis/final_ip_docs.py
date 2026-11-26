import io
p = r'C:\serverside_re\accountinventory\analysis\verification.md'
s = io.open(p, encoding='utf-8').read()
s = s.replace('''## Remaining limitations
- The instant-payment job state machine beyond the restore-request command (cmd4) is represented
  by the recovered dispatcher and DB facade but not exercised end-to-end; the test database has
  no active instant-payment service to drive a full purchase/restore cycle.''', '''## Instant-payment restore state machine (DB-driven)
A test copy `GameWarehouseDB_RE` was restored from the live DB and an in-progress service row
inserted (analysis/db/GameWarehouseDB_TEST.bak). Both daemons were pointed at it and the manage
cmd4 restore request was compared for three parameter sets:

```
service 99990001 / pcid 12345 / type 2 : MATCH  cmd5 [tid,1]
service 123      / pcid 12345 / type 2 : MATCH  cmd5 [tid,1]
service 291      / pcid 0     / type 2 : MATCH  cmd6 [tid,4]
```

The original's SETUP validates request parameters (pc-id/service-type), then starts the async
GET_PURCHASE job; the immediate client reply is cmd5/result=1 for valid parameters and cmd6/
reason=4 otherwise. Our handler mirrors this (see `HandleManageRestore`).

## Remaining limitations
- The asynchronous GET_PURCHASE / PURCHASE / second-password states of the instant-payment job
  are implemented in the DB facade but are not driven by a live NP purchase; the gateway has no
  matching purchase record for the test service.''')
io.open(p, 'w', encoding='utf-8').write(s)
print('verification updated')
