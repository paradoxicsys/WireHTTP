#!/usr/bin/env bash
# run_tests.sh — every check from PLAN.md §4, against the real programs.
# Server-side tests send the hand-made frames in tests/*.hex (written from
# SPEC.md, not from our code) with nc, so the server is tested against bytes
# our own client never produced. Client-side tests use nc -l as a fake server.
#
#   usage: ./tests/run_tests.sh        (or: make test)

cd "$(dirname "$0")/.." || exit 2
PORT=9000          # wserve, as on the slide
FAKE=9001          # nc -l pretending to be a server
SILENT=9002        # nc -l that never answers
TMP=$(mktemp -d)
LOG=$TMP/server.log
PASS=0
FAIL=0
SPID=

cleanup() {
    [ -n "$SPID" ] && kill "$SPID" 2>/dev/null
    rm -f www/big.bin www/huge.bin secret.txt
    rm -rf "$TMP"
}
trap cleanup EXIT

ok() {   # ok <exit-status-of-check> <name>
    if [ "$1" = 0 ]; then echo "PASS  $2"; PASS=$((PASS + 1))
    else echo "FAIL  $2"; FAIL=$((FAIL + 1)); fi
}

hex() {  # hand-written hex file (with # comments) -> raw bytes
    sed 's/#.*//' "$1" | xxd -r -p
}

statuses() {  # send a .hex file to wserve on one connection; print each :status it answered
    echo $(hex "$1" | timeout 5 nc -N 127.0.0.1 $PORT | xxd -p | tr -d '\n' |
           grep -o '060003[0-9a-f]\{6\}' | cut -c7- |
           while read -r h; do echo "$h" | xxd -r -p; echo; done)
}

start_server() {
    ./wserve ./www $PORT 2>>"$LOG" &
    SPID=$!
    for _ in $(seq 20); do nc -z 127.0.0.1 $PORT 2>/dev/null && return; sleep 0.1; done
}

count() { grep -c -- "$1" "$LOG"; }

# ---------------------------------------------------------------- build
out=$(make -s -B 2>&1)
! grep -q -i warning <<<"$out" && [ -x wserve ] && [ -x wcurl ]
ok $? "T0  builds with -Wall -Wextra and no warnings"

start_server
head -c 300000 /dev/urandom > www/big.bin
head -c 20000000 /dev/zero > www/huge.bin

# ---------------------------------------------------------------- client + server
./wcurl localhost:$PORT/index.html > "$TMP/out"; rc=$?
[ $rc = 0 ] && cmp -s "$TMP/out" www/index.html
ok $? "T1  GET /index.html: body identical to the file, exit 0"

./wcurl localhost:$PORT/missing.html > "$TMP/out"; rc=$?
[ $rc = 1 ] && [ "$(cat "$TMP/out")" = "404 Not Found" ]
ok $? "T2  missing file: 404 Not Found, exit 1"

a=$(count accepted)
./wcurl -v localhost:$PORT/index.html localhost:$PORT/style.css > "$TMP/out" 2> "$TMP/err"; rc=$?
b=$(count accepted)
[ $rc = 0 ] && [ $((b - a)) = 1 ] && cat www/index.html www/style.css | cmp -s - "$TMP/out" &&
    grep -q "stream 2 GET /style.css -> 200" "$LOG" && [ "$(grep -c '^\* connected' "$TMP/err")" = 1 ]
ok $? "T3  two requests, ONE connection (server accepted once, streams 1 and 2)"

./wcurl -v localhost:$PORT/big.bin > "$TMP/out" 2> "$TMP/err"; rc=$?
[ $rc = 0 ] && cmp -s "$TMP/out" www/big.bin && [ "$(grep -c '^<- DATA' "$TMP/err")" = 19 ]
ok $? "T4  300,000-byte file: identical, sent as 19 DATA frames"

./wcurl -v localhost:$PORT/empty.txt > "$TMP/out" 2> "$TMP/err"; rc=$?
[ $rc = 0 ] && [ ! -s "$TMP/out" ] && grep -q '^<- DATA len=0 flags=0x01(END_STREAM)' "$TMP/err"
ok $? "T5  empty file: one DATA len=0 END_STREAM, exit 0"

# ---------------------------------------------------------------- server vs hand-made bytes
[ "$(statuses tests/malformed_then_get.hex)" = "400 200" ]
ok $? "T6  malformed block -> 400, then 200 on the SAME connection"

[ "$(statuses tests/unknown_then_get.hex)" = "200" ] && grep -q "skipped unknown frame type 0x7f" "$LOG"
ok $? "T7  server skips unknown frame type 0x7f, then answers 200"

[ "$(statuses tests/extensions.hex)" = "200" ]
ok $? "T7b unknown flag bit + literal name + unknown header index: still 200"

# ---------------------------------------------------------------- client vs a fake server
hex tests/resp_with_unknown.hex > "$TMP/resp.bin"
nc -l $FAKE < "$TMP/resp.bin" > "$TMP/captured.bin" &
NCPID=$!
sleep 0.3
./wcurl -v localhost:$FAKE/index.html > "$TMP/out" 2> "$TMP/err"; rc=$?
wait $NCPID 2>/dev/null
[ $rc = 0 ] && [ "$(cat "$TMP/out")" = "hello" ] && grep -q 'UNKNOWN(0x7f)' "$TMP/err"
ok $? "T8  client skips unknown frame type in a hand-made response"

hex tests/expected_request_9001.hex | cmp -s - "$TMP/captured.bin"
ok $? "T8b client's request bytes == SPEC §6 example, byte for byte"

# ---------------------------------------------------------------- malformed and refused
echo "TOP SECRET" > secret.txt
./wcurl 'localhost:9000/../secret.txt' > "$TMP/out"; rc=$?
[ $rc = 1 ] && [ "$(cat "$TMP/out")" = "400 Bad Request" ]
ok $? "T9  /../secret.txt: 400, file outside the root not served"

[ "$(statuses tests/post.hex)" = "405" ];           ok $? "T10 POST -> 405"
[ "$(statuses tests/missing_path.hex)" = "400" ];   ok $? "T11 no :path -> 400"
[ "$(statuses tests/no_end_stream.hex)" = "400" ];  ok $? "T12 request HEADERS without END_STREAM -> 400"
[ "$(statuses tests/stream_zero.hex)" = "400" ];    ok $? "T13 request on stream 0 -> 400"
[ "$(statuses tests/client_data.hex)" = "400" ];    ok $? "T13b DATA frame from a client -> 400"

# ---------------------------------------------------------------- robustness
./wcurl localhost:$PORT/huge.bin | head -c 10 > /dev/null    # wcurl dies, its socket sends RST
sleep 0.3
kill -0 "$SPID" 2>/dev/null && ./wcurl localhost:$PORT/index.html > /dev/null
ok $? "T14 client killed mid-download (RST): server survives"

# nc -N half-closes after the request (server goes to CLOSE_WAIT), stops reading
# (stdout is a pipe nobody reads), then is killed -> RST. The server's next write
# now fails with EPIPE, which raises SIGPIPE; without SIG_IGN it would die (status 141).
hex tests/get_huge.hex > "$TMP/get_huge.bin"    # a file, so nc sees end-of-input at once
timeout 0.5 nc -N 127.0.0.1 $PORT < "$TMP/get_huge.bin" | sleep 1
kill -0 "$SPID" 2>/dev/null && grep -q "write failed: Broken pipe" "$LOG" &&
    ./wcurl localhost:$PORT/index.html > /dev/null
ok $? "T14b half-close then reset: EPIPE, server survives (SIGPIPE ignored)"

a=$(count accepted)
./wcurl localhost:$PORT/a localhost:$FAKE/b 2> "$TMP/err"; rc=$?
[ $rc = 2 ] && [ "$(count accepted)" = "$a" ] && grep -q "same host:port" "$TMP/err"
ok $? "T17 URLs on two different servers: refused before connecting, exit 2"

./wcurl localhost:9003/x 2> "$TMP/err"; rc=$?
[ $rc = 2 ] && grep -q "Connection refused" "$TMP/err"
ok $? "T18 nobody listening: 'Connection refused', exit 2"

sleep 3 | nc 127.0.0.1 $PORT > /dev/null &       # hold a connection open...
HOLD=$!
sleep 0.3
kill "$SPID"; wait "$SPID" 2>/dev/null           # ...kill the server (it closes first)
start_server                                     # ...and restart at once
./wcurl localhost:$PORT/index.html > /dev/null && ! grep -q "Address already in use" "$LOG"
ok $? "T15 immediate restart works (SO_REUSEADDR)"
kill $HOLD 2>/dev/null

sleep 15 | nc -l $SILENT > /dev/null &
SIL=$!
sleep 0.3
t0=$(date +%s)
./wcurl localhost:$SILENT/x 2> "$TMP/err"; rc=$?
t1=$(date +%s)
[ $rc = 2 ] && grep -q "timed out" "$TMP/err" && [ $((t1 - t0)) -ge 9 ] && [ $((t1 - t0)) -le 12 ]
ok $? "T16 silent server: client gives up after ~10 s, exit 2"
kill $SIL 2>/dev/null

echo
echo "$PASS passed, $FAIL failed"
echo "--- server log ---"
cat "$LOG"
[ $FAIL = 0 ]
