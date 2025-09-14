#pragma once

#include <string>
#include <vector>

// 二维码识别函数
// 接收图片的二进制数据，返回识别结果。如果失败，返回空字符串。
std::string recognizeQrCode(const std::vector<unsigned char>& imageData);

// 超分辨率函数
// 接收低分辨率图片的二进制数据，返回超分辨率后的图片二进制数据。
std::vector<unsigned char> upscaleQrCode(const std::vector<unsigned char>& imageData);