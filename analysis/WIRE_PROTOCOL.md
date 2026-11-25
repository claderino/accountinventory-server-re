# Wire protocol (recovered from `AccountInventoryDaemon.exe` 0.0.210.314)

## Framing

`[u16 length including these 4 bytes][u16 cmd][fields...]`, little-endian, max 4000 bytes.
The frame command word is consumed by the first `h` of the packet field format.

Field codes (BloodLib packet engine): `c/Q`=u8, `h/E/U/C/u`=u16, `i/d`=u32, `f`=float,
`q`=u64, `o`=16-byte UUID, `s`=u16 count + UTF-16, `b/K`=u16 count + bytes,
`C ... ;` = repeat block, `P` = property.

## Command tables (recovered from `.data` function pointers)

| port | table RVA | command ids |
|---|---|---|
| manage (31700) | 0xCC1D0 | 1, 4 |
| lobby (21700) | 0xCC210 | 1, 4, 7, 10, 13, 16, 19, 22, 25, 28, 32, 33 |
| world (21701) | 0xCC320 | 1, 4, 7, 10, 13, 16, 19, 22, 25, 28, 31, 34 |

## Hello (cmd 1) — verified

| port | request format | expected version | success reply | failure reply |
|---|---|---|---|---|
| manage | `"hh"` (cmd, version) | 1 | `"h"` cmd=2 | `"hh"` (3, reason) |
| lobby | `"hhhh"` (cmd, version, wid, port) | 7 | `"h"` cmd=2 | `"hh"` (3, reason) |
| world | `"hhhh"` (cmd, version, wid, port) | 8 | `"h"` cmd=2 | `"hh"` (3, reason) |

Other format strings observed in the original handlers (for future work):
`"hqqoqi"` (manage restore), `"hqoQq"` / `"hiqiCqqqiQ;"` / `"hqqqqiQ"` / `"hqqqo"` /
`"hqqqoE"` (lobby service-item / consumption), `"hqqqohqi"` / `"hqiqi"` / `"hqiibohqsi"`
(game goods/warehouse), `"hiqCqiQQqqssissbbCiiiibb;;"` and variants (goods lists).
