#include "ImageLoaderWIC.hpp"

#ifdef _WIN32
#define NOMINMAX
#include <combaseapi.h>
#include <wincodec.h>
#include <windows.h>

#include <sstream>

#pragma comment(lib, "windowscodecs.lib")

static std::wstring toWide(const std::string& s)
{
    if (s.empty())
        return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (len <= 0) {
        len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0);
        if (len <= 0)
            return {};
        std::wstring ws((size_t)len, L'\0');
        MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), ws.data(), len);
        return ws;
    }
    std::wstring ws((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), ws.data(), len);
    return ws;
}

bool loadImageRGBA8_WIC(const std::string& path, ImageRGBA8& out, std::string& err)
{
    out = {};

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool didCoInit = SUCCEEDED(hr);

    IWICImagingFactory* factory = nullptr;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) {
        err = "WIC: failed to create imaging factory";
        if (didCoInit)
            CoUninitialize();
        return false;
    }

    IWICBitmapDecoder* decoder = nullptr;
    std::wstring wpath = toWide(path);
    hr = factory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr) || !decoder) {
        err = "WIC: failed to open image: " + path;
        factory->Release();
        if (didCoInit)
            CoUninitialize();
        return false;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        err = "WIC: failed to get frame: " + path;
        decoder->Release();
        factory->Release();
        if (didCoInit)
            CoUninitialize();
        return false;
    }

    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    if (w == 0 || h == 0) {
        err = "WIC: image has zero size: " + path;
        frame->Release();
        decoder->Release();
        factory->Release();
        if (didCoInit)
            CoUninitialize();
        return false;
    }

    IWICFormatConverter* conv = nullptr;
    hr = factory->CreateFormatConverter(&conv);
    if (FAILED(hr) || !conv) {
        err = "WIC: failed to create converter";
        frame->Release();
        decoder->Release();
        factory->Release();
        if (didCoInit)
            CoUninitialize();
        return false;
    }

    hr = conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        err = "WIC: failed to convert to RGBA8: " + path;
        conv->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        if (didCoInit)
            CoUninitialize();
        return false;
    }

    out.width = (uint32_t)w;
    out.height = (uint32_t)h;
    out.pixels.resize((size_t)w * (size_t)h * 4);

    hr = conv->CopyPixels(nullptr, (UINT)w * 4, (UINT)out.pixels.size(), out.pixels.data());
    if (FAILED(hr)) {
        err = "WIC: CopyPixels failed: " + path;
        conv->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        if (didCoInit)
            CoUninitialize();
        return false;
    }

    conv->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    if (didCoInit)
        CoUninitialize();
    return true;
}

#else

bool loadImageRGBA8_WIC(const std::string&, ImageRGBA8&, std::string& err)
{
    err = "WIC image loader only implemented on Windows (_WIN32)";
    return false;
}

#endif
