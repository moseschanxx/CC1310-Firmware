# J-Link 烧录 CC1310F128

`flash_all_jlink.sh` 用 SEGGER J-Link 通过两线 cJTAG 全量烧录 bootloader、统一应用和有效 metadata。该操作适用于首次部署或恢复，不用于现场 OTA 更新。

## 构建依赖

构建 CC1310F128 固件需要两类 TI 组件：项目内的 ARM 编译工具链，以及外部安装的 SimpleLink CC13x0 SDK。二者用途不同，不能互相替代。

- 默认 ARM 工具链为工作区内的 `toolchains/ti-cgt-arm_18.12.5.LTS`，提供 `armcl`、`armhex`、运行库和编译器头文件。可通过 `TI_ARM_CGT=/path/to/ti-cgt-arm_18.12.5.LTS` 覆盖。
- `simplelink_cc13x0_sdk_4_20_02_07` 默认安装在 `/Applications/ti/simplelink_cc13x0_sdk_4_20_02_07`。它提供 CC1310 平台的 TI-RTOS/NoRTOS 源码、头文件及 RF、DPL、driverlib、Display、GRLIB、SPIFFS 等库。可通过 `SIMPLELINK_SDK=/path/to/simplelink_cc13x0_sdk_4_20_02_07` 覆盖。

例如 SDK 安装在其他位置时：

```sh
SIMPLELINK_SDK=/opt/ti/simplelink_cc13x0_sdk_4_20_02_07 \
    ./firmware/build.sh
```

构建脚本会在开始时检查工具链中的 `armcl`、`armhex` 和 SDK 根目录。缺少 SDK 时，即使 ARM 编译器存在，也会因缺少 CC1310 的平台头文件和库而无法完成编译或链接。

## 接线

目标板和 J-Link 必须共地，目标板需自行供电；`VTref` 仅用于 J-Link 检测目标 I/O 电平。

| J-Link ARM 20-pin | CC1310 目标 | 说明 |
| --- | --- | --- |
| 1 `VTref` | 目标 VDD | 电平参考，CC1310 为 1.8–3.8 V。 |
| 7 `TMS/SWDIO` | `DIO` / `TMSC` | cJTAG 双向数据线。 |
| 9 `TCK/SWCLK` | `TCKC` | cJTAG 时钟。 |
| 15 `nRESET` | `RESET_N` | 建议连接，便于恢复连接。 |
| 任一 GND | GND | 必接。 |

`DIO` 不是四线 JTAG 的独立 TDI/TDO。首次使用 1000 kHz；连接不稳定时使用 `JLINK_SPEED=100` 降速。

## 构建并烧录

在工作区根目录构建 bootloader 和应用：

```sh
./bootloader/build.sh
./firmware/build.sh
```

清理可再生构建产物后重新构建：

```sh
./bootloader/build.sh clean
./firmware/build.sh clean
./bootloader/build.sh
./firmware/build.sh
```

`bootloader/build.sh clean` 删除专用的 `bootloader/build/` 输出目录。`firmware/build.sh clean` 仅删除 `firmware/boot_build/nonrom_test/` 下的对象文件、映像、映射文件和 OTA 包，保留版本控制的 TI-RTOS 配置源文件。

然后选择启动角色并烧录。RX 是默认角色：

```sh
./firmware/flash_all_jlink.sh -r rx
./firmware/flash_all_jlink.sh -r tx
```

指定探针序列号：

```sh
./firmware/flash_all_jlink.sh -s 123456789 -r rx
```

脚本默认使用：

- bootloader：`bootloader/build/bootloader.out`
- app HEX：`firmware/boot_build/nonrom_test/firmware.hex`
- app package：`firmware/boot_build/nonrom_test/firmware.pkg`

可通过 `-b`、`-a`、`-p` 覆盖这些路径。`-a` 与 `-p` 必须是一对由同一次构建生成的匹配文件：脚本会验证 HEX 的内容与 package 的 CRC/长度完全一致，然后依据 `-r` 写入 metadata。metadata 决定统一应用启动为 RX 或 TX。

## 行为与限制

脚本调用 `JLinkExe`，使用 `CC1310F128`、`cJTAG`、`-ExitOnError 1`，并依次：

1. 连接并**全片擦除**目标；
2. 烧录 bootloader OUT；
3. 烧录从 `0x00008000` 开始的应用 HEX；
4. 写入经 package 验证的启动 metadata；
5. 复位并运行。

全片擦除会清除已有应用、metadata 和其他 Flash 数据。不要单独烧录 `firmware.hex`：没有有效 metadata 时 bootloader 不会启动该应用。也不要将 `firmware.out` 当作零地址 standalone 镜像烧录；该应用不含 CCFG，向量表位于 `0x00008000`。

## 故障排查

1. 安装 SEGGER J-Link Software Pack，确保 `JLinkExe` 在 `PATH` 中；否则设置 `JLINK_BIN=/path/to/JLinkExe`。
2. 检查目标供电、`VTref`、公共地以及 DIO/TCKC 接线。
3. 无法连接时用 `JLINK_SPEED=100 ./firmware/flash_all_jlink.sh -r rx`，并在按住 `RESET_N` 时启动脚本后释放。
4. 仅当脚本返回 0 时才判定烧录成功；它会先验证应用与 OTA package 的对应关系。

现场更新请使用 UART OTA 客户端及 `firmware.pkg`，而非 J-Link 全擦除流程。
