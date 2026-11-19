#!/usr/bin/env python3
"""Add LC_LOAD_DYLIB to a Mach-O executable, in place, without changing its size.

The load commands of this backboardd end at 0x1e68 and its first section starts at
0x2090, so 552 bytes are free; one LC_LOAD_DYLIB needs 64. Because the file keeps its
length, the patched binary can be written to the phone by replacing only its data blocks
(the tool that does that is on the Windows side).

    bbd_add_dylib.py FILE /topaz/lib/libtopaz-inject.dylib

An ad-hoc signature is invalidated by this: run `codesign -f -s - FILE` afterwards, which
is what the workflow does.
"""
import struct
import sys

LC_SEGMENT_64 = 0x19
LC_LOAD_DYLIB = 0xc


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    path, dylib = sys.argv[1], sys.argv[2]

    d = bytearray(open(path, 'rb').read())
    magic, cputype, cpusub, filetype, ncmds, sizeofcmds, flags, res = struct.unpack('<IiiIIIII', d[:32])
    if magic != 0xfeedfacf:
        print('%s: not a 64-bit Mach-O' % path)
        return 1

    payload = dylib.encode() + b'\0'
    cmdsize = (24 + len(payload) + 7) & ~7
    cmd = struct.pack('<IIIIII', LC_LOAD_DYLIB, cmdsize, 24, 0, 0x20000, 0x10000) + payload
    cmd += b'\0' * (cmdsize - len(cmd))

    end = 32 + sizeofcmds
    first_sect = None
    off = 32
    for _ in range(ncmds):
        c, s = struct.unpack('<II', d[off:off + 8])
        if c == LC_SEGMENT_64:
            nsects = struct.unpack('<I', d[off + 64:off + 68])[0]
            if nsects and first_sect is None:
                first_sect = struct.unpack('<I', d[off + 72 + 48:off + 72 + 52])[0]
        off += s

    print('load commands end %#x, first section %#x, %d bytes free, need %d'
          % (end, first_sect or 0, (first_sect or 0) - end, len(cmd)))
    if first_sect is None or end + len(cmd) > first_sect:
        print('no room for another load command')
        return 1

    d[end:end + len(cmd)] = cmd
    struct.pack_into('<II', d, 16, ncmds + 1, sizeofcmds + len(cmd))
    open(path, 'wb').write(bytes(d))
    print('added LC_LOAD_DYLIB %s (ncmds %d -> %d, size unchanged %d)'
          % (dylib, ncmds, ncmds + 1, len(d)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
