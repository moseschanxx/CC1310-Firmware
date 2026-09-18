# CC1310 Combined Firmware

这是 CC1310F128 的统一应用镜像：同一份程序根据 bootloader metadata 在启动时进入 RX 或 TX 角色。它不是 TI 示例工程中的独立 Packet RX 镜像。

应用由驻留 bootloader 启动，链接地址为 `0x00008000`，使用 custom、non-ROM SYS/BIOS 库。bootloader 保留 `0x00000000–0x00007FFF`、metadata 页和 CCFG；应用不应直接覆盖这些区域。

## 功能与配置

- RX：连续 proprietary RF 接收；使用 32 深度应用 mailbox 保存数据，支持统计和可选逐包串口输出。
- TX：通过 CLI 发送 30-byte 同步时间包或帧同步包。
- 启动角色：metadata 为 `tx` 时运行 TX，其他情况默认 RX。
- RF 设置在 `smartrf_settings/smartrf_settings.c`：当前为 **433.000 MHz、50 kBaud、2-GFSK、-10 dBm**。修改频率、功率或调制参数前须评估硬件能力和当地无线法规。
- UART CLI：`Board_UART0`，115200 8N1；具体命令参见 [CLI.md](CLI.md)。
- DIO1 默认在 TX 执行和 RX 处理期间输出时序脉冲，供延迟测量使用。

## 构建

构建依赖 TI ARM CGT、SimpleLink CC13x0 SDK 4.20.02.07、XDCtools/TI-RTOS。脚本默认使用工作区相邻的 `../ti/ti-cgt-arm_18.12.5.LTS`，SDK 路径为 `/Applications/ti/simplelink_cc13x0_sdk_4_20_02_07`。

在 `firmware/` 中执行：

```sh
FW_PACKAGE_VERSION=111 ./build.sh
```

`FW_PACKAGE_VERSION` 默认为 `110`。每次发布应使用新的递增值。成功后生成：

| 文件 | 用途 |
| --- | --- |
| `boot_build/nonrom_test/firmware.out` | 链接后的应用 ELF/OUT。 |
| `boot_build/nonrom_test/firmware.hex` | 地址从 `0x00008000` 开始的 Intel HEX。 |
| `boot_build/nonrom_test/firmware.pkg` | UART OTA 包，target ID 为 `0x4343314D`（`CC1M`）。 |

脚本会校验生成的 OTA 包。也可单独校验：

```sh
python3 ../tools/fw_package.py --verify boot_build/nonrom_test/firmware.pkg
```

## 运行模型

主 radio Task 的优先级为 2；CLI 和 RX 的 `packet_print` Task 优先级为 1。RX 角色创建 radio、CLI 和 packet-print 三个应用 Task；TX 角色创建 radio 和 CLI Task。`stack` CLI 命令可查看每个 Task 的栈高水位，作为缩减 SRAM 前的实测依据。

应用仅在 RF 初始化完成且 UART CLI 已打开、命令表已安装后调用 `bl_confirm_boot()`。若需要 OTA 更新，使用 CLI 的 `bootloader` 命令请求复位到 bootloader；烧录和恢复流程见 [JLINK.md](JLINK.md)。

## 目录

| 路径 | 内容 |
| --- | --- |
| `firmware.c` | RX radio Task。 |
| `firmware_tx.c` | TX radio Task 与 TX CLI。 |
| `rf_packet_queue.c` | RX mailbox、统计和逐包输出。 |
| `cli_*.c` | 串口 CLI、角色命令和 bootloader 命令。 |
| `boot_app.cmd` | bootloader 应用的 Flash/SRAM 布局。 |
| `boot_build/boot_release.cfg` | non-ROM SYS/BIOS 配置。 |
| `smartrf_settings/` | SmartRF 导出的无线参数。 |
