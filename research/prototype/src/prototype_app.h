/*
 * Copyright (c) 2019-2020, NVIDIA CORPORATION. All rights reserved.
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

#include <gst/gst.h>
//#include <glib.h>

//#include <stdio.h>
#include <stdbool.h>
//#include <stdlib.h>
//#include <string.h>

#include "gstnvdsmeta.h"
#include "nvds_version.h"
#include "nvdsmeta_schema.h"

//#include "deepstream_config.h"
//#include "deepstream_config_file_parser.h"
#include "deepstream_app.h"

#ifndef __PROTOTYPE_APP_H__
#define __PROTOTYPE_APP_H__

#define MAX_DISPLAY_LEN (64)
#define MAX_TIME_STAMP_LEN (64)
#define STREAMMUX_BUFFER_POOL_SIZE (16)

#define INOTIFY_EVENT_SIZE    (sizeof (struct inotify_event))
#define INOTIFY_EVENT_BUF_LEN (1024 * ( INOTIFY_EVENT_SIZE + 16))

#define IS_YAML(file) (g_str_has_suffix(file, ".yml") || g_str_has_suffix(file, ".yaml"))

/** @{
 * Macro's below and corresponding code-blocks are used to demonstrate
 * nvmsgconv + Broker Metadata manipulation possibility
 */
/**
 * 아래 매크로와 해당 코드 블록은 nvmsgconv 및 브로커 메타데이터 조작 가능
 * 성을 보여주기 위해 사용됩니다.
 */

/**
 * IMPORTANT Note 1:
 * The code within the check for model_used == APP_CONFIG_ANALYTICS_RESNET_PGIE_3SGIE_TYPE_COLOR_MAKE
 * is applicable as sample demo code for
 * configs that use resnet PGIE model
 * with class ID's: {0, 1, 2, 3} for {CAR, BICYCLE, PERSON, ROADSIGN}
 * followed by optional Tracker + 3 X SGIEs (Vehicle-Type,Color,Make)
 * only!
 * Please comment out the code if using any other
 * custom PGIE + SGIE combinations
 * and use the code as reference to write your own
 * NvDsEventMsgMeta generation code in generate_event_msg_meta()
 * function
 */
/**
 * 중요한 노트 1:
 * model_used == APP_CONFIG_ANALYTICS_RESNET_PGIE_3SGIE_TYPE_COLOR_MAKE에 대한 체크 내의 코드는 resnet
 * PGIE 모델을 사용하는 config에 대한 샘플 데모 코드로 적용됩니다.
 * 클래스 ID가 {0, 1, 2, 3}인 {CAR, BICYCLE, PERSON, ROADSIGN}에 해당합니다.
 * 그 뒤로 선택적으로 Tracker + 3개의 SGIE (Vehicle-Type, Color, Make)가 이어집니다.
 * 주의: 다른 사용자 정의 PGIE + SGIE 조합을 사용하는 경우 코드를 주석 처리하고, generate_event_msg_meta()
 * 함수에서 자체 NvDsEventMsgMeta 생성 코드를 작성하는 참고로 사용하세요.
*/
typedef enum
{
  APP_CONFIG_ANALYTICS_MODELS_UNKNOWN = 0,
  APP_CONFIG_ANALYTICS_RESNET_PGIE_3SGIE_TYPE_COLOR_MAKE = 1,
} AppConfigAnalyticsModel;

/**
 * IMPORTANT Note 2:
 * GENERATE_DUMMY_META_EXT macro implements code
 * that assumes APP_CONFIG_ANALYTICS_RESNET_PGIE_3SGIE_TYPE_COLOR_MAKE
 * case discussed above, and generate dummy metadata
 * for other classes like Person class
 *
 * Vehicle class schema meta (NvDsVehicleObject) is filled
 * in properly from Classifier-Metadata;
 * see in-code documentation and usage of
 * schema_fill_sample_sgie_vehicle_metadata()
 */
/**
 * 중요한 노트 2:
 * GENERATE_DUMMY_META_EXT 매크로는 위에서 논의한
 * APP_CONFIG_ANALYTICS_RESNET_PGIE_3SGIE_TYPE_COLOR_MAKE 경우를 가정하고
 * 다른 클래스인 Person 클래스와 같은 클래스에 대한 더미 메타데이터를 생성하는
 * 코드를 구현합니다.
 * 차량 클래스 스키마 메타 (NvDsVehicleObject)는 Classifier-Metadata에서
 * 올바르게 채워집니다.
 * schema_fill_sample_sgie_vehicle_metadata()의 인코드 문서 및 사용법을
 * 참조하세요.
*/
//#define GENERATE_DUMMY_META_EXT

/** Following class-ID's
 * used for demonstration code
 * assume an ITS detection model
 * which outputs CLASS_ID=0 for Vehicle class
 * and CLASS_ID=2 for Person class
 * and SGIEs X 3 same as the sample DS config for test5-app:
 * configs/test5_config_file_src_infer_tracker_sgie.txt
 */
/** 다음 클래스 ID들은
 * 데모 코드에 사용됩니다.
 * ITS 감지 모델을 가정하고,
 * 차량 클래스에 대한 CLASS_ID=0을 출력하고
 * 인간 클래스에 대한 CLASS_ID=2를 출력합니다.
 * 그리고 SGIEs X 3은 test5-app의 샘플 DS 구성과 동일합니다:
 * configs/test5_config_file_src_infer_tracker_sgie.txt
 */
#define SECONDARY_GIE_VEHICLE_TYPE_UNIQUE_ID  (4)
#define SECONDARY_GIE_VEHICLE_COLOR_UNIQUE_ID (5)
#define SECONDARY_GIE_VEHICLE_MAKE_UNIQUE_ID  (6)

#define RESNET10_PGIE_3SGIE_TYPE_COLOR_MAKECLASS_ID_CAR    (0)
#ifdef GENERATE_DUMMY_META_EXT
#define RESNET10_PGIE_3SGIE_TYPE_COLOR_MAKECLASS_ID_PERSON (2)
#endif
/** @} */


#ifdef EN_DEBUG
#define LOGD(...) printf(__VA_ARGS__)
#else
#define LOGD(...)
#endif

typedef struct
{
  gint anomaly_count;
  gint meta_number;
  struct timespec timespec_first_frame;
  GstClockTime gst_ts_first_frame;
  GMutex lock_stream_rtcp_sr;
  guint32 id;
  gint frameCount;
  GstClockTime last_ntp_time;
} StreamSourceInfo;

typedef struct
{
  StreamSourceInfo streams[MAX_SOURCE_BINS];
} TestAppCtx;

void
generate_event_msg_meta (AppCtx * appCtx, gpointer data, gint class_id, gboolean useTs,
    GstClockTime ts, gchar * src_uri, gint stream_id, guint sensor_id,
    NvDsObjectMeta * obj_params, float scaleW, float scaleH,
    NvDsFrameMeta * frame_meta);

#endif /**__PROTOTYPE_APP_H__*/
