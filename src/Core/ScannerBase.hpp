#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>

#include "ApiDefs.hpp"

class ScannerBase
{
public:
    GameType gameType;
    std::string_view scanUrl{};
    std::string_view confirmUrl{};
    std::string lastTicket;
    std::string lastQrCode;
    std::string uid;
    std::string gameToken{};
    std::string mid{};

    static std::string getUrlQueryParam(const std::string_view url, const std::string_view key)
    {
        const std::string needle = std::string(key) + "=";
        std::size_t position = url.find(needle);
        while (position != std::string_view::npos)
        {
            if (position == 0 || url[position - 1] == '?' || url[position - 1] == '&')
                break;
            position = url.find(needle, position + needle.size());
        }
        if (position == std::string_view::npos)
            return {};

        const std::size_t valueBegin = position + needle.size();
        const std::size_t valueEnd = url.find_first_of("&#", valueBegin);
        return std::string(url.substr(valueBegin, valueEnd == std::string_view::npos ?
            url.size() - valueBegin : valueEnd - valueBegin));
    }

    [[nodiscard]] bool setGameTypeByAppId(const std::string_view appId)
    {
        static const std::map<std::string_view, std::string_view> appToBiz{
            { "1", "8F3" },
            { "4", "9E&" },
            { "8", "8F%" },
            { "12", "%BA" }
        };
        const auto it = appToBiz.find(appId);
        if (it == appToBiz.end())
            return false;
        setGameType[it->second]();
        return true;
    }

    [[nodiscard]] bool parseOfficialQRCode(const std::string_view qrCode, std::string& ticket)
    {
        ticket = getUrlQueryParam(qrCode, "ticket");
        if (ticket.empty())
            ticket = getUrlQueryParam(qrCode, "tk");
        if (ticket.empty())
            return false;

        const std::string bizKey = getUrlQueryParam(qrCode, "biz_key");
        if (const auto it = setGameType.find(std::string_view(bizKey)); it != setGameType.end())
        {
            it->second();
            return true;
        }

        const std::string gameBiz = getUrlQueryParam(qrCode, "game_biz");
        if (const auto it = setGameType.find(std::string_view(gameBiz)); it != setGameType.end())
        {
            it->second();
            return true;
        }

        return setGameTypeByAppId(getUrlQueryParam(qrCode, "app_id"));
    }

protected:
    std::map<std::string_view, std::function<void()>> setGameType{
        { "8F3", [this]() {
             gameType = GameType::Honkai3;
             scanUrl = api::mhy::bh3::qrcode_scan;
             confirmUrl = api::mhy::bh3::qrcode_confirm;
         } },
        { "9E&", [this]() {
             gameType = GameType::Genshin;
             scanUrl = api::mhy::hk4e::qrcode_scan;
             confirmUrl = api::mhy::hk4e::qrcode_confirm;
         } },
        { "8F%", [this]() {
             gameType = GameType::HonkaiStarRail;
             scanUrl = api::mhy::hkrpg::qrcode_scan;
             confirmUrl = api::mhy::hkrpg::qrcode_confirm;
         } },
        { "%BA", [this]() {
             gameType = GameType::ZenlessZoneZero;
             scanUrl = api::mhy::nap::qrcode_scan;
             confirmUrl = api::mhy::nap::qrcode_confirm;
         } },
    };
};
