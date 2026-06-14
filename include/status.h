#ifndef DAFT_STATUS_H
#define DAFT_STATUS_H

/*
 * Project-wide status codes. Every fallible function returns one of these;
 * output values travel through validated pointer arguments.
 */
typedef enum
{
    DAFT_STATUS_OK = 0,
    DAFT_STATUS_INVALID_ARGUMENT,
    DAFT_STATUS_OUT_OF_RANGE,
    DAFT_STATUS_IO_ERROR,
    DAFT_STATUS_FORMAT_ERROR,
    DAFT_STATUS_FULL,
    DAFT_STATUS_NOT_FOUND,
    DAFT_STATUS_UNAVAILABLE,
    DAFT_STATUS_INTERNAL_ERROR
} daft_status_t;

#endif /* DAFT_STATUS_H */
