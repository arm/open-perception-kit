/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include "opk/ImageOpDesc.h"
#include "opk/Types.h"

namespace opk::stdop::preproc {

/**
 * @brief CPU image conversion kernels for semi-planar NV12 source frames.
 */
struct ImageOpsNv12 {
    /**
     * @brief Stretch-blits Nv12 full-frame input into RGB float32 CHW full-frame output.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Nv12_Full_Rgbf32_Full_Chw(const ImageOpDesc &src,
                                                      const ImageOpDesc &dst,
                                                      Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits Nv12 source rect into RGB float32 CHW destination rect.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Nv12_Rect_Rgbf32_Rect_Chw(const ImageOpDesc &src,
                                                      const ImageOpDesc &dst,
                                                      Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits Nv12 full-frame input into RGB8 HWC full-frame output.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Nv12_Full_Rgb8_Full_Hwc(const ImageOpDesc &src,
                                                    const ImageOpDesc &dst,
                                                    Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits Nv12 source rect into RGB8 HWC destination rect.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Nv12_Rect_Rgb8_Rect_Hwc(const ImageOpDesc &src,
                                                    const ImageOpDesc &dst,
                                                    Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits Nv12 full-frame input into RGB float32 HWC full-frame output.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Nv12_Full_Rgbf32_Full_Hwc(const ImageOpDesc &src,
                                                      const ImageOpDesc &dst,
                                                      Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits Nv12 source rect into RGB float32 HWC destination rect.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Nv12_Rect_Rgbf32_Rect_Hwc(const ImageOpDesc &src,
                                                      const ImageOpDesc &dst,
                                                      Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits Nv12 full-frame input into RGB float16 HWC full-frame output.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Nv12_Full_Rgbf16_Full_Hwc(const ImageOpDesc &src,
                                                      const ImageOpDesc &dst,
                                                      Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits Nv12 source rect into RGB float16 HWC destination rect.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Nv12_Rect_Rgbf16_Rect_Hwc(const ImageOpDesc &src,
                                                      const ImageOpDesc &dst,
                                                      Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits Nv12 full-frame input into RGB float16 CHW full-frame output.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Nv12_Full_Rgbf16_Full_Chw(const ImageOpDesc &src,
                                                      const ImageOpDesc &dst,
                                                      Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits Nv12 source rect into RGB float16 CHW destination rect.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Nv12_Rect_Rgbf16_Rect_Chw(const ImageOpDesc &src,
                                                      const ImageOpDesc &dst,
                                                      Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits Nv12 full-frame input into Gray8 full-frame output.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Nv12_Full_Gray8_Full(const ImageOpDesc &src,
                                                 const ImageOpDesc &dst,
                                                 Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits Nv12 source rect into Gray8 destination rect.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Nv12_Rect_Gray8_Rect(const ImageOpDesc &src,
                                                 const ImageOpDesc &dst,
                                                 Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits Nv12 full-frame input into Grayf32 full-frame output.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Nv12_Full_Grayf32_Full(const ImageOpDesc &src,
                                                   const ImageOpDesc &dst,
                                                   Sampling sampling = Sampling::Nearest);

    /**
     * @brief Stretch-blits Nv12 source rect into Grayf32 destination rect.
     * @param src Source image descriptor.
     * @param dst Destination image descriptor and output buffer.
     * @param sampling Sampling mode used during resize.
     * @return true on success, false when descriptors, buffers, or layout are invalid.
     */
    static bool StretchBlit_Nv12_Rect_Grayf32_Rect(const ImageOpDesc &src,
                                                   const ImageOpDesc &dst,
                                                   Sampling sampling = Sampling::Nearest);
};

} // namespace opk::stdop::preproc
