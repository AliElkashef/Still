from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import math, random, struct

root = Path(__file__).resolve().parents[1]
assets = root / 'Card/STILL/UI'
assets.mkdir(parents=True, exist_ok=True)
design = root / 'Design'
S = 3
ink = '#402022'
muted = '#744945'
accent = '#80362E'
names = ['Rain garden', 'Campfire', 'Woodland', 'My mix']

def font(size, weight=400, scale=S):
    f = ImageFont.truetype(str(design/'Inter.ttf'), round(size*scale))
    f.set_variation_by_axes([14, weight])
    return f

def image(w,h):
    return Image.new('RGB',(w*S,h*S),'#E6F6FD')

def rounded(im, box, radius, fill, outline=None):
    ImageDraw.Draw(im).rounded_rectangle(tuple(round(v*S) for v in box), radius*S, fill, outline, S if outline else 1)

def text(im,xy,s,size,weight=400,fill=ink, anchor='lt'):
    ImageDraw.Draw(im).text(tuple(v*S for v in xy),s,font=font(size,weight),fill=fill,anchor=anchor)

def grad(im, top, bottom):
    d=ImageDraw.Draw(im)
    for y in range(im.height):
        c=tuple(round(a+(b-a)*y/(im.height-1)) for a,b in zip(top,bottom))
        d.line((0,y,im.width,y),fill=c)

def rgb565(im):
    values=[((r>>3)<<11)|((g>>2)<<5)|(b>>3) for r,g,b in im.getdata()]
    return struct.pack('<'+'H'*len(values),*values)

def pack(im,name,preview=False):
    im=im.resize((im.width//S,im.height//S),Image.Resampling.LANCZOS)
    if preview: im.save(design/(name+'.png'))
    (assets/(name+'.raw')).write_bytes(rgb565(im))

def icon(im,kind,x,y,color=accent):
    d=ImageDraw.Draw(im)
    def line(points): d.line([(int((x+a)*S),int((y+b)*S)) for a,b in points],fill=color,width=2*S,joint='curve')
    def ellipse(a,b,c,e): d.ellipse(((x+a)*S,(y+b)*S,(x+c)*S,(y+e)*S),fill=color)
    if kind==0:
        for j in (0,5,10): line([(j+2,1),(j,7)])
    elif kind==1:
        d.polygon([((x+a)*S,(y+b)*S) for a,b in [(1,8),(4,3),(6,6),(8,0),(12,7),(10,11),(5,12)]],fill=color)
    elif kind==2:
        line([(0,5),(10,5),(12,2),(9,1)]);line([(2,9),(9,9),(12,11)])
    else:
        line([(0,6),(3,4),(6,7),(9,4),(12,6)])

THEMES = [
    ("#70D8FF", "#09233B"),
    ("#FFBA75", "#3D1A10"),
    ("#C6ED86", "#1A3027"),
    ("#FFE0A3", "#2B263D"),
]
PAGES = ("scene", "mixer", "settings")

def illustration(theme, tick=0):
    palette=[
        ((13,27,68),(63,139,163),['#477789','#32576C','#224559','#142B45'],'#8EE7F7'),
        ((35,27,70),(205,104,99),['#93617C','#604961','#383750','#222940'],'#FFD19A'),
        ((27,70,80),(127,179,138),['#739982','#456F67','#305951','#143C3B'],'#E1F2AA'),
        ((59,83,162),(249,154,101),['#CB827E','#AB626E','#79566A','#514455'],'#FFE0A4'),
    ]
    top,bottom,hills,light=palette[theme]
    im=image(320,240);grad(im,top,bottom)
    d=ImageDraw.Draw(im)
    def ellipse(cx,cy,rx,ry,c):
        d.ellipse(tuple(round(v*S) for v in (cx-rx,cy-ry,cx+rx,cy+ry)),fill=c)
    def polygon(points,c):
        d.polygon([(round(x*S),round(y*S)) for x,y in points], fill=c)
    def line(points,c,width=1):
        d.line([(round(x*S),round(y*S)) for x,y in points],fill=c,width=max(1,round(width*S)),joint='curve')
    x0,y0=237,67
    for radius,alpha in [(46,.14),(31,.22),(19,1)]:
        ellipse(x0,y0,radius,radius,Image.blend(Image.new('RGB',(1,1),top),Image.new('RGB',(1,1),light),alpha).getpixel((0,0)))
    for k,color in enumerate(hills):
        y=105+22*k
        polygon([(x,y+17*math.sin(x/65+k*1.2)+8*math.sin(x/29+k)) for x in range(321)]+[(320,240),(0,240)],color)
    if theme != 3:
        rng=random.Random(22+theme)
        for j in range(34):
            x=rng.uniform(-5,325); y=rng.uniform(153,219)
            height=(y-128)*rng.uniform(.37,.72)
            color=hills[2] if y<178 else hills[3]
            for tier in range(3):
                p=y-height+tier*height*.23
                w=height*(.09+.045*tier)
                polygon([(x,p),(x-w,p+height*.54),(x+w,p+height*.54)],color)
    if theme==0:
        polygon([(0,162),(73,145),(129,149),(202,125),(320,143),(320,240),(0,240)],'#193E58')
        rng=random.Random(36)
        for _ in range(28):
            x=rng.uniform(1,319); y=rng.uniform(129,228)
            phase=(tick/24+rng.random())%1
            size=(1+phase*8)*(y/190)
            c=Image.blend(Image.new('RGB',(1,1),'#193E58'),Image.new('RGB',(1,1),'#92E6FB'),(1-phase)*.65).getpixel((0,0))
            d.arc(tuple(round(v*S) for v in (x-size,y-size*.23,x+size,y+size*.23)),0,360,fill=c,width=S)
        for _ in range(67):
            x=rng.randrange(334)-7; phase=(tick/24+rng.random())%1
            y=32+phase*142
            line([(x,y),(x-3,y+8)], '#83BDD1' if x%4==0 else '#5490AB',.65)
    if theme==1:
        cx,ground=202,141
        for r,c in [(64,'#733F49'),(48,'#97514C'),(36,'#B7664E')]:
            ellipse(cx,ground+1,r,r*.26,c)
        line([(174,153),(223,144)],'#311E2D',7)
        line([(179,144),(221,154)],'#492A33',7)
        for j,(color,h,w) in enumerate([('#EF603D',53,22),('#FFA447',43,15),('#FFE181',27,8)]):
            angle=math.tau*tick/24
            def edge(u,side):
                t=u/32
                bend=(7*math.sin(angle+j)+7)*t*t+4*math.sin(t*7+angle)*t
                span=w*(1-t)**.62*(.50+.65*math.sin(math.pi*t)**.8)
                span*=1+.20*math.sin(t*15+angle*2+side)
                return (cx+bend+side*span,ground-h*t)
            points=[edge(u,1) for u in range(33)]+[edge(u,-1) for u in range(32,-1,-1)]
            polygon(points,color)
        for j in range(8):
            phase=(tick/24+(j*.137))%1
            x=cx+(j-4)*3+phase*12*math.sin(j);y=ground-15-phase*(38+j*3)
            ellipse(x,y,1.1,1.1,'#FFE0A0' if phase<.6 else '#FF965C')
    if theme==2:
        polygon([(159,240),(176,210),(191,184),(179,166),(203,155),(218,146),(218,149),
                 (204,160),(191,168),(209,185),(204,213),(232,240)],'#B6A572')
        for x,y in [(223,95),(239,89),(252,98)]:
            line([(x-5,y),(x,y+3),(x+5,y)],'#E8ECCA',1.4)
    return im

def glass(im, box, tint, alpha=.48, radius=10):
    x0,y0,x1,y1=box
    bounds=tuple(v*S for v in box)
    region=im.crop(bounds)
    plate=Image.blend(region.filter(ImageFilter.GaussianBlur(3*S)),
                      Image.new('RGB',region.size,tint),alpha)
    mask=Image.new('L',region.size)
    ImageDraw.Draw(mask).rounded_rectangle((0,0,plate.width-1,plate.height-1),radius*S,fill=255)
    im.paste(plate,(x0*S,y0*S),mask)
    outline=Image.new('RGBA',im.size)
    ImageDraw.Draw(outline).rounded_rectangle(bounds,radius*S,outline=(255,255,255,78),width=S)
    im.paste(outline,(0,0),outline)

def shading(im):
    layer=Image.new('RGBA',im.size)
    d=ImageDraw.Draw(layer)
    for y in range(im.height):
        v=y/S
        alpha=round(143*max(0,1-v/105)**1.8 + 160*max(0,(v-143)/97)**1.6)
        d.line((0,y,im.width,y),fill=(5,8,16,alpha))
    im.paste(layer,(0,0),layer)
    return im

def navigation(im, selected, highlight, dark):
    glass(im,(4,201,316,238),dark,.65,14)
    for index,label in enumerate(("Scenes","Mixer","Settings")):
        if index==selected:
            glass(im,(10+80*index,205,70+80*index,234),dark,.66,10)
        text(im,(40+80*index,219),label,11,650,highlight if index==selected else '#FFFFFF',anchor='mm')

backgrounds=[shading(illustration(i)) for i in range(4)]
for index,name in enumerate(('rain','fire')):
    area=(0,47*S,320*S,151*S)
    base=backgrounds[index].crop(area)
    mask=Image.new('L',base.size)
    for row in range(mask.height):
        alpha=min(255,round(min(row,mask.height-1-row)/(10*S)*255))
        ImageDraw.Draw(mask).line((0,row,mask.width,row),fill=alpha)
    frames=[]
    for tick in range(24):
        shot=base.copy()
        shot.paste(shading(illustration(index,tick)).crop(area),(0,0),mask)
        frames.append(shot.resize((320,104),Image.Resampling.LANCZOS))
    (assets/(name+'.frames')).write_bytes(b''.join(rgb565(z) for z in frames))
    preview=[f.resize((640,208),Image.Resampling.LANCZOS) for f in frames]
    preview[0].save(design/(name.title()+'-loop.gif'),save_all=True,
                    append_images=preview[1:],duration=200,loop=0)

for i,back in enumerate(backgrounds):
    highlight,dark=THEMES[i]
    for page in range(3):
        im=back.copy()
        if page==0:
            text(im,(12,7),'STILL',9,700,highlight)
            text(im,(11,21),names[i],24,700,'#FFFFFF')
            for j,label in enumerate(['Rain','Fire','Woodland','My mix']):
                x=4+80*j
                glass(im,(x,156,x+72,197),dark,.60 if j!=i else .83)
                color=highlight if j==i else '#FFFFFF'
                icon(im,[0,1,3,2][j],x+30,161,color)
                text(im,(x+36,185),label,10,650,color,anchor='mm')
        else:
            title='Sound mixer' if page==1 else 'Settings'
            text(im,(11,8),title,18,700,'#FFFFFF')
            text(im,(12,34),names[i],11,650,highlight)
            rows=[33,73,113,153] if page==1 else [32,77,118,159]
            labels=['Rain','Fire','Wind','Birds'] if page==1 else ['Master','Sleep timer','Save My mix','Calibrate touch']
            for j,(y,label) in enumerate(zip(rows,labels)):
                glass(im,(158,y,315,y+36),dark,.70)
                if page==1:
                    icon(im,j,164,y+5,highlight)
                text(im,(181 if page==1 else 166,y+6 if page==1 or j<2 else y+13),
                     label,11,650,'#FFFFFF')
            if page==1 or page==2:
                slider_rows=rows if page==1 else rows[:1]
                strips=Image.new('RGB',(144,14*len(slider_rows)))
                # These exact pixels are restored when a slider moves.
                small=im.resize((320,240),Image.Resampling.LANCZOS)
                for j,y in enumerate(slider_rows):
                    strips.paste(small.crop((164,y+20,308,y+34)),(0,j*14))
                (assets/(PAGES[page]+str(i)+'-rails.raw')).write_bytes(rgb565(strips))
        navigation(im,page,highlight,dark)
        stem=PAGES[page]+str(i)
        pack(im,stem,True)
        for label in ('play','pause'):
            button=im.copy()
            glass(button,(241,205,311,234),highlight,.92,10)
            text(button,(276,219),label.title(),12,700,dark,anchor='mm')
            pack(button.crop((240*S,202*S,316*S,236*S)),stem+'-'+label)

def vlw(size,weight):
    f=font(size,weight,1)
    ascent,descent=f.getmetrics()
    table=[]; bitmap=[]
    for code in range(33,127):
        c=chr(code)
        l,t,r,b=f.getbbox(c,anchor='ls')
        glyph=Image.new('L',(r-l,b-t))
        ImageDraw.Draw(glyph).text((-l,-t),c,255,font=f,anchor='ls')
        table.extend([code,b-t,r-l,round(f.getlength(c)),-t,l,0])
        bitmap.append(glyph.tobytes())
    return struct.pack('>6i',94,11,size,0,ascent,descent)+struct.pack('>'+str(len(table))+'i',*table)+b''.join(bitmap)

with (root/'Still/UiFonts.h').open('w') as h:
    h.write('#pragma once\n#include <Arduino.h>\n')
    for name,size,weight in [('interRegular',12,400),('interSemibold',13,600)]:
        data=vlw(size,weight)
        h.write(f'const uint8_t {name}[] PROGMEM = {{\n')
        for i in range(0,len(data),24):
            h.write('  '+','.join(map(str,data[i:i+24]))+',\n')
        h.write('};\n')

# Native-size panel assets shown at double scale.
preview=image(680,530)
grad(preview,(36,46,63),(12,21,32))
text(preview,(14,10),'Still',20,700,'#FFFFFF')
for name,x,y in [('scene0',12,41),('scene1',348,41),('mixer2',12,287),('scene3',348,287)]:
    panel=Image.open(design/(name+'.png')).resize((320*S,240*S),Image.Resampling.NEAREST)
    theme=int(name[-1])
    highlight,dark=THEMES[theme]
    sprite=(assets/(name+'-play.raw')).read_bytes()
    overlay=Image.new('RGB',(76,34))
    overlay.putdata([((v>>11)*255//31,((v>>5)&63)*255//63,(v&31)*255//31)
                     for (v,) in struct.iter_unpack('<H',sprite)])
    panel.paste(overlay.resize((76*S,34*S),Image.Resampling.NEAREST),(240*S,202*S))
    if name.startswith('mixer'):
        for y0,v in zip((33,73,113,153),(25,0,48,80)):
            rounded(panel,(170,y0+24,261,y0+28),2,'#8A9599')
            cx=170+v*91/100
            if v: rounded(panel,(170,y0+24,cx,y0+28),2,highlight)
            ImageDraw.Draw(panel).ellipse(((cx-4)*S,(y0+22)*S,(cx+4)*S,(y0+30)*S),fill=highlight)
            rounded(panel,(269,y0+20,307,y0+33),5,dark)
            text(panel,(288,y0+27),str(v)+'%',10,650,'#FFFFFF',anchor='mm')
    preview.paste(panel,(x*S,y*S))
preview.resize((1360,1060),Image.Resampling.LANCZOS).save(design/'Still-preview.png')
