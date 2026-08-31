#pragma once

#include <string>

#include <opencv2/opencv.hpp>

class QRScanner
{
public:
    QRScanner() = default;
    ~QRScanner() = default;

    void decodeSingle(const cv::Mat& img, std::string& qrCode);
    void decodeMultiple(const cv::Mat& img, std::string& qrCode);
};
