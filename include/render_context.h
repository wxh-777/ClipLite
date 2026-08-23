#ifndef CLIPLITE_RENDER_CONTEXT_H
#define CLIPLITE_RENDER_CONTEXT_H

#include <d2d1.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <wincodec.h>
#include <windows.h>

#include <cstdint>
#include <string>

#include <wrl/client.h>

class RenderContext {
public:
    RenderContext() = default;
    ~RenderContext();

    RenderContext(const RenderContext&) = delete;
    RenderContext& operator=(const RenderContext&) = delete;

    static bool initializeShared();
    static void shutdownShared();

    bool attach(HWND hwnd, UINT dpi = 96);
    void detach();
    bool resize(UINT width, UINT height, UINT dpi = 0);
    bool beginDraw();
    bool endDraw();
    void clear(D2D1_COLOR_F color);
    void fillRect(const D2D1_RECT_F& rect, D2D1_COLOR_F color);
    void strokeRect(const D2D1_RECT_F& rect, D2D1_COLOR_F color, float width);
    void fillEllipse(const D2D1_ELLIPSE& ellipse, D2D1_COLOR_F color);
    void strokeEllipse(const D2D1_ELLIPSE& ellipse, D2D1_COLOR_F color, float width);
    bool drawDib(const std::string& payload, const RECT& destination);
    void fillRoundedRect(const D2D1_ROUNDED_RECT& rect, D2D1_COLOR_F color);
    void strokeRoundedRect(const D2D1_ROUNDED_RECT& rect, D2D1_COLOR_F color, float width);
    void drawLine(D2D1_POINT_2F first, D2D1_POINT_2F second, D2D1_COLOR_F color,
                  float width = 1.0f);
    bool drawText(const std::wstring& text, const RECT& rect, float fontSize,
                  D2D1_COLOR_F color, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
                  DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING,
                  DWRITE_PARAGRAPH_ALIGNMENT paragraph = DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    ID2D1RenderTarget* target() const { return target_.Get(); }
    IDWriteFactory* writeFactory() const { return writeFactory_.Get(); }
    IWICImagingFactory* wicFactory() const { return wicFactory_.Get(); }
    UINT dpi() const { return dpi_; }
    bool usingSoftwareFallback() const { return softwareFallback_; }
    bool attached() const { return target_ != nullptr; }

private:
    bool createTarget(UINT width, UINT height);
    bool createBrush(D2D1_COLOR_F color);
    bool handleDeviceLost(HRESULT result);
    static bool initializeWic();
    bool createTargetBitmap();

    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    bool drawing_ = false;
    bool softwareFallback_ = false;
    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> target_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> targetBitmap_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;

    static Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
    static Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory_;
    static Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory_;
};

#endif
