// 瞬截 SwiftSnip - Windows 轻量截图工具
// Copyright (C) 2026 Lin1848624
// SPDX-License-Identifier: GPL-3.0-or-later

#include "png_writer.h"

#include <wincodec.h>

namespace {

std::wstring HResultMessage(HRESULT hr) {
    wchar_t buffer[64] = {};
    swprintf_s(buffer, L"错误码 0x%08X", static_cast<unsigned int>(hr));
    return buffer;
}

template <typename T>
void SafeRelease(T** pointer) {
    if (*pointer != nullptr) {
        (*pointer)->Release();
        *pointer = nullptr;
    }
}

}  // namespace

bool SaveBitmapAsPng(HBITMAP bitmap, const std::wstring& path, std::wstring* errorOut) {
    if (bitmap == nullptr || path.empty()) {
        if (errorOut != nullptr) {
            *errorOut = L"参数无效";
        }
        return false;
    }

    HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(comResult);

    IWICImagingFactory* factory = nullptr;
    IWICBitmap* wicBitmap = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* properties = nullptr;

    bool ok = false;
    HRESULT hr = S_OK;

    do {
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            break;
        }
        hr = factory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapIgnoreAlpha, &wicBitmap);
        if (FAILED(hr)) {
            break;
        }

        UINT width = 0;
        UINT height = 0;
        hr = wicBitmap->GetSize(&width, &height);
        if (FAILED(hr)) {
            break;
        }

        hr = factory->CreateStream(&stream);
        if (FAILED(hr)) {
            break;
        }
        hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
        if (FAILED(hr)) {
            break;
        }

        hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
        if (FAILED(hr)) {
            break;
        }
        hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
        if (FAILED(hr)) {
            break;
        }

        hr = encoder->CreateNewFrame(&frame, &properties);
        if (FAILED(hr)) {
            break;
        }
        hr = frame->Initialize(properties);
        if (FAILED(hr)) {
            break;
        }
        hr = frame->SetSize(width, height);
        if (FAILED(hr)) {
            break;
        }

        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        hr = frame->SetPixelFormat(&format);
        if (FAILED(hr)) {
            break;
        }
        hr = frame->WriteSource(wicBitmap, nullptr);
        if (FAILED(hr)) {
            break;
        }
        hr = frame->Commit();
        if (FAILED(hr)) {
            break;
        }
        hr = encoder->Commit();
        if (FAILED(hr)) {
            break;
        }
        ok = true;
    } while (false);

    SafeRelease(&properties);
    SafeRelease(&frame);
    SafeRelease(&encoder);
    SafeRelease(&stream);
    SafeRelease(&wicBitmap);
    SafeRelease(&factory);

    if (shouldUninitialize) {
        CoUninitialize();
    }

    if (!ok && errorOut != nullptr) {
        *errorOut = HResultMessage(hr);
    }
    return ok;
}

