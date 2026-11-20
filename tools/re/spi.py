import sys; sys.path.insert(0,__import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from lib import *
import re, collections
ins,order=load()
c=collections.Counter(); ex={}
for i,a in enumerate(order):
    s=ins[a]
    if s.startswith('out 0x05, r21') and order[i-1] and ins[order[i-1]].startswith('ldi r21'):
        pb=ins[order[i-1]].split(',')[1].split(';')[0].strip()
        # find SPCR within next 8
        sp='?'
        for k in order[i+1:i+8]:
            if ins[k].startswith('out 0x2c'):
                prev=ins[order[order.index(k)-1]]
                sp=prev.split(',')[1].split(';')[0].strip() if prev.startswith('ldi') else prev
                break
        c[(pb,sp)]+=1; ex.setdefault((pb,sp),[]).append(hex(a))
for k,v in sorted(c.items()): print(k,v,ex[k][:6])
