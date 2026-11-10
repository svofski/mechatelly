#pragma once
inline constexpr int KICKSTART_TSTEP = 60*1000;
inline constexpr int KICKSTART_DUTY = 100;
inline constexpr int MIN_DUTY = 30;
inline constexpr int MAX_DUTY = 240;

void motori_begin();
