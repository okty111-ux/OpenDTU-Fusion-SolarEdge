from PIL import Image, ImageDraw, ImageFont
import sys
S=3; COL=(255,196,60); OFF=(22,18,10)
def oled(fn):
    lines=open(fn).read().split('\n')[2:]
    im=Image.new('RGB',(256*S,64*S),(8,8,8)); d=ImageDraw.Draw(im)
    for y,l in enumerate(lines[:64]):
        for x,v in enumerate(l.split()):
            d.rectangle([x*S,y*S,x*S+S-2,y*S+S-2],fill=COL if v=='1' else OFF)
    return im
def sheet(items,out,title):
    font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',18)
    fb=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',24)
    pad=24; bw=256*S+2*16; bh=64*S+2*16
    H=70+len(items)*(bh+44)
    im=Image.new('RGB',(bw+2*pad,H),(245,245,242)); d=ImageDraw.Draw(im)
    d.text((pad,22),title,font=fb,fill=(30,30,30))
    y=70
    for fn,lab in items:
        d.text((pad,y),lab,font=font,fill=(60,60,60)); y+=26
        d.rounded_rectangle([pad,y,pad+bw,y+bh],12,fill=(18,18,18))
        im.paste(oled(fn),(pad+16,y+16)); y+=bh+18
    im.save(out)
day=[('dzien_0.pbm','1. Produkcja – moc chwilowa i uzysk dziś'),('dzien_1.pbm','2. Wykres mocy dziś – Hoymiles (pełne) + SolarEdge (kratka)'),('dzien_2.pbm','3. Sieć – licznik SolarEdge (eksport / pobór)'),('dzien_3.pbm','4. Hoymiles MPPT – napięcie, prąd, moc, dziś'),('dzien_4.pbm','5. Panele – szacunek mocy każdego panelu w szeregu'),('dzien_5.pbm','6. SolarEdge – szczegóły falownika'),('dzien_6.pbm','7. Historia – wczoraj, miesiąc, rok, łącznie'),('dzien_7.pbm','8. Status – IP, sieć, karta SD, inwertery'),('dzien_0_overlay.pbm','Długie przytrzymanie przycisku – komunikat')]
sheet(day,'/tmp/fdprev/ssd1322_dzien.png','SSD1322 256×64 – strony w dzień')
night=[('noc_0.pbm','1. Produkcja – brak produkcji'),('noc_1.pbm','2. Wykres mocy – cały dzisiejszy dzień'),('noc_2.pbm','3. Sieć – pobór nocny'),('noc_3.pbm','4. Hoymiles MPPT – inwerter uśpiony'),('noc_5.pbm','6. SolarEdge – uśpiony'),('noc_7.pbm','8. Status')]
sheet(night,'/tmp/fdprev/ssd1322_noc.png','SSD1322 256×64 – strony w nocy')
