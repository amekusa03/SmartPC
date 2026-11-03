# SmartPC (ESP32-S3 Matter On/Off Switch)

ESP32-S3 を使用して PC の電源制御などを行う、Matter (over Wi-Fi) 対応スマートホームノードプロジェクトです。  
Google Home, Apple Home, Alexa 等の Matter エコシステムから直接シームレスに操作できます。

---

## 📱 初回ペアリング・コミッショニング手順

### ⚠️ 【超重要】Wi-Fi 接続前の事前準備
1. **Wi-Fi は 2.4GHz 帯のみ対応**
   - **ESP32-S3 は 5GHz 帯の Wi-Fi に非対応です（2.4GHz 専用）。**
   - セットアップを行うスマートフォンは、あらかじめご自宅ルーターの **2.4GHz 帯の SSID**（`_g` や `2G` など）に接続しておいてください。
   - ※スマホが 5GHz 帯に繋がったままだと、その設定が転送されて接続エラーになります。
2. **スマホの Bluetooth を ON にする**
   - Matter の初回設定は **BLE (Bluetooth Low Energy)** 経由で行われます。

---

### ペアリング方法（Google Home / Apple Home 等）

1. 各スマートホームアプリ（Google Home など）を開き、**「デバイスの追加」→「Matter 対応デバイス」** を選択。
2. 以下のいずれかの方法でデバイスを認識させます。

#### 方法 A: QRコードをスキャンする
以下のリンク先の QR コード画像をスマホカメラで読み取ります。

* **[テスト用 Matter QR コード画像を開く (外部QR生成リンク)](https://api.qrserver.com/v1/create-qr-code/?size=300x300&data=MT%3AY.K9042C00KA0648G00)**
* Setup Payload: `MT:Y.K9042C00KA0648G00`

#### 方法 B: 11桁のセットアップコードを手動入力する
QRスキャン画面で「コードをお持ちでない場合」または「手動入力」を選択し、以下を入力します。

```text
34970112332
```
*(予備コード: `00440121110`)*

3. 画面の指示に従い、2.4GHz Wi-Fi のパスワードを転送すればペアリング完了です。

---

## 🛠 ハードウェア構成 & 注意点

- **マイコンボード**: ESP32-S3
- **通信規格**: Matter over Wi-Fi (コミッショニングは CHIPoBLE)
- **USB ケーブルの罠に注意**:
  - 初回書き込みやシリアル通信時、**充電専用の Type-C ケーブル** では PC 側にポート（`/dev/ttyACM0` 等）が認識されません。
  - 必ず **データ通信対応の USB ケーブル** を使用してください。

---

## 💻 ビルド & 書き込み手順

ESP-IDF および ESP-Matter の環境変数をロードしたシェルで実行します。

```bash
# 環境変数の読み込み
. $HOME/esp/esp-idf/export.sh
. $HOME/esp/esp-matter/export.sh

# 設定 (ESP32-S3 ターゲット)
idf.py set-target esp32s3

# ビルド
idf.py build

# フラッシュ書き込み & モニター起動
idf.py -p /dev/ttyACM0 flash monitor
```

### デバイスの初期化（工場出荷状態リセット / 再ペアリング）
Wi-Fi 設定や Matter Fabric 情報などの NVS 領域を消去して出荷時状態に戻す方法は 2 通りあります。

1. **本体の BOOT ボタン（物理リセット / 推奨）**:
   - ESP32-S3 ボード上の **BOOT ボタン（GPIO 0）を 5 秒間長押し** します。
   - シリアルログにカウントダウン（`1/5 sec...`）が表示され、5 秒経過後に自動で NVS 消去（`esp_matter::factory_reset` & `nvs_flash_erase`）が行われて再起動します。途中で離すとキャンセルされます。

2. **PC から USB 経由で消去する場合**:
   ```bash
   idf.py -p /dev/ttyACM0 erase-flash flash monitor
   ```
