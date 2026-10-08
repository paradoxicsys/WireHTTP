# WireHTTP/1 — HTTP in binary (`wserve` + `wcurl`)

Network Architecture course project: a binary framing for HTTP, a server that speaks it, and a client that speaks it.

| Hand-in | File |
|---|---|
| 1. The spec (2 pages) | `SPEC.pdf`, built from `SPEC.md` |
| 2. The program | `src/` (C), built by `make` into `./wserve` and `./wcurl` |
| 3. Annotated hexdump of one request and response | `HEXDUMP.md` |

## Build, run, test

```
make                                   # gcc -std=gnu11 -Wall -Wextra; builds ./wserve and ./wcurl
./wserve ./www 9000                    # Track 1: serve ./www on port 9000
./wcurl -v localhost:9000/index.html   # Track 2: body to stdout, every frame to stderr
make test                              # 23 checks, about 17 s (needs ports 9000-9003 free, nc, xxd)
make spec                              # SPEC.md -> SPEC.pdf (python3-markdown + headless Chrome)
```

### `wserve <root-dir> <port>`
- Serves regular files under `<root-dir>`; `/` means `/index.html`.
- Answers 200, 400 (malformed), 404 (missing) or 405 (not GET).
- Keeps every connection open until the client closes it, including after a 400.
- Logs one line per connection and per request to stderr.

### `wcurl [-v] [-H 'name: value']... host:port/path [host:port/path ...]`
- Fetches every URL over **one** connection, one request at a time. All URLs must therefore share one `host:port`; otherwise it refuses before connecting.
- `-v` prints every frame sent (`->`) and received (`<-`) in hex and decoded, to stderr.
- `-H` adds a header. Names outside the 10-entry table are sent as literals.
- Never retries, and gives up if the server is silent for 10 s.

| Exit code | Meaning |
|---|---|
| 0 | Every response was below 400 |
| 1 | At least one response was 4xx or 5xx (the error body still goes to stdout) |
| 2 | Usage error, connection refused, timeout, or protocol error |

## Design in five lines (details and reasons in SPEC.md)
1. **Frame header, 8 bytes, big-endian:** Length 16 · Type 8 · Flags 8 · Stream ID 32. Length counts the payload only, and every value is legal.
2. **Types:** DATA `0x00` and HEADERS `0x01`. Flag `0x01` is END_STREAM. **Unknown types are skipped by Length**; unknown flag bits and header indexes are ignored.
3. **Headers:** the 10 names the programs send are table indexes 1–10 (1 byte each). Any other name is a literal (index 0 + length + text). Values are always length-prefixed text.
4. **Exchange:** a request is one HEADERS frame with END_STREAM. A response is one HEADERS frame, then DATA frames of at most 16 KiB, the last with END_STREAM.
5. **Errors:** a malformed request gets 400 and the connection stays open. The length's fixed position means a bad frame can never desynchronise the stream.

## Testing approach
`tests/*.hex` are frames written by hand **from the spec, before the code existed**, with a comment on every line. The server is tested by sending them with `nc` (bytes our client never produced). The client is tested against `nc -l` playing a server with a hand-made response (bytes our server never produced). Test T8b also checks that the client's request matches the spec's worked example byte for byte. Because both programs share `src/proto.c`, a bug in the shared encoder could hide if they were only tested against each other; these tests rule that out.

```
PASS  T0  builds with -Wall -Wextra and no warnings
PASS  T1  GET /index.html: body identical to the file, exit 0
PASS  T2  missing file: 404 Not Found, exit 1
PASS  T3  two requests, ONE connection (server accepted once, streams 1 and 2)
PASS  T4  300,000-byte file: identical, sent as 19 DATA frames
PASS  T5  empty file: one DATA len=0 END_STREAM, exit 0
PASS  T6  malformed block -> 400, then 200 on the SAME connection
PASS  T7  server skips unknown frame type 0x7f, then answers 200
PASS  T7b unknown flag bit + literal name + unknown header index: still 200
PASS  T8  client skips unknown frame type in a hand-made response
PASS  T8b client's request bytes == SPEC §6 example, byte for byte
PASS  T9  /../secret.txt: 400, file outside the root not served
PASS  T10 POST -> 405
PASS  T11 no :path -> 400
PASS  T12 request HEADERS without END_STREAM -> 400
PASS  T13 request on stream 0 -> 400
PASS  T13b DATA frame from a client -> 400
PASS  T14 client killed mid-download (RST): server survives
PASS  T14b half-close then reset: EPIPE, server survives (SIGPIPE ignored)
PASS  T17 URLs on two different servers: refused before connecting, exit 2
PASS  T18 nobody listening: 'Connection refused', exit 2
PASS  T15 immediate restart works (SO_REUSEADDR)
PASS  T16 silent server: client gives up after ~10 s, exit 2
23 passed, 0 failed
```

T14b and T15 were each checked against a build *without* the protection they test. Without `signal(SIGPIPE, SIG_IGN)` the server is killed with status 141 (128 + SIGPIPE). Without `SO_REUSEADDR` the restart fails with `bind: Address already in use`.

## Known limits (deliberate, for simplicity)
- **One connection at a time.** A second client waits in the accept queue until the first disconnects (Day 1 ch.6). The fix would be fork, threads or epoll (Day 4).
- **GET only.** No request bodies, query strings, caching headers or ranges.
- **No multiplexing or pipelining in v1,** although every frame already carries a stream ID for it.
- **No TLS and no header compression** (no HPACK dynamic table or Huffman coding).
- **The client is IPv4 only,** like the server.

## Files
```
SPEC.md / SPEC.pdf   the protocol (hand-in 1)
HEXDUMP.md           annotated request + response (hand-in 3)
PLAN.md              the plan: requirements, lecture mapping, marks strategy
Makefile             make / make test / make spec / make clean
spec.css             page style for SPEC.pdf
src/proto.h, proto.c frame read/write, header block encode/decode, hexdump
src/wserve.c         Track 1 server
src/wcurl.c          Track 2 client
www/                 sample files served by the tests
tests/run_tests.sh   all checks; tests/*.hex hand-made frames
```
