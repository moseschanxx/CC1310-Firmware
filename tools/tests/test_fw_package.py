"""Host-only tests for the CC1310 firmware package format."""
import struct
import sys
import unittest
import zlib
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import fw_package


class FirmwarePackageTests(unittest.TestCase):
    TARGET_ID = 0x43433152

    def build_package(self, image=b"\x00\x01\x02\x03", version=7):
        return fw_package.build(image, self.TARGET_ID, version)

    def replace_header(self, package, **changes):
        fields = list(fw_package.HEADER.unpack_from(package))
        names = ("magic", "format_version", "header_size", "target", "address",
                 "size", "version", "image_crc", "header_crc")
        for name, value in changes.items():
            fields[names.index(name)] = value
        prefix = fw_package.HEADER.pack(*fields[:-1], 0)
        fields[-1] = zlib.crc32(prefix[:-4]) & 0xFFFFFFFF
        return fw_package.HEADER.pack(*fields) + package[fw_package.HEADER.size:]

    def test_round_trip_preserves_metadata_and_pads_image(self):
        package = self.build_package(b"abcde", version=109)
        parsed = fw_package.parse(package)

        self.assertEqual(parsed["target"], self.TARGET_ID)
        self.assertEqual(parsed["address"], fw_package.APP_ADDRESS)
        self.assertEqual(parsed["version"], 109)
        self.assertEqual(parsed["image"], b"abcde\xff\xff\xff")
        self.assertEqual(parsed["size"], 8)

    def test_formats_packed_semantic_version(self):
        version = (2 << 16) | (17 << 8) | 255
        self.assertEqual(fw_package.format_version(version), "2.17.255")

    def test_rejects_non_reserved_most_significant_version_byte(self):
        with self.assertRaisesRegex(ValueError, "most-significant byte"):
            fw_package.build(b"test", self.TARGET_ID, 0x01000000)

    def test_rejects_corrupted_header_crc(self):
        package = bytearray(self.build_package())
        package[fw_package.HEADER.size - 1] ^= 0x01
        with self.assertRaisesRegex(ValueError, "header CRC32"):
            fw_package.parse(package)

    def test_rejects_corrupted_image_crc(self):
        package = bytearray(self.build_package())
        package[-1] ^= 0x01
        with self.assertRaisesRegex(ValueError, "image CRC32"):
            fw_package.parse(package)

    def test_rejects_invalid_header_fields_even_with_valid_header_crc(self):
        cases = (
            ({"magic": 0}, "unsupported package header"),
            ({"format_version": 2}, "unsupported package header"),
            ({"header_size": 0}, "unsupported package header"),
            ({"address": 0}, "invalid application address"),
            ({"size": 0}, "invalid application address"),
            ({"size": fw_package.MAX_IMAGE_SIZE + 1}, "invalid application address"),
        )
        for changes, error in cases:
            with self.subTest(changes=changes):
                with self.assertRaisesRegex(ValueError, error):
                    fw_package.parse(self.replace_header(self.build_package(), **changes))

    def test_rejects_package_length_mismatch(self):
        package = self.build_package()
        with self.assertRaisesRegex(ValueError, "package length"):
            fw_package.parse(package[:-1])

    def test_build_rejects_empty_and_oversized_images(self):
        with self.assertRaisesRegex(ValueError, "outside the App slot"):
            fw_package.build(b"", self.TARGET_ID, 1)
        with self.assertRaisesRegex(ValueError, "outside the App slot"):
            fw_package.build(b"x" * (fw_package.MAX_IMAGE_SIZE + 1), self.TARGET_ID, 1)

    def test_accepts_minimum_and_maximum_aligned_images(self):
        minimum = fw_package.build(b"\xff\xff\xff\xff", self.TARGET_ID, 7)
        self.assertEqual(fw_package.parse(minimum)["size"], 4)
        maximum = fw_package.build(b"\xff" * fw_package.MAX_IMAGE_SIZE, self.TARGET_ID, 8)
        self.assertEqual(fw_package.parse(maximum)["size"], fw_package.MAX_IMAGE_SIZE)

    def test_rejects_appended_package_bytes(self):
        with self.assertRaisesRegex(ValueError, "package length"):
            fw_package.parse(self.build_package() + b"\xff")


if __name__ == "__main__":
    unittest.main()
