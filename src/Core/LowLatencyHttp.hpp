#pragma once

#include <mutex>
#include <string>
#include <string_view>

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

    void warmup()
    {
        std::scoped_lock lock(mutex_);
        session_.RemoveContent();
        session_.SetUrl(cpr::Url{ "https://api-sdk.mihoyo.com/" });
        session_.SetHeader(cpr::Header{ { "Connection", "keep-alive" } });
        session_.SetConnectTimeout(cpr::ConnectTimeout{ 1200 });
        session_.SetTimeout(cpr::Timeout{ 1800 });
        (void)session_.Get();
    }

    cpr::Response post(const std::string_view url, const std::string& body, const cpr::Header& headers)
    {
        std::scoped_lock lock(mutex_);
        session_.SetUrl(cpr::Url{ std::string(url) });
        session_.SetHeader(headers);
        session_.SetBody(cpr::Body{ body });
        session_.SetConnectTimeout(cpr::ConnectTimeout{ 1200 });
        session_.SetTimeout(cpr::Timeout{ 3500 });
        return session_.Post();
    }

private:
    HotApiSession() = default;

    std::mutex mutex_;
    cpr::Session session_;
};

inline void WarmupQrApiConnection()
{
    HotApiSession::instance().warmup();
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
