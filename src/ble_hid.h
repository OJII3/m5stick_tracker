#pragma once
#include <cstdint>

void bleHidInit();
void bleHidSendImu(uint16_t seq, uint32_t ms,
                   float ax, float ay, float az,
                   float gx, float gy, float gz);
void bleHidSendButton(bool pressed);
