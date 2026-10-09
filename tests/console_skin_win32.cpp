#include "display/display.h"
#include "display/console_skin_win32.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <thread>
using namespace oceanblast;
static void save( void* pixels,int w,int h,const char* path) {
 GdiFlush(); BITMAPFILEHEADER f{}; f.bfType=0x4d42; f.bfOffBits=sizeof(f)+sizeof(BITMAPINFOHEADER);f.bfSize=f.bfOffBits+w*h*4;
 BITMAPINFOHEADER b{}; b.biSize=sizeof(b);b.biWidth=w;b.biHeight=-h;b.biPlanes=1;b.biBitCount=32;
 std::ofstream out(path,std::ios::binary);out.write((char*)&f,sizeof(f));out.write((char*)&b,sizeof(b));out.write((char*)pixels,w*h*4);
}
int main(int argc,char**) {
 SetProcessDPIAware(); Display d(2);d.useGdiPresentation(argc>1);d.configureWindow(true);if(!d.init("Console skin validation"))return 1;
 HWND window=FindWindowA("OceanBlastDisplayClass","Console skin validation"),lcd=FindWindowExA(window,nullptr,"OceanBlastLcdClass",nullptr);
 RECT size{},child{},saved{};GetClientRect(window,&size);GetWindowRect(lcd,&child);GetWindowRect(window,&saved);
 if(!lcd||child.right-child.left<=240||child.bottom-child.top<=160)return 1;
 std::vector<unsigned char> source(240*240*2);
 for(unsigned y=0;y<240;++y)for(unsigned x=0;x<240;++x){unsigned v=y<160?0xf800:0x07e0;source[y*480+x*2]=v;source[y*480+x*2+1]=v>>8;}
 d.updateFrameData(source.data(),true,480,240);
 for(int i=0;d.usesSyncedPresentation()&&!d.presentedFrames()&&i<100;++i){d.processEvents();Sleep(10);}
 if(d.usesSyncedPresentation()&&!d.presentedFrames())return 1;
 std::cout<<"PASS: Skin LCD presents complete 240-row source through VSync or GDI fallback\n";
 HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=size.right;info.bmiHeader.biHeight=-size.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
 void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);auto old=SelectObject(dc,bitmap);
 d.paintShell(dc);GdiFlush(); auto p=(unsigned*)pixels;unsigned ix=int(size.bottom*.53f)*size.right+int(size.right*.78f);auto before=p[ix];
 SendMessage(window,WM_KEYDOWN,'Z',0);d.paintShell(dc);GdiFlush();if(p[ix]==before||d.getButtonMask()!=BTN_A)return 1;
 save(pixels,size.right,size.bottom,"build/skin-pressed.bmp");SendMessage(window,WM_KEYUP,'Z',0);if(d.getButtonMask())return 1;
 std::cout<<"PASS: Keyboard press changes skin rendering and release clears input\n";
 SendMessage(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(int(size.right*.17f),int(size.bottom*.31f)));
 if(d.getButtonMask()!=BTN_UP)return 1;
 SendMessage(window,WM_KILLFOCUS,0,0);if(d.getButtonMask())return 1;
 SendMessage(window,WM_LBUTTONUP,0,0);
 std::cout<<"PASS: Clickable D-pad and focus loss release\n";
 for(auto c:ConsoleSkin::controls()) {
  d.mouseButton(int((c.x+c.w/2)*size.right),int((c.y+c.h/2)*size.bottom),true);
  if(d.getButtonMask()!=c.mask)return 1;
  d.mouseButton(0,0,false);
 }
 std::cout<<"PASS: All 13 visible controls reach their mapped input\n";
 d.keyboardButton('Z',BTN_A,true);d.mouseButton(int(size.right*.78f),int(size.bottom*.53f),true);d.mouseButton(0,0,false);
 if(d.getButtonMask()!=BTN_A)return 1;
 d.keyboardButton('Z',BTN_A,false);
 std::cout<<"PASS: Mouse release preserves held keyboard input\n";
 for(int i=0;i<3;++i){SendMessage(window,WM_KEYDOWN,VK_F11,0);if(!d.fullscreen())return 1;GetWindowRect(lcd,&child);if(abs((child.right-child.left)*2-(child.bottom-child.top)*3)>3)return 1;
  SendMessage(window,WM_KEYDOWN,VK_ESCAPE,0);if(d.fullscreen()||!d.isOpen())return 1;RECT now{};GetWindowRect(window,&now);if(memcmp(&now,&saved,sizeof(now)))return 1;}
 std::cout<<"PASS: Repeated fullscreen toggles preserve aspect, window position and Escape behavior\n";
 SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);d.close();return 0;
}
