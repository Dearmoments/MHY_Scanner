#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

#include <cpr/connection_pool.h>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

#include "ApiDefs.hpp"

namespace lowlatency
{
class HotApiSession final
{
public:
    static HotApiSession& instance()
    {
        static HotApiSession value;
        return value;
    }

    // Call while an official livestream scanner is active. A separate CPR
    // session keeps a connection warm, while ConnectionPool lets the real
    // Scan/Confirm session reuse the same libcurl connection cache.
    void acquireKeepWarm()
    {
        {
            std::scoped_lock lock(stateMutex_);
            ++activeUsers_;
        }
        stateCv_.notify_all();
    }

    void releaseKeepWarm()
    {
        {
            std::scoped_lock lock(stateMutex_);
            if (activeUsers_ > 0)
                --activeUsers_;
        }
        stateCv_.notify_all();
    }

    cpr::Response post(const std::string_view url, const std::string& body, const cpr::Header& headers)
    {
        // cpr::Session is stateful and not thread safe. This mutex protects only
        // the actual Scan/Confirm session; keep-alive traffic uses another
        // session and therefore never queues in front of the critical request.
        std::scoped_lock lock(requestMutex_);
        requestSession_.SetUrl(cpr::Url{ std::string(url) });
        requestSession_.SetHeader(headers);
        requestSession_.SetBody(cpr::Body{ body });
        requestSession_.SetConnectTimeout(cpr::ConnectTimeout{ 1000 });
        requestSession_.SetTimeout(cpr::Timeout{ 3500 });
        return requestSession_.Post();
    }

private:
    HotApiSession()
    {
        requestSession_.SetConnectionPool(connectionPool_);
        keepWarmSession_.SetConnectionPool(connectionPool_);
        keepWarmThread_ = std::jthread([this](std::stop_token token) { keepWarmLoop(token); });
    }

    void warmupOnce()
    {
        keepWarmSession_.RemoveContent();
        keepWarmSession_.SetUrl(cpr::Url{ "https://api-sdk.mihoyo.com/" });
        keepWarmSession_.SetHeader(cpr::Header{ { "Connection", "keep-alive" } });
        keepWarmSession_.SetConnectTimeout(cpr::ConnectTimeout{ 800 });
        keepWarmSession_.SetTimeout(cpr::Timeout{ 1200 });
        (void)keepWarmSession_.Get();
    }

    void keepWarmLoop(const std::stop_token token)
    {
        using namespace std::chrono_literals;

        std::unique_lock lock(stateMutex_);
        while (!token.stop_requested())
        {
            if (!stateCv_.wait(lock, token, [this]() { return activeUsers_ > 0; }))
                return;

            lock.unlock();
            warmupOnce();
            lock.lock();

            // Refresh well before a typical idle HTTP connection is discarded.
            // If scanning stops, wake immediately and stop generating traffic.
            stateCv_.wait_for(lock, token, 20s, [this]() { return activeUsers_ == 0; });
        }
    }

    cpr::ConnectionPool connectionPool_;
    cpr::Session requestSession_;
    cpr::Session keepWarmSession_;

    std::mutex requestMutex_;
    std::mutex stateMutex_;
    std::condition_variable_any stateCv_;
    unsigned int activeUsers_{ 0 };

    // Keep this member last so its destructor requests stop and joins before
    // the CPR sessions/connection pool are destroyed.
    std::jthread keepWarmThread_;
};

inline void StartQrApiKeepWarm()
{
    HotApiSession::instance().acquireKeepWarm();
}

inline void StopQrApiKeepWarm()
{
    HotApiSession::instance().releaseKeepWarm();
}

inline bool FastScanQRLogin(const std::string_view url,
                            const std::string_view ticket,
                            const GameType gameType,
                            const std::string_view deviceId)
{
    const std::string body = nlohmann::json{
        { "app_id", static_cast<int>(gameType) },
        { "device", deviceId },
        { "ticket", ticket }
    }.dump();

    const auto response = HotApiSession::instance().post(
        url,
        body,
        cpr::Header{
            { "Content-Type", "application/json" },
            { "Connection", "keep-alive" }
        });

    const auto j = nlohmann::json::parse(response.text, nullptr, false);
    return !j.is_discarded() && j.value("retcode", -1) == 0;
}

inline bool FastConfirmQRLogin(const std::string_view url,
                               const std::string_view uid,
                               const std::string_view gameToken,
                               const std::string_view ticket,
                               const GameType gameType,
                               const std::string_view deviceId)
{
    const std::string body = nlohmann::json{
        { "app_id", static_cast<int>(gameType) },
        { "device", deviceId },
        { "ticket", ticket },
        { "payload", {
            { "proto", "Account" },
            { "raw", nlohmann::json{
                { "uid", uid },
                { "token", gameToken }
            }.dump() }
        } }
    }.dump();

    const auto response = HotApiSession::instance().post(
        url,
        body,
        cpr::Header{
            { "Content-Type", "application/json" },
            { "Connection", "keep-alive" }
        });

    const auto j = nlohmann::json::parse(response.text, nullptr, false);
    return !j.is_discarded() && j.value("retcode", -1) == 0;
}
} // namespace lowlatency
