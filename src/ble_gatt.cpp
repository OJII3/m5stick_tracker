#include "ble_gatt.h"
#include <atomic>
#include <string.h>

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>

namespace {
constexpr char SERVICE_UUID[]        = "7d6a0001-8f7a-4f4f-9d4a-7f0b7a6a0001";
constexpr char SAMPLE_CHAR_UUID[]    = "7d6a0002-8f7a-4f4f-9d4a-7f0b7a6a0001";
constexpr char COMMAND_CHAR_UUID[]   = "7d6a0003-8f7a-4f4f-9d4a-7f0b7a6a0001";

NimBLEServer* pServer = nullptr;
NimBLEService* pService = nullptr;
NimBLECharacteristic* pSampleChar = nullptr;
NimBLECharacteristic* pCommandChar = nullptr;

std::atomic<bool> notifyEnabled{false};
std::atomic<uint32_t> notifyCount{0};

volatile bleGattState_t gBleState = BLE_GATT_ADVERTISING;
portMUX_TYPE gPeerMux = portMUX_INITIALIZER_UNLOCKED;
char gPeerInfo[64] = "-";

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* srv, NimBLEConnInfo& connInfo) override {
    Serial.printf("BLE: connected handle=%d addr=%s\n",
      connInfo.getConnHandle(), connInfo.getAddress().toString().c_str());

    std::string addr = connInfo.getAddress().toString();
    const char* src = addr.empty() ? "-" : addr.c_str();
    taskENTER_CRITICAL(&gPeerMux);
    strncpy(gPeerInfo, src, sizeof(gPeerInfo) - 1);
    gPeerInfo[sizeof(gPeerInfo) - 1] = '\0';
    taskEXIT_CRITICAL(&gPeerMux);
    gBleState = BLE_GATT_CONNECTED;
  }

  void onDisconnect(NimBLEServer* srv, NimBLEConnInfo& connInfo, int reason) override {
    Serial.printf("BLE: disconnected reason=%d\n", reason);
    notifyEnabled.store(false, std::memory_order_relaxed);
    gBleState = BLE_GATT_ADVERTISING;
    taskENTER_CRITICAL(&gPeerMux);
    strncpy(gPeerInfo, "-", sizeof(gPeerInfo) - 1);
    gPeerInfo[sizeof(gPeerInfo) - 1] = '\0';
    taskEXIT_CRITICAL(&gPeerMux);
    NimBLEDevice::startAdvertising();
  }
};

class SampleCharCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* chr, NimBLEConnInfo& connInfo, uint16_t subValue) override {
    bool enabled = (subValue != 0);
    notifyEnabled.store(enabled, std::memory_order_relaxed);
    Serial.printf("BLE: sample notify %s\n", enabled ? "enabled" : "disabled");
  }
};

class CommandCharCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& connInfo) override {
    std::string value = chr->getValue();
    if (value == "RESET") {
      Serial.println("OK RESET");
    } else if (!value.empty()) {
      Serial.printf("ERR UNKNOWN:%s\n", value.c_str());
    }
  }
};

ServerCallbacks serverCallbacks;
SampleCharCallbacks sampleCharCallbacks;
CommandCharCallbacks commandCharCallbacks;
} // namespace

void bleGattInit() {
  NimBLEDevice::init("M5Stick Tracker");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  // Just-works pairing, no bonding, no MITM
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityAuth(false, false, false);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(&serverCallbacks);

  pService = pServer->createService(SERVICE_UUID);

  pSampleChar = pService->createCharacteristic(
      SAMPLE_CHAR_UUID,
      NIMBLE_PROPERTY::NOTIFY);
  pSampleChar->setCallbacks(&sampleCharCallbacks);

  pCommandChar = pService->createCharacteristic(
      COMMAND_CHAR_UUID,
      NIMBLE_PROPERTY::WRITE);
  pCommandChar->setCallbacks(&commandCharCallbacks);

  pService->start();

  auto* adv = NimBLEDevice::getAdvertising();
  adv->setName("M5Stick Tracker");
  adv->setAppearance(0x03C0);
  adv->addServiceUUID(pService->getUUID());
  adv->start();

  Serial.println("BLE: advertising started");
}

bool bleGattIsConnected() {
  return pServer != nullptr && pServer->getConnectedCount() > 0;
}

bool bleGattNotifyEnabled() {
  return notifyEnabled.load(std::memory_order_relaxed);
}

void bleGattSendSample(uint16_t seq, uint32_t ms,
                       float ax, float ay, float az,
                       float gx, float gy, float gz,
                       bool buttonA) {
  if (!pSampleChar) return;

  uint8_t packet[19];

  packet[0] = seq & 0xFF;
  packet[1] = (seq >> 8) & 0xFF;

  packet[2] = ms & 0xFF;
  packet[3] = (ms >> 8) & 0xFF;
  packet[4] = (ms >> 16) & 0xFF;
  packet[5] = (ms >> 24) & 0xFF;

  auto packI16 = [&](int idx, float val, float scale) {
    float scaled = val * scale;
    if (scaled < -32768.0f) scaled = -32768.0f;
    if (scaled > 32767.0f) scaled = 32767.0f;
    int16_t s = static_cast<int16_t>(scaled);
    packet[idx] = s & 0xFF;
    packet[idx + 1] = (s >> 8) & 0xFF;
  };

  packI16(6,  ax, 1000.0f);
  packI16(8,  ay, 1000.0f);
  packI16(10, az, 1000.0f);
  packI16(12, gx, 10.0f);
  packI16(14, gy, 10.0f);
  packI16(16, gz, 10.0f);

  packet[18] = buttonA ? 1 : 0;

  pSampleChar->notify(packet, sizeof(packet));
  notifyCount.fetch_add(1, std::memory_order_relaxed);
}

bleGattState_t bleGattGetState() {
  return gBleState;
}

void bleGattGetPeerName(char* buf, size_t len) {
  if (buf == nullptr || len == 0) return;
  taskENTER_CRITICAL(&gPeerMux);
  strncpy(buf, gPeerInfo, len - 1);
  buf[len - 1] = '\0';
  taskEXIT_CRITICAL(&gPeerMux);
}

uint32_t bleGattConsumeNotifyCount() {
  return notifyCount.exchange(0, std::memory_order_relaxed);
}
