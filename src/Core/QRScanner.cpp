#include "QRScanner.h"

#include <stdexcept>

#include <BarcodeFormat.h>
#include <ImageView.h>
#include <ReadBarcode.h>
#include <ReaderOptions.h>

namespace
{
ZXing::ReaderOptions FastQrOptions()
{
    ZXing::ReaderOptions options;
    options.setFormats(ZXing::BarcodeFormat::QRCode);
    options.setTryHarder(false);
    options.setTryRotate(false);
    options.setTryInvert(false);
    options.setTryDownscale(true);
    options.setMaxNumberOfSymbols(1);
    return options;
}

ZXing::ImageView MakeImageView(const cv::Mat& img)
{
    switch (img.channels())
    {
    case 1:
        return ZXing::ImageView(img.data, img.cols, img.rows, ZXing::ImageFormat::Lum,
                                static_cast<int>(img.step));
    case 3:
        return ZXing::ImageView(img.data, img.cols, img.rows, ZXing::ImageFormat::BGR,
                                static_cast<int>(img.step));
    case 4:
        return ZXing::ImageView(img.data, img.cols, img.rows, ZXing::ImageFormat::BGRA,
                                static_cast<int>(img.step));
    default:
        throw std::invalid_argument("Unsupported cv::Mat channel count for QR decoding");
    }
}
} // namespace

void QRScanner::decodeSingle(const cv::Mat& img, std::string& qrCode)
{
    qrCode.clear();
    if (img.empty())
        return;

    const auto imageView = MakeImageView(img);
    const auto result = ZXing::ReadBarcode(imageView, FastQrOptions());
    if (result.isValid())
        qrCode = result.text();
}

void QRScanner::decodeMultiple(const cv::Mat& img, std::string& qrCode)
{
    qrCode.clear();
    if (img.empty())
        return;

    ZXing::ReaderOptions options = FastQrOptions();
    options.setMaxNumberOfSymbols(8);
    const auto results = ZXing::ReadBarcodes(MakeImageView(img), options);
    for (const auto& result : results)
    {
        if (result.isValid())
        {
            qrCode = result.text();
            return;
        }
    }
}
