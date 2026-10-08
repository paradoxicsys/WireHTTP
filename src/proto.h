/* proto.h — the WireHTTP/1 wire format from SPEC.md, shared by wserve and wcurl. */
#ifndef PROTO_H
#define PROTO_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define FRAME_HEADER_LEN 8
#define MAX_PAYLOAD      65535   /* the largest number a 16-bit Length can hold */
#define DATA_CHUNK       16384   /* how much wserve puts in one DATA frame */

#define TYPE_DATA        0x00
#define TYPE_HEADERS     0x01
#define FLAG_END_STREAM  0x01

/* The static table: the ten header names we actually send (SPEC §3). */
#define TABLE_SIZE 10
enum { H_METHOD = 1, H_PATH, H_AUTHORITY, H_USER_AGENT, H_ACCEPT,
       H_STATUS, H_CONTENT_TYPE, H_CONTENT_LENGTH, H_SERVER, H_DATE };

struct frame {
    uint16_t length;
    uint8_t  type;
    uint8_t  flags;
    uint32_t stream;
    uint8_t  payload[MAX_PAYLOAD];
};

/* One field of a header block. name/value point into the frame payload
   (or into the table), so nothing is copied. name is NULL for an unknown
   index (11-255), which the receiver skips. */
struct field {
    int         index;
    const char *name;
    size_t      name_len;
    const char *value;
    size_t      value_len;
};

int  read_exact(int fd, void *buf, size_t n);
int  write_all(int fd, const void *buf, size_t n);
int  send_frame(int fd, const struct frame *f);
int  recv_frame(int fd, struct frame *f);

int  add_field(uint8_t *block, size_t *pos, const char *name, const char *value);
int  next_field(const uint8_t *block, size_t len, size_t *pos, struct field *f);
int  value_is(const struct field *f, const char *s);

void dump_frame(FILE *out, const char *arrow, const struct frame *f);
void dump_fields(FILE *out, const uint8_t *block, size_t len);

#endif
