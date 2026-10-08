# PLAN — "HTTP, in binary": `wserve` + `wcurl` (Network Architecture final project, 30 marks)

> Approved plan. Implementation follows the steps in §4.
> Decisions you made: **solo (both tracks)**, **C**, **assumed marks split** (no rubric yet), **spec as PDF made from Markdown**.

## Context
The final project is the binary-HTTP project that Day 5 Ch.18 says it is preparing you for ("the 24/8/8/31 your binary-HTTP project asked you to defend"). The marks come from three things: designing a small binary framing protocol, defending its choices with the course's own reasoning, and proving it works with two tiny programs plus an annotated hexdump. The aim is the simplest design that meets every line of the slide, built only from things the 8 lectures teach.

---

## Step 1 — The notes were read completely (verification)
- **File:** `~/Downloads/network-architecture-complete-notes.html` (543 KB, md5 `a72b1000…`). It is the same file as the attachment.
- **How it was read:** I parsed the static source with Python's stdlib. Playwright is not installed, and installing it is a write action that plan mode forbids. A browser pass isn't needed anyway: the page's JavaScript only toggles `hidden` on `<article>` blocks and builds the quizzes from JSON embedded in the page, so all the text is in the source.
- **What was found:** 8 `<article class="day">` blocks, 197 chapters, 79 hidden "check yourself" answers in `<details>`, and 106 quiz questions in the `#quizdata` JSON (Day 5: 26, Day 6: 26, Day 7: 26, Day 8: 28). All were read.
- **Optional:** if you still want a rendered pass, Step 0b can install Playwright and repeat this count.

| Day | Title | Ch. | Key topics |
|---|---|---|---|
| 1 | Sockets, from zero | 17 | kernel/syscalls, file descriptors, IP/port/4-tuple, the seven calls (socket, bind, listen, accept, read, write, close) and connect, byte-stream framing, ephemeral ports, TIME_WAIT/SO_REUSEADDR, SIGPIPE, htons/htonl, DNS/getaddrinfo, nc/telnet/curl, fork/exec/threads/pipes, OSI |
| 2 | Layers, framing & SS7 | 21 | OSI and encapsulation, Ethernet/Wi-Fi L2, MTU/MSS arithmetic, **framing (fixed/delimiter/length/both)**, text vs binary, hex/BCD/base64, **TLV/ASN.1**, RPC/gRPC, SS7 stack, SMS 140/160, SIP, end-to-end principle |
| 3 | TCP, UDP & text protocols | 19 | TCP promises, handshake/ISN/options, SYN cookies, seq/ack/window, congestion control/BBR, 4-way close and states, RTT tax, UDP, QUIC, SMTP/MIME/POP3/IMAP/FTP/NAT |
| 4 | Scaling servers & nginx | 28 | process/thread/event-loop servers, select vs epoll, timeouts, pre-fork, CGI/**FastCGI 8-byte record header**, servlets, Little's Law, C10K, Apache/HAProxy/Varnish, nginx internals, sendfile, reverse proxy cache, location rules, try_files, resumable parser |
| 5 | Evolution of HTTP | 29 | semantics vs encoding, 1.0→1.1 (keep-alive, Host, ETag, Range, chunked), 307, request smuggling, HOL blocking, Nagle + delayed ACK, **HTTP/2 9-byte frame header 24/8/8/31, frame types, HPACK static/dynamic/Huffman, CRIME**, multiplexing, push, QUIC/0-RTT/QPACK |
| 6 | (Ab)using CDNs | 27 | speed of light, Mathis, TTFB, edge pools, consistent hashing, anycast, cache keys, cf-cache-status, ESI, poisoning/deception, BBR, collapsing, WAF, rate limiting, DDoS, API gateway/JWT, Workers |
| 7 | Building for failures | 25 | failure map by syscall, retry vs handling, idempotency, backoff + full jitter, Retry-After, **timeouts**, circuit breaker, fallbacks, stale-if-error, recovery herd |
| 8 | Streaming video at scale | 31 | polling/long-poll/SSE/WebSockets/MQTT, held-socket cost, CDN billing, TTL stacking, negative caching, HLS playlists, MPEG-TS 188-byte packets, LL-HLS, ABR, ffmpeg |

The full syllabus boundary (every protocol, concept, tool, formula and method) is in **Appendix A**.

## Step 2 — Assignment slide transcription (fully legible; nothing guessed)
- **Header:** COURSE PROJECT · HTTP, IN BINARY · TWO TRACKS, ONE PROTOCOL — **"Now you write the spec"**
- **Track 1 — the server:** `$ ./wserve ./www 9000`. Accept a TCP connection · read one binary request frame · map the path to a file under a root · reply: status, headers, the bytes · 404 if it is not there · 400 if the frame is malformed · **and keep the connection open**
- **Track 2 — the client:** `$ ./wcurl -v localhost:9000/index.html`. Build the binary request frame · read the response, body to stdout · `-v` hexdumps every frame · exit non-zero on 4xx / 5xx · **and never open a second connection**
- **The bit in the middle is the actual project:**
  - A fixed-size frame header: you pick the fields and the widths, and you defend them. HTTP/2 chose 24 / 8 / 8 / 31. Why?
  - Headers: number the ten names you actually send, length-prefix the rest. HPACK's first two mechanisms, in an evening.
  - *And one line you may not skip:* **a receiver meeting a frame type it does not know MUST skip it cleanly.** That is how you leave room for a version 2.
- **What you hand in:**
  1. The spec. Two pages. Enough for a stranger.
  2. Your program.
  3. An annotated hexdump of one complete request and response. *"If you cannot annotate your own bytes, the spec is not finished."*
- **Footer:** *In pairs: one server, one client, and the only thing that crosses between you is the spec. A client that only works against your own server is an implementation, not a protocol.*
- **Not on the slide:** a marks breakdown, a deadline, a submission format, a language. (Assumptions are in §8.)

---

## 1. Requirements checklist (marks are ASSUMED; replace them when you get the rubric)
The weighting follows the slide's own emphasis: "the bit in the middle is the actual project".

| ID | Requirement (slide) | Assumed marks |
|---|---|---|
| **Spec** | | **10** |
| R1 | Fixed-size frame header: fields and widths chosen **and defended**, including an answer to "HTTP/2 chose 24/8/8/31 — why?" | 4 |
| R2 | Header encoding: the 10 names actually sent are numbered; everything else is length-prefixed | 2 |
| R3 | MUST-skip rule for unknown frame types (room for v2) | 2 |
| R4 | At most 2 pages, readable by a stranger (unambiguous: byte order, what lengths count, end-of-message, errors) | 2 |
| **Track 1 — `wserve`** | | **7** |
| R5 | `./wserve ./www 9000`: accepts TCP and reads one binary request frame per request | 1 |
| R6 | Maps the path to a file under the root, and cannot escape it | 1 |
| R7 | Replies with status, headers and the bytes | 2 |
| R8 | 404 when the file is missing | 1 |
| R9 | 400 when the frame is malformed | 1 |
| R10 | Keeps the connection open (several requests on one TCP connection) | 1 |
| **Track 2 — `wcurl`** | | **7** |
| R11 | `./wcurl -v localhost:9000/index.html` builds the binary request frame | 1 |
| R12 | Reads the response; body to stdout | 2 |
| R13 | `-v` hexdumps **every** frame | 2 |
| R14 | Exits non-zero on 4xx/5xx | 1 |
| R15 | Never opens a second connection | 1 |
| **Hand-in 3** | | **6** |
| R16 | Annotated hexdump of one complete request **and** response, every byte group labelled | 6 |
| — | **Total** | **30** |
| R17 (implicit) | Works against bytes **not** produced by our own code (footer). Solo, so proven with hand-made byte tests | (protects R5–R15) |

## 2. Requirement → lecture mapping (for viva and report justification)
| Req | Design element | Notes that justify it |
|---|---|---|
| R1 | 8-byte header, length first, byte-aligned fields | D5 Ch.18 (24/8/8/31, stream 0), D4 Ch.10 (FastCGI 8-byte record header), D1 Ch.11 (htons/htonl), D2 Ch.7 (field-by-field byte budget) |
| R1 | Length field; body sent as several DATA frames ending with END_STREAM | D2 Ch.8 + D1 Ch.7 (length vs delimiter, "never a third"), D5 Ch.9 (chunked = both, nested), D4 Ch.10 (empty-record end marker) |
| R1 | Stream ID on every frame | D5 Ch.15 (HTTP/1.1 had no id, so order became the id), D5 Ch.27 idea 1 ("multiplexing needs identity") |
| R1 | Fixed length position, so a bad request never desynchronises framing | D5 Ch.13 (request smuggling: two parsers, two readings) and Ch.18 ("unrepresentable") |
| R2 | 10-entry static name table, length-prefixed values, literal names | D5 Ch.19 (HPACK static table and indexing), D2 Ch.11 (TLV) |
| R2 | No compression, no dynamic table | D5 Ch.19 (CRIME), D5 Ch.25 (shared mutable table state) |
| R3 | Skip `length` bytes of unknown types; ignore unknown flags and header indexes | D2 Ch.11 (TLV lets an old parser skip what it doesn't know), D5 Ch.23 (ossification) |
| R4 | MUST/SHOULD wording, worked example, semantics reused from HTTP | D5 Ch.1/Ch.14 (semantics vs encoding: RFC 9110 vs 9112–9114), D5 Ch.12 (deployed reality — be precise) |
| R5 | socket/bind/listen/accept, INADDR_ANY, htons(port), SO_REUSEADDR | D1 Ch.4, Ch.5, Ch.6, Ch.9 |
| R5/R12 | `read_exact` / `write_all` loops | D1 Ch.7 (byte stream; short reads and writes), D4 Ch.25 (request line split over segments) |
| R6 | Reject `..`, map `/` to `/index.html`, `stat()` regular file | D6 Ch.19 (path traversal), D4 Ch.24 (try_files fallback), D4 Ch.23 (`stat()`) |
| R7 | Status/headers reuse HTTP meanings (200/400/404/405, content-type, content-length, server, date) | D5 Ch.2 (1.0 headers and status list), D5 Ch.10 (405) |
| R8/R9 | Strict validation; 4xx = "you failed" | D7 Ch.4, D5 Ch.13 (Postel's law loses) |
| R10 | Loop until `read()` returns 0; ignore SIGPIPE; TCP_NODELAY | D5 Ch.4 (persistent by default), D1 Ch.5 (`read()`=0 is peer close), D1 Ch.10 (SIGPIPE), D5 Ch.16 (Nagle + delayed ACK) |
| R11 | getaddrinfo, connect once, ephemeral port | D1 Ch.8, D1 Ch.12 (use getaddrinfo, not gethostbyname) |
| R12/R13 | Body to stdout (fd 1), `-v` output to stderr (fd 2) | D1 Ch.3 (fd 0/1/2), D1 Ch.13 (`curl -vv`) |
| R13 | Hex + ASCII dump of each frame | D2 Ch.10 (1 byte = 2 hex chars; `tcpdump -X`) |
| R14 | Exit 0 / 1 / 2 | D7 Ch.4 (status classes) |
| R15 | One connection for all URLs; **no retries** | D3 Ch.9 (round-trip tax), D4 Ch.11 (amortise the setup), D7 Ch.9 (GET is idempotent, but a retry would need a 2nd connection) |
| R15 | Client read timeout | D7 Ch.13 (connect/read timeouts), D4 Ch.6, D1 Ch.9 case 2 (listen but no accept → client hangs) |
| R16 | Annotated field-by-field dump | D2 Ch.7, D2 Ch.15 (SMS onion in hex), D5 Ch.18 (frame dump format) |
| Server model | Iterative, one connection at a time (documented limit) | D1 Ch.6, D4 Ch.1 (fork, threads and epoll = D4 Ch.2–5, left for v2) |

## 3. Proposed design
**"Topology"** is one machine over loopback: `wserve` listens on `0.0.0.0:9000` (INADDR_ANY) and `wcurl` connects to `localhost:9000` over TCP. No routing, addressing or subnet design is needed. Only `host:port` matters (Day 1 Ch.4).

### 3.1 Frame header — 8 bytes, all integers big-endian (network byte order)
```
 0               1               2               3
+---------------+---------------+---------------+---------------+
|          Length (16)          |   Type (8)    |   Flags (8)   |
+---------------+---------------+---------------+---------------+
|                        Stream ID (32)                         |
+---------------------------------------------------------------+
|                Payload: exactly Length bytes (0..65535)        |
```
| Field | Ours | HTTP/2 | Defence (each point traceable to the notes) |
|---|---|---|---|
| Length | 16 bits, payload only, header not counted | 24 bits | v1 has no SETTINGS frame, so the spec fixes the maximum. **Every value the field can hold is legal**, which means no "frame too big" error path, one fixed 64 KiB receive buffer, and a peer can't make us allocate megabytes. Bodies go as several DATA frames (wserve sends ≤16,384 B, HTTP/2's default MAX_FRAME_SIZE). That is length + end-marker, as in chunked encoding and FastCGI. Overhead: 8 B per 16 KiB ≈ 0.05% |
| Type | 8 | 8 | 256 types; v1 uses 2. The skip rule makes the other 254 safe for v2 |
| Flags | 8 | 8 | v1 uses one bit (END_STREAM). The other 7 are sent as 0 and ignored on receipt |
| Stream ID | 32 | R(1) + 31 | Every frame names its request, so a response never depends on order (D5 Ch.15). That leaves room for v2 pipelining. No odd/even split, because our server never starts streams (push died, D5 Ch.21). No reserved bit, because type and flags already leave room |
| Order/size | Length first; 8 B | 9 B | The reader always knows how far to skip before interpreting anything. Byte-aligned fields read with `ntohs`/`ntohl`, with no 24-bit or 31-bit masking (D1 Ch.11). Same size as FastCGI's header (D4 Ch.10) |

**"Why did HTTP/2 choose 24/8/8/31?"** Facts from D5 Ch.17–20:
- Binary frames with the length at a fixed place, so smuggling can't be expressed.
- About 10 frame types fit in 8 bits; 8 flag bits cover END_STREAM, END_HEADERS and the rest.
- A 31-bit stream id plus a reserved bit; client streams are odd and server streams even (so push could open streams without coordinating); stream 0 is the connection.
- Many streams interleave on one TCP connection, so frames are kept small (default max 16,384).

Our reading (label it as reasoning): 24 bits is enough for frames that are deliberately small, and saves a byte on every frame compared with 32.

### 3.2 Frame types and flags
- `0x00 DATA`: body bytes. `0x01 HEADERS`: a header block. Flag `0x01 END_STREAM`: the last frame of this request or response.
- **MUST-skip rule:** a receiver (client *or* server) that meets any other type MUST read and discard exactly `Length` payload bytes, then carry on with the next frame. It MUST NOT reply with an error or close the connection.
- Also extensible: unknown flag bits are ignored; unknown header indexes (11–255) are skipped, because the value is still length-prefixed.

### 3.3 Header block (HEADERS payload) — fields repeat until the payload ends
```
field := Index(8) [ NameLen(8) Name ]  ValueLen(16) Value
         Index 1..10  = static table name
         Index 0      = literal name follows (NameLen >= 1, lowercase ASCII)
         Index 11..255= reserved for v2 -> receiver skips the field
Values are always ASCII text, including :status ("404") and content-length ("15")
```
| Idx | Name | Sent by | | Idx | Name | Sent by |
|---|---|---|---|---|---|---|
| 1 | `:method` | wcurl | | 6 | `:status` | wserve |
| 2 | `:path` | wcurl | | 7 | `content-type` | wserve |
| 3 | `:authority` | wcurl | | 8 | `content-length` | wserve |
| 4 | `user-agent` | wcurl | | 9 | `server` | wserve |
| 5 | `accept` | wcurl | | 10 | `date` | wserve |

These are exactly the 10 names the two programs send. An indexed name costs 1 byte instead of up to 14 (`content-length`). Value length is 16 bits so long values still fit (D5 Ch.19 mentions a 300-byte cookie). Left out on purpose: value indexing, the dynamic table, Huffman coding and any compression (CRIME). `wcurl -H 'name: value'` (curl's flag, D6 Ch.14) sends literal names, which proves the "length-prefix the rest" path works.

### 3.4 Exchange rules (HTTP semantics, our encoding — D5 Ch.1)
- **Request:** exactly one HEADERS frame, END_STREAM set, stream id 1, 2, 3… It must contain one `:method` and one `:path`; it should contain `:authority`. GET only, no body.
- **Response:** one HEADERS frame (END_STREAM clear), then one or more DATA frames, the last with END_STREAM. An empty body is one DATA frame with length 0 and END_STREAM. Error responses carry a short `text/plain` body (`404 Not Found\n`), so the client has no special case.
- **Lengths:** frames decide where the body ends. `content-length` is informational. If the two disagree, the client reports a protocol error rather than guessing (the D5 Ch.13 lesson).
- **Status codes:**
  - 200: found.
  - 404: missing, or not a regular file.
  - 405: method is not GET.
  - **400** for any of these:
    - HEADERS frame on stream 0
    - request HEADERS frame without END_STREAM
    - DATA frame sent by the client
    - a header field that runs past the end of the payload, or NameLen = 0
    - `:method` or `:path` missing or repeated
    - `:path` not starting with `/`, or containing a `..` segment or a NUL byte
- **Connection:** the server keeps it open after **every** response, including 400, 404 and 405. That is safe because the fixed-position length means framing is never in doubt. It closes only when `read()` returns 0 or fails. The client waits for END_STREAM before sending its next request (no pipelining in v1), then closes when it has no URLs left.

### 3.5 Program behaviour
- **`wserve <root> <port>`:**
  - Startup: `signal(SIGPIPE, SIG_IGN)`, SO_REUSEADDR, bind INADDR_ANY, `listen(fd, 16)`.
  - Loop: `accept` → TCP_NODELAY → serve frames until EOF → `close`. Iterative: one connection at a time (documented limit).
  - Each frame is written with **one** `write_all` (header + payload in one buffer).
  - Content-type is chosen by file extension (html, css, js, txt, png, jpg; otherwise `application/octet-stream`).
  - Logs to stderr: `[conn 3] stream 1 GET /index.html -> 200 (15 B)`, `[conn 3] skipped type 0x7f (5 B)`, `[conn 3] closed`.
- **`wcurl [-v] [-H 'n: v']... host:port/path [host:port/path ...]`:**
  - All URLs must share one `host:port`; otherwise it exits 2 before connecting. That is the "never a second connection" rule.
  - `getaddrinfo` → one `connect` → SO_RCVTIMEO of 10 s.
  - For each URL: send HEADERS, then read frames. Unknown types are skipped; a known frame on the wrong stream is a protocol error. HEADERS gives `:status`; DATA goes to stdout with `fwrite`; stop at END_STREAM.
  - **No retries.** A retry after a dropped connection would need a second connection.
  - `-v` prints each frame to **stderr** as `-> HEADERS len=53 flags=0x01(END_STREAM) stream=1`, then a full hex + ASCII dump, then the decoded header fields.
  - **Exit codes:** 0 if every status < 400 · 1 if any status is 4xx/5xx · 2 for usage, connect, timeout or protocol errors.

### 3.6 Worked example (this goes in the spec and HEXDUMP.md) — `GET /index.html`, 61 bytes
```
00 35 01 01 00 00 00 01          header: len=53 type=HEADERS flags=END_STREAM stream=1
01 00 03 47 45 54                [1]:method      len 3   "GET"
02 00 0b 2f 69 6e 64 65 78 2e 68 74 6d 6c      [2]:path   len 11 "/index.html"
03 00 0e 6c 6f 63 61 6c 68 6f 73 74 3a 39 30 30 30   [3]:authority len 14 "localhost:9000"
04 00 07 77 63 75 72 6c 2f 31    [4]user-agent   len 7   "wcurl/1"
05 00 03 2a 2f 2a                [5]accept       len 3   "*/*"
```
The same request as HTTP/1.1 text is 84 bytes. Ours is 61, 27% smaller, and every name costs 1 byte.

### 3.7 Deliberately left out (keeps the project simple; mention as "v2" in the spec)
- TLS
- Concurrency (fork/threads/epoll — D4)
- Pipelining and multiplexing (stream IDs leave room for them)
- SETTINGS, PING, GOAWAY, RST_STREAM, CONTINUATION, PUSH_PROMISE and PRIORITY frames
- HPACK dynamic table and Huffman coding; any compression
- Methods other than GET, request bodies, query strings
- ETag/Range/caching
- Retries and backoff
- sendfile
- A connection preface or version field
- Virtual hosting (`:authority` is sent but ignored)
- Symlink handling inside `www/`

## 4. Step-by-step implementation (in order; each step has a check)
| # | Task | Output | Verify |
|---|---|---|---|
| 0 | Copy this plan to `PLAN.md`; create `src/ www/ tests/` | folders | `ls` |
| 1 | **Write `SPEC.md` first** (§3.1–3.4 + worked example) — the spec is the contract | SPEC.md draft | Decode the §3.6 bytes by hand using only the spec |
| 2 | Hand-write test frames **from the spec, not the code** as `tests/*.hex` (valid request, unknown-type frame, malformed blocks, POST, missing `:path`, no END_STREAM, stream 0, a hand-made response containing an unknown frame) | .hex files | `xxd -r -p f.hex \| xxd` shows the intended bytes, and each length field matches its byte count |
| 3 | `src/proto.h/.c`: constants and static table, `read_exact`, `write_all`, `send_frame`, `read_frame_header` (ntohs/ntohl), header-block encode/decode, `hexdump()` (~150 lines) | proto.c | `make` with `-Wall -Wextra` prints no warnings |
| 4 | `src/wserve.c` socket skeleton + frame loop + skip rule + logging | wserve | `xxd -r -p tests/unknown_then_get.hex \| nc -N localhost 9000 \| xxd` |
| 5 | wserve request handling: 400/405 checks, path mapping + `stat`, HEADERS + ≤16 KiB DATA frames, error bodies | wserve | tests T6, T7, T9–T13 below |
| 6 | `src/wcurl.c`: args, `-H`, same-host check, getaddrinfo, one connect, timeout, send, read loop, `-v` to stderr, exit codes | wcurl | tests T1–T5, T8 |
| 7 | `tests/run_tests.sh` automates everything below; prints PASS/FAIL | script | all PASS |
| 8 | Generate `HEXDUMP.md` from `./wcurl -v -H 'x-course: na' localhost:9000/index.html 2> trace.txt` (tiny `www/index.html` = `<h1>hello</h1>\n`, 15 B); annotate every byte group. Optional: `sudo tcpdump -i lo -X port 9000` to show the wire bytes match | HEXDUMP.md | Bytes in the spec example == bytes in the trace |
| 9 | Finalise SPEC.md; export with `python3 -m markdown -x tables -x fenced_code SPEC.md > SPEC.html && google-chrome --headless --print-to-pdf=SPEC.pdf SPEC.html` | SPEC.pdf | ≤ 2 pages; cold read the next day ("stranger test") |
| 10 | `README.md`: build/run/test, file map, limits, short lecture mapping | README.md | Fresh clone → `make && make test` works |

**Tests** (in `run_tests.sh`; nc is OpenBSD netcat, so use `-N` to send FIN after stdin ends):
| # | Test | Pass condition |
|---|---|---|
| T1 | `wcurl localhost:9000/index.html > out` | `cmp out www/index.html`, exit 0 |
| T2 | `wcurl …/missing.html` | exit 1, `404 Not Found` on stdout |
| T3 | `wcurl …/index.html …/style.css` | both bodies correct; server log shows **one** `accepted` and two requests (R10 + R15) |
| T4 | 300,000-byte random `big.bin` | `cmp` equal; `-v` shows 19 DATA frames |
| T5 | empty file | exit 0, empty stdout, one `DATA len=0 END_STREAM` |
| T6 | malformed block **then** a valid request on one nc session | response contains `:status 400`, then `200` (connection stayed open) |
| T7 | unknown type `0x7f` then a valid request → server | one 200 response; log says `skipped` |
| T8 | `nc -l 9001 < resp_with_unknown.bin` → wcurl | body printed, exit 0 (client side of the skip rule; bytes not from our server) |
| T9 | `wcurl 'localhost:9000/../secret.txt'` (secret.txt placed beside www/) | 400, file not served |
| T10–T13 | POST; missing `:path`; no END_STREAM; stream 0 | 405; 400; 400; 400 |
| T14 | `wcurl …/big5MB.bin \| head -c 10`, then a normal request | the server is still alive (SIGPIPE ignored) |
| T15 | hold `nc localhost 9000`, kill wserve, restart it immediately | no "Address already in use" (SO_REUSEADDR) |
| T16 | `nc -l 9002` that never replies → wcurl | exits 2 after about 10 s |
| T17 | `wcurl a:1/x b:2/y` | exit 2, no connection made |

## 5. Deliverables
```
NA-Final-Project/
  PLAN.md            this plan
  SPEC.md / SPEC.pdf HAND-IN 1: <=2 pages: overview, byte order, header diagram + field table,
                     types/flags + MUST-skip, header block + 10-name table, exchange rules +
                     status codes + 400 list, "why our widths vs 24/8/8/31" table, worked example
  Makefile           builds ./wserve and ./wcurl at top level (so the slide's commands work verbatim); `make test`
  src/proto.h proto.c wserve.c wcurl.c        HAND-IN 2 (~500 lines of C total)
  www/index.html style.css empty.txt          (big files generated by the test script)
  tests/run_tests.sh tests/*.hex
  HEXDUMP.md         HAND-IN 3: one request + its full response; each frame header split into
                     len/type/flags/stream; each header field split into index/len/value with ASCII;
                     the literal-name field (-H) and the DATA frame labelled; byte totals; 61 B vs 84 B
  README.md          how to build/run/test; known limits (iterative server, GET only)
```
Screenshots aren't needed, since every piece of evidence is text: test output, server log, `-v` trace. Include `run_tests.sh` output in the README. No diagrams beyond the ASCII header diagram.

## 6. Marks strategy
| Criterion | Show this for full credit |
|---|---|
| R1 widths defended | The field table with an HTTP/2 column and a reason per field; a separate 3-line answer to "why 24/8/8/31" |
| R2 headers | The 10-name table matches what the programs actually send (check against the trace); one literal field in the hexdump |
| R3 skip rule | A MUST sentence in the spec + T7 (server) and T8 (client) passing |
| R4 stranger-ready | States byte order, what Length counts, END_STREAM, empty body, max sizes, behaviour after 400, exit codes; one worked example; ≤2 pages |
| R5–R10 server | T1–T7, T9–T15 output; the server log proving one connection serves several requests |
| R11–R15 client | `-v` trace; T2 exit code; T3 one-connection proof; T8 against foreign bytes |
| R16 hexdump | Every byte is accounted for and the annotation totals equal the length fields |

**Viva one-liners to prepare:**
- Why 16-bit length? Every value is legal and the receiver uses a fixed buffer.
- Why a stream ID when there's no multiplexing? Identity, and room for v2.
- Why not gzip the headers? CRIME.
- Why keep the connection after a 400? The length is at a fixed position.
- Why no retries? A retry would need a second connection.
- Why TCP_NODELAY? Nagle + delayed ACK costs about 40 ms (D5 Ch.16).

**Common mistakes that lose marks:**
- Assuming one `read()` returns one frame.
- Ignoring short writes.
- Missing `htons`/`ntohl`.
- The server dying on SIGPIPE.
- Closing the connection after each response, or after a 400.
- Opening a new connection per URL or on a retry.
- `-v` output going to stdout (it corrupts the body).
- Exit 0 on a 404.
- Path traversal.
- Treating an unknown type as an error, or not consuming its payload.
- A spec that doesn't say whether Length includes the header.
- A spec longer than 2 pages.
- A spec example that disagrees with the real bytes.
- A table listing names you never send.
- Testing only own-client-vs-own-server (the shared `proto.c` would hide symmetric bugs — **that is why the step-2 .hex tests are written from the spec first**).

## 7. Student-realism check
- About 500 lines of C using only Day 1 calls plus `stat`, `open`, `strftime` and `getaddrinfo`. One Makefile, one shell test script. Third-year level, with no frameworks or libraries.
- **Flags — where I was tempted to go beyond the notes:**
  - **(a)** `SO_RCVTIMEO` on the client: the concept is in the notes (D7 Ch.13, D4 Ch.6) but the option name isn't. It's 3 lines; drop it if you prefer.
  - **(b)** `xxd` / `xxd -r -p`: the notes teach hex and `tcpdump -X` but don't name `xxd`. It's a standard hex tool.
  - **(c)** HTTP/2's *reason* for 24 bits isn't stated in the notes; the spec presents it as our reasoning.
  - **(d)** `python-markdown` + headless Chrome are used only to make the PDF.
  - **(e)** 405 and `-H` are both in the notes (D5 Ch.10, D6 Ch.14) but go slightly past the slide's minimum. Each is under 10 lines.
- **Explicitly avoided:** fork/epoll concurrency, the HPACK dynamic table, TLS, Wireshark, valgrind/ASan, and RFC 9292 (a real "Binary HTTP" standard that isn't in the notes — don't cite it).

## 8. Open questions / assumptions (none block starting)
1. The slide says **"in pairs"** and you're working solo. Please confirm the instructor allows it. If not, the spec stays the same and the tracks split.
2. The marks split is **assumed**. Swap in the real rubric when you have it.
3. The deadline and submission method (zip, repo or LMS?) are unknown.
4. `wcurl` accepts several same-host URLs, to demonstrate keep-alive. The slide's single-URL form still works exactly.
5. `-v` dumps the **full** payload of every frame (to stderr).
6. Non-GET gets 405 rather than 400. Error responses carry a short text body.
7. Two pages means A4 at about 10–11 pt with the rationale table inside. The README is extra and not one of the 3 hand-ins.
8. Your name/ID go in the spec header. Does a partner name field apply?

---

## Appendix A — Syllabus boundary (anything not listed here is out of scope)
- **D1 — Sockets:** kernel/user space/syscall/process/PID; fd 0/1/2; socket; buffer; IPv4, 127.0.0.1, 0.0.0.0/INADDR_ANY; ports, <1024 needs root, well-known ports (21/20, 22, 25, 53, 80/443, 110/143); 4-tuple; socket/bind/listen/accept/read/write/close/connect; AF_INET, SOCK_STREAM/SOCK_DGRAM, sockaddr_in; backlog, somaxconn; blocking; byte stream; framing (length/delimiter); short writes; ephemeral ports 32768–60999; FIN/RST; TIME_WAIT; SO_REUSEADDR; SO_LINGER{1,0}; signals SIGINT/KILL/PIPE/TERM/HUP, SIG_IGN, MSG_NOSIGNAL, EPIPE; endianness, htons/htonl/ntohs/ntohl; DNS, /etc/hosts, resolv.conf, TTL, gethostbyname vs getaddrinfo, DNS hijacking; TLS, certificates, pinning; fork/exec, threads (8 MB stack), pipes, connection pools; OSI; encapsulation; reverse proxy. *Tools:* gcc, nc (`-l`), telnet, `openssl s_client`, `curl -i/-vv`, `ulimit -n`, kill.
- **D2 — Layers & framing:** OSI layers and units (frame/packet/segment); MAC, EtherType 0x0800, FCS; MTU 1500, min 46; CSMA/CD, CSMA/CA; Wi-Fi L2 ACKs; Starlink; IPv4 fields (TTL, protocol 6/17, checksum); TCP fields; framing fixed/delimiter/length/both; dot-stuffing/escaping; text vs binary; ASCII, octet, nibble, BCD, hex, base64; TLV; ASN.1/BER; RPC, gRPC, protobuf, marshalling; SS7 (MTP1–3, SCCP, TCAP, MAP, ISUP, INAP), point codes, 2600 Hz, in/out-of-band, control/data plane; SMS (GSM-7/UCS-2, TP-DCS), MSC/HLR/SMSC; IAM/ACM/ANM/REL/RLC; SIP/SDP/RTP; end-to-end principle. *Tools:* `tcpdump -X`, traceroute.
- **D3 — TCP/UDP & text protocols:** best-effort IP; connection state; seq/ack; RTT; 3-way handshake; SYN/ACK/FIN/RST/PSH; ISN randomisation; MSS/SACK/wscale; half-open, SYN flood, SYN cookies; RTO, fast retransmit; receive/sliding window; flow vs congestion control; Reno/CUBIC, bufferbloat, BBR; 4-way close, half-close, CLOSE_WAIT/FIN_WAIT_2/TIME_WAIT, MSL; TLS RTT counts; TCP Fast Open; UDP 8-byte header; QUIC/HTTP3; multiplexing; HOL blocking; SMTP (25/587/465, EHLO…QUIT, envelope vs letter, SPF/DKIM/DMARC); MIME; POP3; IMAP; FTP active/passive, 227 decoding; NAT. *Tools:* telnet, nc, podman/docker, Mailpit, GreenMail, netstat, ss.
- **D4 — Scaling/nginx:** process/thread per client, event loop, select/FD_SETSIZE 1024, epoll/kqueue/IOCP/io_uring, read/write/except fd sets, timeouts, pre-forking, SO_REUSEPORT, thundering herd, keep-alive, Apache MPMs, CGI (env vars, pipes), mod_php, FastCGI (8-byte header, empty-record end), php-fpm, servlets, connection pools, amortise the setup, stateless, four walls, Little's Law, C10K/C10M, HAProxy, Varnish, nginx master/worker, accept_mutex, sendfile/page cache/DMA, reverse/forward proxy, upstream, proxy_cache (keys_zone, levels, inactive, valid, lock, use_stale), LRU, location precedence, try_files, named locations, smooth weighted RR, resumable state-machine parser, phases, memory pools. *Tools:* ps, ulimit, nginx config.
- **D5 — HTTP:** semantics vs encoding (RFC 9110–9114, 7230–7235, 1945, 2068, 2616); 1.0 methods/16 headers/status codes; If-Modified-Since/304; keep-alive vs Connection: close; Host/vhosts/SNI/:authority; ETag/If-None-Match/If-Match, weak validators; Content-Encoding, Vary; Range/206/416; chunked; OPTIONS/PUT/DELETE/TRACE/Via/Expect/Cache-Control; no-cache vs no-store; 2/6 connections, domain sharding, pipelining; 302 vs 307; Postel's law, request smuggling; token grammar/base64url/JWT; HOL blocking, header bloat; Nagle + delayed ACK, TCP_NODELAY; SPDY; connection preface; ALPN, h2c; 9-byte frame header 24/8/8/31, odd/even/0 streams, 10 frame types, SETTINGS defaults; HPACK static (61)/dynamic/Huffman, CRIME; multiplexing; push vs 103 Early Hints; TCP HOL under HTTP/2; QUIC (UDP, middleboxes, ossification, userspace), RTT counts, 0-RTT/replay/425, connection IDs, QPACK; Alt-Svc, HTTPS RR; UDP blocking and fallback.
- **D6 — CDNs:** CDN/edge/PoP/origin; light in fibre; Mathis; TTFB; origin pools; shield/tiered cache; consistent hashing; anycast/colo/cf-ray; default caching, cache key; cf-cache-status (8 values); SSI/ESI/Ajax/Railgun; dictionary compression (RFC 9842); poisoning/deception; BBR; shared vs per-worker pools; request collapsing; WAF; rate-limit algorithms; DDoS; API gateway, JWT alg:none, mTLS; Workers/V8 isolates; domain fronting. *Tools:* `curl -H/-s/-sI`, dig.
- **D7 — Failures:** six failure stages; failure map by syscall; status classes; retry vs handling; four classify questions; transient vs structural; idempotency, idempotency keys; exponential backoff, full jitter, cap, max attempts; Retry-After, 408/429/503; connect/read/write timeouts, deadlines, differential timeouts; circuit breaker; video-player failures, live-edge 404, custom codes; fallbacks; stale-if-error; failover, feature flags, graceful degradation; recovery herd, slow start.
- **D8 — Streaming:** pull vs push; polling + ETag + jitter; long polling; SSE (Last-Event-ID); WebSockets (Upgrade/101/Accept/frame/mask); MQTT (retained, QoS 1, varint, keepalive); held-socket cost; S3 limits; CDN request vs byte billing; TTL stacking; prefresh; collapsing; negative caching; JSON Patch diffs; HLS master/media playlists, segments, tags; MPEG-TS 188 B (0x47, PID); fMP4/CMAF; segment length; buffer; LL-HLS; spoiler fix; ABR; multi-CDN/content steering; stale playlists; ffmpeg HLS flags.
- **Formulas:**
  - MSS = 1500 − 20 − 20 = 1460; 1518 B on the wire
  - throughput ≤ window ÷ RTT
  - Little's Law L = λ × W
  - Mathis: throughput ≈ MSS ÷ (RTT × √loss)
  - base64 = 4/3 size
  - SMS: 140 × 8 ÷ 7 = 160 characters; 140 ÷ 2 = 70
  - FTP port = hi × 256 + lo
  - light in fibre ≈ 4.9 ms per 1000 km
  - backoff = min(cap, base × 2^attempt); full jitter = uniform(0, d)
  - 2 MSL ≈ 60 s
  - worst-case staleness = poll interval + Σ TTL
  - HLS BANDWIDTH = (video + audio) × 1.1
- **Design methods:** layering/encapsulation; framing length/delimiter/both; TLV extensibility; control/data plane split; end-to-end principle; push state into a token; amortise the setup; nothing is free per connection; who holds the state; semantics vs encoding; multiplexing needs identity; deployed reality beats the spec; strict parsing over Postel; inform, don't act; classify before retrying; move the work or move the data; number the pieces, then cache them forever.
