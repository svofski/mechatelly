#pragma once

extern int movie_frame;

int movie_open();
void movie_get_next_frame(uint8_t *dst);
