/*
 * Copyright (C) 2014 Intel Corporation
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
 *    Zhao Yakui <yakui.zhao@intel.com>
 *    Xiang Haihao <haihao.xiang@intel.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "intel_batchbuffer.h"
#include "intel_driver.h"

#include "i965_defines.h"
#include "i965_drv_video.h"
#include "i965_encoder.h"
#include "gen6_vme.h"
#include "gen6_mfc.h"
#include "gen9_mfc.h"
#include "intel_media.h"
#include "gen9_vp9_encapi.h"
#include "i965_post_processing.h"
#include "i965_encoder_api.h"

#ifdef SURFACE_STATE_PADDED_SIZE
#undef SURFACE_STATE_PADDED_SIZE
#endif

#define SURFACE_STATE_PADDED_SIZE				SURFACE_STATE_PADDED_SIZE_GEN8
#define SURFACE_STATE_OFFSET(index)				(SURFACE_STATE_PADDED_SIZE * index)
#define BINDING_TABLE_OFFSET(index)				(SURFACE_STATE_OFFSET(MAX_MEDIA_SURFACES_GEN6) + sizeof(unsigned int) * index)

#define VME_INTRA_SHADER		0
#define VME_INTER_SHADER		1
#define VME_BINTER_SHADER		2

#define CURBE_ALLOCATION_SIZE	37				/* in 256-bit */
#define CURBE_TOTAL_DATA_LENGTH	(4 * 32)		/* in byte, it should be less than or equal to CURBE_ALLOCATION_SIZE * 32 */
#define CURBE_URB_ENTRY_LENGTH	4				/* in 256-bit, it should be less than or equal to CURBE_TOTAL_DATA_LENGTH / 32 */

#define VME_MSG_LENGTH			32

static const uint32_t gen9_vme_intra_frame[][4] = {
#include "shaders/vme/intra_frame_gen9.g9b"
};

static const uint32_t gen9_vme_inter_frame[][4] = {
#include "shaders/vme/inter_frame_gen9.g9b"
};

static const uint32_t gen9_vme_inter_bframe[][4] = {
#include "shaders/vme/inter_bframe_gen9.g9b"
};

static struct i965_kernel gen9_vme_kernels[] = {
	{
		"VME Intra Frame",
		VME_INTRA_SHADER, /*index*/
		gen9_vme_intra_frame,
		sizeof(gen9_vme_intra_frame),
		NULL
	},
	{
		"VME inter Frame",
		VME_INTER_SHADER,
		gen9_vme_inter_frame,
		sizeof(gen9_vme_inter_frame),
		NULL
	},
	{
		"VME inter BFrame",
		VME_BINTER_SHADER,
		gen9_vme_inter_bframe,
		sizeof(gen9_vme_inter_bframe),
		NULL
	}
};

static const uint32_t gen9_vme_mpeg2_intra_frame[][4] = {
#include "shaders/vme/intra_frame_gen9.g9b"
};

static const uint32_t gen9_vme_mpeg2_inter_frame[][4] = {
#include "shaders/vme/mpeg2_inter_gen9.g9b"
};

static struct i965_kernel gen9_vme_mpeg2_kernels[] = {
	{
		"VME Intra Frame",
		VME_INTRA_SHADER, /*index*/
		gen9_vme_mpeg2_intra_frame,
		sizeof(gen9_vme_mpeg2_intra_frame),
		NULL
	},
	{
		"VME inter Frame",
		VME_INTER_SHADER,
		gen9_vme_mpeg2_inter_frame,
		sizeof(gen9_vme_mpeg2_inter_frame),
		NULL
	},
};

static const uint32_t gen9_vme_vp8_intra_frame[][4] = {
#include "shaders/vme/vp8_intra_frame_gen9.g9b"
};

static const uint32_t gen9_vme_vp8_inter_frame[][4] = {
#include "shaders/vme/vp8_inter_frame_gen9.g9b"
};

static struct i965_kernel gen9_vme_vp8_kernels[] = {
	{
		"VME Intra Frame",
		VME_INTRA_SHADER, /*index*/
		gen9_vme_vp8_intra_frame,
		sizeof(gen9_vme_vp8_intra_frame),
		NULL
	},
	{
		"VME inter Frame",
		VME_INTER_SHADER,
		gen9_vme_vp8_inter_frame,
		sizeof(gen9_vme_vp8_inter_frame),
		NULL
	},
};

/* HEVC */

static const uint32_t gen9_vme_hevc_intra_frame[][4] = {
#include "shaders/vme/intra_frame_gen9.g9b"
};

static const uint32_t gen9_vme_hevc_inter_frame[][4] = {
#include "shaders/vme/inter_frame_gen9.g9b"
};

static const uint32_t gen9_vme_hevc_inter_bframe[][4] = {
#include "shaders/vme/inter_bframe_gen9.g9b"
};

static struct i965_kernel gen9_vme_hevc_kernels[] = {
	{
		"VME Intra Frame",
		VME_INTRA_SHADER, /*index*/
		gen9_vme_hevc_intra_frame,
		sizeof(gen9_vme_hevc_intra_frame),
		NULL
	},
	{
		"VME inter Frame",
		VME_INTER_SHADER,
		gen9_vme_hevc_inter_frame,
		sizeof(gen9_vme_hevc_inter_frame),
		NULL
	},
	{
		"VME inter BFrame",
		VME_BINTER_SHADER,
		gen9_vme_hevc_inter_bframe,
		sizeof(gen9_vme_hevc_inter_bframe),
		NULL
	}
};
/* only used for VME source surface state */
static void
gen9_vme_source_surface_state(VADriverContextP ctx,
							  int index,
							  struct object_surface *obj_surface,
							  struct intel_encoder_context *encoder_context)
{
	struct gen6_vme_context *vme_context = encoder_context->vme_context;

	vme_context->vme_surface2_setup(ctx,
									&vme_context->gpe_context,
									obj_surface,
									BINDING_TABLE_OFFSET(index),
									SURFACE_STATE_OFFSET(index));
}

static void
gen9_vme_media_source_surface_state(VADriverContextP ctx,
									int index,
									struct object_surface *obj_surface,
									struct intel_encoder_context *encoder_context)
{
	struct gen6_vme_context *vme_context = encoder_context->vme_context;

	vme_context->vme_media_rw_surface_setup(ctx,
											&vme_context->gpe_context,
											obj_surface,
											BINDING_TABLE_OFFSET(index),
											SURFACE_STATE_OFFSET(index),
											0);
}

static void
gen9_vme_media_chroma_source_surface_state(VADriverContextP ctx,
										   int index,
										   struct object_surface *obj_surface,
										   struct intel_encoder_context *encoder_context)
{
	struct gen6_vme_context *vme_context = encoder_context->vme_context;

	vme_context->vme_media_chroma_surface_setup(ctx,
												&vme_context->gpe_context,
												obj_surface,
												BINDING_TABLE_OFFSET(index),
												SURFACE_STATE_OFFSET(index),
												0);
}

static void
gen9_vme_output_buffer_setup(VADriverContextP ctx,
							 struct encode_state *encode_state,
							 int index,
							 struct intel_encoder_context *encoder_context,
							 int is_intra,
							 int width_in_mbs,
							 int height_in_mbs)

{
	struct i965_driver_data *i965 = i965_driver_data(ctx);
	struct gen6_vme_context *vme_context = encoder_context->vme_context;

	vme_context->vme_output.num_blocks = width_in_mbs * height_in_mbs;
	vme_context->vme_output.pitch = 16; /* in bytes, always 16 */

	if (is_intra)
		vme_context->vme_output.size_block = INTRA_VME_OUTPUT_IN_BYTES * 2;
	else
		vme_context->vme_output.size_block = INTRA_VME_OUTPUT_IN_BYTES * 24;
	/*
	 * Inter MV . 32-byte Intra search + 16 IME info + 128 IME MV + 32 IME Ref
	 * + 16 FBR Info + 128 FBR MV + 32 FBR Ref.
	 * 16 * (2 + 2 * (1 + 8 + 2))= 16 * 24.
	 */

	vme_context->vme_output.bo = dri_bo_alloc(i965->intel.bufmgr,
											  "VME output buffer",
											  vme_context->vme_output.num_blocks * vme_context->vme_output.size_block,
											  0x1000);
	assert(vme_context->vme_output.bo);
	vme_context->vme_buffer_suface_setup(ctx,
										 &vme_context->gpe_context,
										 &vme_context->vme_output,
										 BINDING_TABLE_OFFSET(index),
										 SURFACE_STATE_OFFSET(index));
}

static void
gen9_vme_avc_output_buffer_setup(VADriverContextP ctx,
								 struct encode_state *encode_state,
								 int index,
								 struct intel_encoder_context *encoder_context)
{
	VAEncSequenceParameterBufferH264 *pSequenceParameter = (VAEncSequenceParameterBufferH264 *)encode_state->seq_param_ext->buffer;
	VAEncSliceParameterBufferH264 *pSliceParameter = (VAEncSliceParameterBufferH264 *)encode_state->slice_params_ext[0]->buffer;
	int is_intra = pSliceParameter->slice_type == SLICE_TYPE_I;
	int width_in_mbs = pSequenceParameter->picture_width_in_mbs;
	int height_in_mbs = pSequenceParameter->picture_height_in_mbs;

	gen9_vme_output_buffer_setup(ctx, encode_state, index, encoder_context, is_intra, width_in_mbs, height_in_mbs);

}

static void
gen9_vme_output_vme_batchbuffer_setup(VADriverContextP ctx,
									  struct encode_state *encode_state,
									  int index,
									  struct intel_encoder_context *encoder_context,
									  int width_in_mbs,
									  int height_in_mbs)
{
	struct i965_driver_data *i965 = i965_driver_data(ctx);
	struct gen6_vme_context *vme_context = encoder_context->vme_context;

	vme_context->vme_batchbuffer.num_blocks = width_in_mbs * height_in_mbs + 1;
	vme_context->vme_batchbuffer.size_block = 64; /* 4 OWORDs */
	vme_context->vme_batchbuffer.pitch = 16;
	vme_context->vme_batchbuffer.bo = dri_bo_alloc(i965->intel.bufmgr,
												   "VME batchbuffer",
												   vme_context->vme_batchbuffer.num_blocks * vme_context->vme_batchbuffer.size_block,
												   0x1000);
	vme_context->vme_buffer_suface_setup(ctx,
										 &vme_context->gpe_context,
										 &vme_context->vme_batchbuffer,
										 BINDING_TABLE_OFFSET(index),
										 SURFACE_STATE_OFFSET(index));
}

static void
gen9_vme_avc_output_vme_batchbuffer_setup(VADriverContextP ctx,
										  struct encode_state *encode_state,
										  int index,
										  struct intel_encoder_context *encoder_context)
{
	VAEncSequenceParameterBufferH264 *pSequenceParameter = (VAEncSequenceParameterBufferH264 *)encode_state->seq_param_ext->buffer;
	int width_in_mbs = pSequenceParameter->picture_width_in_mbs;
	int height_in_mbs = pSequenceParameter->picture_height_in_mbs;

	gen9_vme_output_vme_batchbuffer_setup(ctx, encode_state, index, encoder_context, width_in_mbs, height_in_mbs);
}


static VAStatus
gen9_vme_surface_setup(VADriverContextP ctx,
					   struct encode_state *encode_state,
					   int is_intra,
					   struct intel_encoder_context *encoder_context)
{
	struct object_surface *obj_surface;

	/*Setup surfaces state*/
	/* current picture for encoding */
	obj_surface = encode_state->input_yuv_object;
	assert(obj_surface);
	gen9_vme_source_surface_state(ctx, 0, obj_surface, encoder_context);
	gen9_vme_media_source_surface_state(ctx, 4, obj_surface, encoder_context);
	gen9_vme_media_chroma_source_surface_state(ctx, 6, obj_surface, encoder_context);

	if (!is_intra) {
		VAEncSliceParameterBufferH264 *slice_param = (VAEncSliceParameterBufferH264 *)encode_state->slice_params_ext[0]->buffer;
		int slice_type;

		slice_type = intel_avc_enc_slice_type_fixup(slice_param->slice_type);
		assert(slice_type != SLICE_TYPE_I && slice_type != SLICE_TYPE_SI);

		intel_avc_vme_reference_state(ctx, encode_state, encoder_context, 0, 1, gen9_vme_source_surface_state);

		if (slice_type == SLICE_TYPE_B)
			intel_avc_vme_reference_state(ctx, encode_state, encoder_context, 1, 2, gen9_vme_source_surface_state);
	}

	/* VME output */
	gen9_vme_avc_output_buffer_setup(ctx, encode_state, 3, encoder_context);
	gen9_vme_avc_output_vme_batchbuffer_setup(ctx, encode_state, 5, encoder_context);
	intel_h264_setup_cost_surface(ctx, encode_state, encoder_context,
								  BINDING_TABLE_OFFSET(INTEL_COST_TABLE_OFFSET),
								  SURFACE_STATE_OFFSET(INTEL_COST_TABLE_OFFSET));

	return VA_STATUS_SUCCESS;
}

static VAStatus gen9_vme_interface_setup(VADriverContextP ctx,
										 struct encode_state *encode_state,
										 struct intel_encoder_context *encoder_context)
{
	struct gen6_vme_context *vme_context = encoder_context->vme_context;
	struct gen8_interface_descriptor_data *desc;
	int i;
	dri_bo *bo;
	unsigned char *desc_ptr;

	bo = vme_context->gpe_context.idrt.bo;
	dri_bo_map(bo, 1);
	assert(bo->virtual);
	desc_ptr = (unsigned char *)bo->virtual + vme_context->gpe_context.idrt.offset;

	desc = (struct gen8_interface_descriptor_data *)desc_ptr;

	for (i = 0; i < vme_context->vme_kernel_sum; i++) {
		struct i965_kernel *kernel;
		kernel = &vme_context->gpe_context.kernels[i];
		assert(sizeof(*desc) == 32);
		/*Setup the descritor table*/
		memset(desc, 0, sizeof(*desc));
		desc->desc0.kernel_start_pointer = kernel->kernel_offset >> 6;
		desc->desc3.sampler_count = 0; /* FIXME: */
		desc->desc3.sampler_state_pointer = 0;
		desc->desc4.binding_table_entry_count = 1; /* FIXME: */
		desc->desc4.binding_table_pointer = (BINDING_TABLE_OFFSET(0) >> 5);
		desc->desc5.constant_urb_entry_read_offset = 0;
		desc->desc5.constant_urb_entry_read_length = CURBE_URB_ENTRY_LENGTH;

		desc++;
	}

	dri_bo_unmap(bo);

	return VA_STATUS_SUCCESS;
}

static VAStatus gen9_vme_constant_setup(VADriverContextP ctx,
										struct encode_state *encode_state,
										struct intel_encoder_context *encoder_context,
										int denom)
{
	struct gen6_vme_context *vme_context = encoder_context->vme_context;
	unsigned char *constant_buffer;
	unsigned int *vme_state_message;
	int mv_num = 32;

	vme_state_message = (unsigned int *)vme_context->vme_state_message;

	if (encoder_context->codec == CODEC_H264 ||
		encoder_context->codec == CODEC_H264_MVC) {
		if (vme_context->h264_level >= 30) {
			mv_num = 16 / denom;

			if (vme_context->h264_level >= 31)
				mv_num = 8 / denom;
		}
	} else if (encoder_context->codec == CODEC_MPEG2) {
		mv_num = 2 / denom;
	} else if (encoder_context->codec == CODEC_HEVC) {
		if (vme_context->hevc_level >= 30 * 3) {
			mv_num = 16;

			if (vme_context->hevc_level >= 31 * 3)
				mv_num = 8;
		}/* use the avc level setting */
	}

	vme_state_message[31] = mv_num;

	dri_bo_map(vme_context->gpe_context.curbe.bo, 1);
	assert(vme_context->gpe_context.curbe.bo->virtual);
	constant_buffer = (unsigned char *)vme_context->gpe_context.curbe.bo->virtual +
					  vme_context->gpe_context.curbe.offset;

	/* VME MV/Mb cost table is passed by using const buffer */
	/* Now it uses the fixed search path. So it is constructed directly
	 * in the GPU shader.
	 */
	memcpy(constant_buffer, (char *)vme_context->vme_state_message, 128);

	dri_bo_unmap(vme_context->gpe_context.curbe.bo);

	return VA_STATUS_SUCCESS;
}

#define     MB_SCOREBOARD_A     (1 << 0)
#define     MB_SCOREBOARD_B     (1 << 1)
#define     MB_SCOREBOARD_C     (1 << 2)

/* check whether the mb of (x_index, y_index) is out of bound */
static inline int loop_in_bounds(int x_index, int y_index, int first_mb, int num_mb, int mb_width, int mb_height)
{
	int mb_index;
	if (x_index < 0 || x_index >= mb_width)
		return -1;
	if (y_index < 0 || y_index >= mb_height)
		return -1;

	mb_index = y_index * mb_width + x_index;
	if (mb_index < first_mb || mb_index > (first_mb + num_mb))
		return -1;
	return 0;
}

static void
gen9wa_vme_walker_fill_vme_batchbuffer(VADriverContextP ctx,
									   struct encode_state *encode_state,
									   int mb_width, int mb_height,
									   int kernel,
									   int transform_8x8_mode_flag,
									   struct intel_encoder_context *encoder_context)
{
	struct gen6_vme_context *vme_context = encoder_context->vme_context;
	int mb_row;
	int s;
	unsigned int *command_ptr;

#define     USE_SCOREBOARD      (1 << 21)

	dri_bo_map(vme_context->vme_batchbuffer.bo, 1);
	command_ptr = vme_context->vme_batchbuffer.bo->virtual;

	for (s = 0; s < encode_state->num_slice_params_ext; s++) {
		VAEncSliceParameterBufferH264 *pSliceParameter = (VAEncSliceParameterBufferH264 *)encode_state->slice_params_ext[s]->buffer;
		int first_mb = pSliceParameter->macroblock_address;
		int num_mb = pSliceParameter->num_macroblocks;
		unsigned int mb_intra_ub, score_dep;
		int x_outer, y_outer, x_inner, y_inner;
		int xtemp_outer = 0;

		x_outer = first_mb % mb_width;
		y_outer = first_mb / mb_width;
		mb_row = y_outer;

		for (; x_outer < (mb_width - 2) && !loop_in_bounds(x_outer, y_outer, first_mb, num_mb, mb_width, mb_height);) {
			x_inner = x_outer;
			y_inner = y_outer;
			for (; !loop_in_bounds(x_inner, y_inner, first_mb, num_mb, mb_width, mb_height);) {
				mb_intra_ub = 0;
				score_dep = 0;
				if (x_inner != 0) {
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_AE;
					score_dep |= MB_SCOREBOARD_A;
				}
				if (y_inner != mb_row) {
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_B;
					score_dep |= MB_SCOREBOARD_B;
					if (x_inner != 0)
						mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_D;
					if (x_inner != (mb_width - 1)) {
						mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_C;
						score_dep |= MB_SCOREBOARD_C;
					}
				}

				*command_ptr++ = (CMD_MEDIA_OBJECT | (8 - 2));
				*command_ptr++ = kernel;
				*command_ptr++ = USE_SCOREBOARD;
				/* Indirect data */
				*command_ptr++ = 0;
				/* the (X, Y) term of scoreboard */
				*command_ptr++ = ((y_inner << 16) | x_inner);
				*command_ptr++ = score_dep;
				/*inline data */
				*command_ptr++ = (mb_width << 16 | y_inner << 8 | x_inner);
				*command_ptr++ = ((1 << 18) | (1 << 16) | transform_8x8_mode_flag | (mb_intra_ub << 8));
				*command_ptr++ = CMD_MEDIA_STATE_FLUSH;
				*command_ptr++ = 0;

				x_inner -= 2;
				y_inner += 1;
			}
			x_outer += 1;
		}

		xtemp_outer = mb_width - 2;
		if (xtemp_outer < 0)
			xtemp_outer = 0;
		x_outer = xtemp_outer;
		y_outer = first_mb / mb_width;
		for (; !loop_in_bounds(x_outer, y_outer, first_mb, num_mb, mb_width, mb_height);) {
			y_inner = y_outer;
			x_inner = x_outer;
			for (; !loop_in_bounds(x_inner, y_inner, first_mb, num_mb, mb_width, mb_height);) {
				mb_intra_ub = 0;
				score_dep = 0;
				if (x_inner != 0) {
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_AE;
					score_dep |= MB_SCOREBOARD_A;
				}
				if (y_inner != mb_row) {
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_B;
					score_dep |= MB_SCOREBOARD_B;
					if (x_inner != 0)
						mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_D;

					if (x_inner != (mb_width - 1)) {
						mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_C;
						score_dep |= MB_SCOREBOARD_C;
					}
				}

				*command_ptr++ = (CMD_MEDIA_OBJECT | (8 - 2));
				*command_ptr++ = kernel;
				*command_ptr++ = USE_SCOREBOARD;
				/* Indirect data */
				*command_ptr++ = 0;
				/* the (X, Y) term of scoreboard */
				*command_ptr++ = ((y_inner << 16) | x_inner);
				*command_ptr++ = score_dep;
				/*inline data */
				*command_ptr++ = (mb_width << 16 | y_inner << 8 | x_inner);
				*command_ptr++ = ((1 << 18) | (1 << 16) | transform_8x8_mode_flag | (mb_intra_ub << 8));

				*command_ptr++ = CMD_MEDIA_STATE_FLUSH;
				*command_ptr++ = 0;
				x_inner -= 2;
				y_inner += 1;
			}
			x_outer++;
			if (x_outer >= mb_width) {
				y_outer += 1;
				x_outer = xtemp_outer;
			}
		}
	}

	*command_ptr++ = MI_BATCH_BUFFER_END;
	*command_ptr++ = 0;

	dri_bo_unmap(vme_context->vme_batchbuffer.bo);
}

static void
gen9_vme_fill_vme_batchbuffer(VADriverContextP ctx,
							  struct encode_state *encode_state,
							  int mb_width, int mb_height,
							  int kernel,
							  int transform_8x8_mode_flag,
							  struct intel_encoder_context *encoder_context)
{
	struct gen6_vme_context *vme_context = encoder_context->vme_context;
	int mb_x = 0, mb_y = 0;
	int i, s;
	unsigned int *command_ptr;
	struct gen6_mfc_context *mfc_context = encoder_context->mfc_context;
	VAEncPictureParameterBufferH264 *pic_param = (VAEncPictureParameterBufferH264 *)encode_state->pic_param_ext->buffer;
	VAEncSliceParameterBufferH264 *slice_param = (VAEncSliceParameterBufferH264 *)encode_state->slice_params_ext[0]->buffer;
	int qp;
	int slice_type = intel_avc_enc_slice_type_fixup(slice_param->slice_type);
	int qp_mb, qp_index;

	if (encoder_context->rate_control_mode == VA_RC_CQP)
		qp = pic_param->pic_init_qp + slice_param->slice_qp_delta;
	else
		qp = mfc_context->brc.qp_prime_y[encoder_context->layer.curr_frame_layer_id][slice_type];

	dri_bo_map(vme_context->vme_batchbuffer.bo, 1);
	command_ptr = vme_context->vme_batchbuffer.bo->virtual;

	for (s = 0; s < encode_state->num_slice_params_ext; s++) {
		VAEncSliceParameterBufferH264 *pSliceParameter = (VAEncSliceParameterBufferH264 *)encode_state->slice_params_ext[s]->buffer;
		int slice_mb_begin = pSliceParameter->macroblock_address;
		int slice_mb_number = pSliceParameter->num_macroblocks;
		unsigned int mb_intra_ub;
		int slice_mb_x = pSliceParameter->macroblock_address % mb_width;
		for (i = 0; i < slice_mb_number;) {
			int mb_count = i + slice_mb_begin;
			mb_x = mb_count % mb_width;
			mb_y = mb_count / mb_width;
			mb_intra_ub = 0;
			if (mb_x != 0) {
				mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_AE;
			}
			if (mb_y != 0) {
				mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_B;
				if (mb_x != 0)
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_D;
				if (mb_x != (mb_width - 1))
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_C;
			}
			if (i < mb_width) {
				if (i == 0)
					mb_intra_ub &= ~(INTRA_PRED_AVAIL_FLAG_AE);
				mb_intra_ub &= ~(INTRA_PRED_AVAIL_FLAG_BCD_MASK);
				if ((i == (mb_width - 1)) && slice_mb_x) {
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_C;
				}
			}

			if ((i == mb_width) && slice_mb_x) {
				mb_intra_ub &= ~(INTRA_PRED_AVAIL_FLAG_D);
			}
			*command_ptr++ = (CMD_MEDIA_OBJECT | (9 - 2));
			*command_ptr++ = kernel;
			*command_ptr++ = 0;
			*command_ptr++ = 0;
			*command_ptr++ = 0;
			*command_ptr++ = 0;

			/*inline data */
			*command_ptr++ = (mb_width << 16 | mb_y << 8 | mb_x);
			*command_ptr++ = ((encoder_context->quality_level << 24) | (1 << 16) | transform_8x8_mode_flag | (mb_intra_ub << 8));
			/* qp occupies one byte */
			if (vme_context->roi_enabled) {
				qp_index = mb_y * mb_width + mb_x;
				qp_mb = *(vme_context->qp_per_mb + qp_index);
			} else
				qp_mb = qp;
			*command_ptr++ = qp_mb;

			*command_ptr++ = CMD_MEDIA_STATE_FLUSH;
			*command_ptr++ = 0;
			i += 1;
		}
	}

	*command_ptr++ = MI_BATCH_BUFFER_END;
	*command_ptr++ = 0;

	dri_bo_unmap(vme_context->vme_batchbuffer.bo);
}

static void gen9_vme_media_init(VADriverContextP ctx, struct intel_encoder_context *encoder_context)
{
	struct gen6_vme_context *vme_context = encoder_context->vme_context;

	gen8_gpe_context_init(ctx, &vme_context->gpe_context);

	/* VME output buffer */
	dri_bo_unreference(vme_context->vme_output.bo);
	vme_context->vme_output.bo = NULL;

	dri_bo_unreference(vme_context->vme_batchbuffer.bo);
	vme_context->vme_batchbuffer.bo = NULL;

	/* VME state */
	dri_bo_unreference(vme_context->vme_state.bo);
	vme_context->vme_state.bo = NULL;
}

static void gen9_vme_pipeline_programing(VADriverContextP ctx,
										 struct encode_state *encode_state,
										 struct intel_encoder_context *encoder_context)
{
	struct gen6_vme_context *vme_context = encoder_context->vme_context;
	struct intel_batchbuffer *batch = encoder_context->base.batch;
	VAEncPictureParameterBufferH264 *pPicParameter = (VAEncPictureParameterBufferH264 *)encode_state->pic_param_ext->buffer;
	VAEncSliceParameterBufferH264 *pSliceParameter = (VAEncSliceParameterBufferH264 *)encode_state->slice_params_ext[0]->buffer;
	VAEncSequenceParameterBufferH264 *pSequenceParameter = (VAEncSequenceParameterBufferH264 *)encode_state->seq_param_ext->buffer;
	int width_in_mbs = pSequenceParameter->picture_width_in_mbs;
	int height_in_mbs = pSequenceParameter->picture_height_in_mbs;
	int kernel_shader;
	bool allow_hwscore = true;
	int s;
	unsigned int is_low_quality = (encoder_context->quality_level == ENCODER_LOW_QUALITY);

	if (is_low_quality)
		allow_hwscore = false;
	else {
		for (s = 0; s < encode_state->num_slice_params_ext; s++) {
			pSliceParameter = (VAEncSliceParameterBufferH264 *)encode_state->slice_params_ext[s]->buffer;
			if ((pSliceParameter->macroblock_address % width_in_mbs)) {
				allow_hwscore = false;
				break;
			}
		}
	}

	if ((pSliceParameter->slice_type == SLICE_TYPE_I) ||
		(pSliceParameter->slice_type == SLICE_TYPE_SI)) {
		kernel_shader = VME_INTRA_SHADER;
	} else if ((pSliceParameter->slice_type == SLICE_TYPE_P) ||
			   (pSliceParameter->slice_type == SLICE_TYPE_SP)) {
		kernel_shader = VME_INTER_SHADER;
	} else {
		kernel_shader = VME_BINTER_SHADER;
		if (!allow_hwscore)
			kernel_shader = VME_INTER_SHADER;
	}
	if (allow_hwscore)
		gen9wa_vme_walker_fill_vme_batchbuffer(ctx,
											   encode_state,
											   width_in_mbs, height_in_mbs,
											   kernel_shader,
											   pPicParameter->pic_fields.bits.transform_8x8_mode_flag,
											   encoder_context);
	else
		gen9_vme_fill_vme_batchbuffer(ctx,
									  encode_state,
									  width_in_mbs, height_in_mbs,
									  kernel_shader,
									  pPicParameter->pic_fields.bits.transform_8x8_mode_flag,
									  encoder_context);

	intel_batchbuffer_start_atomic(batch, 0x1000);
	gen9_gpe_pipeline_setup(ctx, &vme_context->gpe_context, batch);
	BEGIN_BATCH(batch, 3);
	OUT_BATCH(batch, MI_BATCH_BUFFER_START | (1 << 8) | (1 << 0));
	OUT_RELOC64(batch,
				vme_context->vme_batchbuffer.bo,
				I915_GEM_DOMAIN_COMMAND, 0,
				0);
	ADVANCE_BATCH(batch);

	gen9_gpe_pipeline_end(ctx, &vme_context->gpe_context, batch);

	intel_batchbuffer_end_atomic(batch);
}

static VAStatus gen9_vme_prepare(VADriverContextP ctx,
								 struct encode_state *encode_state,
								 struct intel_encoder_context *encoder_context)
{
	VAStatus vaStatus = VA_STATUS_SUCCESS;
	VAEncSliceParameterBufferH264 *pSliceParameter = (VAEncSliceParameterBufferH264 *)encode_state->slice_params_ext[0]->buffer;
	int is_intra = pSliceParameter->slice_type == SLICE_TYPE_I;
	VAEncSequenceParameterBufferH264 *pSequenceParameter = (VAEncSequenceParameterBufferH264 *)encode_state->seq_param_ext->buffer;
	struct gen6_vme_context *vme_context = encoder_context->vme_context;

	if (!vme_context->h264_level ||
		(vme_context->h264_level != pSequenceParameter->level_idc)) {
		vme_context->h264_level = pSequenceParameter->level_idc;
	}

	intel_vme_update_mbmv_cost(ctx, encode_state, encoder_context);
	intel_h264_initialize_mbmv_cost(ctx, encode_state, encoder_context);
	intel_h264_enc_roi_config(ctx, encode_state, encoder_context);

	/*Setup all the memory object*/
	gen9_vme_surface_setup(ctx, encode_state, is_intra, encoder_context);
	gen9_vme_interface_setup(ctx, encode_state, encoder_context);
	//gen9_vme_vme_state_setup(ctx, encode_state, is_intra, encoder_context);
	gen9_vme_constant_setup(ctx, encode_state, encoder_context, (pSliceParameter->slice_type == SLICE_TYPE_B) ? 2 : 1);

	/*Programing media pipeline*/
	gen9_vme_pipeline_programing(ctx, encode_state, encoder_context);

	return vaStatus;
}

static VAStatus gen9_vme_run(VADriverContextP ctx,
							 struct encode_state *encode_state,
							 struct intel_encoder_context *encoder_context)
{
	struct intel_batchbuffer *batch = encoder_context->base.batch;

	intel_batchbuffer_flush(batch);

	return VA_STATUS_SUCCESS;
}

static VAStatus gen9_vme_stop(VADriverContextP ctx,
							  struct encode_state *encode_state,
							  struct intel_encoder_context *encoder_context)
{
	return VA_STATUS_SUCCESS;
}

static VAStatus
gen9_vme_pipeline(VADriverContextP ctx,
				  VAProfile profile,
				  struct encode_state *encode_state,
				  struct intel_encoder_context *encoder_context)
{
	gen9_vme_media_init(ctx, encoder_context);
	gen9_vme_prepare(ctx, encode_state, encoder_context);
	gen9_vme_run(ctx, encode_state, encoder_context);
	gen9_vme_stop(ctx, encode_state, encoder_context);

	return VA_STATUS_SUCCESS;
}

static void
gen9_vme_mpeg2_output_buffer_setup(VADriverContextP ctx,
								   struct encode_state *encode_state,
								   int index,
								   int is_intra,
								   struct intel_encoder_context *encoder_context)

{
	VAEncSequenceParameterBufferMPEG2 *seq_param = (VAEncSequenceParameterBufferMPEG2 *)encode_state->seq_param_ext->buffer;
	int width_in_mbs = ALIGN(seq_param->picture_width, 16) / 16;
	int height_in_mbs = ALIGN(seq_param->picture_height, 16) / 16;

	gen9_vme_output_buffer_setup(ctx, encode_state, index, encoder_context, is_intra, width_in_mbs, height_in_mbs);
}

static void
gen9_vme_mpeg2_output_vme_batchbuffer_setup(VADriverContextP ctx,
											struct encode_state *encode_state,
											int index,
											struct intel_encoder_context *encoder_context)

{
	VAEncSequenceParameterBufferMPEG2 *seq_param = (VAEncSequenceParameterBufferMPEG2 *)encode_state->seq_param_ext->buffer;
	int width_in_mbs = ALIGN(seq_param->picture_width, 16) / 16;
	int height_in_mbs = ALIGN(seq_param->picture_height, 16) / 16;

	gen9_vme_output_vme_batchbuffer_setup(ctx, encode_state, index, encoder_context, width_in_mbs, height_in_mbs);
}

static VAStatus
gen9_vme_mpeg2_surface_setup(VADriverContextP ctx,
							 struct encode_state *encode_state,
							 int is_intra,
							 struct intel_encoder_context *encoder_context)
{
	struct object_surface *obj_surface;

	/*Setup surfaces state*/
	/* current picture for encoding */
	obj_surface = encode_state->input_yuv_object;
	gen9_vme_source_surface_state(ctx, 0, obj_surface, encoder_context);
	gen9_vme_media_source_surface_state(ctx, 4, obj_surface, encoder_context);
	gen9_vme_media_chroma_source_surface_state(ctx, 6, obj_surface, encoder_context);

	if (!is_intra) {
		/* reference 0 */
		obj_surface = encode_state->reference_objects[0];

		if (obj_surface->bo != NULL)
			gen9_vme_source_surface_state(ctx, 1, obj_surface, encoder_context);

		/* reference 1 */
		obj_surface = encode_state->reference_objects[1];

		if (obj_surface && obj_surface->bo != NULL)
			gen9_vme_source_surface_state(ctx, 2, obj_surface, encoder_context);
	}

	/* VME output */
	gen9_vme_mpeg2_output_buffer_setup(ctx, encode_state, 3, is_intra, encoder_context);
	gen9_vme_mpeg2_output_vme_batchbuffer_setup(ctx, encode_state, 5, encoder_context);

	return VA_STATUS_SUCCESS;
}

static void
gen9wa_vme_mpeg2_walker_fill_vme_batchbuffer(VADriverContextP ctx,
											 struct encode_state *encode_state,
											 int mb_width, int mb_height,
											 int kernel,
											 struct intel_encoder_context *encoder_context)
{
	struct gen6_vme_context *vme_context = encoder_context->vme_context;
	unsigned int *command_ptr;

#define     MPEG2_SCOREBOARD        (1 << 21)

	dri_bo_map(vme_context->vme_batchbuffer.bo, 1);
	command_ptr = vme_context->vme_batchbuffer.bo->virtual;

	{
		unsigned int mb_intra_ub, score_dep;
		int x_outer, y_outer, x_inner, y_inner;
		int xtemp_outer = 0;
		int first_mb = 0;
		int num_mb = mb_width * mb_height;

		x_outer = 0;
		y_outer = 0;

		for (; x_outer < (mb_width - 2) && !loop_in_bounds(x_outer, y_outer, first_mb, num_mb, mb_width, mb_height);) {
			x_inner = x_outer;
			y_inner = y_outer;
			for (; !loop_in_bounds(x_inner, y_inner, first_mb, num_mb, mb_width, mb_height);) {
				mb_intra_ub = 0;
				score_dep = 0;
				if (x_inner != 0) {
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_AE;
					score_dep |= MB_SCOREBOARD_A;
				}
				if (y_inner != 0) {
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_B;
					score_dep |= MB_SCOREBOARD_B;

					if (x_inner != 0)
						mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_D;

					if (x_inner != (mb_width - 1)) {
						mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_C;
						score_dep |= MB_SCOREBOARD_C;
					}
				}

				*command_ptr++ = (CMD_MEDIA_OBJECT | (8 - 2));
				*command_ptr++ = kernel;
				*command_ptr++ = MPEG2_SCOREBOARD;
				/* Indirect data */
				*command_ptr++ = 0;
				/* the (X, Y) term of scoreboard */
				*command_ptr++ = ((y_inner << 16) | x_inner);
				*command_ptr++ = score_dep;
				/*inline data */
				*command_ptr++ = (mb_width << 16 | y_inner << 8 | x_inner);
				*command_ptr++ = ((1 << 18) | (1 << 16) | (mb_intra_ub << 8));
				*command_ptr++ = CMD_MEDIA_STATE_FLUSH;
				*command_ptr++ = 0;

				x_inner -= 2;
				y_inner += 1;
			}
			x_outer += 1;
		}

		xtemp_outer = mb_width - 2;
		if (xtemp_outer < 0)
			xtemp_outer = 0;
		x_outer = xtemp_outer;
		y_outer = 0;
		for (; !loop_in_bounds(x_outer, y_outer, first_mb, num_mb, mb_width, mb_height);) {
			y_inner = y_outer;
			x_inner = x_outer;
			for (; !loop_in_bounds(x_inner, y_inner, first_mb, num_mb, mb_width, mb_height);) {
				mb_intra_ub = 0;
				score_dep = 0;
				if (x_inner != 0) {
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_AE;
					score_dep |= MB_SCOREBOARD_A;
				}
				if (y_inner != 0) {
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_B;
					score_dep |= MB_SCOREBOARD_B;

					if (x_inner != 0)
						mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_D;

					if (x_inner != (mb_width - 1)) {
						mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_C;
						score_dep |= MB_SCOREBOARD_C;
					}
				}

				*command_ptr++ = (CMD_MEDIA_OBJECT | (8 - 2));
				*command_ptr++ = kernel;
				*command_ptr++ = MPEG2_SCOREBOARD;
				/* Indirect data */
				*command_ptr++ = 0;
				/* the (X, Y) term of scoreboard */
				*command_ptr++ = ((y_inner << 16) | x_inner);
				*command_ptr++ = score_dep;
				/*inline data */
				*command_ptr++ = (mb_width << 16 | y_inner << 8 | x_inner);
				*command_ptr++ = ((1 << 18) | (1 << 16) | (mb_intra_ub << 8));

				*command_ptr++ = CMD_MEDIA_STATE_FLUSH;
				*command_ptr++ = 0;
				x_inner -= 2;
				y_inner += 1;
			}
			x_outer++;
			if (x_outer >= mb_width) {
				y_outer += 1;
				x_outer = xtemp_outer;
			}
		}
	}

	*command_ptr++ = MI_BATCH_BUFFER_END;
	*command_ptr++ = 0;

	dri_bo_unmap(vme_context->vme_batchbuffer.bo);
	return;
}

static void
gen9_vme_mpeg2_fill_vme_batchbuffer(VADriverContextP ctx,
									struct encode_state *encode_state,
									int mb_width, int mb_height,
									int kernel,
									int transform_8x8_mode_flag,
									struct intel_encoder_context *encoder_context)
{
	struct gen6_vme_context *vme_context = encoder_context->vme_context;
	int mb_x = 0, mb_y = 0;
	int i, s, j;
	unsigned int *command_ptr;


	dri_bo_map(vme_context->vme_batchbuffer.bo, 1);
	command_ptr = vme_context->vme_batchbuffer.bo->virtual;

	for (s = 0; s < encode_state->num_slice_params_ext; s++) {
		VAEncSliceParameterBufferMPEG2 *slice_param = (VAEncSliceParameterBufferMPEG2 *)encode_state->slice_params_ext[s]->buffer;

		for (j = 0; j < encode_state->slice_params_ext[s]->num_elements; j++) {
			int slice_mb_begin = slice_param->macroblock_address;
			int slice_mb_number = slice_param->num_macroblocks;
			unsigned int mb_intra_ub;

			for (i = 0; i < slice_mb_number;) {
				int mb_count = i + slice_mb_begin;

				mb_x = mb_count % mb_width;
				mb_y = mb_count / mb_width;
				mb_intra_ub = 0;

				if (mb_x != 0) {
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_AE;
				}

				if (mb_y != 0) {
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_B;

					if (mb_x != 0)
						mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_D;

					if (mb_x != (mb_width - 1))
						mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_C;
				}

				*command_ptr++ = (CMD_MEDIA_OBJECT | (8 - 2));
				*command_ptr++ = kernel;
				*command_ptr++ = 0;
				*command_ptr++ = 0;
				*command_ptr++ = 0;
				*command_ptr++ = 0;

				/*inline data */
				*command_ptr++ = (mb_width << 16 | mb_y << 8 | mb_x);
				*command_ptr++ = ((1 << 16) | transform_8x8_mode_flag | (mb_intra_ub << 8));

				*command_ptr++ = CMD_MEDIA_STATE_FLUSH;
				*command_ptr++ = 0;
				i += 1;
			}

			slice_param++;
		}
	}

	*command_ptr++ = MI_BATCH_BUFFER_END;
	*command_ptr++ = 0;

	dri_bo_unmap(vme_context->vme_batchbuffer.bo);
}

static void
gen9_vme_mpeg2_pipeline_programing(VADriverContextP ctx,
								   struct encode_state *encode_state,
								   int is_intra,
								   struct intel_encoder_context *encoder_context)
{
	struct gen6_vme_context *vme_context = encoder_context->vme_context;
	struct intel_batchbuffer *batch = encoder_context->base.batch;
	VAEncSequenceParameterBufferMPEG2 *seq_param = (VAEncSequenceParameterBufferMPEG2 *)encode_state->seq_param_ext->buffer;
	int width_in_mbs = ALIGN(seq_param->picture_width, 16) / 16;
	int height_in_mbs = ALIGN(seq_param->picture_height, 16) / 16;
	bool allow_hwscore = true;
	int s;
	int kernel_shader;
	VAEncPictureParameterBufferMPEG2 *pic_param = NULL;

	for (s = 0; s < encode_state->num_slice_params_ext; s++) {
		int j;
		VAEncSliceParameterBufferMPEG2 *slice_param = (VAEncSliceParameterBufferMPEG2 *)encode_state->slice_params_ext[s]->buffer;

		for (j = 0; j < encode_state->slice_params_ext[s]->num_elements; j++) {
			if (slice_param->macroblock_address % width_in_mbs) {
				allow_hwscore = false;
				break;
			}
		}
	}

	pic_param = (VAEncPictureParameterBufferMPEG2 *)encode_state->pic_param_ext->buffer;
	if (pic_param->picture_type == VAEncPictureTypeIntra) {
		allow_hwscore = false;
		kernel_shader = VME_INTRA_SHADER;
	} else {
		kernel_shader = VME_INTER_SHADER;
	}

	if (allow_hwscore)
		gen9wa_vme_mpeg2_walker_fill_vme_batchbuffer(ctx,
													 encode_state,
													 width_in_mbs, height_in_mbs,
													 kernel_shader,
													 encoder_context);
	else
		gen9_vme_mpeg2_fill_vme_batchbuffer(ctx,
											encode_state,
											width_in_mbs, height_in_mbs,
											is_intra ? VME_INTRA_SHADER : VME_INTER_SHADER,
											0,
											encoder_context);

	intel_batchbuffer_start_atomic(batch, 0x1000);
	gen9_gpe_pipeline_setup(ctx, &vme_context->gpe_context, batch);
	BEGIN_BATCH(batch, 4);
	OUT_BATCH(batch, MI_BATCH_BUFFER_START | (1 << 8) | (1 << 0));
	OUT_RELOC64(batch,
				vme_context->vme_batchbuffer.bo,
				I915_GEM_DOMAIN_COMMAND, 0,
				0);
	OUT_BATCH(batch, 0);
	ADVANCE_BATCH(batch);

	gen9_gpe_pipeline_end(ctx, &vme_context->gpe_context, batch);

	intel_batchbuffer_end_atomic(batch);
}

static VAStatus
gen9_vme_mpeg2_prepare(VADriverContextP ctx,
					   struct encode_state *encode_state,
					   struct intel_encoder_context *encoder_context)
{
	VAStatus vaStatus = VA_STATUS_SUCCESS;
	VAEncSliceParameterBufferMPEG2 *slice_param = (VAEncSliceParameterBufferMPEG2 *)encode_state->slice_params_ext[0]->buffer;
	VAEncSequenceParameterBufferMPEG2 *seq_param = (VAEncSequenceParameterBufferMPEG2 *)encode_state->seq_param_ext->buffer;
	struct gen6_vme_context *vme_context = encoder_context->vme_context;

	if ((!vme_context->mpeg2_level) ||
		(vme_context->mpeg2_level != (seq_param->sequence_extension.bits.profile_and_level_indication & MPEG2_LEVEL_MASK))) {
		vme_context->mpeg2_level = seq_param->sequence_extension.bits.profile_and_level_indication & MPEG2_LEVEL_MASK;
	}

	/*Setup all the memory object*/
	gen9_vme_mpeg2_surface_setup(ctx, encode_state, slice_param->is_intra_slice, encoder_context);
	gen9_vme_interface_setup(ctx, encode_state, encoder_context);
	//gen9_vme_vme_state_setup(ctx, encode_state, slice_param->is_intra_slice, encoder_context);
	intel_vme_mpeg2_state_setup(ctx, encode_state, encoder_context);
	gen9_vme_constant_setup(ctx, encode_state, encoder_context, 1);

	/*Programing media pipeline*/
	gen9_vme_mpeg2_pipeline_programing(ctx, encode_state, slice_param->is_intra_slice, encoder_context);

	return vaStatus;
}

static VAStatus
gen9_vme_mpeg2_pipeline(VADriverContextP ctx,
						VAProfile profile,
						struct encode_state *encode_state,
						struct intel_encoder_context *encoder_context)
{
	gen9_vme_media_init(ctx, encoder_context);
	gen9_vme_mpeg2_prepare(ctx, encode_state, encoder_context);
	gen9_vme_run(ctx, encode_state, encoder_context);
	gen9_vme_stop(ctx, encode_state, encoder_context);

	return VA_STATUS_SUCCESS;
}

static void
gen9_vme_vp8_output_buffer_setup(VADriverContextP ctx,
								 struct encode_state *encode_state,
								 int index,
								 int is_intra,
								 struct intel_encoder_context *encoder_context)
{
	VAEncSequenceParameterBufferVP8 *seq_param = (VAEncSequenceParameterBufferVP8 *)encode_state->seq_param_ext->buffer;
	int width_in_mbs = ALIGN(seq_param->frame_width, 16) / 16;
	int height_in_mbs = ALIGN(seq_param->frame_height, 16) / 16;

	gen9_vme_output_buffer_setup(ctx, encode_state, index, encoder_context, is_intra, width_in_mbs, height_in_mbs);
}

static void
gen9_vme_vp8_output_vme_batchbuffer_setup(VADriverContextP ctx,
										  struct encode_state *encode_state,
										  int index,
										  struct intel_encoder_context *encoder_context)
{
	VAEncSequenceParameterBufferVP8 *seq_param = (VAEncSequenceParameterBufferVP8 *)encode_state->seq_param_ext->buffer;
	int width_in_mbs = ALIGN(seq_param->frame_width, 16) / 16;
	int height_in_mbs = ALIGN(seq_param->frame_height, 16) / 16;

	gen9_vme_output_vme_batchbuffer_setup(ctx, encode_state, index, encoder_context, width_in_mbs, height_in_mbs);
}

static VAStatus
gen9_vme_vp8_surface_setup(VADriverContextP ctx,
						   struct encode_state *encode_state,
						   int is_intra,
						   struct intel_encoder_context *encoder_context)
{
	struct object_surface *obj_surface;

	/*Setup surfaces state*/
	/* current picture for encoding */
	obj_surface = encode_state->input_yuv_object;
	gen9_vme_source_surface_state(ctx, 0, obj_surface, encoder_context);
	gen9_vme_media_source_surface_state(ctx, 4, obj_surface, encoder_context);
	gen9_vme_media_chroma_source_surface_state(ctx, 6, obj_surface, encoder_context);

	if (!is_intra) {
		/* reference 0 */
		obj_surface = encode_state->reference_objects[0];

		if (obj_surface->bo != NULL)
			gen9_vme_source_surface_state(ctx, 1, obj_surface, encoder_context);

		/* reference 1 */
		obj_surface = encode_state->reference_objects[1];

		if (obj_surface && obj_surface->bo != NULL)
			gen9_vme_source_surface_state(ctx, 2, obj_surface, encoder_context);
	}

	/* VME output */
	gen9_vme_vp8_output_buffer_setup(ctx, encode_state, 3, is_intra, encoder_context);
	gen9_vme_vp8_output_vme_batchbuffer_setup(ctx, encode_state, 5, encoder_context);

	return VA_STATUS_SUCCESS;
}

static void
gen9_vme_vp8_pipeline_programing(VADriverContextP ctx,
								 struct encode_state *encode_state,
								 int is_intra,
								 struct intel_encoder_context *encoder_context)
{
	struct gen6_vme_context *vme_context = encoder_context->vme_context;
	struct intel_batchbuffer *batch = encoder_context->base.batch;
	VAEncSequenceParameterBufferVP8 *seq_param = (VAEncSequenceParameterBufferVP8 *)encode_state->seq_param_ext->buffer;
	int width_in_mbs = ALIGN(seq_param->frame_width, 16) / 16;
	int height_in_mbs = ALIGN(seq_param->frame_height, 16) / 16;
	int kernel_shader = (is_intra ? VME_INTRA_SHADER : VME_INTER_SHADER);

	gen9wa_vme_mpeg2_walker_fill_vme_batchbuffer(ctx,
												 encode_state,
												 width_in_mbs, height_in_mbs,
												 kernel_shader,
												 encoder_context);

	intel_batchbuffer_start_atomic(batch, 0x1000);
	gen9_gpe_pipeline_setup(ctx, &vme_context->gpe_context, batch);
	BEGIN_BATCH(batch, 4);
	OUT_BATCH(batch, MI_BATCH_BUFFER_START | (1 << 8) | (1 << 0));
	OUT_RELOC64(batch,
				vme_context->vme_batchbuffer.bo,
				I915_GEM_DOMAIN_COMMAND, 0,
				0);
	OUT_BATCH(batch, 0);
	ADVANCE_BATCH(batch);

	gen9_gpe_pipeline_end(ctx, &vme_context->gpe_context, batch);

	intel_batchbuffer_end_atomic(batch);
}

static VAStatus gen9_vme_vp8_prepare(VADriverContextP ctx,
									 struct encode_state *encode_state,
									 struct intel_encoder_context *encoder_context)
{
	VAStatus vaStatus = VA_STATUS_SUCCESS;
	VAEncPictureParameterBufferVP8 *pPicParameter = (VAEncPictureParameterBufferVP8 *)encode_state->pic_param_ext->buffer;
	int is_intra = !pPicParameter->pic_flags.bits.frame_type;

	/* update vp8 mbmv cost */
	intel_vme_vp8_update_mbmv_cost(ctx, encode_state, encoder_context);

	/*Setup all the memory object*/
	gen9_vme_vp8_surface_setup(ctx, encode_state, is_intra, encoder_context);
	gen9_vme_interface_setup(ctx, encode_state, encoder_context);
	gen9_vme_constant_setup(ctx, encode_state, encoder_context, 1);

	/*Programing media pipeline*/
	gen9_vme_vp8_pipeline_programing(ctx, encode_state, is_intra, encoder_context);

	return vaStatus;
}

static VAStatus
gen9_vme_vp8_pipeline(VADriverContextP ctx,
					  VAProfile profile,
					  struct encode_state *encode_state,
					  struct intel_encoder_context *encoder_context)
{
	gen9_vme_media_init(ctx, encoder_context);
	gen9_vme_vp8_prepare(ctx, encode_state, encoder_context);
	gen9_vme_run(ctx, encode_state, encoder_context);
	gen9_vme_stop(ctx, encode_state, encoder_context);

	return VA_STATUS_SUCCESS;
}

/* HEVC */

static void
gen9_vme_hevc_output_buffer_setup(VADriverContextP ctx,
								  struct encode_state *encode_state,
								  int index,
								  struct intel_encoder_context *encoder_context)

{
	struct i965_driver_data *i965 = i965_driver_data(ctx);
	struct gen6_vme_context *vme_context = encoder_context->vme_context;
	VAEncSequenceParameterBufferHEVC *pSequenceParameter = (VAEncSequenceParameterBufferHEVC *)encode_state->seq_param_ext->buffer;
	VAEncSliceParameterBufferHEVC *pSliceParameter = (VAEncSliceParameterBufferHEVC *)encode_state->slice_params_ext[0]->buffer;
	int is_intra = pSliceParameter->slice_type == HEVC_SLICE_I;
	int width_in_mbs = (pSequenceParameter->pic_width_in_luma_samples + 15) / 16;
	int height_in_mbs = (pSequenceParameter->pic_height_in_luma_samples + 15) / 16;


	vme_context->vme_output.num_blocks = width_in_mbs * height_in_mbs;
	vme_context->vme_output.pitch = 16; /* in bytes, always 16 */

	if (is_intra)
		vme_context->vme_output.size_block = INTRA_VME_OUTPUT_IN_BYTES * 2;
	else
		vme_context->vme_output.size_block = INTRA_VME_OUTPUT_IN_BYTES * 24;
	/*
	 * Inter MV . 32-byte Intra search + 16 IME info + 128 IME MV + 32 IME Ref
	 * + 16 FBR Info + 128 FBR MV + 32 FBR Ref.
	 * 16 * (2 + 2 * (1 + 8 + 2))= 16 * 24.
	 */

	vme_context->vme_output.bo = dri_bo_alloc(i965->intel.bufmgr,
											  "VME output buffer",
											  vme_context->vme_output.num_blocks * vme_context->vme_output.size_block,
											  0x1000);
	assert(vme_context->vme_output.bo);
	vme_context->vme_buffer_suface_setup(ctx,
										 &vme_context->gpe_context,
										 &vme_context->vme_output,
										 BINDING_TABLE_OFFSET(index),
										 SURFACE_STATE_OFFSET(index));
}

static void
gen9_vme_hevc_output_vme_batchbuffer_setup(VADriverContextP ctx,
										   struct encode_state *encode_state,
										   int index,
										   struct intel_encoder_context *encoder_context)

{
	struct i965_driver_data *i965 = i965_driver_data(ctx);
	struct gen6_vme_context *vme_context = encoder_context->vme_context;
	VAEncSequenceParameterBufferHEVC *pSequenceParameter = (VAEncSequenceParameterBufferHEVC *)encode_state->seq_param_ext->buffer;
	int width_in_mbs = (pSequenceParameter->pic_width_in_luma_samples + 15) / 16;
	int height_in_mbs = (pSequenceParameter->pic_height_in_luma_samples + 15) / 16;

	vme_context->vme_batchbuffer.num_blocks = width_in_mbs * height_in_mbs + 1;
	vme_context->vme_batchbuffer.size_block = 64; /* 4 OWORDs */
	vme_context->vme_batchbuffer.pitch = 16;
	vme_context->vme_batchbuffer.bo = dri_bo_alloc(i965->intel.bufmgr,
												   "VME batchbuffer",
												   vme_context->vme_batchbuffer.num_blocks * vme_context->vme_batchbuffer.size_block,
												   0x1000);
}
static VAStatus
gen9_vme_hevc_surface_setup(VADriverContextP ctx,
							struct encode_state *encode_state,
							int is_intra,
							struct intel_encoder_context *encoder_context)
{
	struct object_surface *obj_surface;
	VAEncSequenceParameterBufferHEVC *pSequenceParameter = (VAEncSequenceParameterBufferHEVC *)encode_state->seq_param_ext->buffer;
	GenHevcSurface *hevc_encoder_surface = NULL;

	/*Setup surfaces state*/
	/* current picture for encoding */
	obj_surface = encode_state->input_yuv_object;

	if ((pSequenceParameter->seq_fields.bits.bit_depth_luma_minus8 > 0)
		|| (pSequenceParameter->seq_fields.bits.bit_depth_chroma_minus8 > 0)) {
		hevc_encoder_surface = (GenHevcSurface *)encode_state->reconstructed_object->private_data;
		assert(hevc_encoder_surface);
		obj_surface = hevc_encoder_surface->nv12_surface_obj;
	}
	gen9_vme_source_surface_state(ctx, 0, obj_surface, encoder_context);
	gen9_vme_media_source_surface_state(ctx, 4, obj_surface, encoder_context);
	gen9_vme_media_chroma_source_surface_state(ctx, 6, obj_surface, encoder_context);

	if (!is_intra) {
		VAEncSliceParameterBufferHEVC *slice_param = (VAEncSliceParameterBufferHEVC *)encode_state->slice_params_ext[0]->buffer;
		int slice_type;

		slice_type = slice_param->slice_type;
		assert(slice_type != HEVC_SLICE_I);

		/* to do HEVC */
		intel_hevc_vme_reference_state(ctx, encode_state, encoder_context, 0, 1, gen9_vme_source_surface_state);

		if (slice_type == HEVC_SLICE_B)
			intel_hevc_vme_reference_state(ctx, encode_state, encoder_context, 1, 2, gen9_vme_source_surface_state);
	}

	/* VME output */
	gen9_vme_hevc_output_buffer_setup(ctx, encode_state, 3, encoder_context);
	gen9_vme_hevc_output_vme_batchbuffer_setup(ctx, encode_state, 5, encoder_context);

	return VA_STATUS_SUCCESS;
}
static void
gen9wa_vme_hevc_walker_fill_vme_batchbuffer(VADriverContextP ctx,
											struct encode_state *encode_state,
											int mb_width, int mb_height,
											int kernel,
											int transform_8x8_mode_flag,
											struct intel_encoder_context *encoder_context)
{
	struct gen6_vme_context *vme_context = encoder_context->vme_context;
	int mb_row;
	int s;
	unsigned int *command_ptr;
	VAEncSequenceParameterBufferHEVC *pSequenceParameter = (VAEncSequenceParameterBufferHEVC *)encode_state->seq_param_ext->buffer;
	int log2_cu_size = pSequenceParameter->log2_min_luma_coding_block_size_minus3 + 3;
	int log2_ctb_size = pSequenceParameter->log2_diff_max_min_luma_coding_block_size + log2_cu_size;
	int ctb_size = 1 << log2_ctb_size;
	int num_mb_in_ctb = (ctb_size + 15) / 16;
	num_mb_in_ctb = num_mb_in_ctb * num_mb_in_ctb;

#define     USE_SCOREBOARD      (1 << 21)

	dri_bo_map(vme_context->vme_batchbuffer.bo, 1);
	command_ptr = vme_context->vme_batchbuffer.bo->virtual;

	/*slice_segment_address  must picture_width_in_ctb alainment */
	for (s = 0; s < encode_state->num_slice_params_ext; s++) {
		VAEncSliceParameterBufferHEVC *pSliceParameter = (VAEncSliceParameterBufferHEVC *)encode_state->slice_params_ext[s]->buffer;
		int first_mb = pSliceParameter->slice_segment_address * num_mb_in_ctb;
		int num_mb = pSliceParameter->num_ctu_in_slice * num_mb_in_ctb;
		unsigned int mb_intra_ub, score_dep;
		int x_outer, y_outer, x_inner, y_inner;
		int xtemp_outer = 0;

		x_outer = first_mb % mb_width;
		y_outer = first_mb / mb_width;
		mb_row = y_outer;

		for (; x_outer < (mb_width - 2) && !loop_in_bounds(x_outer, y_outer, first_mb, num_mb, mb_width, mb_height);) {
			x_inner = x_outer;
			y_inner = y_outer;
			for (; !loop_in_bounds(x_inner, y_inner, first_mb, num_mb, mb_width, mb_height);) {
				mb_intra_ub = 0;
				score_dep = 0;
				if (x_inner != 0) {
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_AE;
					score_dep |= MB_SCOREBOARD_A;
				}
				if (y_inner != mb_row) {
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_B;
					score_dep |= MB_SCOREBOARD_B;
					if (x_inner != 0)
						mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_D;
					if (x_inner != (mb_width - 1)) {
						mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_C;
						score_dep |= MB_SCOREBOARD_C;
					}
				}

				*command_ptr++ = (CMD_MEDIA_OBJECT | (8 - 2));
				*command_ptr++ = kernel;
				*command_ptr++ = USE_SCOREBOARD;
				/* Indirect data */
				*command_ptr++ = 0;
				/* the (X, Y) term of scoreboard */
				*command_ptr++ = ((y_inner << 16) | x_inner);
				*command_ptr++ = score_dep;
				/*inline data */
				*command_ptr++ = (mb_width << 16 | y_inner << 8 | x_inner);
				*command_ptr++ = ((1 << 18) | (1 << 16) | transform_8x8_mode_flag | (mb_intra_ub << 8));
				*command_ptr++ = CMD_MEDIA_STATE_FLUSH;
				*command_ptr++ = 0;

				x_inner -= 2;
				y_inner += 1;
			}
			x_outer += 1;
		}

		xtemp_outer = mb_width - 2;
		if (xtemp_outer < 0)
			xtemp_outer = 0;
		x_outer = xtemp_outer;
		y_outer = first_mb / mb_width;
		for (; !loop_in_bounds(x_outer, y_outer, first_mb, num_mb, mb_width, mb_height);) {
			y_inner = y_outer;
			x_inner = x_outer;
			for (; !loop_in_bounds(x_inner, y_inner, first_mb, num_mb, mb_width, mb_height);) {
				mb_intra_ub = 0;
				score_dep = 0;
				if (x_inner != 0) {
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_AE;
					score_dep |= MB_SCOREBOARD_A;
				}
				if (y_inner != mb_row) {
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_B;
					score_dep |= MB_SCOREBOARD_B;
					if (x_inner != 0)
						mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_D;

					if (x_inner != (mb_width - 1)) {
						mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_C;
						score_dep |= MB_SCOREBOARD_C;
					}
				}

				*command_ptr++ = (CMD_MEDIA_OBJECT | (8 - 2));
				*command_ptr++ = kernel;
				*command_ptr++ = USE_SCOREBOARD;
				/* Indirect data */
				*command_ptr++ = 0;
				/* the (X, Y) term of scoreboard */
				*command_ptr++ = ((y_inner << 16) | x_inner);
				*command_ptr++ = score_dep;
				/*inline data */
				*command_ptr++ = (mb_width << 16 | y_inner << 8 | x_inner);
				*command_ptr++ = ((1 << 18) | (1 << 16) | transform_8x8_mode_flag | (mb_intra_ub << 8));

				*command_ptr++ = CMD_MEDIA_STATE_FLUSH;
				*command_ptr++ = 0;
				x_inner -= 2;
				y_inner += 1;
			}
			x_outer++;
			if (x_outer >= mb_width) {
				y_outer += 1;
				x_outer = xtemp_outer;
			}
		}
	}

	*command_ptr++ = MI_BATCH_BUFFER_END;
	*command_ptr++ = 0;

	dri_bo_unmap(vme_context->vme_batchbuffer.bo);
}

static void
gen9_vme_hevc_fill_vme_batchbuffer(VADriverContextP ctx,
								   struct encode_state *encode_state,
								   int mb_width, int mb_height,
								   int kernel,
								   int transform_8x8_mode_flag,
								   struct intel_encoder_context *encoder_context)
{
	struct gen6_vme_context *vme_context = encoder_context->vme_context;
	int mb_x = 0, mb_y = 0;
	int i, s;
	unsigned int *command_ptr;
	VAEncSequenceParameterBufferHEVC *pSequenceParameter = (VAEncSequenceParameterBufferHEVC *)encode_state->seq_param_ext->buffer;
	int log2_cu_size = pSequenceParameter->log2_min_luma_coding_block_size_minus3 + 3;
	int log2_ctb_size = pSequenceParameter->log2_diff_max_min_luma_coding_block_size + log2_cu_size;

	int ctb_size = 1 << log2_ctb_size;
	int num_mb_in_ctb = (ctb_size + 15) / 16;
	num_mb_in_ctb = num_mb_in_ctb * num_mb_in_ctb;

	dri_bo_map(vme_context->vme_batchbuffer.bo, 1);
	command_ptr = vme_context->vme_batchbuffer.bo->virtual;

	for (s = 0; s < encode_state->num_slice_params_ext; s++) {
		VAEncSliceParameterBufferHEVC *pSliceParameter = (VAEncSliceParameterBufferHEVC *)encode_state->slice_params_ext[s]->buffer;
		int slice_mb_begin = pSliceParameter->slice_segment_address * num_mb_in_ctb;
		int slice_mb_number = pSliceParameter->num_ctu_in_slice * num_mb_in_ctb;

		unsigned int mb_intra_ub;
		int slice_mb_x = slice_mb_begin % mb_width;
		for (i = 0; i < slice_mb_number;) {
			int mb_count = i + slice_mb_begin;
			mb_x = mb_count % mb_width;
			mb_y = mb_count / mb_width;
			mb_intra_ub = 0;

			if (mb_x != 0) {
				mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_AE;
			}
			if (mb_y != 0) {
				mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_B;
				if (mb_x != 0)
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_D;
				if (mb_x != (mb_width - 1))
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_C;
			}
			if (i < mb_width) {
				if (i == 0)
					mb_intra_ub &= ~(INTRA_PRED_AVAIL_FLAG_AE);
				mb_intra_ub &= ~(INTRA_PRED_AVAIL_FLAG_BCD_MASK);
				if ((i == (mb_width - 1)) && slice_mb_x) {
					mb_intra_ub |= INTRA_PRED_AVAIL_FLAG_C;
				}
			}

			if ((i == mb_width) && slice_mb_x) {
				mb_intra_ub &= ~(INTRA_PRED_AVAIL_FLAG_D);
			}

			*command_ptr++ = (CMD_MEDIA_OBJECT | (8 - 2));
			*command_ptr++ = kernel;
			*command_ptr++ = 0;
			*command_ptr++ = 0;
			*command_ptr++ = 0;
			*command_ptr++ = 0;

			/*inline data */
			*command_ptr++ = (mb_width << 16 | mb_y << 8 | mb_x);
			*command_ptr++ = ((1 << 16) | transform_8x8_mode_flag | (mb_intra_ub << 8));

			*command_ptr++ = CMD_MEDIA_STATE_FLUSH;
			*command_ptr++ = 0;
			i += 1;
		}
	}

	*command_ptr++ = MI_BATCH_BUFFER_END;
	*command_ptr++ = 0;

	dri_bo_unmap(vme_context->vme_batchbuffer.bo);
}

static void gen9_vme_hevc_pipeline_programing(VADriverContextP ctx,
											  struct encode_state *encode_state,
											  struct intel_encoder_context *encoder_context)
{
	struct gen6_vme_context *vme_context = encoder_context->vme_context;
	struct intel_batchbuffer *batch = encoder_context->base.batch;
	VAEncSliceParameterBufferHEVC *pSliceParameter = (VAEncSliceParameterBufferHEVC *)encode_state->slice_params_ext[0]->buffer;
	VAEncSequenceParameterBufferHEVC *pSequenceParameter = (VAEncSequenceParameterBufferHEVC *)encode_state->seq_param_ext->buffer;
	int width_in_mbs = (pSequenceParameter->pic_width_in_luma_samples + 15) / 16;
	int height_in_mbs = (pSequenceParameter->pic_height_in_luma_samples + 15) / 16;
	int kernel_shader;
	bool allow_hwscore = true;
	int s;

	int log2_cu_size = pSequenceParameter->log2_min_luma_coding_block_size_minus3 + 3;
	int log2_ctb_size = pSequenceParameter->log2_diff_max_min_luma_coding_block_size + log2_cu_size;

	int ctb_size = 1 << log2_ctb_size;
	int num_mb_in_ctb = (ctb_size + 15) / 16;
	int transform_8x8_mode_flag = 1;
	num_mb_in_ctb = num_mb_in_ctb * num_mb_in_ctb;

	for (s = 0; s < encode_state->num_slice_params_ext; s++) {
		pSliceParameter = (VAEncSliceParameterBufferHEVC *)encode_state->slice_params_ext[s]->buffer;
		int slice_mb_begin = pSliceParameter->slice_segment_address * num_mb_in_ctb;
		if ((slice_mb_begin % width_in_mbs)) {
			allow_hwscore = false;
			break;
		}
	}

	if (pSliceParameter->slice_type == HEVC_SLICE_I) {
		kernel_shader = VME_INTRA_SHADER;
	} else if (pSliceParameter->slice_type == HEVC_SLICE_P) {
		kernel_shader = VME_INTER_SHADER;
	} else {
		kernel_shader = VME_BINTER_SHADER;
		if (!allow_hwscore)
			kernel_shader = VME_INTER_SHADER;
	}
	if (allow_hwscore)
		gen9wa_vme_hevc_walker_fill_vme_batchbuffer(ctx,
													encode_state,
													width_in_mbs, height_in_mbs,
													kernel_shader,
													transform_8x8_mode_flag,
													encoder_context);
	else
		gen9_vme_hevc_fill_vme_batchbuffer(ctx,
										   encode_state,
										   width_in_mbs, height_in_mbs,
										   kernel_shader,
										   transform_8x8_mode_flag,
										   encoder_context);

	intel_batchbuffer_start_atomic(batch, 0x1000);
	gen9_gpe_pipeline_setup(ctx, &vme_context->gpe_context, batch);
	BEGIN_BATCH(batch, 3);
	OUT_BATCH(batch, MI_BATCH_BUFFER_START | (1 << 8) | (1 << 0));
	OUT_RELOC64(batch,
				vme_context->vme_batchbuffer.bo,
				I915_GEM_DOMAIN_COMMAND, 0,
				0);
	ADVANCE_BATCH(batch);

	gen9_gpe_pipeline_end(ctx, &vme_context->gpe_context, batch);

	intel_batchbuffer_end_atomic(batch);
}

static VAStatus gen9_intel_init_hevc_surface(VADriverContextP ctx,
											 struct intel_encoder_context *encoder_context,
											 struct encode_state *encode_state,
											 struct object_surface *input_obj_surface,
											 struct object_surface *output_obj_surface,
											 int set_flag)
{
	struct i965_driver_data *i965 = i965_driver_data(ctx);
	struct gen9_hcpe_context *mfc_context = encoder_context->mfc_context;
	VAEncSequenceParameterBufferHEVC *pSequenceParameter = (VAEncSequenceParameterBufferHEVC *)encode_state->seq_param_ext->buffer;
	GenHevcSurface *hevc_encoder_surface;
	struct i965_surface src_surface, dst_surface;
	struct object_surface *obj_surface;
	VARectangle rect;
	VAStatus status;

	uint32_t size;

	obj_surface = output_obj_surface;
	assert(obj_surface && obj_surface->bo);

	if (obj_surface->private_data == NULL) {

		if (mfc_context->pic_size.ctb_size == 16)
			size = ((pSequenceParameter->pic_width_in_luma_samples + 63) >> 6) *
				   ((pSequenceParameter->pic_height_in_luma_samples + 15) >> 4);
		else
			size = ((pSequenceParameter->pic_width_in_luma_samples + 31) >> 5) *
				   ((pSequenceParameter->pic_height_in_luma_samples + 31) >> 5);
		size <<= 6; /* in unit of 64bytes */

		hevc_encoder_surface = calloc(sizeof(GenHevcSurface), 1);

		assert(hevc_encoder_surface);
		hevc_encoder_surface->motion_vector_temporal_bo =
			dri_bo_alloc(i965->intel.bufmgr,
						 "motion vector temporal buffer",
						 size,
						 0x1000);
		assert(hevc_encoder_surface->motion_vector_temporal_bo);

		hevc_encoder_surface->ctx = ctx;
		hevc_encoder_surface->nv12_surface_obj = NULL;
		hevc_encoder_surface->nv12_surface_id = VA_INVALID_SURFACE;
		hevc_encoder_surface->has_p010_to_nv12_done = 0;

		obj_surface->private_data = (void *)hevc_encoder_surface;
		obj_surface->free_private_data = (void *)gen_free_hevc_surface;
	}

	hevc_encoder_surface = (GenHevcSurface *) obj_surface->private_data;

	if (!hevc_encoder_surface->has_p010_to_nv12_done && obj_surface->fourcc == VA_FOURCC_P010) {
		// convert input
		rect.x = 0;
		rect.y = 0;
		rect.width = obj_surface->orig_width;
		rect.height = obj_surface->orig_height;

		src_surface.base = (struct object_base *)input_obj_surface;
		src_surface.type = I965_SURFACE_TYPE_SURFACE;
		src_surface.flags = I965_SURFACE_FLAG_FRAME;

		if (SURFACE(hevc_encoder_surface->nv12_surface_id) == NULL) {
			status = i965_CreateSurfaces(ctx,
										 obj_surface->orig_width,
										 obj_surface->orig_height,
										 VA_RT_FORMAT_YUV420,
										 1,
										 &hevc_encoder_surface->nv12_surface_id);
			assert(status == VA_STATUS_SUCCESS);

			if (status != VA_STATUS_SUCCESS)
				return status;
		}

		obj_surface = SURFACE(hevc_encoder_surface->nv12_surface_id);
		hevc_encoder_surface->nv12_surface_obj = obj_surface;
		assert(obj_surface);
		i965_check_alloc_surface_bo(ctx, obj_surface, 1, VA_FOURCC_NV12, SUBSAMPLE_YUV420);

		dst_surface.base = (struct object_base *)obj_surface;
		dst_surface.type = I965_SURFACE_TYPE_SURFACE;
		dst_surface.flags = I965_SURFACE_FLAG_FRAME;

		status = i965_image_processing(ctx,
									   &src_surface,
									   &rect,
									   &dst_surface,
									   &rect);
		assert(status == VA_STATUS_SUCCESS);

		if (set_flag)
			hevc_encoder_surface->has_p010_to_nv12_done = 1;
	}

	return VA_STATUS_SUCCESS;
}

static VAStatus gen9_intel_hevc_input_check(VADriverContextP ctx,
											struct encode_state *encode_state,
											struct intel_encoder_context *encoder_context)
{
	struct i965_driver_data *i965 = i965_driver_data(ctx);
	struct gen9_hcpe_context *mfc_context = encoder_context->mfc_context;
	struct object_surface *obj_surface;
	GenHevcSurface *hevc_encoder_surface = NULL;
	int i;
	int fourcc;

	obj_surface = SURFACE(encoder_context->input_yuv_surface);
	assert(obj_surface && obj_surface->bo);

	fourcc = obj_surface->fourcc;
	/* Setup current frame and current direct mv buffer*/
	obj_surface = encode_state->reconstructed_object;
	if (fourcc == VA_FOURCC_P010)
		i965_check_alloc_surface_bo(ctx, obj_surface, 1, VA_FOURCC_P010, SUBSAMPLE_YUV420);
	else
		i965_check_alloc_surface_bo(ctx, obj_surface, 1, VA_FOURCC_NV12, SUBSAMPLE_YUV420);
	hevc_encoder_surface = NULL;
	hevc_encoder_surface = (GenHevcSurface *) obj_surface->private_data;
	if (hevc_encoder_surface)
		hevc_encoder_surface->has_p010_to_nv12_done = 0;
	gen9_intel_init_hevc_surface(ctx, encoder_context, encode_state, encode_state->input_yuv_object,
								 obj_surface, 0);

	/* Setup reference frames and direct mv buffers*/
	for (i = 0; i < MAX_HCP_REFERENCE_SURFACES; i++) {
		obj_surface = encode_state->reference_objects[i];

		if (obj_surface && obj_surface->bo) {
			mfc_context->reference_surfaces[i].bo = obj_surface->bo;
			dri_bo_reference(obj_surface->bo);

			gen9_intel_init_hevc_surface(ctx, encoder_context, encode_state, obj_surface,
										 obj_surface, 1);
		} else {
			break;
		}
	}

	return VA_STATUS_SUCCESS;
}

static VAStatus gen9_vme_hevc_prepare(VADriverContextP ctx,
									  struct encode_state *encode_state,
									  struct intel_encoder_context *encoder_context)
{
	VAStatus vaStatus = VA_STATUS_SUCCESS;
	VAEncSliceParameterBufferHEVC *pSliceParameter = (VAEncSliceParameterBufferHEVC *)encode_state->slice_params_ext[0]->buffer;
	int is_intra = pSliceParameter->slice_type == HEVC_SLICE_I;
	VAEncSequenceParameterBufferHEVC *pSequenceParameter = (VAEncSequenceParameterBufferHEVC *)encode_state->seq_param_ext->buffer;
	struct gen6_vme_context *vme_context = encoder_context->vme_context;

	/* here use the avc level for hevc vme */
	if (!vme_context->hevc_level ||
		(vme_context->hevc_level != pSequenceParameter->general_level_idc)) {
		vme_context->hevc_level = pSequenceParameter->general_level_idc;
	}

	//internal input check for main10
	gen9_intel_hevc_input_check(ctx, encode_state, encoder_context);

	intel_vme_hevc_update_mbmv_cost(ctx, encode_state, encoder_context);

	/*Setup all the memory object*/
	gen9_vme_hevc_surface_setup(ctx, encode_state, is_intra, encoder_context);
	gen9_vme_interface_setup(ctx, encode_state, encoder_context);
	//gen9_vme_vme_state_setup(ctx, encode_state, is_intra, encoder_context);
	gen9_vme_constant_setup(ctx, encode_state, encoder_context, 1);

	/*Programing media pipeline*/
	gen9_vme_hevc_pipeline_programing(ctx, encode_state, encoder_context);

	return vaStatus;
}


static VAStatus
gen9_vme_hevc_pipeline(VADriverContextP ctx,
					   VAProfile profile,
					   struct encode_state *encode_state,
					   struct intel_encoder_context *encoder_context)
{
	gen9_vme_media_init(ctx, encoder_context);
	gen9_vme_hevc_prepare(ctx, encode_state, encoder_context);
	gen9_vme_run(ctx, encode_state, encoder_context);
	gen9_vme_stop(ctx, encode_state, encoder_context);

	return VA_STATUS_SUCCESS;
}


static void
gen9_vme_context_destroy(void *context)
{
	struct gen6_vme_context *vme_context = context;

	gen8_gpe_context_destroy(&vme_context->gpe_context);

	dri_bo_unreference(vme_context->vme_output.bo);
	vme_context->vme_output.bo = NULL;

	dri_bo_unreference(vme_context->vme_state.bo);
	vme_context->vme_state.bo = NULL;

	dri_bo_unreference(vme_context->vme_batchbuffer.bo);
	vme_context->vme_batchbuffer.bo = NULL;

	free(vme_context->vme_state_message);
	vme_context->vme_state_message = NULL;

	dri_bo_unreference(vme_context->i_qp_cost_table);
	vme_context->i_qp_cost_table = NULL;

	dri_bo_unreference(vme_context->p_qp_cost_table);
	vme_context->p_qp_cost_table = NULL;

	dri_bo_unreference(vme_context->b_qp_cost_table);
	vme_context->b_qp_cost_table = NULL;

	free(vme_context->qp_per_mb);
	vme_context->qp_per_mb = NULL;

	free(vme_context);
}

extern Bool i965_encoder_vp8_vme_context_init(VADriverContextP ctx, struct intel_encoder_context *encoder_context);

Bool gen9_vme_context_init(VADriverContextP ctx, struct intel_encoder_context *encoder_context)
{
	struct i965_driver_data *i965 = i965_driver_data(ctx);
	struct gen6_vme_context *vme_context;
	struct i965_kernel *vme_kernel_list = NULL;
	int i965_kernel_num;

	if (encoder_context->low_power_mode || encoder_context->codec == CODEC_JPEG) {
		encoder_context->vme_context = NULL;
		encoder_context->vme_pipeline = NULL;
		encoder_context->vme_context_destroy = NULL;

		return True;
	}

	if (encoder_context->codec == CODEC_VP9) {
		return gen9_vp9_vme_context_init(ctx, encoder_context);
	} else if (encoder_context->codec == CODEC_VP8) {
		return i965_encoder_vp8_vme_context_init(ctx, encoder_context);
	} else if (encoder_context->codec == CODEC_H264 ||
			   encoder_context->codec == CODEC_H264_MVC) {
		return gen9_avc_vme_context_init(ctx, encoder_context);
	} else if (encoder_context->codec == CODEC_HEVC) {
		if (IS_GEN10(i965->intel.device_info))
			return gen10_hevc_vme_context_init(ctx, encoder_context);
		else
			return gen9_hevc_vme_context_init(ctx, encoder_context);
	}

	vme_context = calloc(1, sizeof(struct gen6_vme_context));

	switch (encoder_context->codec) {
	case CODEC_H264:
	case CODEC_H264_MVC:
		vme_kernel_list = gen9_vme_kernels;
		encoder_context->vme_pipeline = gen9_vme_pipeline;
		i965_kernel_num = sizeof(gen9_vme_kernels) / sizeof(struct i965_kernel);
		break;

	case CODEC_MPEG2:
		vme_kernel_list = gen9_vme_mpeg2_kernels;
		encoder_context->vme_pipeline = gen9_vme_mpeg2_pipeline;
		i965_kernel_num = sizeof(gen9_vme_mpeg2_kernels) / sizeof(struct i965_kernel);
		break;

	case CODEC_VP8:
		vme_kernel_list = gen9_vme_vp8_kernels;
		encoder_context->vme_pipeline = gen9_vme_vp8_pipeline;
		i965_kernel_num = sizeof(gen9_vme_vp8_kernels) / sizeof(struct i965_kernel);
		break;

	case CODEC_HEVC:
		vme_kernel_list = gen9_vme_hevc_kernels;
		encoder_context->vme_pipeline = gen9_vme_hevc_pipeline;
		i965_kernel_num = sizeof(gen9_vme_hevc_kernels) / sizeof(struct i965_kernel);
		break;

	default:
		/* never get here */
		assert(0);
		i965_kernel_num = 0;

		break;
	}

	assert(vme_context);
	vme_context->vme_kernel_sum = i965_kernel_num;
	vme_context->gpe_context.surface_state_binding_table.length = (SURFACE_STATE_PADDED_SIZE + sizeof(unsigned int)) * MAX_MEDIA_SURFACES_GEN6;

	vme_context->gpe_context.idrt.entry_size = ALIGN(sizeof(struct gen8_interface_descriptor_data), 64);
	vme_context->gpe_context.idrt.max_entries = MAX_INTERFACE_DESC_GEN6;
	vme_context->gpe_context.curbe.length = CURBE_TOTAL_DATA_LENGTH;
	vme_context->gpe_context.sampler.entry_size = 0;
	vme_context->gpe_context.sampler.max_entries = 0;

	if (i965->intel.eu_total > 0) {
		vme_context->gpe_context.vfe_state.max_num_threads = 6 *
															 i965->intel.eu_total;
	} else
		vme_context->gpe_context.vfe_state.max_num_threads = 60 - 1;

	vme_context->gpe_context.vfe_state.num_urb_entries = 64;
	vme_context->gpe_context.vfe_state.gpgpu_mode = 0;
	vme_context->gpe_context.vfe_state.urb_entry_size = 16;
	vme_context->gpe_context.vfe_state.curbe_allocation_size = CURBE_ALLOCATION_SIZE - 1;

	gen7_vme_scoreboard_init(ctx, vme_context);

	gen8_gpe_load_kernels(ctx,
						  &vme_context->gpe_context,
						  vme_kernel_list,
						  i965_kernel_num);
	vme_context->vme_surface2_setup = gen8_gpe_surface2_setup;
	vme_context->vme_media_rw_surface_setup = gen8_gpe_media_rw_surface_setup;
	vme_context->vme_buffer_suface_setup = gen8_gpe_buffer_suface_setup;
	vme_context->vme_media_chroma_surface_setup = gen8_gpe_media_chroma_surface_setup;

	encoder_context->vme_context = vme_context;
	encoder_context->vme_context_destroy = gen9_vme_context_destroy;

	vme_context->vme_state_message = malloc(VME_MSG_LENGTH * sizeof(int));

	return True;
}
