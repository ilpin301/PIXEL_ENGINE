#!/usr/bin/env python3
"""Per-module flash/RAM report from a GNU ld map file (see --help)."""
import argparse, collections, os, re, shutil, subprocess, sys, tempfile

HEX = r'0x[0-9a-fA-F]+'
OUT_HDR = re.compile(rf'^(\.\S+|/DISCARD/)(?:\s+({HEX})\s+({HEX})(?:\s+load address\s+({HEX}))?)?\s*$')
OUT_NUMS = re.compile(rf'^\s+({HEX})\s+({HEX})(?:\s+load address\s+({HEX}))?\s*$')
IN_FULL = re.compile(rf'^ (\S+)\s+({HEX})\s+({HEX})\s*(.*?)\s*$')
IN_CONT = re.compile(rf'^\s+({HEX})\s+({HEX})\s*(.*?)\s*$')
REGION = re.compile(rf'^(\w+)\s+({HEX})\s+({HEX})')
LINKER, PADDING = '(linker-generated)', '(padding)'
CAPS = (('SMALL', 64), ('MEDIUM', 128), ('LARGE', 256))
SKIP_DIRS = ('build', '.git', 'tools', 'docs')


def read_regions(text):
    """name -> (origin, end, kind); kind is 'flash' or 'ram'."""
    block = text.split('Memory Configuration', 1)[1].split('Linker script and memory map', 1)[0]
    regs = {}
    for line in block.splitlines():
        m = REGION.match(line)
        if m and int(m[2], 16):  # origin 0 = *default*, not a real region
            org = int(m[2], 16)
            regs[m[1]] = (org, org + int(m[3], 16), 'flash' if m[1] in ('ROM', 'FLASH') else 'ram')
    return regs


def region_of(addr, regs):
    for name, (lo, hi, _) in regs.items():
        if lo <= addr < hi:
            return name
    return 'EXT' if addr >= 0xC0000000 else None  # address 0 = not loaded


class Namer:
    """Maps a map-file object path to a module name."""

    def __init__(self, src_root):
        self.src = collections.defaultdict(list)
        for d, dirs, files in os.walk(src_root):
            dirs[:] = [x for x in dirs if not x.startswith(SKIP_DIRS)]
            for f in files:
                if f.lower().endswith(('.c', '.cpp', '.s')):
                    rel = os.path.relpath(os.path.join(d, f), src_root).replace('\\', '/')
                    self.src[f].append(rel)
        self.cache = {}

    def __call__(self, raw):
        if raw not in self.cache:
            self.cache[raw] = self._name(raw.replace('\\', '/'))
        return self.cache[raw]

    @staticmethod
    def _group(rel):
        parts = rel.split('/')
        if parts[0] == 'core':
            return 'core/' + parts[2] if parts[1:2] == ['src'] and len(parts) > 3 else 'core'
        if parts[0] == 'drivers' and len(parts) > 2:
            return 'drivers/' + parts[1]
        return parts[0]

    def _name(self, p):
        absolute = re.match(r'^([A-Za-z]:)?/', p) is not None
        m = re.match(r'^(.*)\((.+)\)$', p)
        if m:
            hits = self.src.get(re.sub(r'\.(obj|o)$', '', m[2]), [])
            if len(hits) == 1:
                return self._group(hits[0])
            stem = os.path.basename(m[1])[:-2]  # drop ".a"
            if absolute:  # toolchain archive: keep "lib" so libc/libgcc read as such
                return stem.replace('_nano', '')
            return (stem[3:] if stem.startswith('lib') else stem) + '(other)'
        if absolute:
            return 'crt' if re.match(r'crt.*\.o$', os.path.basename(p)) else '(other)'
        if '.dir/' in p:
            before, after = p.rsplit('.dir/', 1)
            base = before.split('/')[:-1]
            if base and base[-1] == 'CMakeFiles':
                base.pop()
            rest = after.split('/')
            while rest[0] == '__':  # each "__" is one ".." out of the CMake object dir
                rest.pop(0)
                if base:
                    base.pop()
            return self._group('/'.join(base + rest))
        return '(other)'


def parse_map(text, regs, namer):
    """Returns (tally[module][region] -> bytes, violations, flash-resident *_array bytes)."""
    tally = collections.defaultdict(lambda: collections.defaultdict(int))
    bad, array_flash = [], 0
    first_flash = next((n for n, r in regs.items() if r[2] == 'flash'), None)
    sec = pend = None  # current output section (dict), pending name-only line
    name_of = lambda n: LINKER if n == '*fill*' and sec['name'] == '._user_heap_stack' else \
        PADDING if n == '*fill*' else n

    def emit(mod, n, image):
        tally[mod][sec['region']] += n
        sec['prim'] += n
        if sec['lma'] or (image and first_flash and regs.get(sec['region'], (0, 0, ''))[2] == 'ram'
                          and sec['name'].startswith('.data')):
            tally[mod][sec['lma'] or first_flash] += n

    def close():
        nonlocal array_flash
        if sec and sec['region']:
            if sec['size'] > sec['prim']:
                emit(LINKER, sec['size'] - sec['prim'], False)
            if sec['prim'] != sec['size']:
                bad.append(f"{sec['name']}: modules sum {sec['prim']} != section size {sec['size']}")
            if regs.get(sec['region'], (0, 0, ''))[2] == 'flash' and sec['name'].endswith('_array'):
                array_flash += sec['size']

    for line in text.split('Linker script and memory map', 1)[1].splitlines():
        if line and not line[0].isspace():
            close()
            sec, pend = None, None
            m = OUT_HDR.match(line)
            if m and m[1] != '/DISCARD/':
                sec = dict(name=m[1], prim=0, region=None, size=0, lma=None)
                if m[2]:
                    sec.update(addr=int(m[2], 16), size=int(m[3], 16))
                    _set_region(sec, regs, m[4])
                else:
                    pend = ('out', None)
            continue
        if sec is None:
            continue
        if pend:
            kind, name = pend
            pend = None
            m = (OUT_NUMS if kind == 'out' else IN_CONT).match(line)
            if not m:
                continue
            if kind == 'out':
                sec.update(addr=int(m[1], 16), size=int(m[2], 16))
                _set_region(sec, regs, m[3])
            elif sec['region']:
                emit(namer(m[3]) if m[3] else '(other)', int(m[2], 16), name.startswith('.data'))
            continue
        m = IN_FULL.match(line)
        if m and sec['region']:
            n = name_of(m[1])
            emit(n if n in (LINKER, PADDING) else namer(m[4]) if m[4] else '(other)',
                 int(m[3], 16), m[1].startswith('.data'))
        elif not m:
            m = re.match(r'^ (\.\S+|COMMON)\s*$', line)  # long names put the numbers on the next line
            if m:
                pend = ('in', m[1])
    close()
    return tally, bad, array_flash


def _set_region(sec, regs, lma):
    sec['region'] = region_of(sec['addr'], regs) if sec['addr'] else None
    sec['lma'] = region_of(int(lma, 16), regs) if lma else None


def run_size(tool, *args):
    exe = shutil.which(tool) or (r'C:/ProgramData/chocolatey/bin/' + tool)
    out = subprocess.run([exe, *args], capture_output=True, text=True, check=True).stdout
    return [int(x) for x in out.strip().splitlines()[-1].split()[:3]]  # text data bss


def report(tally, regs, array_flash, args=None):
    kind = {n: r[2] for n, r in regs.items()}
    kind['EXT'] = 'ext'
    col = lambda mod, k: sum(b for r, b in tally[mod].items() if kind[r] == k)
    tot = {k: sum(col(m, k) for m in tally) for k in ('flash', 'ram', 'ext')}
    used = collections.Counter()
    for m in tally:
        used.update(tally[m])
    print('Regions')
    for n, (lo, hi, k) in regs.items():
        print(f'  {n:<8} used {used[n]:>9} / {hi - lo:>9} B  {100.0 * used[n] / (hi - lo):6.2f} %')
    if used['EXT']:
        print(f"  {'EXT':<8} used {used['EXT']:>9} B")
    print(f"  flash used {tot['flash']} B (incl. .data load images), ram used {tot['ram']} B (incl. .data)\n")
    w = max([len(m) for m in tally] + [6])
    print(f"{'module':<{w}} | {'flash B':>9} | {'ram B':>9} | {'ext B':>9}")
    for m in sorted(tally, key=lambda m: (-col(m, 'flash'), -col(m, 'ram'), m)):
        if not any(tally[m].values()):
            continue  # discarded/zero-size stubs
        print(f"{m:<{w}} | {col(m, 'flash'):>9} | {col(m, 'ram'):>9} | {col(m, 'ext'):>9}")
    print(f"{'TOTAL':<{w}} | {tot['flash']:>9} | {tot['ram']:>9} | {tot['ext']:>9}\n")
    eng = sum(col(m, 'flash') for m in tally if m == 'core' or m.startswith('core/'))
    fits = [n for n, kb in CAPS if eng <= kb * 1024]
    print(f"ENGINE (core/*) flash = {eng} B; fits caps: {', '.join(fits) or 'none'} "
          f"(SMALL 64 KB, MEDIUM 128 KB, LARGE 256 KB)")
    print('  linked objects only; templates instantiated in the game object are counted under examples; '
          'unlinked modules are not counted')
    return tot, array_flash


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('map', nargs='?')
    ap.add_argument('--elf')
    ap.add_argument('--src-root', default=root)
    ap.add_argument('--core-lib')
    ap.add_argument('--size-tool', default='arm-none-eabi-size')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.map:
        ap.error('MAP is required')
    text = open(a.map, encoding='utf-8', errors='replace').read()
    if 'Linker script and memory map' not in text or 'Memory Configuration' not in text:
        print('not a GNU ld map file', file=sys.stderr)
        return 2
    regs = read_regions(text)
    tally, bad, array_flash = parse_map(text, regs, Namer(a.src_root))
    tot, array_flash = report(tally, regs, array_flash)
    rc = 0
    for b in bad:
        print('WARNING invariant violated:', b)
        rc = 1
    if a.core_lib:
        t, d, _ = run_size(a.size_tool, '-t', a.core_lib)
        print(f'libpge_core.a all modules (text+data) = {t + d} B (arm-none-eabi-size -t)')
    if a.elf:
        t, d, b = run_size(a.size_tool, a.elf)
        # size classes writable flash-resident *_array sections as data, though they are not RAM
        want_ram = tot['ram'] + array_flash
        ok = (t + d == tot['flash'], d + b == want_ram)
        if all(ok):
            print(f'CROSS-CHECK OK: size text+data = {t + d} = flash {tot["flash"]}; '
                  f'size data+bss = {d + b} = ram {tot["ram"]} + {array_flash} (init/fini arrays)')
        else:
            print(f'CROSS-CHECK MISMATCH: size text+data = {t + d} vs report flash {tot["flash"]}; '
                  f'size data+bss = {d + b} vs report ram {want_ram}')
            rc = 1
    return rc


SELFTEST_MAP = """\
Discarded input sections

 .text.dead     0x00000000       0x99 core/libpge_core.a(dead.cpp.obj)

Memory Configuration

Name             Origin             Length             Attributes
CCMRAM           0x10000000         0x00010000         xrw
RAM              0x20000000         0x00030000         xrw
ROM              0x08000000         0x00200000         xr
*default*        0x00000000         0xffffffff

Linker script and memory map

LOAD c:/tc/crtbegin.o
.isr_vector     0x08000000       0x40
 *(.isr_vector)
 .isr_vector    0x08000000       0x40 platform/hal/libpge_hal.a(startup.s.obj)
                0x08000000                g_pfnVectors

.text           0x08000040       0x88
 *(.text)
 .text          0x08000040       0x10 c:/tc/lib/gcc/10.3.1/crtbegin.o
 *fill*         0x08000050        0x4
 *(.text*)
 .text._ZN3pge5world4stepEv
                0x08000054       0x60 core/libpge_core.a(world.cpp.obj)
                0x08000054                pge::world::step()
 .text.memchr   0x080000b4       0x14 c:/tc/lib\\libc_nano.a(lib_a-memchr.o)

.data           0x20000000       0x14 load address 0x080000c8
                0x20000000                _sdata = .
 *(.data*)
 .data.g_world  0x20000000       0x10 core/libpge_core.a(world.cpp.obj)
 *fill*         0x20000010        0x4

.bss            0x20000014       0x40
 *(.bss*)
 .bss.s_buf     0x20000014       0x30 platform/hal/CMakeFiles/game.dir/__/__/examples/main.cpp.obj

._user_heap_stack
                0x20000054      0x204
                0x20000058                PROVIDE (end = .)
                0x20000258                . = (. + 0x200)
 *fill*         0x20000054        0x4

.debug_info     0x00000000      0x100
 .debug_info    0x00000000      0x100 core/libpge_core.a(world.cpp.obj)
"""


def selftest():
    with tempfile.TemporaryDirectory() as root:
        os.makedirs(os.path.join(root, 'core', 'src', 'world'))
        open(os.path.join(root, 'core', 'src', 'world', 'world.cpp'), 'w').close()
        regs = read_regions(SELFTEST_MAP)
        tally, bad, _ = parse_map(SELFTEST_MAP, regs, Namer(root))
    got = {m: (sum(b for r, b in t.items() if regs[r][2] == 'flash'),
               sum(b for r, b in t.items() if regs[r][2] == 'ram')) for m, t in tally.items()}
    want = {'core/world': (112, 16), 'crt': (16, 0), 'libc': (20, 0), PADDING: (8, 4),
            'pge_hal(other)': (64, 0), 'examples': (0, 48), LINKER: (0, 532)}
    assert not bad, bad
    assert got == want, got
    print('selftest OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
