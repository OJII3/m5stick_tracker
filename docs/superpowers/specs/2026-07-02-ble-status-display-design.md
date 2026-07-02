# M5Stick Tracker: BLE Status Display

## Purpose

M5StickS3 の LCD に Bluetooth 接続状態を表示する。表示はユーザがデバイス単体で状態を把握できるようにするためのもので、現状の `Serial.printf` ログを置き換えるものではない。

## Background

- 現状: BLE 接続/切断/購読の通知は USB CDC Serial にだけ出ており、M5Stick 単体では目視確認できない。
- 接続先名、IMU notify の実送信レートを LCD に出す。
- Bluetooth の接続状態と IMU 購読状態は混ぜない。IMU 購読状態は IMU notify の Hz にだけ反映する。

## Goals

- LCD に以下の 3 行を常時表示する:
  - Bluetooth 接続状態
  - 接続先 (名前が取れれば名前、無ければ MAC アドレス)
  - BLE IMU notify の実送信 Hz
- シーケンス番号は表示しない。
- 状態変化時および 1 秒ごとに更新する。

## Non-Goals

- LCD のテーマ変更、フォント変更、画面遷移。
- Serial プロトコルの変更。
- ボタン操作での画面切り替え。
- Battery 残量表示 (将来の課題として残す)。

## Design

### 表示レイアウト

画面上部固定のステータス領域と、IMU ログが流れるスクロール領域の 2 層構造にする。スクロール領域の 1 行目に「IMU:」のような区切りヘッダを置き、ステータスとログを視覚的に分離する。

ステータス領域 (固定 4 行、y=0..):

```
M5Stick Tracker
BT: <state>
Peer: <peer>
IMU notify: <hz> Hz
```

スクロール領域 (y=64..):

```
IMU:
ax=... ay=... az=... gx=... gy=... gz=...
...
```

未接続時:

```
M5Stick Tracker
BT: advertising
Peer: -
IMU notify: 0 Hz
```

購読なし接続時:

```
M5Stick Tracker
BT: connected
Peer: AA:BB:CC:DD:EE:FF
IMU notify: 0 Hz
```

購読中接続時:

```
M5Stick Tracker
BT: connected
Peer: iPhone
IMU notify: 49 Hz
```

`<state>` は `advertising` または `connected` のみ。IMU 購読の有無は Hz 数値だけで表現する。

`<peer>` の表示ルール:
- 接続中かつ NimBLE が peer の name を返した場合、その名前
- 接続中だが name が取得できない場合、`AA:BB:CC:DD:EE:FF` 形式の MAC アドレス
- 未接続時、`-`

`<hz>` の計算:
- 過去 1 秒間に `bleHidSendImu()` から実際に notify が呼ばれて成功した回数
- 表示更新も 1 秒ごと。`advertising` 時と IMU 非購読時は `0 Hz`

### 状態管理

`ble_hid` モジュールが自身の接続状態 (`connected` / `advertising`) と現在の peer を内部に保持する。`main.cpp` 側はそれを API 経由 (読み取り専用) で取得する。

- `ble_hid.cpp` の `onConnect` で peer name を取得し、名前が取れなければ MAC アドレスを保存
- `ble_hid.cpp` の `onDisconnect` で内部状態を `advertising` に戻し、peer をクリア
- `ble_hid.h` に `void bleHidGetPeerName(char* buf, size_t len)` のような読み取り API を追加

peer name の取得は NimBLE の `NimBLEConnInfo::getPeerName()` を試し、空文字なら MAC アドレスを使う。

### IMU notify レート計測

`ble_hid.cpp` の `bleHidSendImu()` の notify 直後に、インクリメント可能な共有カウンタをアトミック加算する。`main.cpp` 側は 1 秒ごとにカウンタ値を読んで Hz 表示に使い、その後リセットする。

- カウンタは `std::atomic<uint32_t>` を使う。ISR からは呼ばれないので mutex は不要。
- 未購読時は notify 関数の中で早期 return するので、カウントは増えない。

### 再描画タイミング

- 接続/切断/購読変化などのイベント時にフラグを立てる
- `main.cpp` の `loop` で 1 秒ごとに、状態変化フラグが立っているか、前回表示から 1 秒経過していれば LCD を再描画する
- 再描画はステータス領域全体を上書き (背景を黒で塗りつぶしてから 4 行テキストを描画)
- スクロール領域 (IMU ログ) は触らない

### ファイル変更

- `src/ble_hid.h`
  - `void bleHidGetPeerName(char* buf, size_t len)` を追加 (未接続時は `'-'` を 1 文字書き込む)
  - notify 成功カウンタを 1 秒窓で読み取って 0 クリアする API (`uint32_t bleHidConsumeNotifyCount()`) を追加
  - 接続状態のスナップショット用 enum (`bleHidState_t { BLE_HID_ADVERTISING, BLE_HID_CONNECTED }`) と getter (`bleHidState_t bleHidGetState()`) を追加
- `src/ble_hid.cpp`
  - 内部状態 (`volatile const char* state`、`char peer[64]`、`std::atomic<uint32_t> notifyCount`) を追加
  - `onConnect` で peer を保存
  - `onDisconnect` で state を `advertising` に戻し peer をクリア
  - `bleHidSendImu()` 内で notify 成功後にカウンタをインクリメント
  - 追加 API の実装
- `src/main.cpp`
  - ステータス描画用の関数 `drawStatus()` を追加
  - `setup` でステータス領域を初期描画
  - `loop` で 1 秒ごと、または状態変化時に `drawStatus()` を呼ぶ
  - Serial と LCD のログが混ざらないよう LCD のログには `IMU:` プレフィックスを付ける
- `README.md`
  - LCD 表示仕様を追記

## Error Handling

- peer name 取得失敗時は MAC アドレスにフォールバック
- notify 失敗 (`notify()` の戻り値) は無視 (現状と同じ)
- LCD 描画失敗時は状態を維持し、次回更新タイミングで再試行

## Testing

- 実機での目視確認が主。`platformio test` フレームワークは導入しない。
- 検証項目:
  1. 起動時に `BT: advertising` / `Peer: -` / `IMU notify: 0 Hz` が表示される
  2. 接続時に `BT: connected` に切り替わり、`Peer:` に名前または MAC が表示される
  3. IMU 購読開始時に `IMU notify:` の Hz が 0 以外になり、概ね 50Hz 付近で安定する
  4. 切断時に `BT: advertising` に戻り、`Peer: -` になる
- ホスト PC がない状況で実機確認できないため、ユーザに動作確認を依頼する

## Open Questions

なし。
