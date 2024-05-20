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

#include <gst/gst.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>


#include "deepstream_app.h"

#define MAX_DISPLAY_LEN 64
static guint demux_batch_num = 0;

GST_DEBUG_CATEGORY_EXTERN (NVDS_APP);

GQuark _dsmeta_quark;

#define CEIL(a,b) ((a + b - 1) / b)

/**
 * @brief  (nvmsgconv->nvmsgbroker) 싱크-빈을 전체 DS 파이프라인에 추가하고
 *         common_elements.tee에 연결합니다. (이 티는
 *         공통 분석 경로를 Tiler/디스플레이 싱크로 연결하며,
 *         구성된 브로커 싱크가 있는 경우에는 해당 싱크로도 연결합니다.)
 *         참고: 파이프라인에 추가할 브로커 싱크가 없는 경우에는
 *         이 API는 TRUE를 반환합니다.
 *
 * @param  appCtx [IN] 앱 컨텍스트
 * @return 성공 시 TRUE; 그렇지 않으면 FALSE
 */
static gboolean add_and_link_broker_sink (AppCtx * appCtx);

/**
 * @brief  [sink] 그룹이 설정되어 있는지 확인합니다.
 *         주어진 source_id에 대해 설정된 source_id 그룹이 있는지 확인합니다.
 *         참고: tiler를 비활성화하고 따라서 개별 스트림을 위해 demuxer를 사용할 때만
 *         source_id 키와 이 API가 유효합니다.
 * @param  config [IN] DS 파이프라인 구성 구조체
 * @param  source_id [IN] 특정 [sink] 그룹을 검색할 source ID
 */
static gboolean is_sink_available_for_source_id (PrototypeConfig * config,
    guint source_id);

static NvDsSensorInfo* s_sensor_info_create(NvDsSensorInfo* sensor_info) {
  NvDsSensorInfo* sensorInfoToHash = (NvDsSensorInfo*)g_malloc0(sizeof(NvDsSensorInfo));
  *sensorInfoToHash = *sensor_info;
  sensorInfoToHash->sensor_id = (gchar const*)g_strdup(sensor_info->sensor_id);
  sensorInfoToHash->sensor_name = (gchar const*)g_strdup(sensor_info->sensor_name);
  sensorInfoToHash->uri = (gchar const*)g_strdup(sensor_info->uri);
  return sensorInfoToHash;
}

static void s_sensor_info_destroy(NvDsSensorInfo* sensor_info) {
  if(!sensor_info)
    return;
  if(sensor_info->sensor_id) {
    g_free((void*)sensor_info->sensor_id);
  }
  if(sensor_info->sensor_name) {
    g_free((void*)sensor_info->sensor_name);
  }

  g_free(sensor_info);
}

static void s_sensor_info_callback_stream_added (AppCtx *appCtx, NvDsSensorInfo* sensorInfo) {

  NvDsSensorInfo* sensorInfoToHash = s_sensor_info_create(sensorInfo);
  /** save the sensor info into the hash map */
  g_hash_table_insert (appCtx->sensorInfoHash, sensorInfo->source_id + (char *)NULL, sensorInfoToHash);
}

static void s_sensor_info_callback_stream_removed (AppCtx *appCtx, NvDsSensorInfo* sensorInfo) {

  NvDsSensorInfo* sensorInfoFromHash = get_sensor_info(appCtx, sensorInfo->source_id);
  /** remove the sensor info from the hash map */
  if(sensorInfoFromHash) {
    g_hash_table_remove(appCtx->sensorInfoHash, sensorInfo->source_id + (gchar*)NULL);
    s_sensor_info_destroy(sensorInfoFromHash);
  }
}

NvDsSensorInfo* get_sensor_info(AppCtx* appCtx, guint source_id) {
  NvDsSensorInfo* sensorInfo = (NvDsSensorInfo*)g_hash_table_lookup(appCtx->sensorInfoHash,
        source_id + (gchar*)NULL);
  return sensorInfo;
}

/* 참고: FPS 로깅을 위해 아래 콜백/함수가 정의되어 있습니다.
 * nvmultiurisrcbin을 사용하는 경우에 해당됩니다. */
static NvDsFPSSensorInfo* s_fps_sensor_info_create(NvDsFPSSensorInfo* sensor_info);
static void s_fps_sensor_info_destroy(NvDsFPSSensorInfo* sensor_info);
static NvDsFPSSensorInfo* get_fps_sensor_info(AppCtx* appCtx, guint source_id);

static NvDsFPSSensorInfo* s_fps_sensor_info_create(NvDsFPSSensorInfo* sensor_info) {
  NvDsFPSSensorInfo* fpssensorInfoToHash = (NvDsFPSSensorInfo*)g_malloc0(sizeof(NvDsFPSSensorInfo));
  *fpssensorInfoToHash = *sensor_info;
  fpssensorInfoToHash->uri = (gchar const*)g_strdup(sensor_info->uri);
  fpssensorInfoToHash->source_id = sensor_info->source_id;
  fpssensorInfoToHash->sensor_id = (gchar const*)g_strdup(sensor_info->sensor_id);
  fpssensorInfoToHash->sensor_name = (gchar const*)g_strdup(sensor_info->sensor_name);
  return fpssensorInfoToHash;
}

static void s_fps_sensor_info_destroy(NvDsFPSSensorInfo* sensor_info) {
  if(!sensor_info)
    return;
  if(sensor_info->sensor_id) {
    g_free((void*)sensor_info->sensor_id);
  }
  if(sensor_info->sensor_name) {
    g_free((void*)sensor_info->sensor_name);
  }
  if(sensor_info->uri) {
    g_free((void*)sensor_info->uri);
  }

  g_free(sensor_info);
}

static NvDsFPSSensorInfo* get_fps_sensor_info(AppCtx* appCtx, guint source_id) {
  NvDsFPSSensorInfo* sensorInfo = (NvDsFPSSensorInfo*)g_hash_table_lookup(appCtx->perf_struct.FPSInfoHash,
        GUINT_TO_POINTER(source_id));
  return sensorInfo;
}

static void s_fps_sensor_info_callback_stream_added (AppCtx *appCtx, NvDsFPSSensorInfo* sensorInfo) {

  NvDsFPSSensorInfo* fpssensorInfoToHash = s_fps_sensor_info_create(sensorInfo);
  /** save the sensor info into the hash map */
  g_hash_table_insert (appCtx->perf_struct.FPSInfoHash, GUINT_TO_POINTER(sensorInfo->source_id), fpssensorInfoToHash);
}

static void s_fps_sensor_info_callback_stream_removed (AppCtx *appCtx, NvDsFPSSensorInfo* sensorInfo) {

  NvDsFPSSensorInfo* fpsensorInfoFromHash = get_fps_sensor_info(appCtx, sensorInfo->source_id);
  /** remove the sensor info from the hash map */
  if(fpsensorInfoFromHash) {
    g_hash_table_remove(appCtx->perf_struct.FPSInfoHash, GUINT_TO_POINTER(sensorInfo->source_id));
    s_fps_sensor_info_destroy(fpsensorInfoFromHash);
  }
}

/**
 * callback function to receive messages from components
 * in the pipeline.
 */
static gboolean
bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  //printf(">>> [bus_callback]\n");
  AppCtx *appCtx = (AppCtx *) data;
  GST_CAT_DEBUG (NVDS_APP,
      "Received message on bus: source %s, msg_type %s",
      GST_MESSAGE_SRC_NAME (message), GST_MESSAGE_TYPE_NAME (message));
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_INFO:{
      printf(">>> [bus_callback] GST_MESSAGE_INFO:(%s)\n", GST_MESSAGE_TYPE_NAME(message));
      GError *error = NULL;
      gchar *debuginfo = NULL;
      gst_message_parse_info (message, &error, &debuginfo);
      g_printerr ("INFO from %s: %s\n",
          GST_OBJECT_NAME (message->src), error->message);
      if (debuginfo) {
        g_printerr ("Debug info: %s\n", debuginfo);
      }
      g_error_free (error);
      g_free (debuginfo);
      break;
    }
    case GST_MESSAGE_WARNING:{
      printf(">>> [bus_callback] GST_MESSAGE_WARNING(%s):\n", GST_MESSAGE_TYPE_NAME(message));
      GError *error = NULL;
      gchar *debuginfo = NULL;
      gst_message_parse_warning (message, &error, &debuginfo);
      g_printerr ("WARNING from %s: %s\n",
          GST_OBJECT_NAME (message->src), error->message);
      if (debuginfo) {
        g_printerr ("Debug info: %s\n", debuginfo);
      }
      g_error_free (error);
      g_free (debuginfo);
      break;
    }
    case GST_MESSAGE_ERROR:{
      printf(">>> [bus_callback] GST_MESSAGE_ERROR(%s):\n", GST_MESSAGE_TYPE_NAME(message));
      GError *error = NULL;
      gchar *debuginfo = NULL;
      const gchar *attempts_error =
          "Reconnection attempts exceeded for all sources or EOS received.";
      guint i = 0;
      gst_message_parse_error (message, &error, &debuginfo);

      if (strstr (error->message, attempts_error)) {
        g_print
            ("Reconnection attempt  exceeded or EOS received for all sources."
            " Exiting.\n");
        g_error_free (error);
        g_free (debuginfo);
        appCtx->return_value = 0;
        appCtx->quit = TRUE;
        return TRUE;
      }

      g_printerr ("ERROR from %s: %s\n",
          GST_OBJECT_NAME (message->src), error->message);
      if (debuginfo) {
        g_printerr ("Debug info: %s\n", debuginfo);
      }

      NvDsSrcParentBin *bin = &appCtx->pipeline.multi_src_bin;
      GstElement *msg_src_elem = (GstElement *) GST_MESSAGE_SRC (message);
      gboolean bin_found = FALSE;
      /* Find the source bin which generated the error. */
      while (msg_src_elem && !bin_found) {
        for (i = 0; i < bin->num_bins && !bin_found; i++) {
          if (bin->sub_bins[i].src_elem == msg_src_elem ||
              bin->sub_bins[i].bin == msg_src_elem) {
            bin_found = TRUE;
            break;
          }
        }
        msg_src_elem = GST_ELEMENT_PARENT (msg_src_elem);
      }

      if ((i != bin->num_bins) &&
          (appCtx->config.multi_source_config[0].type == NV_DS_SOURCE_RTSP)) {
        // Error from one of RTSP source.
        NvDsSrcBin *subBin = &bin->sub_bins[i];

        if (!subBin->reconfiguring ||
            g_strrstr (debuginfo, "500 (Internal Server Error)")) {
          subBin->reconfiguring = TRUE;
          g_timeout_add (0, reset_source_pipeline, subBin);
        }
        g_error_free (error);
        g_free (debuginfo);
        return TRUE;
      }

      if (appCtx->config.multi_source_config[0].type ==
          NV_DS_SOURCE_CAMERA_V4L2) {
        if (g_strrstr (debuginfo, "reason not-negotiated (-4)")) {
          NVGSTDS_INFO_MSG_V
              ("incorrect camera parameters provided, please provide supported resolution and frame rate\n");
        }

        if (g_strrstr (debuginfo, "Buffer pool activation failed")) {
          NVGSTDS_INFO_MSG_V ("usb bandwidth might be saturated\n");
        }
      }

      g_error_free (error);
      g_free (debuginfo);
      appCtx->return_value = -1;
      appCtx->quit = TRUE;
      break;
    }
    case GST_MESSAGE_STATE_CHANGED:{
      printf(">>> [bus_callback] GST_MESSAGE_STATE_CHANGED(%s):\n", GST_MESSAGE_TYPE_NAME(message));
      
      GstState oldstate, newstate;
      gst_message_parse_state_changed (message, &oldstate, &newstate, NULL);
      if (GST_ELEMENT (GST_MESSAGE_SRC (message)) == appCtx->pipeline.pipeline) {
        switch (newstate) {
          case GST_STATE_PLAYING:
            NVGSTDS_INFO_MSG_V ("Pipeline running\n");
            GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (appCtx->pipeline.
                    pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "ds-app-playing");
            break;
          case GST_STATE_PAUSED:
            if (oldstate == GST_STATE_PLAYING) {
              NVGSTDS_INFO_MSG_V ("Pipeline paused\n");
            }
            break;
          case GST_STATE_READY:
            GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (appCtx->
                    pipeline.pipeline), GST_DEBUG_GRAPH_SHOW_ALL,
                "ds-app-ready");
            if (oldstate == GST_STATE_NULL) {
              NVGSTDS_INFO_MSG_V ("Pipeline ready\n");
            } else {
              NVGSTDS_INFO_MSG_V ("Pipeline stopped\n");
            }
            break;
          case GST_STATE_NULL:
            g_mutex_lock (&appCtx->app_lock);
            g_cond_broadcast (&appCtx->app_cond);
            g_mutex_unlock (&appCtx->app_lock);
            break;
          default:
            break;
        }
      }
      break;
    }
    case GST_MESSAGE_EOS:{
      printf(">>> [bus_callback] GST_MESSAGE_EOS(%s):\n", GST_MESSAGE_TYPE_NAME(message));
      /*
       * 일반적인 상황에서는 g_main_loop_quit()을 사용하여 루프를 종료하고
       * 리소스를 해제합니다. 그러나 이 응용 프로그램은 여러 파이프라인을
       * 구성 파일을 통해 실행할 수 있으므로, 모든 파이프라인이 완료될 때까지
       * 기다려야 합니다.
       */
      NVGSTDS_INFO_MSG_V ("Received EOS. Exiting ...\n");
      appCtx->quit = TRUE;
      return FALSE;
      break;
    }
    case GST_MESSAGE_ELEMENT:{
      printf(">>> [bus_callback] GST_MESSAGE_ELEMENT(%s):\n", GST_MESSAGE_TYPE_NAME(message));
      if(gst_nvmessage_is_stream_add(message)) {
	      g_mutex_lock (&(appCtx->perf_struct).struct_lock);

        NvDsSensorInfo sensorInfo = {0};
        gst_nvmessage_parse_stream_add(message, &sensorInfo);
        g_print("new stream added [%d:%s:%s]\n\n\n\n", sensorInfo.source_id, sensorInfo.sensor_id, sensorInfo.sensor_name);
        /** Callback */
        s_sensor_info_callback_stream_added(appCtx, &sensorInfo);
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (appCtx->
                    pipeline.pipeline), GST_DEBUG_GRAPH_SHOW_ALL,
                "ds-app-added");
        NvDsFPSSensorInfo fpssensorInfo = {0};
        gst_nvmessage_parse_fps_stream_add(message, &fpssensorInfo);
        s_fps_sensor_info_callback_stream_added(appCtx, &fpssensorInfo);
      	g_mutex_unlock (&(appCtx->perf_struct).struct_lock);
      }
      if(gst_nvmessage_is_stream_remove(message)) {
        g_mutex_lock (&(appCtx->perf_struct).struct_lock);
        NvDsSensorInfo sensorInfo = {0};
        gst_nvmessage_parse_stream_remove(message, &sensorInfo);
        g_print("new stream removed [%d:%s]\n\n\n\n", sensorInfo.source_id, sensorInfo.sensor_id);
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (appCtx->
                pipeline.pipeline), GST_DEBUG_GRAPH_SHOW_ALL,
                "ds-app-removed");
        /** Callback */
        s_sensor_info_callback_stream_removed(appCtx, &sensorInfo);
        NvDsFPSSensorInfo fpssensorInfo = {0};
        gst_nvmessage_parse_fps_stream_remove(message, &fpssensorInfo);
	      s_fps_sensor_info_callback_stream_removed(appCtx, &fpssensorInfo);
	      g_mutex_unlock (&(appCtx->perf_struct).struct_lock);
      }
      break;
    }
    // case GST_MESSAGE_TAG:
    //   printf(">>> [bus_callback] GST_MESSAGE_TAG(%s):\n", GST_MESSAGE_TYPE_NAME(message));
    //   GstTagList *tags = NULL;
    //   gchar *tag_string = NULL;

    //   // 파싱
    //   gst_message_parse_tag(message, &tags);

    //   // 태그가 있는 경우
    //   if (tags) {
    //       // 태그를 문자열로 변환
    //       tag_string = gst_tag_list_to_string(tags);
    //       g_print("Tags received: %s\n", tag_string);
    //       g_free(tag_string);
    //       gst_tag_list_unref(tags);
    //   } else {
    //       g_print("No tags found.\n");
    //   }
    //   break;
    default:
      //printf(">>> [bus_callback] default(%s):\n", GST_MESSAGE_TYPE_NAME(message));
      break;
  }
  return TRUE;
}

static gint
component_id_compare_func (gconstpointer a, gconstpointer b)
{
  NvDsClassifierMeta *cmetaa = (NvDsClassifierMeta *) a;
  NvDsClassifierMeta *cmetab = (NvDsClassifierMeta *) b;

  if (cmetaa->unique_component_id < cmetab->unique_component_id)
    return -1;
  if (cmetaa->unique_component_id > cmetab->unique_component_id)
    return 1;
  return 0;
}

/**
 * 첨부된 메타데이터를 처리하는 함수입니다. 이것은 단순히 데모용이며
 * 필요하지 않은 경우 제거할 수 있습니다.
 * 여기서는 다른 유형/클래스의 객체에 대해 다른 색상과 크기의 바운딩 박스를 사용하는 방법을 보여줍니다.
 * 또한 객체의 다른 라벨 (PGIE + SGIEs)을 하나의 문자열로 결합하는 방법을 보여줍니다.
 */
static void
process_meta (AppCtx * appCtx, NvDsBatchMeta * batch_meta)
{
  // 단일 소스의 경우 항상 텍스트를 데먹서 또는 타일러와 함께 표시합니다.
  if (!appCtx->config.tiled_display_config.enable ||
      appCtx->config.num_source_sub_bins == 1) {
    appCtx->show_bbox_text = 1;
  }

  for (NvDsMetaList * l_frame = batch_meta->frame_meta_list; l_frame != NULL;
      l_frame = l_frame->next) {
    NvDsFrameMeta *frame_meta = (NvDsFrameMeta *) l_frame->data;
    for (NvDsMetaList * l_obj = frame_meta->obj_meta_list; l_obj != NULL;
        l_obj = l_obj->next) {
      NvDsObjectMeta *obj = (NvDsObjectMeta *) l_obj->data;
      gint class_index = obj->class_id;
      NvDsGieConfig *gie_config = NULL;
      gchar *str_ins_pos = NULL;

      if (obj->unique_component_id ==
          (gint) appCtx->config.primary_gie_config.unique_id) {
        gie_config = &appCtx->config.primary_gie_config;
      } else {
        for (gint i = 0; i < (gint) appCtx->config.num_secondary_gie_sub_bins;
            i++) {
          gie_config = &appCtx->config.secondary_gie_sub_bin_config[i];
          if (obj->unique_component_id == (gint) gie_config->unique_id) {
            break;
          }
          gie_config = NULL;
        }
      }
      g_free (obj->text_params.display_text);
      obj->text_params.display_text = NULL;

      if (gie_config != NULL) {
        if (g_hash_table_contains (gie_config->bbox_border_color_table,
                class_index + (gchar *) NULL)) {
          obj->rect_params.border_color = *((NvOSD_ColorParams *)
              g_hash_table_lookup (gie_config->bbox_border_color_table,
                  class_index + (gchar *) NULL));
        } else {
          obj->rect_params.border_color = gie_config->bbox_border_color;
        }
        obj->rect_params.border_width = appCtx->config.osd_config.border_width;

        if (g_hash_table_contains (gie_config->bbox_bg_color_table,
                class_index + (gchar *) NULL)) {
          obj->rect_params.has_bg_color = 1;
          obj->rect_params.bg_color = *((NvOSD_ColorParams *)
              g_hash_table_lookup (gie_config->bbox_bg_color_table,
                  class_index + (gchar *) NULL));
        } else {
          obj->rect_params.has_bg_color = 0;
        }
      }

      if (!appCtx->show_bbox_text)
        continue;

      obj->text_params.x_offset = obj->rect_params.left;
      obj->text_params.y_offset = obj->rect_params.top - 30;
      obj->text_params.font_params.font_color =
          appCtx->config.osd_config.text_color;
      obj->text_params.font_params.font_size =
          appCtx->config.osd_config.text_size;
      obj->text_params.font_params.font_name = appCtx->config.osd_config.font;
      if (appCtx->config.osd_config.text_has_bg) {
        obj->text_params.set_bg_clr = 1;
        obj->text_params.text_bg_clr = appCtx->config.osd_config.text_bg_color;
      }

      obj->text_params.display_text = (char *) g_malloc (128);
      obj->text_params.display_text[0] = '\0';
      str_ins_pos = obj->text_params.display_text;

      if (obj->obj_label[0] != '\0')
        sprintf (str_ins_pos, "%s", obj->obj_label);
      str_ins_pos += strlen (str_ins_pos);

      if (obj->object_id != UNTRACKED_OBJECT_ID) {
        /** object_id is a 64-bit sequential value;
         * but considering the display aesthetic,
         * trimming to lower 32-bits */
        if (appCtx->config.tracker_config.display_tracking_id) {
          guint64 const LOW_32_MASK = 0x00000000FFFFFFFF;
          sprintf (str_ins_pos, " %lu", (obj->object_id & LOW_32_MASK));
          str_ins_pos += strlen (str_ins_pos);
        }
      }

      obj->classifier_meta_list =
          g_list_sort (obj->classifier_meta_list, component_id_compare_func);
      for (NvDsMetaList * l_class = obj->classifier_meta_list; l_class != NULL;
          l_class = l_class->next) {
        NvDsClassifierMeta *cmeta = (NvDsClassifierMeta *) l_class->data;
        for (NvDsMetaList * l_label = cmeta->label_info_list; l_label != NULL;
            l_label = l_label->next) {
          NvDsLabelInfo *label = (NvDsLabelInfo *) l_label->data;
          if (label->pResult_label) {
            sprintf (str_ins_pos, " %s", label->pResult_label);
          } else if (label->result_label[0] != '\0') {
            sprintf (str_ins_pos, " %s", label->result_label);
          }
          str_ins_pos += strlen (str_ins_pos);
        }

      }
    }
  }
}

/**
 * 추론된 버퍼와 해당 메타데이터를 처리하는 함수입니다.
 * 또한 애플리케이션별 메타데이터 (예: 시계, 분석 출력 등)를 첨부할 수 있는 기회를 제공합니다.
 */
static void
process_buffer (GstBuffer * buf, AppCtx * appCtx)
{
  NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta (buf);
  if (!batch_meta) {
    NVGSTDS_WARN_MSG_V ("Batch meta not found for buffer %p", buf);
    return;
  }
  process_meta (appCtx, batch_meta);
}

/**
 * 주요 추론 결과를 얻기 위한 버퍼 프로브 함수입니다.
 * 여기서는 bounding box 좌표를 kitti 형식으로 덤프하는 방법을 보여줍니다.
 */
static GstPadProbeReturn
gie_primary_processing_done_buf_prob (GstPad * pad, GstPadProbeInfo * info,
    gpointer u_data)
{
  GstBuffer *buf = (GstBuffer *) info->data;
  AppCtx *appCtx = (AppCtx *) u_data;
  NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta (buf);
  if (!batch_meta) {
    NVGSTDS_WARN_MSG_V ("Batch meta not found for buffer %p", buf);
    return GST_PAD_PROBE_OK;
  }

  return GST_PAD_PROBE_OK;
}

/**
 * 모든 추론(주요 + 보조)이 완료된 후 결과를 얻기 위한 프로브 함수입니다.
 * 이는 OSD가 비활성화된 경우 OSD 또는 싱크 바로 이전에 발생합니다.
 */
static GstPadProbeReturn
gie_processing_done_buf_prob (GstPad * pad, GstPadProbeInfo * info,
    gpointer u_data)
{
  GstBuffer *buf = (GstBuffer *) info->data;
  PrototypeInstanceBin *instance_bin = (PrototypeInstanceBin *) u_data;
  AppCtx *appCtx = instance_bin->appCtx;

  if (gst_buffer_is_writable (buf))
    process_buffer (buf, appCtx);
  return GST_PAD_PROBE_OK;
}

/**
 * 트래커 이후의 버퍼 프로브 함수입니다.
 */
static GstPadProbeReturn
analytics_done_buf_prob (GstPad * pad, GstPadProbeInfo * info, gpointer u_data)
{
  PrototypeCommonElements *common_elements = (PrototypeCommonElements *) u_data;
  AppCtx *appCtx = common_elements->appCtx;
  GstBuffer *buf = (GstBuffer *) info->data;
  NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta (buf);
  if (!batch_meta) {
    NVGSTDS_WARN_MSG_V ("Batch meta not found for buffer %p", buf);
    return GST_PAD_PROBE_OK;
  }

  if (appCtx->bbox_generated_post_analytics_cb) {
    appCtx->bbox_generated_post_analytics_cb (appCtx, buf, batch_meta);
  }
  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
latency_measurement_buf_prob (GstPad * pad, GstPadProbeInfo * info,
    gpointer u_data)
{
  AppCtx *appCtx = (AppCtx *) u_data;
  guint i = 0, num_sources_in_batch = 0;
  if (nvds_enable_latency_measurement) {
    GstBuffer *buf = (GstBuffer *) info->data;
    NvDsFrameLatencyInfo *latency_info = NULL;
    g_mutex_lock (&appCtx->latency_lock);
    latency_info = appCtx->latency_info;
    guint64 batch_num= GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(pad),"latency-batch-num"));
    g_print("\n************BATCH-NUM = %lu**************\n",batch_num);

    num_sources_in_batch = nvds_measure_buffer_latency (buf, latency_info);

    for (i = 0; i < num_sources_in_batch; i++) {
      g_print ("Source id = %d Frame_num = %d Frame latency = %lf (ms) \n",
          latency_info[i].source_id,
          latency_info[i].frame_num, latency_info[i].latency);
    }
    g_mutex_unlock (&appCtx->latency_lock);
    g_object_set_data(G_OBJECT(pad),"latency-batch-num",GSIZE_TO_POINTER(batch_num+1));
  }

  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
demux_latency_measurement_buf_prob (GstPad * pad, GstPadProbeInfo * info,
    gpointer u_data)
{
  AppCtx *appCtx = (AppCtx *) u_data;
  guint i = 0, num_sources_in_batch = 0;
  if (nvds_enable_latency_measurement) {
    GstBuffer *buf = (GstBuffer *) info->data;
    NvDsFrameLatencyInfo *latency_info = NULL;
    g_mutex_lock (&appCtx->latency_lock);
    latency_info = appCtx->latency_info;
    g_print ("\n************DEMUX BATCH-NUM = %d**************\n",
        demux_batch_num);
    num_sources_in_batch = nvds_measure_buffer_latency (buf, latency_info);

    for (i = 0; i < num_sources_in_batch; i++) {
      g_print ("Source id = %d Frame_num = %d Frame latency = %lf (ms) \n",
          latency_info[i].source_id,
          latency_info[i].frame_num, latency_info[i].latency);
    }
    g_mutex_unlock (&appCtx->latency_lock);
    demux_batch_num++;
  }

  return GST_PAD_PROBE_OK;
}

static gboolean
add_and_link_broker_sink (AppCtx * appCtx)
{
  PrototypeConfig *config = &appCtx->config;
  /** 
   * 오직 첫 번째 instance_bin 브로커 싱크만 사용됩니다.
   * 왜냐하면 N 개의 소스에 대해 단 하나의 분석 경로만 있기 때문입니다.
   * 주의: type=6 (NV_DS_SINK_MSG_CONV_BROKER)인 [sink] 그룹은 하나만 존재해야 합니다.
   * a) 여러 개의 그룹을 설정하는 것은 하나의 분석 파이프가 브로커 싱크를 생성하기 때문에 의미가 없습니다.
   * b) 사용자가 구성 파일에서 여러 브로커 싱크를 구성하는 경우, 등장 순서대로 첫 번째 것만 고려됩니다.
   *    그리고 다른 것들은 무시됩니다.
   * c) 이상적으로는 문서화되어야 합니다 (또는 명백해야 함):
   *    type=6 (NV_DS_SINK_MSG_CONV_BROKER)인 여러 [sink] 그룹은 잘못된 것입니다.
   */
  PrototypeInstanceBin *instance_bin = &appCtx->pipeline.instance_bins[0];
  PrototypePipeline *pipeline = &appCtx->pipeline;

  for (guint i = 0; i < config->num_sink_sub_bins; i++) {
    if (config->sink_bin_sub_bin_config[i].type == NV_DS_SINK_MSG_CONV_BROKER) {
      if (!pipeline->common_elements.tee) {
        NVGSTDS_ERR_MSG_V
            ("%s failed; broker added without analytics; check config file\n",
            __func__);
        return FALSE;
      }
      /** add the broker sink bin to pipeline */
      if (!gst_bin_add (GST_BIN (pipeline->pipeline),
              instance_bin->sink_bin.sub_bins[i].bin)) {
        return FALSE;
      }
      /** link the broker sink bin to the common_elements tee
       * (The tee after nvinfer -> tracker (optional) -> sgies (optional) block) */
      if (!link_element_to_tee_src_pad (pipeline->common_elements.tee,
              instance_bin->sink_bin.sub_bins[i].bin)) {
        return FALSE;
      }
    }
  }
  return TRUE;
}

static gboolean
create_demux_pipeline (AppCtx * appCtx, guint index)
{
  printf(">>> [create_demux_pipeline]\n");
  gboolean ret = FALSE;
  PrototypeConfig *config = &appCtx->config;
  PrototypeDemuxInstanceBin *demux_instance_bin = &appCtx->pipeline.demux_instance_bins[index];
  GstElement *last_elem;
  gchar elem_name[32];

  g_snprintf (elem_name, 32, "processing_demux_bin_%d", index);
  demux_instance_bin->bin = gst_bin_new (elem_name);

  if (!create_demux_sink_bin (config->num_sink_sub_bins,
          config->sink_bin_sub_bin_config, &demux_instance_bin->demux_sink_bin,
          config->sink_bin_sub_bin_config[index].source_id)) {
    goto done;
  }

  gst_bin_add (GST_BIN (demux_instance_bin->bin), demux_instance_bin->demux_sink_bin.bin);
  last_elem = demux_instance_bin->demux_sink_bin.bin;

  if (config->osd_config.enable) {
    if (!create_osd_bin (&config->osd_config, &demux_instance_bin->osd_bin)) {
      goto done;
    }

    gst_bin_add (GST_BIN (demux_instance_bin->bin), demux_instance_bin->osd_bin.bin);
    NVGSTDS_LINK_ELEMENT (demux_instance_bin->osd_bin.bin, last_elem);
    last_elem = demux_instance_bin->osd_bin.bin;
  }

  NVGSTDS_BIN_ADD_GHOST_PAD (demux_instance_bin->bin, last_elem, "sink");
  if (config->osd_config.enable) {
    NVGSTDS_ELEM_ADD_PROBE (demux_instance_bin->demux_instance_bin_probe_id,
        demux_instance_bin->osd_bin.nvosd, "sink",
        gie_processing_done_buf_prob, GST_PAD_PROBE_TYPE_BUFFER, demux_instance_bin);
  }

  ret = TRUE;
done:
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

/**
 * 스트림 수에 의존하는 파이프라인에 구성 요소를 추가하는 함수입니다.
 * 이러한 구성 요소는 단일 버퍼에서 작동합니다. 타일링이 사용되는 경우 단일 인스턴스가 생성되고,
 * 그렇지 않으면 < N > 개의 스트림에 대해 < N > 개의 이러한 인스턴스가 생성됩니다.
 */
static gboolean
create_processing_instance (AppCtx * appCtx, guint index)
{
  gboolean ret = FALSE;
  PrototypeConfig *config = &appCtx->config;
  PrototypeInstanceBin *instance_bin = &appCtx->pipeline.instance_bins[index];
  GstElement *last_elem;
  gchar elem_name[32];

  instance_bin->appCtx = appCtx;

  g_snprintf (elem_name, 32, "processing_bin_%d", index);
  instance_bin->bin = gst_bin_new (elem_name);

  if (!create_sink_bin (config->num_sink_sub_bins,
          config->sink_bin_sub_bin_config, &instance_bin->sink_bin, index)) {
    goto done;
  }

  gst_bin_add (GST_BIN (instance_bin->bin), instance_bin->sink_bin.bin);
  last_elem = instance_bin->sink_bin.bin;

  if (config->osd_config.enable) {
    if (!create_osd_bin (&config->osd_config, &instance_bin->osd_bin)) {
      goto done;
    }

    gst_bin_add (GST_BIN (instance_bin->bin), instance_bin->osd_bin.bin);

    NVGSTDS_LINK_ELEMENT (instance_bin->osd_bin.bin, last_elem);

    last_elem = instance_bin->osd_bin.bin;
  }

  NVGSTDS_BIN_ADD_GHOST_PAD (instance_bin->bin, last_elem, "sink");
  if (config->osd_config.enable) {
    NVGSTDS_ELEM_ADD_PROBE (instance_bin->instance_bin_probe_id,
        instance_bin->osd_bin.nvosd, "sink",
        gie_processing_done_buf_prob, GST_PAD_PROBE_TYPE_BUFFER, instance_bin);
  }

  ret = TRUE;
done:
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

/**
 * 파이프라인의 공통 요소(주요 추론, 추적기, 보조 추론)를 생성하는 함수입니다.
 * 이러한 구성 요소들은 파이프라인의 모든 스트림에서 묶인 데이터를 처리합니다.
 * 따라서 파이프라인의 스트림 수에 독립적입니다.
 */
static gboolean
create_common_elements (PrototypeConfig * config, PrototypePipeline * pipeline,
    GstElement ** sink_elem, GstElement ** src_elem,
    bbox_generated_post_analytics_callback bbox_generated_post_analytics_cb)
{
  gboolean ret = FALSE;
  *sink_elem = *src_elem = NULL;

  if (config->primary_gie_config.enable) {
    if (config->num_secondary_gie_sub_bins > 0) {
      if (!create_secondary_gie_bin (config->num_secondary_gie_sub_bins,
              config->primary_gie_config.unique_id,
              config->secondary_gie_sub_bin_config,
              &pipeline->common_elements.secondary_gie_bin)) {
        goto done;
      }
      gst_bin_add (GST_BIN (pipeline->pipeline),
          pipeline->common_elements.secondary_gie_bin.bin);
      if (!*src_elem) {
        *src_elem = pipeline->common_elements.secondary_gie_bin.bin;
      }
      if (*sink_elem) {
        NVGSTDS_LINK_ELEMENT (pipeline->common_elements.secondary_gie_bin.bin,
            *sink_elem);
      }
      *sink_elem = pipeline->common_elements.secondary_gie_bin.bin;
    }
  }

  if (config->tracker_config.enable) {
    if (!create_tracking_bin (&config->tracker_config,
            &pipeline->common_elements.tracker_bin)) {
      g_print ("creating tracker bin failed\n");
      goto done;
    }
    gst_bin_add (GST_BIN (pipeline->pipeline),
        pipeline->common_elements.tracker_bin.bin);
    if (!*src_elem) {
      *src_elem = pipeline->common_elements.tracker_bin.bin;
    }
    if (*sink_elem) {
      NVGSTDS_LINK_ELEMENT (pipeline->common_elements.tracker_bin.bin,
          *sink_elem);
    }
    *sink_elem = pipeline->common_elements.tracker_bin.bin;
  }

  if (config->primary_gie_config.enable) {
    if (!create_primary_gie_bin (&config->primary_gie_config,
            &pipeline->common_elements.primary_gie_bin)) {
      goto done;
    }
    gst_bin_add (GST_BIN (pipeline->pipeline),
        pipeline->common_elements.primary_gie_bin.bin);
    if (*sink_elem) {
      NVGSTDS_LINK_ELEMENT (pipeline->common_elements.primary_gie_bin.bin,
          *sink_elem);
    }
    *sink_elem = pipeline->common_elements.primary_gie_bin.bin;
    if (!*src_elem) {
      *src_elem = pipeline->common_elements.primary_gie_bin.bin;
    }
    NVGSTDS_ELEM_ADD_PROBE (pipeline->
        common_elements.primary_bbox_buffer_probe_id,
        pipeline->common_elements.primary_gie_bin.bin, "src",
        gie_primary_processing_done_buf_prob, GST_PAD_PROBE_TYPE_BUFFER,
        pipeline->common_elements.appCtx);
  }

  if (*src_elem) {
    NVGSTDS_ELEM_ADD_PROBE (pipeline->
        common_elements.primary_bbox_buffer_probe_id, *src_elem, "src",
        analytics_done_buf_prob, GST_PAD_PROBE_TYPE_BUFFER,
        &pipeline->common_elements);

    pipeline->common_elements.tee =
        gst_element_factory_make (NVDS_ELEM_TEE, "common_analytics_tee");
    if (!pipeline->common_elements.tee) {
      NVGSTDS_ERR_MSG_V ("Failed to create element 'common_analytics_tee'");
      goto done;
    }

    gst_bin_add (GST_BIN (pipeline->pipeline), pipeline->common_elements.tee);

    NVGSTDS_LINK_ELEMENT (*src_elem, pipeline->common_elements.tee);
    *src_elem = pipeline->common_elements.tee;
  }

  ret = TRUE;
done:
  return ret;
}

static gboolean
is_sink_available_for_source_id (PrototypeConfig * config, guint source_id)
{
  for (guint j = 0; j < config->num_sink_sub_bins; j++) {
    if (config->sink_bin_sub_bin_config[j].enable &&
        config->sink_bin_sub_bin_config[j].source_id == source_id &&
        config->sink_bin_sub_bin_config[j].link_to_demux == FALSE) {
      return TRUE;
    }
  }
  return FALSE;
}

/**
 * 파이프라인을 생성하는 주요 함수입니다.
 */
gboolean
create_pipeline (AppCtx * appCtx,
    bbox_generated_post_analytics_callback bbox_generated_post_analytics_cb,
    perf_callback perf_cb)
{
  gboolean ret = FALSE;
  PrototypePipeline *pipeline = &appCtx->pipeline;
  PrototypeConfig *config = &appCtx->config;
  GstBus *bus;
  GstElement *last_elem;
  GstElement *tmp_elem1;
  GstElement *tmp_elem2;
  guint i;
  GstPad *fps_pad = NULL;
  gulong latency_probe_id;

  _dsmeta_quark = g_quark_from_static_string (NVDS_META_STRING);

  appCtx->bbox_generated_post_analytics_cb = bbox_generated_post_analytics_cb;
  appCtx->sensorInfoHash = g_hash_table_new (NULL, NULL);
  appCtx->perf_struct.FPSInfoHash = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);

  if (config->osd_config.num_out_buffers < 8) {
    config->osd_config.num_out_buffers = 8;
  }

  pipeline->pipeline = gst_pipeline_new ("pipeline");
  if (!pipeline->pipeline) {
    NVGSTDS_ERR_MSG_V ("Failed to create pipeline");
    goto done;
  }

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline->pipeline));
  guint bus_id = gst_bus_add_watch (bus, bus_callback, appCtx);
  gst_object_unref (bus);

  for (guint i = 0; i < config->num_sink_sub_bins; i++) {
    NvDsSinkSubBinConfig *sink_config = &config->sink_bin_sub_bin_config[i];
    switch (sink_config->type) {
      case NV_DS_SINK_FAKE:
#ifndef IS_TEGRA
      case NV_DS_SINK_RENDER_EGL:
#else
      case NV_DS_SINK_RENDER_3D:
#endif
      case NV_DS_SINK_RENDER_DRM:
        /* Set the "qos" property of sink, if not explicitly specified in the
           config. */
        if (!sink_config->render_config.qos_value_specified) {
          sink_config->render_config.qos = FALSE;
        }
      default:
        break;
    }
  }
  /*
   * 설정 파일에 있는 설정에 기반하여 멀티플렉서 및 < N > 소스 구성 요소를 파이프라인에 추가합니다.
   */
  if (!create_multi_source_bin (config->num_source_sub_bins,
          config->multi_source_config, &pipeline->multi_src_bin))
  {
    goto done;
  }
  gst_bin_add (GST_BIN (pipeline->pipeline), pipeline->multi_src_bin.bin);


  if (config->streammux_config.is_parsed) {
    if (!set_streammux_properties (&config->streammux_config,
            pipeline->multi_src_bin.streammux)) {
      NVGSTDS_WARN_MSG_V ("Failed to set streammux properties");
    }
  }


  if (appCtx->latency_info == NULL) {
    appCtx->latency_info = (NvDsFrameLatencyInfo *)
        calloc (1, config->streammux_config.batch_size *
        sizeof (NvDsFrameLatencyInfo));
  }

  /** a tee after the tiler which shall be connected to sink(s) */
  pipeline->tiler_tee = gst_element_factory_make (NVDS_ELEM_TEE, "tiler_tee");
  if (!pipeline->tiler_tee) {
    NVGSTDS_ERR_MSG_V ("Failed to create element 'tiler_tee'");
    goto done;
  }
  gst_bin_add (GST_BIN (pipeline->pipeline), pipeline->tiler_tee);

  /** 타일러 + 데먹서 병렬 사용 사례 */
  if (config->tiled_display_config.enable ==
      NV_DS_TILED_DISPLAY_ENABLE_WITH_PARALLEL_DEMUX) {
    pipeline->demuxer =
        gst_element_factory_make (NVDS_ELEM_STREAM_DEMUX, "demuxer");
    if (!pipeline->demuxer) {
      NVGSTDS_ERR_MSG_V ("Failed to create element 'demuxer'");
      goto done;
    }
    gst_bin_add (GST_BIN (pipeline->pipeline), pipeline->demuxer);

    /** 참고:
     * 데먹서 출력은 하나의 소스에 대해서만 지원됩니다.
     * link_to_demux=1로 구성된 여러 [sink] 그룹이 있는 경우,
     * link_to_demux=1로 설정된 모든 [sink] 그룹에 대해 첫 번째 [sink]만
     * 구성될 것입니다.
     */
    {
      gchar pad_name[16];
      GstPad *demux_src_pad;

      i = 0;
      if (!create_demux_pipeline (appCtx, i)) {
        goto done;
      }

      for (i = 0; i < config->num_sink_sub_bins; i++) {
        if (config->sink_bin_sub_bin_config[i].link_to_demux == TRUE) {
          g_snprintf (pad_name, 16, "src_%02d",
              config->sink_bin_sub_bin_config[i].source_id);
          break;
        }
      }

      if (i >= config->num_sink_sub_bins) {
        g_print
            ("\n\nError : sink for demux (use link-to-demux-only property) is not provided in the config file\n\n");
        goto done;
      }

      i = 0;

      gst_bin_add (GST_BIN (pipeline->pipeline),
          pipeline->demux_instance_bins[i].bin);

      demux_src_pad = gst_element_request_pad_simple (pipeline->demuxer, pad_name);
      NVGSTDS_LINK_ELEMENT_FULL (pipeline->demuxer, pad_name,
          pipeline->demux_instance_bins[i].bin, "sink");
      gst_object_unref (demux_src_pad);

      NVGSTDS_ELEM_ADD_PROBE (latency_probe_id,
          appCtx->pipeline.demux_instance_bins[i].demux_sink_bin.bin,
          "sink",
          demux_latency_measurement_buf_prob, GST_PAD_PROBE_TYPE_BUFFER,
          appCtx);
      latency_probe_id = latency_probe_id;
    }

    last_elem = pipeline->demuxer;
    link_element_to_tee_src_pad (pipeline->tiler_tee, last_elem);
    last_elem = pipeline->tiler_tee;
  }

  if (config->tiled_display_config.enable) {
    /* 타일러는 모든 소스에 대해 단일 합성된 버퍼를 생성합니다. 따라서
     * 하나의 처리 인스턴스만 생성하면 됩니다. */
    if (!create_processing_instance (appCtx, 0)) {
      goto done;
    }
    // create and add tiling component to pipeline.
    // 타일링 구성 요소를 파이프라인에 생성하고 추가합니다.
    if (config->tiled_display_config.columns *
        config->tiled_display_config.rows < config->num_source_sub_bins) {
      if (config->tiled_display_config.columns == 0) {
        config->tiled_display_config.columns =
            (guint) (sqrt (config->num_source_sub_bins) + 0.5);
      }
      config->tiled_display_config.rows =
          (guint) ceil (1.0 * config->num_source_sub_bins /
          config->tiled_display_config.columns);
      NVGSTDS_WARN_MSG_V
          ("Num of Tiles less than number of sources, readjusting to "
          "%u rows, %u columns", config->tiled_display_config.rows,
          config->tiled_display_config.columns);
    }

    gst_bin_add (GST_BIN (pipeline->pipeline), pipeline->instance_bins[0].bin);
    last_elem = pipeline->instance_bins[0].bin;

    if (!create_tiled_display_bin (&config->tiled_display_config,
            &pipeline->tiled_display_bin)) {
      goto done;
    }
    gst_bin_add (GST_BIN (pipeline->pipeline), pipeline->tiled_display_bin.bin);
    NVGSTDS_LINK_ELEMENT (pipeline->tiled_display_bin.bin, last_elem);
    last_elem = pipeline->tiled_display_bin.bin;

    link_element_to_tee_src_pad (pipeline->tiler_tee,
        pipeline->tiled_display_bin.bin);
    last_elem = pipeline->tiler_tee;

    NVGSTDS_ELEM_ADD_PROBE (latency_probe_id,
        pipeline->instance_bins->sink_bin.sub_bins[0].sink, "sink",
        latency_measurement_buf_prob, GST_PAD_PROBE_TYPE_BUFFER, appCtx);
    latency_probe_id = latency_probe_id;
  } else {
    /*
     * Create demuxer only if tiled display is disabled.
     */
    pipeline->demuxer =
        gst_element_factory_make (NVDS_ELEM_STREAM_DEMUX, "demuxer");
    if (!pipeline->demuxer) {
      NVGSTDS_ERR_MSG_V ("Failed to create element 'demuxer'");
      goto done;
    }
    gst_bin_add (GST_BIN (pipeline->pipeline), pipeline->demuxer);

    for (i = 0; i < config->num_source_sub_bins; i++) {
      gchar pad_name[16];
      GstPad *demux_src_pad;

      /* Check if any sink has been configured to render/encode output for
       * source index `i`. The processing instance for that source will be
       * created only if atleast one sink has been configured as such.
       */
      if (!is_sink_available_for_source_id (config, i))
        continue;

      if (!create_processing_instance (appCtx, i)) {
        goto done;
      }
      gst_bin_add (GST_BIN (pipeline->pipeline),
          pipeline->instance_bins[i].bin);

      g_snprintf (pad_name, 16, "src_%02d", i);
      demux_src_pad = gst_element_request_pad_simple (pipeline->demuxer, pad_name);
      NVGSTDS_LINK_ELEMENT_FULL (pipeline->demuxer, pad_name,
          pipeline->instance_bins[i].bin, "sink");
      gst_object_unref (demux_src_pad);

      for (int k = 0; k < MAX_SINK_BINS; k++) {
        if (pipeline->instance_bins[i].sink_bin.sub_bins[k].sink) {
          NVGSTDS_ELEM_ADD_PROBE (latency_probe_id,
              pipeline->instance_bins[i].sink_bin.sub_bins[k].sink, "sink",
              latency_measurement_buf_prob, GST_PAD_PROBE_TYPE_BUFFER, appCtx);
          break;
        }
      }

      latency_probe_id = latency_probe_id;
    }
    last_elem = pipeline->demuxer;
  }

  if (config->tiled_display_config.enable == NV_DS_TILED_DISPLAY_DISABLE) {
    fps_pad = gst_element_get_static_pad (pipeline->demuxer, "sink");
  } else {
    fps_pad =
        gst_element_get_static_pad (pipeline->tiled_display_bin.bin, "sink");
  }

  pipeline->common_elements.appCtx = appCtx;

  // create and add common components to pipeline.
  if (!create_common_elements (config, pipeline, &tmp_elem1, &tmp_elem2,
          bbox_generated_post_analytics_cb)) {
    goto done;
  }

  if (!add_and_link_broker_sink (appCtx)) {
    goto done;
  }

  if (tmp_elem2) {
    NVGSTDS_LINK_ELEMENT (tmp_elem2, last_elem);
    last_elem = tmp_elem1;
  }

  NVGSTDS_LINK_ELEMENT (pipeline->multi_src_bin.bin, last_elem);

  // enable performance measurement and add call back function to receive
  // performance data.
  if (config->enable_perf_measurement) {
    appCtx->perf_struct.context = appCtx;
    enable_perf_measurement (&appCtx->perf_struct, fps_pad,
        pipeline->multi_src_bin.num_bins,
        config->perf_measurement_interval_sec,
        config->multi_source_config[0].dewarper_config.num_surfaces_per_frame,
        perf_cb);
  }

  latency_probe_id = latency_probe_id;

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (appCtx->pipeline.pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "ds-app-null");

  g_mutex_init (&appCtx->app_lock);
  g_cond_init (&appCtx->app_cond);
  g_mutex_init (&appCtx->latency_lock);

  ret = TRUE;
done:
  if (fps_pad)
    gst_object_unref (fps_pad);

  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

/**
 * Function to destroy pipeline and release the resources, probes etc.
 */
void
destroy_pipeline (AppCtx * appCtx)
{
  gint64 end_time;
  PrototypeConfig *config = &appCtx->config;
  guint i;
  GstBus *bus = NULL;

  end_time = g_get_monotonic_time () + G_TIME_SPAN_SECOND;

  if (!appCtx)
    return;

  if (appCtx->pipeline.demuxer) {
    GstPad *gstpad =
        gst_element_get_static_pad (appCtx->pipeline.demuxer, "sink");
    gst_pad_send_event (gstpad, gst_event_new_eos ());
    gst_object_unref (gstpad);
  } else if (appCtx->pipeline.multi_src_bin.streammux) {
    gchar pad_name[16];
    for (i = 0; i < config->num_source_sub_bins; i++) {
      GstPad *gstpad = NULL;
      g_snprintf (pad_name, 16, "sink_%d", i);
      gstpad =
          gst_element_get_static_pad (appCtx->pipeline.multi_src_bin.streammux,
          pad_name);
      if(gstpad) {
        /**
         * nvmultiurisrcbin을 사용하는 경우 gstpad는 NULL일 것입니다.
         * 파이프라인 해체 시 pad에 대한 EOS는
         * nvmultiurisrcbin 내에서 자동으로 처리됩니다.
         */
        gst_pad_send_event (gstpad, gst_event_new_eos ());
        gst_object_unref (gstpad);
      }
    }
  } else if (appCtx->pipeline.instance_bins[0].sink_bin.bin) {
    GstPad *gstpad =
        gst_element_get_static_pad (appCtx->pipeline.instance_bins[0].sink_bin.
        bin, "sink");
    gst_pad_send_event (gstpad, gst_event_new_eos ());
    gst_object_unref (gstpad);
  }

  g_usleep (100000);

  g_mutex_lock (&appCtx->app_lock);
  if (appCtx->pipeline.pipeline) {
    destroy_smart_record_bin (&appCtx->pipeline.multi_src_bin);
    bus = gst_pipeline_get_bus (GST_PIPELINE (appCtx->pipeline.pipeline));

    while (TRUE) {
      GstMessage *message = gst_bus_pop (bus);
      if (message == NULL)
        break;
      else if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR)
        bus_callback (bus, message, appCtx);
      else
        gst_message_unref (message);
    }
    gst_object_unref (bus);
    gst_element_set_state (appCtx->pipeline.pipeline, GST_STATE_NULL);
  }
  g_cond_wait_until (&appCtx->app_cond, &appCtx->app_lock, end_time);
  g_mutex_unlock (&appCtx->app_lock);

  for (i = 0; i < appCtx->config.num_source_sub_bins; i++) {
    PrototypeInstanceBin *instance_bin = &appCtx->pipeline.instance_bins[i];
    if (config->osd_config.enable) {
      NVGSTDS_ELEM_REMOVE_PROBE (instance_bin->instance_bin_probe_id,
          instance_bin->osd_bin.nvosd, "sink");
    }
  }

  for (i = 0; i < appCtx->config.num_source_sub_bins; i++) {
    PrototypeDemuxInstanceBin *demux_instance_bin = &appCtx->pipeline.demux_instance_bins[i];
    if (config->osd_config.enable) {
      NVGSTDS_ELEM_REMOVE_PROBE (demux_instance_bin->demux_instance_bin_probe_id,
          demux_instance_bin->osd_bin.nvosd, "sink");
    }
  }

  if (config->primary_gie_config.enable) {
    PrototypeCommonElements *common_elements = &appCtx->pipeline.common_elements;
    NVGSTDS_ELEM_REMOVE_PROBE (common_elements->primary_bbox_buffer_probe_id,
        common_elements->primary_gie_bin.bin, "src");
  }
  if (appCtx->latency_info == NULL) {
    free (appCtx->latency_info);
    appCtx->latency_info = NULL;
  }

  destroy_sink_bin ();
  g_mutex_clear (&appCtx->latency_lock);

  if (appCtx->pipeline.pipeline) {
    bus = gst_pipeline_get_bus (GST_PIPELINE (appCtx->pipeline.pipeline));
    gst_bus_remove_watch (bus);
    gst_object_unref (bus);
    gst_object_unref (appCtx->pipeline.pipeline);
    appCtx->pipeline.pipeline = NULL;
    pause_perf_measurement (&appCtx->perf_struct);

    //for pipeline-recreate, reset rtsp srouce's depay, such as rtph264depay.
    NvDsSrcParentBin *pbin = &appCtx->pipeline.multi_src_bin;
    if(pbin){
        NvDsSrcBin *src_bin;
        for (i = 0; i < MAX_SOURCE_BINS; i++) {
          src_bin = &pbin->sub_bins[i];
          if (src_bin && src_bin->config
              && src_bin->config->type == NV_DS_SOURCE_RTSP){
              src_bin->depay = NULL;
          }
        }
    }
  }
}

gboolean
pause_pipeline (AppCtx * appCtx)
{
  GstState cur;
  GstState pending;
  GstStateChangeReturn ret;
  GstClockTime timeout = 5 * GST_SECOND / 1000;

  ret =
      gst_element_get_state (appCtx->pipeline.pipeline, &cur, &pending,
      timeout);

  if (ret == GST_STATE_CHANGE_ASYNC) {
    return FALSE;
  }

  if (cur == GST_STATE_PAUSED) {
    return TRUE;
  } else if (cur == GST_STATE_PLAYING) {
    gst_element_set_state (appCtx->pipeline.pipeline, GST_STATE_PAUSED);
    gst_element_get_state (appCtx->pipeline.pipeline, &cur, &pending,
        GST_CLOCK_TIME_NONE);
    pause_perf_measurement (&appCtx->perf_struct);
    return TRUE;
  } else {
    return FALSE;
  }
}

gboolean
resume_pipeline (AppCtx * appCtx)
{
  GstState cur;
  GstState pending;
  GstStateChangeReturn ret;
  GstClockTime timeout = 5 * GST_SECOND / 1000;

  ret =
      gst_element_get_state (appCtx->pipeline.pipeline, &cur, &pending,
      timeout);

  if (ret == GST_STATE_CHANGE_ASYNC) {
    return FALSE;
  }

  if (cur == GST_STATE_PLAYING) {
    return TRUE;
  } else if (cur == GST_STATE_PAUSED) {
    gst_element_set_state (appCtx->pipeline.pipeline, GST_STATE_PLAYING);
    gst_element_get_state (appCtx->pipeline.pipeline, &cur, &pending,
        GST_CLOCK_TIME_NONE);
    resume_perf_measurement (&appCtx->perf_struct);
    return TRUE;
  } else {
    return FALSE;
  }
}
