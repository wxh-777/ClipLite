#include "popup_preview_renderer.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr wchar_t kWindowClass[] = L"ClipLitePopupPreviewCanvas";

} // namespace

PopupPreviewRenderer::~PopupPreviewRenderer() {
    detach();
}

bool PopupPreviewRenderer::attach(HWND parent) {
    if (!parent) return false;
    if (window_) return true;
    static std::once_flag registered;
    std::call_once(registered, [] {
        WNDCLASSW classInfo{};
        classInfo.lpfnWndProc = &PopupPreviewRenderer::windowProc;
        classInfo.hInstance = GetModuleHandleW(nullptr);
        classInfo.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        classInfo.hbrBackground = nullptr;
        classInfo.lpszClassName = kWindowClass;
        RegisterClassW(&classInfo);
    });
    parent_ = parent;
    window_ = CreateWindowExW(0, kWindowClass, L"", WS_CHILD,
                              0, 0, 0, 0, parent, nullptr,
                              GetModuleHandleW(nullptr), this);
    return window_ != nullptr;
}

void PopupPreviewRenderer::detach() {
    clearSource();
    discardDeviceResources();
    if (window_) DestroyWindow(window_);
    window_ = nullptr;
    parent_ = nullptr;
    factory_.Reset();
}

void PopupPreviewRenderer::resize(const RECT& content) {
    if (!window_) return;
    SetWindowPos(window_, nullptr, content.left, content.top,
                 std::max(0L, content.right - content.left),
                 std::max(0L, content.bottom - content.top),
                 SWP_NOACTIVATE | SWP_NOZORDER |
                     (sourceBitmap_ ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    if (renderTarget_) {
        renderTarget_->Resize(D2D1::SizeU(
            static_cast<UINT>(std::max(0L, content.right - content.left)),
            static_cast<UINT>(std::max(0L, content.bottom - content.top))));
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void PopupPreviewRenderer::setSource(HBITMAP bitmap, int bitmapWidth, int bitmapHeight,
                                     int sourceWidth, int sourceHeight,
                                     bool premultipliedAlpha) {
    if (!window_ || !bitmap || bitmapWidth <= 0 || bitmapHeight <= 0 ||
        sourceWidth <= 0 || sourceHeight <= 0) return;
    clearSource();
    sourceBitmap_ = bitmap;
    bitmapWidth_ = bitmapWidth;
    bitmapHeight_ = bitmapHeight;
    sourceWidth_ = sourceWidth;
    sourceHeight_ = sourceHeight;
    premultipliedAlpha_ = premultipliedAlpha;
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    if (!createDeviceResources()) return;
    createSourceResource();
    InvalidateRect(window_, nullptr, FALSE);
}

bool PopupPreviewRenderer::createSourceResource() {
    if (source_ || !renderTarget_ || !sourceBitmap_) return source_ != nullptr;
    DIBSECTION dib{};
    if (GetObjectW(sourceBitmap_, sizeof(dib), &dib) != sizeof(dib) || !dib.dsBm.bmBits) return false;
    const D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(
        D2D1::PixelFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM,
            premultipliedAlpha_ ? D2D1_ALPHA_MODE_PREMULTIPLIED
                                : D2D1_ALPHA_MODE_IGNORE));
    if (renderTarget_->CreateBitmap(D2D1::SizeU(static_cast<UINT>(bitmapWidth_),
                                                static_cast<UINT>(bitmapHeight_)),
                                     dib.dsBm.bmBits,
                                     static_cast<UINT>(dib.dsBm.bmWidthBytes),
                                     &properties, source_.GetAddressOf()) != S_OK) {
        source_.Reset();
        return false;
    }
    return true;
}

void PopupPreviewRenderer::clearSource() {
    source_.Reset();
    sourceBitmap_ = nullptr;
    bitmapWidth_ = 0;
    bitmapHeight_ = 0;
    sourceWidth_ = 0;
    sourceHeight_ = 0;
    premultipliedAlpha_ = false;
    if (window_) ShowWindow(window_, SW_HIDE);
}

void PopupPreviewRenderer::setTransform(float zoom, float panX, float panY) {
    zoom_ = std::clamp(zoom, 0.01f, 100.0f);
    panX_ = panX;
    panY_ = panY;
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void PopupPreviewRenderer::setBackground(COLORREF color) {
    background_ = color;
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

bool PopupPreviewRenderer::createDeviceResources() {
    if (renderTarget_) return true;
    if (!window_) return false;
    if (!factory_ && D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                       factory_.GetAddressOf()) != S_OK) {
        return false;
    }
    RECT client{};
    GetClientRect(window_, &client);
    const D2D1_SIZE_U size = D2D1::SizeU(
        static_cast<UINT>(std::max(1L, client.right - client.left)),
        static_cast<UINT>(std::max(1L, client.bottom - client.top)));
    const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN),
        96.0f, 96.0f);
    return factory_->CreateHwndRenderTarget(
        properties,
        D2D1::HwndRenderTargetProperties(window_, size),
        renderTarget_.GetAddressOf()) == S_OK;
}

void PopupPreviewRenderer::discardDeviceResources() {
    source_.Reset();
    renderTarget_.Reset();
}

void PopupPreviewRenderer::paint(HDC dc) {
    if (!sourceBitmap_) {
        RECT client{};
        GetClientRect(window_, &client);
        HBRUSH brush = CreateSolidBrush(background_);
        FillRect(dc, &client, brush);
        DeleteObject(brush);
        return;
    }
    if (!createDeviceResources()) {
        const RECT client = [&] { RECT value{}; GetClientRect(window_, &value); return value; }();
        HBRUSH brush = CreateSolidBrush(background_);
        FillRect(dc, &client, brush);
        DeleteObject(brush);
        if (sourceBitmap_ && bitmapWidth_ > 0 && bitmapHeight_ > 0) {
            HDC source = CreateCompatibleDC(dc);
            HGDIOBJ oldSource = source ? SelectObject(source, sourceBitmap_) : nullptr;
            const bool integerZoom = zoom_ > 1.01f &&
                std::abs(zoom_ - std::round(zoom_)) < 0.001f;
            SetStretchBltMode(dc, integerZoom ? COLORONCOLOR : HALFTONE);
            if (source) {
                const float scaleX = zoom_ * sourceWidth_ / bitmapWidth_;
                const float scaleY = zoom_ * sourceHeight_ / bitmapHeight_;
                const int width = std::max(1, static_cast<int>(std::lround(bitmapWidth_ * scaleX)));
                const int height = std::max(1, static_cast<int>(std::lround(bitmapHeight_ * scaleY)));
                const int left = static_cast<int>(std::lround(client.right / 2.0f + panX_ - width / 2.0f));
                const int top = static_cast<int>(std::lround(client.bottom / 2.0f + panY_ - height / 2.0f));
                StretchBlt(dc, left, top, width, height, source, 0, 0,
                           bitmapWidth_, bitmapHeight_, SRCCOPY);
                SelectObject(source, oldSource);
                DeleteDC(source);
            }
        }
        return;
    }
    if (!source_ && !createSourceResource()) return;
    renderTarget_->BeginDraw();
    renderTarget_->Clear(D2D1::ColorF(
        GetRValue(background_) / 255.0f,
        GetGValue(background_) / 255.0f,
        GetBValue(background_) / 255.0f));
    if (source_ && sourceWidth_ > 0 && sourceHeight_ > 0) {
        const D2D1_SIZE_F target = renderTarget_->GetSize();
        const float scaleX = zoom_ * sourceWidth_ / bitmapWidth_;
        const float scaleY = zoom_ * sourceHeight_ / bitmapHeight_;
        const float width = bitmapWidth_ * scaleX;
        const float height = bitmapHeight_ * scaleY;
        const float left = target.width / 2.0f + panX_ - width / 2.0f;
        const float top = target.height / 2.0f + panY_ - height / 2.0f;
        const D2D1_RECT_F destination = D2D1::RectF(
            left, top, left + width, top + height);
        const bool integerZoom = zoom_ > 1.01f &&
            std::abs(zoom_ - std::round(zoom_)) < 0.001f;
        renderTarget_->DrawBitmap(
            source_.Get(), destination, 1.0f,
            integerZoom ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                        : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    const HRESULT result = renderTarget_->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) {
        discardDeviceResources();
        InvalidateRect(window_, nullptr, FALSE);
    }
}

LRESULT CALLBACK PopupPreviewRenderer::windowProc(HWND hwnd, UINT message,
                                                   WPARAM wParam, LPARAM lParam) {
    auto* renderer = reinterpret_cast<PopupPreviewRenderer*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        renderer = static_cast<PopupPreviewRenderer*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(renderer));
    }
    return renderer ? renderer->handleMessage(hwnd, message, wParam, lParam)
                    : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT PopupPreviewRenderer::handleMessage(HWND hwnd, UINT message,
                                             WPARAM wParam, LPARAM lParam) {
    if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
        message == WM_LBUTTONDBLCLK || message == WM_RBUTTONDOWN || message == WM_RBUTTONUP ||
        message == WM_MBUTTONDOWN || message == WM_MBUTTONUP) {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ClientToScreen(hwnd, &point);
        ScreenToClient(parent_, &point);
        SendMessageW(parent_, message, wParam, MAKELPARAM(point.x, point.y));
        return 0;
    }
    if (message == WM_MOUSEWHEEL) {
        SendMessageW(parent_, message, wParam, lParam);
        return 0;
    }
    if (message == WM_MOUSELEAVE) {
        SendMessageW(parent_, message, wParam, lParam);
        return 0;
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT paintInfo{};
        HDC dc = BeginPaint(hwnd, &paintInfo);
        paint(dc);
        EndPaint(hwnd, &paintInfo);
        return 0;
    }
    if (message == WM_DPICHANGED) {
        // The preview transform uses Win32 client pixels for mouse anchoring
        // and panning. Keep D2D at 96 DPI so one input pixel remains one
        // render coordinate instead of applying the monitor scale twice.
        return 0;
    }
    if (message == WM_SIZE && renderTarget_) {
        renderTarget_->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
        return 0;
    }
    if (message == WM_NCDESTROY) {
        discardDeviceResources();
        window_ = nullptr;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
