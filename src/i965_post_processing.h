/*
 * Copyright (C) 2010 Intel Corporation
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
 *    Xiang Haihao <haihao.xiang@intel.com>
 *
 */

#ifndef __I965_POST_PROCESSING_H__
#define __I965_POST_PROCESSING_H__

#include "i965_vpp_avs.h"
#include <drm.h>
#include <i915_drm.h>
#include <intel_bufmgr.h>
#include "i965_gpe_utils.h"

#define MAX_PP_SURFACES                 48

struct i965_gpe_context;

enum {
	PP_NULL = 0,
	PP_NV12_LOAD_SAVE_N12,
	PP_NV12_LOAD_SAVE_PL3,
	PP_PL3_LOAD_SAVE_N12,
	PP_PL3_LOAD_SAVE_PL3,
	PP_NV12_SCALING,
	PP_NV12_AVS,
	PP_NV12_DNDI,
	PP_NV12_DN,
	PP_NV12_LOAD_SAVE_PA,
	PP_PL3_LOAD_SAVE_PA,
	PP_PA_LOAD_SAVE_NV12,
	PP_PA_LOAD_SAVE_PL3,
	PP_PA_LOAD_SAVE_PA,
	PP_RGBX_LOAD_SAVE_NV12,
	PP_NV12_LOAD_SAVE_RGBX,
	NUM_PP_MODULES,
};

struct i965_post_processing_context;

struct pp_load_save_context {
	int dest_x;
	int dest_y;
	int dest_w;
	int dest_h;
};

struct pp_scaling_context {
	int dest_x; /* in pixel */
	int dest_y; /* in pixel */
	int dest_w;
	int dest_h;
	float src_normalized_x;
	float src_normalized_y;
};

struct pp_avs_context {
	AVSState state;
	int dest_x; /* in pixel */
	int dest_y; /* in pixel */
	int dest_w;
	int dest_h;
	float src_normalized_x;
	float src_normalized_y;
	int src_w;
	int src_h;
	float horiz_range;
};

enum {
	DNDI_FRAME_IN_CURRENT = 0,
	DNDI_FRAME_IN_PREVIOUS,
	DNDI_FRAME_IN_STMM,
	DNDI_FRAME_OUT_STMM,
	DNDI_FRAME_OUT_CURRENT,
	DNDI_FRAME_OUT_PREVIOUS,
	DNDI_FRAME_STORE_COUNT
};

typedef struct dndi_frame_store {
	struct object_surface *obj_surface;
	VASurfaceID surface_id; /* always relative to the input surface */
	unsigned int is_scratch_surface : 1;
} DNDIFrameStore;

struct pp_dndi_context {
	int dest_w;
	int dest_h;
	DNDIFrameStore frame_store[DNDI_FRAME_STORE_COUNT];

	/* Temporary flags live until the current picture is processed */
	unsigned int is_di_enabled          : 1;
	unsigned int is_di_adv_enabled      : 1;
	unsigned int is_first_frame         : 1;
	unsigned int is_second_field        : 1;
};

struct pp_dn_context {
	int dest_w;
	int dest_h;
	dri_bo *stmm_bo;
};

struct i965_post_processing_context;

struct pp_module {
	struct i965_kernel kernel;

	/* others */
	VAStatus(*initialize)(VADriverContextP ctx, struct i965_post_processing_context *pp_context,
						  const struct i965_surface *src_surface,
						  const VARectangle *src_rect,
						  struct i965_surface *dst_surface,
						  const VARectangle *dst_rect,
						  void *filter_param);
};

struct pp_static_parameter {
	struct {
		/* Procamp r1.0 */
		float procamp_constant_c0;

		/* Load and Same r1.1 */
		unsigned int source_packed_y_offset: 8;
		unsigned int source_packed_u_offset: 8;
		unsigned int source_packed_v_offset: 8;
		unsigned int source_rgb_layout: 8;      // 1 for |R|G|B|X| layout, 0 for |B|G|R|X| layout

		union {
			/* Load and Save r1.2 */
			struct {
				unsigned int destination_packed_y_offset: 8;
				unsigned int destination_packed_u_offset: 8;
				unsigned int destination_packed_v_offset: 8;
				unsigned int pad0: 8;
			} load_and_save;

			/* CSC r1.2 */
			struct {
				unsigned int pad0: 24;
				unsigned int destination_rgb_layout: 8; // 1 for |R|G|B|X| layout, 0 for |B|G|R|X| layout
			} csc;
		} r1_2;

		/* Procamp r1.3 */
		float procamp_constant_c1;

		/* Procamp r1.4 */
		float procamp_constant_c2;

		/* DI r1.5 */
		unsigned int statistics_surface_picth: 16; /* Devided by 2 */
		unsigned int pad1: 16;

		union {
			/* DI r1.6 */
			struct {
				unsigned int pad0: 24;
				unsigned int top_field_first: 8;
			} di;

			/* AVS/Scaling r1.6 */
			float normalized_video_y_scaling_step;
		} r1_6;

		/* Procamp r1.7 */
		float procamp_constant_c5;
	} grf1;

	struct {
		/* Procamp r2.0 */
		float procamp_constant_c3;

		/* MBZ r2.1*/
		unsigned int pad0;

		/* WG+CSC r2.2 */
		float wg_csc_constant_c4;

		/* WG+CSC r2.3 */
		float wg_csc_constant_c8;

		/* Procamp r2.4 */
		float procamp_constant_c4;

		/* MBZ r2.5 */
		unsigned int pad1;

		/* MBZ r2.6 */
		unsigned int pad2;

		/* WG+CSC r2.7 */
		float wg_csc_constant_c9;
	} grf2;

	struct {
		/* WG+CSC r3.0 */
		float wg_csc_constant_c0;

		/* Blending r3.1 */
		float scaling_step_ratio;

		/* Blending r3.2 */
		float normalized_alpha_y_scaling;

		/* WG+CSC r3.3 */
		float wg_csc_constant_c4;

		/* WG+CSC r3.4 */
		float wg_csc_constant_c1;

		/* ALL r3.5 */
		int horizontal_origin_offset: 16;
		int vertical_origin_offset: 16;

		/* Shared r3.6*/
		union {
			/* Color filll */
			unsigned int color_pixel;

			/* WG+CSC */
			float wg_csc_constant_c2;
		} r3_6;

		/* WG+CSC r3.7 */
		float wg_csc_constant_c3;
	} grf3;

	struct {
		/* WG+CSC r4.0 */
		float wg_csc_constant_c6;

		/* ALL r4.1 MBZ ???*/
		unsigned int pad0;

		/* Shared r4.2 */
		union {
			/* AVS */
			struct {
				unsigned int pad1: 15;
				unsigned int nlas: 1;
				unsigned int pad2: 16;
			} avs;

			/* DI */
			struct {
				unsigned int motion_history_coefficient_m2: 8;
				unsigned int motion_history_coefficient_m1: 8;
				unsigned int pad0: 16;
			} di;
		} r4_2;

		/* WG+CSC r4.3 */
		float wg_csc_constant_c7;

		/* WG+CSC r4.4 */
		float wg_csc_constant_c10;

		/* AVS r4.5 */
		float source_video_frame_normalized_horizontal_origin;

		/* MBZ r4.6 */
		unsigned int pad1;

		/* WG+CSC r4.7 */
		float wg_csc_constant_c11;
	} grf4;
};

struct pp_inline_parameter {
	struct {
		/* ALL r5.0 */
		int destination_block_horizontal_origin: 16;
		int destination_block_vertical_origin: 16;

		/* Shared r5.1 */
		union {
			/* AVS/Scaling */
			float source_surface_block_normalized_horizontal_origin;

			/* FMD */
			struct {
				unsigned int variance_surface_vertical_origin: 16;
				unsigned int pad0: 16;
			} fmd;
		} r5_1;

		/* AVS/Scaling r5.2 */
		float source_surface_block_normalized_vertical_origin;

		/* Alpha r5.3 */
		float alpha_surface_block_normalized_horizontal_origin;

		/* Alpha r5.4 */
		float alpha_surface_block_normalized_vertical_origin;

		/* Alpha r5.5 */
		unsigned int alpha_mask_x: 16;
		unsigned int alpha_mask_y: 8;
		unsigned int block_count_x: 8;

		/* r5.6 */
		/* we only support M*1 or 1*N block partitation now.
		 *   -- it means asm code only need update this mask from grf6 for the last block
		 */
		unsigned int block_horizontal_mask: 16;
		unsigned int block_vertical_mask: 8;
		unsigned int number_blocks: 8;

		/* AVS/Scaling r5.7 */
		float normalized_video_x_scaling_step;
	} grf5;

	struct {
		/* AVS r6.0 */
		float video_step_delta;

		/* r6.1 */    // sizeof(int) == 4?
		unsigned int block_horizontal_mask_right: 16;
		unsigned int block_vertical_mask_bottom: 8;
		unsigned int pad1: 8;

		/* r6.2 */
		unsigned int block_horizontal_mask_middle: 16;
		unsigned int pad2: 16;

		/* r6.3-r6.7 */
		unsigned int padx[5];
	} grf6;
};

struct gen7_pp_static_parameter {
	struct {
		/* r1.0-r1.5 */
		unsigned int padx[6];
		/* r1.6 */
		unsigned int di_statistics_surface_pitch_div2: 16;
		unsigned int di_statistics_surface_height_div4: 16;
		/* r1.7 */
		unsigned int di_top_field_first: 8;
		unsigned int pad0: 16;
		unsigned int pointer_to_inline_parameter: 8; /* value: 7 */
	} grf1;

	struct {
		/* r2.0 */
		/* Indicates whether the rgb is swapped for the src surface
		 * 0: RGBX(MSB. X-B-G-R). 1: BGRX(MSB: X-R-G-B)
		 */
		unsigned int src_avs_rgb_swap: 1;
		unsigned int pad3: 31;

		/* r2.1 */
		unsigned int pad2: 16;
		unsigned int save_avs_rgb_swap: 1; /* 0: RGB, 1: BGR */
		unsigned int avs_wa_enable: 1; /* must enabled for GEN7 */
		unsigned int ief_enable: 1;
		unsigned int avs_wa_width: 13;

		/* 2.2 */
		float avs_wa_one_div_256_width;

		/* 2.3 */
		float avs_wa_five_div_256_width;

		/* 2.4 - 2.6 */
		unsigned int padx[3];

		/* r2.7 */
		unsigned int di_destination_packed_y_component_offset: 8;
		unsigned int di_destination_packed_u_component_offset: 8;
		unsigned int di_destination_packed_v_component_offset: 8;
		unsigned int alpha: 8;
	} grf2;

	struct {
		float sampler_load_horizontal_scaling_step_ratio;
		unsigned int padx[7];
	} grf3;

	struct {
		float sampler_load_vertical_scaling_step;
		unsigned int pad0;
		unsigned int di_hoffset_svf_from_dvf: 16;
		unsigned int di_voffset_svf_from_dvf: 16;
		unsigned int padx[5];
	} grf4;

	struct {
		float sampler_load_vertical_frame_origin;
		unsigned int padx[7];
	} grf5;

	struct {
		float sampler_load_horizontal_frame_origin;
		unsigned int padx[7];
	} grf6;

	struct {
		/* r7.0 -> r7.3 */
		float coef_ry;
		float coef_ru;
		float coef_rv;
		float coef_yd;

		/* r7.4 -> r7.7 */
		float coef_gy;
		float coef_gu;
		float coef_gv;
		float coef_ud;
	} grf7;

	struct {
		/* r8.0 -> r8.3 */
		float coef_by;
		float coef_bu;
		float coef_bv;
		float coef_vd;

		/* r8.4 -> r8.7 */
		unsigned int padx[4];
	} grf8;
};

struct gen7_pp_inline_parameter {
	struct {
		/* r9.0 */
		unsigned int destination_block_horizontal_origin: 16;
		unsigned int destination_block_vertical_origin: 16;
		/* r9.1: 0xffffffff */
		unsigned int constant_0;
		/* r9.2 */
		unsigned int pad0;
		/* r9.3 */
		unsigned int pad1;
		/* r9.4 */
		float sampler_load_main_video_x_scaling_step;
		/* r9.5 */
		unsigned int pad2;
		/* r9.6: must be zero */
		unsigned int avs_vertical_block_number;
		/* r9.7: 0 */
		unsigned int group_id_number;
	} grf9;

	struct {
		unsigned int padx[8];
	} grf10;
};

struct i965_post_processing_context {
	int current_pp;
	struct pp_module pp_modules[NUM_PP_MODULES];
	void *pp_static_parameter;
	void *pp_inline_parameter;

	struct {
		dri_bo *bo;
	} surface_state_binding_table;

	struct {
		dri_bo *bo;
	} curbe;

	struct {
		dri_bo *bo;
		int num_interface_descriptors;
	} idrt;

	struct {
		dri_bo *bo;
	} vfe_state;

	struct {
		dri_bo *bo;
		dri_bo *bo_8x8;
		dri_bo *bo_8x8_uv;
	} sampler_state_table;

	struct {
		unsigned int size;

		unsigned int vfe_start;
		unsigned int cs_start;

		unsigned int num_vfe_entries;
		unsigned int num_cs_entries;

		unsigned int size_vfe_entry;
		unsigned int size_cs_entry;
	} urb;

	struct {
		unsigned int gpgpu_mode : 1;
		unsigned int pad0 : 7;
		unsigned int max_num_threads : 16;
		unsigned int num_urb_entries : 8;
		unsigned int urb_entry_size : 16;
		unsigned int curbe_allocation_size : 16;
	} vfe_gpu_state;

	struct intel_vebox_context *vebox_proc_ctx;

	struct pp_load_save_context pp_load_save_context;
	struct pp_scaling_context pp_scaling_context;
	struct pp_avs_context pp_avs_context;
	struct pp_dndi_context pp_dndi_context;
	struct pp_dn_context pp_dn_context;
	void *private_context; /* pointer to the current private context */
	void *pipeline_param;  /* pointer to the pipeline parameter */
	/**
	 * \ref Extra filter flags used as a fast path.
	 *
	 * This corresponds to vaPutSurface() flags, for direct rendering,
	 * or to VAProcPipelineParameterBuffer.filter_flags when the VPP
	 * interfaces are used. In the latter case, this is just a copy of
	 * that field.
	 */
	unsigned int filter_flags;

	int (*pp_x_steps)(void *private_context);
	int (*pp_y_steps)(void *private_context);
	int (*pp_set_block_parameter)(struct i965_post_processing_context *pp_context, int x, int y);

	struct intel_batchbuffer *batch;

	unsigned int block_horizontal_mask_left: 16;
	unsigned int block_horizontal_mask_right: 16;
	unsigned int block_vertical_mask_bottom: 8;

	struct {
		dri_bo *bo;
		int bo_size;
		unsigned int end_offset;
	} instruction_state;

	struct {
		dri_bo *bo;
	} indirect_state;

	struct {
		dri_bo *bo;
		int bo_size;
		unsigned int end_offset;
	} dynamic_state;

	unsigned int sampler_offset;
	int sampler_size;
	unsigned int idrt_offset;
	int idrt_size;
	unsigned int curbe_offset;
	int curbe_size;

	VAStatus(*intel_post_processing)(VADriverContextP   ctx,
									 struct i965_post_processing_context *pp_context,
									 const struct i965_surface *src_surface,
									 const VARectangle *src_rect,
									 struct i965_surface *dst_surface,
									 const VARectangle *dst_rect,
									 int   pp_index,
									 void * filter_param);
	void (*finalize)(VADriverContextP ctx,
					 struct i965_post_processing_context *pp_context);


	struct i965_gpe_context scaling_gpe_context;

#define VPPGPE_8BIT_8BIT        (1 << 0)
#define VPPGPE_8BIT_10BIT       (1 << 1)
#define VPPGPE_10BIT_10BIT      (1 << 2)
#define VPPGPE_10BIT_8BIT       (1 << 3)
#define VPPGPE_8BIT_420_RGB32   (1 << 4)

	unsigned int scaling_gpe_context_initialized;
};

struct i965_proc_context {
	struct hw_context base;
	void *driver_context;
	struct i965_post_processing_context pp_context;
};

VASurfaceID
i965_post_processing(
	VADriverContextP   ctx,
	struct object_surface *obj_surface,
	const VARectangle *src_rect,
	const VARectangle *dst_rect,
	unsigned int       va_flags,
	int                *has_done_scaling,
	VARectangle *calibrated_rect
);

VAStatus
i965_scaling_processing(
	VADriverContextP   ctx,
	struct object_surface *src_surface_obj,
	const VARectangle *src_rect,
	struct object_surface *dst_surface_obj,
	const VARectangle *dst_rect,
	unsigned int       va_flags
);

VAStatus
i965_image_processing(VADriverContextP ctx,
					  const struct i965_surface *src_surface,
					  const VARectangle *src_rect,
					  struct i965_surface *dst_surface,
					  const VARectangle *dst_rect);

void
i965_post_processing_terminate(VADriverContextP ctx);
bool
i965_post_processing_init(VADriverContextP ctx);


extern VAStatus
i965_proc_picture(VADriverContextP ctx,
				  VAProfile profile,
				  union codec_state *codec_state,
				  struct hw_context *hw_context);

int
pp_get_surface_fourcc(VADriverContextP ctx, const struct i965_surface *surface);

#endif /* __I965_POST_PROCESSING_H__ */
