"""Find RIP-relative references to addresses in a PE, mapped to their .pdata function.

usage: xref.py <pe> <addr hex> [...]
"""
import bisect
import sys

import capstone
import pefile

p = pefile.PE(sys.argv[1])
B = p.OPTIONAL_HEADER.ImageBase
img = p.get_memory_mapped_image()
funcs = sorted((e.struct.BeginAddress, e.struct.EndAddress) for e in p.DIRECTORY_ENTRY_EXCEPTION)
starts = [f[0] for f in funcs]
targets = {int(a, 16) - B for a in sys.argv[2:]}
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.skipdata = True
for s in p.sections:
    if not s.Characteristics & 0x20000000:
        continue
    va = s.VirtualAddress
    for i in md.disasm(img[va:va + s.Misc_VirtualSize], B + va):
        if "rip" not in i.op_str:
            continue
        try:
            d = int(i.op_str.split("rip")[1].split("]")[0].replace(" ", ""), 16)
        except ValueError:
            continue
        t = i.address + i.size + d - B
        if t in targets:
            k = bisect.bisect_right(starts, i.address - B) - 1
            f = funcs[k] if k >= 0 and funcs[k][0] <= i.address - B < funcs[k][1] else None
            print(f"{hex(t + B)} <- {hex(i.address)} {i.mnemonic} {i.op_str}  func={hex(f[0] + B) if f else '?'}")
