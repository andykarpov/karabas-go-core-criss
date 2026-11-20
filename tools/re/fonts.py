import sys
from PIL import Image, ImageDraw
import os; U=os.path.join(os.path.dirname(os.path.abspath(__file__)),'../../upstream/')
F=U+'fw/x/criss.fw.2023.11.CLR/'
OUT=U+'extract/'
a=open(F+'vga_328.hex.bin','rb').read(); b=open(F+'vga_328_fonts.hex.bin','rb').read()
sets=[('cp0',a,0),('cp1',a,1),('cp2',a,2),('cp3',a,3),('cp4',a,4),('cp4_2023_11',b,4)]
R=20
for name,src,i in sets:
    base=0x800+i*0x1400
    raw=bytearray()
    for c in range(256):
        for r in range(R):
            raw.append(src[base+r*256+c])
    open(OUT+'font_%s.bin'%name,'wb').write(raw)
    S=2; cw=8*S+2; ch=R*S+2
    im=Image.new('RGB',(24+16*cw,14+16*ch),(40,40,40))
    d=ImageDraw.Draw(im)
    for k in range(16):
        d.text((24+k*cw+4,1),'%X'%k,fill=(255,255,0))
        d.text((2,14+k*ch+R*S//2-4),'%X'%k,fill=(255,255,0))
    for c in range(256):
        x0=24+(c%16)*cw+1; y0=14+(c//16)*ch+1
        d.rectangle([x0,y0,x0+8*S-1,y0+R*S-1],fill=(0,0,0))
        for r in range(R):
            v=raw[c*R+r]
            for bit in range(8):
                if v&(0x80>>bit):
                    d.rectangle([x0+bit*S,y0+r*S,x0+bit*S+S-1,y0+r*S+S-1],fill=(0,255,0))
    im.save(OUT+'font_%s.png'%name)
    print(name,len(raw))
