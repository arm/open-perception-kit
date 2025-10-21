#pragma once

#include "public_types.h"

namespace uflw {

    struct ImageOps {

        enum Sampling { Nearest, Linear };
        
        struct Rect { 
            Rect(size_t x, size_t y, size_t w, size_t h) {
                this->x = x;
                this->y = y;
                this->w = w;
                this->h = h;
            }

            size_t x, y, w, h; 
        };

        struct Mean { float r, g, b; };

        // ---

        static bool StrechBlit_Rgb8_Rect_Rgbf32_Rect(uint8_t* src, size_t srcWidth, size_t srcHeight, const Rect& srcRect,
            float* dst, size_t dstWidth, size_t dstHeight, const Rect& dstRect, Sampling sampling = Sampling::Nearest);

        static bool StrechBlit_Rgb8_Full_Rgbf32_Full(uint8_t* src, size_t srcWidth, size_t srcHeight,
            float* dst, size_t dstWidth, size_t dstHeight, Sampling sampling = Sampling::Nearest);

        static bool Clear_Rgbf32(float* dst, size_t dstWidth, size_t dstHeight, float r, float g, float b);        

        static bool Fill_Rgbf32_Rect(float* dst, size_t dstWidth, size_t dstHeight, const Rect& dstRect, 
            float r, float g, float b);

        // ---

        static bool StrechBlit_Rgb8_Rect_Rgb8_Rect(uint8_t* src, size_t srcWidth, size_t srcHeight, const Rect& srcRect,
            uint8_t* dst, size_t dstWidth, size_t dstHeight, const Rect& dstRect, Sampling sampling = Sampling::Nearest);

        static bool StrechBlit_Rgb8_Full_Rgb8_Full(uint8_t* src, size_t srcWidth, size_t srcHeight,
            uint8_t* dst, size_t dstWidth, size_t dstHeight, Sampling sampling = Sampling::Nearest);

        static bool Clear_Rgb8(uint8_t* dst, size_t dstWidth, size_t dstHeight, uint8_t r, uint8_t g, uint8_t b);        

        static bool Fill_Rgb8_Rect(uint8_t* dst, size_t dstWidth, size_t dstHeight, const Rect& dstRect, 
            uint8_t r, uint8_t g, uint8_t b);

    };


}

