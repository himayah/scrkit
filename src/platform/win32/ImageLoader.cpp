#include "ImageLoader.h"

// initguid.h forces the WIC CLSID/IID GUID constants to be defined directly
// in this translation unit, instead of relying on an import library
// providing them -- keeps this working the same way on MSVC and MinGW-w64.
#include <initguid.h>
#include <wincodec.h>

#include "../../core/ContentMask.h"
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

namespace {

GLuint UploadRgbaTexture(int width, int height, const uint8_t* rgba, bool nearestFilter = false) {
    if (width <= 0 || height <= 0 || rgba == nullptr) return 0;

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) return 0;
    glBindTexture(GL_TEXTURE_2D, texture);
    const GLint filter = nearestFilter ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

} // namespace

GLuint CreateTextureFromImage(const DecodedImage& image) {
    if (image.width <= 0 || image.height <= 0 ||
        image.rgba.size() < static_cast<size_t>(image.width) * image.height * 4) {
        return 0;
    }
    return UploadRgbaTexture(image.width, image.height, image.rgba.data());
}

GLuint CreateMaskedTextureFromImage(const DecodedImage& image, const std::vector<bool>& mask, int gridN,
                                     const core::BoundaryRefinement* refinement) {
    if (image.width <= 0 || image.height <= 0 || gridN <= 0 ||
        image.rgba.size() < static_cast<size_t>(image.width) * image.height * 4 ||
        mask.size() < static_cast<size_t>(gridN) * gridN) {
        return 0;
    }

    std::vector<uint8_t> masked = image.rgba; // copy: never mutate the caller's capture buffer
    // Cell boundaries must match core::ComputeContentMask's exactly, via the
    // shared core::PixelToGridIndex -- NOT a fixed truncated cellW =
    // width/gridN, which drifts from the mask's actual per-cell boundaries
    // by a growing number of pixels whenever width isn't an exact multiple
    // of gridN (true for essentially every real screen resolution, e.g.
    // 1920/54). That drift is exactly what showed up as wrongly-transparent/
    // wrongly-opaque areas not lining up with real content (user feedback:
    // empty areas getting captured, parts of windows going transparent).
    for (int y = 0; y < image.height; ++y) {
        const int row = core::PixelToGridIndex(y, gridN, image.height);
        for (int x = 0; x < image.width; ++x) {
            const int col = core::PixelToGridIndex(x, gridN, image.width);
            const int cellIndex = row * gridN + col;
            bool contentHere = mask[static_cast<size_t>(cellIndex)];

            // A cell RefineBoundaryMask actually subdivided: trace its finer
            // per-leaf shape instead of the flat whole-cell fill, using the
            // exact same cell-relative PixelToGridIndex trick (now against
            // the cell's own pixel span instead of the whole image) so the
            // leaf boundaries line up precisely, for the same reason the
            // outer loop uses it against the whole image.
            if (refinement) {
                auto it = refinement->cells.find(cellIndex);
                if (it != refinement->cells.end()) {
                    const int cellX0 = (col * image.width) / gridN;
                    const int cellX1 = ((col + 1) * image.width) / gridN;
                    const int cellY0 = (row * image.height) / gridN;
                    const int cellY1 = ((row + 1) * image.height) / gridN;
                    const int leafGrid = refinement->leafGrid;
                    const int lx = core::PixelToGridIndex(x - cellX0, leafGrid, cellX1 - cellX0);
                    const int ly = core::PixelToGridIndex(y - cellY0, leafGrid, cellY1 - cellY0);
                    contentHere = it->second[static_cast<size_t>(ly) * leafGrid + lx];
                }
            }

            if (!contentHere) {
                masked[(static_cast<size_t>(y) * image.width + x) * 4 + 3] = 0;
            }
        }
    }
    // GL_NEAREST, not GL_LINEAR: this texture has a hard alpha cutoff at
    // every cell boundary (content cell alpha=255 directly adjacent to a
    // masked-out cell's alpha=0). Bilinear filtering blends across that
    // boundary, fading real content toward transparent for about a texel on
    // either side of every single cell edge -- on a fragment that's shrunk
    // by an effect (e.g. VortexSuction spiraling into the center), that
    // texel-wide fringe is a much larger fraction of what's left on screen,
    // showing up as real content going see-through (user feedback). v1
    // never had this failure mode since it never baked a hard alpha cutoff
    // into a shared texture -- it simply didn't draw a quad at all for a
    // masked-out cell, sampling the *unmasked* capture for the cells it did
    // draw.
    return UploadRgbaTexture(image.width, image.height, masked.data(), /*nearestFilter=*/true);
}

GLuint CreateTextureFromRgba(int width, int height, const uint8_t* rgba) {
    return UploadRgbaTexture(width, height, rgba);
}

} // namespace platform
