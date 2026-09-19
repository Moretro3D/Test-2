#!/usr/bin/env python3
"""Planche de controle hors firmware: badges, champions et Pokemon des 8 arenes."""
from pathlib import Path
import struct
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parents[1]
DEX=[95,121,26,45,110,65,59,112]
LEADERS=["PIERRE","ONDINE","MAJOR BOB","ERIKA","KOGA","MORGANE","AUGUSTE","GIOVANNI"]
MONS=["ONIX","STAROSS","RAICHU","RAFFLESIA","SMOGOGO","ALAKAZAM","ARCANIN","RHINOFEROS"]

def rgb565(v):
    return (((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31,255)

def idle(dex):
    buf=(ROOT/f"tools/sdcard/mons/p{dex:03d}.bin").read_bytes()
    nacts=buf[4]; palcount=struct.unpack_from('<H',buf,5)[0]
    pal=list(struct.unpack_from(f'<{palcount}H',buf,7)); p=7+palcount*2
    for _ in range(nacts):
        aid,w,h,nf=buf[p:p+4]; p+=4+nf*2
        if aid==0:
            data=buf[p:p+w*h]; out=Image.new('RGBA',(w,h))
            out.putdata([(0,0,0,0) if i==255 else rgb565(pal[i]) for i in data])
            return out
        p+=w*h*nf
    raise ValueError(dex)

canvas=Image.new('RGB',(1200,650),(17,21,35)); d=ImageDraw.Draw(canvas)
d.text((36,22),"APERÇU PIXEL ART — ARÈNES KANTO",fill=(255,255,255),font=ImageFont.load_default())
leaders=Image.open(ROOT/'assets/design/champions-kanto-recolores.png').convert('RGBA')
badges=Image.open(ROOT/'assets/design/badges-kanto-originaux.png').convert('RGBA')
badge_boxes=[
    (150,145,410,390), (660,145,890,390), (1100,125,1390,405), (1600,125,1920,405),
    (150,495,425,765), (650,490,900,750), (1135,480,1410,750), (1625,485,1955,785),
]

for i in range(8):
    col=i%4; row=i//4; x=35+col*290; y=70+row*280
    d.rounded_rectangle((x,y,x+260,y+245),18,fill=(31,39,61),outline=(81,102,145),width=2)
    badge=badges.crop(badge_boxes[i])
    alpha=badge.getchannel('A').getbbox()
    badge=badge.crop(alpha)
    badge.thumbnail((54,54),Image.Resampling.NEAREST)
    canvas.paste(badge,(x+18,y+16),badge)
    champ=leaders.crop((i*64,0,i*64+64,56)).resize((128,112),Image.Resampling.NEAREST)
    canvas.paste(champ,(x+8,y+80),champ)
    mon=idle(DEX[i]); scale=min(4,130//max(mon.width,mon.height))
    mon=mon.resize((mon.width*scale,mon.height*scale),Image.Resampling.NEAREST)
    canvas.paste(mon,(x+185-mon.width//2,y+145-mon.height//2),mon)
    d.text((x+78,y+20),LEADERS[i],fill=(255,255,255),font=ImageFont.load_default())
    d.text((x+145,y+202),MONS[i],fill=(108,220,255),font=ImageFont.load_default())

out=ROOT.parent/'APERCU-PIXEL-ART-ARENES-KANTO.png'
canvas.save(out,optimize=True)
print(out)
