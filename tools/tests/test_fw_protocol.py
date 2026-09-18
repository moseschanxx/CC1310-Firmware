"""Host-only tests for COBS framing and the UART protocol codec."""
import os
import struct
import sys
import unittest
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import fw_protocol


class FirmwareProtocolTests(unittest.TestCase):
    def test_cobs_round_trip_with_zeroes_and_long_runs(self):
        payload = b"\x00" + bytes(range(1, 255)) + b"\x00" + os.urandom(512)
        self.assertEqual(fw_protocol.cobs_decode(fw_protocol.cobs_encode(payload)), payload)

    def test_frame_round_trip_for_empty_and_max_data_payloads(self):
        for payload in (b"", bytes(range(128))):
            with self.subTest(length=len(payload)):
                wire = fw_protocol.encode(fw_protocol.DATA, 0xFFFF, payload)
                self.assertEqual(wire[-1], 0)
                self.assertEqual(fw_protocol.decode(wire),
                                 (fw_protocol.DATA, 0xFFFF, payload))

    def test_rejects_invalid_cobs_data(self):
        for encoded in (b"\x00", b"\x02", b"\x03\x01"):
            with self.subTest(encoded=encoded):
                with self.assertRaises(ValueError):
                    fw_protocol.cobs_decode(encoded)

    def test_rejects_corrupted_frame_crc(self):
        wire = fw_protocol.encode(fw_protocol.HELLO, 9)
        raw = bytearray(fw_protocol.cobs_decode(wire[:-1]))
        raw[-1] ^= 0x01
        with self.assertRaisesRegex(ValueError, "CRC16"):
            fw_protocol.decode(fw_protocol.cobs_encode(raw) + b"\0")

    def test_rejects_bad_version_and_payload_length(self):
        frame = bytearray(fw_protocol.encode(fw_protocol.HELLO, 3))
        raw = bytearray(fw_protocol.cobs_decode(frame[:-1]))
        raw[0] = (fw_protocol.PROTOCOL_VERSION + 1) & 0xFF
        raw[-2:] = struct.pack("<H", fw_protocol.crc16_ccitt(raw[:-2]))
        with self.assertRaisesRegex(ValueError, "frame header"):
            fw_protocol.decode(fw_protocol.cobs_encode(raw) + b"\0")

        raw = bytearray(fw_protocol.cobs_decode(frame[:-1]))
        raw[4:6] = struct.pack("<H", 1)
        raw[-2:] = struct.pack("<H", fw_protocol.crc16_ccitt(raw[:-2]))
        with self.assertRaisesRegex(ValueError, "frame header"):
            fw_protocol.decode(fw_protocol.cobs_encode(raw) + b"\0")

    def test_crc16_reference_vector(self):
        self.assertEqual(fw_protocol.crc16_ccitt(b"123456789"), 0x29B1)


if __name__ == "__main__":
    unittest.main()
