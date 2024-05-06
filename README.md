# M5Stack向けMatterサンプル

## 概要

M5Stackを使ったMatterのサンプルコード集です。今のところNanoC6によるThread経由のMatterサンプルに必要なものが置いてあります。

## ビルド用Dockerイメージ

ビルド用のDockerイメージを `docker` ディレクトリに用意してあります。Espressif公式の `esp-matter` Dockerイメージに対して、NanoC6対応のパッチを当てたものになっています。

以下のコマンドでビルド用Dockerイメージを構築しておきます。

```
cd docker
make build
```

## ファームウェア

### openthread/ot_rcp

ESP-IDFのopenthreadサンプルから持ってきたot_rcpファームウェアです。NanoC6向けにデフォルト設定を調整してあります。

以下のデフォルト設定を変更し、RCPのピンをNanoC6のGROVE互換ポートのピンにしています。

```
CONFIG_OPENTHREAD_UART_PIN_MANUAL=y
CONFIG_OPENTHREAD_UART_RX_PIN=2
CONFIG_OPENTHREAD_UART_TX_PIN=1
```

NanoC6のピン配置は以下の通りです。

| GPIO番号 | 配置箇所        | 方向 | 内容    |
| -------: | --------------- | ---- | ------- |
|        1 | GROVE互換ポート | 出力 | UART TX |
|        2 | GROVE互換ポート | 入力 | UART RX |

#### ビルドと書き込み

```
source esp-idf/export.sh
cd openthread/ot_rcp
idf.py set-target esp32c6
idf.py flash -p /dev/ttyACM0
```

### openthread/ot_br

ESP-IDFのopenthreadサンプルから持ってきたot_brファームウェアです。NanoC6 1台でWi-Fi Thread coexistenceを利用、もしくはot_rcpファームウェアを書き込んだNanoC6と組み合わせてThread Border Routerを構成します。

#### NanoC6 2台での構成

OT-RCPの接続先をGROVE互換ポートのピンとしています。上記のot_rcpファームウェアでのGROVE互換ポートのピンと対抗するようにTXとRXを入れ替えてあります。

NanoC6のピン配置は以下の通りです。

| GPIO番号 | 配置箇所        | 方向 | 内容    |
| -------: | --------------- | ---- | ------- |
|        1 | GROVE互換ポート | 入力 | UART RX |
|        2 | GROVE互換ポート | 出力 | UART TX |

ot_rcpを書き込んだNanoC6とot_brを書き込んだNanoC6をGROVE互換ポートで互いに接続し、ot_brを書き込んだNanoC6をPCに接続します。

![ot_rcpとotbrの接続](./doc/nanoc6_otbr.drawio.svg)

#### NanoC6 1台での構成

ESP-IDFのドキュメントにはWi-FiとThreadのCoexistenceでの共用はできないとありましたが、どうも可能になっているようなので試したところ、NanoC6 1台でのOTBRを構成できました。

設定も単純で、 

```
CONFIG_SW_COEXIST_ENABLE=y
CONFIG_OPENTHREAD_RADIO_NATIVE=y
```

の2つの設定を有効にしてビルドするだけです。上記の設定は `sdkconfig.defaults.coex` として保存してあるので、 `SDKCONFIG_DEFAULTS` 変数にて使用するsdkconfig.defaultsの定義を変更することにより、NanoC6 1台構成でのOTBRのファームウェアをビルドできます。

```
rm -rf build sdkconfig
idf.py set-target esp32c6
export SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32c6;sdkconfig.defaults.coex"
idf.py build
```

#### 使い方

PC上でシリアルターミナルを開き、以下のコマンドを入力して Wi-Fiの接続設定を行います。 `<SSID>` と `<PASS>` をそれぞれ接続先のWi-FiアクセスポイントのSSIDとパスワードで置き換えます。

```
wifi connect -s <SSID> -p <PASS>
```

その後、以下のように表示されWi-Fiアクセスポイントに接続が完了するまで待ちます。

```
wifi sta is connected successfully
Done
```

必要に応じて `dataset` コマンドのサブコマンドをつかって、Threadネットワークのパラメータを設定します。
デフォルトではランダムな値でパラメータが初期化されます。
また、NVSに保存されるため、M5Burner等でファームウェアを書き込みなおした場合を除いて、基本的にパラメータは次回以降は同じものが使われます。

例としてネットワークキーを簡単なもの ( `11112222333344445555666677778888` ) に変更する場合を示します。
※ネットワークキーを簡単なものにするのは実験用以外にはおすすめしません。

```
dataset init new
dataset networkkey 11112222333344445555666677778888
dataset commit active
```

| パラメータ        | 値                   |
| :---------------- | -------------------- |
| `networkname`     | `OpenThread-(PanID)` |
| `meshlocalprefix` | `(ランダム)/64`      |
| `channel`         | `(ランダム)`         |
| `panid`           | `(ランダム)`         |
| `extendedpanid`   | `(ランダム)`         |
| `networkkey`      | `(ランダム)`         |
| `pskc`            | `(ランダム)`         |


完了後、 `ifconfig up` と `thread start` を実行してThreadの通信処理を開始します。

```
ifconfig up
```

```
I (48483) OPENTHREAD: Platform UDP bound to port 49153
Done
I (48483) OT_STATE: netif up
I (48493) OPENTHREAD: NAT64 ready
> thread start
```

```
thread start
```

```
> thread start

I(51323) OPENTHREAD:[N] Mle-----------: Role disabled -> detached
Done
```

`dataset active -x` を実行して、Threadネットワークに接続するために必要なデータセットの値を取得しておきます。この値はあとでThreadデバイスを接続するために必要なので記録しておきます。

```
dataset active -x
```

```
0e080000000000000000000300000b35060004001fffe00208dead00beef00cafe0708fddead00beef000005107443e9e3e3f9bf4ea87009b17455b039030a4f70656e54687265616401026f64041043e769eacf5354de6d9ce281ab1b3bdd0c0402a0f7f8
Done
```

また、`dataset active` を実行して、Threadネットワークに接続するために必要なネットワークキーやPanID等を取得しておきます。

```
dataset active
```

```
Active Timestamp: 1
Channel: 19
Channel Mask: 0x07fff800
Ext PAN ID: d83b01e82992c0df
Mesh Local Prefix: fddf:4b94:8740:3ba1::/64
Network Key: 11112222333344445555666677778888
Network Name: OpenThread-357b
PAN ID: 0x357b
PSKc: 1f2d4df0ebf13f02981077a847facfe4
Security Policy: 672 onrc 0
Done
```

以上でThreadボーダールーターの起動処理は完了です。

### matter/sleepy_device

NanoC6向けの低消費電力Thread Matterデバイスのファームウェアです。

#### ハードウェア構成

NanoC6のGROVE互換ポートにスイッチと、必要に応じてデバッグログ出力用のUSB-UARTのRXぴんを接続します。

| GPIO番号 | 配置箇所        | 方向 | 内容    |
| -------: | --------------- | ---- | ------- |
|        1 | GROVE互換ポート | 出力 | UART TX |
|        2 | GROVE互換ポート | 入力 | スイッチ |


#### ビルド

esp-matterのDockerイメージを使ってビルドします。

```
cd matter/sleepy_device
make build
```

#### 書き込み

NanoC6の接続先デバイス名が `/dev/ttyACM0` の場合、以下のコマンドで書き込みを実行します。

```
export PORT=/dev/ttyACM0
make flash flash-matter-factory
```

#### コミッショニング

コミッショニングに必要なMPCやQRコードは `common/mfg_manifest` に置いてあります。

MPCは `34970012334` QRコードは以下の内容です。

![QRコード](matter/common/mfg_manifest/20202020_3841_qrcode.png)

PINコード (MPCではない) は `20202020` ディスクリミネーターは `3841` です。

# ライセンス

元になったesp-matterやESP-IDFのサンプルと同様に、Apache Licenseです。