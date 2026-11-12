import re
css=open('/tmp/page/common.css').read()+open('/tmp/page/extra.css').read()
I={
 'home':'<path d="M3 10.5 12 3l9 7.5"/><path d="M5 9.5V21h14V9.5"/><path d="M10 21v-6h4v6"/>',
 'chart':'<path d="M3 3v18h18"/><path d="M7 15l4-5 3 3 5-7"/>',
 'panel':'<rect x="3" y="4" width="18" height="12" rx="1"/><path d="M3 10h18M9 4v12M15 4v12M12 16v4M8 20h8"/>',
 'table':'<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M3 10h18M9 10v10"/>',
 'sd':'<path d="M8 3h7l4 4v13a1 1 0 0 1-1 1H8a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"/><path d="M10 7v3M13 7v3M16 8v2"/>',
 'set':'<path d="M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1"/><circle cx="15" cy="6" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="17" cy="18" r="2"/>',
 'upd':'<path d="M12 3v12"/><path d="M7 10l5 5 5-5"/><path d="M4 17v3h16v-3"/>',
 'cloud':'<path d="M7 18a4.5 4.5 0 0 1-.6-8.96A6 6 0 0 1 18 10.5a3.75 3.75 0 0 1-.5 7.5z"/>',
 'dtu':'<path d="M5 11a7 7 0 0 1 14 0"/><path d="M8.5 11a3.5 3.5 0 0 1 7 0"/><circle cx="12" cy="11" r="1"/><path d="M12 12v9"/>',
}
def ic(k): return f'<svg viewBox="0 0 24 24" aria-hidden="true">{I[k]}</svg>'
MENU=[('Instalacja',[('/history','home','Pulpit'),('/history#wykres','chart','Wykresy'),('/history#mppt','panel','Inwertery i panele'),('/history#tabele','table','Historia')]),
      ('Urządzenie',[('/sd','sd','Karta SD'),('/fusion/settings','set','Ustawienia'),('/fusion/tuya','cloud','Chmura Tuya'),('/fusion/update','upd','Aktualizacja z GitHuba'),('/?opendtu','dtu','OpenDTU')])]
def shell(title,body):
    nav=''.join(f'<div class="grp">{g}</div>'+''.join(f'<a class="it" href="{h}">{ic(k)}<span>{t}</span></a>' for h,k,t in items) for g,items in MENU)
    side=('<aside class="side" id="side" aria-label="Menu"><div class="brand"><div class="mk"><svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="#fff" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M2 12h2M20 12h2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg></div>'
          '<div><b>Fusion</b><small>Hoymiles + SolarEdge</small></div></div>'+nav+
          '<div class="foot">OpenDTU Fusion v2<span id="sideVer"></span><br><span id="sideSt">dane lokalne, bez chmury</span><span class="cred">Przygotował Okty z materiałów dostępnych na GitHub</span></div></aside><div class="scrim" id="scrim"></div>')
    top=f'<div class="top"><button class="burger" id="burger" aria-label="Otwórz menu" aria-controls="side" aria-expanded="false"><svg viewBox="0 0 24 24"><path d="M4 6h16M4 12h16M4 18h16"/></svg></button><h1>{title}</h1><div class="t" id="now"></div></div>'
    js=('<script>(function(){const s=document.getElementById("side"),c=document.getElementById("scrim"),b=document.getElementById("burger");'
        'const set=o=>{s.classList.toggle("open",o);c.classList.toggle("on",o);b.setAttribute("aria-expanded",o)};'
        'b.onclick=()=>set(!s.classList.contains("open"));c.onclick=()=>set(false);document.addEventListener("keydown",e=>{if(e.key==="Escape")set(false)});'
        'const mark=()=>{const p=location.pathname.replace(/\\/$/,"")||"/",h=location.hash;let best=null;'
        's.querySelectorAll("a.it").forEach(a=>{a.classList.remove("act");const u=new URL(a.getAttribute("href"),location.href),ap=u.pathname.replace(/\\/$/,"")||"/";'
        'if(ap===p&&(u.hash===h||(!u.hash&&!best)))best=a;});'
        'if(!best)s.querySelectorAll("a.it").forEach(a=>{const ap=new URL(a.getAttribute("href"),location.href).pathname;if(!best&&ap===location.pathname)best=a;});'
        'if(best){best.classList.add("act");best.setAttribute("aria-current","page")}};'
        's.querySelectorAll("a.it").forEach(a=>a.addEventListener("click",()=>{set(false);setTimeout(mark,0)}));window.addEventListener("hashchange",mark);mark();'
        'fetch("/api/fusion/update",{cache:"no-store"}).then(r=>r.ok?r.json():null).then(u=>{if(!u)return;const a=s.querySelector("a.it[href=\'/fusion/update\']");'
        'if(a&&u.available&&u.latest){const n=document.createElement("span");n.className="nb";n.textContent="v"+u.latest.version;a.appendChild(n);}'
        'const f=document.getElementById("sideVer");if(f)f.textContent=" · v"+u.version;}).catch(()=>{});})();</script>')
    return f'<div class="app">{side}<div class="content">{top}\n{body}</div></div>\n{js}\n'
def page(title,head_title,body,extra_css=''):
    return (f'<!DOCTYPE html>\n<html lang="pl">\n<head>\n<meta charset="utf-8">\n<meta name="viewport" content="width=device-width,initial-scale=1">\n'
            f'<title>{head_title}</title>\n<style>\n{css}{extra_css}</style>\n</head>\n<body>\n{shell(title,body)}</body>\n</html>\n')
pages={}
for name,title,ht in [('settings','Ustawienia falowników','Fusion – ustawienia'),('sd','Karta microSD','Fusion – karta microSD'),('update','Aktualizacja z GitHuba','Fusion – aktualizacja'),('tuya','Chmura Tuya','Fusion – chmura Tuya')]:
    pages[name]=page(title,ht,open(f'/tmp/page/{name}.body').read())
    open(f'/tmp/page/{name}.html','w').write(pages[name])
hist=page('Mój dom','Fusion – pulpit',open('/tmp/page/history.body').read(),open('/tmp/page/history.css').read())
open('/tmp/page/history.html','w').write(hist)
for h in list(pages.values())+[hist]: assert ')HTMLPAGE"' not in h
open('/tmp/odtu/include/FusionPages.h','w').write('// SPDX-License-Identifier: GPL-2.0-or-later\n// /sd and /fusion/settings pages (generated by narzedzia/gen.py)\n#pragma once\n#include <Arduino.h>\n\n'
 +f'static const char SD_PAGE_HTML[] PROGMEM = R"HTMLPAGE({pages["sd"]})HTMLPAGE";\n\n'
 +f'static const char SETTINGS_PAGE_HTML[] PROGMEM = R"HTMLPAGE({pages["settings"]})HTMLPAGE";\n\n'
 +f'static const char UPDATE_PAGE_HTML[] PROGMEM = R"HTMLPAGE({pages["update"]})HTMLPAGE";\n\n'
 +f'static const char TUYA_PAGE_HTML[] PROGMEM = R"HTMLPAGE({pages["tuya"]})HTMLPAGE";\n')
open('/tmp/odtu/include/HistoryPage.h','w').write('// SPDX-License-Identifier: GPL-2.0-or-later\n// Dashboard served at /history (generated by narzedzia/gen.py)\n#pragma once\n#include <Arduino.h>\n\n'
 +f'static const char HISTORY_PAGE_HTML[] PROGMEM = R"HTMLPAGE({hist})HTMLPAGE";\n')
print("ok",len(pages['settings']),len(pages['sd']),len(hist))
