#!/usr/bin/env python3
"""Study tools for the original OP-1 firmware R246 (and the op1r patch). Standard library only.

    fwtool.py unpack  op1_246.op1 WORK/orig          .op1 -> files (CRC-32 + LZMA + tar)
    fwtool.py apply   WORK/orig WORK/patched         apply data/manifest.json edits (no repack)
    fwtool.py blocks  WORK/orig/OP1_vdk.ldr          list the LDR blocks
    fwtool.py mem     WORK/orig/OP1_vdk.ldr WORK/mem rebuild the memory image (regions.json + mem_*.bin)
    fwtool.py strings WORK/mem/regions.json          "ADDR string" for every printable string
    fwtool.py addr    WORK/orig/OP1_vdk.ldr 0x93032  LDR file offset -> load address
    fwtool.py offset  WORK/orig/OP1_vdk.ldr 0x193de2a  load address -> LDR file offset
    fwtool.py rd      WORK/mem/regions.json 0x19103e0 [N]       dump N 32-bit words
    fwtool.py dump    WORK/mem/regions.json 0x18ab7cc 0x1aa7078 code.bin   raw bytes of an address range
    fwtool.py ptr     WORK/mem/regions.json 0x19b99d4 [END]     find 32-bit words == value (or in [value, END))
    fwtool.py xref    code.dis strings.txt funcs.json            heuristic function map from a bfin objdump listing
    fwtool.py hooks   WORK/orig [--funcs funcs.json]            decode every code edit of the patch (CALL/JUMP.L)

The firmware files stay in WORK (git-ignored); nothing of the original firmware belongs in the repository.
See docs/firmware-r246.md for what the outputs mean.
"""
import argparse, bisect, json, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import patch_op1 as P                                       # noqa: E402

# ---------------------------------------------------------------- LDR (Blackfin BF52x boot stream)
# 16-byte header: block code, target address, byte count, argument (all LE32)
#   block code: bits 0-3 DMA code, 4-15 flags, 16-23 header XOR checksum, 24-31 sign 0xAD
FLAGS = {0x0010: 'SAFE', 0x0020: 'AUX', 0x0100: 'FILL', 0x0200: 'QUICKBOOT', 0x0400: 'CALLBACK',
         0x0800: 'INIT', 0x1000: 'IGNORE', 0x2000: 'INDIRECT', 0x4000: 'FIRST', 0x8000: 'FINAL'}


def flag_str(f):
    return '|'.join(v for k, v in FLAGS.items() if f & k) or '-'


def ldr_blocks(d):
    off = 0
    while off + 16 <= len(d):
        code, addr, cnt, arg = struct.unpack_from('<IIII', d, off)
        if code >> 24 != 0xad:
            raise SystemExit(f'not an LDR block header at {off:#x}')
        flags = code & 0xfff0
        dlen = 0 if flags & 0x0100 else cnt                 # FILL blocks carry no payload
        yield {'off': off, 'code': code, 'addr': addr, 'cnt': cnt, 'arg': arg, 'flags': flags,
               'hdrchk': (code >> 16) & 0xff, 'data_off': off + 16, 'dlen': dlen}
        off += 16 + dlen


def ldr_regions(d):
    """merged contiguous memory runs [addr, bytearray, nblocks]; IGNORE blocks skipped, FILL expanded"""
    regs = []
    for b in ldr_blocks(d):
        if b['flags'] & 0x1000 or not b['cnt']:
            continue
        if b['flags'] & 0x0100:
            data = struct.pack('<I', b['arg']) * (b['cnt'] // 4) + bytes(b['cnt'] % 4)
        else:
            data = d[b['data_off']:b['data_off'] + b['cnt']]
        if regs and regs[-1][0] + len(regs[-1][1]) == b['addr']:
            regs[-1][1] += data
            regs[-1][2] += 1
        else:
            regs.append([b['addr'], bytearray(data), 1])
    return regs


def off2addr(blocks, o):
    for b in blocks:
        if b['data_off'] <= o < b['data_off'] + b['dlen']:
            return b['addr'] + o - b['data_off']
    return None


def addr2off(blocks, a):
    for b in blocks:
        if b['dlen'] and b['addr'] <= a < b['addr'] + b['dlen']:
            return b['data_off'] + a - b['addr']
    return None


class Mem:
    def __init__(self, regions_json):
        self.r = [(e['addr'], open(e['file'], 'rb').read()) for e in json.load(open(regions_json))]

    def read(self, a, n):
        for base, d in self.r:
            if base <= a < base + len(d):
                return d[a - base:a - base + n]
        raise SystemExit(f'address {a:#010x} is not mapped')


def decode_branch(pc, b):
    """32-bit CALL/JUMP.L pcrel24 (stored as two LE halfwords) -> (kind, target) or (None, None)"""
    h1, h2 = struct.unpack('<HH', b)
    kind = {0xe3: 'CALL', 0xe2: 'JUMP.L'}.get(h1 >> 8)
    if not kind:
        return None, None
    imm = ((h1 & 0xff) << 16) | h2
    if imm & 0x800000:
        imm -= 1 << 24
    return kind, pc + 2 * imm


def encode_branch(kind, pc, target):
    """inverse of decode_branch: bytes of CALL/JUMP.L from pc to target"""
    imm = (target - pc) // 2
    if (target - pc) % 2 or not -(1 << 23) <= imm < (1 << 23):
        raise ValueError('branch target out of pcrel24 range or odd')
    imm &= 0xffffff
    return struct.pack('<HH', ({'CALL': 0xe3, 'JUMP.L': 0xe2}[kind] << 8) | (imm >> 16), imm & 0xffff)


# ---------------------------------------------------------------- commands
def cmd_unpack(a):
    op1 = open(a.firmware, 'rb').read()
    h = P.sha256(op1)
    man = P.load_manifest()
    print(f'sha256 {h}' + ('  (original R246, as expected by the patcher)' if h == man['original']['sha256']
                          else '  (WARNING: not the R246 image the patcher expects)'))
    _, members = P.unpack(op1)
    for m, data in members:
        p = os.path.join(a.out, m.name)
        if m.isdir():
            os.makedirs(p, exist_ok=True)
        elif data is not None:
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, 'wb').write(data)
    print(f'{len(members)} members -> {a.out}')


def cmd_apply(a):
    man = P.load_manifest()
    for f in man['patched_files']:
        data = open(os.path.join(a.orig, f['path']), 'rb').read()
        if P.sha256(data) != f['orig_sha256']:
            raise SystemExit(f'{f["path"]}: not the expected original')
        new = P.apply_ops(f['path'], data, f['ops'])
        assert P.sha256(new) == f['result_sha256']
        p = os.path.join(a.out, f['path'])
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, 'wb').write(new)
        print(f'patched {f["path"]}')
    for f in man['added_files']:
        src = os.path.join(a.orig, f['copy_of']) if 'copy_of' in f else os.path.join(P.DATA, f['blob'])
        p = os.path.join(a.out, f['path'])
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, 'wb').write(open(src, 'rb').read())
        print(f'added   {f["path"]}')


def cmd_blocks(a):
    d = open(a.ldr, 'rb').read()
    n = 0
    for b in ldr_blocks(d):
        print(f'{b["off"]:07x}  addr={b["addr"]:08x} cnt={b["cnt"]:08x} arg={b["arg"]:08x} '
              f'chk={b["hdrchk"]:02x} dma={b["code"] & 15} {flag_str(b["flags"])}')
        n += 1
    print(f'{n} blocks, {len(d):,} bytes')


def cmd_mem(a):
    d = open(a.ldr, 'rb').read()
    os.makedirs(a.out, exist_ok=True)
    idx = []
    for addr, data, nb in ldr_regions(d):
        fn = os.path.join(os.path.abspath(a.out), f'mem_{addr:08x}.bin')
        open(fn, 'wb').write(data)
        idx.append({'addr': addr, 'size': len(data), 'file': fn, 'blocks': nb})
        print(f'{addr:08x}-{addr + len(data):08x}  {len(data):>9,} bytes  {nb} blocks')
    json.dump(idx, open(os.path.join(a.out, 'regions.json'), 'w'), indent=1)


def cmd_strings(a):
    for e in json.load(open(a.regions)):
        d = open(e['file'], 'rb').read()
        for m in re.finditer(rb'[\x20-\x7e\t\n]{%d,}' % a.min, d):
            print('%08x %s' % (e['addr'] + m.start(), m.group().decode().replace('\n', '\\n')))


def cmd_addr(a):
    bl = list(ldr_blocks(open(a.ldr, 'rb').read()))
    r = off2addr(bl, int(a.value, 0))
    print(f'{r:#010x}' if r is not None else 'offset is in a header or a fill block')


def cmd_offset(a):
    bl = list(ldr_blocks(open(a.ldr, 'rb').read()))
    r = addr2off(bl, int(a.value, 0))
    print(f'{r:#x}' if r is not None else 'address not backed by LDR payload (BSS/fill or unmapped)')


def cmd_rd(a):
    m = Mem(a.regions)
    base = int(a.addr, 0)
    b = m.read(base, 4 * a.n)
    for i in range(0, len(b) - len(b) % 4, 16):
        chunk = b[i:i + 16]
        w = struct.unpack_from('<%dI' % (len(chunk) // 4), chunk)
        print(f'{base + i:08x}: ' + ' '.join(f'{x:08x}' for x in w) + '  '
              + ''.join(chr(c) if 32 <= c < 127 else '.' for c in chunk))


def cmd_dump(a):
    lo, hi = int(a.start, 0), int(a.end, 0)
    open(a.out, 'wb').write(Mem(a.regions).read(lo, hi - lo))
    print(f'{hi - lo:,} bytes {lo:08x}-{hi:08x} -> {a.out}')


def cmd_ptr(a):
    lo = int(a.value, 0)
    hi = int(a.end, 0) if a.end else lo + 1
    for base, d in Mem(a.regions).r:
        for o in range(0, len(d) - 3, 4):
            v = struct.unpack_from('<I', d, o)[0]
            if lo <= v < hi:
                print(f'{base + o:08x}: {v:08x}')


def cmd_xref(a):
    """function starts = LINK / register-push prologues + direct CALL targets; per function: referenced strings
    (from the full constants objdump prints in comments), source files named by assert strings, callees"""
    strs = {}
    for line in open(a.strings):
        ad, _, s = line.rstrip('\n').partition(' ')
        strs[int(ad, 16)] = s
    sk = sorted(strs)

    def str_at(v):
        i = bisect.bisect_right(sk, v) - 1
        if i >= 0 and v < sk[i] + len(strs[sk[i]]):
            return strs[sk[i]][v - sk[i]:]
    line_re = re.compile(r'^\s*([0-9a-f]+):\t(?:[0-9a-f]{2} )+\s*(.*)$')
    val_re = re.compile(r'\.[HL] = .*?/\*.*?\b(?:[RP][0-7]|I[0-3])=0x([0-9a-f]+)')
    call_re = re.compile(r'\bCALL 0x0x([0-9a-f]+);')
    insns = []
    for line in open(a.dis):
        m = line_re.match(line)
        if m:
            insns.append((int(m.group(1), 16), m.group(2)))
    starts, callers = set(), {}
    for i, (ad, t) in enumerate(insns):
        if t.startswith('LINK') and not (i and t and insns[i - 1][1].startswith('[--SP] = (')):
            starts.add(ad)
        elif re.match(r'\[--SP\] = \((R7|P5)', t):
            starts.add(ad)
        m = call_re.search(t)
        if m:
            callers.setdefault(int(m.group(1), 16), []).append(ad)
    starts |= set(callers)
    fs = sorted(starts)
    out = {}
    for ad, t in insns:
        i = bisect.bisect_right(fs, ad) - 1
        if i < 0:
            continue
        e = out.setdefault(fs[i], {'strings': set(), 'callees': set()})
        m = val_re.search(t)
        if m:
            s = str_at(int(m.group(1), 16))
            if s:
                e['strings'].add(s[:120])
        m = call_re.search(t)
        if m:
            e['callees'].add(int(m.group(1), 16))
    res = {}
    for f, e in sorted(out.items()):
        src = sorted({m.group(1) for s in e['strings']
                      for m in [re.search(r'\\([A-Za-z0-9_]+\.(?:cpp|c|h)):\d+', s)] if m})
        res[f'{f:08x}'] = {'src': src, 'strings': sorted(e['strings'])[:30],
                           'callees': [f'{c:08x}' for c in sorted(e['callees'])],
                           'callers': [f'{c:08x}' for c in callers.get(f, [])]}
    json.dump(res, open(a.out, 'w'), indent=0)
    print(f'{len(res)} functions, {sum(1 for r in res.values() if r["src"])} named by an assert string -> {a.out}')


def cmd_hooks(a):
    ldr = open(os.path.join(a.orig, 'OP1_vdk.ldr'), 'rb').read()
    bl = list(ldr_blocks(ldr))
    funcs, fs = {}, []
    if a.funcs:
        funcs = json.load(open(a.funcs))
        fs = sorted(int(k, 16) for k in funcs)

    def where(ad):
        if not fs:
            return ''
        i = bisect.bisect_right(fs, ad) - 1
        e = funcs[f'{fs[i]:08x}']
        return f'  [fn {fs[i]:08x} {",".join(e["src"]) or "?"}]'
    man = P.load_manifest()
    f = next(f for f in man['patched_files'] if f['path'] == 'OP1_vdk.ldr')
    for op in f['ops']:
        if op['op'] == 'insert':
            print(f'insert {op["size"]:,} bytes at file offset {op["offset"]:#x} (new LDR block(s))')
            continue
        ad = off2addr(bl, op['offset'])
        old, new = bytes.fromhex(op['old']), bytes.fromhex(op['new'])
        if ad is None:
            print(f'header  file {op["offset"]:#08x}: {old.hex()} -> {new.hex()}')
            continue
        if len(old) == 4 and decode_branch(ad, new)[0]:
            ko, to = decode_branch(ad, old)
            kn, tn = decode_branch(ad, new)
            was = f'{ko} {to:08x}{where(to)}' if ko else f'insn {old.hex()}'
            print(f'code    {ad:08x}{where(ad)}\n          was {was}\n          now {kn} {tn:08x}')
        else:
            print(f'data    {ad:08x}: {old.hex()} -> {new.hex()}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest='cmd', required=True)
    c = sp.add_parser('unpack'); c.add_argument('firmware'); c.add_argument('out')
    c = sp.add_parser('apply'); c.add_argument('orig'); c.add_argument('out')
    c = sp.add_parser('blocks'); c.add_argument('ldr')
    c = sp.add_parser('mem'); c.add_argument('ldr'); c.add_argument('out')
    c = sp.add_parser('strings'); c.add_argument('regions'); c.add_argument('--min', type=int, default=5)
    c = sp.add_parser('addr'); c.add_argument('ldr'); c.add_argument('value')
    c = sp.add_parser('offset'); c.add_argument('ldr'); c.add_argument('value')
    c = sp.add_parser('rd'); c.add_argument('regions'); c.add_argument('addr'); c.add_argument('n', type=int, nargs='?', default=16)
    c = sp.add_parser('dump'); c.add_argument('regions'); c.add_argument('start'); c.add_argument('end'); c.add_argument('out')
    c = sp.add_parser('ptr'); c.add_argument('regions'); c.add_argument('value'); c.add_argument('end', nargs='?')
    c = sp.add_parser('xref'); c.add_argument('dis'); c.add_argument('strings'); c.add_argument('out')
    c = sp.add_parser('hooks'); c.add_argument('orig'); c.add_argument('--funcs')
    a = ap.parse_args()
    globals()['cmd_' + a.cmd](a)


if __name__ == '__main__':
    main()
