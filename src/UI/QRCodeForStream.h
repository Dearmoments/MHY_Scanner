#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <mutex>
#include <string_view>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
};

#include <opencv2/core/mat.hpp>

#include <QThread>
#include <QThreadPool>

#include "ApiDefs.hpp"
#include "ConfigDate.h"
#include "ScannerBase.hpp"

class QRCodeForStream final :
    public QThread,
    public ScannerBase
{
    Q_OBJECT
public:
    QRCodeForStream(QObject* parent = nullptr);
    ~QRCodeForStream();
    Q_DISABLE_COPY_MOVE(QRCodeForStream)

    void setLoginInfo(const std::string_view uid, const std::string_view gameToken);
    void setLoginInfo(const std::string_view uid, const std::string_view gameToken, const std::string& name);
    void setServerType(const ServerType servertype);
    void setUrl(const std::string& url, const std::map<std::string, std::string> heard = {});
    auto init() -> bool;
    void run() override;
    void stop();
    void continueLastLogin();

Q_SIGNALS:
    void loginResults(const ScanRet ret);
    void loginConfirm(const GameType gameType, bool b);

private:
    void LoginOfficial();
    void LoginBH3BiliBili();
    void setStreamHW();

    // Low-latency frame path: at most one frame is waiting. A newer frame
    // overwrites an older pending frame instead of building a stale queue.
    void submitLatestFrame(cv::Mat&& img);
    void processLatestFrames();
    void processOfficialFrame(const cv::Mat& img);
    void processBiliFrame(const cv::Mat& img);

    std::mutex loginMutex;
    std::mutex frameMutex;
    cv::Mat latestFrame;
    std::atomic<bool> qrWorkerRunning{ false };

    std::string streamUrl{};
    std::string m_name;
    ConfigDate* m_config;
    ServerType servertype;
    ScanRet ret = ScanRet::UNKNOW;
    AVDictionary* pAvdictionary;
    AVFormatContext* pAVFormatContext;
    AVCodecContext* pAVCodecContext;
    SwsContext* pSwsContext;
    AVFrame* pAVFrame;
    AVPacket* pAVPacket;
    int videoStreamIndex{ 0 };
    int videoStreamWidth{};
    int videoStreamHeight{};
    QThreadPool threadPool;
    std::atomic<bool> m_stop;
};
