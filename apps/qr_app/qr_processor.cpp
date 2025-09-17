#include "qr_processor.hpp"
#include <opencv4/opencv2/opencv.hpp>
#include <torch/script.h>
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
    try {
        // 1. 加载模型
        torch::jit::Module module;
        module = torch::jit::load("/home/amonologue/Projects/WebServer/apps/qr_app/upscale_generator.pt");
        module.eval();

        // 2. 将 OpenCV 图片转换为张量
        cv::Mat lowResImage = cv::imdecode(imageData, cv::IMREAD_GRAYSCALE);
        if (lowResImage.empty()) {
            std::cerr << "Error: Could not decode image for upscale." << std::endl;
            return {};
        }

        cv::resize(lowResImage, lowResImage, cv::Size(128, 128));
        cv::Mat floatImage;
        lowResImage.convertTo(floatImage, CV_32FC1, 1.0 / 255.0); // 注意：CV_32FC1
        
        // OpenCV Mat 到 LibTorch 张量
        auto input_tensor = torch::from_blob(
            floatImage.data, 
            {1, 1, floatImage.rows, floatImage.cols}, 
            torch::kFloat32
        );

        // 3. 执行模型推理
        std::vector<torch::jit::IValue> inputs;
        inputs.push_back(input_tensor);
        torch::Tensor output = module.forward(inputs).toTensor().squeeze(0).detach();

        // 4. 将输出张量转换为 OpenCV Mat
        // 反标准化并调整维度
        output = output.mul(255).clamp(0, 255).to(torch::kU8);
        
        // 转换为 OpenCV Mat
        cv::Mat highResImage(
            output.size(1), 
            output.size(2), 
            CV_8UC1, // 注意：CV_8UC1
            output.data_ptr<unsigned char>()
        );

        // 转换为 BGR
        cv::cvtColor(highResImage, highResImage, cv::COLOR_RGB2BGR);
        
        // 5. 将图片编码为 PNG 格式的二进制数据
        std::vector<unsigned char> highResData;
        cv::imencode(".png", highResImage, highResData);

        return highResData;

    } catch (const c10::Error& e) {
        std::cerr << "Error loading or running the model: " << e.msg() << std::endl;
        return {};
    }
}

/*
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
*/