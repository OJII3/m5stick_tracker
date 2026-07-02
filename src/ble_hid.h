#pragma once
#include <cstdint>
#include <stddef.h>

enum bleHidState_t : uint8_t {
  BLE_HID_ADVERTISING = 0,
  BLE_HID_CONNECTED   = 1,
};

void bleHidInit();
bool bleHidIsConnected();
bool bleHidImuSubscribed();
bleHidState_t bleHidGetState();
void bleHidGetPeerName(char* buf, size_t len);
uint32_t bleHidConsumeNotifyCount();
void bleHidSendImu(uint16_t seq, uint32_t ms,
                   float ax, float ay, float az,
                   float gx, float gy, float gz);
void bleHidForceSendImu(uint16_t seq, uint32_t ms,
                        float ax, float ay, float az,
                        float gx, float gy, float gz);
void bleHidSendButton(bool pressed);
