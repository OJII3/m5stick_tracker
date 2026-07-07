#pragma once
#include <cstdint>
#include <stddef.h>

enum bleGattState_t : uint8_t {
  BLE_GATT_ADVERTISING = 0,
  BLE_GATT_CONNECTED   = 1,
};

void bleGattInit();
bool bleGattIsConnected();
bool bleGattNotifyEnabled();
bleGattState_t bleGattGetState();
void bleGattGetPeerName(char* buf, size_t len);
uint32_t bleGattConsumeNotifyCount();
void bleGattSendSample(uint16_t seq, uint32_t ms,
                       float ax, float ay, float az,
                       float gx, float gy, float gz,
                       bool buttonA);
