import re,sys
import os; BASE=os.path.join(os.path.dirname(os.path.abspath(__file__)),'../../upstream/')
BIN=open(BASE+'fw/x/criss.cpu.4.67/cpu.hex.bin','rb').read()
def load(fn=BASE+'dis/cpu467.S'):
    ins={}; order=[]
    for l in open(fn,errors='replace'):
        m=re.match(r'\s*([0-9a-f]+):\t([0-9a-f ]+?)\s*\t(.*)$',l)
        if m:
            a=int(m.group(1),16); ins[a]=m.group(3).replace('\t',' '); order.append(a)
    return ins,order
def table(page,src=BIN):
    # page relative to SRAM 0x0A00 -> flash 0x1A000
    off=0x1A000+(page-0x0A)*0x100
    return [ (src[off+i] | src[off+0x100+i]<<8)*2 for i in range(256)]
def dump(ins,order,a,n=60):
    import bisect
    i=bisect.bisect_left(order,a)
    out=[]
    for k in order[i:i+n]:
        out.append('%6x: %s'%(k,ins[k]))
    return '\n'.join(out)
