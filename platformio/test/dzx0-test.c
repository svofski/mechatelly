#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "dzx0.h"

FILE * ifp;
FILE * ofp;

static char *input_name;
static char *output_name;

uint8_t frame_buf[DZX0_BUFFER_SIZE];

// typedef int (*dzx0_read_cb_t)(void *, size_t);
int read_cb(void *buf, size_t size)
{
    return fread(buf, sizeof(char), size, ifp);
}

int main(int argc, char *argv[]) {
    bool forced_mode = false;
    bool classic_mode = false;
    size_t input_size;
    int i;

    printf("DZX0 v2.2: Data decompressor by Einar Saukas\n");

    /* process hidden optional parameters */
    for (i = 1; i < argc && *argv[i] == '-'; i++) {
        if (!strcmp(argv[i], "-f")) {
            forced_mode = true;
        } else if (!strcmp(argv[i], "-c")) {
            classic_mode = true;
            fprintf(stderr, "CLASSIC MODE\n");
        } else {
            fprintf(stderr, "Error: Invalid parameter %s\n", argv[i]);
            exit(1);
        }
    }

    /* determine output filename */
    if (argc == i+1) {
        input_name = argv[i];
        input_size = strlen(input_name);
        if (input_size > 4 && !strcmp(input_name+input_size-4, ".zx0")) {
            input_size = strlen(input_name);
            output_name = (char *)malloc(input_size);
            strcpy(output_name, input_name);
            output_name[input_size-4] = '\0';
        } else {
            fprintf(stderr, "Error: Cannot infer output filename\n");
            exit(1);
        }
    } else if (argc == i+2) {
        input_name = argv[i];
        output_name = argv[i+1];
    } else {
        fprintf(stderr, "Usage: %s [-f] [-c] input.zx0 [output]\n"
                        "  -f      Force overwrite of output file\n"
                        "  -c      Classic file format (v1.*)\n", argv[0]);
        exit(1);
    }

    /* open input file */
    ifp = fopen(input_name, "rb");
    if (!ifp) {
        fprintf(stderr, "Error: Cannot access input file %s\n", input_name);
        exit(1);
    }

    /* check output file */
    if (!forced_mode && fopen(output_name, "rb") != NULL) {
        fprintf(stderr, "Error: Already existing output file %s\n", output_name);
        exit(1);
    }

    /* create output file */
    ofp = fopen(output_name, "wb");
    if (!ofp) {
        fprintf(stderr, "Error: Cannot create output file %s\n", output_name);
        exit(1);
    }

    // setup decoder: read_cb provides input, writes to output_buf, classic optional
    dzx0_begin(read_cb, frame_buf, classic_mode);

    /* generate output file */
    int chunk_sz;
    do {
        chunk_sz = dzx0_decompress_frame();
        //fprintf(stderr, "Chunk: %d bytes state=%d copy_len=%d length=%d wi=%d\n", 
        //        chunk_sz, state, copy_len, length, wi);
        fwrite(frame_buf, chunk_sz, 1, ofp);
    } while (chunk_sz == DZX0_BUFFER_SIZE);

    dzx0_end();

    /* close input file */
    fclose(ifp);

    /* close output file */
    fclose(ofp);

    /* done! */
    printf("File decompressed from %lu to %lu bytes!\n", (unsigned long)input_size, (unsigned long)dzx0_output_size());

    return 0;
}

