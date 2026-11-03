"""Make (and check) Tsum Tsum mascot figure tag data, the way tmac.dll validates it.

usage: o26figure.py <tmac.dll> list
       o26figure.py <tmac.dll> make <chara> <type> <variant> [uid hex]

A figure is an ISO15693 tag (UID E0.., IC type 38). The game reads 8 blocks; the first 16 data bytes matter
(tmac sub_1801818C0):
  data[8..15]  = MD5(key64 @0x18024E6E0 || uid || data[0..7])[:8]
  data[1..7]  ^= LFSR keystream seeded from uid[6..7] (each byte ^ 0x57)
  CRC-8 (table @0x1801E0870) over the unscrambled data[0..7] must be 0
The unscrambled data[3], data[4], data[5] >> 4 are the key into the figure table (463 x 144 bytes @0x1802B8F40):
character, type (1 normal, 2 pink, 101 ...), variant.
"""
import hashlib
import struct
import sys

import pefile

KEY, CRC, TABLE, COUNT = 0x18024E6E0, 0x1801E0870, 0x1802B8F40, 463


class Tmac:
    def __init__(self, path):
        pe = pefile.PE(path, fast_load=True)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.img = pe.get_memory_mapped_image()

    def read(self, va, n):
        return self.img[va - self.base:va - self.base + n]

    def cstr(self, va):
        o = va - self.base
        return self.img[o:self.img.find(b"\0", o)].decode("cp932")

    def figures(self):
        for i in range(COUNT):
            e = self.read(TABLE + i * 144, 144)
            q, d = struct.unpack("<18Q", e), struct.unpack("<36I", e)
            name = self.cstr(q[1]) if q[1] else ""
            yield d[6], d[7], d[8], d[9], name


def keystream(uid):
    """The 7 bytes XORed into data[1..7] (16-bit LFSR from tmac, seeded with uid[6] | 0x80, uid[7])."""
    v = uid[7] | ((uid[6] | 0x80) << 8)
    out = []
    for _ in range(7):
        a = (v ^ (v >> 2)) & 0xFFFF
        b = (v ^ (a >> 1)) & 0xFFFF
        c = (v ^ (b >> 2)) & 0xFFFF
        v = ((v << 1) | ((c >> 10) & 1)) & 0xFFFF
        out.append((v & 0xFF) ^ 0x57)
    return out


def crc8(t, data):
    c = 0
    for b in data:
        c = t[c ^ b]
    return c


def make(tm, uid, chara, typ, variant):
    t = tm.read(CRC, 256)
    p = bytearray(8)
    p[3], p[4], p[5] = chara, typ, variant << 4
    p[7] = crc8(t, p[:7])
    enc = bytearray(p[:1]) + bytes(b ^ k for b, k in zip(p[1:], keystream(uid)))
    mac = hashlib.md5(tm.read(KEY, 64) + bytes(uid) + bytes(enc)).digest()[:8]
    return bytes(enc) + mac


def check(tm, uid, data):
    """Python copy of sub_1801818C0: returns (status, unscrambled data[0..7]); 0 = accepted."""
    if hashlib.md5(tm.read(KEY, 64) + bytes(uid) + data[:8]).digest()[:8] != data[8:16]:
        return 3, None
    p = bytes(data[:1]) + bytes(b ^ k for b, k in zip(data[1:8], keystream(uid)))
    return (4 if crc8(tm.read(CRC, 256), p) else 0), p


def main():
    tm = Tmac(sys.argv[1])
    if sys.argv[2] == "list":
        for c, ty, v, fid, name in tm.figures():
            print(f"{c:3} {ty:3} {v:2}  id {fid:#010x}  {name}")
        return
    chara, typ, variant = map(int, sys.argv[3:6])
    uid = bytes.fromhex(sys.argv[6]) if len(sys.argv) > 6 else bytes([0xE0, 0x02, 0x26, 0, 0, 0, chara, typ])
    data = make(tm, uid, chara, typ, variant)
    status, p = check(tm, uid, data)
    print("uid ", uid.hex().upper())
    print("data", data.hex().upper(), "check", status, "plain", p.hex().upper())


if __name__ == "__main__":
    main()
