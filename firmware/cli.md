# 串口 CLI

固件通过 `Board_UART0` 提供 ASCII 串口 CLI，参数为 **115200 8N1**。每行一个命令，接受 `CR`、`LF` 或 `CRLF` 结尾；每个响应以 `OK` 或 `ERR <code>` 开头。输入 `help` 可查看目标当前角色实际支持的命令。

启动时会输出固件版本和角色，例如：

```text
OK version=0.2.1 role=rx
OK cli=ready count=0 commands=help,rx,bootloader,version,ipc,stack
```

角色由 bootloader 写入的启动 metadata 决定；只有明确写入 `tx` 才运行 TX，其余值均安全地按 RX 运行。烧录时用 `flash_all_jlink.sh -r rx` 或 `-r tx` 选择角色，运行中的 CLI 不提供切换角色的命令。

## 通用命令

| 命令 | 说明 |
| --- | --- |
| `help [command]` | 列出所有命令，或显示某一命令的用法。 |
| `version` | 输出编译时的固件版本。 |
| `bootloader` | metadata 成功持久化后输出 `OK rebooting_to_bootloader`，复位并进入 UART OTA 更新器；写入失败时输出 `ERR BOOT metadata_write_failed` 并保持当前应用运行。 |
| `ipc dump on\|off` | 控制 I2C slave 事务的串口打印；默认 `off`。 |
| `stack` | 列出 SYS/BIOS Task 的栈高水位。 |

`ipc dump on` 后，每次 I2C master 写入或读取完成并发送 STOP 时输出事务方向、寄存器指针和数据，例如：

```text
OK ipc write reg=0x10 len=3 data=010203
OK ipc read reg=0x00 len=4 data=49324353
```

`write` 是 slave 从 master 接收的 payload；`read` 是 slave 实际通过 `I2CSlaveDataPut()` 发给 master 的字节。读取数据在 I2C ISR 内按发送顺序捕获，STOP 后再交给 I2C worker task 格式化并输出，因此 UART 输出不会在 ISR 中执行；read dump 不会递增 I2C 错误计数。`ipc dump off` 不影响 I2C 通信或统计。

单笔 write payload 和可记录的 read 数据均最多为 64 字节。dump 的 worker 队列深度为 1；连续事务快于 worker/UART 消费速度时，后续待处理记录会丢弃并计入 I2C 错误计数。因此用于逐笔核对的主机测试应串行执行，等待对应的 `OK ipc ...` 输出后再发下一笔。

`stack` 的输出格式如下：

```text
OK stack: name priority used/size free mode
OK stack: i2c_slave 1 224/1024 800 blocked
OK stack: radio_rx 2 736/1024 288 blocked
OK stack: cli 1 1184/2048 864 running
```

字段依次为任务名、优先级、历史最大已用栈/总栈、未触及栈空间及状态。任务名可为 `radio_rx`、`radio_tx`、`cli`、`i2c_slave`、`packet_print`、`idle` 或 `unnamed`；状态为 `running`、`ready`、`blocked`、`terminated`、`inactive` 或 `unknown`。`used` 是 SYS/BIOS 填充栈扫描得到的高水位，应在覆盖最坏业务路径后再据此调整栈大小。

## RX 命令

只有 metadata 角色为 `rx` 时提供：

| 命令 | 说明 |
| --- | --- |
| `rx status` | 输出接收、入队、丢包、RF 缓冲满、CRC 错误、碰撞和 RSSI 统计。 |
| `rx dump on` | 打开逐包输出。 |
| `rx dump off` | 关闭逐包输出；接收和统计仍继续。 |

打开 dump 后，每个入队包会输出：

```text
RX seq=42 tick=123456 len=30 data=...
```

`rx status` 中 `rx` 为无线侧收到的包数，`enq` 为成功放入应用 mailbox 的数量，`drop` 为应用 mailbox 满造成的丢弃，`full` 为 RF 接收缓冲满，`crc` 和 `coll` 分别为 CRC 错误与碰撞计数；`rssi=last[min,max]` 以 dBm 表示，`samples` 为有效 RSSI 样本数。

## TX 命令

只有 metadata 角色为 `tx` 时提供：

| 命令 | 说明 |
| --- | --- |
| `tx sync_time <utc_hex>` | 发送同步时间包；参数是至多 16 个十六进制字符的 `uint64`。 |
| `tx sync_frame <exposure_hex>` | 发送帧同步包；参数是至多 8 个十六进制字符的 `uint32`。 |

参数可带 `0x` 前缀。例如：

```text
tx sync_time 0x0000000067D2A100
tx sync_frame 0x00001234
```

成功响应包含递增序号，例如 `OK sync_time seq=7`；无线侧命令失败时返回 `ERR RF`。

## 移植说明

`cli_core.c`、`cli_core.h` 和 `cli_port.h` 不依赖 TI SDK。新目标只需实现 `cli_port_write()`，并将完整输入行交给 `cli_process_line()`。本项目的 `cli_task_cc1310.c` 是 CC1310 UART0 与 TI-RTOS pthread 适配层。
