"""COBS framed UART protocol shared by the firmware updater and tests."""
import struct

PROTOCOL_VERSION = 2
HELLO, INFO, BEGIN, READY, DATA, ACK, NACK, END, COMPLETE, ERROR, SET_ROLE = range(1, 12)
FRAME_HEAD = struct.Struct("<BBHH")


def crc16_ccitt(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def cobs_encode(data):
    output = bytearray([0]); code_index = 0; code = 1
    for byte in data:
        if byte == 0:
            output[code_index] = code; code_index = len(output); output.append(0); code = 1
        else:
            output.append(byte); code += 1
            if code == 0xFF:
                output[code_index] = code; code_index = len(output); output.append(0); code = 1
    output[code_index] = code
    return bytes(output)


def cobs_decode(data):
    output = bytearray(); index = 0
    while index < len(data):
        code = data[index]
        # A code byte describes itself plus code - 1 following data bytes.
        # Reject truncated blocks instead of silently slicing fewer bytes.
        if code == 0 or index + code > len(data):
            raise ValueError("invalid COBS frame")
        index += 1; output.extend(data[index:index + code - 1]); index += code - 1
        if code != 0xFF and index < len(data): output.append(0)
    return bytes(output)


def encode(message_type, sequence, payload=b""):
    head = FRAME_HEAD.pack(PROTOCOL_VERSION, message_type, sequence, len(payload)) + payload
    return cobs_encode(head + struct.pack("<H", crc16_ccitt(head))) + b"\0"


def decode(wire):
    raw = cobs_decode(wire.rstrip(b"\0"))
    if len(raw) < FRAME_HEAD.size + 2: raise ValueError("short frame")
    version, kind, sequence, length = FRAME_HEAD.unpack_from(raw)
    if version != PROTOCOL_VERSION or len(raw) != FRAME_HEAD.size + length + 2:
        raise ValueError("invalid frame header")
    if crc16_ccitt(raw[:-2]) != struct.unpack_from("<H", raw, len(raw) - 2)[0]:
        raise ValueError("frame CRC16 mismatch")
    return kind, sequence, raw[FRAME_HEAD.size:-2]
