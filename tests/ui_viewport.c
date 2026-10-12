#include <assert.h>
#include <math.h>
#include "../engine/src/ui/ui_viewport.h"
int main(void) {
 const int modes[][2]={{640,480},{1920,1080},{3440,1440},{1280,1024},{320,480},{640,720},{1720,1440}};
 int i;
 for(i=0;i<sizeof(modes)/sizeof(*modes);i++) {
  float sx,sy,b,x,y,w,h;
  UI_Viewport(modes[i][0],modes[i][1],&sx,&sy,&b);
  assert(sx==sy && sx>0 && b>=0);
  { float s0,t0,s1,t1,aspect=2048.f/856.f;
   UI_ViewportCover(modes[i][0],modes[i][1],aspect,&s0,&t0,&s1,&t1);
   assert(s0>=0 && t0>=0 && s1<=1 && t1<=1);
   assert(s0==0 || t0==0);
   assert(fabsf((s1-s0)*aspect/(t1-t0)-(float)modes[i][0]/modes[i][1])<.001f);
   t0=(84.f+856.f*t0)/1024.f;t1=(84.f+856.f*t1)/1024.f;
   assert(t0>=84.f/1024 && t1<=940.f/1024);
   assert(fabsf((s1-s0)*2048/((t1-t0)*1024)-(float)modes[i][0]/modes[i][1])<.001f);
  }
  x=320-16;y=240-16;w=h=32;
  UI_ViewportRect(&x,&y,&w,&h,sx,b);
  assert(w==h && fabsf(x+w/2-modes[i][0]/2.f)<.01f);
  assert(fabsf(y+h/2+UI_ViewportYBias(modes[i][1],sy,.5f)-modes[i][1]/2.f)<.01f);
  assert(fabsf(480*sy+UI_ViewportYBias(modes[i][1],sy,1)-modes[i][1])<.01f);
  assert(UI_ViewportYBias(modes[i][1],sy,0)==0);
  // Menu cursor: pixel movement inverted with the same uniform scale.
  assert(fabsf((x-b)/sx-(320-16))<.01f);
  x=4;y=4;w=h=12;
  UI_ViewportRect(&x,&y,&w,&h,sx,0);
  assert(x==4*sx && w==h);
  x=640-16;y=4;w=h=12;
  UI_ViewportRect(&x,&y,&w,&h,sx,2*b);
  assert(fabsf((x+w)-(modes[i][0]-4*sx))<.01f);
 }
 { float s0,t0,s1,t1;
  UI_ViewportCover(3840,1080,1940.f/811.f,&s0,&t0,&s1,&t1);
  assert(s0==0 && s1==1 && t0>0 && t1<1);
 }
 { float rem=0; int sum=0,j;
  for(j=0;j<3;j++)sum+=UI_ViewportMouse(1,3,&rem);
  assert(sum==1 && rem==0);
  for(j=0;j<3;j++)sum+=UI_ViewportMouse(-1,3,&rem);
  assert(sum==0 && rem==0);
 }
 return 0;
}
