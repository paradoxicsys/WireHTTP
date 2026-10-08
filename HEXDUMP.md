# Annotated hexdump: one complete WireHTTP/1 request and response

One `GET /index.html` over one TCP connection, captured with the client's `-v` option. Every byte is accounted for below; field names refer to `SPEC.md`.

```
$ ./wserve ./www 9000 &
$ ./wcurl -v -H 'x-course: na' localhost:9000/index.html
```

`-H 'x-course: na'` adds one header that is **not** in the static table, so the dump shows both encodings: indexed names (1 byte) and a literal, length-prefixed name. `www/index.html` is the 15-byte file `<h1>hello</h1>\n`.

## Raw `-v` output (stderr)

`->` is a frame wcurl sent and `<-` is a frame it received. Each frame is shown in full: the 8-byte header, then the payload.

```
* connected to localhost:9000 (one connection for every request)
-> HEADERS len=67 flags=0x01(END_STREAM) stream=1
   0000  00 43 01 01 00 00 00 01  01 00 03 47 45 54 02 00  |.C.........GET..|
   0010  0b 2f 69 6e 64 65 78 2e  68 74 6d 6c 03 00 0e 6c  |./index.html...l|
   0020  6f 63 61 6c 68 6f 73 74  3a 39 30 30 30 04 00 07  |ocalhost:9000...|
   0030  77 63 75 72 6c 2f 31 05  00 03 2a 2f 2a 00 08 78  |wcurl/1...*/*..x|
   0040  2d 63 6f 75 72 73 65 00  02 6e 61                 |-course..na|
   [1] :method: GET
   [2] :path: /index.html
   [3] :authority: localhost:9000
   [4] user-agent: wcurl/1
   [5] accept: */*
   [lit] x-course: na
<- HEADERS len=66 flags=0x00 stream=1
   0000  00 42 01 00 00 00 00 01  06 00 03 32 30 30 07 00  |.B.........200..|
   0010  09 74 65 78 74 2f 68 74  6d 6c 08 00 02 31 35 09  |.text/html...15.|
   0020  00 08 77 73 65 72 76 65  2f 31 0a 00 1d 57 65 64  |..wserve/1...Wed|
   0030  2c 20 30 37 20 4f 63 74  20 32 30 32 36 20 30 38  |, 07 Oct 2026 08|
   0040  3a 33 39 3a 32 30 20 47  4d 54                    |:39:20 GMT|
   [6] :status: 200
   [7] content-type: text/html
   [8] content-length: 15
   [9] server: wserve/1
   [10] date: Wed, 07 Oct 2026 08:39:20 GMT
<- DATA len=15 flags=0x01(END_STREAM) stream=1
   0000  00 0f 00 01 00 00 00 01  3c 68 31 3e 68 65 6c 6c  |........<h1>hell|
   0010  6f 3c 2f 68 31 3e 0a                              |o</h1>.|
* stream 1 done: status 200, 15 body bytes
* connection closed
```

The body `<h1>hello</h1>` went to stdout and wcurl exited with 0. Offsets below count from the first byte of each frame.

### Frame 1: HEADERS (wcurl → wserve), 75 bytes = 8 header + 67 payload

| Offset | Bytes | Field | Meaning |
|---|---|---|---|
| 0000 | `00 43` | Length | 0x0043 = 67 bytes of payload (header not counted) |
| 0002 | `01` | Type | 0x01 HEADERS |
| 0003 | `01` | Flags | 0x01 END_STREAM: last frame of this request |
| 0004 | `00 00 00 01` | Stream ID | 1: the first request on this connection |
| 0008 | `01` | Index | 1 → `:method` (static table) |
| 0009 | `00 03` | ValueLen | 3 |
| 000b | `47 45 54` | Value | "GET" |
| 000e | `02` | Index | 2 → `:path` (static table) |
| 000f | `00 0b` | ValueLen | 11 |
| 0011 | `2f 69 6e 64 65 78 2e 68 74 6d 6c` | Value | "/index.html" |
| 001c | `03` | Index | 3 → `:authority` (static table) |
| 001d | `00 0e` | ValueLen | 14 |
| 001f | `6c 6f 63 61 6c 68 6f 73 74 3a 39 30 30 30` | Value | "localhost:9000" |
| 002d | `04` | Index | 4 → `user-agent` (static table) |
| 002e | `00 07` | ValueLen | 7 |
| 0030 | `77 63 75 72 6c 2f 31` | Value | "wcurl/1" |
| 0037 | `05` | Index | 5 → `accept` (static table) |
| 0038 | `00 03` | ValueLen | 3 |
| 003a | `2a 2f 2a` | Value | "*/*" |
| 003d | `00` | Index | 0: literal name follows (not in the table) |
| 003e | `08` | NameLen | 8 |
| 003f | `78 2d 63 6f 75 72 73 65` | Name | "x-course" |
| 0047 | `00 02` | ValueLen | 2 |
| 0049 | `6e 61` | Value | "na" |

### Frame 2: HEADERS (wserve → wcurl), 74 bytes = 8 header + 66 payload

| Offset | Bytes | Field | Meaning |
|---|---|---|---|
| 0000 | `00 42` | Length | 0x0042 = 66 bytes of payload (header not counted) |
| 0002 | `01` | Type | 0x01 HEADERS |
| 0003 | `00` | Flags | 0x00: none, so more frames follow |
| 0004 | `00 00 00 01` | Stream ID | 1: the first request on this connection |
| 0008 | `06` | Index | 6 → `:status` (static table) |
| 0009 | `00 03` | ValueLen | 3 |
| 000b | `32 30 30` | Value | "200" |
| 000e | `07` | Index | 7 → `content-type` (static table) |
| 000f | `00 09` | ValueLen | 9 |
| 0011 | `74 65 78 74 2f 68 74 6d 6c` | Value | "text/html" |
| 001a | `08` | Index | 8 → `content-length` (static table) |
| 001b | `00 02` | ValueLen | 2 |
| 001d | `31 35` | Value | "15" |
| 001f | `09` | Index | 9 → `server` (static table) |
| 0020 | `00 08` | ValueLen | 8 |
| 0022 | `77 73 65 72 76 65 2f 31` | Value | "wserve/1" |
| 002a | `0a` | Index | 10 → `date` (static table) |
| 002b | `00 1d` | ValueLen | 29 |
| 002d | `57 65 64 2c 20 30 37 20 4f 63 74 20 32 30 32 36 20 30 38 3a 33 39 3a 32 30 20 47 4d 54` | Value | "Wed, 07 Oct 2026 08:39:20 GMT" |

### Frame 3: DATA (wserve → wcurl), 23 bytes = 8 header + 15 payload

| Offset | Bytes | Field | Meaning |
|---|---|---|---|
| 0000 | `00 0f` | Length | 0x000f = 15 bytes of payload (header not counted) |
| 0002 | `00` | Type | 0x00 DATA |
| 0003 | `01` | Flags | 0x01 END_STREAM: last frame of this response |
| 0004 | `00 00 00 01` | Stream ID | 1: the first request on this connection |
| 0008 | `3c 68 31 3e 68 65 6c 6c 6f 3c 2f 68 31 3e 0a` | Body | 15 bytes of the file: `'<h1>hello</h1>\n'` |


## Checks a reader can make from the bytes alone

- **Every Length is right.** Frame 1's header says 0x43 = 67, and its fields are 6 + 14 + 17 + 10 + 6 + 14 = 67 bytes. Frame 2 says 0x42 = 66 = 6 + 12 + 5 + 11 + 32. Frame 3 says 0x0f = 15, which is the file size.
- **The body ends because of the frames.** END_STREAM is set only on the last frame of each direction: Frame 1, the request, which has no body, and Frame 3, the last DATA frame. `content-length: 15` agrees with the 15 DATA bytes, as SPEC §4 requires.
- **All three frames carry Stream ID 1.** The response is matched to its request by ID, not by arrival order.
- **Indexed names cost one byte.** `content-length` is 14 characters as text and `0x08` here. The one name not in the table, `x-course`, is sent literally: `00`, then NameLen `08`, then the 8 bytes.
- **Every value is ASCII text, including the status.** `:status` is the three bytes `32 30 30` ("200"), not a binary number.
- **Integers are big-endian.** Length `00 43` is 0x0043. In little-endian memory order it would appear as `43 00` (Day 1 ch.11).

## Size, compared with the same exchange as HTTP/1.1 text

| | WireHTTP/1 | HTTP/1.1 text |
|---|---|---|
| Request (with `x-course`) | 75 bytes | 98 bytes |
| Request (SPEC §6 example, no `-H`) | 61 bytes | 84 bytes |
| Response (headers + 15-byte body) | 97 bytes (74 + 23) | 134 bytes |
| Whole exchange | **172 bytes** | **232 bytes** |

The saving comes from the header *names*. The values (`localhost:9000`, the date) cost the same in both. Compressing values as well would need HPACK's dynamic table or Huffman coding, which this version leaves out on purpose (SPEC §3).
