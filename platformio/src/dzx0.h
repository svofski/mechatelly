#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DZX0_BUFFER_SIZE 2048  /* must be > MAX_OFFSET */

enum dzx0_error {
    DZX0_INVALID_DATA = -1,
    DZX0_FILE_TOO_LONG = -2,
    DZX0_UNKNOWN_STATE = -3,
    DZX0_FILE_TRUNCATED = -4,
    DZX0_MALLOC_ERROR = -5,
};

typedef int (*dzx0_read_cb_t)(void *, size_t);

int dzx0_begin(dzx0_read_cb_t read_cb, uint8_t *output_buf, bool _classic_mode);
void dzx0_end();

int dzx0_decompress_frame();

size_t dzx0_output_size();


#ifdef __cplusplus
}
#endif
