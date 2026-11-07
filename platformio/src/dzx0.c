/* frame decompressor based on ZX0 decompressor by Einar Saukas
 *
 * modifications by svofski, 2026
 * decompresses one frame at a time (2048 bytes)
 * to compress:
 *   salvador -classic -w 2048 video.pcm video.pcz
 *
 */

/*
 * ZX0 decompressor - by Einar Saukas
 * https://github.com/einar-saukas/ZX0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "dzx0.h"

#define INITIAL_OFFSET 1

static unsigned char *input_data;
static unsigned char *output_data;
static size_t input_index;
static size_t output_index;
static size_t input_size;
static size_t output_size;
static size_t partial_counter;
static int bit_mask;
static int bit_value;
static int backtrack;
static int last_byte;

// state vars between coroutine calls
static int copy_len = 0;
static int wi = 0;
static int last_offset = INITIAL_OFFSET;
static int length;
static int i;
static int state = 0;
static bool classic_mode = 0;
static bool file_truncated;

static dzx0_read_cb_t read_cb;

static int read_byte() {
    if (input_index == partial_counter) {
        input_index = 0;
        partial_counter = read_cb(input_data, DZX0_BUFFER_SIZE);
        input_size += partial_counter;
        if (partial_counter == 0) {
            file_truncated = true;
            return 0;
#if TEST
            fprintf(stderr, (input_size ? "Error: Truncated input file %s\n" : "Error: Empty input file %s\n"), input_name);
            exit(1);
#endif
        }
    }
    last_byte = input_data[input_index++];
    
    return last_byte;
}

int read_bit() {
    if (backtrack) {
        backtrack = false;
        return last_byte & 1;
    }
    bit_mask >>= 1;
    if (bit_mask == 0) {
        bit_mask = 128;
        bit_value = read_byte();
    }
    return bit_value & bit_mask ? 1 : 0;
}

static int read_interlaced_elias_gamma(bool inverted) {
    int value = 1;
    while (!read_bit()) {
        value = value << 1 | read_bit() ^ inverted;
    }
    return value;
}

int dzx0_begin(dzx0_read_cb_t _read_cb, uint8_t *output_buf, bool _classic_mode)
{
    input_data = (unsigned char *)malloc(DZX0_BUFFER_SIZE);
    if (input_data == NULL) {
        return DZX0_MALLOC_ERROR;
    }
    read_cb = _read_cb;
    classic_mode = _classic_mode;
    output_data = output_buf;
    state = 0;
    file_truncated = false;
    return 0;
}

void dzx0_end()
{
    free(input_data);
    input_data = NULL;
}

int dzx0_decompress_frame() {
    if (state < 0) return state;
    if (file_truncated) return DZX0_FILE_TRUNCATED;

    switch (state) {
        case 0:
            input_size = 0;
            input_index = 0;
            partial_counter = 0;
            output_index = 0;
            output_size = 0;
            bit_mask = 0;
            backtrack = false;

    COPY_LITERALS:
            length = read_interlaced_elias_gamma(false);
            for (i = 0; i < length; i++) {
                //write_byte(read_byte());
                uint8_t b = read_byte();
                //fprintf(stderr, "o[%d]=%02x\n", output_index, b);
                output_data[output_index++] = b;
                if (output_index == DZX0_BUFFER_SIZE) {
                    output_size += output_index;
                    output_index = 0;
                    state = 10;
                    return DZX0_BUFFER_SIZE;
                }
        case 10:
            }

            if (read_bit())
                goto COPY_FROM_NEW_OFFSET;

        /*COPY_FROM_LAST_OFFSET:*/
            length = read_interlaced_elias_gamma(false);
            //write_bytes(last_offset, length);
            {
              if (last_offset > output_size + output_index) {
                  return DZX0_INVALID_DATA;
              }
              for (copy_len = length; copy_len-- > 0;) {
                  wi = output_index - last_offset;
                  //write_byte(output_data[wi >= 0 ? wi : DZX0_BUFFER_SIZE + wi]);
                  uint8_t value = output_data[wi >= 0 ? wi : DZX0_BUFFER_SIZE + wi];
                  output_data[output_index++] = value;
                  if (output_index == DZX0_BUFFER_SIZE) {
                      // yield
                      output_size += output_index;
                      output_index = 0;
                      state = 20;
                      return DZX0_BUFFER_SIZE;
                  }
        case 20:
              }
            }

            if (!read_bit())
                goto COPY_LITERALS;

COPY_FROM_NEW_OFFSET:
            last_offset = read_interlaced_elias_gamma(!classic_mode);
            if (last_offset == 256) {
                //save_output();
                output_size += output_index;
                int last_sz = output_index;
                output_index = 0;

                if (input_index != partial_counter) {
                    state = -2;
                    return DZX0_FILE_TOO_LONG;
                }
                return last_sz;   // finished
            }
            last_offset = last_offset*128-(read_byte()>>1);
            backtrack = true;
            length = read_interlaced_elias_gamma(false)+1;
            //write_bytes(last_offset, length);
            {
              if (last_offset > output_size + output_index) {
                  return DZX0_INVALID_DATA;
              }
              for (copy_len = length; copy_len-- > 0;) {
                  wi = output_index - last_offset;
                  //write_byte(output_data[wi >= 0 ? wi : DZX0_BUFFER_SIZE + wi]);
                  uint8_t value = output_data[wi >= 0 ? wi : DZX0_BUFFER_SIZE + wi];
                  output_data[output_index++] = value;
                  if (output_index == DZX0_BUFFER_SIZE) {
                      // yield
                      output_size += output_index;
                      output_index = 0;
                      state = 30;
                      return DZX0_BUFFER_SIZE;
                  }
        case 30:
              }
            }
            //
            if (read_bit())
                goto COPY_FROM_NEW_OFFSET;
            else
                goto COPY_LITERALS;
        default:
            return DZX0_UNKNOWN_STATE;
    }
}



size_t dzx0_output_size()
{
    return output_size;
}
