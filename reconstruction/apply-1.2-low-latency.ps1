param(
    [switch]$SkipCompatibilityImport
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$compatCommit = 'ac8ad00ca19fd786755ffce06436873085241cbd'
$compatRepo = 'https://github.com/loqwe/MHY_Scanner2.git'

function Invoke-Git {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Args)
    & git @Args
    if ($LASTEXITCODE -ne 0) { throw "git failed: git $($Args -join ' ')" }
}

function Set-Utf8File {
    param([string]$Path, [string]$Content)
    [System.IO.File]::WriteAllText((Join-Path $root $Path), $Content, [System.Text.UTF8Encoding]::new($true))
}

function Replace-Required {
    param([string]$Path, [string]$Old, [string]$New)
    $full = Join-Path $root $Path
    $text = [System.IO.File]::ReadAllText($full)
    if (-not $text.Contains($Old)) { throw "required anchor not found in $Path" }
    $text = $text.Replace($Old, $New)
    [System.IO.File]::WriteAllText($full, $text, [System.Text.UTF8Encoding]::new($true))
}

Push-Location $root
try {
    if (-not $SkipCompatibilityImport -and -not (Test-Path 'src/Core/UrlQuery.hpp')) {
        Invoke-Git fetch --no-tags --depth=1 $compatRepo $compatCommit
        Invoke-Git cherry-pick FETCH_HEAD
    }

    # Identify this rebuild as our maintainable 1.2 line.
    $cmakePath = Join-Path $root 'CMakeLists.txt'
    $cmake = [System.IO.File]::ReadAllText($cmakePath)
    $cmake = [regex]::Replace($cmake, 'VERSION\s+1\.16\.1', 'VERSION 1.2.1', 1)

    if (-not $cmake.Contains('# Low-latency ZXing')) {
        $zxingBlock = @'

# Low-latency ZXing: match the 1.2 binary's lightweight QR path.
set(ZXING_READERS ON CACHE BOOL "" FORCE)
set(ZXING_WRITERS OFF CACHE STRING "" FORCE)
set(ZXING_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(ZXING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZXING_ENABLE_1D OFF CACHE BOOL "" FORCE)
set(ZXING_ENABLE_AZTEC OFF CACHE BOOL "" FORCE)
set(ZXING_ENABLE_DATAMATRIX OFF CACHE BOOL "" FORCE)
set(ZXING_ENABLE_MAXICODE OFF CACHE BOOL "" FORCE)
set(ZXING_ENABLE_PDF417 OFF CACHE BOOL "" FORCE)
set(ZXING_ENABLE_QRCODE ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    zxing_cpp
    GIT_REPOSITORY https://github.com/zxing-cpp/zxing-cpp.git
    GIT_TAG v3.1.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(zxing_cpp)
'@
        $anchor = 'FetchContent_MakeAvailable(nlohmann_json)'
        if (-not $cmake.Contains($anchor)) { throw 'nlohmann FetchContent anchor not found' }
        $cmake = $cmake.Replace($anchor, $anchor + $zxingBlock)
    }
    if (-not $cmake.Contains("`tZXing::ZXing")) {
        $cmake = $cmake.Replace("`tcpr::cpr`r`n", "`tcpr::cpr`r`n`tZXing::ZXing`r`n")
        $cmake = $cmake.Replace("`tcpr::cpr`n", "`tcpr::cpr`n`tZXing::ZXing`n")
    }
    [System.IO.File]::WriteAllText($cmakePath, $cmake, [System.Text.UTF8Encoding]::new($true))

    Set-Utf8File 'src/Core/QRScanner.h' @'
#pragma once

#include <string>
#include <opencv2/core.hpp>

class QRScanner
{
public:
    QRScanner() = default;
    ~QRScanner() = default;
    void decodeSingle(const cv::Mat& img, std::string& qrCode);
    void decodeMultiple(const cv::Mat& img, std::string& qrCode);
};
'@

    Set-Utf8File 'src/Core/QRScanner.cpp' @'
#include "QRScanner.h"

#include "ZXing/ZXingCpp.h"

namespace
{
ZXing::ImageView ToImageView(const cv::Mat& img)
{
    if (img.empty() || img.depth() != CV_8U)
        return {};

    ZXing::ImageFormat format = ZXing::ImageFormat::None;
    if (img.channels() == 1)
        format = ZXing::ImageFormat::Lum;
    else if (img.channels() == 3)
        format = ZXing::ImageFormat::BGR;
    else if (img.channels() == 4)
        format = ZXing::ImageFormat::BGRA;

    if (format == ZXing::ImageFormat::None)
        return {};

    return ZXing::ImageView(img.data, img.cols, img.rows, format, static_cast<int>(img.step));
}

ZXing::ReaderOptions FastQrOptions()
{
    // Livestream/login QR codes are upright, normal-polarity QR codes. Avoid work that
    // cannot help this workload and stop after the first symbol.
    return ZXing::ReaderOptions()
        .formats(ZXing::BarcodeFormat::QRCode)
        .tryHarder(false)
        .tryRotate(false)
        .tryInvert(false)
        .tryDownscale(true)
        .maxNumberOfSymbols(1);
}
}

void QRScanner::decodeSingle(const cv::Mat& img, std::string& qrCode)
{
    const auto view = ToImageView(img);
    if (view.format() == ZXing::ImageFormat::None)
        return;

    const auto result = ZXing::ReadBarcode(view, FastQrOptions());
    if (result.isValid())
        qrCode = result.text();
}

void QRScanner::decodeMultiple(const cv::Mat& img, std::string& qrCode)
{
    const auto view = ToImageView(img);
    if (view.format() == ZXing::ImageFormat::None)
        return;

    auto options = FastQrOptions();
    options.maxNumberOfSymbols(8);
    const auto results = ZXing::ReadBarcodes(view, options);
    for (const auto& result : results)
        if (result.isValid())
            qrCode = result.text();
}
'@

    # 1.2 deliberately uses a low quality Bilibili stream to reduce CDN/decode latency.
    Replace-Required 'src/Core/LiveStreamLink.cpp' '{ "qn", "10000" }' '{ "qn", "80" }'

    # Match the low-buffer FFmpeg behavior seen in 1.2.
    $streamPath = Join-Path $root 'src/UI/QRCodeForStream.cpp'
    $stream = [System.IO.File]::ReadAllText($streamPath)
    if (-not $stream.Contains('"analyzeduration", "0"')) {
        $anchor = '    av_dict_set(&pAvdictionary, "buffer_size", "1000", 0);'
        if (-not $stream.Contains($anchor)) { throw 'FFmpeg dictionary anchor not found' }
        $extra = @'
    av_dict_set(&pAvdictionary, "analyzeduration", "0", 0);
    av_dict_set(&pAvdictionary, "fflags", "nobuffer", 0);
    av_dict_set(&pAvdictionary, "flags", "low_delay", 0);
    av_dict_set(&pAvdictionary, "reorder_queue_size", "0", 0);
'@
        $stream = $stream.Replace($anchor, $anchor + "`r`n" + $extra.TrimEnd())
    }
    if (-not $stream.Contains('WarmupQrConnections();')) {
        $anchor = '    ret = ScanRet::UNKNOW;'
        if (-not $stream.Contains($anchor)) { throw 'run() warmup anchor not found' }
        $stream = $stream.Replace($anchor, $anchor + "`r`n    WarmupQrConnections();")
    }
    [System.IO.File]::WriteAllText($streamPath, $stream, [System.Text.UTF8Encoding]::new($true))

    # Reuse one CPR/libcurl handle for the QR hot path so DNS/TCP/TLS connections stay hot.
    $apiPath = Join-Path $root 'src/Core/MhyApi.hpp'
    $api = [System.IO.File]::ReadAllText($apiPath)
    if (-not $api.Contains('#include <mutex>')) {
        $api = $api.Replace('#include <fstream>', "#include <fstream>`r`n#include <mutex>")
    }

    if (-not $api.Contains('class QrHttpClient')) {
        $anchor = 'static GameType loginType{ GameType::HonkaiStarRail };'
        if (-not $api.Contains($anchor)) { throw 'MhyApi loginType anchor not found' }
        $helper = @'

class QrHttpClient
{
public:
    cpr::Response Post(const cpr::Url& url, const cpr::Body& body, const cpr::Header& header)
    {
        std::scoped_lock lock(mutex_);
        session_.SetUrl(url);
        session_.SetHeader(header);
        session_.SetBody(body);
        session_.SetTimeout(cpr::Timeout{ 3000 });
        return session_.Post();
    }

    void Warmup(const std::string_view url)
    {
        std::scoped_lock lock(mutex_);
        session_.SetUrl(cpr::Url{ std::string(url) });
        session_.SetHeader(cpr::Header{ { "Connection", "keep-alive" } });
        session_.SetTimeout(cpr::Timeout{ 1000 });
        session_.RemoveContent();
        (void)session_.Get();
    }

private:
    std::mutex mutex_;
    cpr::Session session_;
};

inline QrHttpClient& GetQrHttpClient()
{
    static QrHttpClient client;
    return client;
}

inline cpr::Response QrPost(const cpr::Url& url, const cpr::Body& body, const cpr::Header& header)
{
    return GetQrHttpClient().Post(url, body, header);
}

inline void WarmupQrConnections()
{
    auto& client = GetQrHttpClient();
    client.Warmup("https://api-sdk.mihoyo.com");
    client.Warmup("https://passport-api.mihoyo.com");
}
'@
        $api = $api.Replace($anchor, $anchor + $helper)
    }

    # Keep logging available without reopening the file on every scan/confirm request.
    $oldLog = @'
inline void WriteScannerLog(const std::string_view message)
{
    std::ofstream file{ "Config/scanner.log", std::ios::app };
    if (file)
    {
        file << message << '\n';
    }
}
'@
    $newLog = @'
inline void WriteScannerLog(const std::string_view message)
{
    static std::mutex logMutex;
    static std::ofstream file{ "Config/scanner.log", std::ios::app };
    std::scoped_lock lock(logMutex);
    if (file)
        file << message << '\n';
}
'@
    if ($api.Contains($oldLog)) { $api = $api.Replace($oldLog, $newLog) }

    $oldPanda = @'
    const auto response = cpr::Post(
        cpr::Url{ std::string(url) },
        cpr::Body{ body.dump() },
        cpr::Header{
            { "Content-Type", "application/json" },
            { "x-rpc-app_id", "bll8iq97cem8" },
            { "x-rpc-device_id", device_id } });
'@
    $newPanda = @'
    const auto response = QrPost(
        cpr::Url{ std::string(url) },
        cpr::Body{ body.dump() },
        cpr::Header{
            { "Content-Type", "application/json" },
            { "x-rpc-app_id", "bll8iq97cem8" },
            { "x-rpc-device_id", device_id } });
'@
    if (-not $api.Contains('const auto response = QrPost(')) {
        if (-not $api.Contains($oldPanda)) { throw 'PandaScanQRCode request anchor not found' }
        $api = $api.Replace($oldPanda, $newPanda)
    }

    $oldPassport = @'
    const auto response = cpr::Post(
        cpr::Url{ url },
        cpr::Body{ body.dump() },
        cpr::Header{
            { "Content-Type", "application/json" },
            { "x-rpc-app_id", "bll8iq97cem8" },
            { "x-rpc-device_id", device_id },
            { "Cookie", cookie } });
'@
    $newPassport = @'
    const auto response = QrPost(
        cpr::Url{ url },
        cpr::Body{ body.dump() },
        cpr::Header{
            { "Content-Type", "application/json" },
            { "x-rpc-app_id", "bll8iq97cem8" },
            { "x-rpc-device_id", device_id },
            { "Cookie", cookie } });
'@
    if ($api.Contains($oldPassport)) { $api = $api.Replace($oldPassport, $newPassport) }

    [System.IO.File]::WriteAllText($apiPath, $api, [System.Text.UTF8Encoding]::new($true))

    Write-Host '1.2 reconstruction + low-latency patch applied.'
    Write-Host 'Key changes: modern QR protocol, ZXing, qn=80, FFmpeg no-buffer, persistent/prewarmed QR HTTP session.'
}
finally {
    Pop-Location
}
