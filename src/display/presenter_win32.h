#pragma once
#ifdef _WIN32
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <atomic>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace oceanblast {
class SyncedPresenter {
public:
    ~SyncedPresenter() {
        { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
        wake_.notify_one();
        if (worker_.joinable()) {
            // Present can synchronously message the window thread. Keep sent
            // messages flowing until the worker exits, then join without blocking it.
            while (!workerDone_) {
                MSG message;
                PeekMessage(&message, nullptr, 0, 0, PM_NOREMOVE);
                Sleep(1);
            }
            worker_.join();
        }
        release(view_); release(texture_); release(sampler_); release(pixel_); release(vertex_);
        release(target_); release(chain_); release(context_); release(device_);
    }
    bool init(HWND window, unsigned width, unsigned height) {
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = width; desc.BufferDesc.Height = height;
        desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2; desc.OutputWindow = window; desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        D3D_FEATURE_LEVEL level;
        HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &desc, &chain_, &device_, &level, &context_);
        if (FAILED(result)) return false;
        ID3D11Texture2D* back = nullptr;
        if (FAILED(chain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) return false;
        result = device_->CreateRenderTargetView(back, nullptr, &target_); back->Release();
        if (FAILED(result)) return false;
        const char* code =
            "struct V { float4 p:SV_POSITION; float2 uv:TEXCOORD0; };"
            "V vs(uint id:SV_VertexID) { V o; float2 uv=float2((id<<1)&2,id&2); o.uv=uv; o.p=float4(uv*float2(2,-2)+float2(-1,1),0,1); return o; }"
            "Texture2D image:register(t0); SamplerState pointSample:register(s0);"
            "float4 ps(V v):SV_TARGET { return float4(image.Sample(pointSample,v.uv).rgb,1); }";
        ID3DBlob *vs = nullptr, *ps = nullptr, *errors = nullptr;
        result = D3DCompile(code, std::strlen(code), nullptr, nullptr, nullptr, "vs", "vs_4_0", 0, 0, &vs, &errors);
        release(errors); if (FAILED(result)) return false;
        result = D3DCompile(code, std::strlen(code), nullptr, nullptr, nullptr, "ps", "ps_4_0", 0, 0, &ps, &errors);
        release(errors); if (FAILED(result)) { release(vs); return false; }
        result = device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex_);
        if (SUCCEEDED(result)) result = device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixel_);
        release(vs); release(ps); if (FAILED(result)) return false;
        D3D11_SAMPLER_DESC sample{}; sample.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sample.AddressU = sample.AddressV = sample.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sample.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device_->CreateSamplerState(&sample, &sampler_))) return false;
        viewport_.Width = float(width); viewport_.Height = float(height); viewport_.MaxDepth = 1;
        healthy_ = true;
        workerDone_ = false;
        worker_ = std::thread([this] { run(); workerDone_ = true; });
        return true;
    }
    bool healthy() const { return healthy_.load(); }
    uint64_t presented() const { return presented_.load(); }
    void submit(const std::vector<uint32_t>& image, unsigned height) {
        { std::lock_guard<std::mutex> lock(mutex_); pending_ = image; pendingHeight_ = height; dirty_ = true; }
        wake_.notify_one();
    }
private:
    template<class T> static void release(T*& object) { if (object) object->Release(); object = nullptr; }
    bool setTexture(unsigned height) {
        if (height == textureHeight_) return true;
        release(view_); release(texture_);
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = 240; desc.Height = height;
        desc.MipLevels = desc.ArraySize = 1; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device_->CreateTexture2D(&desc, nullptr, &texture_))) return false;
        if (FAILED(device_->CreateShaderResourceView(texture_, nullptr, &view_))) return false;
        textureHeight_ = height; return true;
    }
    void run() {
        std::vector<uint32_t> pixels;
        for (;;) {
            unsigned height;
            { std::unique_lock<std::mutex> lock(mutex_); wake_.wait(lock, [this] { return stopping_ || dirty_; });
              if (stopping_) return;
              pixels.swap(pending_); height = pendingHeight_; dirty_ = false; }
            if (!setTexture(height)) { healthy_ = false; return; }
            context_->UpdateSubresource(texture_, 0, nullptr, pixels.data(), 240 * 4, 0);
            context_->OMSetRenderTargets(1, &target_, nullptr); context_->RSSetViewports(1, &viewport_);
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context_->VSSetShader(vertex_, nullptr, 0); context_->PSSetShader(pixel_, nullptr, 0);
            context_->PSSetShaderResources(0, 1, &view_); context_->PSSetSamplers(0, 1, &sampler_);
            context_->Draw(3, 0);
            const HRESULT result = chain_->Present(1, 0); // Vertical synchronization, no tearing flag.
            if (FAILED(result)) { healthy_ = false; return; }
            ++presented_;
        }
    }
    ID3D11Device* device_ = nullptr; ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* chain_ = nullptr; ID3D11RenderTargetView* target_ = nullptr;
    ID3D11VertexShader* vertex_ = nullptr; ID3D11PixelShader* pixel_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr; ID3D11Texture2D* texture_ = nullptr;
    ID3D11ShaderResourceView* view_ = nullptr; D3D11_VIEWPORT viewport_{};
    unsigned textureHeight_ = 0, pendingHeight_ = 0;
    std::vector<uint32_t> pending_;
    std::mutex mutex_; std::condition_variable wake_; std::thread worker_;
    bool stopping_ = false, dirty_ = false;
    std::atomic<bool> healthy_{false}, workerDone_{true}; std::atomic<uint64_t> presented_{0};
};
}
#endif
