#include "uniflow/cpu_image_kernels.h"
#include <gtest/gtest.h>
#include <iomanip>

TEST(img_transforms, no_resize_u8rgb_chw_to_float32rgb_chw) {
    using namespace uflw;
    constexpr size_t in_width = 8;
    constexpr size_t in_height = 8;
    constexpr size_t in_channel_number = 3;

    // create some input tensor pattern
    uint8_t input[in_channel_number * in_height * in_width];
    for (size_t c = 0; c < in_channel_number; c++) {
        for (size_t h = 0; h < in_height; h++) {
            for (size_t w = 0; w < in_width; w++) {
                input[c * in_height * in_width + h * in_width + w] =
                    static_cast<uint8_t>(c * 100 + h * 10 + w);
            }
        }
    }

    float output[in_channel_number * in_height * in_width];

    // trivial conversion from u8 rgb chw to float32 rgb chw without resize
    // no resize no layout conversion, just type conversion
    ASSERT_TRUE(ImageOps::StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Chw(input,
                                                                   in_width,
                                                                   in_height,
                                                                   {0, 0, in_width, in_height},
                                                                   output,
                                                                   in_width,
                                                                   in_height,
                                                                   {0, 0, in_width, in_height},
                                                                   ImageOps::Sampling::Nearest));
    // check that float converted back to u8 matches input values
    for (size_t c = 0; c < in_channel_number; c++) {
        for (size_t h = 0; h < in_height; h++) {
            for (size_t w = 0; w < in_width; w++) {
                auto px_addr = c * in_height * in_width + h * in_width + w;
                ASSERT_EQ(static_cast<uint8_t>(output[px_addr] * 255.f), input[px_addr])
                    << "at channel " << c << ", row " << h << ", column " << w;
            }
        }
    }
}
