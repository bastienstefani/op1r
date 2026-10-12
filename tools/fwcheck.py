#!/usr/bin/env python3
"""Offline checks of a built .op1 image before flashing it (standard library only).

    fwcheck.py diff  IMAGE.op1 REF.op1          what IMAGE changes against REF (e.g. the op1r reference image or
                                                a third-party base): files, LDR structure, memory, databases
    fwcheck.py calls LISTING.dis LO HI          CALL / JUMP.L targets outside [LO, HI) in a bfin objdump listing
                                                (the code your blob calls into: must be stock or verified op1r)
    fwcheck.py refs  LISTING.dis [LO HI]        absolute addresses built with X.H / X.L pairs in a listing
                                                (memory a block of code uses; default range 01e00000-01f00000)
    fwcheck.py blockhash IMAGE.op1 ADDR         sha256 of the LDR block loaded at ADDR (to pin a code block)

Make REF with:  python3 tools/fwbuild.py op1_246.op1 --op1r -o ref.op1
Nothing here reads or writes the repository; temporary files only.  See docs/developpement.md.
"""
import argparse, hashlib, os, re, sqlite3, struct, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
import fwtool as T                                           # noqa: E402
import patch_op1 as P                                        # noqa: E402

FREE = (0x01dfff34, 0x01f00000)                              # SDRAM free for added code (see docs/firmware-r246.md 4)


def files_of(op1):
    _, members = P.unpack(op1)
    return {m.name: d for m, d in members if m.isfile()}


def ldr_checks(ldr):
    """header checksums, FIRST arg, FINAL position; returns (blocks, problems)"""
    bl = list(T.ldr_blocks(ldr))
    bad = []
    for b in bl:
        c = b['code']
        x = c & 0xff ^ (c >> 8) & 0xff ^ (c >> 24) & 0xff
        for w in (b['addr'], b['cnt'], b['arg']):
            for i in range(4):
                x ^= (w >> (8 * i)) & 0xff
        if x != b['hdrchk']:
            bad.append(f"header checksum of block at file offset {b['off']:#x}")
    if not bl[0]['flags'] & 0x4000 or bl[0]['arg'] != len(ldr) - 16:
        bad.append('first block: not FIRST or argument != file size - 16')
    fin = [i for i, b in enumerate(bl) if b['flags'] & 0x8000]
    if len(fin) != 1:
        bad.append(f'{len(fin)} FINAL blocks')
    return bl, bad


def regions(ldr):
    return {a: bytes(d) for a, d, _ in T.ldr_regions(ldr)}


def mem_diff(new, ref):
    out = []
    for a, b in sorted(new.items()):
        if a not in ref:
            out.append((a, len(b), 'new region'))
            continue
        r = ref[a]
        n = min(len(r), len(b))
        i = 0
        while i < n:
            if r[i] != b[i]:
                j = i
                while j < n and any(r[k] != b[k] for k in range(j, min(j + 4, n))):
                    j += 1
                out.append((a + i, j - i, ''))
                i = j
            else:
                i += 1
        if len(r) != len(b):
            out.append((a + n, abs(len(r) - len(b)), 'region size differs'))
    for a in sorted(set(ref) - set(new)):
        out.append((a, len(ref[a]), 'region MISSING'))
    return out


def db_diff(new, ref, tmp):
    pa, pb = os.path.join(tmp, 'new.db'), os.path.join(tmp, 'ref.db')
    open(pa, 'wb').write(new)
    open(pb, 'wb').write(ref)
    a, b = sqlite3.connect(pa), sqlite3.connect(pb)
    lines = []
    if a.execute('pragma integrity_check').fetchone()[0] != 'ok':
        lines.append('  integrity_check FAILED')
    ta = {n for (n,) in a.execute("select name from sqlite_master where type='table'")}
    tb = {n for (n,) in b.execute("select name from sqlite_master where type='table'")}
    for t in sorted(ta | tb):
        if t not in tb:
            lines.append(f'  new table {t}')
            continue
        if t not in ta:
            lines.append(f'  table {t} REMOVED')
            continue
        ra, rb = set(a.execute(f'select * from "{t}"')), set(b.execute(f'select * from "{t}"'))
        for r in sorted(ra - rb, key=repr):
            lines.append(f'  {t} + {r!r}'[:160])
        for r in sorted(rb - ra, key=repr):
            lines.append(f'  {t} - {r!r}'[:160])
    return lines


def cmd_diff(a):
    new, ref = open(a.image, 'rb').read(), open(a.ref, 'rb').read()
    print(f'image {a.image}: {len(new):,} bytes, sha256 {P.sha256(new)}')
    print(f'ref   {a.ref}: {len(ref):,} bytes, sha256 {P.sha256(ref)}')
    if len(new) >= 15_000_000:
        print('PROBLEM: image is 15 MB or more (the OP-1 refuses it)')
    fn, fr = files_of(new), files_of(ref)
    print('\n[files]')
    for k in sorted(set(fn) | set(fr)):
        if k not in fr:
            print(f'  added   {k} ({len(fn[k])} bytes)')
        elif k not in fn:
            print(f'  REMOVED {k}')
        elif fn[k] != fr[k]:
            print(f'  changed {k}')
    if 'te-boot.ldr' in fn and fn.get('te-boot.ldr') != fr.get('te-boot.ldr'):
        print('  PROBLEM: te-boot.ldr (the bootloader) differs from the reference')
    ldr = fn['OP1_vdk.ldr']
    bl, bad = ldr_checks(ldr)
    print(f'\n[OP1_vdk.ldr] {len(bl)} blocks' + ('' if bad else ', headers / FIRST / FINAL ok'))
    for p in bad:
        print('  PROBLEM:', p)
    print('  blocks in the free SDRAM window %08x-%08x:' % FREE)
    used = []
    for b in bl:
        if b['cnt'] and not b['flags'] & 0x1000 and b['addr'] < FREE[1] and b['addr'] + b['cnt'] > FREE[0]:
            used.append((b['addr'], b['addr'] + b['cnt']))
            print('    %08x-%08x %s' % (b['addr'], b['addr'] + b['cnt'], T.flag_str(b['flags'])))
    used.sort()
    for (a0, e0), (a1, e1) in zip(used, used[1:]):
        if a1 < e0:
            print('  PROBLEM: blocks %08x and %08x overlap' % (a0, a1))
    print('\n[memory] changed ranges against the reference')
    for addr, n, k in mem_diff(regions(ldr), regions(fr['OP1_vdk.ldr'])):
        print('  %08x +%-7d %s' % (addr, n, k))
    with tempfile.TemporaryDirectory() as tmp:
        for name in sorted(k for k in fn if k.endswith('.db') and k in fr and fn[k] != fr[k]):
            print(f'\n[{name}] rows added (+) / removed (-)')
            for line in db_diff(fn[name], fr[name], tmp):
                print(line)


BR = re.compile(r'^ *([0-9a-f]+):\t.*\b(CALL|JUMP\.L) 0x(?:0x)?([0-9a-f]+)(?: <([^>]+)>)?', re.I)


def cmd_calls(a):
    lo, hi = int(a.lo, 16), int(a.hi, 16)
    seen = {}
    for line in open(a.listing):
        m = BR.match(line)
        if m:
            t = int(m.group(3), 16)
            if not lo <= t < hi:
                seen.setdefault(t, (m.group(4) or '?', int(m.group(1), 16)))
    for t, (name, site) in sorted(seen.items()):
        print('%08x %-28s (first call at %08x)' % (t, name, site))


PAIR = re.compile(r'^ *([0-9a-f]+):\t.*\t([PRI][0-7]|FP)\.([HL]) = (-?0x[0-9a-f]+|-?\d+);')


def cmd_refs(a):
    lo, hi = (int(a.lo, 16), int(a.hi, 16)) if a.lo else (0x01e00000, 0x01f00000)
    ins = []
    for line in open(a.listing):
        m = PAIR.match(line)
        ins.append((int(m.group(1), 16), m.group(2), m.group(3), int(m.group(4), 0) & 0xffff) if m else None)
    vals = {}
    for i, e in enumerate(ins):
        if not e or e[2] != 'H':
            continue
        for d in range(1, 40):                       # the nearest .L of the same register
            hit = [ins[j] for j in (i + d, i - d) if 0 <= j < len(ins) and ins[j] and ins[j][1] == e[1] and ins[j][2] == 'L']
            if hit:
                v = (e[3] << 16) | hit[0][3]
                if lo <= v < hi:
                    vals.setdefault(v, e[0])
                break
    for v, at in sorted(vals.items()):
        print('%08x (built at %08x)' % (v, at))
    if vals:
        print(f'{len(vals)} addresses, lowest {min(vals):08x}, highest {max(vals):08x}')


def cmd_blockhash(a):
    ldr = files_of(open(a.image, 'rb').read())['OP1_vdk.ldr']
    addr = int(a.addr, 16)
    for b in T.ldr_blocks(ldr):
        if b['addr'] == addr and not b['flags'] & 0x0100:
            print('%08x +%#x sha256 %s' % (addr, b['cnt'],
                  hashlib.sha256(ldr[b['data_off']:b['data_off'] + b['cnt']]).hexdigest()))
            return
    raise SystemExit(f'no payload block loaded at {addr:#x}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('diff'); s.add_argument('image'); s.add_argument('ref'); s.set_defaults(f=cmd_diff)
    s = sub.add_parser('calls'); s.add_argument('listing'); s.add_argument('lo'); s.add_argument('hi')
    s.set_defaults(f=cmd_calls)
    s = sub.add_parser('refs'); s.add_argument('listing'); s.add_argument('lo', nargs='?'); s.add_argument('hi', nargs='?')
    s.set_defaults(f=cmd_refs)
    s = sub.add_parser('blockhash'); s.add_argument('image'); s.add_argument('addr'); s.set_defaults(f=cmd_blockhash)
    a = ap.parse_args()
    a.f(a)


if __name__ == '__main__':
    main()
