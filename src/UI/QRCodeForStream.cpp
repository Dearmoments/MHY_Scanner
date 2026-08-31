#include "QRCodeForStream.h"

#include <iostream>
#include <string>
#include <string_view>

#include "LowLatencyHttp.hpp"
#include "MhyApi.hpp"
#include "QRScanner.h"

QRCodeForStream::QRCodeForStream(QObject* parent) :
    QThread(parent),
    pAvdictionary(nullptr),
    pAVFormatContext(nullptr),
    pSwsContext(nullptr),
    pAVFrame(nullptr),
    pAVPacket(nullptr),
    pAVCodecContext(nullptr),
    m_stop(false),
    servertype(ServerType::Official)
{
    av_log_set_level(AV_LOG_FATAL);
    m_config = &(ConfigDate::getInstance());
}

QRCodeForStream::~QRCodeForStream()
{
    stop();
    requestInterruption();
    wait();
    threadPool.waitForDone();
}

void QRCodeForStream::setLoginInfo(const std::string_view uid, const std::string_view gameToken)
{
    this->uid = uid;
    this->gameToken = gameToken;
}

void QRCodeForStream::setLoginInfo(const std::string_view uid, const std::string_view gameToken, const std::string& name)
{
    this->uid = uid;
    this->gameToken = gameToken;
    this->m_name = name;
}

void QRCodeForStream::setServerType(const ServerType servertype)
{
    this->servertype = servertype;
}

void QRCodeForStream::submitLatestFrame(cv::Mat&& img)
{
    bool startWorker = false;
    {
        std::scoped_lock lock(frameMutex);
        latestFrame = std::move(img); // overwrite any stale pending frame
        if (!qrWorkerRunning.load(std::memory_order_relaxed))
        {
            qrWorkerRunning.store(true, std::memory_order_release);
            startWorker = true;
        }
    }

    if (startWorker)
    {
        threadPool.start([this]() { processLatestFrames(); });
    }
}

void QRCodeForStream::processLatestFrames()
{
    while (m_stop.load(std::memory_order_acquire))
    {
        cv::Mat img;
        {
            std::scoped_lock lock(frameMutex);
            if (latestFrame.empty())
            {
                qrWorkerRunning.store(false, std::memory_order_release);
                return;
            }
            img = std::move(latestFrame);
            latestFrame = {};
        }

        switch (servertype)
        {
            using enum ServerType;
        case Official:
            processOfficialFrame(img);
            break;
        case BH3_BiliBili:
            processBiliFrame(img);
            break;
        default:
            break;
        }
    }

    std::scoped_lock lock(frameMutex);
    latestFrame.release();
    qrWorkerRunning.store(false, std::memory_order_release);
}

void QRCodeForStream::processOfficialFrame(const cv::Mat& img)
{
    thread_local QRScanner qrScanner;
    std::string str;
    qrScanner.decodeSingle(img, str);
    if (str.size() < 85)
        return;

    const std::string_view view(str.data() + 79, 3);
    if (!setGameType.contains(view))
        return;

    setGameType[view]();
    const std::string ticket = str.substr(str.size() - 24, 24);
    if (lastTicket == ticket)
        return;

    // Mark an attempted code before the network call. If this ticket is stale,
    // keep watching the newest frames instead of stopping the entire scanner.
    lastTicket = ticket;

    std::scoped_lock lock(loginMutex);
    if (!m_stop.load(std::memory_order_acquire))
        return;

    if (!lowlatency::FastScanQRLogin(scanUrl, ticket, gameType, device_id))
        return;

    const nlohmann::json config = nlohmann::json::parse(m_config->getConfig(), nullptr, false);
    if (!config.is_discarded() && config.value("auto_login", false))
    {
        continueLastLogin();
    }
    else
    {
        Q_EMIT loginConfirm(gameType, false);
    }
    stop();
}

void QRCodeForStream::processBiliFrame(const cv::Mat& img)
{
    thread_local QRScanner qrScanner;
    std::string str;
    qrScanner.decodeSingle(img, str);
    if (str.size() < 85)
        return;

    if (const std::string_view view(str.data() + 79, 3); view != "8F3")
        return;

    const std::string ticket = str.substr(str.size() - 24, 24);
    if (lastTicket == ticket)
        return;

    lastTicket = ticket;
    std::scoped_lock lock(loginMutex);
    if (!m_stop.load(std::memory_order_acquire))
        return;

    ret = scanCheck(ticket);
    if (ret != ScanRet::SUCCESS)
        return;

    const nlohmann::json config = nlohmann::json::parse(m_config->getConfig(), nullptr, false);
    if (!config.is_discarded() && config.value("auto_login", false))
    {
        continueLastLogin();
    }
    else
    {
        Q_EMIT loginConfirm(GameType::Honkai3_BiliBili, false);
    }
    stop();
}

void QRCodeForStream::LoginOfficial()
{
    while (m_stop.load(std::memory_order_acquire))
    {
        if (av_read_frame(pAVFormatContext, pAVPacket) < 0)
        {
            ret = ScanRet::LIVESTOP;
            break;
        }

        if (pAVPacket->stream_index != videoStreamIndex)
        {
            av_packet_unref(pAVPacket);
            continue;
        }

        const int sendRet = avcodec_send_packet(pAVCodecContext, pAVPacket);
        av_packet_unref(pAVPacket);
        if (sendRet < 0)
            continue;

        while (m_stop.load(std::memory_order_acquire) && avcodec_receive_frame(pAVCodecContext, pAVFrame) == 0)
        {
            cv::Mat img(videoStreamHeight, videoStreamWidth, CV_8UC1);
            uint8_t* dstData[1] = { img.data };
            const int dstLinesize[1] = { static_cast<int>(img.step) };
            sws_scale(pSwsContext, pAVFrame->data, pAVFrame->linesize, 0, pAVFrame->height,
                      dstData, dstLinesize);
#ifndef SHOW
            cv::imshow("Video_Stream", img);
            cv::waitKey(1);
#endif
            submitLatestFrame(std::move(img));
            av_frame_unref(pAVFrame);
        }
    }
}

void QRCodeForStream::LoginBH3BiliBili()
{
    while (m_stop.load(std::memory_order_acquire))
    {
        if (av_read_frame(pAVFormatContext, pAVPacket) < 0)
        {
            ret = ScanRet::LIVESTOP;
            break;
        }

        if (pAVPacket->stream_index != videoStreamIndex)
        {
            av_packet_unref(pAVPacket);
            continue;
        }

        const int sendRet = avcodec_send_packet(pAVCodecContext, pAVPacket);
        av_packet_unref(pAVPacket);
        if (sendRet < 0)
            continue;

        while (m_stop.load(std::memory_order_acquire) && avcodec_receive_frame(pAVCodecContext, pAVFrame) == 0)
        {
            cv::Mat img(videoStreamHeight, videoStreamWidth, CV_8UC1);
            uint8_t* dstData[1] = { img.data };
            const int dstLinesize[1] = { static_cast<int>(img.step) };
            sws_scale(pSwsContext, pAVFrame->data, pAVFrame->linesize, 0, pAVFrame->height,
                      dstData, dstLinesize);
#ifndef SHOW
            cv::imshow("Video_Stream", img);
            cv::waitKey(1);
#endif
            submitLatestFrame(std::move(img));
            av_frame_unref(pAVFrame);
        }
    }
}

void QRCodeForStream::setStreamHW()
{
    if (pAVCodecContext->width < pAVCodecContext->height ||
        pAVCodecContext->height == 480 ||
        pAVCodecContext->height == 720)
    {
        videoStreamWidth = pAVCodecContext->width;
        videoStreamHeight = pAVCodecContext->height;
    }
    else
    {
        videoStreamWidth = static_cast<int>(pAVCodecContext->width / 1.5);
        videoStreamHeight = static_cast<int>(pAVCodecContext->height / 1.5);
    }
}

void QRCodeForStream::stop()
{
    m_stop.store(false, std::memory_order_release);
    std::scoped_lock lock(frameMutex);
    latestFrame.release();
}

void QRCodeForStream::setUrl(const std::string& url, const std::map<std::string, std::string> heard)
{
    streamUrl = url;
    for (const auto& it : heard)
        av_dict_set(&pAvdictionary, it.first.c_str(), it.second.c_str(), 0);

    // Reconstructed from the 1.2 binary/PDB low-latency FFmpeg configuration.
    av_dict_set(&pAvdictionary, "rtsp_transport", "tcp", 0);
    av_dict_set(&pAvdictionary, "max_delay", "0", 0);
    av_dict_set(&pAvdictionary, "probesize", "16384", 0);
    av_dict_set(&pAvdictionary, "analyzeduration", "200000", 0);
    av_dict_set(&pAvdictionary, "fflags", "nobuffer", 0);
    av_dict_set(&pAvdictionary, "flags", "low_delay", 0);
    av_dict_set(&pAvdictionary, "framedrop", "1", 0);
    av_dict_set(&pAvdictionary, "strict_std_compliance", "-2", 0);
}

auto QRCodeForStream::init() -> bool
{
    pAVFormatContext = avformat_alloc_context();
    if (!pAVFormatContext)
        return false;

    pAVFormatContext->flags |= AVFMT_FLAG_NOBUFFER;
    pAVFormatContext->max_delay = 0;

    if (avformat_open_input(&pAVFormatContext, streamUrl.c_str(), nullptr, &pAvdictionary) != 0)
    {
        std::cerr << "Error opening input file" << std::endl;
        return false;
    }
    if (avformat_find_stream_info(pAVFormatContext, nullptr) < 0)
    {
        std::cerr << "Error finding stream information" << std::endl;
        return false;
    }

    AVStream* videoStream = nullptr;
    for (unsigned int i = 0; i < pAVFormatContext->nb_streams; ++i)
    {
        if (pAVFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStream = pAVFormatContext->streams[i];
            break;
        }
    }
    if (!videoStream)
    {
        std::cerr << "No video stream found" << std::endl;
        return false;
    }

    videoStreamIndex = videoStream->index;
    const AVCodec* decoder = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if (!decoder)
    {
        std::cerr << "Codec not found" << std::endl;
        return false;
    }

    pAVCodecContext = avcodec_alloc_context3(decoder);
    if (!pAVCodecContext)
        return false;
    avcodec_parameters_to_context(pAVCodecContext, videoStream->codecpar);
    pAVCodecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
    pAVCodecContext->flags2 |= AV_CODEC_FLAG2_FAST;

    if (avcodec_open2(pAVCodecContext, decoder, nullptr) < 0)
    {
        std::cerr << "Error opening codec" << std::endl;
        return false;
    }

    setStreamHW();
    pSwsContext = sws_getContext(
        pAVCodecContext->width, pAVCodecContext->height, pAVCodecContext->pix_fmt,
        videoStreamWidth, videoStreamHeight, AV_PIX_FMT_GRAY8, SWS_FAST_BILINEAR,
        nullptr, nullptr, nullptr);
    if (!pSwsContext)
        return false;

    pAVPacket = av_packet_alloc();
    pAVFrame = av_frame_alloc();
    return pAVPacket != nullptr && pAVFrame != nullptr;
}

void QRCodeForStream::continueLastLogin()
{
    switch (servertype)
    {
        using enum ServerType;
    case Official:
    {
        const bool ok = lowlatency::FastConfirmQRLogin(
            confirmUrl, uid, gameToken, lastTicket, gameType, device_id);
        Q_EMIT loginResults(ok ? ScanRet::SUCCESS : ScanRet::FAILURE_2);
    }
    break;
    case BH3_BiliBili:
    {
        ret = scanConfirm(lastTicket, uid, gameToken, m_name);
        Q_EMIT loginResults(ret);
    }
    break;
    default:
        break;
    }
}

void QRCodeForStream::run()
{
    threadPool.setMaxThreadCount(1);
    m_stop.store(true, std::memory_order_release);
    qrWorkerRunning.store(false, std::memory_order_release);
    ret = ScanRet::UNKNOW;
    {
        std::scoped_lock lock(frameMutex);
        latestFrame.release();
    }

    const bool keepOfficialApiWarm = servertype == ServerType::Official;
    if (keepOfficialApiWarm)
        lowlatency::StartQrApiKeepWarm();

    if (init())
    {
#ifndef SHOW
        cv::namedWindow("Video_Stream", cv::WINDOW_AUTOSIZE);
        cv::resizeWindow("Video_Stream", videoStreamWidth / 2, videoStreamHeight / 2);
#endif
        switch (servertype)
        {
            using enum ServerType;
        case Official:
            LoginOfficial();
            break;
        case BH3_BiliBili:
            LoginBH3BiliBili();
            break;
        default:
            break;
        }
    }
    else
    {
        ret = ScanRet::STREAMERROR;
    }

    m_stop.store(false, std::memory_order_release);
    threadPool.waitForDone();

    if (keepOfficialApiWarm)
        lowlatency::StopQrApiKeepWarm();

    if (ret == ScanRet::LIVESTOP || ret == ScanRet::STREAMERROR)
        Q_EMIT loginResults(ret);

#ifndef SHOW
    cv::destroyWindow("Video_Stream");
#endif

    if (pAVFormatContext)
        avformat_close_input(&pAVFormatContext);
    if (pAVCodecContext)
        avcodec_free_context(&pAVCodecContext);
    if (pSwsContext)
        sws_freeContext(pSwsContext);
    av_dict_free(&pAvdictionary);
    if (pAVFrame)
        av_frame_free(&pAVFrame);
    if (pAVPacket)
        av_packet_free(&pAVPacket);

    pAVFormatContext = nullptr;
    pAVCodecContext = nullptr;
    pSwsContext = nullptr;
    pAvdictionary = nullptr;
    pAVFrame = nullptr;
    pAVPacket = nullptr;
}
