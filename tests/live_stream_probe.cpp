// Last updated: 2026-09-15 00:18:00 +08:00. Never starts a QR login worker.
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <iostream>
#include "LiveStreamLink.h"
#include "QRCodeForStream.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2 && argc != 3) return 2;
    const bool omitHeaders = argc == 3 && std::string_view(argv[2]) == "--no-headers";
    if (argc == 3 && !omitHeaders) return 2;
    // Use a throwaway working directory instead of the user's account file.
    QTemporaryDir configDir(QCoreApplication::applicationDirPath() + "/probe-config-XXXXXX");
    if (!configDir.isValid() || !QDir::setCurrent(configDir.path())) return 3;
    const auto info = GetLiveInfo(LivePlatform::BiliBili, argv[1]);
    std::cout << "room_status=" << static_cast<int>(info.status) << std::endl;
    if (info.status != LiveStreamStatus::Normal)
    {
        QDir::setCurrent(QCoreApplication::applicationDirPath());
        return 4;
    }
    std::cout << "referer=" << info.options.at("referer")
              << " browser_user_agent=" << info.options.at("user_agent").starts_with("Mozilla/")
              << std::endl;
    QRCodeForStream scanner;
    scanner.setUrl(info.link, omitHeaders ? std::map<std::string, std::string>{} : info.options);
    const bool initialized = scanner.init();
    std::cout << "scanner_init=" << initialized << std::endl;
    QDir::setCurrent(QCoreApplication::applicationDirPath());
    return initialized ? 0 : 5;
}
