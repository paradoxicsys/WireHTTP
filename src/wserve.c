/* wserve — Track 1: serves files from a directory using WireHTTP/1 (SPEC.md).
 *
 *   usage: ./wserve <root-dir> <port>          e.g.  ./wserve ./www 9000
 *
 * One connection at a time (the Day 1 ch.6 shape): while a client is
 * connected the next one waits in the accept queue. A connection carries as
 * many requests as the client likes and stays open until the client closes.
 * Log lines go to stderr. */
#include "proto.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *root;
static int conn_no;
static struct frame in, out;   /* 64 KiB each, so static rather than on the stack */

static const char *reason(int status)
{
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    default:  return "";
    }
}

static const char *content_type(const char *file)
{
    const char *dot = strrchr(file, '.');
    if (dot == NULL || strchr(dot, '/'))
        return "application/octet-stream";
    if (!strcmp(dot, ".html") || !strcmp(dot, ".htm")) return "text/html";
    if (!strcmp(dot, ".css"))  return "text/css";
    if (!strcmp(dot, ".js"))   return "text/javascript";
    if (!strcmp(dot, ".txt"))  return "text/plain";
    if (!strcmp(dot, ".png"))  return "image/png";
    if (!strcmp(dot, ".jpg") || !strcmp(dot, ".jpeg")) return "image/jpeg";
    return "application/octet-stream";
}

/* The response HEADERS frame: :status, content-type, content-length,
   server, date. END_STREAM is clear because DATA always follows. */
static int send_headers(int fd, uint32_t stream, int status, const char *ctype, long long length)
{
    char st[8], len[24], date[64];
    time_t now = time(NULL);
    size_t pos = 0;

    snprintf(st, sizeof st, "%d", status);
    snprintf(len, sizeof len, "%lld", length);
    strftime(date, sizeof date, "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now));
    add_field(out.payload, &pos, ":status", st);
    add_field(out.payload, &pos, "content-type", ctype);
    add_field(out.payload, &pos, "content-length", len);
    add_field(out.payload, &pos, "server", "wserve/1");
    add_field(out.payload, &pos, "date", date);

    out.type = TYPE_HEADERS;
    out.flags = 0;
    out.stream = stream;
    out.length = (uint16_t)pos;
    return send_frame(fd, &out);
}

/* 400 / 404 / 405 have the same shape as a 200: HEADERS, then one DATA
   frame with a one-line text body. The connection stays open afterwards. */
static int send_error(int fd, uint32_t stream, int status, const char *why)
{
    char body[64];
    int n = snprintf(body, sizeof body, "%d %s\n", status, reason(status));

    fprintf(stderr, "[conn %d] stream %u -> %d (%s)\n", conn_no, stream, status, why);
    if (send_headers(fd, stream, status, "text/plain", n) < 0)
        return -1;
    memcpy(out.payload, body, n);
    out.type = TYPE_DATA;
    out.flags = FLAG_END_STREAM;
    out.stream = stream;
    out.length = (uint16_t)n;
    return send_frame(fd, &out);
}

/* A whole ".." segment anywhere in the path would climb out of the root. */
static int has_dotdot(const char *path)
{
    for (const char *p = path; (p = strstr(p, "..")) != NULL; p += 2)
        if (p[-1] == '/' && (p[2] == '/' || p[2] == '\0'))
            return 1;
    return 0;
}

/* One request: the HEADERS frame is already in `in`.
   Returns -1 only if writing to the client failed. */
static int handle_request(int fd)
{
    uint32_t sid = in.stream;
    struct field f, method = {0}, path_field = {0};
    size_t pos = 0;
    int r, n_method = 0, n_path = 0;
    char path[1024], file[2048], why[1100];
    struct stat st;
    int file_fd = -1;

    if (sid == 0)
        return send_error(fd, sid, 400, "HEADERS on stream 0");
    if (!(in.flags & FLAG_END_STREAM))
        return send_error(fd, sid, 400, "request HEADERS without END_STREAM");

    while ((r = next_field(in.payload, in.length, &pos, &f)) == 1) {
        if (f.index == H_METHOD) {
            method = f;
            n_method++;
        } else if (f.index == H_PATH) {
            path_field = f;
            n_path++;
        }                       /* every other field, known or not, is ignored */
    }
    if (r < 0)
        return send_error(fd, sid, 400, "header block runs past the end of the frame");
    if (n_method != 1 || n_path != 1)
        return send_error(fd, sid, 400, "need exactly one :method and one :path");
    if (path_field.value_len == 0 || path_field.value[0] != '/' ||
        memchr(path_field.value, '\0', path_field.value_len))
        return send_error(fd, sid, 400, ":path must start with / and contain no NUL");
    if (path_field.value_len >= sizeof path)
        return send_error(fd, sid, 404, "path too long to exist");
    memcpy(path, path_field.value, path_field.value_len);
    path[path_field.value_len] = '\0';
    if (has_dotdot(path)) {
        snprintf(why, sizeof why, "%s tries to leave the root", path);
        return send_error(fd, sid, 400, why);
    }
    if (!value_is(&method, "GET")) {
        snprintf(why, sizeof why, "method %.*s on %s", (int)method.value_len, method.value, path);
        return send_error(fd, sid, 405, why);
    }

    /* Map the path to a file under the root; "/" means "/index.html". */
    if (snprintf(file, sizeof file, "%s%s%s", root, path,
                 path[strlen(path) - 1] == '/' ? "index.html" : "") >= (int)sizeof file ||
        stat(file, &st) < 0 || !S_ISREG(st.st_mode) ||
        (file_fd = open(file, O_RDONLY)) < 0) {
        snprintf(why, sizeof why, "GET %s: no such file", path);
        return send_error(fd, sid, 404, why);
    }

    if (send_headers(fd, sid, 200, content_type(file), (long long)st.st_size) < 0) {
        close(file_fd);
        return -1;
    }
    /* The body as DATA frames of up to 16 KiB; the last one has END_STREAM.
       An empty file still gets one DATA frame (Length 0, END_STREAM). */
    off_t left = st.st_size;
    int rc = 0;
    do {
        size_t chunk = left > DATA_CHUNK ? DATA_CHUNK : (size_t)left;
        if (chunk > 0 && read_exact(file_fd, out.payload, chunk) != 1) {
            chunk = 0;          /* the file shrank under us: end the body now */
            left = 0;
        } else {
            left -= chunk;
        }
        out.type = TYPE_DATA;
        out.flags = left == 0 ? FLAG_END_STREAM : 0;
        out.stream = sid;
        out.length = (uint16_t)chunk;
        if (send_frame(fd, &out) < 0) {
            rc = -1;
            break;
        }
    } while (left > 0);
    close(file_fd);

    if (rc == 0)
        fprintf(stderr, "[conn %d] stream %u GET %s -> 200 (%lld B)\n",
                conn_no, sid, path, (long long)st.st_size);
    return rc;
}

/* Read frames until the client closes. Every frame is dealt with by its
   Type; a type we do not know is already fully read, so we just log it. */
static void serve_connection(int fd)
{
    for (;;) {
        int r = recv_frame(fd, &in);
        if (r == 0)
            return;                     /* read() returned 0: client closed */
        if (r < 0) {
            fprintf(stderr, "[conn %d] connection ended in the middle of a frame\n", conn_no);
            return;
        }
        if (in.type == TYPE_HEADERS) {
            r = handle_request(fd);
        } else if (in.type == TYPE_DATA) {
            r = send_error(fd, in.stream, 400, "client sent a DATA frame");
        } else {
            fprintf(stderr, "[conn %d] skipped unknown frame type 0x%02x (%u B)\n",
                    conn_no, in.type, in.length);
            r = 0;
        }
        if (r < 0) {
            fprintf(stderr, "[conn %d] write failed: %s\n", conn_no, strerror(errno));
            return;
        }
    }
}

int main(int argc, char **argv)
{
    struct sockaddr_in addr = {0};
    struct stat st;
    int server_fd, port, yes = 1;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <root-dir> <port>\n", argv[0]);
        return 2;
    }
    root = argv[1];
    port = atoi(argv[2]);
    if (stat(root, &st) < 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "wserve: %s is not a directory\n", root);
        return 2;
    }
    if (port < 1 || port > 65535) {
        fprintf(stderr, "wserve: bad port %s\n", argv[2]);
        return 2;
    }

    signal(SIGPIPE, SIG_IGN);   /* a client that vanishes gives EPIPE, not death (Day 1 ch.10) */

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    /* restart straight away even if an old connection is in TIME_WAIT (Day 1 ch.9) */
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(server_fd, 16) < 0) {
        perror("listen");
        return 1;
    }
    fprintf(stderr, "wserve: serving %s on port %d\n", root, port);

    for (;;) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }
        conn_no++;
        /* HEADERS then DATA are two small writes in a row: without this,
           Nagle + delayed ACK would add ~40 ms per request (Day 5 ch.16). */
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof yes);
        fprintf(stderr, "[conn %d] accepted\n", conn_no);
        serve_connection(client_fd);
        close(client_fd);
        fprintf(stderr, "[conn %d] closed\n", conn_no);
    }
}
