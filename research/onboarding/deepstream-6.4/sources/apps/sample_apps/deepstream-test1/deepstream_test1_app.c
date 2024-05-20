/*
 * SPDX-FileCopyrightText: Copyright (c) 2018-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>
#include <cuda_runtime_api.h>
#include "gstnvdsmeta.h"
#include "nvds_yml_parser.h"

#define MAX_DISPLAY_LEN 64

#define PGIE_CLASS_ID_VEHICLE 0
#define PGIE_CLASS_ID_PERSON 2

/* The muxer output resolution must be set if the input streams will be of
 * different resolution. The muxer will scale all the input frames to this
 * resolution. */
/* 입력 스트림이 다른 해상도를 갖게 될 경우 먹서의 출력 해상도를 설정해야 합니다
 * . 먹서는 모든 입력 프레임을 이 해상도로 조절할 것입니다. */
#define MUXER_OUTPUT_WIDTH 1920
#define MUXER_OUTPUT_HEIGHT 1080

/* Muxer batch formation timeout, for e.g. 40 millisec. Should ideally be set
 * based on the fastest source's framerate. */
/* 먹서 배치 형성 타임아웃, 예를 들면 40밀리초. 이상적으로는 가장 빠른 소스의 프레
 * 임 속도를 기반으로 설정해야 합니다. */
#define MUXER_BATCH_TIMEOUT_USEC 40000

/* Check for parsing error. */
/* 파싱 오류 확인 */
#define RETURN_ON_PARSER_ERROR(parse_expr) \
  if (NVDS_YAML_PARSER_SUCCESS != parse_expr) { \
    g_printerr("Error in parsing configuration file.\n"); \
    return -1; \
  }

gint frame_number = 0;
gchar pgie_classes_str[4][32] = { "Vehicle", "TwoWheeler", "Person",
  "Roadsign"
};

/* osd_sink_pad_buffer_probe  will extract metadata received on OSD sink pad
 * and update params for drawing rectangle, object information etc. */
/* osd_sink_pad_buffer_probe는 OSD 싱크 패드에서 수신한 메타데이터를 추출하여 사
 * 각형, 객체 정보 등을 그리기 위한 매개변수를 업데이트합니다. */
static GstPadProbeReturn
osd_sink_pad_buffer_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer u_data)
{
    GstBuffer *buf = (GstBuffer *) info->data;
    guint num_rects = 0;
    NvDsObjectMeta *obj_meta = NULL;
    guint vehicle_count = 0;
    guint person_count = 0;
    NvDsMetaList * l_frame = NULL;
    NvDsMetaList * l_obj = NULL;
    NvDsDisplayMeta *display_meta = NULL;

    NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta (buf);

    for (l_frame = batch_meta->frame_meta_list; l_frame != NULL;
      l_frame = l_frame->next) {
        NvDsFrameMeta *frame_meta = (NvDsFrameMeta *) (l_frame->data);
        int offset = 0;
        for (l_obj = frame_meta->obj_meta_list; l_obj != NULL;
                l_obj = l_obj->next) {
            obj_meta = (NvDsObjectMeta *) (l_obj->data);
            if (obj_meta->class_id == PGIE_CLASS_ID_VEHICLE) {
                vehicle_count++;
                num_rects++;
            }
            if (obj_meta->class_id == PGIE_CLASS_ID_PERSON) {
                person_count++;
                num_rects++;
            }
        }
        display_meta = nvds_acquire_display_meta_from_pool(batch_meta);
        NvOSD_TextParams *txt_params  = &display_meta->text_params[0];
        display_meta->num_labels = 1;
        txt_params->display_text = g_malloc0 (MAX_DISPLAY_LEN);
        offset = snprintf(txt_params->display_text, MAX_DISPLAY_LEN, "Person = %d ", person_count);
        offset = snprintf(txt_params->display_text + offset , MAX_DISPLAY_LEN, "Vehicle = %d ", vehicle_count);

        /* Now set the offsets where the string should appear */
        txt_params->x_offset = 10;
        txt_params->y_offset = 12;

        /* Font , font-color and font-size */
        txt_params->font_params.font_name = "Serif";
        txt_params->font_params.font_size = 10;
        txt_params->font_params.font_color.red = 1.0;
        txt_params->font_params.font_color.green = 1.0;
        txt_params->font_params.font_color.blue = 1.0;
        txt_params->font_params.font_color.alpha = 1.0;

        /* Text background color */
        txt_params->set_bg_clr = 1;
        txt_params->text_bg_clr.red = 0.0;
        txt_params->text_bg_clr.green = 0.0;
        txt_params->text_bg_clr.blue = 0.0;
        txt_params->text_bg_clr.alpha = 1.0;

        nvds_add_display_meta_to_frame(frame_meta, display_meta);
    }

    g_print ("Frame Number = %d Number of objects = %d "
            "Vehicle Count = %d Person Count = %d\n",
            frame_number, num_rects, vehicle_count, person_count);
    frame_number++;
    return GST_PAD_PROBE_OK;
}

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_ERROR:{
      gchar *debug;
      GError *error;
      gst_message_parse_error (msg, &error, &debug);
      g_printerr ("ERROR from element %s: %s\n",
          GST_OBJECT_NAME (msg->src), error->message);
      if (debug)
        g_printerr ("Error details: %s\n", debug);
      g_free (debug);
      g_error_free (error);
      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

int
main (int argc, char *argv[])
{
  GMainLoop *loop = NULL;
  GstElement *pipeline = NULL, *source = NULL, *h264parser = NULL,
      *decoder = NULL, *streammux = NULL, *sink = NULL, *pgie = NULL, *nvvidconv = NULL,
      *nvosd = NULL;

  GstBus *bus = NULL;
  guint bus_watch_id;
  GstPad *osd_sink_pad = NULL;
  gboolean yaml_config = FALSE;
  NvDsGieType pgie_type = NVDS_GIE_PLUGIN_INFER;

  int current_device = -1;
  cudaGetDevice(&current_device);
  printf(">>> current-device: %d\n", current_device);
  struct cudaDeviceProp prop;
  cudaGetDeviceProperties(&prop, current_device);
  /* Check input arguments */
  /* 입력 인수 확인 */
  if (argc != 2) {
    g_printerr ("Usage: %s <yml file>\n", argv[0]);
    g_printerr ("OR: %s <H264 filename>\n", argv[0]);
    return -1;
  }

  /* Standard GStreamer initialization */
  /* 표준 GStreamer 초기화 */
  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  /* Parse inference plugin type */
  /* 추론 플러그인 유형 파싱 */
  yaml_config = (g_str_has_suffix (argv[1], ".yml") ||
          g_str_has_suffix (argv[1], ".yaml"));

  if (yaml_config) {
    RETURN_ON_PARSER_ERROR(nvds_parse_gie_type(&pgie_type, argv[1],
                "primary-gie"));
  }

  /* Create gstreamer elements */
  /* GStreamer 요소 생성 */
  /* Create Pipeline element that will form a connection of other elements */
  /* 다른 요소들 간의 연결을 형성할 파이프라인 요소 생성 */
  pipeline = gst_pipeline_new ("dstest1-pipeline");

  /* Source element for reading from the file */
  /* 파일에서 읽기 위한 소스 요소 */
  source = gst_element_factory_make ("filesrc", "file-source");

  /* Since the data format in the input file is elementary h264 stream,
   * we need a h264parser */
  /* 입력 파일의 데이터 형식이 elementary h264 스트림이므로 h264 파서가 필
   * 요합니다. */
  h264parser = gst_element_factory_make ("h264parse", "h264-parser");

  /* Use nvdec_h264 for hardware accelerated decode on GPU */
  /* GPU에서 하드웨어 가속 디코딩을 위해 nvdec_h264 사용 */
  decoder = gst_element_factory_make ("nvv4l2decoder", "nvv4l2-decoder");

  /* Create nvstreammux instance to form batches from one or more sources. */
  /* 하나 이상의 소스에서 배치를 형성하기 위한 nvstreammux 인스턴스 생성 */
  streammux = gst_element_factory_make ("nvstreammux", "stream-muxer");

  if (!pipeline || !streammux) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }

  /* Use nvinfer or nvinferserver to run inferencing on decoder's output,
   * behaviour of inferencing is set through config file */
  /* nvinfer 또는 nvinferserver를 사용하여 디코더 출력에서 추론을 실행하고,
   * 추론의 동작은 구성 파일을 통해 설정됩니다. */
  if (pgie_type == NVDS_GIE_PLUGIN_INFER_SERVER) {
    pgie = gst_element_factory_make ("nvinferserver", "primary-nvinference-engine");
  } else {
    pgie = gst_element_factory_make ("nvinfer", "primary-nvinference-engine");
  }

  /* Use convertor to convert from NV12 to RGBA as required by nvosd */
  /* nvosd에서 필요한대로 NV12에서 RGBA로 변환하기 위해 변환기 사용 */
  nvvidconv = gst_element_factory_make ("nvvideoconvert", "nvvideo-converter");

  /* Create OSD to draw on the converted RGBA buffer */
  /* 변환된 RGBA 버퍼에 그리기 위한 OSD 생성 */
  nvosd = gst_element_factory_make ("nvdsosd", "nv-onscreendisplay");

  /* Finally render the osd output */
  /* 마지막으로 OSD 출력 렌더링 */
  if(prop.integrated) {
    sink = gst_element_factory_make("nv3dsink", "nv3d-sink");
  } else {
    sink = gst_element_factory_make ("nveglglessink", "nvvideo-renderer");
  }

  if (!source || !h264parser || !decoder || !pgie
      || !nvvidconv || !nvosd || !sink) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }

  /* we set the input filename to the source element */
  /* 입력 파일 이름을 소스 요소로 설정합니다. */
  g_object_set (G_OBJECT (source), "location", argv[1], NULL);

  if (g_str_has_suffix (argv[1], ".h264")) {
    g_object_set (G_OBJECT (source), "location", argv[1], NULL);

    g_object_set (G_OBJECT (streammux), "batch-size", 1, NULL);

    g_object_set (G_OBJECT (streammux), "width", MUXER_OUTPUT_WIDTH, "height",
        MUXER_OUTPUT_HEIGHT,
        "batched-push-timeout", MUXER_BATCH_TIMEOUT_USEC, NULL);

    /* Set all the necessary properties of the nvinfer element,
     * the necessary ones are : */
    /* nvinfer 요소의 모든 필요한 속성을 설정하십시오. 필요한 속성
     * 은 다음과 같습니다: */
    g_object_set (G_OBJECT (pgie),
        "config-file-path", "dstest1_pgie_config.txt", NULL);
  }

  if (yaml_config) {
    RETURN_ON_PARSER_ERROR(nvds_parse_file_source(source, argv[1],"source"));
    RETURN_ON_PARSER_ERROR(nvds_parse_streammux(streammux, argv[1],"streammux"));

    /* Set all the necessary properties of the inference element */
    /* 추론 요소의 모든 필요한 속성을 설정하십시오. */
    RETURN_ON_PARSER_ERROR(nvds_parse_gie(pgie, argv[1], "primary-gie"));
  }

  /* we add a message handler */
  /* 메시지 핸들러를 추가합니다. */
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  /* Set up the pipeline */
  /* 파이프라인 설정 */
  /* we add all elements into the pipeline */
  /* 모든 요소를 파이프라인에 추가합니다. */
  // gst_bin_add_many (GST_BIN (pipeline),
  //     source, h264parser, decoder, streammux, pgie,
  //     nvvidconv, nvosd, sink, NULL);
  gst_bin_add (GST_BIN (pipeline), source);
  gst_bin_add (GST_BIN (pipeline), h264parser);
  gst_bin_add (GST_BIN (pipeline), decoder);
  gst_bin_add (GST_BIN (pipeline), streammux);
  gst_bin_add (GST_BIN (pipeline), pgie);
  gst_bin_add (GST_BIN (pipeline), nvvidconv);
  gst_bin_add (GST_BIN (pipeline), nvosd);
  gst_bin_add (GST_BIN (pipeline), sink);
  g_print ("Added elements to bin\n");

  GstPad *sinkpad, *srcpad;
  gchar pad_name_sink[16] = "sink_0";
  gchar pad_name_src[16] = "src";

  sinkpad = gst_element_request_pad_simple (streammux, pad_name_sink);
  if (!sinkpad) {
    g_printerr ("Streammux request sink pad failed. Exiting.\n");
    return -1;
  }

  srcpad = gst_element_get_static_pad (decoder, pad_name_src);
  if (!srcpad) {
    g_printerr ("Decoder request src pad failed. Exiting.\n");
    return -1;
  }

  if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK) {
      g_printerr ("Failed to link decoder to stream muxer. Exiting.\n");
      return -1;
  }

  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  /* we link the elements together */
  /* 요소들을 서로 연결합니다. */
  /* file-source -> h264-parser -> nvh264-decoder ->
   * pgie -> nvvidconv -> nvosd -> video-renderer */

  if (!gst_element_link_many (source, h264parser, decoder, NULL)) {
    g_printerr ("Elements could not be linked: 1. Exiting.\n");
    return -1;
  }

  if (!gst_element_link_many (streammux, pgie,
        nvvidconv, nvosd, sink, NULL)) {
      g_printerr ("Elements could not be linked: 2. Exiting.\n");
      return -1;
  }

  /* Lets add probe to get informed of the meta data generated, we add probe to
   * the sink pad of the osd element, since by that time, the buffer would have
   * had got all the metadata. */
  /* 생성된 메타데이터에 대한 정보를 얻기 위해 프로브를 추가하겠습니다. osd 요소의 싱
   * 크 패드에 프로브를 추가하므로 그때까지 버퍼는 모든 메타데이터를 갖게 될 것입니다
   * . */
  osd_sink_pad = gst_element_get_static_pad (nvosd, "sink");
  if (!osd_sink_pad)
    g_print ("Unable to get sink pad\n");
  else
    gst_pad_add_probe (osd_sink_pad, GST_PAD_PROBE_TYPE_BUFFER,
        osd_sink_pad_buffer_probe, NULL, NULL);
  gst_object_unref (osd_sink_pad);

  /* Set the pipeline to "playing" state */
  /* 파이프라인을 "재생 중" 상태로 설정합니다. */
  g_print ("Using file: %s\n", argv[1]);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* Wait till pipeline encounters an error or EOS */
  /* 파이프라인이 오류 또는 EOS(End of Stream)를 만날 때까지 대기합니다. */
  g_print ("Running...\n");
  g_main_loop_run (loop);

  /* Out of the main loop, clean up nicely */
  /* 메인 루프를 벗어나면 깔끔하게 정리합니다. */
  g_print ("Returned, stopping playback\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);
  return 0;
}
