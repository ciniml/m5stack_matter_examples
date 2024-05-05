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

ESP-IDFのopenthreadサンプルから持ってきたot_brファームウェアです。ot_rcpファームウェアを書き込んだNanoC6と組み合わせてThread Border Routerを構成します。

OT-RCPの接続先をGROVE互換ポートのピンとしています。上記のot_rcpファームウェアでのGROVE互換ポートのピンと対抗するようにTXとRXを入れ替えてあります。

NanoC6のピン配置は以下の通りです。

| GPIO番号 | 配置箇所        | 方向 | 内容    |
| -------: | --------------- | ---- | ------- |
|        1 | GROVE互換ポート | 入力 | UART RX |
|        2 | GROVE互換ポート | 出力 | UART TX |

ot_rcpを書き込んだNanoC6とot_brを書き込んだNanoC6をGROVE互換ポートで互いに接続し、ot_brを書き込んだNanoC6をPCに接続します。

![ot_rcpとotbrの接続](./doc/nanoc6_otbr.drawio.svg)

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
デフォルトでは以下のパラメータでThreadネットワークを構成します。(元のot_brサンプルのデフォルト値です)

```
dataset init active
dataset commit active
dataset active
```

```
Active Timestamp: 0
Channel: 11
Channel Mask: 0x07fff800
Ext PAN ID: dead00beef00cafe
Mesh Local Prefix: fdde:ad00:beef:0::/64
Network Key: 7443e9e3e3f9bf4ea87009b17455b039
Network Name: OpenThread
PAN ID: 0x6f64
PSKc: 43e769eacf5354de6d9ce281ab1b3bdd
Security Policy: 672 onrc 0
Done
```

| パラメータ        | 値                                   |
| :---------------- | ------------------------------------ |
| `networkname`     | `OpenThread`                     |
| `meshlocalprefix` | `fdde:ad00:beef:0::/64`                 |
| `channel`         | `11`                                 |
| `panid`           | `0x6f64`                             |
| `extendedpanid`   | `0xdead00beef00cafe`                 |
| `networkkey`      | `0x7443e9e3e3f9bf4ea87009b17455b039` |
| `pskc`            | `0x43e769eacf5354de6d9ce281ab1b3bdd` |


`dataset active -x` を実行して、Threadネットワークに接続するために必要なデータセットの値を取得しておきます。この値はあとでThreadデバイスを接続するために必要なので記録しておきます。

```
dataset active -x
```

```
0e080000000000000000000300000b35060004001fffe00208dead00beef00cafe0708fddead00beef000005107443e9e3e3f9bf4ea87009b17455b039030a4f70656e54687265616401026f64041043e769eacf5354de6d9ce281ab1b3bdd0c0402a0f7f8
Done
```

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