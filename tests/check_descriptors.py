#!/usr/bin/env python3
"""Validate the actual linked UAC1 configuration bytes, not a copied fixture."""
import re
import subprocess
import sys

elf = sys.argv[1]
symbols = subprocess.check_output(["arm-none-eabi-nm", "-S", elf], text=True)
match = re.search(r"^([0-9a-f]+)\s+([0-9a-f]+)\s+[dD]\s+config_descriptor$", symbols, re.M)
assert match, "configuration descriptor missing from ELF"
address, size = (int(x, 16) for x in match.groups())
sections = subprocess.check_output(["arm-none-eabi-objdump", "-h", elf], text=True)
section = re.search(r"^\s*\d+\s+\.data\s+([0-9a-f]+)\s+([0-9a-f]+)", sections, re.M)
assert section, ".data section missing"
data_address = int(section.group(2), 16)
raw = subprocess.check_output(["arm-none-eabi-objcopy", "-O", "binary", "-j", ".data", elf, "/dev/stdout"])
descriptor = raw[address - data_address:address - data_address + size]
assert len(descriptor) == 142
assert descriptor[:4] == bytes([9, 2, 142, 0])
items = []
offset = 0
while offset < len(descriptor):
    length = descriptor[offset]
    assert length >= 2 and offset + length <= len(descriptor)
    items.append(descriptor[offset:offset + length])
    offset += length
assert offset == len(descriptor)
assert [(x[2], x[3]) for x in items if x[1] == 4] == [(0, 0), (1, 0), (1, 1), (2, 0)]
formats = [x for x in items if x[:3] == bytes([17, 0x24, 2])]
assert len(formats) == 1 and formats[0][5:8] == bytes([3, 24, 3])
assert formats[0][8:17] == bytes([0x44, 0xac, 0, 0x80, 0xbb, 0, 0, 0x77, 1])
endpoints = [x for x in items if x[1] == 5]
assert len(endpoints) == 2
assert endpoints[0][2:7] == bytes([1, 5, 582 & 255, 582 >> 8, 1])
assert endpoints[0][8] == 0x81 and endpoints[1][2:6] == bytes([0x81, 0x11, 3, 0])
assert any(x[:3] == bytes([9, 0x21, 0x0b]) for x in items)
print("Linked UAC1 descriptor: 44.1/48/96 kHz, 24-bit stereo, async feedback, DFU OK")
