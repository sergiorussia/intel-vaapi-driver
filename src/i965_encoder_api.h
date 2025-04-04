/*
 * Copyright (C) 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *     Pengfei Qu <Pengfei.Qu@intel.com>
 *
 */

#ifndef _I965_ENCODER_API_H_
#define _I965_ENCODER_API_H_

#include <va/va.h>

struct intel_encoder_context;
struct hw_context;

/* H264/AVC */
extern Bool
gen9_avc_vme_context_init(VADriverContextP ctx, struct intel_encoder_context *encoder_context);

extern Bool
gen9_avc_pak_context_init(VADriverContextP ctx, struct intel_encoder_context *encoder_context);

extern VAStatus
gen9_avc_coded_status(VADriverContextP ctx, char *buffer, struct hw_context *hw_context);


/* H265/HEVC */
extern Bool
gen9_hevc_vme_context_init(VADriverContextP ctx, struct intel_encoder_context *encoder_context);

extern Bool
gen9_hevc_pak_context_init(VADriverContextP ctx, struct intel_encoder_context *encoder_context);

extern Bool
gen10_hevc_vme_context_init(VADriverContextP ctx, struct intel_encoder_context *encoder_context);

extern Bool
gen10_hevc_pak_context_init(VADriverContextP ctx, struct intel_encoder_context *encoder_context);

#endif  // _I965_ENCODER_API_H_
