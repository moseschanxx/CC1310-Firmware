# CC1310 Unified RX/TX Firmware

面向 TI CC1310F128 的统一无线固件、驻留 UART bootloader 与配套主机工具。一个应用镜像可由 bootloader metadata 在启动时选择运行 RX 或 TX 角色，支持 J-Link 工厂烧录、UART OTA 更新和角色切换。

> 本项目面向 CC1310F128；Flash、CCFG、射频参数和引脚配置均与硬件相关。烧录或修改无线参数前，请确认目标硬件和当地无线法规。

## 主要功能

- **统一应用镜像**：应用链接到 `0x00008000`，由 metadata 选择 RX 或 TX；未明确为 TX 时安全回退 RX。
- **RX/TX 无线功能**：RX 连续接收 proprietary RF 数据并维护队列/统计；TX 可经 UART CLI 发送同步时间包或帧同步包。
- **驻留 bootloader**：位于 Flash `0x00000000–0x00007FFF`，负责应用校验、启动角色、确认启动与 UART 更新。
- **掉电安全 metadata**：metadata 使用双页 append-only journal；更新或启动异常时，设备保守地停留在 UART 更新模式。
- **UART OTA**：使用 CRC32 校验的 `.pkg` 升级包；主机端支持查询、刷写和切换 RX/TX 角色。
- **J-Link 恢复/量产烧录**：一次性写入 bootloader、应用、CCFG 和有效 metadata。
- **串口 CLI 与测量接口**：默认 UART 为 `115200 8N1`，DIO1 可输出 TX/RX 时序脉冲以支持延迟测量。

当前射频配置为 **433.000 MHz、50 kBaud、2-GFSK、-10 dBm**。配置位于 `firmware/smartrf_settings/`。

## 目录

| 路径 | 说明 |
| --- | --- |
| `firmware/` | 统一 RX/TX 应用、UART CLI、无线逻辑、链接脚本和构建/烧录脚本。 |
| `bootloader/` | NoRTOS bootloader、CCFG、metadata journal、镜像校验与 UART 更新协议。 |
| `tools/` | OTA package、UART 更新、J-Link metadata 与测试工具。 |
| `analysis/` | 无线延迟采集数据及分析脚本。 |
| `tirtos_builds_CC1310_LAUNCHXL_release_ccs/` | firmware 构建使用的 CCS/TI-RTOS 生成配置。 |
| `toolchains/` | 项目内 TI ARM 编译工具链；默认使用 `ti-cgt-arm_18.12.5.LTS`。 |

## 构建依赖

需要：

- TI ARM CGT `18.12.5.LTS`，默认位置为 `toolchains/ti-cgt-arm_18.12.5.LTS`。
- SimpleLink CC13x0 SDK `4.20.02.07`，默认位置为 `/Applications/ti/simplelink_cc13x0_sdk_4_20_02_07`。
- Python 3（用于 package 生成、校验与 OTA 工具）。
- SEGGER J-Link Software（仅 J-Link 烧录需要；`JLinkExe` 应在 `PATH` 中）。

`SIMPLELINK_SDK` 和 `TI_ARM_CGT` 可覆盖默认路径：

```sh
TI_ARM_CGT="$PWD/toolchains/ti-cgt-arm_18.12.5.LTS" \
SIMPLELINK_SDK=/path/to/simplelink_cc13x0_sdk_4_20_02_07 \
./firmware/build.sh
```

`firmware/firmware_build.h` 中的 `FIRMWARE_VERSION` 是唯一发布版本源，必须在每次发布时递增。构建脚本将 `major.minor.patch` 编码为 32-bit 的 `0x00MMmmpp`：最高字节保留为 0，`major`、`minor`、`patch` 各占一个字节（范围均为 0–255），例如 `0.2.0` 编码为 `0x00000200`。OTA header 以该 4-byte 值存储，主机工具与设备 `info` 均显示为 `major.minor.patch`。构建脚本会拒绝缺少编译器、HEX 工具或 SDK 的环境。

## 编译

在工作区根目录执行：

```sh
./bootloader/build.sh
./firmware/build.sh
python3 -m unittest discover -s tools/tests -v
```

构建结果：

| 文件 | 用途 |
| --- | --- |
| `bootloader/build/bootloader.out` | bootloader 镜像。 |
| `firmware/boot_build/nonrom_test/firmware.out` | 链接到 `0x00008000` 的应用镜像。 |
| `firmware/boot_build/nonrom_test/firmware.hex` | 地址保持的 Intel HEX，供 J-Link 烧录应用。 |
| `firmware/boot_build/nonrom_test/firmware.pkg` | CRC32 保护的 UART OTA 包，目标 ID 为 `CC1M`。 |

验证 OTA 包：

```sh
python3 tools/fw_package.py --verify firmware/boot_build/nonrom_test/firmware.pkg
```

清理可再生产物：

```sh
./bootloader/build.sh clean
./firmware/build.sh clean
```

## J-Link 工厂烧录或恢复

> **警告：此操作会全片擦除。** 它会清除已有应用、metadata 和其他 Flash 数据。

完成上述构建后，明确选择启动角色：

```sh
./firmware/flash_all_jlink.sh -r rx
# 或
./firmware/flash_all_jlink.sh -r tx
```

多探针环境指定序列号：

```sh
./firmware/flash_all_jlink.sh -s 123456789 -r rx
```

该脚本依次擦除芯片、烧录 bootloader、从 `0x00008000` 烧录应用 HEX，并写入已验证 package 对应的 metadata。不要单独烧录 `firmware.hex` 或将 `firmware.out` 当作零地址镜像烧录：应用没有 CCFG，且需要有效 metadata 才能启动。

接线与故障排查见 [firmware/build_flash.md](firmware/build_flash.md)。

## UART OTA 更新

设备进入 bootloader 更新模式后，使用主机工具刷写 package，并选择角色：

```sh
python3 tools/fw_update.py --port /dev/cu.usbserial-XXXX flash \
  --package firmware/boot_build/nonrom_test/firmware.pkg --role rx
```

切换已安装有效镜像的角色无需重新传输应用：

```sh
python3 tools/fw_update.py --port /dev/cu.usbserial-XXXX set-role --role tx
```

查询设备状态：

```sh
python3 tools/fw_update.py --port /dev/cu.usbserial-XXXX info
```

例如，`info` 会显示 `target=CC1M(0x4343314D)`、
`version=0.2.0(0x00000200)`、`role=rx(1)`；状态也会以
`state=update_requested(2)` 的形式同时给出名称和原始数值。

bootloader 会校验 package header、目标 ID、应用地址和 CRC32。它使用单 App slot，不支持断点续传或 A/B 回滚；更新中断、镜像异常或连续三次未确认启动都会使设备进入 UART 更新模式。

## 安全与硬件边界

- OTA 的 CRC32 只保证完整性，**不提供来源认证或加密**。可物理访问升级 UART 的人员可以刷写自行构造但 CRC 正确的镜像；部署时必须控制该接口的物理访问。
- bootloader 独占 `0x00000000–0x00007FFF`、metadata 页 `0x6000/0x7000` 和 CCFG。应用仅可使用 `0x00008000–0x0001EFFF`。
- 修改 Flash 布局、CCFG、RF 频率/功率、调制方式或引脚分配时，必须同步审查 bootloader、firmware、主机工具和硬件验证流程。

## 更多文档

- [Firmware 设计](firmware/design.md)
- [RX 数据流](firmware/RF_PACKET_DATA_FLOW.md)
- [Firmware CLI](firmware/cli.md)
- [Bootloader 设计与 OTA 协议](bootloader/BOOTLOADER_DESIGN.md)
- [J-Link 构建与烧录说明](firmware/build_flash.md)
