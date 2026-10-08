/* wcurl — Track 2: fetches files using WireHTTP/1 (SPEC.md).
 *
 *   usage: ./wcurl [-v] [-H 'name: value']... host:port/path [host:port/path ...]
 *
 * Every URL goes over ONE TCP connection, one request at a time, so all
 * URLs must share the same host:port. The body goes to stdout; -v prints
 * every frame (hex + decoded) to stderr, so it never mixes with the body.
 * No retries: a retry after a lost connection would need a second one.
 *
 * Exit: 0 = every response < 400, 1 = some response was 4xx/5xx,
 *       2 = usage, network, timeout or protocol error. */
#include "proto.h"

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define MAX_URLS         64
#define MAX_EXTRA        16
#define READ_TIMEOUT_SEC 10   /* never wait forever for a reply (Day 7 ch.13) */

struct url   { char host[256]; char port[8]; char authority[264]; const char *path; };
struct extra { char name[256]; const char *value; };

static int verbose;
static struct frame in, out;

static void die(const char *msg)
{
    fprintf(stderr, "wcurl: %s\n", msg);
    exit(2);
}

static void usage(void)
{
    die("usage: wcurl [-v] [-H 'name: value']... host:port/path [host:port/path ...]");
}

/* "localhost:9000/index.html" -> host "localhost", port "9000", path "/index.html" */
static int parse_url(const char *s, struct url *u)
{
    const char *slash = strchr(s, '/');
    size_t hp_len = slash ? (size_t)(slash - s) : strlen(s);
    const char *colon = memchr(s, ':', hp_len);
    size_t host_len, port_len;

    if (colon == NULL || colon == s)
        return -1;
    host_len = colon - s;
    port_len = hp_len - host_len - 1;
    if (host_len >= sizeof u->host || port_len == 0 || port_len >= sizeof u->port)
        return -1;
    memcpy(u->host, s, host_len);
    u->host[host_len] = '\0';
    memcpy(u->port, colon + 1, port_len);
    u->port[port_len] = '\0';
    for (size_t i = 0; i < port_len; i++)
        if (!isdigit((unsigned char)u->port[i]))
            return -1;
    memcpy(u->authority, s, hp_len);    /* "host:port", sent as :authority */
    u->authority[hp_len] = '\0';
    u->path = slash ? slash : "/";
    return 0;
}

/* "X-Course: na" -> name "x-course" (names are lower case), value "na" */
static int parse_extra(const char *s, struct extra *e)
{
    const char *colon = strchr(s, ':');
    size_t n;

    if (colon == NULL || colon == s || (size_t)(colon - s) >= sizeof e->name)
        return -1;
    n = colon - s;
    for (size_t i = 0; i < n; i++)
        e->name[i] = tolower((unsigned char)s[i]);
    e->name[n] = '\0';
    e->value = colon + 1;
    while (*e->value == ' ')
        e->value++;
    return 0;
}

static int connect_to(const char *host, const char *port)
{
    struct addrinfo hints = {0}, *res;
    struct timeval tv = { .tv_sec = READ_TIMEOUT_SEC };
    int fd, err;

    hints.ai_family = AF_INET;          /* IPv4, like wserve (and the notes) */
    hints.ai_socktype = SOCK_STREAM;
    err = getaddrinfo(host, port, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "wcurl: %s: %s\n", host, gai_strerror(err));
        exit(2);
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        fprintf(stderr, "wcurl: connect to %s:%s: %s\n", host, port, strerror(errno));
        exit(2);
    }
    freeaddrinfo(res);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    if (verbose)
        fprintf(stderr, "* connected to %s:%s (one connection for every request)\n", host, port);
    return fd;
}

static void protocol_error(const char *msg)
{
    fprintf(stderr, "wcurl: protocol error: %s\n", msg);
    exit(2);
}

/* Parse "123" style decimal values; -1 if not all digits. */
static long long number(const struct field *f)
{
    long long n = 0;
    if (f->value_len == 0 || f->value_len > 18)
        return -1;
    for (size_t i = 0; i < f->value_len; i++) {
        if (!isdigit((unsigned char)f->value[i]))
            return -1;
        n = n * 10 + (f->value[i] - '0');
    }
    return n;
}

/* Send one request on `stream` and read its whole response.
   Returns the HTTP status. Any protocol problem exits with 2. */
static int fetch(int fd, const struct url *u, uint32_t stream,
                 const struct extra *extra, int n_extra)
{
    size_t pos = 0;
    int status = 0;
    long long expected = -1, got = 0;

    if (add_field(out.payload, &pos, ":method", "GET") < 0 ||
        add_field(out.payload, &pos, ":path", u->path) < 0 ||
        add_field(out.payload, &pos, ":authority", u->authority) < 0 ||
        add_field(out.payload, &pos, "user-agent", "wcurl/1") < 0 ||
        add_field(out.payload, &pos, "accept", "*/*") < 0)
        die("request does not fit in one frame");
    for (int i = 0; i < n_extra; i++)
        if (add_field(out.payload, &pos, extra[i].name, extra[i].value) < 0)
            die("request does not fit in one frame");

    out.type = TYPE_HEADERS;
    out.flags = FLAG_END_STREAM;        /* a GET has no body */
    out.stream = stream;
    out.length = (uint16_t)pos;
    if (verbose) {
        dump_frame(stderr, "->", &out);
        dump_fields(stderr, out.payload, out.length);
    }
    if (send_frame(fd, &out) < 0) {
        fprintf(stderr, "wcurl: write: %s\n", strerror(errno));
        exit(2);
    }

    for (;;) {
        int r;
        errno = 0;
        r = recv_frame(fd, &in);
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            die("timed out waiting for the server");
        if (r <= 0)
            die("connection closed before the response ended");
        if (verbose)
            dump_frame(stderr, "<-", &in);

        if (in.type != TYPE_HEADERS && in.type != TYPE_DATA) {
            if (verbose)
                fputs("   unknown frame type: skipped (SPEC 2)\n", stderr);
            continue;                   /* already read in full, so just move on */
        }
        if (in.stream != stream)
            protocol_error("frame for a stream that is not the open request");

        if (in.type == TYPE_HEADERS) {
            struct field f;
            size_t p = 0;
            int rf;
            if (status != 0)
                protocol_error("second HEADERS frame in one response");
            if (verbose)
                dump_fields(stderr, in.payload, in.length);
            while ((rf = next_field(in.payload, in.length, &p, &f)) == 1) {
                if (f.index == H_STATUS && f.value_len == 3)
                    status = (int)number(&f);
                else if (f.index == H_CONTENT_LENGTH)
                    expected = number(&f);
            }
            if (rf < 0)
                protocol_error("malformed header block");
            if (status < 100)
                protocol_error("response has no valid :status");
            if (in.flags & FLAG_END_STREAM)
                protocol_error("response HEADERS has END_STREAM, but DATA must follow");
        } else {
            if (status == 0)
                protocol_error("DATA before HEADERS");
            fwrite(in.payload, 1, in.length, stdout);
            got += in.length;
            if (in.flags & FLAG_END_STREAM)
                break;
        }
    }
    fflush(stdout);
    if (expected >= 0 && got != expected)
        protocol_error("content-length does not match the DATA frames");
    if (verbose)
        fprintf(stderr, "* stream %u done: status %d, %lld body bytes\n", stream, status, got);
    return status;
}

int main(int argc, char **argv)
{
    struct url urls[MAX_URLS];
    struct extra extra[MAX_EXTRA];
    int n_urls = 0, n_extra = 0, failed = 0, fd;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-H") == 0) {
            if (++i == argc || n_extra == MAX_EXTRA || parse_extra(argv[i], &extra[n_extra]) < 0)
                usage();
            n_extra++;
        } else if (argv[i][0] == '-') {
            usage();
        } else {
            if (n_urls == MAX_URLS)
                die("too many URLs");
            if (parse_url(argv[i], &urls[n_urls]) < 0)
                die("URL must look like host:port/path");
            n_urls++;
        }
    }
    if (n_urls == 0)
        usage();
    for (int i = 1; i < n_urls; i++)
        if (strcmp(urls[i].host, urls[0].host) || strcmp(urls[i].port, urls[0].port))
            die("all URLs must use the same host:port (wcurl never opens a second connection)");

    fd = connect_to(urls[0].host, urls[0].port);
    for (int i = 0; i < n_urls; i++)
        if (fetch(fd, &urls[i], (uint32_t)(i + 1), extra, n_extra) >= 400)
            failed = 1;
    close(fd);
    if (verbose)
        fputs("* connection closed\n", stderr);
    return failed ? 1 : 0;
}
