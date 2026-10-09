#include "display/display.h"
#include <windows.h>
#include <vector>
#include <iostream>
#include <chrono>
#include <thread>
int main(){
 SetProcessDPIAware();
 oceanblast::Display display(1); if(!display.init("LCD height validation")) return 1;
 std::vector<unsigned char> ram(32*1024*1024);
 // Red upper 160 rows, green bottom 80: cropping would omit all green.
 for(unsigned y=0;y<240;++y) for(unsigned x=0;x<240;++x){unsigned v=y<160?0xf800:0x07e0;ram[y*480+x*2]=v;ram[y*480+x*2+1]=v>>8;}
 display.updateFrame(ram.data(),0x30000000,true,480,240);
 BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=240;info.bmiHeader.biHeight=-160;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
 void* pixels=nullptr;HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);auto old=SelectObject(dc,bitmap);
 display.renderToDc(dc);GdiFlush();auto p=static_cast<unsigned*>(pixels);
 bool pass=(p[0]&0xffffff)==0xff0000&&(p[159*240]&0xffffff)==0x00ff00;
 std::cout<<(pass?"PASS":"FAIL")<<" Windows presentation includes the bottom source rows\n";
 bool gpuPixels=true;
 if(display.usesSyncedPresentation()) {
  HWND window=FindWindowA("OceanBlastDisplayClass","LCD height validation");
  SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
  for(unsigned attempt=0;display.presentedFrames()==0&&attempt<100;++attempt){display.processEvents();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  POINT origin{0,0};ClientToScreen(window,&origin);HDC screen=GetDC(nullptr);
  BitBlt(dc,0,0,240,160,screen,origin.x,origin.y,SRCCOPY);GdiFlush();ReleaseDC(nullptr,screen);
  std::cout<<"GPU samples: "<<std::hex<<p[20*240+120]<<","<<p[150*240+120]<<std::dec<<" origin="<<origin.x<<","<<origin.y<<"\n";
  gpuPixels=(p[20*240+120]&0xffffff)==0xff0000&&(p[150*240+120]&0xffffff)==0x00ff00;
  std::cout<<(gpuPixels?"PASS":"FAIL")<<" DXGI output includes red upper and green lower source rows\n";
  SetWindowPos(window,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
 }
 // Returning to 160 lines must restore the full red image.
 const uint64_t beforeResize = display.presentedFrames();
 display.updateFrame(ram.data(),0x30000000,true,480,160);display.renderToDc(dc);GdiFlush();
 bool restored=(p[159*240]&0xffffff)==0xff0000;std::cout<<(restored?"PASS":"FAIL")<<" Windows presentation restores game height\n";
 bool gpuResize=true;
 if(display.usesSyncedPresentation()) {
  HWND window=FindWindowA("OceanBlastDisplayClass","LCD height validation");
  SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
  for(unsigned attempt=0;display.presentedFrames()<=beforeResize&&attempt<100;++attempt){display.processEvents();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  POINT origin{0,0};ClientToScreen(window,&origin);HDC screen=GetDC(nullptr);
  BitBlt(dc,0,0,240,160,screen,origin.x,origin.y,SRCCOPY);GdiFlush();ReleaseDC(nullptr,screen);
  gpuResize=(p[150*240+120]&0xffffff)==0xff0000;
  std::cout<<(gpuResize?"PASS":"FAIL")<<" DXGI texture height returns to the full game image\n";
  SetWindowPos(window,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
 }
 bool synced = display.usesSyncedPresentation();
 for(unsigned attempt=0; synced && display.presentedFrames()==0 && attempt<100; ++attempt) std::this_thread::sleep_for(std::chrono::milliseconds(10));
 bool gpu = !synced || display.presentedFrames()>0;
 std::cout<<(gpu?"PASS":"FAIL")<<" Presenter submits a synchronized frame or reports GDI fallback\n";
 SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);display.close();return pass&&restored&&gpu&&gpuPixels&&gpuResize?0:1;
}
