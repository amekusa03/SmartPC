# 【製品仕様書】SmartPC on ESP32-S3
**Google Home（Matter）対応・ハイブリッドPC電源コントローラー**
**(キャプティブポータル初期設定 × WoL電源ON × USB HID/BLEハイブリッド電源OFF × USB/Pingハイブリッド稼働検知)**

- **作成日**: 2026-10-31 (ESP32-S3移行・フルスペック仕様)
- **ターゲットチップ**: ESP32-S3 (Xtensa Dual-Core 240MHz, Wi-Fi 2.4GHz + Bluetooth 5.0 LE, Native USB OTG / HID / USB-Serial-JTAG)
- **ステータス**: **動作確認完了・リリース準備完了 (v1.0.0)**

---

## 1. 製品コンセプト ＆ 提供価値 (UX)

### コンセプト
**「PCの分解・改造一切不要。PC背面のUSBポートに挿すだけで、Google Home等のスマートスピーカーからPCの電源を自在にコントロールできるデバイス」**

ESP32-S3 を採用することで、C3 で制限されていた Native USB HID による有線直結制御と、Wi-Fi/WoL/Bluetooth による遠隔制御の両方をフルサポート。完全シャットダウンからの起動からスリープ管理、状態監視までシームレスに実現します。

### ユーザー体験 (初期セットアップ)
1. **箱から出してPCのUSBポート（または給電アダプタ）に接続**。
2. スマホのWi-Fi一覧から **`SmartPC-Setup`** に接続。
3. 自動的に **洗練された設定Web画面（Captive Portal）** がポップアップ：
   - 自宅の 2.4GHz Wi-Fi を一覧から選択し、パスワードを入力。
   - 起動したい PC の MAC アドレスを入力（調べ方ガイド付き）。
   - PC の IP アドレスを入力（Ping死活監視用 / 任意）。
   - 「保存」を押すと ESP32-S3 が自動再起動して設定完了。
4. Google Home アプリからデバイス追加（Matter over Wi-Fi 標準対応）。
5. **「OK Google, パソコンつけて」「パソコン消して」** で音声操作。

---

## 2. コア機能アーキテクチャ

### (1) 電源 ON（Wake-on-LAN ＋ USB Remote Wakeup ハイブリッド）
- **動作**: 
  - **主動作 (WoL)**: ESP32-S3 から UDP 9 番ポート宛てに 102 バイトの WoL マジックパケットをブロードキャスト（信頼性のため3回連続送出）。
  - **補助動作 (USB Wakeup)**: PC がスリープ（S3）の場合、Native USB HID より `Remote Wakeup` 信号および `Space` キー入力を送信。
- **対象OS**: **全OS対応（Windows 10/11, macOS, Linux, サーバーOS）**
- **メリット**: PCの電源が完全シャットダウン状態（S5）や休止状態からでも WoL で確実に起動可能。

### (2) 電源 OFF（USB HID ＋ BLE HID ハイブリッド）
- **動作**: 
  - **有線USB接続時**: Native USB HID 経由で Consumer Control の「Sleep」キーコード（`0x0032`）を送信。Bluetooth ペアリングの手間すら不要。
  - **ワイヤレス運用時**: ESP32-S3 が標準 Bluetooth キーボード（BLE HID）として振る舞い、スリープ信号を送信。
- **対象OS**: **Windows 10/11, macOS, Linux (GUI)**
- **メリット**: PC側に専用常駐アプリのインストールが **一切不要**。OS標準の安全なスリープ機能が動作。

### (3) PC電源状態検知（★ ハイブリッド検知方式）
「PCが今ついているか消えているか」を、使用スタイルに応じて自動判定：

```mermaid
graph TD
    Start[状態監視ループ] --> CheckUSB{USB接続されているか？\n(Native USB 列挙 / JTAG)}
    CheckUSB -- YES (PCに直接接続中) --> USBState[★ USBハードウェア検知\n・誤検知ゼロ\n・遅延ほぼ0秒で瞬時同期\n・PCからの給電で動作\n・USB Suspendでスリープ検知]
    CheckUSB -- NO (ACアダプタ等で運用中) --> PingCheck{Ping (ICMP) 応答はあるか？}
    PingCheck -- 応答あり --> StateON[PC: ON 状態]
    PingCheck -- タイムアウト --> StateOFF[PC: OFF 状態]
    USBState --> GoogleHome[Matter経由でGoogle Homeにリアルタイム反映]
    StateON --> GoogleHome
    StateOFF --> GoogleHome
```

- **プライマリ (USB接続時)**:
  - ESP32-S3 の Native USB / USB-Serial-JTAG を使用。
  - PCの電源が入るとUSB通信が開始され、**遅延0秒・誤検知ゼロ** で即座に「ON」を検知。
  - PCがスリープ/シャットダウンするとUSBバスがサスペンド（または通電切断）し、瞬時に「OFF」を検知。
  - PCからの給電と通信をUSBケーブル1本で両立可能。
- **セカンダリ (USB非接続時 / 完全ワイヤレス運用時)**:
  - 設定された PC の IP アドレスへ Ping (ICMP) を定期送信して生死監視（フォールバック）。

---

## 3. システムブロック図

```mermaid
graph TD
    subgraph InitialSetup [1. 初期セットアップ時]
        Phone[スマートフォン] -- "Wi-Fi接続 (SmartPC-Setup)" --> AP[ESP32-S3 SoftAP]
        AP -- "DNS Captive Portal (Port 53)" --> WebUI[Web設定UI (192.168.4.1)]
        Phone -- "SSID / PASS / PC MAC / IP" --> WebUI
        WebUI -- "Flashに永続化" --> NVS[(ESP32 NVS)]
    end

    subgraph NormalOperation [2. 通常稼働時]
        GH[Google Home / Alexa / Siri] -- "Matter (Wi-Fi)" --> MatterNode[ESP32-S3 Matterスタック]
        MatterNode -- "ON指令" --> WoLEngine[WoL マジックパケット送出 ＋ USB Wakeup]
        MatterNode -- "OFF指令" --> SleepEngine[USB HID / BLE HID スリープ信号]
        PC[対象 PC] -- "USB通信 (SOF/Suspend)" --> USBDetect[USBハードウェア検知]
        PC -- "Ping応答 (フォールバック)" --> PingDetect[Ping 生死監視]
        USBDetect --> StateSync[状態同期エンジン]
        PingDetect --> StateSync
        StateSync --> MatterNode
        WoLEngine -- "有線LAN / USB" --> PC
        SleepEngine -- "USB / Bluetooth" --> PC
    end
```

---

## 4. ハードウェア仕様（ESP32-S3）

| 項目 | スペック / 理由 |
| :--- | :--- |
| **SoC** | **ESP32-S3** (Xtensa 32-bit LX7 Dual-Core 240MHz) |
| **Flash** | **8MB または 16MB** (Matter + Wi-Fi + BLE + WebServer の大容量ファームウェアに対応) |
| **PSRAM** | **2MB または 8MB** (Matter 通信スタックや通信バッファの安定運用) |
| **USB インターフェース** | **Native USB (OTG / HID)** ＋ **USB-Serial/JTAG** |
| **外装ケース** | 3Dプリンタ製小型ドングルケース |

---

## 5. 開発フェーズと実装タスク

### フェーズ 1: C3資産のS3移行＆統合（進行中）
- [x] NVS 設定管理モジュール移植 (`nvs_config.c/h`)
- [x] キャプティブポータル移植 (`captive_portal.c/h`)
- [x] WoL 送出エンジン移植 (`wol.c/h`)
- [x] ESP32-S3 向け CMake / パーティション / sdkconfig 最適化

### フェーズ 2: S3特有のUSB HID・ハードウェア検知
- [x] Native USB HID ドライバ（TinyUSB / Keyboard / Consumer Control）
- [x] USB 接続＆サスペンド検知による即時 PC 稼働判定
- [x] ハードウェア生死監視タスク（Native USB サスペンド検知 & Matter On/Off 自動双方向同期）

### フェーズ 3: Matter統合＆完成
- [x] Matter On/Off プラグエンドポイントと WoL / HID / 状態検知の完全結合
- [x] 実機テストおよび Google Home アプリ連携確認
