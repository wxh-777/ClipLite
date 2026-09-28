#ifndef CLIPLITE_POPUP_PREVIEW_RENDERER_H
#define CLIPLITE_POPUP_PREVIEW_RENDERER_H

#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <wrl/client.h>

#include <mutex>

using Microsoft::WRL::ComPtr;

class PopupPreviewRenderer {
public:
    PopupPreviewRenderer() = default;
    ~PopupPreviewRenderer();

    PopupPreviewRenderer(const PopupPreviewRenderer&) = delete;
    PopupPreviewRenderer& operator=(const PopupPreviewRenderer&) = delete;

    bool attach(HWND parent);
    void detach();
    void resize(const RECT& content);
    void setSource(HBITMAP bitmap, int bitmapWidth, int bitmapHeight,
                   int sourceWidth, int sourceHeight, bool premultipliedAlpha);
    void clearSource();
    void setTransform(float zoom, float panX, float panY);
    HWND window() const { return window_; }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    bool createDeviceResources();
    bool createSourceResource();
    void discardDeviceResources();
    void paint(HDC dc);

    HWND parent_ = nullptr;
    HWND window_ = nullptr;
    ComPtr<ID2D1Factory> factory_;
    ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    ComPtr<ID2D1Bitmap> source_;
    HBITMAP sourceBitmap_ = nullptr;
    int bitmapWidth_ = 0;
    int bitmapHeight_ = 0;
    int sourceWidth_ = 0;
    int sourceHeight_ = 0;
    bool premultipliedAlpha_ = false;
    float zoom_ = 1.0f;
    float panX_ = 0.0f;
    float panY_ = 0.0f;
};

#endif
