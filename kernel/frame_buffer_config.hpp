#pragma once

#include <stdint.h>

enum PixelFormat
{
    kPixelRGBResv8BitPerColor,
    kPixelBGRResv8BitPerColor,
};

struct FrameBufferConfig {
    // フレームバッファへのポインタ
    uint8_t* frame_buffer;
    // フレームバッファの余白を含めた横方向のピクセル数
    uint32_t pixels_per_scan_line;
    // 水平の解像度
    uint32_t horizontal_resolution;
    // 垂直の解像度
    uint32_t vertical_resolution;
    // ピクセルのデータ形式
    enum PixelFormat pixel_format;
};