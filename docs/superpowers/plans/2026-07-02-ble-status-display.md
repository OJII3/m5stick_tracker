# BLE Status Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** M5StickS3 の LCD に Bluetooth 接続状態 / 接続先 / BLE IMU notify 実送信 Hz を常時表示する。

**Architecture:** `ble_hid` モジュールが接続状態と peer 情報を内部に保持し、読み取り API を提供する。`main.cpp` が 1 秒ごとにその API を読み、LCD 上部 64px のステータス領域を再描画する。LCD 下部には IMU ログがスクロール表示され続ける。

**Tech Stack:** ESP32 (Arduino フレームワーク), M5Unified, NimBLE-Arduino 2.x, PlatformIO.

**Testing note:** This is embedded firmware with no host-side test framework in this project. Each task is verified by `pio run -e m5sticks3` (compile success). End-to-end visual verification is delegated to the user per spec.

---

## File Structure

| File | Responsibility |
| --- | --- |
| `src/ble_hid.h` | Public BLE HID API. 状態 enum / peer 取得 / notify カウンタ consume を追加。 |
| `src/ble_hid.cpp` | BLE 初期化と notify 送信。状態管理 (`bleHidState_t`)、peer info、notify カウンタを実装。 |
| `src/main.cpp` | LCD ステータス描画 (1Hz) と IMU ログ出力。 |
| `README.md` | LCD 表示仕様を追記。 |

---

## Task 1: Extend `ble_hid.h` with state and counters

**Files:**
- Modify: `src/ble_hid.h`

- [ ] **Step 1: Replace the file with the new public API**

Write the following to `src/ble_hid.h`:

```cpp
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
```

- [ ] **Step 2: Compile-check header changes**

Run: `pio run -e m5sticks3`
Expected: compile FAILS (symbols still missing in `ble_hid.cpp`). That's expected at this stage; the header is the source of truth.

- [ ] **Step 3: Commit**

```bash
git add src/ble_hid.h
git commit -m "feat(ble_hid): add state, peer name, and notify count APIs to header"
```

---

## Task 2: Implement state tracking and notify counter in `ble_hid.cpp`

**Files:**
- Modify: `src/ble_hid.cpp`

- [ ] **Step 1: Add includes and internal state at the top of the file**

After `#include "ble_hid.h"` add:

```cpp
#include <atomic>
#include <string.h>
```

Replace the existing `static volatile bool imuSubscribed = false;` and `static volatile bool btnSubscribed = false;` block with:

```cpp
static volatile bool imuSubscribed = false;
static volatile bool btnSubscribed = false;

static volatile bleHidState_t gBleState = BLE_HID_ADVERTISING;
static portMUX_TYPE gPeerMux = portMUX_INITIALIZER_UNLOCKED;
static char gPeerInfo[64] = "-";
static std::atomic<uint32_t> gNotifyCount{0};
```

- [ ] **Step 2: Save peer info on connect, clear on disconnect**

Replace the body of `onConnect` with:

```cpp
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
```

Replace the body of `onDisconnect` with:

```cpp
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
```

- [ ] **Step 3: Add the new public API implementations**

Append the following to the end of `ble_hid.cpp`:

```cpp
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
```

- [ ] **Step 4: Increment the counter when notify actually fires**

In `bleHidSendImu`, after the existing `pImuReportChar->notify(report, sizeof(report));` line, add:

```cpp
  gNotifyCount.fetch_add(1, std::memory_order_relaxed);
```

The notify result is already ignored (current behavior). Counting on the call site is sufficient — when not subscribed the function early-returns above this line, so the counter only reflects successful notify calls.

- [ ] **Step 5: Build and verify compile success**

Run: `pio run -e m5sticks3`
Expected: BUILD SUCCESS. Warnings about unused variables are acceptable; errors are not.

- [ ] **Step 6: Commit**

```bash
git add src/ble_hid.cpp
git commit -m "feat(ble_hid): track connection state, peer info, and IMU notify count"
```

---

## Task 3: Add LCD status display to `main.cpp`

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add the draw helpers and refresh state**

After the existing `static bool lastBtnState = false;` line, add:

```cpp
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
  int hz = static_cast<int>(count);

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
```

- [ ] **Step 2: Initialize the status area in `setup()`**

Replace the existing block:

```cpp
  M5.Lcd.setRotation(1);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextScroll(true);
  M5.Lcd.println("M5Stick Tracker");
```

with:

```cpp
  M5.Lcd.setRotation(1);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextScroll(true);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("M5Stick Tracker");
  M5.Lcd.setCursor(0, STATUS_AREA_HEIGHT);
  M5.Lcd.println("IMU:");
  drawStatus(true);
```

Also remove the now-redundant `M5.Lcd.println("BLE: started");` and the IMU error prints inside the `if (!imuOk)` branch — the status area now drives BLE visibility, and IMU errors are not in scope of this change. The final `if (!imuOk)` block becomes a single Serial line, no LCD write:

```cpp
  bool imuOk = M5.Imu.isEnabled();
  Serial.printf("READY IMU:%s\n", imuOk ? "OK" : "ERR");
```

(`bleHidInit()` itself already prints `BLE: advertising started` to Serial — that is the source of truth and the LCD shows it via `BT: advertising`.)

- [ ] **Step 3: Add the periodic refresh in `loop()`**

Replace the body of `loop()` with:

```cpp
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
```

The `drawStatus(false)` call at the start of every second is the 1Hz refresh; it short-circuits when nothing changed, so the LCD write cost is bounded.

- [ ] **Step 4: Build and verify**

Run: `pio run -e m5sticks3`
Expected: BUILD SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat(ui): render BLE status (BT state, peer, IMU notify Hz) on LCD"
```

---

## Task 4: Document the LCD behavior in `README.md`

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add a "## LCD 表示" section before the existing "## 通信プロトコル" section**

Insert the following block right after the "## ビルド & 書き込み" section (i.e. immediately above "## 通信プロトコル"):

```markdown
## LCD 表示

画面上部に 4 行のステータスを常時表示する。下部は IMU ログのスクロール領域。

```
M5Stick Tracker
BT: connected
Peer: iPhone
IMU notify: 49 Hz
```

| 行 | 意味 |
|---|---|
| `BT` | Bluetooth の接続状態 (`advertising` / `connected`)。IMU 購読の有無は含まない。 |
| `Peer` | 接続中の相手名 (取れれば名前、無ければ `AA:BB:CC:DD:EE:FF` の MAC、未接続時は `-`)。 |
| `IMU notify` | 直近 1 秒間に BLE IMU notify が実際に送信された回数。購読なしは `0 Hz`。 |

ステータスは 1 秒ごとに更新する。変化がないフレームは LCD 書き込みをスキップする。
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: document LCD status display"
```

---

## Task 5: Final build verification

**Files:**
- none (verification only)

- [ ] **Step 1: Clean build**

Run: `pio run -e m5sticks3 -t clean && pio run -e m5sticks3`
Expected: BUILD SUCCESS. No new warnings introduced beyond what already existed before this change.

- [ ] **Step 2: Confirm the new symbols are present in the binary**

Run: `nm .pio/build/m5sticks3/firmware.elf | rg -e 'bleHidGetState|bleHidGetPeerName|bleHidConsumeNotifyCount|drawStatus'`
Expected: each symbol is listed (non-empty output). This is a smoke test that confirms the new code wasn't accidentally dropped by the preprocessor.

- [ ] **Step 3: Commit (no changes expected, just verify clean tree)**

Run: `git status`
Expected: `nothing to commit, working tree clean`.

---

## Out of scope

- GAP 経由での peer name 取得 (NimBLE 2.x に `NimBLEConnInfo::getPeerName()` が無いため、MAC のみで初期実装とする)。
- ボタン操作での LCD ページ切替、Battery 残量表示、画面遷移。
- Serial プロトコルの変更。
