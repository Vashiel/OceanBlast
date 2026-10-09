#include "display/display.h"
#include <windows.h>
#include <vector>
#include <iostream>
int main(){
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
 // Returning to 160 lines must restore the full red image.
 display.updateFrame(ram.data(),0x30000000,true,480,160);display.renderToDc(dc);GdiFlush();
 bool restored=(p[159*240]&0xffffff)==0xff0000;std::cout<<(restored?"PASS":"FAIL")<<" Windows presentation restores game height\n";
 SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);display.close();return pass&&restored?0:1;
}
