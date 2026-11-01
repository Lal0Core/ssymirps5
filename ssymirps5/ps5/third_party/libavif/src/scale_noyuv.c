// SSYmirPS5: stands in for scale.c, which needs libyuv (not built here). libavif only scales a gain map to its base
// image's size; the app decodes plain images, so scaling reports "not implemented", as libavif's own build does
// without libyuv.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

avifResult avifImageScaleWithLimit(avifImage * image,
                                   uint32_t dstWidth,
                                   uint32_t dstHeight,
                                   uint32_t imageSizeLimit,
                                   uint32_t imageDimensionLimit,
                                   avifDiagnostics * diag)
{
    (void)image;
    (void)dstWidth;
    (void)dstHeight;
    (void)imageSizeLimit;
    (void)imageDimensionLimit;
    avifDiagnosticsPrintf(diag, "avifImageScale() needs libyuv");
    return AVIF_RESULT_NOT_IMPLEMENTED;
}

avifResult avifImageScale(avifImage * image, uint32_t dstWidth, uint32_t dstHeight, avifDiagnostics * diag)
{
    return avifImageScaleWithLimit(image, dstWidth, dstHeight, AVIF_DEFAULT_IMAGE_SIZE_LIMIT, AVIF_DEFAULT_IMAGE_DIMENSION_LIMIT, diag);
}
