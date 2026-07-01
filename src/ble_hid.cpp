#include "ble_hid.h"
#include <Arduino.h>
#include <NimBLEDevice.h>

static NimBLECharacteristic* pImuReportChar = nullptr;
static NimBLECharacteristic* pBtnReportChar = nullptr;

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
    Serial.println("BLE: connected");
  }

  void onDisconnect(NimBLEServer* srv, NimBLEConnInfo& connInfo, int reason) override {
    Serial.println("BLE: disconnected");
    NimBLEDevice::startAdvertising();
  }
} bleServerCallbacks;

static class HidControlPointCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& connInfo) override {
    uint8_t val = chr->getValue().data()[0];
    (void)val;
  }
} hidControlPointCallbacks;

void bleHidInit() {
  NimBLEDevice::init("M5Stick Tracker");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  NimBLEServer* srv = NimBLEDevice::createServer();
  srv->setCallbacks(&bleServerCallbacks);

  NimBLEService* svc = srv->createService(NimBLEUUID("1812"));

  // Report Map characteristic
  NimBLECharacteristic* reportMapChar = svc->createCharacteristic(
    NimBLEUUID("2A4B"),
    NIMBLE_PROPERTY::READ
  );
  reportMapChar->setValue(hidReportDescriptor, sizeof(hidReportDescriptor));

  // HID Information characteristic
  NimBLECharacteristic* hidInfoChar = svc->createCharacteristic(
    NimBLEUUID("2A4A"),
    NIMBLE_PROPERTY::READ
  );
  const uint8_t hidInfo[] = { 0x01, 0x01, 0x00, 0x02 };
  hidInfoChar->setValue(hidInfo, sizeof(hidInfo));

  // HID Control Point characteristic
  NimBLECharacteristic* ctrlPtChar = svc->createCharacteristic(
    NimBLEUUID("2A4C"),
    NIMBLE_PROPERTY::WRITE_NR
  );
  ctrlPtChar->setCallbacks(&hidControlPointCallbacks);

  // Report characteristic for IMU (Report ID=1)
  pImuReportChar = svc->createCharacteristic(
    NimBLEUUID("2A4D"),
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  NimBLEDescriptor* imuRefDesc = pImuReportChar->createDescriptor(
    NimBLEUUID("2A48"),
    NIMBLE_PROPERTY::READ
  );
  const uint8_t imuRefValue[] = { 0x01, 0x01 };
  imuRefDesc->setValue(imuRefValue, sizeof(imuRefValue));

  // Report characteristic for Button (Report ID=2)
  pBtnReportChar = svc->createCharacteristic(
    NimBLEUUID("2A4D"),
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  NimBLEDescriptor* btnRefDesc = pBtnReportChar->createDescriptor(
    NimBLEUUID("2A48"),
    NIMBLE_PROPERTY::READ
  );
  const uint8_t btnRefValue[] = { 0x02, 0x01 };
  btnRefDesc->setValue(btnRefValue, sizeof(btnRefValue));

  auto* adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(0x03C0);
  adv->addServiceUUID(NimBLEUUID("1812"));
  adv->start();

  Serial.println("BLE: advertising started");
}

void bleHidSendImu(uint16_t seq, uint32_t ms,
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
    int16_t s = static_cast<int16_t>(val * scale);
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
}

void bleHidSendButton(bool pressed) {
  if (!pBtnReportChar) return;

  uint8_t report[1] = { static_cast<uint8_t>(pressed ? 1 : 0) };
  pBtnReportChar->notify(report, sizeof(report));
}
