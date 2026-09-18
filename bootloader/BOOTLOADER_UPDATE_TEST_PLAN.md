# CC1310 RX Bootloader 升级功能测试设计

## 1. 目的、范围与通过准则

本文定义 CC1310F128 RX bootloader 的验证方案，覆盖从 app 请求更新、UART 升级、
flash 写入、元数据恢复、app 启动确认到异常回退的完整链路。测试对象包括：

- `bootloader/bootloader.c`：升级协议、flash 操作、元数据 A/B journal、app handoff；
- `rfPacketRx/boot_api.c`、`cli_bootloader.c`：app 确认启动及请求更新；
- `tools/fw_package.py`、`tools/fw_protocol.py`、`tools/fw_update.py`：包和串口客户端；
- RX v0.1.0 或后续可识别版本的 non-ROM boot app。

通过标准：任何损坏、断电、通信失败或非法输入都不得执行不完整 app；有效包升级后
app 必须可启动、确认启动并可再次进入更新模式。所有 P0/P1 项必须通过，P2 项需
记录结果及已知限制。

## 2. 测试环境

| 项目 | 要求 |
| --- | --- |
| DUT | CC1310F128 RX，UART `/dev/cu.usbserial-A50285BI` |
| 供电控制 | `python3 tools/relay_control.py off/on`，可在指定阶段断电 |
| 调试 | J-Link Pro，cJTAG，CC1310F128，必要时读取 flash/RAM/异常寄存器 |
| 主机 | Python 3、`pyserial`、`tools/fw_update.py` |
| 日志 | 保存客户端 stdout/stderr、串口原始字节、J-Link 命令/输出、包 SHA-256 |

测试前构建两个可区分的有效 app 包 A/B，例如不同包版本和 CLI 启动文本：

```bash
bash rfPacketRx/build_boot_nonrom_test.sh 110
cp rfPacketRx/boot_build/nonrom_test/rfPacketRx_boot_nonrom.pkg /tmp/rx_A.pkg
# 修改可见版本或构建号后重新构建 B；不要覆盖 A。
bash bootloader/build_bootloader.sh
```

每个包均须通过：

```bash
python3 tools/fw_package.py --verify /tmp/rx_A.pkg
```

## 3. 可观测性与判定依据

### 3.1 串口和协议

UART 固定为 115200 8N1；帧为 COBS 编码并以 `0x00` 结束。解码帧包含协议版本、
消息类型、sequence、payload 长度、payload 和 CRC16/CCITT-FALSE。测试脚本应直接
调用 `tools/fw_protocol.py` 构造正常与畸形帧，不只依赖高层客户端。

`HELLO` 的 `INFO` payload 应为 20 bytes：target、state、最大 app 长度、当前
imageSize、firmwareVersion，均为 little-endian uint32。

### 3.2 元数据与启动状态

元数据页为 `0x6000` 和 `0x7000`，app 为 `0x8000–0x1EFFF`。必要时使用 J-Link
读取两页并离线校验 record CRC、sequence 单调性和最新记录选择。状态定义：

| 值 | 状态 | 预期 |
| ---: | --- | --- |
| 1 | VALID_APPLICATION | 合法且未超过失败门限时启动 app |
| 2 | UPDATE_REQUESTED | 进入 UART 更新模式 |
| 3 | UPDATE_IN_PROGRESS | 进入 UART 更新模式，禁止启动 app |

未确认启动门限当前为 3。每次 bootloader 决定启动 app 前加一；app 仅在 RF 初始化
成功后调用 `bl_confirm_boot()` 减一。观察启动确认时，应比较重启前后最新记录的
`unconfirmedBootCount`、`bootAttemptId` 与 `confirmedBootAttemptId`。

## 4. 正常功能测试

| ID | 优先级 | 前置条件 | 步骤 | 预期结果 |
| --- | --- | --- | --- | --- |
| BL-N-01 | P0 | 已有有效 app A | 上电，等待 5 s，发送 `help`、`version` | app 正常运行；版本正确；无 bootloader 二进制帧输出。 |
| BL-N-02 | P0 | app A 运行 | 输入 `bootloader`，随后运行 `fw_update.py ... info` | CLI 返回 `OK rebooting_to_bootloader`；INFO state=2、target=`0x43433152`。 |
| BL-N-03 | P0 | state=2，包 B 有效 | 执行 `fw_update.py ... flash --package B` | 显示 0–100%、`COMPLETE`；设备复位进入 B。 |
| BL-N-04 | P0 | B 已启动 | 发送 `help`、`version`、业务 `rx status` | B 的可见标识正确，全部 CLI 正常，RF 接收正常。 |
| BL-N-05 | P1 | B 已启动 | 正常重启 10 次，每次等待 RF 初始化完成 | 每次均进入 B；启动确认后未确认计数回到 0，不会误进更新模式。 |
| BL-N-06 | P1 | state=2 | 发送 B 后再次发送相同 B | 第二次升级同样成功；元数据版本、CRC 与 imageSize 一致。 |
| BL-N-07 | P1 | bootloader 已烧录、无元数据 | `HELLO` 后发送有效包 A | 初始 state=3；成功完成后 state 写为有效并启动 A。 |

## 5. 包头、镜像与容量校验

每个用例以有效 `.pkg` 为基线，只修改一个字段或字节。客户端本地预检会拒绝的
用例仍要通过“原始帧发送器”直接发 `BEGIN` 验证 bootloader 的防线。

| ID | 优先级 | 注入 | 预期结果 |
| --- | --- | --- | --- |
| BL-P-01 | P0 | 包头 magic 错误 | `BEGIN` 返回 ERROR；不擦 app、不改有效状态。 |
| BL-P-02 | P0 | formatVersion 或 headerSize 错误 | ERROR；不写 flash。 |
| BL-P-03 | P0 | header CRC32 错误 | ERROR；不写 flash。 |
| BL-P-04 | P0 | target 改为 TX 或任意非 `0x43433152` | ERROR；旧 RX app 仍可启动。 |
| BL-P-05 | P0 | load address 非 `0x8000` | ERROR。 |
| BL-P-06 | P0 | size=0、size>`0x17000`、size 非 4-byte 对齐 | ERROR。 |
| BL-P-07 | P0 | image 中翻转 1 bit，但保留旧 image CRC | DATA 可 ACK，END 必须 ERROR；下次重启 state=3。 |
| BL-P-08 | P1 | 正确 image CRC、错误包总长度（截断/追加） | Python parser 拒绝；裸协议不能将不完整 image 置为有效。 |
| BL-P-09 | P1 | 首 MSP 不在 `0x20000000–0x20004FFF` | END 可完成元数据写入前必须通过启动校验；下次必须进更新模式，不能跳转。 |
| BL-P-10 | P1 | ResetISR 非 Thumb、在 app slot 外 | 同 BL-P-09。 |
| BL-P-11 | P2 | imageSize=`0x17000` 的最大合法镜像 | 全部写入、CRC 正确、可启动；不得覆盖 `0x1F000` CCFG。 |
| BL-P-12 | P2 | 4 bytes 的最小合法镜像 | 包层可接受，但因向量表非法，重启必须保持更新模式。 |

## 6. UART 协议与传输容错

| ID | 优先级 | 注入/步骤 | 预期结果 |
| --- | --- | --- | --- |
| BL-U-01 | P0 | HELLO 正常，sequence 分别为 0、1、65535 后回绕 0 | INFO sequence 与请求一致，服务持续可用。 |
| BL-U-02 | P0 | DATA 中每 10 个分块故意篡改 CRC16 后重发正确分块 | 错帧被丢弃；正确帧获得 ACK；最终 CRC 成功。 |
| BL-U-03 | P0 | 构造非法 COBS、空帧、短帧、长度字段与实际不符 | 无异常/复位/越界写；随后 HELLO 仍有响应。 |
| BL-U-04 | P0 | DATA offset 小于当前 offset（完全重复） | ACK 返回当前已写 offset；flash 内容不变。 |
| BL-U-05 | P0 | DATA offset 大于当前 offset | NACK 返回期望 offset；不得跳过 flash 区间。 |
| BL-U-06 | P1 | DATA 长度 0、>128、非 4-byte 对齐、offset+length 超过 imageSize | NACK/ERROR；bytesWritten 不变。 |
| BL-U-07 | P1 | 未 BEGIN 直接 DATA、END；BEGIN 后直接 END；BEGIN 中途再次 BEGIN | 均返回 ERROR 或明确重置会话；不得将 app 置有效。 |
| BL-U-08 | P1 | 丢失 ACK：主机重发同一 DATA | bootloader 幂等 ACK，最终升级成功。 |
| BL-U-09 | P1 | 高速连续发送、随机插入噪声、帧长度超过接收缓存 | 无内存破坏；丢弃异常帧；后续正常 HELLO/BEGIN 可用。 |
| BL-U-10 | P2 | 主机拔插/关闭串口 1 min 后重连 | 当前实现无跨复位续传；同一上电会话可重新 BEGIN 并全量传输。 |

## 7. 断电、复位与 flash 故障注入

使用 relay 在以下点断电；每点至少执行 10 次，覆盖不同写偏移。每次恢复后先执行
`info`，再检查是否有任何旧/半写 app 被执行。

| ID | 优先级 | 断电点 | 恢复后的必须结果 |
| --- | --- | --- | --- |
| BL-R-01 | P0 | app CLI 已回复 `bootloader`、复位前 | 下次能进入更新模式或保守地进入 app；不得损坏 metadata。 |
| BL-R-02 | P0 | BEGIN 已验包、写 UPDATE_IN_PROGRESS metadata 前/后 | 不得擦 app 后保留 VALID；若状态不确定，必须进更新模式。 |
| BL-R-03 | P0 | app slot 首页擦除中 | state=3；不能启动 app；可重新完整升级。 |
| BL-R-04 | P0 | 任意 DATA FlashProgram 中（首块、中间、末块） | state=3；可重新 BEGIN；最终重刷成功。 |
| BL-R-05 | P0 | END CRC 计算中 | 不得误置 VALID；重启进更新模式。 |
| BL-R-06 | P0 | END 后写 VALID metadata 的主体或 record CRC 最后 4 bytes时 | journal 忽略不完整记录，选择上一条有效记录；不得启动半写 app。 |
| BL-R-07 | P1 | app 跳转前、VTOR 切换后 | 下次复位按有效 app 正常启动；无永久中断关闭状态。 |
| BL-R-08 | P1 | app 已启动但尚未 `bl_confirm_boot()` | 未确认计数保留增加；重复达到 3 次后进入更新模式。 |
| BL-R-09 | P1 | app 已确认后 | 下次正常启动；计数不会累积。 |

断电控制应记录“收到的最后一个协议响应/块 offset/继电器时间戳”。若硬件无法精确
卡在 flash API 内，应采用 loop、GPIO 测试点或 J-Link breakpoint 辅助，不应用
“仅断电一次”替代全覆盖。

## 8. 元数据 journal 与启动计数压力测试

`BootMetadata` 为 48 bytes，每页约可容纳 85 条记录。正常启动、确认、更新请求和
升级完成都会追加记录，必须验证满页轮换。

| ID | 优先级 | 步骤 | 预期结果 |
| --- | --- | --- | --- |
| BL-M-01 | P0 | 连续启动/确认至少 200 次 | sequence 严格递增；两页轮换后仍有最新有效记录。 |
| BL-M-02 | P0 | 在页满、擦除目标页、复制/写入新记录各阶段断电 | 至少保留一条可解析有效记录；不会误启动无效 app。 |
| BL-M-03 | P1 | 手工损坏最新 record CRC | 选择前一有效 record；行为安全且可升级。 |
| BL-M-04 | P1 | 手工损坏一整页 | 另一页有效记录仍可启动或进入更新；无死循环。 |
| BL-M-05 | P1 | 两页均无有效记录 | 默认 state=3，HELLO 可用，接受有效包。 |
| BL-M-06 | P1 | 三次未确认启动，第四次上电 | 不跳 app，INFO 可响应，state/计数符合门限策略。 |
| BL-M-07 | P1 | 同一 bootAttemptId 重复调用 `bl_confirm_boot()` | 只减一次，不发生 underflow。 |
| BL-M-08 | P2 | sequence 接近 `UINT32_MAX`（J-Link 注入） | 明确记录当前比较策略的行为；若无法保证回绕正确，列为设计限制。 |

## 9. App handoff 与 ABI 测试

| ID | 优先级 | 步骤 | 预期结果 |
| --- | --- | --- | --- |
| BL-A-01 | P0 | 有效 app 上电后读取 VTOR、MSP、PRIMASK | VTOR=`0x8000`；MSP 在 app RAM；PRIMASK 已恢复为可使能状态。 |
| BL-A-02 | P0 | app 连续运行 RF 接收、CLI、版本查询至少 30 min | 无 HardFault、串口与 RF 功能正常。 |
| BL-A-03 | P1 | app 未调用确认 API（测试镜像） | 3 次后只进更新模式。 |
| BL-A-04 | P1 | app 的 API magic/version 不匹配（测试镜像） | API wrapper 安全返回；不跳至任意地址。 |
| BL-A-05 | P1 | `bootloader` CLI 后验证最新 metadata state | state=2 必须持久化；若追加 metadata 失败，CLI 必须报告错误或设备必须进入安全更新模式。 |
| BL-A-06 | P2 | UART 仍有 app 残留输入时切换 bootloader | bootloader 帧解析不应让残留 app 文本影响升级；升级客户端应在进入后清空并重试 HELLO。 |

## 10. 自动化设计

### 10.1 主机端测试工具

在 `tools/tests/` 增加 pytest 或标准库测试：

- `test_fw_package.py`：对包头的每一个字段、长度、CRC、padding 做正反例；
- `test_fw_protocol.py`：COBS round-trip、CRC16、空 payload、最大 payload、畸形帧；
- `raw_update_client.py`：支持指定 sequence、offset、payload、CRC 错误、丢 ACK、
  发送速率、在第 N 块调用 relay；
- `metadata_decode.py`：从 J-Link dump 解码两个元数据页并输出 JSON/CSV。

主机单元测试应不依赖硬件；每次提交运行。硬件回归至少自动执行 BL-N-01 至
BL-N-04、BL-P-01 至 BL-P-07、BL-U-01 至 BL-U-08。

### 10.2 硬件测试编排

每个硬件用例采用以下固定结构：

1. J-Link 恢复已知 bootloader，随后用 `.pkg` 安装基线 app A；
2. 记录 `INFO`、版本、包 hash 和起始 metadata dump；
3. 执行单一注入动作；
4. 复位，保存 `INFO`、串口和 metadata dump；
5. 判定“是否错误启动 app”“是否仍可重刷”“新旧 app 哪一个启动”；
6. 用有效包 A 恢复，作为下一个用例基线。

J-Link 整片擦除会清除 metadata 页，因此不得将“raw app ELF 加载成功”当成升级
恢复成功。每次 J-Link 恢复后必须通过 UART `.pkg` 安装 app，创建合法 metadata。

## 11. 缺陷分级与交付物

| 等级 | 定义 |
| --- | --- |
| P0 | 可执行损坏/未认证状态的 app、无法恢复、flash 越界、元数据导致砖机、协议内存破坏。 |
| P1 | 正常升级或失败恢复不可靠、错误状态/版本、确认计数不正确、升级需人工非预期介入。 |
| P2 | 诊断、日志、性能、错误文本或已文档化的限制。 |

每次测试输出：测试版本、bootloader/app 包 SHA-256、DUT 编号、环境版本、用例 ID、
操作时间线、串口原始日志、INFO、metadata dump、结果、缺陷编号和恢复结果。发布
前应形成用例通过率表；P0/P1 未关闭不得发布。

## 12. 当前实现需重点验证的风险

以下不是预设结论，而是本实现中应优先执行的验证项：

1. `appendMetadataRecord()` 的返回值在请求更新、启动确认、BEGIN 和 END 路径多处
   被忽略。BL-A-05、BL-M-01/02 必须确认页满或 flash 写失败时不会静默丢失状态。
2. 协议错误帧当前被丢弃而不是回 ERROR，客户端依赖超时重传。BL-U-02/03/09 应确认
   这不会导致永久卡死或错误写入。
3. 当前 app 与 bootloader 共用 UART；切换前残留的二进制/文本可能污染下一端的输入。
   BL-A-06 应决定是否需要在切换后 flush UART 或在客户端进入模式时先发送 delimiter。
4. CRC32 仅验证完整性，不验证来源。该限制需在产品威胁模型和量产流程中明确接受，
   或在后续版本加入认证机制。
