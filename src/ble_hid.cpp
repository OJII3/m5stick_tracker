#include "ble_hid.h"
#include <atomic>
#include <string.h>

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

static NimBLEServer* pServer = nullptr;
static NimBLEHIDDevice* pHid = nullptr;
static NimBLECharacteristic* pImuReportChar = nullptr;
static NimBLECharacteristic* pBtnReportChar = nullptr;

static volatile bool imuSubscribed = false;
static volatile bool btnSubscribed = false;

static volatile bleHidState_t gBleState = BLE_HID_ADVERTISING;
static portMUX_TYPE gPeerMux = portMUX_INITIALIZER_UNLOCKED;
static char gPeerInfo[64] = "-";
static std::atomic<uint32_t> gNotifyCount{0};

static const uint8_t hidReportDescriptor[] = {
  0x06, 0x00, 0xFF,
  0x09, 0x01,
  0xA1, 0x01,
  0x85, 0x01,

  0x09, 0x02,
  0x15, 0x00,
  0x26, 0xFF, 0xFF,
  0x75, 0x10,
  0x95, 0x01,
  0x81, 0x02,

  0x09, 0x03,
  0x15, 0x00,
  0x27, 0xFF, 0xFF, 0xFF, 0xFF,
  0x75, 0x20,
  0x95, 0x01,
  0x81, 0x02,

  0x09, 0x04,
  0x09, 0x05,
  0x09, 0x06,
  0x16, 0x00, 0x80,
  0x26, 0xFF, 0x7F,
  0x36, 0x80, 0xC1,
  0x46, 0x80, 0x3E,
  0x75, 0x10,
  0x95, 0x03,
  0x81, 0x02,

  0x09, 0x07,
  0x09, 0x08,
  0x09, 0x09,
  0x16, 0x00, 0x80,
  0x26, 0xFF, 0x7F,
  0x36, 0xE0, 0xB1,
  0x46, 0x20, 0x4E,
  0x75, 0x10,
  0x95, 0x03,
  0x81, 0x02,

  0xC0,

  0x06, 0x00, 0xFF,
  0x09, 0x0A,
  0xA1, 0x01,
  0x85, 0x02,

  0x09, 0x0B,
  0x15, 0x00,
  0x25, 0x01,
  0x75, 0x08,
  0x95, 0x01,
  0x81, 0x02,

  0xC0
};

static class BleServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* srv, NimBLEConnInfo& connInfo) override {
    Serial.printf("BLE: connected handle=%d addr=%s\n",
      connInfo.getConnHandle(), connInfo.getAddress().toString().c_str());

    std::string addr = connInfo.getAddress().toString();
    const char* src = addr.empty() ? "-" : addr.c_str();
    taskENTER_CRITICAL(&gPeerMux);
    strncpy(gPeerInfo, src, sizeof(gPeerInfo) - 1);
    gPeerInfo[sizeof(gPeerInfo) - 1] = '\0';
    taskEXIT_CRITICAL(&gPeerMux);
    gBleState = BLE_HID_CONNECTED;
  }

  void onDisconnect(NimBLEServer* srv, NimBLEConnInfo& connInfo, int reason) override {
    Serial.printf("BLE: disconnected reason=%d\n", reason);
    imuSubscribed = false;
    btnSubscribed = false;
    gBleState = BLE_HID_ADVERTISING;
    taskENTER_CRITICAL(&gPeerMux);
    strncpy(gPeerInfo, "-", sizeof(gPeerInfo) - 1);
    gPeerInfo[sizeof(gPeerInfo) - 1] = '\0';
    taskEXIT_CRITICAL(&gPeerMux);
    NimBLEDevice::startAdvertising();
  }
} bleServerCallbacks;

static class InputReportCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* chr, NimBLEConnInfo& connInfo, uint16_t subValue) override {
    bool subscribed = (subValue != 0);
    if (chr == pImuReportChar) {
      imuSubscribed = subscribed;
      Serial.printf("BLE: imu %s\n", subscribed ? "subscribed" : "unsubscribed");
    } else if (chr == pBtnReportChar) {
      btnSubscribed = subscribed;
      Serial.printf("BLE: btn %s\n", subscribed ? "subscribed" : "unsubscribed");
    }
  }
} inputReportCallbacks;

void bleHidInit() {
  NimBLEDevice::init("M5Stick Tracker");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  // Just-works pairing, no bonding, no MITM
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityAuth(false, false, false);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(&bleServerCallbacks);

  pHid = new NimBLEHIDDevice(pServer);

  pHid->setManufacturer("M5Stack");
  pHid->setPnp(0x02, 0x1234, 0x0001, 0x0100);
  pHid->setHidInfo(0x00, 0x02);
  pHid->setReportMap((uint8_t*)hidReportDescriptor, sizeof(hidReportDescriptor));

  pImuReportChar = pHid->getInputReport(1);
  pBtnReportChar = pHid->getInputReport(2);
  pImuReportChar->setCallbacks(&inputReportCallbacks);
  pBtnReportChar->setCallbacks(&inputReportCallbacks);

  auto* adv = NimBLEDevice::getAdvertising();
  adv->setName("M5Stick Tracker");
  adv->setAppearance(0x03C0);
  adv->addServiceUUID(pHid->getHidService()->getUUID());
  adv->addServiceUUID(pHid->getDeviceInfoService()->getUUID());
  adv->addServiceUUID(pHid->getBatteryService()->getUUID());
  adv->start();

  Serial.println("BLE: advertising started");
}

bool bleHidIsConnected() {
  return pServer != nullptr && pServer->getConnectedCount() > 0;
}

bool bleHidImuSubscribed() {
  return imuSubscribed;
}

void bleHidSendImu(uint16_t seq, uint32_t ms,
                   float ax, float ay, float az,
                   float gx, float gy, float gz) {
  if (!pImuReportChar || !imuSubscribed) return;

  uint8_t report[18];

  report[0] = seq & 0xFF;
  report[1] = (seq >> 8) & 0xFF;

  report[2] = ms & 0xFF;
  report[3] = (ms >> 8) & 0xFF;
  report[4] = (ms >> 16) & 0xFF;
  report[5] = (ms >> 24) & 0xFF;

  auto packI16 = [&](int idx, float val, float scale) {
    float scaled = val * scale;
    if (scaled < -32768.0f) scaled = -32768.0f;
    if (scaled > 32767.0f) scaled = 32767.0f;
    int16_t s = static_cast<int16_t>(scaled);
    report[idx] = s & 0xFF;
    report[idx + 1] = (s >> 8) & 0xFF;
  };

  packI16(6,  ax, 1000.0f);
  packI16(8,  ay, 1000.0f);
  packI16(10, az, 1000.0f);
  packI16(12, gx, 10.0f);
  packI16(14, gy, 10.0f);
  packI16(16, gz, 10.0f);

  pImuReportChar->notify(report, sizeof(report));
  gNotifyCount.fetch_add(1, std::memory_order_relaxed);
}

void bleHidSendButton(bool pressed) {
  if (!pBtnReportChar || !btnSubscribed) return;

  uint8_t report[1] = { static_cast<uint8_t>(pressed ? 1 : 0) };
  pBtnReportChar->notify(report, sizeof(report));
}

void bleHidForceSendImu(uint16_t seq, uint32_t ms,
                        float ax, float ay, float az,
                        float gx, float gy, float gz) {
  if (!pImuReportChar) return;

  uint8_t report[18];

  report[0] = seq & 0xFF;
  report[1] = (seq >> 8) & 0xFF;

  report[2] = ms & 0xFF;
  report[3] = (ms >> 8) & 0xFF;
  report[4] = (ms >> 16) & 0xFF;
  report[5] = (ms >> 24) & 0xFF;

  auto packI16 = [&](int idx, float val, float scale) {
    float scaled = val * scale;
    if (scaled < -32768.0f) scaled = -32768.0f;
    if (scaled > 32767.0f) scaled = 32767.0f;
    int16_t s = static_cast<int16_t>(scaled);
    report[idx] = s & 0xFF;
    report[idx + 1] = (s >> 8) & 0xFF;
  };

  packI16(6,  ax, 1000.0f);
  packI16(8,  ay, 1000.0f);
  packI16(10, az, 1000.0f);
  packI16(12, gx, 10.0f);
  packI16(14, gy, 10.0f);
  packI16(16, gz, 10.0f);

  // notify() will return false if not subscribed, but the call itself is harmless
  pImuReportChar->notify(report, sizeof(report));
}

bleHidState_t bleHidGetState() {
  return gBleState;
}

void bleHidGetPeerName(char* buf, size_t len) {
  if (buf == nullptr || len == 0) return;
  taskENTER_CRITICAL(&gPeerMux);
  strncpy(buf, gPeerInfo, len - 1);
  buf[len - 1] = '\0';
  taskEXIT_CRITICAL(&gPeerMux);
}

uint32_t bleHidConsumeNotifyCount() {
  return gNotifyCount.exchange(0, std::memory_order_relaxed);
}
