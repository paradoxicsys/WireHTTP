CC     = gcc
CFLAGS = -std=gnu11 -Wall -Wextra -O2

all: wserve wcurl

wserve: src/wserve.c src/proto.c src/proto.h
	$(CC) $(CFLAGS) -o $@ src/wserve.c src/proto.c

wcurl: src/wcurl.c src/proto.c src/proto.h
	$(CC) $(CFLAGS) -o $@ src/wcurl.c src/proto.c

spec: SPEC.pdf

SPEC.pdf: SPEC.md spec.css
	( echo '<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="spec.css">'; \
	  python3 -m markdown -x tables -x fenced_code SPEC.md ) > SPEC.html
	google-chrome --headless --no-pdf-header-footer --print-to-pdf=SPEC.pdf SPEC.html 2>/dev/null
	rm -f SPEC.html

test: all
	./tests/run_tests.sh

clean:
	rm -f wserve wcurl

.PHONY: all spec test clean
