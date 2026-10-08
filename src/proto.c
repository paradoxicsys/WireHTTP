/* proto.c — reading, writing and printing WireHTTP/1 frames (SPEC.md). */
#include "proto.h"

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

static const char *TABLE[TABLE_SIZE + 1] = {
    NULL, ":method", ":path", ":authority", "user-agent", "accept",
    ":status", "content-type", "content-length", "server", "date"
};

static int table_index(const char *name)
{
    for (int i = 1; i <= TABLE_SIZE; i++)
        if (strcmp(TABLE[i], name) == 0)
            return i;
    return 0;
}

/* TCP is a byte stream (Day 1 ch.7): one read() may return only part of a
   frame, so keep reading until we have all n bytes.
   Returns 1 = got n bytes, 0 = clean EOF before the first byte,
   -1 = error, or EOF part-way through. */
int read_exact(int fd, void *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, (char *)buf + got, n - got);
        if (r < 0 && errno == EINTR)
            continue;
        if (r < 0)
            return -1;
        if (r == 0)
            return got == 0 ? 0 : -1;
        got += r;
    }
    return 1;
}

/* write() can also send less than we asked for, so loop. */
int write_all(int fd, const void *buf, size_t n)
{
    size_t sent = 0;
    while (sent < n) {
        ssize_t w = write(fd, (const char *)buf + sent, n - sent);
        if (w < 0 && errno == EINTR)
            continue;
        if (w < 0)
            return -1;
        sent += w;
    }
    return 0;
}

/* The 8-byte header, in network byte order (Day 1 ch.11). */
static void encode_header(const struct frame *f, uint8_t h[FRAME_HEADER_LEN])
{
    uint16_t len = htons(f->length);
    uint32_t sid = htonl(f->stream);
    memcpy(h, &len, 2);
    h[2] = f->type;
    h[3] = f->flags;
    memcpy(h + 4, &sid, 4);
}

/* Header and payload go out in ONE write, so a frame is never split into a
   tiny segment followed by the rest (see Nagle, Day 5 ch.16). */
int send_frame(int fd, const struct frame *f)
{
    static uint8_t buf[FRAME_HEADER_LEN + MAX_PAYLOAD];
    encode_header(f, buf);
    memcpy(buf + FRAME_HEADER_LEN, f->payload, f->length);
    return write_all(fd, buf, FRAME_HEADER_LEN + f->length);
}

/* Returns 1 = got a frame, 0 = peer closed between frames, -1 = error or
   the connection ended in the middle of a frame. */
int recv_frame(int fd, struct frame *f)
{
    uint8_t h[FRAME_HEADER_LEN];
    uint16_t len;
    uint32_t sid;
    int r = read_exact(fd, h, sizeof h);
    if (r <= 0)
        return r;
    memcpy(&len, h, 2);
    memcpy(&sid, h + 4, 4);
    f->length = ntohs(len);
    f->type   = h[2];
    f->flags  = h[3];
    f->stream = ntohl(sid);
    /* Every 16-bit length fits in payload[], so there is nothing to reject. */
    return read_exact(fd, f->payload, f->length) == 1 ? 1 : -1;
}

/* Append one field to a header block. A name in the table costs one byte;
   any other name is sent as a literal: index 0, NameLen, Name.
   Returns -1 if the field does not fit in one frame. */
int add_field(uint8_t *block, size_t *pos, const char *name, const char *value)
{
    size_t nlen = strlen(name), vlen = strlen(value);
    int idx = table_index(name);
    size_t need = 1 + (idx ? 0 : 1 + nlen) + 2 + vlen;
    uint16_t vlen16;
    uint8_t *p;

    if (nlen == 0 || nlen > 255 || vlen > 0xffff || *pos + need > MAX_PAYLOAD)
        return -1;
    p = block + *pos;
    *p++ = (uint8_t)idx;
    if (idx == 0) {
        *p++ = (uint8_t)nlen;
        memcpy(p, name, nlen);
        p += nlen;
    }
    vlen16 = htons((uint16_t)vlen);
    memcpy(p, &vlen16, 2);
    p += 2;
    memcpy(p, value, vlen);
    *pos += need;
    return 0;
}

/* Read the field starting at *pos. Returns 1 = got a field, 0 = end of the
   block, -1 = malformed (a length runs past the end, or NameLen is 0). */
int next_field(const uint8_t *block, size_t len, size_t *pos, struct field *f)
{
    size_t p = *pos;
    uint16_t vlen16;

    if (p == len)
        return 0;
    f->index = block[p++];
    if (f->index == 0) {                     /* literal name */
        if (p >= len)
            return -1;
        f->name_len = block[p++];
        if (f->name_len == 0 || p + f->name_len > len)
            return -1;
        f->name = (const char *)block + p;
        p += f->name_len;
    } else if (f->index <= TABLE_SIZE) {     /* name from the table */
        f->name = TABLE[f->index];
        f->name_len = strlen(f->name);
    } else {                                 /* 11-255: reserved, skip it */
        f->name = NULL;
        f->name_len = 0;
    }
    if (p + 2 > len)
        return -1;
    memcpy(&vlen16, block + p, 2);
    f->value_len = ntohs(vlen16);
    p += 2;
    if (p + f->value_len > len)
        return -1;
    f->value = (const char *)block + p;
    *pos = p + f->value_len;
    return 1;
}

int value_is(const struct field *f, const char *s)
{
    return f->value_len == strlen(s) && memcmp(f->value, s, f->value_len) == 0;
}

static const char *type_name(uint8_t type)
{
    if (type == TYPE_DATA)
        return "DATA";
    if (type == TYPE_HEADERS)
        return "HEADERS";
    return NULL;
}

/* -v output: a summary line, then the whole frame (header + payload) as
   16 bytes per line in hex with an ASCII column, like tcpdump -X. */
void dump_frame(FILE *out, const char *arrow, const struct frame *f)
{
    uint8_t h[FRAME_HEADER_LEN];
    size_t total = FRAME_HEADER_LEN + f->length;
    const char *name = type_name(f->type);

    encode_header(f, h);
    if (name)
        fprintf(out, "%s %s", arrow, name);
    else
        fprintf(out, "%s UNKNOWN(0x%02x)", arrow, f->type);
    fprintf(out, " len=%u flags=0x%02x%s stream=%u\n", f->length, f->flags,
            (f->flags & FLAG_END_STREAM) ? "(END_STREAM)" : "", f->stream);

    for (size_t off = 0; off < total; off += 16) {
        fprintf(out, "   %04zx  ", off);
        for (size_t i = off; i < off + 16; i++) {
            if (i < total)
                fprintf(out, "%02x ", i < FRAME_HEADER_LEN ? h[i] : f->payload[i - FRAME_HEADER_LEN]);
            else
                fputs("   ", out);
            if (i == off + 7)
                fputc(' ', out);
        }
        fputs(" |", out);
        for (size_t i = off; i < off + 16 && i < total; i++) {
            uint8_t c = i < FRAME_HEADER_LEN ? h[i] : f->payload[i - FRAME_HEADER_LEN];
            fputc(c >= 32 && c < 127 ? c : '.', out);
        }
        fputs("|\n", out);
    }
}

/* -v output: the decoded fields of a header block. */
void dump_fields(FILE *out, const uint8_t *block, size_t len)
{
    size_t pos = 0;
    struct field f;
    int r;

    while ((r = next_field(block, len, &pos, &f)) == 1) {
        if (f.index == 0)
            fprintf(out, "   [lit] %.*s: %.*s\n", (int)f.name_len, f.name,
                    (int)f.value_len, f.value);
        else if (f.name)
            fprintf(out, "   [%d] %s: %.*s\n", f.index, f.name,
                    (int)f.value_len, f.value);
        else
            fprintf(out, "   [%d] unknown index, field skipped (%zu-byte value)\n",
                    f.index, f.value_len);
    }
    if (r < 0)
        fputs("   (malformed header block)\n", out);
}
