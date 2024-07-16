# Matter example for M5Stack

## Overview

This is a collection of Matter sample code using M5Stack. For now, here is what you need for the Matter sample via Thread by NanoC6.

## Docker image for build

A build Docker image is available in the `docker` directory, which is a patched version of Espressif's official `esp-matter` Docker image with NanoC6 support.

Build a Docker image for building with the following command.

```
cd docker
make build
```

## Firmware

### openthread/ot_rcp

This is ot_rcp firmware from the ESP-IDF openthread sample, with default settings adjusted for NanoC6.

The following default settings have been changed to make the RCP pins the pins of the GROVE compatible port of the NanoC6.

```
CONFIG_OPENTHREAD_UART_PIN_MANUAL=y
CONFIG_OPENTHREAD_UART_RX_PIN=2
CONFIG_OPENTHREAD_UART_TX_PIN=1
```

The pin layout of NanoC6 is as follows

| GPIO Number | Location              | Direction | Contents |
| ----------: | --------------------- | --------- | -------- |
|           1 | GROVE compatible port | output    | UART TX  |
|           2 | GROVE compatible port | input     | UART RX  |

#### Build and Write

```
source esp-idf/export.sh
cd openthread/ot_rcp
idf.py set-target esp32c6
idf.py flash -p /dev/ttyACM0
```

### openthread/ot_br

This ot_br firmware is from the ESP-IDF openthread sample and can be used with a single NanoC6 for Wi-Fi Thread coexistence, or combined with a NanoC6 with ot_rcp firmware to form a Thread Border Router.

#### NanoC6 Configuration with 2 units

The OT-RCP connection is made to the pins of the GROVE compatible port. TX and RX are swapped to compete with the pins of the GROVE compatible port in the above ot_rcp firmware.

The pin layout of NanoC6 is as follows

| GPIO Number | Location              | Direction | Contents |
| ----------: | --------------------- | --------- | -------- |
|           1 | GROVE compatible port | output    | UART RX  |
|           2 | GROVE compatible port | input     | UART TX  |

Connect the NanoC6 with ot_rcp written and the NanoC6 with ot_br written to each other via GROVE compatible ports, and connect the NanoC6 with ot_br written to the PC.

![ot_rcp and otbr connections](./doc/nanoc6_otbr.drawio.svg)

#### Configuration with one NanoC6

The ESP-IDF document says that Wi-Fi and Thread cannot be shared in Coexistence, but apparently it is now possible, so I tried it and was able to configure OTBR with a single NanoC6.

The setting is simple,

```
CONFIG_SW_COEXIST_ENABLE=y
CONFIG_OPENTHREAD_RADIO_NATIVE=y
```

You only need to enable the two settings and build. The above settings are saved as `sdkconfig.defaults.coex`, so by changing the definition of sdkconfig.defaults used in the `SDKCONFIG_DEFAULTS` variable, the firmware for OTBR with a single NanoC6 configuration can be OTBR firmware for a single NanoC6 configuration can be built.

```
rm -rf build sdkconfig
idf.py set-target esp32c6
export SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32c6;sdkconfig.defaults.coex"
idf.py build
```

#### Usage

Open a serial terminal on your PC and enter the following commands to set up your Wi-Fi connection Replace `<SSID>` and `<PASS>` with the SSID and password of the Wi-Fi access point you are connecting to, respectively.

```
wifi connect -s <SSID> -p <PASS>
```

Then wait until the following is displayed and the connection to the Wi-Fi access point is complete.

```
wifi sta is connected successfully
Done
```

If necessary, subcommands of the `dataset` command can be used to set parameters for the Thread network.
By default, the parameters are initialized with random values.
Also, since they are stored in NVS, the parameters are basically the same from the next time on, unless the firmware is rewritten by M5Burner or other means.

As an example, we show a case where the network key is changed to a simple one ( `1111112222223333334444555566666677778888` ).
We do not recommend changing the network key to a simple one except for experimental use.

```
dataset init new
dataset networkkey 11112222333344445555666677778888
dataset commit active
```

| Parameter         | Value                |
| :---------------- | -------------------- |
| `networkname`     | `OpenThread-(PanID)` |
| `meshlocalprefix` | `(random)/64`.`      |
| `channel`         | `(Random)`           |
| `panid`           | `(Random)`           |
| `extendedpanid`   | `(Random)`           |
| `networkkey`      | `(Random)`           |
| `pskc`            | `(Random)`           |


After completion, run `ifconfig up` and `thread start` to start the Thread communication process.

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

Run `dataset active -x` to get the dataset values needed to connect to the Thread network. Record this value as it will be needed later to connect Thread devices.

```
dataset active -x
```

```
0e080000000000000000000300000b35060004001fffe00208dead00beef00cafe0708fddead00beef000005107443e9e3e3f9bf4ea87009b17455b039030a4f70656e54687265616401026f64041043e769eacf5354de6d9ce281ab1b3bdd0c0402a0f7f8
Done
```

Also, run `dataset active` to obtain the network key, PanID, etc. needed to connect to the Thread network.

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

This completes the startup process for the Thread border router.

### matter/sleepy_device

Firmware for low-power Thread Matter devices for NanoC6.

#### Hardware configuration

Connect a switch to the GROVE compatible port of the NanoC6 and a USB-UART RX pin for debug log output if necessary.

| GPIO Number | Location              | Direction | Contents |
| ----------: | --------------------- | --------- | -------- |
|           1 | GROVE compatible port | output    | UART TX  |
|           2 | GROVE compatible port | input     | UART RX  |


#### Build

Build using the esp-matter Docker image.

```
cd matter/sleepy_device
make build
```

#### Write

If the device name to which NanoC6 is connected is `/dev/ttyACM0`, the following command will execute the write operation.

```
export PORT=/dev/ttyACM0
make flash flash-matter-factory
```

#### Commissioning

MPCs and QR codes required for commissioning are placed in `common/mfg_manifest`.

MPC is `34970012334` The QR code is the following.

![QRコード](matter/common/mfg_manifest/20202020_3841_qrcode.png)

The PIN code (not MPC) is `20202020` and the discriminator is `3841`.

# License

Like the original esp-matter and ESP-IDF samples, it is under the Apache License.
