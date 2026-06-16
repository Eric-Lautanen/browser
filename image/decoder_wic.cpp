#include "decoder.hpp"
#include <wincodec.h>
#include <wrl/client.h>
#include <cassert>

namespace browser::image {

using Microsoft::WRL::ComPtr;

static HRESULT ensure_com_init() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == S_FALSE || hr == S_OK) return S_OK;
    if (hr == RPC_E_CHANGED_MODE) {
        // Already initialized in a different concurrency model — that's ok
        return S_OK;
    }
    return hr;
}

static HRESULT init_wic_factory(IWICImagingFactory** factory) {
    ensure_com_init();
    return CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                            IID_PPV_ARGS(factory));
}

class WICDecoder : public Decoder {
public:
    Result<Image> decode(const u8* data, size_t size) override {
        if (size == 0 || !data) {
            return Result<Image>("Empty image data");
        }

        ComPtr<IWICImagingFactory> factory;
        HRESULT hr = init_wic_factory(&factory);
        if (FAILED(hr)) {
            return Result<Image>("Failed to create WIC factory");
        }

        ComPtr<IWICStream> stream;
        hr = factory->CreateStream(&stream);
        if (FAILED(hr)) {
            return Result<Image>("Failed to create WIC stream");
        }

        hr = stream->InitializeFromMemory(const_cast<u8*>(data), static_cast<DWORD>(size));
        if (FAILED(hr)) {
            return Result<Image>("Failed to initialize WIC stream from memory");
        }

        ComPtr<IWICBitmapDecoder> decoder;
        hr = factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                              WICDecodeMetadataCacheOnLoad, &decoder);
        if (FAILED(hr)) {
            return Result<Image>("WIC does not support this format or data is corrupt");
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) {
            return Result<Image>("Failed to get first frame");
        }

        UINT w = 0, h = 0;
        hr = frame->GetSize(&w, &h);
        if (FAILED(hr) || w == 0 || h == 0) {
            return Result<Image>("Invalid image dimensions");
        }

        WICPixelFormatGUID pixel_format;
        hr = frame->GetPixelFormat(&pixel_format);
        if (FAILED(hr)) {
            return Result<Image>("Failed to get pixel format");
        }

        ComPtr<IWICFormatConverter> converter;
        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr)) {
            return Result<Image>("Failed to create format converter");
        }

        hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0f,
                                   WICBitmapPaletteTypeMedianCut);
        if (FAILED(hr)) {
            return Result<Image>("Failed to convert pixel format");
        }

        UINT stride = w * 4;
        UINT buffer_size = stride * h;
        std::vector<u8> pixels(buffer_size);

        hr = converter->CopyPixels(nullptr, stride, buffer_size, pixels.data());
        if (FAILED(hr)) {
            return Result<Image>("Failed to copy pixels");
        }

        ImageFormat fmt = ImageFormat::UNKNOWN;
        GUID container_format;
        if (SUCCEEDED(decoder->GetContainerFormat(&container_format))) {
            if (container_format == GUID_ContainerFormatPng) fmt = ImageFormat::PNG;
            else if (container_format == GUID_ContainerFormatJpeg) fmt = ImageFormat::JPEG;
            else if (container_format == GUID_ContainerFormatGif) fmt = ImageFormat::GIF;
            else if (container_format == GUID_ContainerFormatBmp) fmt = ImageFormat::BMP;
            else if (container_format == GUID_ContainerFormatWebp) fmt = ImageFormat::WEBP;
            else if (container_format == GUID_ContainerFormatTiff) fmt = ImageFormat::TIFF;
            else if (container_format == GUID_ContainerFormatIco) fmt = ImageFormat::ICO;
        }

        Image img;
        img.width = static_cast<u32>(w);
        img.height = static_cast<u32>(h);
        img.format = fmt;
        img.rgba_pixels = std::move(pixels);
        return Result<Image>(std::move(img));
    }
};

std::unique_ptr<Decoder> create_wic_decoder() {
    return std::make_unique<WICDecoder>();
}

} // namespace browser::image
