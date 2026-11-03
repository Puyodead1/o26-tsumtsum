import sys,pefile,capstone
p=pefile.PE(sys.argv[1],fast_load=True)
B=p.OPTIONAL_HEADER.ImageBase;img=p.get_memory_mapped_image()
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
a=int(sys.argv[2],16);n=int(sys.argv[3])
for i in md.disasm(img[a-B:a-B+n],a): print(hex(i.address),i.mnemonic,i.op_str)
