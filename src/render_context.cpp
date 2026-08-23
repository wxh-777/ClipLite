#include "render_context.h"

#include <algorithm>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

ComPtr<ID2D1Factory1> RenderContext::d2dFactory_;
ComPtr<IDWriteFactory> RenderContext::writeFactory_;
ComPtr<IWICImagingFactory> RenderContext::wicFactory_;

namespace {

float dpiScale(UINT dpi) {
    return static_cast<float>(dpi == 0 ? 96 : dpi) / 96.0f;
}

D2D1::ColorF makeOpaqueColor(D2D1_COLOR_F color) {
    color.a = std::clamp(color.a, 0.0f, 1.0f);
    return D2D1::ColorF(color.r, color.g, color.b, color.a);
}

} // namespace

RenderContext::~RenderContext() {
    detach();
}

bool RenderContext::initializeShared() {
    if (d2dFactory_ && writeFactory_) return true;

    if (!d2dFactory_ && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,
                                                 d2dFactory_.GetAddressOf()))) {
        return false;
    }
    if (!writeFactory_ && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                                      __uuidof(IDWriteFactory),
                                                      reinterpret_cast<IUnknown**>(
                                                          writeFactory_.GetAddressOf())))) {
        d2dFactory_.Reset();
        return false;
    }
    return true;
}

bool RenderContext::initializeWic() {
    if (wicFactory_) return true;
    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(wicFactory_.GetAddressOf()));
    if (FAILED(result)) {
        result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(wicFactory_.GetAddressOf()));
    }
    return SUCCEEDED(result);
}

void RenderContext::shutdownShared() {
    wicFactory_.Reset();
    writeFactory_.Reset();
    d2dFactory_.Reset();
}

bool RenderContext::attach(HWND hwnd, UINT dpi) {
    if (!hwnd || !initializeShared()) return false;
    if (hwnd_ == hwnd && target_) {
        return resize(0, 0, dpi);
    }
    detach();
    hwnd_ = hwnd;
    dpi_ = dpi == 0 ? 96 : dpi;
    RECT client{};
    GetClientRect(hwnd_, &client);
    return createTarget(static_cast<UINT>(std::max(1L, client.right - client.left)),
                        static_cast<UINT>(std::max(1L, client.bottom - client.top)));
}

void RenderContext::detach() {
    drawing_ = false;
    brush_.Reset();
    if (target_) target_->SetTarget(nullptr);
    targetBitmap_.Reset();
    target_.Reset();
    d2dDevice_.Reset();
    swapChain_.Reset();
    dxgiDevice_.Reset();
    d3dDevice_.Reset();
    hwnd_ = nullptr;
    softwareFallback_ = false;
}

bool RenderContext::resize(UINT width, UINT height, UINT dpi) {
    if (dpi != 0) dpi_ = dpi;
    if (!target_) {
        if (!hwnd_) return false;
        RECT client{};
        GetClientRect(hwnd_, &client);
        width = static_cast<UINT>(std::max(1L, client.right - client.left));
        height = static_cast<UINT>(std::max(1L, client.bottom - client.top));
        return createTarget(width, height);
    }
    if (width == 0 || height == 0) {
        RECT client{};
        GetClientRect(hwnd_, &client);
        width = static_cast<UINT>(std::max(1L, client.right - client.left));
        height = static_cast<UINT>(std::max(1L, client.bottom - client.top));
    }
    brush_.Reset();
    target_->SetTarget(nullptr);
    targetBitmap_.Reset();
    HRESULT result = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(result)) return handleDeviceLost(result);
    return createTargetBitmap();
}

bool RenderContext::createTarget(UINT width, UINT height) {
    if (!d2dFactory_ || !hwnd_) return false;
    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1
    };
    D3D_FEATURE_LEVEL selectedLevel{};
    HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                        D3D11_CREATE_DEVICE_BGRA_SUPPORT, featureLevels,
                                        ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
                                        d3dDevice_.GetAddressOf(), &selectedLevel, nullptr);
    softwareFallback_ = false;
    if (FAILED(result)) {
        result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                    D3D11_CREATE_DEVICE_BGRA_SUPPORT, featureLevels,
                                    ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
                                    d3dDevice_.GetAddressOf(), &selectedLevel, nullptr);
        softwareFallback_ = SUCCEEDED(result);
    }
    if (FAILED(result) || FAILED(d3dDevice_.As(&dxgiDevice_))) return false;

    result = d2dFactory_->CreateDevice(dxgiDevice_.Get(), d2dDevice_.GetAddressOf());
    if (FAILED(result) || FAILED(d2dDevice_->CreateDeviceContext(
            D2D1_DEVICE_CONTEXT_OPTIONS_NONE, target_.GetAddressOf()))) return false;

    ComPtr<IDXGIFactory2> factory;
    result = CreateDXGIFactory2(0, IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(result)) return false;
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = width;
    description.Height = height;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    result = factory->CreateSwapChainForHwnd(d3dDevice_.Get(), hwnd_, &description, nullptr,
                                             nullptr, swapChain_.GetAddressOf());
    if (FAILED(result)) return false;
    factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    return createTargetBitmap();
}

bool RenderContext::createTargetBitmap() {
    if (!target_ || !swapChain_) return false;
    ComPtr<IDXGISurface> surface;
    HRESULT result = swapChain_->GetBuffer(0, IID_PPV_ARGS(surface.GetAddressOf()));
    if (FAILED(result)) return false;
    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    result = target_->CreateBitmapFromDxgiSurface(surface.Get(), &properties,
                                                   targetBitmap_.GetAddressOf());
    if (FAILED(result)) return false;
    target_->SetTarget(targetBitmap_.Get());
    target_->SetDpi(96.0f, 96.0f);
    return true;
}

bool RenderContext::beginDraw() {
    if (!target_ || drawing_) return false;
    target_->BeginDraw();
    drawing_ = true;
    return true;
}

bool RenderContext::endDraw() {
    if (!target_ || !drawing_) return false;
    drawing_ = false;
    HRESULT result = target_->EndDraw();
    if (FAILED(result)) return handleDeviceLost(result);
    result = swapChain_->Present(1, 0);
    if (FAILED(result)) return handleDeviceLost(result);
    return true;
}

bool RenderContext::handleDeviceLost(HRESULT result) {
    if (result != D2DERR_RECREATE_TARGET) {
        return SUCCEEDED(result);
    }
    brush_.Reset();
    if (target_) target_->SetTarget(nullptr);
    targetBitmap_.Reset();
    target_.Reset();
    d2dDevice_.Reset();
    swapChain_.Reset();
    dxgiDevice_.Reset();
    d3dDevice_.Reset();
    if (!hwnd_) return false;
    RECT client{};
    GetClientRect(hwnd_, &client);
    return createTarget(static_cast<UINT>(std::max(1L, client.right - client.left)),
                        static_cast<UINT>(std::max(1L, client.bottom - client.top)));
}

bool RenderContext::createBrush(D2D1_COLOR_F color) {
    if (!target_) return false;
    return SUCCEEDED(target_->CreateSolidColorBrush(makeOpaqueColor(color), brush_.GetAddressOf()));
}

void RenderContext::clear(D2D1_COLOR_F color) {
    if (target_ && drawing_) target_->Clear(makeOpaqueColor(color));
}

void RenderContext::fillRect(const D2D1_RECT_F& rect, D2D1_COLOR_F color) {
    if (!target_ || !drawing_ || !createBrush(color)) return;
    target_->FillRectangle(rect, brush_.Get());
}

void RenderContext::strokeRect(const D2D1_RECT_F& rect, D2D1_COLOR_F color, float width) {
    if (!target_ || !drawing_ || !createBrush(color)) return;
    target_->DrawRectangle(rect, brush_.Get(), width);
}

void RenderContext::fillEllipse(const D2D1_ELLIPSE& ellipse, D2D1_COLOR_F color) {
    if (!target_ || !drawing_ || !createBrush(color)) return;
    target_->FillEllipse(ellipse, brush_.Get());
}

void RenderContext::strokeEllipse(const D2D1_ELLIPSE& ellipse, D2D1_COLOR_F color, float width) {
    if (!target_ || !drawing_ || !createBrush(color)) return;
    target_->DrawEllipse(ellipse, brush_.Get(), width);
}

bool RenderContext::drawDib(const std::string& payload, const RECT& destination) {
    if (!target_ || !drawing_ || payload.size() < sizeof(BITMAPINFOHEADER) || !initializeWic()) {
        return false;
    }
    BITMAPINFOHEADER header{};
    std::memcpy(&header, payload.data(), sizeof(header));
    if (header.biSize < sizeof(BITMAPINFOHEADER) || header.biSize > payload.size() ||
        header.biWidth <= 0 || header.biHeight == 0 ||
        (header.biBitCount != 24 && header.biBitCount != 32) ||
        (header.biCompression != BI_RGB && header.biCompression != BI_BITFIELDS) ||
        header.biWidth > 10000 ||
        header.biHeight < -10000 || header.biHeight > 10000) {
        return false;
    }
    const std::size_t width = static_cast<std::size_t>(header.biWidth);
    const std::size_t height = static_cast<std::size_t>(std::abs(header.biHeight));
    if (header.biBitCount == 24 && header.biCompression != BI_RGB) return false;
    const std::size_t sourcePixelBytes = header.biBitCount / 8;
    const std::size_t sourceStride = (width * sourcePixelBytes + 3u) & ~std::size_t{3};
    const std::size_t destinationStride = width * 4;
    const std::size_t bitsOffset = header.biSize +
        ((header.biCompression == BI_BITFIELDS && header.biSize == sizeof(BITMAPINFOHEADER))
             ? 3u * sizeof(std::uint32_t) : 0u);
    if (bitsOffset > payload.size() || height > (payload.size() - bitsOffset) / sourceStride) {
        return false;
    }

    // WIC receives a bounded, top-down BGRA copy and owns the decoded bitmap.
    std::vector<std::uint8_t> pixels(destinationStride * height);
    const auto* source = reinterpret_cast<const std::uint8_t*>(payload.data() + bitsOffset);
    for (std::size_t row = 0; row < height; ++row) {
        const std::size_t sourceRow = header.biHeight > 0 ? height - row - 1 : row;
        const auto* sourcePixels = source + sourceRow * sourceStride;
        auto* destinationPixels = pixels.data() + row * destinationStride;
        if (header.biBitCount == 32) {
            std::memcpy(destinationPixels, sourcePixels, width * 4);
        } else {
            for (std::size_t column = 0; column < width; ++column) {
                destinationPixels[column * 4] = sourcePixels[column * 3];
                destinationPixels[column * 4 + 1] = sourcePixels[column * 3 + 1];
                destinationPixels[column * 4 + 2] = sourcePixels[column * 3 + 2];
            }
        }
        for (std::size_t column = 0; column < width; ++column) {
            destinationPixels[column * 4 + 3] = 255;
        }
    }

    Microsoft::WRL::ComPtr<IWICBitmap> sourceBitmap;
    HRESULT result = wicFactory_->CreateBitmapFromMemory(
        static_cast<UINT>(width), static_cast<UINT>(height), GUID_WICPixelFormat32bppBGRA,
        static_cast<UINT>(destinationStride), static_cast<UINT>(pixels.size()), pixels.data(),
        sourceBitmap.GetAddressOf());
    if (FAILED(result)) return false;

    const UINT targetWidth = static_cast<UINT>(std::max(1L, destination.right - destination.left));
    const UINT targetHeight = static_cast<UINT>(std::max(1L, destination.bottom - destination.top));
    Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
    result = wicFactory_->CreateBitmapScaler(scaler.GetAddressOf());
    if (FAILED(result) || FAILED(scaler->Initialize(sourceBitmap.Get(), targetWidth, targetHeight,
                                                     WICBitmapInterpolationModeFant))) {
        return false;
    }
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    result = wicFactory_->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(result) || FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
                                                        WICBitmapDitherTypeNone, nullptr, 0.0,
                                                        WICBitmapPaletteTypeMedianCut))) {
        return false;
    }
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    result = target_->CreateBitmapFromWicBitmap(converter.Get(), bitmap.GetAddressOf());
    if (FAILED(result)) return false;
    target_->DrawBitmap(bitmap.Get(), D2D1::RectF(static_cast<float>(destination.left),
                                                 static_cast<float>(destination.top),
                                                 static_cast<float>(destination.right),
                                                 static_cast<float>(destination.bottom)), 1.0f,
                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    return true;
}

void RenderContext::fillRoundedRect(const D2D1_ROUNDED_RECT& rect, D2D1_COLOR_F color) {
    if (!target_ || !drawing_ || !createBrush(color)) return;
    target_->FillRoundedRectangle(rect, brush_.Get());
}

void RenderContext::strokeRoundedRect(const D2D1_ROUNDED_RECT& rect, D2D1_COLOR_F color,
                                      float width) {
    if (!target_ || !drawing_ || !createBrush(color)) return;
    target_->DrawRoundedRectangle(rect, brush_.Get(), width);
}

void RenderContext::drawLine(D2D1_POINT_2F first, D2D1_POINT_2F second,
                             D2D1_COLOR_F color, float width) {
    if (!target_ || !drawing_ || !createBrush(color)) return;
    target_->DrawLine(first, second, brush_.Get(), width);
}

bool RenderContext::drawText(const std::wstring& text, const RECT& rect, float fontSize,
                             D2D1_COLOR_F color, DWRITE_FONT_WEIGHT weight,
                             DWRITE_TEXT_ALIGNMENT alignment,
                             DWRITE_PARAGRAPH_ALIGNMENT paragraph) {
    if (!target_ || !drawing_ || !writeFactory_ || text.empty()) return false;
    ComPtr<IDWriteTextFormat> format;
    const float scaledSize = fontSize * dpiScale(dpi_);
    HRESULT result = writeFactory_->CreateTextFormat(L"Segoe UI", nullptr, weight,
                                                     DWRITE_FONT_STYLE_NORMAL,
                                                     DWRITE_FONT_STRETCH_NORMAL, scaledSize,
                                                     L"", format.GetAddressOf());
    if (FAILED(result)) return false;
    format->SetTextAlignment(alignment);
    format->SetParagraphAlignment(paragraph);
    if (FAILED(result = createBrush(color))) return false;
    const D2D1_RECT_F layout = D2D1::RectF(static_cast<float>(rect.left),
                                            static_cast<float>(rect.top),
                                            static_cast<float>(rect.right),
                                            static_cast<float>(rect.bottom));
    target_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format.Get(), layout,
                       brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT,
                       DWRITE_MEASURING_MODE_NATURAL);
    return true;
}
