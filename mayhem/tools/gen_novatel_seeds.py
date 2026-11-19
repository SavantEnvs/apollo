#!/usr/bin/env python3
"""Generate the NovAtel OEM4/OEM6 binary-log seeds for the rtklib_oem4 and novatel_parser targets.

One CRC'd log per message type each decoder handles, with a realistic GPS week (2300), plus one
stream of all of them back to back. Run from the repo root: python3 mayhem/tools/gen_novatel_seeds.py
Not part of the build; the seeds it writes are committed.
"""
import os
import struct

WEEK = 2300
MS = 345600000  # Wednesday 00:00 GPS time


def crc32(buf):  # the CRC from the NovAtel manual (novatel_parser.cc crc32_block)
    crc = 0
    for b in buf:
        t = (crc ^ b) & 0xFF
        for _ in range(8):
            t = (t >> 1) ^ 0xEDB88320 if t & 1 else t >> 1
        crc = ((crc >> 8) & 0xFFFFFF) ^ t
    return crc


def long_log(msg_id, body):
    hdr = struct.pack('<3BBHBBHHBBHIIHH', 0xAA, 0x44, 0x12, 28, msg_id, 0, 0x20, len(body), 0, 0,
                      180, WEEK, MS, 0, 0, 0x3f8c)
    frame = hdr + body
    return frame + struct.pack('<I', crc32(frame))


def short_log(msg_id, body):
    hdr = struct.pack('<3BBHHI', 0xAA, 0x44, 0x13, len(body), msg_id, WEEK, MS)
    frame = hdr + body
    return frame + struct.pack('<I', crc32(frame))


def pad(b, n):
    return (b + bytes(n))[:n]


def gps_subframes(prn):
    out = b''
    for sf in (1, 2, 3):
        s = bytearray(30)
        s[0] = 0x8B  # TLM preamble
        s[5] = sf << 2  # HOW subframe id, bits 43-45
        out += bytes(s)
    return struct.pack('<IIi', prn, WEEK, MS // 1000) + out


def rangeb_obs(prn):
    # prn, glofreq, psr, psr std, adr, adr std, dopp, C/No, locktime, ch-tr-status (GPS L1 C/A, locked)
    return struct.pack('<HHdfdfffff', prn, 0, 21000000.123, 0.05, -110000000.5, 0.01, -1200.5,
                       45.0, 1500.0, 0) + struct.pack('<I', 0x18109C04)


def rangecmp_obs(prn):
    rec = bytearray(24)
    struct.pack_into('<I', rec, 0, 0x18109C04)
    rec[17] = prn
    rec[20] = 0x40  # C/No bits
    return bytes(rec)


rtklib = {
    'rangecmpb': long_log(140, struct.pack('<I', 2) + rangecmp_obs(5) + rangecmp_obs(12)),
    'rangeb': long_log(43, struct.pack('<I', 2) + rangeb_obs(5) + rangeb_obs(12)),
    'rawephemb': long_log(41, gps_subframes(5)),
    'ionutcb': long_log(8, pad(struct.pack('<8d', 1.1e-8, 1.5e-8, -6e-8, -1.2e-7, 9e4, 1.6e5, -6.5e4,
                                           -3.9e5) + struct.pack('<IIddIIIII', WEEK, 319488, 0.0, 0.0,
                                                                 WEEK, 7, 18, 18, 0), 108)),
    'gloephemerisb': long_log(723, pad(struct.pack('<HHHHIIIIIdd', 38, 1, 0, 0, WEEK, MS, 0, 0, 0,
                                                   0.0, 0.0), 144)),
    'bdsephemerisb': long_log(1696, pad(struct.pack('<IIdIIIId', 6, WEEK - 1356, 2.0, 0, 1, 0,
                                                    MS // 1000, 0.0), 196)),
    'galephemerisb': long_log(1122, pad(struct.pack('<IIIIII', 11, 1, 1, 0, 0, 0), 220)),
    'galclockb': long_log(1121, pad(b'', 64)),
    'galionob': long_log(1127, pad(b'', 29)),
    'qzssionutcb': long_log(1347, pad(b'', 108)),
    'qzssrawephemb': long_log(1330, pad(struct.pack('<II', 193, 1), 44 + 90)),
    'rawsbasframeb': long_log(973, pad(struct.pack('<II', 120, 0), 48)),
    'rawwaasframeb': long_log(287, pad(struct.pack('<II', 0, 122), 48)),
}

apollo = {
    'bestpos': long_log(42, pad(struct.pack('<IIddd f I fff 4s ff BBBBBBBB', 0, 50, 37.4219, -122.084,
                                            12.5, -32.0, 61, 0.02, 0.02, 0.04, b'0\x00\x00\x00',
                                            1.0, 30.0, 18, 16, 16, 16, 0, 0, 0x37, 0x31), 72)),
    'bestgnsspos': long_log(1429, pad(struct.pack('<IIddd f I fff 4s ff BBBBBBBB', 0, 16, 37.4219,
                                                -122.084, 12.5, -32.0, 61, 1.2, 1.1, 2.5, b'\x00' * 4,
                                                0.0, 0.0, 14, 12, 12, 12, 0, 0, 0x37, 0x31), 72)),
    'bestvel': long_log(99, pad(struct.pack('<IIffddd', 0, 50, 0.15, 0.0, 12.3, 45.0, 0.01), 44)),
    'inspva': long_log(507, pad(struct.pack('<Idddddddddd', WEEK, 345600.0, 37.4219, -122.084, 12.5,
                                            3.0, 4.0, 0.0, 0.5, -0.3, 90.0) + struct.pack('<I', 3), 88)),
    'inspvas': short_log(508, pad(struct.pack('<Idddddddddd', WEEK, 345600.0, 37.4219, -122.084, 12.5,
                                              3.0, 4.0, 0.0, 0.5, -0.3, 90.0) + struct.pack('<I', 3), 88)),
    'inspvax': long_log(1465, pad(struct.pack('<II', 3, 56), 126)),
    'corrimudatas': short_log(813, pad(struct.pack('<Id6d', WEEK, 345600.0, 1e-4, 2e-4, 3e-4, 1e-3,
                                                   2e-3, 9.8e-2), 60)),
    'inscovs': short_log(320, pad(struct.pack('<Id', WEEK, 345600.0) + struct.pack('<27d', *([0.01] * 27)), 228)),
    'rawimusx': short_log(1462, pad(struct.pack('<BBHdIiiiiii', 0, 13, WEEK, 345600.0, 0, 100, -100,
                                                50, 10, -10, 5), 40)),
    'rawimus': short_log(325, pad(struct.pack('<IdIiiiiii', WEEK, 345600.0, 0, 100, -100, 50, 10, -10, 5), 40)),
    'heading': long_log(971, pad(struct.pack('<IIfffff', 0, 50, 1.5, 90.0, -1.0, 0.1, 0.2), 44)),
    'gpsephemeris': long_log(7, pad(struct.pack('<IdIIIIII', 5, 345600.0, 0, 120, 120, WEEK, WEEK, 345600), 224)),
    'gloephemeris': long_log(723, pad(struct.pack('<HHHHII', 38, 1, 0, 0, WEEK, MS), 144)),
    'bdsephemeris': long_log(1696, pad(struct.pack('<IIdIIII', 6, WEEK - 1356, 2.0, 0, 1, 0, 345600), 196)),
    'range': rtklib['rangeb'],
}


def write(d, logs):
    os.makedirs(d, exist_ok=True)
    for name, b in logs.items():
        with open(os.path.join(d, name + '.bin'), 'wb') as f:
            f.write(b)
    with open(os.path.join(d, 'stream.bin'), 'wb') as f:
        f.write(b''.join(logs.values()))


write('mayhem/rtklib_oem4/testsuite', rtklib)
write('mayhem/novatel_parser/testsuite', apollo)
