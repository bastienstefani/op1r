#!/usr/bin/env python3
"""Experimental: build an .op1 from the original R246 image plus your own code and edits (study/prototyping).

    fwbuild.py op1_246.op1 -o out.op1 [--op1r] [--blob code.bin@0x01f00000 ...]
               [--call SITE=TARGET ...] [--jump SITE=TARGET ...] [--word ADDR=VALUE ...] [--bytes ADDR=HEX ...]
               [--text ADDR=TEXT ...] [--file PATH=LOCALFILE ...] [--sql DB=STATEMENT ...] [--ldr-out OP1_vdk.ldr]

  --op1r          start from the op1r patch (data/manifest.json) instead of the stock firmware
  --base B.op1    start from another patched image B (e.g. someone's firmware built on op1r) instead; the
                  positional firmware must still be the original R246 image
  --blob F@ADDR   load the raw binary F at ADDR (new LDR block inserted before the FINAL block)
  --call S=T      the 32-bit CALL at address S now calls T (S must already hold a CALL, unless --force)
  --jump S=T      write JUMP.L T at address S (e.g. at a function entry: S must hold a CALL/JUMP.L or a
                  prologue; use --force to skip the check)
  --word A=V      32-bit little-endian data word at A (e.g. a vtable slot or a pointer)
  --bytes A=HEX   raw bytes at A
  --text A=TEXT   replace the C string at A in place (the new text must fit; the rest is filled with NUL)
  --file P=F      add or replace a file of the image (e.g. content/display/my.svg, content/op1_factory.db)
  --sql DB=SQL    run an SQL statement on a database of the image, e.g.
                  --sql "content/op1_factory.db=INSERT INTO fx_types VALUES (2, 'filter', '[...]')"

Addresses are load addresses (as in the disassembly), not file offsets. Sites must lie in an LDR payload block
(not in BSS/fill). Each edit checks it does not touch a region already modified in this run.

Nothing here is verified on hardware unless you flash-test it. The bootloader is never touched, so the original
op1_246.op1 can always be reflashed. Use at your own risk.
"""
import argparse, copy, os, sqlite3, struct, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
import patch_op1 as P                                       # noqa: E402
import fwtool as T                                          # noqa: E402

FREE_SDRAM = (0x01dfff34, 0x01f00000)    # gap between the firmware image and te-boot's own data (01f00000-01f21308)


def hdr(code_flags, addr, count, arg, dma=6):
    """16-byte BF52x LDR block header with its XOR checksum"""
    code = 0xad000000 | (code_flags & 0xfff0) | dma
    x = code ^ addr ^ count ^ arg
    x ^= x >> 16
    x ^= x >> 8
    return struct.pack('<IIII', code | ((x & 0xff) << 16), addr, count, arg)


def fix_first_header(ldr):
    """first block (FIRST|IGNORE) argument = size of the DXE after its header; recompute + checksum"""
    code, addr, cnt, arg = struct.unpack_from('<IIII', ldr, 0)
    if not code & 0x4000:
        raise SystemExit('first LDR block is not a FIRST block')
    ldr[0:16] = hdr(code & 0xfff0, addr, cnt, len(ldr) - 16, code & 0xf)   # stock: arg == file size - 16


def insert_blob(ldr, addr, data):
    blocks = list(T.ldr_blocks(ldr))
    for b in blocks:
        if b['cnt'] and not b['flags'] & 0x1000 and b['addr'] < addr + len(data) and addr < b['addr'] + b['cnt']:
            raise SystemExit(f'blob {addr:#x}+{len(data):#x} overlaps an existing block at {b["addr"]:#x}')
    if not FREE_SDRAM[0] <= addr and addr + len(data) <= FREE_SDRAM[1]:
        print(f'warning: blob {addr:#x} is outside the free SDRAM window {FREE_SDRAM[0]:#x}-{FREE_SDRAM[1]:#x}')
    if len(data) % 4:
        data += bytes(4 - len(data) % 4)
    final = next(b for b in blocks if b['flags'] & 0x8000)
    ldr[final['off']:final['off']] = hdr(0, addr, len(data), 0) + data
    fix_first_header(ldr)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('firmware')
    ap.add_argument('-o', '--output', required=True)
    ap.add_argument('--op1r', action='store_true')
    ap.add_argument('--base', help='start from this already patched image instead of the original / op1r')
    ap.add_argument('--blob', action='append', default=[])
    ap.add_argument('--call', action='append', default=[])
    ap.add_argument('--jump', action='append', default=[])
    ap.add_argument('--word', action='append', default=[])
    ap.add_argument('--bytes', action='append', default=[])
    ap.add_argument('--text', action='append', default=[])
    ap.add_argument('--file', action='append', default=[])
    ap.add_argument('--sql', action='append', default=[])
    ap.add_argument('--force', action='store_true', help='skip the "site holds a CALL" checks')
    ap.add_argument('--ldr-out', help='also write the resulting OP1_vdk.ldr here')
    a = ap.parse_args()

    op1 = open(a.firmware, 'rb').read()
    man = P.load_manifest()
    if P.sha256(op1) != man['original']['sha256']:
        raise SystemExit('input is not the original R246 image (sha256 mismatch)')
    if a.base and a.op1r:
        raise SystemExit('--base and --op1r exclude each other (a base built on op1r already contains it)')
    if a.base:
        base = open(a.base, 'rb').read()
        print(f'base   {a.base} (sha256 {P.sha256(base)})')
        _, members = P.unpack(base)
    else:
        _, members = P.unpack(op1)
    by_name = {m.name: i for i, (m, _) in enumerate(members)}

    if a.op1r:                                              # same steps as patch_op1.patch(), without repacking
        for f in man['patched_files']:
            i = by_name[f['path']]
            members[i] = (members[i][0], P.apply_ops(f['path'], members[i][1], f['ops']))
        for f in man['added_files']:
            data = members[by_name[f['copy_of']]][1] if 'copy_of' in f else \
                open(os.path.join(P.DATA, f['blob']), 'rb').read()
            info = copy.copy(members[by_name[f['template']]][0])
            info.name = f['path']
            members.append((info, data))
            by_name[f['path']] = len(members) - 1

    li = by_name['OP1_vdk.ldr']
    ldr = bytearray(members[li][1])
    for spec in a.blob:
        path, _, at = spec.rpartition('@')
        insert_blob(ldr, int(at, 0), open(path, 'rb').read())
        print(f'blob   {path} at {int(at, 0):#010x}')

    touched = []

    def put(addr, new, what):
        bl = list(T.ldr_blocks(ldr))
        off = T.addr2off(bl, addr)
        if off is None or T.addr2off(bl, addr + len(new) - 1) != off + len(new) - 1:
            raise SystemExit(f'{what} {addr:#x}: not inside one LDR payload block')
        for lo, hi in touched:
            if lo < addr + len(new) and addr < hi:
                raise SystemExit(f'{what} {addr:#x}: overlaps an earlier edit')
        touched.append((addr, addr + len(new)))
        old = bytes(ldr[off:off + len(new)])
        ldr[off:off + len(new)] = new
        return old

    for kind, specs in (('CALL', a.call), ('JUMP.L', a.jump)):
        for spec in specs:
            s, t = (int(x, 0) for x in spec.split('='))
            bl = list(T.ldr_blocks(ldr))
            off = T.addr2off(bl, s)
            if off is None:
                raise SystemExit(f'{kind} site {s:#x} is not in the image payload')
            k0, t0 = T.decode_branch(s, bytes(ldr[off:off + 4]))
            if kind == 'CALL' and k0 != 'CALL' and not a.force:
                raise SystemExit(f'site {s:#x} does not hold a CALL ({bytes(ldr[off:off + 4]).hex()}); --force to override')
            put(s, T.encode_branch(kind, s, t), kind)
            print(f'{kind:6} {s:#010x} -> {t:#010x}' + (f'   (was {k0} {t0:#010x})' if k0 else ''))
    for spec in a.word:
        ad, v = (int(x, 0) for x in spec.split('='))
        old = put(ad, struct.pack('<I', v & 0xffffffff), 'word')
        print(f'word   {ad:#010x} = {v:#010x}   (was {struct.unpack("<I", old)[0]:#010x})')
    for spec in a.bytes:
        ad, hx = spec.split('=')
        old = put(int(ad, 0), bytes.fromhex(hx), 'bytes')
        print(f'bytes  {int(ad, 0):#010x} = {hx}   (was {old.hex()})')

    for spec in a.text:
        ad, _, txt = spec.partition('=')
        ad = int(ad, 0)
        bl = list(T.ldr_blocks(ldr))
        off = T.addr2off(bl, ad)
        if off is None:
            raise SystemExit(f'text {ad:#x}: not in the image payload')
        n = ldr.index(0, off) - off                         # length of the existing string
        new = txt.encode()
        if len(new) > n:
            raise SystemExit(f'text {ad:#x}: {txt!r} is longer than the original ({n} bytes)')
        old = put(ad, new + bytes(n - len(new)), 'text')
        print(f'text   {ad:#010x} = {txt!r}   (was {old.decode("latin1")!r})')

    for _ in T.ldr_blocks(ldr):                             # re-parse: raises on a broken block chain
        pass
    members[li] = (members[li][0], bytes(ldr))
    for spec in a.file:
        path, _, local = spec.partition('=')
        data = open(local, 'rb').read()
        if path in by_name:
            i = by_name[path]
            members[i] = (members[i][0], data)
        else:
            parent = os.path.dirname(path)
            sib = next(m for m, _ in members if os.path.dirname(m.name) == parent and m.isfile())
            info = copy.copy(sib)
            info.name = path
            members.append((info, data))
        print(f'file   {path} <- {local}')
    for spec in a.sql:
        path, _, stmt = spec.partition('=')
        i = by_name[path]
        with tempfile.TemporaryDirectory() as td:
            fn = os.path.join(td, 'x.db')
            open(fn, 'wb').write(members[i][1])
            con = sqlite3.connect(fn)
            con.execute(stmt)
            con.commit()
            con.close()
            members[i] = (members[i][0], open(fn, 'rb').read())
        print(f'sql    {path}: {stmt}')
    if a.ldr_out:
        open(a.ldr_out, 'wb').write(ldr)
    out = P.pack_op1(P.pack_tar(members))
    open(a.output, 'wb').write(out)
    print(f'{a.output}: {len(out):,} bytes, sha256 {P.sha256(out)}')


if __name__ == '__main__':
    main()
