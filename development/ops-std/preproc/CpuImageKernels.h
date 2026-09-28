/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include "opk/ImageOpDesc.h"
#include "opk/Types.h"

namespace opk::stdop::preproc {

/**
 * @brief CPU image conversion kernels for packed BGRA and RGB source frames.
 */
struct ImageOps {

    // ---
    /**
     * @brief Stretch-blits BGRA8 HWC full-frame input into RGB float32 CHW full-frame output.
     * @param src Source descriptor (BGRA8 HWC, full-frame rect expected).
     * @param dst Destination descriptor (RGB float32 CHW, full-frame rect expected).
     * @param sampling Sampling mode used during resize.
     * @return true on success, false on invalid pointers or out-of-bounds rects.
     */
    static bool StretchBlit_Bgra8_Hwc_Full_Rgbf32_Full_Chw(const ImageOpDesc &src,
                                                           const ImageOpDesc &dst,
                                                           Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits BGRA8 HWC source rect into RGB float32 CHW destination rect.
     * @param src Source descriptor (BGRA8 HWC); src.mean/src.std are applied when non-default.
     * @param dst Destination descriptor (RGB float32 CHW).
     * @param sampling Sampling mode used during resize.
     * @return true on success, false on invalid pointers or out-of-bounds rects.
     */
    static bool StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Chw(const ImageOpDesc &src,
                                                           const ImageOpDesc &dst,
                                                           Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits BGRA8 HWC full-frame input into RGB8 HWC full-frame output.
     * @param src Source descriptor (BGRA8 HWC, full-frame rect expected).
     * @param dst Destination descriptor (RGB8 HWC, full-frame rect expected).
     * @param sampling Sampling mode used during resize.
     * @return true on success, false on invalid pointers or out-of-bounds rects.
     */
    static bool StretchBlit_Bgra8_Hwc_Full_Rgb8_Full_Hwc(const ImageOpDesc &src,
                                                         const ImageOpDesc &dst,
                                                         Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits BGRA8 HWC source rect into RGB8 HWC destination rect.
     * @param src Source descriptor (BGRA8 HWC).
     * @param dst Destination descriptor (RGB8 HWC).
     * @param sampling Sampling mode used during resize.
     * @return true on success, false on invalid pointers or out-of-bounds rects.
     */
    static bool StretchBlit_Bgra8_Hwc_Rect_Rgb8_Rect_Hwc(const ImageOpDesc &src,
                                                         const ImageOpDesc &dst,
                                                         Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits BGRA8 HWC full-frame input into RGB float32 HWC full-frame output.
     * @param src Source descriptor (BGRA8 HWC, full-frame rect expected).
     * @param dst Destination descriptor (RGB float32 HWC, full-frame rect expected).
     * @param sampling Sampling mode used during resize.
     * @return true on success, false on invalid pointers or out-of-bounds rects.
     */
    static bool StretchBlit_Bgra8_Hwc_Full_Rgbf32_Full_Hwc(const ImageOpDesc &src,
                                                           const ImageOpDesc &dst,
                                                           Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits BGRA8 HWC source rect into RGB float32 HWC destination rect.
     * @param src Source descriptor (BGRA8 HWC); src.mean/src.std are applied when non-default.
     * @param dst Destination descriptor (RGB float32 HWC).
     * @param sampling Sampling mode used during resize.
     * @return true on success, false on invalid pointers or out-of-bounds rects.
     */
    static bool StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Hwc(const ImageOpDesc &src,
                                                           const ImageOpDesc &dst,
                                                           Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits BGRA8 HWC full-frame input into RGB float16 HWC full-frame output.
     * @param src Source descriptor (BGRA8 HWC, full-frame rect expected).
     * @param dst Destination descriptor (RGB float16 HWC, full-frame rect expected).
     * @param sampling Sampling mode used during resize.
     * @return true on success, false on invalid pointers or out-of-bounds rects.
     */
    static bool StretchBlit_Bgra8_Hwc_Full_Rgbf16_Full_Hwc(const ImageOpDesc &src,
                                                           const ImageOpDesc &dst,
                                                           Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits BGRA8 HWC source rect into RGB float16 HWC destination rect.
     * @param src Source descriptor (BGRA8 HWC); src.mean/src.std are applied when non-default.
     * @param dst Destination descriptor (RGB float16 HWC).
     * @param sampling Sampling mode used during resize.
     * @return true on success, false on invalid pointers or out-of-bounds rects.
     */
    static bool StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Hwc(const ImageOpDesc &src,
                                                           const ImageOpDesc &dst,
                                                           Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits BGRA8 HWC full-frame input into RGB float16 CHW full-frame output.
     * @param src Source descriptor (BGRA8 HWC, full-frame rect expected).
     * @param dst Destination descriptor (RGB float16 CHW, full-frame rect expected).
     * @param sampling Sampling mode used during resize.
     * @return true on success, false on invalid pointers or out-of-bounds rects.
     */
    static bool StretchBlit_Bgra8_Hwc_Full_Rgbf16_Full_Chw(const ImageOpDesc &src,
                                                           const ImageOpDesc &dst,
                                                           Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits BGRA8 HWC source rect into RGB float16 CHW destination rect.
     * @param src Source descriptor (BGRA8 HWC); src.mean/src.std are applied when non-default.
     * @param dst Destination descriptor (RGB float16 CHW).
     * @param sampling Sampling mode used during resize.
     * @return true on success, false on invalid pointers or out-of-bounds rects.
     */
    static bool StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Chw(const ImageOpDesc &src,
                                                           const ImageOpDesc &dst,
                                                           Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits BGRA8 HWC full-frame input into Gray8 full-frame output.
     * @param src Source descriptor (BGRA8 HWC, full-frame rect expected).
     * @param dst Destination descriptor (Gray8, full-frame rect expected).
     * @param sampling Sampling mode used during resize.
     * @return true on success, false on invalid pointers or out-of-bounds rects.
     */
    static bool StretchBlit_Bgra8_Hwc_Full_Gray8_Full(const ImageOpDesc &src,
                                                      const ImageOpDesc &dst,
                                                      Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits BGRA8 HWC source rect into Gray8 destination rect.
     * @param src Source descriptor (BGRA8 HWC).
     * @param dst Destination descriptor (Gray8).
     * @param sampling Sampling mode used during resize.
     * @return true on success, false on invalid pointers or out-of-bounds rects.
     */
    static bool StretchBlit_Bgra8_Hwc_Rect_Gray8_Rect(const ImageOpDesc &src,
                                                      const ImageOpDesc &dst,
                                                      Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits BGRA8 HWC full-frame input into Grayf32 full-frame output.
     * @param src Source descriptor (BGRA8 HWC, full-frame rect expected).
     * @param dst Destination descriptor (Grayf32, full-frame rect expected).
     * @param sampling Sampling mode used during resize.
     * @return true on success, false on invalid pointers or out-of-bounds rects.
     */
    static bool StretchBlit_Bgra8_Hwc_Full_Grayf32_Full(const ImageOpDesc &src,
                                                        const ImageOpDesc &dst,
                                                        Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits BGRA8 HWC source rect into Grayf32 destination rect.
     * @param src Source descriptor (BGRA8 HWC).
     * @param dst Destination descriptor (Grayf32).
     * @param sampling Sampling mode used during resize.
     * @return true on success, false on invalid pointers or out-of-bounds rects.
     */
    static bool StretchBlit_Bgra8_Hwc_Rect_Grayf32_Rect(const ImageOpDesc &src,
                                                        const ImageOpDesc &dst,
                                                        Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits RGB8 HWC full-frame input into RGB float32 CHW full-frame output.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Rgb8_Hwc_Full_Rgbf32_Full_Chw(const ImageOpDesc &src,
                                                          const ImageOpDesc &dst,
                                                          Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits RGB8 HWC source rect into RGB float32 CHW destination rect.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Rgb8_Hwc_Rect_Rgbf32_Rect_Chw(const ImageOpDesc &src,
                                                          const ImageOpDesc &dst,
                                                          Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits RGB8 HWC full-frame input into RGB8 HWC full-frame output.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Rgb8_Hwc_Full_Rgb8_Full_Hwc(const ImageOpDesc &src,
                                                        const ImageOpDesc &dst,
                                                        Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits RGB8 HWC source rect into RGB8 HWC destination rect.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Rgb8_Hwc_Rect_Rgb8_Rect_Hwc(const ImageOpDesc &src,
                                                        const ImageOpDesc &dst,
                                                        Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits RGB8 HWC full-frame input into RGB float32 HWC full-frame output.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Rgb8_Hwc_Full_Rgbf32_Full_Hwc(const ImageOpDesc &src,
                                                          const ImageOpDesc &dst,
                                                          Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits RGB8 HWC source rect into RGB float32 HWC destination rect.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Rgb8_Hwc_Rect_Rgbf32_Rect_Hwc(const ImageOpDesc &src,
                                                          const ImageOpDesc &dst,
                                                          Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits RGB8 HWC full-frame input into RGB float16 HWC full-frame output.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Rgb8_Hwc_Full_Rgbf16_Full_Hwc(const ImageOpDesc &src,
                                                          const ImageOpDesc &dst,
                                                          Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits RGB8 HWC source rect into RGB float16 HWC destination rect.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Rgb8_Hwc_Rect_Rgbf16_Rect_Hwc(const ImageOpDesc &src,
                                                          const ImageOpDesc &dst,
                                                          Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits RGB8 HWC full-frame input into RGB float16 CHW full-frame output.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Rgb8_Hwc_Full_Rgbf16_Full_Chw(const ImageOpDesc &src,
                                                          const ImageOpDesc &dst,
                                                          Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits RGB8 HWC source rect into RGB float16 CHW destination rect.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Rgb8_Hwc_Rect_Rgbf16_Rect_Chw(const ImageOpDesc &src,
                                                          const ImageOpDesc &dst,
                                                          Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits RGB8 HWC full-frame input into Gray8 full-frame output.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Rgb8_Hwc_Full_Gray8_Full(const ImageOpDesc &src,
                                                     const ImageOpDesc &dst,
                                                     Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits RGB8 HWC source rect into Gray8 destination rect.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Rgb8_Hwc_Rect_Gray8_Rect(const ImageOpDesc &src,
                                                     const ImageOpDesc &dst,
                                                     Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits RGB8 HWC full-frame input into Grayf32 full-frame output.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Rgb8_Hwc_Full_Grayf32_Full(const ImageOpDesc &src,
                                                       const ImageOpDesc &dst,
                                                       Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits RGB8 HWC source rect into Grayf32 destination rect.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Rgb8_Hwc_Rect_Grayf32_Rect(const ImageOpDesc &src,
                                                       const ImageOpDesc &dst,
                                                       Sampling sampling = Sampling::Nearest);
};

} // namespace opk::stdop::preproc
