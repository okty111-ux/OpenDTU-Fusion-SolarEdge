#include "FusionDisplayPages.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdlib.h>
static void curve(FdData& d, bool night){
  srand(3); double raw[288]; double sr=0;
  for(int i=0;i<288;i++){double t=i/12.0,b=0; if(t>=7.2&&t<=16.3){b=pow(fmax(0,sin(M_PI*(t-7.5)/8.5)),1.4);} double cl=1-0.55*pow(fmax(0,sin(i/7.0+3)),6)*(rand()/(double)RAND_MAX); raw[i]=b*cl; sr+=raw[i]/12;}
  int slot= night? 271 : 161; d.curveSlot=slot;
  for(int i=0;i<288;i++){ if(i>slot){d.curveHoy[i]=d.curveSe[i]=FD_CURVE_NONE;continue;}
    d.curveHoy[i]=(uint16_t)(raw[i]*6842/sr); d.curveSe[i]=(uint16_t)(raw[i]*15917/sr);}
}
static void fill(FdData& d, bool night){
  memset(&d,0,sizeof d);
  strcpy(d.clock, night?"22:41":"13:27"); strcpy(d.date,"12.11.2026"); d.timeValid=true; curve(d,night);
  d.hoyReachable=!night; d.seConfigured=true; d.seValid=true;
  d.hoyW= night?0:2148; d.seW= night?0:1323;
  d.todayHoy=7.12f; d.todaySe=5.31f;
  d.meterValid=true; d.gridW= night?-412:1186; d.todayExp=4.87f; d.todayImp=3.02f;
  d.mpptCount=4;
  const char* nA[4]={"Longi410","JA 450","Longi410","Trina425"}; const char* nB[4]={"JA 450","JA 450","Longi410","Trina425"};
  float U[4]={71.4f,74.2f,68.9f,70.3f}, I[4]={7.95f,8.12f,7.40f,7.66f};
  for(int k=0;k<4;k++){FdMppt&m=d.mppt[k];m.inv=1;m.ch=k+1;m.reachable=!night;m.u=night?0:U[k];m.i=night?0:I[k];m.p=m.u*m.i;m.yd=1700+k*120;
    m.panelCount=2; strcpy(m.panel[0].name,nA[k]); strcpy(m.panel[1].name,nB[k]);
    m.panel[0].wp=k==0?410:(k==1?450:(k==2?410:425)); m.panel[1].wp=k==0?450:m.panel[0].wp;
    float s=m.panel[0].wp+m.panel[1].wp; m.panel[0].p=m.p*m.panel[0].wp/s; m.panel[1].p=m.p*m.panel[1].wp/s;}
  strcpy(d.seStatus, night?"uśpiony":"produkuje"); d.seTemp=night?18:41; d.seTotalKWh=18432; d.seDcV=night?0:381; strcpy(d.seModel,"SE5K-RW0TEBEN4");
  d.hasYesterday=true; d.yHoy=9.4f; d.ySe=6.8f; d.mHoy=88.2f; d.mSe=61.5f; d.yrHoy=3120; d.yrSe=4210; d.ltHoy=3480; d.ltSe=18432;
  strcpy(d.ip,"192.168.1.40"); strcpy(d.net,"Wi-Fi -61 dBm"); strcpy(d.sd,"29,7 GB · wolne 29,5 GB"); d.invOnline=night?0:1; d.invTotal=1; d.uptimeS=3*86400+4*3600+12*60;
}
static uint8_t cb(u8x8_t*,uint8_t,uint8_t,void*){return 1;}
static int px(u8g2_t*u,int x,int y){uint8_t*b=u8g2_GetBufferPtr(u);return (b[(y>>3)*256+x]>>(y&7))&1;}
int main(int argc,char**argv){
  u8g2_t u; u8g2_Setup_ssd1322_nhd_256x64_f(&u,&u8g2_cb_r0,cb,cb);
  for(int night=0;night<2;night++){
    FdData d; fill(d,night);
    int cnt=0; for(int p=0;p<FD_PAGE_COUNT;p++) if(fdPageAvailable(d,p)) cnt++;
    int no=0;
    for(int p=0;p<=FD_PAGE_COUNT;p++){
      bool ov = p==FD_PAGE_COUNT; int pg = ov?0:p;
      if(!ov && !fdPageAvailable(d,pg)) continue;
      if(!ov) no++;
      if(ov && night) continue;
      u8g2_ClearBuffer(&u); fdRenderPage(&u,d,pg,ov?1:no,cnt,!ov);
      if(ov) fdRenderOverlay(&u,"Automatyczne przełączanie","wyłączone · krótko = następna");
      char fn[64]; snprintf(fn,64,"/tmp/fdprev/%s_%d%s.pbm",night?"noc":"dzien",pg,ov?"_overlay":"");
      FILE*f=fopen(fn,"w"); fprintf(f,"P1\n256 64\n");
      for(int y=0;y<64;y++){for(int x=0;x<256;x++) fprintf(f,"%d ",px(&u,x,y)); fprintf(f,"\n");}
      fclose(f);
    }
  }
}
