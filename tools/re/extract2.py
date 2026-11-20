from PIL import Image
import os; U=os.path.join(os.path.dirname(os.path.abspath(__file__)),'../../upstream/')
OUT=U+'extract/'
b=open(U+'fw/x/criss.cpd.0.24/cpd.hex.bin','rb').read()
open(OUT+'kbd_main.bin','wb').write(b[0x2400:0x2C00])
open(OUT+'kbd_caps.bin','wb').write(b[0x2000:0x2100])
open(OUT+'kbd_e0.bin','wb').write(b[0x2100:0x2115])
open(OUT+'cpd_tones.bin','wb').write(b[0x4400:0x4480])
# human-readable table
def ch(x):
    if x==0: return ''
    if x<32: return '^%d'%x
    if x==127: return 'DEL'
    return bytes([x]).decode('cp866')
with open(OUT+'kbd_main.txt','w') as f:
    f.write('# kbd_main.bin: offset = page*256 + ctrl*128 + scan; page = shift + 2*rus + 4*alt\n')
    f.write('scan\t'+'\t'.join('s%dr%da%dc%d'%(p&1,(p>>1)&1,p>>2,c) for p in range(8) for c in (0,1))+'\n')
    for s in range(128):
        row=[ch(b[0x2400+p*256+c*128+s]) for p in range(8) for c in (0,1)]
        if any(row): f.write('%02X\t'%s+'\t'.join(row)+'\n')
# VGA EEPROM splash: emulate loader at 0x490
F=U+'fw/x/criss.fw.2023.11.CLR/'
e=open(F+'vga_328.eep.bin','rb').read()
v=open(F+'vga_328.hex.bin','rb').read()
mode=e[0]; fill=0 if (mode&7)==1 else 0x20
buf=bytearray([fill])*2000   # SRAM 0x100..0x8CF
row=0; x=0; i=1
while True:
    c=e[i]; i+=1
    if c==0x1F: break
    if c==0x0D: row+=1; x=row*80; continue
    if c==0x09:
        a=(0x100+x)|7; a+=1; x=a-0x100
        if a==0x880: break
        continue
    buf[x]=c; x+=1
print('mode',hex(mode),'bytes used',i,'last row',row)
open(OUT+'vga_eeprom_splash.bin','wb').write(buf)
cp=mode&7; base=0x800+cp*0x1400; R=20
im=Image.new('L',(640,480))
for r in range(24):
    for line in range(R):
        for col in range(80):
            g=v[base+line*256+buf[r*80+col]]
            for bit in range(8):
                if g&(0x80>>bit): im.putpixel((col*8+bit,r*R+line),255)
im.save(OUT+'vga_eeprom_splash.png')
# cpd start screen rendered as text
s=open(U+'fw/x/criss.cpd.0.24/cpd_start_screen.bin','rb').read()
print(s.split(b'\x1f')[0].replace(b'\r',b'\n').decode('cp866'))
