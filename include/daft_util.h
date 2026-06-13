#ifndef DAFT_UTIL_H
#define DAFT_UTIL_H

#include <stddef.h>
#include <stdint.h>

#include "daft_status.h"

/* Bounded write of an entire buffer to a file descriptor (EINTR-safe,
 * bounded retry count). */
daft_status_t daft_util_write_all(int fd, const uint8_t *buf, size_t len);

/* Read up to cap - 1 bytes of a file into buf (NUL-terminated), with a
 * bounded read loop. Files larger than the buffer are truncated; *out_len
 * receives the number of bytes stored. */
daft_status_t daft_util_read_file(const char *path, char *buf, size_t cap,
                                  size_t *out_len);

/* Advance *pos past spaces and horizontal tabs (never past len). */
daft_status_t daft_util_skip_ws(const char *buf, size_t len, size_t *pos);

/* Parse an unsigned decimal integer at *pos, advancing *pos. Fails with
 * DAFT_STATUS_FORMAT_ERROR if no digit is present, DAFT_STATUS_OUT_OF_RANGE
 * on overflow. */
daft_status_t daft_util_parse_u64(const char *buf, size_t len, size_t *pos,
                                  uint64_t *out);

/* Render value as decimal into buf (capacity cap, NUL-terminated);
 * *out_len receives the digit count. */
daft_status_t daft_util_u32_to_dec(char *buf, size_t cap, uint32_t value,
                                   size_t *out_len);

/* Bounded string copy: fails with DAFT_STATUS_OUT_OF_RANGE if src (up to
 * src_limit bytes) does not fit in dst including the terminator. */
daft_status_t daft_util_str_copy(char *dst, size_t dst_cap, const char *src,
                                 size_t src_limit);

#endif /* DAFT_UTIL_H */
