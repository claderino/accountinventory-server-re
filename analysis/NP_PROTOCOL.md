# Next Platform (NP) gateway protocol — recovered

Recovered by running a **private copy of the original binary** with `np-enabled=yes` against the
live gateway (`SpawnSrv` on `127.0.0.1:6601`) through a logging TCP proxy
(`tests/e2e/np_proxy.py`). The production install was never modified.

## Transport

Plain TCP, HTTP-like STS/1.0 text protocol:

```
POST <path> STS/1.0\r\n
l:<xml body length>\r\n
s:<sequence>\r\n
\r\n
<xml body>
```

Response:

```
STS/1.0 200 OK\r\n
s:<sequence>R\r\n
l:<body length>\r\n
\r\n
<xml reply>
```

## Handshake (startup)

1. `POST /Sts/Connect STS/1.0` — body:
   `<Connect><ConnType>401</ConnType><Address>127.0.0.1</Address><ProductType>0</ProductType>
   <AppIndex>1</AppIndex><Epoch>...</Epoch><Program>1820</Program><Build>4000</Build>
   <Process>...</Process></Connect>`
2. `POST /Gate/RegisterService STS/1.0` — `<Message><Protocol>Srv2GameWarehouse</Protocol></Message>`.
   The gateway answers `400 Success` with an `<Error .../>` body; this is non-fatal.
3. Goods catalogue load: `POST /Goods/ListGoodsInfos` (21 pages of 50), then
   `/Goods/ListCategoryIds`, `/Goods/GetGameGoodsByCategoryId`, `/Goods/GetRevision`.

## Client-facing commands that use NP

| lobby cmd | NP request | reply cmd | wire body |
|---|---|---|---|
| 4 get-goods-count | `POST /Warehouse.bnsgrnTH/GetGoodsCount` with `<UserId>GUID</UserId><IncludeEmptyGameServerId>true</...><IncludeEmptyCharacterId>true</...>` | 5 | `hqi`: (tid u64, GoodsCount u32) |
| 7 list-goods | `POST /Warehouse.bnsgrnTH/List` with `<UserId>` + `<MaxGoodsCount>100</MaxGoodsCount>` + include flags | 8 | `hiqCqiQQqqssissbbCiiiibb;;` |

The gateway returns e.g. `<Reply><UserId>...</UserId><GoodsCount>1</GoodsCount></Reply>` for
GetGoodsCount and a `<GoodsList type="array">` with `<Goods>`/item entries for List.

The original binary's `StsMsgSrv64.dll` exports only `CreateStsMsg` and `CleanUpStsMsg`; it
serializes the message objects into exactly the frames above. Our reconstruction talks the same
protocol directly over Winsock (`src/AccountInventory/NpClient.cpp`), with reconnect-on-demand
since the gateway closes idle sockets (the original sends periodic `/Sts/Ping`).
