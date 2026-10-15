#!/usr/bin/env python3
"""A small 6502 assembler for the subset of AS65 syntax that Bruce Clark's /
Klaus Dormann's 6502_decimal_test.a65 uses (labels, `name = value`, `if` /
`endif`, one-line macros, `org`, `ds`, `db`, `bss` / `code`), written for
this project (MIT). The test source itself is downloaded at test time and
never committed.

    asm6502.py <in.a65> <out.bin> [name=value ...]

Writes the code segment as one image starting at its first `org`, and
prints that address. name=value overrides an assignment in the source
(e.g. chk_n=1).
"""
import re
import sys

OPS = {
    # mnemonic: {mode: opcode}
    'adc': {'imm': 0x69, 'zp': 0x65, 'zpx': 0x75, 'abs': 0x6d, 'absx': 0x7d, 'absy': 0x79},
    'and': {'imm': 0x29, 'zp': 0x25, 'zpx': 0x35, 'abs': 0x2d, 'absx': 0x3d, 'absy': 0x39},
    'cmp': {'imm': 0xc9, 'zp': 0xc5, 'zpx': 0xd5, 'abs': 0xcd, 'absx': 0xdd, 'absy': 0xd9},
    'eor': {'imm': 0x49, 'zp': 0x45, 'zpx': 0x55, 'abs': 0x4d, 'absx': 0x5d, 'absy': 0x59},
    'lda': {'imm': 0xa9, 'zp': 0xa5, 'zpx': 0xb5, 'abs': 0xad, 'absx': 0xbd, 'absy': 0xb9},
    'ora': {'imm': 0x09, 'zp': 0x05, 'zpx': 0x15, 'abs': 0x0d, 'absx': 0x1d, 'absy': 0x19},
    'sbc': {'imm': 0xe9, 'zp': 0xe5, 'zpx': 0xf5, 'abs': 0xed, 'absx': 0xfd, 'absy': 0xf9},
    'sta': {'zp': 0x85, 'zpx': 0x95, 'abs': 0x8d, 'absx': 0x9d, 'absy': 0x99},
    'ldx': {'imm': 0xa2, 'zp': 0xa6, 'zpy': 0xb6, 'abs': 0xae, 'absy': 0xbe},
    'ldy': {'imm': 0xa0, 'zp': 0xa4, 'zpx': 0xb4, 'abs': 0xac, 'absx': 0xbc},
    'stx': {'zp': 0x86, 'zpy': 0x96, 'abs': 0x8e},
    'sty': {'zp': 0x84, 'zpx': 0x94, 'abs': 0x8c},
    'cpx': {'imm': 0xe0, 'zp': 0xe4, 'abs': 0xec},
    'cpy': {'imm': 0xc0, 'zp': 0xc4, 'abs': 0xcc},
    'inc': {'zp': 0xe6, 'zpx': 0xf6, 'abs': 0xee, 'absx': 0xfe},
    'dec': {'zp': 0xc6, 'zpx': 0xd6, 'abs': 0xce, 'absx': 0xde},
    'jmp': {'abs': 0x4c},
    'jsr': {'abs': 0x20},
}
IMPLIED = {
    'brk': 0x00, 'php': 0x08, 'clc': 0x18, 'plp': 0x28, 'sec': 0x38, 'rti': 0x40, 'pha': 0x48,
    'cli': 0x58, 'rts': 0x60, 'pla': 0x68, 'sei': 0x78, 'dey': 0x88, 'txa': 0x8a, 'tya': 0x98,
    'txs': 0x9a, 'tay': 0xa8, 'tax': 0xaa, 'clv': 0xb8, 'tsx': 0xba, 'iny': 0xc8, 'dex': 0xca,
    'cld': 0xd8, 'inx': 0xe8, 'nop': 0xea, 'sed': 0xf8,
}
BRANCHES = {'bpl': 0x10, 'bmi': 0x30, 'bvc': 0x50, 'bvs': 0x70, 'bcc': 0x90, 'bcs': 0xb0,
            'bne': 0xd0, 'beq': 0xf0}


def value(expr, syms, strict):
    """+/- of numbers ($hex, %bin, decimal) and symbols; None if unknown."""
    total = 0
    for sign, term in re.findall(r'([+-]?)\s*([$%]?\w+)', expr.replace(' ', '')):
        if term.startswith('$'):
            v = int(term[1:], 16)
        elif term.startswith('%'):
            v = int(term[1:], 2)
        elif term.isdigit():
            v = int(term)
        elif term in syms:
            v = syms[term]
        elif strict:
            raise SystemExit('undefined symbol ' + term)
        else:
            return None
        total += -v if sign == '-' else v
    return total


def condition(text, syms):
    m = re.match(r'(.+?)\s*(!=|=|<|>)\s*(.+)', text)
    a, op, b = value(m.group(1), syms, True), m.group(2), value(m.group(3), syms, True)
    return {'=': a == b, '!=': a != b, '<': a < b, '>': a > b}[op]


def assemble(lines, overrides):
    syms = {}
    for final in (False, True):
        out = {}
        pc = {'code': 0, 'bss': 0}
        seg = 'code'
        stack = []           # if nesting: True = assembling
        macros, in_macro = {}, None
        start = None
        for raw in lines:
            line = raw.split(';', 1)[0].replace('\t', ' ').rstrip()
            if not line.strip():
                continue
            words = line.split()
            low = [w.lower() for w in words]
            if in_macro is not None:
                if low[0] == 'endm':
                    in_macro = None
                else:
                    macros[in_macro].append(line)
                continue
            if low[0] == 'if':
                stack.append(all(stack) and condition(line.split(None, 1)[1], syms))
                continue
            if low[0] == 'endif':
                stack.pop()
                continue
            if low[0] == 'else':
                stack[-1] = not stack[-1] and all(stack[:-1])
                continue
            if not all(stack):
                continue
            if len(words) > 1 and low[1] == 'macro':
                in_macro = words[0]
                macros[in_macro] = []
                continue
            m = re.match(r'(\w+)\s*=\s*(.+)', line.strip())
            if m and not line[0].isspace():
                name = m.group(1)
                syms[name] = overrides.get(name, value(m.group(2), syms, True))
                continue
            label = None
            if not line[0].isspace():
                parts = line.split(None, 1)
                label, line = parts[0], parts[1] if len(parts) > 1 else ''
                if label in macros:
                    line, label = label, None
            body = line.strip()
            if label:
                syms[label] = pc[seg]
            if not body:
                continue
            mnem, _, operand = body.partition(' ')
            mnem, operand = mnem.lower(), operand.strip()
            if mnem in macros:
                for ml in macros[mnem]:
                    assemble_one(ml.strip(), pc, seg, syms, final, out)
                continue
            if mnem in ('bss', 'code'):
                seg = mnem
                continue
            if mnem == 'org':
                pc[seg] = value(operand, syms, True)
                if seg == 'code' and start is None:
                    start = pc[seg]
                continue
            if mnem == 'end':
                break
            assemble_one(body, pc, seg, syms, final, out)
    return out, start


def assemble_one(body, pc, seg, syms, final, out):
    mnem, _, operand = body.partition(' ')
    mnem, operand = mnem.lower(), operand.strip().lower()
    operand_raw = body.partition(' ')[2].strip()

    def emit(*bs):
        for b in bs:
            if seg == 'code':
                out[pc[seg]] = b & 0xff
            pc[seg] += 1

    if mnem == 'ds':
        pc[seg] += value(operand_raw, syms, True)
    elif mnem == 'db':
        for item in operand_raw.split(','):
            emit(value(item, syms, True))
    elif mnem in IMPLIED:
        emit(IMPLIED[mnem])
    elif mnem in BRANCHES:
        target = value(operand_raw, syms, final)
        off = 0 if target is None else target - (pc[seg] + 2)
        if final and not -128 <= off <= 127:
            raise SystemExit('branch out of range: ' + body)
        emit(BRANCHES[mnem], off)
    elif mnem in OPS:
        modes = OPS[mnem]
        if operand_raw.startswith('#'):
            emit(modes['imm'], value(operand_raw[1:], syms, final) or 0)
            return
        index = ''
        expr = operand_raw
        m = re.match(r'(.+),\s*([xXyY])$', operand_raw)
        if m:
            expr, index = m.group(1), m.group(2).lower()
        v = value(expr, syms, final)
        if v is not None and v < 0x100 and ('zp' + index) in modes:
            emit(modes['zp' + index], v)
        else:
            v = v or 0
            emit(modes['abs' + index], v, v >> 8)
    else:
        raise SystemExit('unknown instruction: ' + body)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    overrides = {}
    for kv in sys.argv[3:]:
        k, v = kv.split('=')
        overrides[k] = int(v, 0)
    with open(src, encoding='latin-1') as f:
        lines = f.read().splitlines()
    out, start = assemble(lines, overrides)
    lo, hi = min(out), max(out)
    image = bytes(out.get(a, 0) for a in range(lo, hi + 1))
    with open(dst, 'wb') as f:
        f.write(image)
    print('%04x' % lo)


if __name__ == '__main__':
    main()
