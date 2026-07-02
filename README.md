# M5Stick Tracker

M5StickS3 を肘トラッカーとして使うファームウェア。

## ハードウェア

- M5StickS3 (ESP32-S3-PICO-1)
- IMU: MPU6886 (内部 I2C: SCL=GPIO48, SDA=GPIO47)
- ボタン A (GPIO41)
- 赤色 LED (GPIO27, active low)
- ディスプレイ: ST7789V 135x240 (LCD)

## ビルド & 書き込み

```bash
pio run -e m5sticks3 -t upload --upload-port /dev/ttyACM0
```

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

## 通信プロトコル

USB CDC Serial @ 115200 baud, 8N1.

### 出力 (デバイス → PC)

周期: 100Hz (10ms 毎)。`availableForWrite()` が不足時はそのフレームをスキップ。

```
D,{seq},{ms},{ax},{ay},{az},{gx},{gy},{gz},{btn}
```

| フィールド | 型 | 意味 |
|-----------|----|------|
| `D` | タグ | データ行 |
| `seq` | 符号なし整数 | シーケンス番号 (0 からインクリメント) |
| `ms` | 符号なし整数 | `millis()` の値 |
| `ax`,`ay`,`az` | 浮動小数点 | 加速度 (g, gravity=1.0) |
| `gx`,`gy`,`gz` | 浮動小数点 | 角速度 (deg/s) |
| `btn` | 0/1 | ボタン A の状態 (0=非押下, 1=押下) |

例:
```
D,0,1234,0.01,-0.00,1.00,0.06,-0.06,-0.18,0
```

軸方向は M5Unified のデフォルト (M5StickS3 設定時) に従う。

### 起動時メッセージ

```
READY IMU:OK
READY IMU:ERR   ← IMU 未検出
```

### 入力 (PC → デバイス)

コマンドは LF (`\n`) 区切り。CR は無視。

| コマンド | 応答 | 意味 |
|---------|------|------|
| `RESET` | `OK RESET` | IMU キャリブレーションオフセットをクリア |
| その他 | `ERR UNKNOWN:{cmd}` | 不明なコマンド |

## Unity Input System 連携 (予定)

Unity 側でシリアルポートを開き、上記プロトコルで受信した IMU データを
Unity Input System のデバイスとして登録する。
