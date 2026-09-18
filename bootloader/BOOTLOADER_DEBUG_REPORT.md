# RX Bootloader / App 升级联调问题分析报告

## 1. 目标与范围

本次工作为 CC1310 RX 固件实现并验证串口升级链路：bootloader 加载位于
`0x8000` 的 app；app 通过 CLI `bootloader` 命令请求更新；Python 客户端发送带
CRC32 包头的升级包。Bootloader 维护双页元数据和未确认启动计数，app 在 RF
初始化成功后确认本次启动。TX 工程不在本次修改范围内。

## 2. 最终结果

RX 设备当前运行 app v108（51,224 bytes，CRC32 `27C75B4A`）。以下真实硬件
链路已验证：

1. app 串口 `help` 正确返回命令列表；
2. app 输入 `bootloader` 返回 `OK rebooting_to_bootloader`；
3. bootloader 的 `HELLO/INFO` 返回 `state=2`（显式请求更新）；
4. `tools/fw_update.py` 通过 `/dev/cu.usbserial-A50285BI` 完整写入并校验 v108，
   返回 `COMPLETE`；
5. bootloader 自动启动更新后的 app，CLI 再次正常响应。

## 3. 关键问题与定位过程

### 3.1 直接跳转没有恢复中断状态

Bootloader 初期能跳转到 app，但应用继承了 bootloader 设置的 `PRIMASK=1`。
这会阻止应用的中断和 RTOS 调度正常工作。修复是在 app 跳转前完成向量表切换、
清除 NVIC pending 位后执行 `CPUcpsie()`，使 handoff 等价于复位后进入 app。

### 3.2 ROM SYS/BIOS 不支持 app 重定位到 0x8000

原 app 使用 ROM SYS/BIOS。其部分启动与运行时入口隐含假设镜像位于 flash
零地址，重定位后出现 HardFault 和异常的 ROM 调用路径。因此为 boot app 生成
了非 ROM 的自定义 SYS/BIOS 库，并使用独立 app linker command file：

| 区域 | 地址/大小 | 用途 |
| --- | --- | --- |
| bootloader | `0x0000–0x5FFF` | 启动、元数据、升级协议 |
| 元数据页 | `0x6000`、`0x7000` | A/B 追加记录 |
| app slot | `0x8000–0x1EFFF` | 可升级 app |
| SRAM_APP | `0x20000000–0x20004DFF` | app 数据、heap、stack |

生成的向量表 reset entry 被替换为清晰的 `ResetISR()` wrapper，然后调用 TI
运行时 `_c_int00()`；此方式避免依赖零地址向量表。

### 3.3 `pthread_create()` HardFault 的根因

症状是 app 在 `pthread_create()` 内触发 HardFault：

- 启动标记已通过 `Board_initGeneral()`、队列初始化和 pthread 属性设置；
- CFSR 为 `0x00008600`，BFAR 为 `0x3EF0E49D`；
- 正确解析异常栈帧后，PC 为 `0x856A`，即 `IHeap_alloc()` 读取 heap 对象函数表；
- RAM `0x20002F00` 本应保存 `0x00013C20`，实际却是一段随机数据。

最初误以为是 pthread stack 或 Mailbox 参数问题。进一步检查 map 和 Intel HEX
发现 `.cinit` 长度为零，而 `.data` 被 `armhex` 输出为 RAM 地址记录。包工具只
抽取 `0x8000–0x1EFFF`，这些 RAM 记录不会进入升级包，造成 app 的全局初始化
数据没有从 flash 复制到 RAM。

根因是自定义链接命令缺少**链接器级** `--rom_model`。该选项必须位于 `-z`
之后；放在前面会被当作编译器选项并被忽略。修复后 map 显示：

```text
.cinit  0x00014260  length 0x280
.data   0x20002C18  UNINITIALIZED
```

此时 `.cinit` 已包含 `.data` 的压缩加载映像和 `.bss` 清零记录，`_c_int00()`
可在 app 启动时正确完成 C 运行时初始化。

### 3.4 UART 无响应

修复数据初始化后 app 已达到 `BIOS_start()`，未再发生 HardFault，但 CLI 无回应。
排查发现调试期间曾临时跳过 `PIN_init()` 和 `Board_initHook()`。恢复这两个标准
板级初始化步骤后，UART 引脚和外设状态恢复，CLI 正常工作。

## 4. 实现与恢复策略

Bootloader 位于 `bootloader/bootloader.c`，采用尽量少的状态：

- 包头含 magic、格式版本、目标 ID、app 地址、长度、版本、image CRC32 和
  header CRC32；CC1310 无 SHA-256/Ed25519 需求，本项目以 CRC32 做完整性校验。
- 接收前先写入 `UPDATE_IN_PROGRESS` 元数据；擦写或掉电失败时，下次启动继续
  更新模式，而不启动不完整 app。
- 完成时校验 flash 中 app CRC32，再写入 `VALID_APPLICATION` 元数据。
- 每次启动 app 前 `unconfirmedBootCount + 1`；app 的
  `bl_confirm_boot()` 仅在 RF 初始化成功后递减。达到失败阈值或 app 无效时，
  bootloader 拒绝启动 app 并进入升级模式。
- app 通过固定地址的 bootloader API 调用 `confirmBoot()` 与 `requestUpdate()`。

## 5. 验证方法与产物

构建命令：

```bash
bash rfPacketRx/build_boot_nonrom_test.sh 108
python3 tools/fw_package.py --verify \
  rfPacketRx/boot_build/nonrom_test/rfPacketRx_boot_nonrom.pkg
```

升级命令：

```bash
python3 tools/fw_update.py --port /dev/cu.usbserial-A50285BI flash \
  --package rfPacketRx/boot_build/nonrom_test/rfPacketRx_boot_nonrom.pkg
```

最终包为 `rfPacketRx/boot_build/nonrom_test/rfPacketRx_boot_nonrom.pkg`。
烧录 bootloader 时必须注意：J-Link 脚本会整片擦除，因而也会抹掉 `0x6000/0x7000`
元数据。raw J-Link 烧录 app 不会创建合法元数据；烧录 bootloader 后必须通过
Python 升级流程写入 app 包。

## 6. 后续建议

- 将 `build_boot_nonrom_test.sh` 由试验性名称整理为正式 boot app 构建入口，并将
  自定义 SYS/BIOS 生成步骤脚本化，避免依赖已有 `configPkg_nonrom5` 目录。
- 对升级协议增加自动化串口回归：非法包头、错误 CRC、中断传输、断电恢复、失败
  阈值和 app 确认计数。
- 将 HardFault 的 CFSR/BFAR 和自动堆栈帧采集固化为可选诊断功能，避免仅依赖
  J-Link 人工读取。
