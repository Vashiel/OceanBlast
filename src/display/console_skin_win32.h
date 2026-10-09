#pragma once
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <vector>
#include "display.h"
#include <string>
namespace oceanblast {
struct ConsoleSkin {
    ULONG_PTR token = 0;
    Gdiplus::Bitmap* image = nullptr;
    Gdiplus::Bitmap* scaled = nullptr;
    int scaledWidth = 0, scaledHeight = 0;
    ConsoleSkin() {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) return;
        wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, 32768);
        std::wstring base(executable); base.resize(base.find_last_of(L"\\/"));
        for (const auto& path : {base + L"/assets/digiblast-skin.png", base + L"/../assets/digiblast-skin.png", base + L"/../../assets/digiblast-skin.png", std::wstring(L"assets/digiblast-skin.png")}) {
            auto* candidate = Gdiplus::Bitmap::FromFile(path.c_str());
            if (candidate && candidate->GetLastStatus() == Gdiplus::Ok) { image = candidate; break; }
            delete candidate;
        }
    }
    ~ConsoleSkin() { delete scaled; delete image; if (token) Gdiplus::GdiplusShutdown(token); }
    struct Control { float x,y,w,h; uint32_t mask; unsigned media; };
    // Normalized against the reconstruction's 1536 x 1024 canvas.
    static const std::vector<Control>& controls() {
        static const std::vector<Control> list = {
            {.145f,.285f,.048f,.066f,BTN_UP,0}, {.145f,.421f,.048f,.066f,BTN_DOWN,0},
            {.104f,.352f,.046f,.062f,BTN_LEFT,0}, {.188f,.352f,.046f,.062f,BTN_RIGHT,0},
            {.807f,.258f,.067f,.094f,BTN_C,0}, {.746f,.358f,.067f,.099f,BTN_B,0},
            {.746f,.487f,.067f,.099f,BTN_A,0},
            {.310f,.757f,.084f,.110f,BTN_REWIND,1}, {.408f,.753f,.083f,.109f,BTN_START,2},
            {.510f,.753f,.083f,.109f,BTN_SELECT,3}, {.610f,.757f,.082f,.110f,BTN_FORWARD,4},
            {.436f,.875f,.034f,.051f,BTN_SELECT,0}, {.528f,.875f,.034f,.051f,BTN_START,0}
        }; return list;
    }
    static RECT lcd(int width, int height) { return {int(width*.293f),int(height*.243f),int(width*.707f),int(height*.664f)}; }
    uint32_t hit(int x,int y,int width,int height) const {
        for(const auto& c:controls()) if(x>=c.x*width&&x<(c.x+c.w)*width&&y>=c.y*height&&y<(c.y+c.h)*height) return c.mask;
        return 0;
    }
    void paint(HDC dc,int width,int height,uint32_t pressed) {
        Gdiplus::Graphics graphics(dc); graphics.Clear(Gdiplus::Color(255,20,28,35));
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        if (image && (width != scaledWidth || height != scaledHeight)) {
            delete scaled; scaled = new Gdiplus::Bitmap(width,height,PixelFormat32bppARGB);
            Gdiplus::Graphics cache(scaled);
            cache.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            cache.Clear(Gdiplus::Color(255,20,28,35)); cache.DrawImage(image,0,0,width,height);
            scaledWidth=width; scaledHeight=height;
        }
        if(scaled) graphics.DrawImage(scaled,0,0,width,height);
        Gdiplus::SolidBrush shade(Gdiplus::Color(110,0,183,240));
        Gdiplus::Pen outline(Gdiplus::Color(230,105,239,255),2);
        if (pressed & 15) {
            const float dx=((pressed&BTN_RIGHT)?1.f:0.f)-((pressed&BTN_LEFT)?1.f:0.f);
            const float dy=((pressed&BTN_DOWN)?1.f:0.f)-((pressed&BTN_UP)?1.f:0.f);
            Gdiplus::SolidBrush knob(Gdiplus::Color(255,136,199,214));
            Gdiplus::Pen edge(Gdiplus::Color(255,36,78,93),2);
            Gdiplus::RectF r(width*.157f+dx*width*.003f,height*.362f+dy*height*.004f,width*.025f,height*.039f);
            graphics.FillEllipse(&knob,r); graphics.DrawEllipse(&edge,r);
        }
        for(const auto& c:controls()) if(pressed&c.mask) {
            Gdiplus::RectF r(c.x*width,c.y*height,c.w*width,c.h*height);
            if(c.media || (c.mask&15)) { graphics.FillRectangle(&shade,r); graphics.DrawRectangle(&outline,r); }
            else { graphics.FillEllipse(&shade,r); graphics.DrawEllipse(&outline,r); }
        }
    }
};
}
#endif
