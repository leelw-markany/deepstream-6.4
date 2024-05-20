/*
 * Copyright (c) 2018-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __NVGSTDS_APP_H__
#define __NVGSTDS_APP_H__

#include <gst/gst.h>
#include <stdio.h>

#include "deepstream_app_version.h"
#include "deepstream_common.h"
#include "deepstream_config.h"
#include "deepstream_osd.h"
#include "deepstream_primary_gie.h"
#include "deepstream_secondary_gie.h"
#include "deepstream_sinks.h"
#include "deepstream_sources.h"
#include "deepstream_streammux.h"
#include "deepstream_tiled_display.h"
#include "deepstream_tracker.h"
#include "deepstream_c2d_msg.h"
#include "gst-nvdscustommessage.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct _AppCtx AppCtx;

typedef void (*bbox_generated_post_analytics_callback) (AppCtx *appCtx, GstBuffer *buf,
    NvDsBatchMeta *batch_meta);
typedef gboolean (*overlay_graphics_callback) (AppCtx *appCtx, GstBuffer *buf,
    NvDsBatchMeta *batch_meta, guint index);

typedef struct
{
  gulong primary_bbox_buffer_probe_id;
  NvDsPrimaryGieBin primary_gie_bin;
  NvDsSecondaryGieBin secondary_gie_bin;
  NvDsTrackerBin tracker_bin;
  GstElement *tee;
  AppCtx *appCtx;
} PrototypeCommonElements;

typedef struct
{
  NvDsSinkBin sink_bin;
  GstElement *bin;
  NvDsOSDBin osd_bin;
  gulong instance_bin_probe_id;
  AppCtx *appCtx;
} PrototypeInstanceBin;

typedef struct
{
  NvDsSinkBin demux_sink_bin;
  GstElement *bin;
  NvDsOSDBin osd_bin;
  gulong demux_instance_bin_probe_id;
} PrototypeDemuxInstanceBin;

typedef struct
{
  GstElement *pipeline;
  NvDsSrcParentBin multi_src_bin;
  PrototypeCommonElements common_elements;
  PrototypeInstanceBin instance_bins[MAX_SOURCE_BINS];
  PrototypeDemuxInstanceBin demux_instance_bins[MAX_SOURCE_BINS];
  GstElement *tiler_tee;
  NvDsTiledDisplayBin tiled_display_bin;
  GstElement *demuxer;
} PrototypePipeline;

typedef struct
{
  // application:
  // enable-perf-measurement: 1
  // perf-measurement-interval-sec: 5
  gboolean enable_perf_measurement;
  guint perf_measurement_interval_sec;

  // tiled-display:
  NvDsTiledDisplayConfig tiled_display_config;

  // source:
  NvDsSourceConfig multi_source_config[MAX_SOURCE_BINS];
  guint num_source_sub_bins;

  // sinkXX:
  NvDsSinkSubBinConfig sink_bin_sub_bin_config[MAX_SINK_BINS];
  guint num_sink_sub_bins;

  // osd:
  NvDsOSDConfig osd_config;

  // streammux:
  NvDsStreammuxConfig streammux_config;

  // primary-gie:
  NvDsGieConfig primary_gie_config;

  // secondary-gieXX:
  NvDsGieConfig secondary_gie_sub_bin_config[MAX_SECONDARY_GIE_BINS];
  guint num_secondary_gie_sub_bins;

  // tracker:
  NvDsTrackerConfig tracker_config;
} PrototypeConfig;

struct _AppCtx
{
  gboolean version;
  gboolean cintr;
  gboolean show_bbox_text;
  gboolean seeking;
  gboolean quit;
  gint person_class_id;
  gint car_class_id;
  gint return_value;
  guint index;
  gint active_source_index;

  GMutex app_lock;
  GCond app_cond;

  PrototypePipeline pipeline;
  PrototypeConfig config;
  NvDsC2DContext *c2d_ctx[MAX_MESSAGE_CONSUMERS];
  NvDsAppPerfStructInt perf_struct;
  bbox_generated_post_analytics_callback bbox_generated_post_analytics_cb;
  NvDsFrameLatencyInfo *latency_info;
  GMutex latency_lock;

  /** REST API stream/add, remove 작업으로 얻은 NvDsSensorInfo를
   * 저장하는 해시 테이블입니다.
   * 키는 source_id입니다*/
  GHashTable *sensorInfoHash;
};

/**
 * @brief  앱 컨텍스트에 따라 DS Anyalytics 파이프라인을 생성합니다.
 *         구성에 따른 앱 컨텍스트를 제공하고 파이프라인 리소스를 유지하는 곳입니다.
 * @param  appCtx [IN/OUT] 애플리케이션 컨텍스트는
 *         구성 정보를 제공하고 파이프라인 리소스를 유지합니다.
 * @param  bbox_generated_post_analytics_cb [IN] 이 콜백은
 *         분석 이후에 트리거됩니다.
 *         (PGIE, Tracker 또는 파이프라인에서 마지막으로 나타나는 SGIE)
 *         자세한 내용은 create_common_elements()를 참조하세요.
 * @param  perf_cb [IN]
 */
gboolean create_pipeline (AppCtx * appCtx,
    bbox_generated_post_analytics_callback bbox_generated_post_analytics_cb,
    perf_callback perf_cb);

gboolean pause_pipeline (AppCtx * appCtx);
gboolean resume_pipeline (AppCtx * appCtx);
gboolean seek_pipeline (AppCtx * appCtx, glong milliseconds, gboolean seek_is_relative);

void toggle_show_bbox_text (AppCtx * appCtx);

void destroy_pipeline (AppCtx * appCtx);
void restart_pipeline (AppCtx * appCtx);

/**
 * YML 구성 파일에서 속성을 읽는 함수입니다.
 *
 * @param[in] config @ref NvDsConfig에 대한 포인터
 * @param[in] cfg_file_path 구성 파일의 경로입니다.
 *
 * @return 파싱이 성공하면 true를 반환합니다.
 */
gboolean
parse_config_file_yaml (PrototypeConfig * config, gchar * cfg_file_path);

/**
 * nvmultiurisrcbin REST API를 사용하여 추가된 source_id에 대한
 * NvDsSensorInfo를 획득하는 함수입니다.
 *
 * @param[in] appCtx [IN/OUT] 애플리케이션 컨텍스트는
 *            구성 정보를 제공하고 파이프라인 리소스를 유지합니다.
 * @param[in] source_id [IN] NvDsFrameMeta에서 발견된 고유한 source_id
 *
 * @return [transfer-floating] nvmultiurisrcbin REST API를 사용하여
 * 추가된 source_id에 대한 NvDsSensorInfo입니다.
 * 반환된 포인터는 스트림이 제거될 때까지 유효함에 유의하십시오.
 */
NvDsSensorInfo* get_sensor_info(AppCtx* appCtx, guint source_id);

#ifdef __cplusplus
}
#endif

#endif
