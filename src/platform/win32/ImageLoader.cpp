#include "ImageLoader.h"

// initguid.h forces the WIC CLSID/IID GUID constants to be defined directly
// in this translation unit, instead of relying on an import library
// providing them -- keeps this working the same way on MSVC and MinGW-w64.
#include <initguid.h>
#include <wincodec.h>

#include "../../core/Logger.h"

namespace platform {

namespace {

// Minimal RAII COM smart pointer. <wrl/client.h> (Microsoft::WRL::ComPtr) is
// not reliably available on older MinGW-w64 toolchains, so this project
// rolls its own tiny equivalent to keep both MSVC and MinGW-w64 builds
// working (要件.txt: フリーのコンパイラでビルド可能にする).
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { Reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    // Address-of operator matches the usual COM out-parameter idiom:
    // hr = Factory(..., &comPtr);
    T** operator&() {
        Reset();
        return &ptr_;
    }

    T* operator->() const { return ptr_; }
    T* Get() const { return ptr_; }

    void Reset() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

private:
    T* ptr_ = nullptr;
};

// RAII helper: initializes COM for the duration of one decode call. Multiple
// nested CoInitializeEx calls on the same thread are reference-counted by
// COM, so this is safe to call even if the caller already initialized COM.
class ScopedCom {
public:
    ScopedCom() { hr_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
    ~ScopedCom() {
        if (SUCCEEDED(hr_)) {
            CoUninitialize();
        }
    }
    bool Ok() const { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }

private:
    HRESULT hr_ = E_FAIL;
};

} // namespace

bool DecodeImageFile(const std::wstring& path, DecodedImage& out) {
    if (path.empty()) {
        return false;
    }

    ScopedCom com;
    if (!com.Ok()) {
        core::Logger::Error("ImageLoader: COM init failed");
        return false;
    }

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IWICImagingFactory, reinterpret_cast<void**>(&factory));
    if (FAILED(hr)) {
        core::Logger::Error("ImageLoader: could not create WIC factory");
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                             WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) {
        core::Logger::Warn("ImageLoader: failed to open image file");
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        core::Logger::Warn("ImageLoader: failed to read image frame");
        return false;
    }

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) {
        return false;
    }

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        core::Logger::Warn("ImageLoader: format conversion failed");
        return false;
    }

    UINT width = 0, height = 0;
    hr = converter->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0) {
        return false;
    }

    out.width = static_cast<int>(width);
    out.height = static_cast<int>(height);
    out.rgba.assign(static_cast<size_t>(width) * height * 4, 0);

    const UINT stride = width * 4;
    hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(out.rgba.size()), out.rgba.data());
    if (FAILED(hr)) {
        core::Logger::Warn("ImageLoader: CopyPixels failed");
        return false;
    }

    return true;
}

DecodedImage MakeFallbackImage(uint8_t r, uint8_t g, uint8_t b) {
    DecodedImage image;
    image.width = 2;
    image.height = 2;
    image.rgba.resize(2 * 2 * 4);
    for (int i = 0; i < 4; ++i) {
        image.rgba[i * 4 + 0] = r;
        image.rgba[i * 4 + 1] = g;
        image.rgba[i * 4 + 2] = b;
        image.rgba[i * 4 + 3] = 255;
    }
    return image;
}

GLuint CreateTextureFromImage(const DecodedImage& image) {
    if (image.width <= 0 || image.height <= 0 ||
        image.rgba.size() < static_cast<size_t>(image.width) * image.height * 4) {
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, image.rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

} // namespace platform
