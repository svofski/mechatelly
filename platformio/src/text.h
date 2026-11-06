#pragma once

void text_begin();
void text_clear();
void text_draw_str(int x, int y, const char *s);
void text_copy_to_frame(uint32_t *dst, int src_x, int src_y);
