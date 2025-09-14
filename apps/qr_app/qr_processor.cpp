#include "qr_processor.hpp"
#include <opencv4/opencv2/opencv.hpp>
#include <iostream>

std::string recognizeQrCode(const std::vector<unsigned char>& imageData) {
    // 使用 OpenCV 将二进制数据解码为图像
    
    cv::Mat image = cv::imdecode(imageData, cv::IMREAD_COLOR);
    if (image.empty()) {
        std::cerr << "Error: Could not decode image." << std::endl;
        return "";
    }

    try {
        // 使用 OpenCV 内置的二维码识别器
        cv::QRCodeDetector qrDecoder;
        std::string decodedInfo = qrDecoder.detectAndDecode(image);

        if (decodedInfo.empty()) {
            std::cerr << "QR Code recognition failed." << std::endl;
            return "";
        }
        
        return decodedInfo;
    } catch (const std::exception& e) {
        std::cerr << "QR Code recognition failed with exception: " << e.what() << std::endl;
        return "";
    }
}

std::vector<unsigned char> upscaleQrCode(const std::vector<unsigned char>& imageData) {
    // 这部分保持不变，因为我们本来就在用 OpenCV
    cv::Mat lowResImage = cv::imdecode(imageData, cv::IMREAD_COLOR);
    if (lowResImage.empty()) {
        std::cerr << "Error: Could not decode image for upscale." << std::endl;
        return {};
    }

    // 使用双三次插值（CUBIC）进行超分，放大到2倍
    cv::Mat highResImage;
    cv::resize(lowResImage, highResImage, cv::Size(), 2.0, 2.0, cv::INTER_CUBIC);

    // 将处理后的图片编码为 PNG 格式的二进制数据
    std::vector<unsigned char> highResData;
    cv::imencode(".png", highResImage, highResData);

    return highResData;
}