# CC1310 Unified Firmware Bootloader Design

## Scope and security boundary

该 bootloader 面向 CC1310F128，在 Flash `0x00000000` 启动，通过 UART 安装并启动位于
`0x00008000` 的统一应用镜像。应用 package 的目标 ID 固定为 `0x4343314D`（`CC1M`）；
同一镜像由 metadata 中的角色选择在启动时运行 RX 或 TX。

该设计采用单一 App slot，而非 A/B 应用 slot。它提供 header、metadata 和镜像 CRC32
完整性检查，可防止意外传输或 Flash 损坏，但**不提供来源认证**。持有升级 UART 物理访问
权限的人能够安装自行构造且 CRC 正确的镜像；部署时必须控制升级接口的物理访问。

## Flash 和 SRAM 布局

| 区域 | 地址范围 | 大小 | 所有者 |
| --- | --- | ---: | --- |
| Bootloader | `0x00000000–0x00005FFF` | 24 KiB | Bootloader |
| Metadata A | `0x00006000–0x00006FFF` | 4 KiB | Bootloader |
| Metadata B | `0x00007000–0x00007FFF` | 4 KiB | Bootloader |
| App slot | `0x00008000–0x0001EFFF` | 92 KiB | Unified firmware |
| CCFG/reserved | `0x0001F000–0x0001FFFF` | 4 KiB | Bootloader |

Bootloader 是唯一包含 `ccfg.c` / `.ccfg` 的镜像。应用使用 `boot_app.cmd` 链接，Flash
起点为 `0x00008000`、长度为 `0x17000`，不得包含 `ccfg.c`。App 可用 SRAM 为
`0x20000000–0x20004DFF`；`0x20004E00–0x20004EFF` 用于 boot 调试，
`0x20004F00–0x20004FFF` 用于 boot handoff。

## Persistent metadata

Metadata 是写入 A/B 页的 append-only journal。记录共 48 bytes，最后才编程
`recordCrc32`；掉电造成的不完整记录因 CRC 无效而被忽略。

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t formatVersion;          /* 当前为 2 */
    uint16_t recordSize;
    uint32_t sequenceNumber;
    uint32_t state;
    uint32_t imageSize;
    uint32_t imageCrc32;
    uint32_t firmwareVersion;
    uint32_t appRole;                /* unset=0, rx=1, tx=2 */
    uint32_t unconfirmedBootCount;
    uint32_t bootAttemptId;
    uint32_t confirmedBootAttemptId;
    uint32_t recordCrc32;
} BootMetadata;
```

状态值为：

| 值 | 状态 | 含义 |
| ---: | --- | --- |
| 1 | `VALID_APPLICATION` | 可在向量表和镜像 CRC 均有效时启动。 |
| 2 | `UPDATE_REQUESTED` | 应用请求更新；复位后进入 UART 更新器。 |
| 3 | `UPDATE_IN_PROGRESS` | 更新开始或失败；禁止启动 App。 |

读取时从两页选择最新的 CRC 有效记录。两页均写满时，bootloader 擦除不含最新记录的页，
再写入新记录，因此另一页始终保留最新有效记录直至新写入完成。

## Boot decision, confirmation and handoff

每次复位后的决策为：

```text
metadata 不是 VALID_APPLICATION  -> UART update mode
unconfirmedBootCount >= 3         -> UART update mode
App 向量表或 image CRC 无效       -> UART update mode
否则                              -> 记录一次未确认启动并跳入 App
```

跳转前 bootloader 增加 `unconfirmedBootCount` 与 `bootAttemptId`，并在
`0x20004F00` 写入：

```c
typedef struct {
    uint32_t magic;
    uint32_t bootAttemptId;
    uint32_t appRole;
} BootHandoff;
```

应用从 handoff 读取角色；只有 `tx` 明确选择 TX，其他值一律回退 RX。应用在 UART/RF
关键初始化成功后调用 `bl_confirm_boot()`，bootloader API 仅对匹配且尚未确认的
`bootAttemptId` 追加确认记录，并将未确认计数减一。复位或掉电发生在确认之前会保留该次
计数；因此故障只会使系统更保守地进入更新模式。

跳转前 bootloader 验证 MSP 落在 `[0x20000000, 0x20005000)`，ResetISR 为 App slot 内的
Thumb 地址，并重算镜像 CRC32。随后关闭 SysTick、禁用/清除 NVIC 中断、将 VTOR 指向
`0x00008000`、恢复 PRIMASK、装载 App MSP 并跳转 ResetISR。恢复 PRIMASK 是必要的：
否则 bootloader 的关中断状态会泄漏给 TI-RTOS App。

## Bootloader API

应用只能通过固定 Flash 地址 `0x00005000` 的 ABI 调用 bootloader：

```c
typedef struct {
    uint32_t magic;                  /* BLAP */
    uint16_t version;                /* 当前为 1 */
    uint16_t reserved;
    void (*confirmBoot)(uint32_t bootAttemptId);
    void (*requestUpdate)(void);
} BootloaderApi;
```

`boot_api.c` 先验证 API magic 与版本，再调用上述函数；不匹配时安全返回。CLI 的
`bootloader` 命令会写 `UPDATE_REQUESTED` 后复位。

## Package format

升级 package 为 32-byte header 加 raw App bytes。header 不写入 App slot，App 向量表
始终位于 `0x00008000`。

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;                  /* FWPK */
    uint16_t formatVersion;          /* 1 */
    uint16_t headerSize;             /* 32 */
    uint32_t targetId;               /* 必须为 CC1M / 0x4343314D */
    uint32_t applicationAddress;     /* 必须为 0x00008000 */
    uint32_t imageSize;              /* 非零、4-byte 对齐且 <= 0x17000 */
    uint32_t firmwareVersion;
    uint32_t imageCrc32;
    uint32_t headerCrc32;            /* 前 28 bytes 的 IEEE CRC32 */
} FirmwarePackageHeader;
```

`BEGIN` 通过 header 后，bootloader 先追加 `UPDATE_IN_PROGRESS`，然后才擦除 App slot。
`END` 只有在全部数据写入且 App Flash CRC32 与 `imageCrc32` 相符时，才追加
`VALID_APPLICATION`。任意 header 错误、写入错误、超时、CRC 不符或复位都会使下次启动
进入 UART 更新模式。

## UART protocol v2

UART 为 115200 8N1、二进制模式。每帧先 COBS 编码并以 `0x00` 终止；解码后格式为：

```text
protocolVersion:u8 | type:u8 | sequence:u16 | payloadLength:u16 |
payload | frameCrc16:u16
```

协议版本为 2；帧 CRC 使用 CRC-16/CCITT-FALSE，package 与镜像使用 IEEE CRC32。

| 消息 | 方向 | Payload |
| --- | --- | --- |
| `HELLO` / `INFO` | Host → target / target → host | 空 / target、状态、slot 大小、镜像大小、版本、角色（24 B）。 |
| `BEGIN` / `READY` | Host → target / target → host | `FwPackageHeader + role:u32` / 起始 offset。 |
| `DATA` / `ACK` / `NACK` | Host / target | `offset:u32 + 1..128 B`（4-byte 对齐）/ 下一个期望 offset。 |
| `END` / `COMPLETE` | Host / target | 空 / 空。 |
| `SET_ROLE` / `COMPLETE` | Host / target | `rx` 或 `tx` 的 role:u32 / 空。 |
| `ERROR` | target | 空。 |

传输采用 stop-and-wait。对已成功写入数据的精确重复 `DATA`，bootloader 返回当前 `ACK`；
未来 offset 返回 `NACK`。不支持跨复位续传：中断后需重新 `BEGIN` 并完整发送镜像。

`SET_ROLE` 只对当前有效 App 生效，不重写镜像；它追加 metadata、更新角色并复位。Host
工具用 `fw_update.py set-role --role rx|tx` 调用它。

## Build, deployment and update

构建统一应用：

```sh
FW_PACKAGE_VERSION=111 ./firmware/build.sh
python3 tools/fw_package.py --verify firmware/boot_build/nonrom_test/firmware.pkg
```

通过 UART 更新并同时指定启动角色：

```sh
python3 tools/fw_update.py --port /dev/cu.usbserial-XXXX flash \
  --package firmware/boot_build/nonrom_test/firmware.pkg --role rx
```

切换已安装有效镜像的角色：

```sh
python3 tools/fw_update.py --port /dev/cu.usbserial-XXXX set-role --role tx
```

首次部署或恢复使用 J-Link 全量烧录：

```sh
./bootloader/build.sh
FW_PACKAGE_VERSION=111 ./firmware/build.sh
./firmware/flash_all_jlink.sh -r rx
```

全量烧录会全片擦除，然后烧录 bootloader、`0x8000` 应用 HEX 和有效 metadata。不能只烧录
应用 HEX：没有有效 metadata 时 bootloader 不会启动它。现场升级应使用 `firmware.pkg` 的
UART 流程，避免 J-Link 全擦除。

## Validation priorities

1. 正常 RX 与 TX 启动、`bl_confirm_boot()` 和连续重启确认。
2. `bootloader` CLI 请求、完整 UART 更新、`set-role` 与 metadata 角色持久化。
3. 错误 target/header/image CRC、越界或非对齐数据、重复 DATA 与丢失 ACK。
4. metadata 写入、App 擦除、数据编程和最终 VALID 提交各阶段的断电恢复。
5. J-Link 全量恢复后 metadata、角色、向量表与应用 CRC 的一致性。
