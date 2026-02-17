# RP2040 Pico W クローン (2023) - 技術メモ

## ボード識別情報

- シルクスクリーン: "RP2040 Pico W"
- 購入先: Aliexpress
- WiFiチップ: **ESP8285** (CYW43439ではない)
- ATファームウェア: v1.6.2.0 (ESP8266互換)

## 公式 Raspberry Pi Pico W との違い

| | 公式 Pico W | 本クローン |
|---|---|---|
| WiFi/BTチップ | CYW43439 (Infineon) | ESP8285 (Espressif) |
| インターフェース | PIO-SPI (内蔵) | UART (ATコマンド) |
| BLE | 対応 | **非対応** |
| Arduino WiFiライブラリ | `WiFi.h` (組込み) | ATコマンド直接 or `WiFiEsp` |
| PlatformIO board設定 | `rpipicow` | `rpipico` |

**重要**: `board = rpipicow` や Pico W 用の CYW43 ドライバは一切動作しません。

## ESP8285 UARTピン設定

| 信号 | GPIO | UART |
|------|------|------|
| TX (RP2040 → ESP) | GP0 | UART0 TX |
| RX (ESP → RP2040) | GP1 | UART0 RX |

ボーレート: **115200**

```cpp
// Arduino での初期化
Serial1.setTX(0);
Serial1.setRX(1);
Serial1.begin(115200);
```

## ATコマンドによるWiFi接続

### 基本的な接続手順

```cpp
// モジュール確認
Serial1.print("AT\r\n");           // -> OK

// ステーションモード
Serial1.print("AT+CWMODE=1\r\n");  // -> OK

// シングル接続モード（デフォルト値; 明示的に設定）
Serial1.print("AT+CIPMUX=0\r\n");  // -> OK

// APに接続
Serial1.print("AT+CWJAP=\"SSID\",\"PASSWORD\"\r\n");
// -> WIFI CONNECTED
// -> WIFI GOT IP
// -> OK

// IPアドレス取得
Serial1.print("AT+CIFSR\r\n");
// -> +CIFSR:STAIP,"192.168.x.x"
```

### HTTPリクエスト

```cpp
// TCP接続を開く
Serial1.print("AT+CIPSTART=\"TCP\",\"api.example.com\",80\r\n");
// -> CONNECT
// -> OK

// 送信データ長を指定
Serial1.print("AT+CIPSEND=<length>\r\n");
// -> ">" プロンプト

// HTTPリクエストを送信
Serial1.print("GET /path HTTP/1.1\r\nHost: api.example.com\r\nConnection: close\r\n\r\n");
// -> SEND OK

// レスポンスは +IPD,<len>:<data> 形式で到着
// 接続終了時: CLOSED
```

### レスポンスの解析

ESP8285はTCPデータを `+IPD,<len>:<data>` フレームでラップして返す。
1つのHTTPレスポンスに対して複数の `+IPD` セグメントが届く場合がある。
HTTPヘッダ/ボディを解析する前に、このフレーミングを除去する必要がある。

```cpp
// +IPD フレームからデータを抽出する例
int ipdPos = response.indexOf("+IPD,");
int colonPos = response.indexOf(':', ipdPos);
String lenStr = response.substring(ipdPos + 5, colonPos);
int dataLen = lenStr.toInt();
String data = response.substring(colonPos + 1, colonPos + 1 + dataLen);
```

## ATファームウェア互換性

- ATバージョン: **1.6.2.0** (ESP8266レガシーATファームウェア)
- `WiFiEspAT` ライブラリ (jandrassy): **非互換** (AT v1.7+が必要)
  - 主な非互換理由: v1.6.2.0 では `AT+CIPRECVMODE`（パッシブ受信モード）等が未サポート
  - jandrassy版は NonOS SDK AT v1.7+ または ESP-IDF AT (ESP32系) を前提としている
- `WiFiEsp` ライブラリ (bportaluri): **未検証** (理論上はAT v1.xに対応)
  - メンテナンスが停滞しており、RP2040の `Serial1` とのタイミング問題の可能性あり
- 生のATコマンド: **推奨** (最も信頼性が高い、本プロジェクトで採用)

## HTTPS/TLS の制限

ESP8285 ATファームウェア v1.6.2.0 の TLS/SSL 対応は限定的または未サポートの可能性が高い。

- `AT+CIPSTART="TCP",...` (HTTP) は動作確認済み
- `AT+CIPSTART="SSL",...` (HTTPS) は **未検証** — v1.6.2.0 で使えるか要確認
- 現在のコードは Open-Meteo の HTTP (port 80) エンドポイントを使用
- **多くの公開APIはHTTPSのみ** のため、対応可能なAPIが限られる

v1.7.4 Loboファームに更新した場合のSSL対応:
- `AT+CIPSTART="SSL","host",443` でHTTPS接続が可能になる
- `AT+CIPSSLSIZE=8192` でSSLバッファ設定（8192以上推奨、最大16384）
- `AT+SSLLOADCERT` でCA証明書をRAMにロード可能

**ただしESP8285のSSLには根本的な制約がある**:
- RAM が極めて少ない（約80KB）— SSLハンドシェイクだけで大半を消費
- バッファを大きくすると他の処理に支障が出る
- 一部のサーバー（証明書チェーンが長い等）は接続失敗する可能性
- ハンドシェイクに数秒かかり、全体的に遅い

現実的な方針:
- HTTP対応のAPIを選択する（現在の方針、最も安定）
- SSLが必要な場合はファームウェアをv1.7.4に更新した上で試す（過度な期待は禁物）
- ローカルHTTPプロキシを中継する構成も選択肢

## ESP8285 ファームウェア更新

### 概要

ESP8285のATファームウェアをv1.6.2.0からv1.7.4に更新可能。
ボード上の物理ボタンとesptoolを使い、macOS/Linux/Windowsいずれからでも実施できる。

更新の主なメリット:
- **WiFiEspATライブラリ対応** — raw ATコマンドからの脱却（v1.7+必須）
- SSL/TLS対応の改善（上記の制約付き）
- OTA（Over-The-Air）更新対応
- 安定性向上・拡張ATコマンド追加

### 必要なファイル

[mentalfl0w/rp2040_with_esp8285_Arduino_guide](https://github.com/mentalfl0w/rp2040_with_esp8285_Arduino_guide) リポジトリから取得:

| ファイル | 用途 |
|---------|------|
| `Serial_port_transmission.uf2` | RP2040をシリアルブリッジ化するUF2 |
| `ESP8285_1MB_1.7.4_AT_Lobo.bin` | 統合済みファームウェア（**推奨**、Lobo版） |
| `ESP8285_1MB_1.7.4_AT.bin` | 統合済みファームウェア（公式版） |

> Lobo版 ([loboris/ESP8266_AT_LoBo](https://github.com/loboris/ESP8266_AT_LoBo)) は公式版に対して
> SSL/TLS改善、CA証明書ロード、拡張ATコマンド等が追加されており、推奨。

### macOSでの手順

#### 1. esptoolのインストール

```bash
pip3 install esptool
```

#### 2. RP2040にシリアルブリッジを書き込む

1. USBを抜く
2. **BOOTSEL**ボタン（USB端子側）を**押しながら**USB接続
3. `RPI-RP2` ドライブが表示される
4. UF2ファイルをコピー:

```bash
cp Serial_port_transmission.uf2 /Volumes/RPI-RP2/
```

#### 3. ESP8285をダウンロードモードにする

1. USBを抜く
2. **BOOT**ボタン（WiFiチップ側、USB端子から遠い方）を**押しながら**USB接続
3. ESP8285がフラッシュ待機状態になる

#### 4. esptoolでフラッシュ

```bash
# ポートを確認
ls /dev/tty.usbmodem*

# フラッシュ実行
esptool.py --chip esp8266 \
  --port /dev/tty.usbmodemXXXX \
  --baud 115200 \
  write_flash \
  --flash_mode dout \
  --flash_freq 40m \
  --flash_size 1MB \
  0x0 ESP8285_1MB_1.7.4_AT_Lobo.bin
```

> ESP8285はESP8266の内蔵Flash版のため、chipは `esp8266` を指定。
> Flash modeは `dout`（ESP8285は1MB DOUT）。

#### 5. 動作確認

USBを抜いて再接続（ボタンは押さない）し、ATコマンドで確認:

```
AT+GMR
```

`AT version:1.7.4` 系が返れば成功。

### 参考リンク

- [元ガイド (Dylan Liu's Blog)](https://blog.ourdocs.cn/post/rp2040_with_esp8285_arduino_guide/)
- [mentalfl0w/rp2040_with_esp8285_Arduino_guide (GitHub)](https://github.com/mentalfl0w/rp2040_with_esp8285_Arduino_guide)
- [mocacinno/rp2040_with_esp8285 (GitHub)](https://github.com/mocacinno/rp2040_with_esp8285)

## ESP8285 ハードウェアメモ

- **リセットピン**: RP2040からESP8285をハードリセットできるGPIOが接続されているか未確認
  - 現在の `ESP8285Wifi` クラスはリセットピンを扱っていない
  - ATコマンドがハングした場合の回復手段がない状態
  - 基板のトレースを追って確認する価値あり

## PlatformIO 設定

```ini
[env:pico]
platform = https://github.com/maxgerhardt/platform-raspberrypi.git
board = rpipico          ; rpipicow ではない
framework = arduino
board_build.core = earlephilhower
monitor_speed = 115200
```

> **注意**: `platform` は特定のコミットやタグに固定していないため、ビルドの再現性に
> 影響する可能性がある。安定版が確認できたら `platform = ...git#<commit-or-tag>` の
> 形式で固定することを推奨。

## 既知のコード上の制限

現在の `ESP8285Wifi` 実装 (`lib/ESP8285Wifi/`) に関する既知の制限:

1. **HTTPリクエストバッファ固定長** (`ESP8285Wifi.cpp`)
   - `req[512]` 固定 — URLパラメータを増やすとバッファオーバーフローの可能性
   - 現在のOpen-Meteoクエリパスは約180文字なので余裕はあるが、拡張時は注意

2. **レスポンスバッファとヒープ断片化**
   - `waitFor()` は `reserve(2048)` だが、レスポンスが大きい場合 `String` が動的拡張
   - RP2040のRAM (264KB) では長期運用時にヒープ断片化のリスク

3. **WiFi再接続ロジックなし**
   - `loop()` で天気取得失敗時にWiFi再接続を試みていない
   - 長期運用ではESP8285側のWiFi切断が発生する可能性が高い
   - `AT+CWJAP?` で接続状態を確認し、切断時に再接続する処理が必要

4. **ATコマンドハング時の回復なし**
   - ESP8285がATコマンドに応答しなくなった場合のリカバリ機構がない
   - ハードリセットピンが判明すれば GPIO 制御で回復可能

## デバッグ履歴

### CYW43ドライバ使用時の症状（誤ったアプローチ）

`board = rpipicow` でCYW43 WiFiを使用した場合:
- `CheckPicoW()` が false を返す (GPIO 25/29 の検出に失敗)
- `WiFi.begin()` が無限にハング
- `--wrap=init_cyw43_wifi` で検出をバイパスした場合:
  - `cyw43_arch_init()` は 0 を返す (成功に見える)
  - `cyw43_is_initialized()` は 1 を返す
  - LED GPIOは `cyw43_gpio_set()` で動作
  - しかし `itf_state` は 0x0 のまま
  - `cyw43_wifi_scan()` が -4 を返す (PICO_ERROR_NOT_PERMITTED)
  - `cyw43_wifi_join()` が -4 を返す
- MicroPython (Pico Wファームウェア): "[CYW43] Failed to start CYW43"

### 根本原因

このボードにはCYW43439チップが搭載されていない。WiFiモジュールは **ESP8285** であり、
PIO-SPIではなくUARTベースのATコマンド通信が必要。

### 特定方法

1. チップ刻印の目視確認: "ESP8285"
2. 候補ピンペアに "AT\r\n" を送信するUARTスキャン
3. UART0 GP0/GP1、115200ボーでESP8285が "OK" を返答
