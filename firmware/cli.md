# Serial CLI

The firmware provides an ASCII serial CLI on `Board_UART0` at **115200 8N1**. One command per line, terminated with `CR`, `LF` or `CRLF`; every response starts with `OK` or `ERR <code>`. Type `help` to see the commands actually supported by the target's current role.

At boot it prints the firmware version and role, for example:

```text
OK version=0.2.1 role=rx
OK cli=ready count=0 commands=help,rx,bootloader,version,ipc,stack
```

The role is decided by the boot metadata written by the bootloader; only an explicit `tx` runs TX, and any other value safely runs as RX. Choose the role when flashing with `flash_all_jlink.sh -r rx` or `-r tx`; the running CLI has no command to switch roles.

## Common commands

| Command | Description |
| --- | --- |
| `help [command]` | List all commands, or show the usage of one command. |
| `version` | Print the firmware version compiled in. |
| `bootloader` | After the metadata is persisted successfully, prints `OK rebooting_to_bootloader`, resets and enters the UART OTA updater; if the write fails, prints `ERR BOOT metadata_write_failed` and keeps the current application running. |
| `ipc dump on\|off` | Controls serial printing of I2C slave transactions; default `off`. |
| `stack` | Lists the stack high-water marks of the SYS/BIOS Tasks. |

After `ipc dump on`, each time an I2C master write or read completes and sends STOP, the transaction direction, register pointer and data are printed, for example:

```text
OK ipc write reg=0x10 len=3 data=010203
OK ipc read reg=0x00 len=4 data=49324353
```

`write` is the payload the slave received from the master; `read` is the bytes the slave actually sent to the master through `I2CSlaveDataPut()`. Read data is captured in transmit order inside the I2C ISR and, after STOP, handed to the I2C worker task for formatting and output, so UART output never runs in the ISR; a read dump does not increment the I2C error counter. `ipc dump off` does not affect I2C communication or statistics.

A single write payload and the recordable read data are each limited to 64 bytes. The dump worker queue depth is 1; when consecutive transactions arrive faster than the worker/UART can consume them, subsequent pending records are dropped and counted in the I2C error counter. Host tests that check transactions one by one should therefore run serially, waiting for the corresponding `OK ipc ...` output before sending the next one.

The output format of `stack` is:

```text
OK stack: name priority used/size free mode
OK stack: i2c_slave 1 224/1024 800 blocked
OK stack: radio_rx 2 736/1024 288 blocked
OK stack: cli 1 1184/2048 864 running
```

The fields are, in order, task name, priority, historical maximum used stack/total stack, untouched stack space and state. The task name can be `radio_rx`, `radio_tx`, `cli`, `i2c_slave`, `packet_print`, `idle` or `unnamed`; the state is `running`, `ready`, `blocked`, `terminated`, `inactive` or `unknown`. `used` is the high-water mark from the SYS/BIOS stack-fill scan; only resize stacks based on it after exercising the worst-case functional paths.

## RX commands

Available only when the metadata role is `rx`:

| Command | Description |
| --- | --- |
| `rx status` | Prints receive, enqueue, drop, RF buffer full, CRC error, collision and RSSI statistics. |
| `rx dump on` | Enables per-packet output. |
| `rx dump off` | Disables per-packet output; reception and statistics continue. |

With dump on, every enqueued packet prints:

```text
RX seq=42 tick=123456 len=30 data=...
```

In `rx status`, `rx` is the number of packets received on the radio side, `enq` the number successfully placed into the application mailbox, `drop` the drops caused by a full application mailbox, `full` the RF receive buffer full count, and `crc` and `coll` the CRC error and collision counts respectively; `rssi=last[min,max]` is in dBm and `samples` is the number of valid RSSI samples.

## TX commands

Available only when the metadata role is `tx`:

| Command | Description |
| --- | --- |
| `tx sync_time <utc_hex>` | Sends a sync-time packet; the argument is a `uint64` of up to 16 hexadecimal characters. |
| `tx sync_frame <exposure_hex>` | Sends a frame-sync packet; the argument is a `uint32` of up to 8 hexadecimal characters. |

Arguments may carry a `0x` prefix. For example:

```text
tx sync_time 0x0000000067D2A100
tx sync_frame 0x00001234
```

A successful response includes an incrementing sequence number, e.g. `OK sync_time seq=7`; if the radio command fails, `ERR RF` is returned.

## Porting notes

`cli_core.c`, `cli_core.h` and `cli_port.h` do not depend on the TI SDK. A new target only needs to implement `cli_port_write()` and pass complete input lines to `cli_process_line()`. In this project, `cli_task_cc1310.c` is the adaptation layer for the CC1310 UART0 and TI-RTOS pthread.
