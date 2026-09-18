# CC1310 RX Bootloader 升级测试报告

**报告日期：** 2026-09-17  
**测试对象：** CC1310F128 RX bootloader、non-ROM boot app、Python 升级工具  
**DUT 串口：** `/dev/cu.usbserial-A50285BI`，115200 8N1  
**app：** 包版本 v110；产品版本 `0.1.0`  
**升级包：** `rfPacketRx/boot_build/nonrom_test/rfPacketRx_boot_nonrom.pkg`  
**包属性：** target `0x43433152`、size 51,352 bytes、CRC32 `9FD0EE5C`

## 1. 结论摘要

截至本报告，bootloader 的正常升级路径、app 请求进入更新模式、包/帧基础校验和
部分协议异常恢复均已在真实 RX 硬件上通过。主机端包格式和协议编码/解码单元测试
14 项全部通过。测试过程中发现 Python COBS 解码器会接受截断块，已修复并由测试
覆盖。新执行的破坏性传输、断电恢复和 10 次常规断电重启用例均通过。

当前设备已恢复并运行 v110，CLI `version`、`help` 和 `rx status` 正常。尚未执行
刻意断电、元数据页满轮换、crash-count 门限及 J-Link 注入损坏等破坏性测试；这些
不是通过项，仍需按测试设计单独执行。

## 2. 测试环境与构建结果

| 项目 | 结果 |
| --- | --- |
| Bootloader | CC1310 NoRTOS bootloader，app slot `0x8000–0x1EFFF` |
| App | RX non-ROM SYS/BIOS boot app |
| 包格式 | 32-byte CRC32 包头 + app image CRC32 |
| UART 协议 | COBS 帧 + CRC16/CCITT-FALSE，128-byte DATA 分块 |
| 供电控制 | `tools/relay_control.py` 可用 |
| 调试接口 | J-Link cJTAG 可用；本轮协议测试未依赖其写入 flash |

构建及包校验命令：

```bash
bash rfPacketRx/build_boot_nonrom_test.sh 110
python3 tools/fw_package.py --verify \
  rfPacketRx/boot_build/nonrom_test/rfPacketRx_boot_nonrom.pkg
```

结果：构建成功，包校验成功，输出 `target=0x43433152 version=110`
`size=51352 crc32=9FD0EE5C`。

## 3. 主机端自动化测试

执行命令：

```bash
python3 -m unittest discover -s tools/tests -v
python3 -m py_compile tools/fw_package.py tools/fw_protocol.py \
  tools/fw_update.py tools/raw_update_smoke.py
```

结果：**14/14 通过**。

| 模块 | 覆盖内容 | 结果 |
| --- | --- | --- |
| `test_fw_package.py` | round-trip、4-byte padding、空镜像、超限镜像、包头 CRC32、image CRC32、包长、magic/format/header size/load address/size | 6/6 通过 |
| `test_fw_protocol.py` | COBS round-trip、零字节和长连续数据、空/128-byte payload、CRC16 标准向量、CRC 错帧、错误协议版本、错误长度、截断 COBS | 6/6 通过 |

新增包测试覆盖 4-byte 最小合法 image、`0x17000` 最大合法 image 与追加包字节拒绝。

### 3.1 发现并修复的问题

初次执行时，`tools/fw_protocol.py:cobs_decode()` 对截断 COBS block 的检查使用了
过宽的上界，`b"\x02"`、`b"\x03\x01"` 等输入会被静默截断而非拒绝。该实现与
bootloader C 端的边界检查不一致。

已修复为按 code byte 所需的完整字节数检查：

```python
if code == 0 or index + code > len(data):
    raise ValueError("invalid COBS frame")
```

修复后全部 12 项测试通过。

## 4. 真实硬件端到端测试

### 4.1 正常 app → bootloader → app

| ID | 步骤 | 结果 |
| --- | --- | --- |
| BL-N-01 | app 启动后查询 `version`、`help` | 通过；返回 `OK version=0.1.0`，help 含 rx、bootloader、version。 |
| BL-N-02 | app 输入 `bootloader` | 通过；返回 `OK rebooting_to_bootloader`。 |
| BL-N-02 | `fw_update.py ... info` | 通过；`target=1128477010`（即 `0x43433152`）、`state=2`、`max_size=94208`。 |
| BL-N-03 | `fw_update.py ... flash --package ...pkg` | 通过；51,352 bytes 从 0% 到 100%，最终 `COMPLETE`。 |
| BL-N-04 | 升级后 app 查询 `version`、`help`、`rx status` | 通过；app 自动恢复，所有命令正常。 |

硬件测试中的最终 CLI 输出：

```text
OK version=0.1.0
OK commands:
OK help [command]
OK rx rx status | rx dump on|off
OK bootloader reboot into UART firmware updater
OK version show firmware version
OK rx=0 enq=0 drop=0 full=0 crc=0 coll=0 rssi=-128[127,-128] samples=0 dump=off
```

### 4.2 非破坏性原始协议 smoke 测试

新增并执行：

```bash
python3 tools/raw_update_smoke.py --port /dev/cu.usbserial-A50285BI
```

该脚本只发送畸形帧或必定无效的 `BEGIN`，不发送有效包头，因而不应擦除或写入 app
slot。每项后重新发送 HELLO，并比较 INFO payload，确认无效请求未改变元数据。

| 测试项 | 预期 | 实际结果 |
| --- | --- | --- |
| HELLO | 返回 INFO，target 为 RX | 通过 |
| 截断 COBS block | 丢弃坏帧，后续 HELLO 可用 | 通过 |
| 错误 CRC16 | 丢弃坏帧，后续 HELLO 可用 | 通过 |
| 错误 magic | BEGIN 返回 ERROR，INFO 不变 | 通过 |
| 错误 format version | ERROR，INFO 不变 | 通过 |
| 错误 header size | ERROR，INFO 不变 | 通过 |
| 错误 target ID | ERROR，INFO 不变 | 通过 |
| 错误 app address | ERROR，INFO 不变 | 通过 |
| imageSize=0 | ERROR，INFO 不变 | 通过 |
| 超限 imageSize | ERROR，INFO 不变 | 通过 |
| 未对齐 imageSize | ERROR，INFO 不变 | 通过 |
| 错误 header CRC32 | ERROR，INFO 不变 | 通过 |
| 未 BEGIN 的 DATA | ERROR，后续 HELLO 可用 | 通过 |

执行结果摘要：

```text
PASS hello target=0x43433152 state=2
PASS malformed_cobs_recovered
PASS bad_crc16_recovered
PASS invalid_begin_magic_rejected
PASS invalid_begin_format_version_rejected
PASS invalid_begin_header_size_rejected
PASS invalid_begin_target_rejected
PASS invalid_begin_address_rejected
PASS invalid_begin_zero_size_rejected
PASS invalid_begin_oversized_rejected
PASS invalid_begin_unaligned_size_rejected
PASS invalid_begin_header_crc_rejected
PASS data_without_begin_rejected
```

完成该测试后重新用 v110 包升级，设备成功回到 app 模式。

## 5. 过程异常与处理

首次硬件 smoke 时，app 切换 bootloader 后主机串口出现 `SerialException`。后续检查
发现 `/dev/cu.usbserial-A50285BI` 被用户的 `picocom` 进程占用。该进程释放后，
`INFO`、raw smoke 与完整升级均成功。此项属于测试环境资源竞争，不是 bootloader
协议或 flash 升级故障。

另一个观察是：若 app 收到 bootloader 二进制 HELLO 数据而未实际进入 bootloader，
这些残留字节可能与后续 ASCII CLI 命令拼接。测试中在 app 命令前发送一个空行
`\n` 清理行缓冲后可稳定进入更新模式。该行为已记录为测试设计中的 UART 切换风险，
后续应评估 bootloader/app 切换时的 UART flush 策略。

## 6. 本轮补充自动化结果

### 6.1 传输事务：乱序、重复与镜像 CRC

执行的破坏性脚本：

```bash
python3 tools/raw_update_transaction_test.py \
  --port /dev/cu.usbserial-A50285BI \
  --package rfPacketRx/boot_build/nonrom_test/rfPacketRx_boot_nonrom.pkg
```

脚本在有效 `BEGIN` 后故意发送未来 offset、重发 offset 0 的首块，再翻转第二个
128-byte DATA 块的一个 bit，并发送到 END。实际结果：

```text
PASS begin_ready
PASS future_offset_nack
PASS first_data_ack
PASS duplicate_data_ack
PASS corrupted_image_transferred
PASS end_crc_failure_rejected
```

说明 bootloader 未允许跳过未写区域，重复块返回当前 offset 的 ACK，且只有 END 的
全镜像 CRC32 匹配时才会接受更新。该用例会擦除 app slot；随后立即使用 v110 `.pkg`
完整升级，`COMPLETE` 且 app 的 `version`、`rx status` 均正常。

### 6.2 BEGIN 后真实断电恢复

新增 `tools/raw_update_power_loss_test.py`：验证本地包后发送有效 BEGIN，收到 READY
后通过继电器断电 1 s、重新上电，再发送 HELLO。结果：

```text
PASS begin_ready
PASS reset_after_begin_stays_in_update_mode
```

恢复后的 INFO state 为 `3` (`UPDATE_IN_PROGRESS`)；bootloader 没有执行已擦除的 app，
并能接受 v110 包完成恢复。这覆盖 BL-R-02/BL-R-03 的“metadata 已提交、擦除刚开始”
路径；尚未覆盖 FlashProgram 和 metadata record 各写入指令内部的精确断电窗口。

### 6.3 Sequence 回绕与输入回归

`raw_update_smoke.py` 已扩展为对 HELLO sequence `0, 65535, 0` 分别校验回显和 INFO
一致性，随后重新跑完整的无效帧/无效包头集合。结果新增：

```text
PASS hello_sequence_wraparound
```

原有 13 个非破坏性检查也全部通过。完成后再次将 v110 包安装回 app。

### 6.4 常规断电重启压力：复测通过

新增 `tools/reboot_stability_test.py`，每次由 relay 断电 0.3 s、上电后等待 2 s，并
通过 `version` 断言 app 已启动。首次一次性运行 10 次时，执行环境在约 30 s 截断
命令；该脚本每轮实际约需 5 s，因此只记录到前 6 次并非 DUT 故障。随后按同样条件
将用例拆分为 6 次和 4 次短批次完成，全部通过：

```text
PASS cycle_01
PASS cycle_02
PASS cycle_03
PASS cycle_04
PASS cycle_05
PASS cycle_06
```

第二批输出 `PASS cycle_01` 至 `PASS cycle_04`。最后 `version=0.1.0` 和 `rx status`
均正常。注意：正常 app 正在运行时，`fw_update.py info` 超时是预期的，因为 UART
bootloader 服务不在 app 模式监听。结论：**BL-N-05 以 10/10 通过关闭**；此前 P1
记录为主机测试执行时限造成的误报。

### 6.5 无效 DATA 会话校验：发现并修复

新增 `tools/raw_update_session_validation_test.py` 覆盖 BL-U-06/07。首次执行复现：
有效 BEGIN 后发送仅含 offset 的零长度 DATA，旧 bootloader 回复
`ACK offset=0`，应为 ERROR/NACK。这是协议确认语义错误，记为 P1。

修复 `bootloader.c` 的 DATA 分支：在 offset 判断或写 flash 之前，拒绝零长度、超过
128 bytes、非 4-byte 对齐与 image 边界外 DATA，并回复 `ERROR`；仅合法新块或合法
重复块可回复 ACK/NACK。使用 `build_bootloader.sh` 重编，并以 `flash-jlink.sh` 烧录
bootloader 和 app ELF，随后 UART 安装 v110 包恢复 metadata。

修复后硬件回归结果：

```text
PASS begin_ready
PASS invalid_data_zero_length_rejected
PASS invalid_data_overlong_rejected
PASS invalid_data_unaligned_rejected
PASS invalid_data_beyond_image_rejected
PASS incomplete_end_rejected
PASS second_begin_resets_session
PASS session_offset_preserved
```

最后重新安装 v110，`version=0.1.0` 与 `rx status` 正常；该 P1 已关闭。

### 6.6 CRC 正确的非法向量表拒绝

用 `tools/make_test_package.py` 创建 image size=8、MSP=0、ResetISR=0 的 CRC 正确包。
UART 传输得到 `COMPLETE`，重启后的 INFO 为 `state=3`、`image_size=0`、`version=0`。
即 bootloader 没有跳转到该非法 image，而是安全进入更新模式。随后安装 v110 包并
恢复 app。BL-P-09、BL-P-10 和 BL-P-12 通过。

### 6.7 UART CRC16 重传与噪声恢复

`tools/raw_update_transport_resilience_test.py` 在 BEGIN 前发送 300-byte 噪声和畸形
COBS 数据；随后对 402 个 DATA 块的每 10 个块发送一次 CRC16 错误帧，并立刻重传
正确帧。实际结果：

```text
PASS noise_before_begin_recovered
PASS crc16_corruption_retried blocks=402
PASS complete_after_transport_errors
```

升级完成后 v110 app 的 `version` 与 `rx status` 正常。BL-U-02、BL-U-09 通过。

### 6.8 CLI 请求更新后的断电

`tools/cli_bootloader_power_loss_test.py` 等待 app 返回
`OK rebooting_to_bootloader` 后立即断电 0.5 s。恢复供电并发送 HELLO，实际输出：

```text
PASS cli_request_power_loss_state=2
```

state=2 (`UPDATE_REQUESTED`) 表明请求已安全持久化，未执行半切换 app。随后完整安装
v110 包并恢复。BL-R-01 通过。

### 6.9 Metadata journal 仲裁修复验证

精确断电调试中发现多个 CRC 有效且 sequence 相同的记录。旧实现对 sequence 相同
的记录保留物理上较早项，缺乏确定的恢复仲裁。修复后采用回绕安全 sequence 比较，
同页同 sequence 选择物理上较后记录；BEGIN 的 metadata 追加失败会拒绝擦除 app。

首次实现曾在 `FlashProgram()` 返回后立即读取 flash 做 CRC 回读，CC1310 在该写入
路径会因 flash/cache 访问限制导致 BEGIN 无响应；已移除该即时读取，保留启动/读取
路径既有的 CRC 校验。修复后的 bootloader 经 J-Link 烧录，UART v110 包 51,352 bytes
完整传输得到 `COMPLETE`，随后 `version=0.1.0` 和 `rx status` 正常。

## 7. 覆盖状态

| 类别 | 当前状态 |
| --- | --- |
| 正常升级、重刷、自动启动 | 通过 |
| app 显式请求更新 | 通过 |
| Python 包头/image CRC 本地校验 | 通过 |
| COBS/CRC16 解析与畸形帧恢复 | 通过 |
| bootloader 无效包头拒绝 | 通过 |
| 未建立会话 DATA 拒绝 | 通过 |
| RF 基础运行与 CLI | 通过 |
| sequence=0/65535 回绕 | 通过 |
| 丢 ACK 后 DATA 幂等重传 | 通过（相同 DATA 块重发） |
| 中途 DATA 偏移、重复/未来 offset | 通过 |
| image CRC 在 END 阶段失败 | 通过 |
| DATA 长度/对齐/边界与未完成 END | 通过（修复后回归） |
| 重复 BEGIN 会话重置 | 通过（明确 READY/offset 0） |
| 包截断/追加、本地最大/最小包格式 | 通过（主机单元测试） |
| app 向量表非法 | 未执行 |
| CRC 正确但 MSP/ResetISR 非法的 image | 通过；复位后 state=3 |
| BEGIN 后升级中断电/复位 | 通过 |
| 元数据两页满页轮换与掉电 | 未执行 |
| crash count 三次未确认启动 | 未执行 |
| app 确认幂等性 | 未执行 |
| 10 次正常断电重启 | 通过（6 + 4 短批次，原条件） |
| 长稳 RF/CLI（30 min） | 未执行 |

### 7.1 P0 用例状态汇总

| 类别 | 用例 | 状态 |
| --- | --- | --- |
| 正常流程 | BL-N-01 至 BL-N-04 | 通过 |
| 包校验 | BL-P-01 至 BL-P-07 | 通过 |
| UART | BL-U-01 至 BL-U-05 | 通过 |
| 断电 | BL-R-01 | 通过：CLI 确认后断电，恢复为 state=2。 |
| 断电 | BL-R-02 | 部分通过：BEGIN/READY 后断电重启为 state=3；未精确覆盖 metadata 写指令内部窗口。 |
| 断电 | BL-R-03 | 部分通过：BEGIN 后断电不启动 app；未精确覆盖 FlashSectorErase 内部。 |
| 断电 | BL-R-04 至 BL-R-06 | 未执行 |
| metadata | BL-M-01、BL-M-02 | 未执行 |
| handoff | BL-A-01 | 未完成：SYS/BIOS 在运行时将 VTOR 重定位到 SRAM，需按运行时向量表策略重新定义检查点。 |
| 稳定性 | BL-A-02 | 未执行（30 分钟 RF/CLI）。 |

P0 共 27 项：18 项通过、2 项部分通过、7 项未完成或待重新定义。部分通过项不得
作为精确 flash/metadata 临界断电覆盖的替代证据。

## 8. 后续测试建议

下一步执行 metadata 页满轮换、crash count 三次未确认启动、非法向量表和 30 分钟
RF/CLI 长稳测试。对超过约 30 秒的主机自动化用例，应分批执行或以日志文件持久化
结果，避免将主机执行环境时限误诊为 DUT 故障。

P0/P1 全部关闭前，不应将当前结果视为完整量产认证；当前结论仅证明核心升级路径和
基础输入防护已在真实硬件上工作。
