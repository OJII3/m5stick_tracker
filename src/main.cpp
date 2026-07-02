#include <M5Unified.h>
#include "ble_hid.h"

static constexpr unsigned long UPDATE_INTERVAL_MS = 20;     // 50Hz when subscribed
static constexpr unsigned long HEARTBEAT_MS = 1000;         // 1Hz heartbeat when connected but not subscribed
static constexpr int SERIAL_BAUD = 115200;

static unsigned long lastUpdate = 0;
static unsigned long lastHeartbeat = 0;
static unsigned long seq = 0;
static bool lastBtnState = false;

static void sendImuData() {
  float ax, ay, az, gx, gy, gz;
  if (!M5.Imu.getAccel(&ax, &ay, &az) || !M5.Imu.getGyro(&gx, &gy, &gz)) {
    return;
  }

  unsigned long now = millis();

  if (Serial.availableForWrite() >= 80) {
    Serial.printf("D,%lu,%lu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d\n",
      seq, now, ax, ay, az, gx, gy, gz, M5.BtnA.isPressed());
  }

  if (bleHidImuSubscribed()) {
    bleHidSendImu(seq, now, ax, ay, az, gx, gy, gz);
  } else {
    // Heartbeat: send a 1Hz notify to keep the BLE link alive
    if (now - lastHeartbeat >= HEARTBEAT_MS) {
      lastHeartbeat = now;
      bleHidForceSendImu(seq, now, ax, ay, az, gx, gy, gz);
    }
  }

  seq++;
}

static void handleButton() {
  bool cur = M5.BtnA.isPressed();
  if (cur != lastBtnState) {
    lastBtnState = cur;
    bleHidSendButton(cur);
  }
}

static void handleCommands() {
  while (Serial.available() >= 2) {
    char buf[16];
    int len = Serial.readBytesUntil('\n', buf, sizeof(buf) - 1);
    if (len <= 0) continue;
    buf[len] = '\0';

    while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == ' ')) {
      buf[--len] = '\0';
    }

    if (strcmp(buf, "RESET") == 0) {
      M5.Imu.clearOffsetData();
      Serial.println("OK RESET");
    } else {
      Serial.printf("ERR UNKNOWN:%s\n", buf);
    }
  }
}

void setup() {
  auto cfg = M5.config();
  cfg.fallback_board = m5::board_t::board_M5StickS3;
  cfg.clear_display = true;
  cfg.serial_baudrate = SERIAL_BAUD;
  M5.begin(cfg);

  M5.Lcd.setRotation(1);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextScroll(true);
  M5.Lcd.println("M5StickS3 Tracker");

  bleHidInit();
  M5.Lcd.println("BLE: started");

  bool imuOk = M5.Imu.isEnabled();
  Serial.printf("READY IMU:%s\n", imuOk ? "OK" : "ERR");
  if (!imuOk) {
    M5.Lcd.println("IMU ERR");
  }
}

void loop() {
  M5.update();
  handleCommands();
  handleButton();

  if (bleHidIsConnected()) {
    unsigned long now = millis();
    if (now - lastUpdate >= UPDATE_INTERVAL_MS) {
      lastUpdate = now;
      sendImuData();
    }
  }

  delay(10);
}
