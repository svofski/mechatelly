#pragma once

inline constexpr float GOAL_FPS = 20; // 10..40 is a good range
inline constexpr int MIN_RPS = 1;
inline constexpr int MAX_RPS = 50;

inline constexpr int NPIXELS = 32 * 32;

inline constexpr int PIN_U = 18;
inline constexpr int PIN_V = 19;
inline constexpr int PIN_W = 20;

inline constexpr int PIN_SYNC = 16;  // optical sensor for line or frame sync
inline constexpr int PIN_HALL = 17;  // hall sensor for BLDC commutation

inline constexpr int PIN_LED_R = 12;
inline constexpr int PIN_LED_G = 13;
inline constexpr int PIN_LED_B = 14;
inline constexpr int PIN_LED_T = 15;

inline constexpr int PIN_BTN_PLUS = 6;
inline constexpr int PIN_BTN_OK = 8;
inline constexpr int PIN_BTN_MINUS = 7;

extern volatile int send_pixels_delay;
extern volatile int goal_fps;
extern bool freeze_frame;
extern volatile int pingpong_rd;
extern volatile float rps;
extern uint32_t test_pixels_u32[];

extern volatile int hsync_interrupt_counter;
extern volatile int last_hsync_interrupt_counter;
extern volatile uint64_t last_sync_us;
