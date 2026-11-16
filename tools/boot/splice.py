#!/usr/bin/env python3
"""Splice the ci16 boot header onto a freshly built boot.img."""
import sys

head_len = 0x16C000
new_off = 0x168000

with open(sys.argv[1], "rb") as f:
    head = f.read(head_len)
with open(sys.argv[2], "rb") as f:
    body = f.read()[new_off:]
with open(sys.argv[3], "wb") as f:
    f.write(head + body)
print(len(head + body))