#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "daft_util.h"

#define DAFT_UTIL_MAX_WRITE_ATTEMPTS 16u
#define DAFT_UTIL_MAX_READ_ATTEMPTS 16u

daft_status_t daft_util_write_all(int fd, const uint8_t *buf, size_t len)
{
    size_t off = 0u;
    unsigned int attempts = 0u;
    daft_status_t status = DAFT_STATUS_OK;

    if ((buf == NULL) || (fd < 0))
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    /* Bounded by DAFT_UTIL_MAX_WRITE_ATTEMPTS iterations. */
    while ((status == DAFT_STATUS_OK) && (off < len) &&
           (attempts < DAFT_UTIL_MAX_WRITE_ATTEMPTS))
    {
        ssize_t n = write(fd, &buf[off], len - off);
        if (n > 0)
        {
            off += (size_t)n;
        }
        else
        {
            int err = errno;
            if ((n != -1) || (err != EINTR))
            {
                status = DAFT_STATUS_IO_ERROR;
            }
        }
        attempts++;
    }

    if ((status == DAFT_STATUS_OK) && (off != len))
    {
        status = DAFT_STATUS_IO_ERROR;
    }
    return status;
}

daft_status_t daft_util_read_file(const char *path, char *buf, size_t cap,
                                  size_t *out_len)
{
    int fd;
    size_t off = 0u;
    daft_status_t status = DAFT_STATUS_OK;

    if ((path == NULL) || (buf == NULL) || (out_len == NULL) || (cap < 2u))
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        return DAFT_STATUS_IO_ERROR;
    }

    /* Bounded by DAFT_UTIL_MAX_READ_ATTEMPTS iterations. */
    {
        int done = 0;
        unsigned int attempts = 0u;

        while ((done == 0) && (off < (cap - 1u)) &&
               (attempts < DAFT_UTIL_MAX_READ_ATTEMPTS))
        {
            ssize_t n = read(fd, &buf[off], (cap - 1u) - off);
            if (n > 0)
            {
                off += (size_t)n;
            }
            else if (n == 0)
            {
                done = 1; /* end of file */
            }
            else
            {
                int err = errno;
                if (err != EINTR)
                {
                    status = DAFT_STATUS_IO_ERROR;
                    done = 1;
                }
            }
            attempts++;
        }
    }

    if (close(fd) != 0)
    {
        status = DAFT_STATUS_IO_ERROR;
    }

    buf[off] = '\0';
    *out_len = off;
    return status;
}

daft_status_t daft_util_skip_ws(const char *buf, size_t len, size_t *pos)
{
    if ((buf == NULL) || (pos == NULL))
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    /* Bounded by len. */
    while ((*pos < len) && ((buf[*pos] == ' ') || (buf[*pos] == '\t')))
    {
        (*pos)++;
    }
    return DAFT_STATUS_OK;
}

daft_status_t daft_util_parse_u64(const char *buf, size_t len, size_t *pos,
                                  uint64_t *out)
{
    uint64_t value = 0u;
    size_t digits = 0u;

    if ((buf == NULL) || (pos == NULL) || (out == NULL))
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    /* Bounded by len. */
    while (*pos < len)
    {
        char c = buf[*pos];
        if ((c < '0') || (c > '9'))
        {
            break;
        }
        if (value > ((UINT64_MAX - 9u) / 10u))
        {
            return DAFT_STATUS_OUT_OF_RANGE;
        }
        value = (value * 10u) + ((uint64_t)c - (uint64_t)'0');
        digits++;
        (*pos)++;
    }

    if (digits == 0u)
    {
        return DAFT_STATUS_FORMAT_ERROR;
    }

    *out = value;
    return DAFT_STATUS_OK;
}

daft_status_t daft_util_u32_to_dec(char *buf, size_t cap, uint32_t value,
                                   size_t *out_len)
{
    char tmp[10];
    size_t n = 0u;
    size_t i;
    uint32_t rest = value;

    if ((buf == NULL) || (out_len == NULL) || (cap < 11u))
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    /* Bounded: a uint32_t has at most 10 decimal digits. */
    do
    {
        uint32_t digit = (rest % 10u) + (uint32_t)'0';
        tmp[n] = (char)digit;
        rest /= 10u;
        n++;
    } while ((rest > 0u) && (n < 10u));

    for (i = 0u; i < n; i++)
    {
        buf[i] = tmp[n - 1u - i];
    }
    buf[n] = '\0';
    *out_len = n;
    return DAFT_STATUS_OK;
}

daft_status_t daft_util_str_copy(char *dst, size_t dst_cap, const char *src,
                                 size_t src_limit)
{
    size_t i = 0u;

    if ((dst == NULL) || (src == NULL) || (dst_cap == 0u))
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    /* Bounded by src_limit and dst_cap. */
    while ((i < src_limit) && (src[i] != '\0'))
    {
        if (i >= (dst_cap - 1u))
        {
            dst[0] = '\0';
            return DAFT_STATUS_OUT_OF_RANGE;
        }
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return DAFT_STATUS_OK;
}
