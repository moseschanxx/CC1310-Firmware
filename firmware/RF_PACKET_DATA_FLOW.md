# Host 到 CC1310 TX/RX 数据流与 GPIO 时序

## 范围与前提

本文描述当前同步帧链路：Host 经 UART 控制一块 CC1310 TX，TX 通过 433 MHz
Proprietary PHY 广播，RX1 和 RX2 分别接收并输出测量 GPIO。TX/RX UART0 均为
115200 baud、8N1、无硬件流控；CC1310 LaunchPad 的 UART0 引脚为 DIO2（RXD）和
DIO3（TXD）。

```text
Host test tool / terminal
        | UART CLI: sync_time / sync_frame
        v
CC1310 TX: parse -> build 30-byte app frame -> RF_runCmd
        | 433 MHz, 50 kBaud, -10 dBm
        +---------------------------> CC1310 RX1: RF queue -> GPIO -> optional UART dump
        |
        +---------------------------> CC1310 RX2: RF queue -> GPIO -> optional UART dump
```

## Host 与 TX

Host 发送一行 ASCII CLI 命令，以 CR、LF 或 CRLF 结束：

```text
sync_time 0123456789ABCDEF    # uint64 UTC，十六进制
sync_frame 000186A0           # uint32 曝光时间，十六进制
```

`tools/tx_sync_stress.py` 默认持续发送 `sync_frame`；`--command sync_time` 仅发时间包，
`--command both` 交替发送。脚本会等待 TX 的 `OK <command> seq=<n>`，因此指定的
`--interval-ms` 是目标最小起始间隔，实际周期还受 UART 往返和同步 RF 发送时间限制。

TX 将命令编码为固定 30-byte 应用帧：

| 偏移 | 字段 | 字节数 | 编码 |
| --- | --- | --- | --- |
| 0–1 | magic | 2 | `0x5359`，big-endian |
| 2 | type | 1 | 时间 `0x01`；帧 `0x02` |
| 3 | payload length | 1 | 时间 8；帧 4 |
| 4–5 | site ID | 2 | 当前 `0x0000`，big-endian |
| 6 | sequence | 1 | 每次发送递增、自然回绕 |
| 7 | session ID | 1 | 当前 `0x00` |
| 8–27 | payload + reserved | 20 | payload 后以伪随机字节填满 |
| 28–29 | CRC16 | 2 | CRC-16/CCITT-FALSE，big-endian |

RF 使用 variable-length PHY；`CMD_PROP_TX.pktLen=30` 向 RF Core 提供长度，应用数组
中**没有**额外的长度首字节。PHY 配置为 50 kBaud、30-byte 最大包、−10 dBm。

## RX 数据处理

RX 持续运行 `CMD_PROP_RX`，最多接受 30-byte payload。RF Core 对 CRC 正确的 entry
触发回调；固件记录 RSSI、取得 SYS/BIOS tick 时间戳、将 payload 拷入深度为 32 的
软件 mailbox。`rx dump on` 时，独立打印任务输出：

```text
RX seq=<local queue sequence> tick=<local tick> len=<payload length> data=<hex>
```

`rx status` 中的 `rx`、`enq`、`drop`、`full`、`crc`、`coll` 可用于区分 RF 接收、
应用队列、RX data-entry buffer、CRC 和 collision 事件。高频测试应关闭 dump，避免 UART
打印本身影响队列消费。

## GPIO 测量点

所有测量 GPIO 均为 **DIO1 / IOID_1**，推挽输出、最大驱动强度、空闲低电平。每块板的
DIO1 都是独立输出；仅接入逻辑分析仪对应通道并共地，**不要将 TX 与 RX 的 DIO1 直接互连**。

| 设备 | 上升沿 | 下降沿 | 含义 |
| --- | --- | --- | --- |
| TX | 调用 `RF_runCmd(CMD_PROP_TX)` 前 | 该调用返回后 | 覆盖软件提交到 RF Core、等待与整包发送；不是首个空口 bit 的精确时刻。 |
| RX1/RX2 | 收到 CRC 正确 entry 后、拷入软件队列前 | 软件队列投递后 | 表示 RX 回调处理点；不是 sync word 检测或首 bit 的精确时刻。 |

因此 TX→RX GPIO 延迟包含 TX RF 命令启动、空口发送、接收完成、RF 回调调度和少量队列
投递时间；不包含 Host→UART 的时间，也不应解释为纯空口传播延迟。RX1/RX2 边沿差则反映
两条接收/回调路径的相对时差。

## 1 小时 周期测试结果

`Session 3.sal` 以 100 MS/s 采集。分析前对导出的数字边沿应用了 20 ns 毛刺滤波：
在 `617.880564480–617.880564490 s`，Tx 出现一个 10 ns 低脉冲，同时 Rx1 出现一个
10 ns 高脉冲。该宽度恰为一个采样周期，且两路反相同步，不符合固件 GPIO 操作能力，
判定为采集/信号完整性毛刺；该伪边沿不计为 TX 或 RX 事件。

分析规则如下：TX 目标周期 20 ms；TX→RX 边沿配对最大延迟 15 ms；RX 相邻接收间隔超过
25 ms 时标记为长间隔。配对窗口与长间隔阈值分离，避免在 TX 周期异常时把后续包的 RX
边沿配给前一 TX。

| 项目 | 结果 |
| --- | --- |
| 有效 TX 数 / 采集时长 | 180,000 包 / 3,600.065 s（约 1 小时） |
| 实际平均发送率 | 49.999 Hz |
| TX 间隔 | 平均 20.000 ms；P50/P95/P99 为 19.983/20.883/21.184 ms；范围 14.637–32.098 ms |
| TX 节拍异常 | 小于 15 ms 共 10 次；大于 25 ms 共 16 次 |
| RX1 接收 | 179,231/180,000 收到；丢失 769 包，丢包率 **0.427%** |
| RX1 连续丢包 | 743 段：单包 726 段、2 包 12 段、3 包 3 段、4 包 1 段、6 包 1 段（最长） |
| RX2 接收 | 180,000/180,000 收到；未检测到丢包 |
| TX→RX1 GPIO 延迟 | 平均 7,289.063 µs；P50/P95/P99 为 7,287.910/7,290.970/7,354.390 µs；范围 7,230.880–7,364.270 µs |
| TX→RX2 GPIO 延迟 | 平均 7,289.226 µs；P50/P95/P99 为 7,288.070/7,291.120/7,354.620 µs；范围 7,231.230–7,364.160 µs |
| RX1/RX2 相对时间差 | 平均 5.544 µs；P50/P95/P99 为 1.870/38.890/68.390 µs；最大 123.180 µs |

RX1 有 759 个超过 25 ms 的接收间隔（平均 40.395 ms、最大 139.324 ms），与其丢包统计
相符。RX2 虽有 16 个超过 25 ms 的间隔，但逐 TX 配对全部成功；这些间隔与 TX 自身的节拍
波动一致，不能认定为 RX2 丢包。

结论：毛刺过滤后，两路正常接收的 GPIO 延迟保持在约 7.29 ms，RX2 在完整 1 小时测试中
无丢包；丢包集中在 RX1。
RX1 丢包已确认与供电有关：RX1 未接电池，供电稳定性较差；RX2 接有电池，供电更稳定且本轮未丢包。
采集时启用至少 20 ns 的数字 glitch filter，或将 Tx/Rx1 改接到非相邻采集通道以验证串扰。
