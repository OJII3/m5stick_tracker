# M5Stick Tracker

M5StickS3 を肘トラッカーとして使うファームウェア。IMU データとボタン状態を
USB CDC Serial と BLE HID の両方で出力する。

## ハードウェア

- M5StickS3

## ビルド & 書き込み

```bash
pio run -e m5sticks3 -t upload --upload-port /dev/ttyACM0
```

## LCD 表示

画面上部に 4 行のステータスを常時表示する。

```
M5Stick Tracker
BT: connected
Peer: AA:BB:CC:DD:EE:FF
IMU notify: 49 Hz
```

| 行 | 意味 |
|---|---|
| `BT` | BLE の接続状態 (`advertising` / `connected`)。IMU 購読の有無は含まない。 |
| `Peer` | 接続中の相手の MAC アドレス (例: `AA:BB:CC:DD:EE:FF`)。未接続時は `-`。 |
| `IMU notify` | 直近 1 秒間に BLE IMU notify が実際に送信された回数。購読なし / 未接続は `0 Hz`。 |

ステータスは 1 秒ごとに更新する。状態・Peer・Hz のいずれにも変化がないフレームは
LCD 書き込みをスキップする。

## USB CDC Serial

115200 baud, 8N1。

### 出力 (デバイス → PC)

BLE 接続中のみ、50Hz (20ms 毎) で 1 行出力する。
`Serial.availableForWrite()` が 80 バイト未満のときはそのフレームをスキップする。

```
D,{seq},{ms},{ax},{ay},{az},{gx},{gy},{gz},{btn}
```

| フィールド | 型 | 意味 |
|-----------|----|------|
| `D` | タグ | データ行 |
| `seq` | 符号なし整数 | シーケンス番号 (0 からインクリメント) |
| `ms` | 符号なし整数 | `millis()` の値 |
| `ax`,`ay`,`az` | 浮動小数点 (小数 3 桁) | 加速度 (g, gravity=1.0) |
| `gx`,`gy`,`gz` | 浮動小数点 (小数 3 桁) | 角速度 (deg/s) |
| `btn` | 0/1 | ボタン A の状態 (0=非押下, 1=押下) |

例:

```
D,0,1234,0.010,-0.001,1.000,0.060,-0.060,-0.180,0
```

軸方向は M5Unified のデフォルト (M5StickS3 設定時) に従う。

### 起動時メッセージ

```
READY IMU:OK
READY IMU:ERR         ← IMU 未検出
BLE: advertising started
```

接続 / 切断 / 購読状態の変化もデバッグ用に Serial へ出力される:

```
BLE: connected handle=0 addr=AA:BB:CC:DD:EE:FF
BLE: imu subscribed
BLE: disconnected reason=0
BLE: imu unsubscribed
```

### 入力 (PC → デバイス)

コマンドは LF (`\n`) 区切り。CR と末尾の空白は無視する。

| コマンド | 応答 | 意味 |
|---------|------|------|
| `RESET` | `OK RESET` | IMU キャリブレーションオフセットをクリア |
| その他 | `ERR UNKNOWN:{cmd}` | 不明なコマンド |

## BLE HID

NimBLE ベースで USB HID と等価なレポートを notify する。PC 側はペアリング後
ペアリング済み BLE HID デバイスとして扱える。

- デバイス名: `M5Stick Tracker`
- アドバタイズ: HID / DeviceInfo / Battery サービス
- セキュリティ: Just-works、bonding なし、MITM なし
- リンク維持: 接続済み & IMU 未購読時は 1Hz のハートビート notify を送る

### Report ID 1: IMU (18 バイト)

相手が IMU characteristic の CCCD を有効化したとき、50Hz で notify する。
`availableForWrite` 不足時のスキップは Serial 側のみ。

| Offset | 型 | 意味 |
|--------|----|------|
| 0-1 | uint16 LE | `seq` (0 からインクリメント) |
| 2-5 | uint32 LE | `ms` (`millis()`) |
| 6-7 | int16 LE | `ax * 1000` (mg) |
| 8-9 | int16 LE | `ay * 1000` (mg) |
| 10-11 | int16 LE | `az * 1000` (mg) |
| 12-13 | int16 LE | `gx * 10` (0.1 deg/s) |
| 14-15 | int16 LE | `gy * 10` (0.1 deg/s) |
| 16-17 | int16 LE | `gz * 10` (0.1 deg/s) |

### Report ID 2: Button (1 バイト)

ボタン A の状態が変化したときだけ notify する。`0` = 非押下、`1` = 押下。

## Unity Input System 連携 (予定)

Unity 側でシリアルポートを開き、上記プロトコルで受信した IMU データを
Unity Input System のデバイスとして登録する。
