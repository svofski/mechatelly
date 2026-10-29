#pragma once

extern int movie_frame;

int movie_open();
void movie_get_next_frame(uint8_t *dst);
void movie_get_next_frame_rgbt(uint32_t *dst);

void create_bunny(uint32_t *dst);
void create_test_pattern(uint32_t *dst);
