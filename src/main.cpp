#include <M5Unified.h>
#include "ble_hid.h"

static constexpr unsigned long UPDATE_INTERVAL_MS = 20;     // 50Hz when subscribed
static constexpr unsigned long HEARTBEAT_MS = 1000;         // 1Hz heartbeat when connected but not subscribed
static constexpr int SERIAL_BAUD = 115200;

static unsigned long lastUpdate = 0;
static unsigned long lastHeartbeat = 0;
static unsigned long seq = 0;
static bool lastBtnState = false;

static constexpr int STATUS_AREA_HEIGHT = 64;
static constexpr unsigned long STATUS_REFRESH_MS = 1000;
static unsigned long lastStatusDraw = 0;
static bleHidState_t lastDrawnState = BLE_HID_ADVERTISING;
static char lastDrawnPeer[64] = "";
static int lastDrawnHz = -1;

static void drawStatus(bool force) {
  bleHidState_t state = bleHidGetState();
  char peer[64];
  bleHidGetPeerName(peer, sizeof(peer));
  uint32_t count = bleHidConsumeNotifyCount();
  int hz = (state == BLE_HID_CONNECTED && bleHidImuSubscribed())
             ? static_cast<int>(count)
             : 0;

  if (!force && state == lastDrawnState &&
      strncmp(peer, lastDrawnPeer, sizeof(lastDrawnPeer)) == 0 &&
      hz == lastDrawnHz) {
    return;
  }

  M5.Lcd.fillRect(0, 0, M5.Lcd.width(), STATUS_AREA_HEIGHT, BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("M5Stick Tracker");
  M5.Lcd.setCursor(0, 16);
  M5.Lcd.printf("BT: %s\n", state == BLE_HID_CONNECTED ? "connected" : "advertising");
  M5.Lcd.setCursor(0, 32);
  M5.Lcd.printf("Peer: %s\n", peer);
  M5.Lcd.setCursor(0, 48);
  M5.Lcd.printf("IMU notify: %d Hz", hz);

  lastDrawnState = state;
  strncpy(lastDrawnPeer, peer, sizeof(lastDrawnPeer) - 1);
  lastDrawnPeer[sizeof(lastDrawnPeer) - 1] = '\0';
  lastDrawnHz = hz;
}

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
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("M5Stick Tracker");
  drawStatus(true);

  bleHidInit();

  bool imuOk = M5.Imu.isEnabled();
  Serial.printf("READY IMU:%s\n", imuOk ? "OK" : "ERR");
}

void loop() {
  M5.update();
  handleCommands();
  handleButton();

  unsigned long nowMs = millis();
  if (nowMs - lastStatusDraw >= STATUS_REFRESH_MS) {
    lastStatusDraw = nowMs;
    drawStatus(false);
  }

  if (bleHidIsConnected()) {
    if (nowMs - lastUpdate >= UPDATE_INTERVAL_MS) {
      lastUpdate = nowMs;
      sendImuData();
    }
  }

  delay(10);
}
