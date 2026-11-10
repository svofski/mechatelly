#pragma once

extern int movie_frame;

int movie_begin();
int movie_next_file();
int movie_prev_file();
int movie_open_current();


void movie_advance_frame();

void movie_get_next_frame(uint8_t *dst);
void movie_get_next_frame_rgbt(uint32_t *dst);
void movie_get_next_frame_rgbt_pcm(uint32_t *dst, uint8_t *dstpcm);

void movie_clear(uint32_t *dst);
void create_bunny(uint32_t *dst);
void create_test_pattern(uint32_t *dst);
void movie_heartbeat(uint32_t *dst);

uint32_t rgbt_unpack(uint8_t rg, uint8_t bt);
uint32_t rgb_unpack(uint8_t rg, uint8_t bt);
