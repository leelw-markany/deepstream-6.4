/*
 * Copyright (c) 2021-2023 , NVIDIA CORPORATION. All rights reserved.
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
#include <glib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <cuda_runtime_api.h>
#include <fstream>
#include <string>
#include <opencv2/opencv.hpp>

#include "gstnvdsmeta.h"
#include "nvdspreprocess_meta.h"
#include "gstnvdsinfer.h"

#ifndef PLATFORM_TEGRA
#include "gst-nvmessage.h"
#endif

#define MAX_DISPLAY_LEN 64

/** Defines the maximum size of an array for storing a text result. */
#define MAX_LABEL_SIZE 128

#define PGIE_CLASS_ID_VEHICLE 0
#define PGIE_CLASS_ID_PERSON 2

/* By default, OSD process-mode is set to GPU_MODE. To change mode, set as:
 * 0: CPU mode
 * 1: GPU mode
 */
#define OSD_PROCESS_MODE 1

/* By default, OSD will not display text. To display text, change this to 1 */
#define OSD_DISPLAY_TEXT 1

/* The muxer output resolution must be set if the input streams will be of
 * different resolution. The muxer will scale all the input frames to this
 * resolution. */
#define MUXER_OUTPUT_WIDTH 1920
#define MUXER_OUTPUT_HEIGHT 1080

/* Muxer batch formation timeout, for e.g. 40 millisec. Should ideally be set
 * based on the fastest source's framerate. */
#define MUXER_BATCH_TIMEOUT_USEC 40000

#define TILED_OUTPUT_WIDTH 1280
#define TILED_OUTPUT_HEIGHT 720

/* NVIDIA Decoder source pad memory feature. This feature signifies that source
 * pads having this capability will push GstBuffers containing cuda buffers. */
#define GST_CAPS_FEATURES_NVMM "memory:NVMM"

gchar pgie_classes_str[4][32] = {"Vehicle", "TwoWheeler", "Person", "RoadSign"};

#define FPS_PRINT_INTERVAL 300


static GstPadProbeReturn get_preprocessed_image_buffer_probe(GstPad *pad, GstPadProbeInfo *info, gpointer u_data)
{
  GstBuffer *buf = (GstBuffer *)info->data;
  guint num_rects = 0;
  NvDsObjectMeta *obj_meta = NULL;
  NvDsMetaList *l_frame = NULL;
  NvDsMetaList *l_obj = NULL;
  NvDsDisplayMeta *display_meta = NULL;
  NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta(buf);

  GstMapInfo inmap = GST_MAP_INFO_INIT;
  if (!gst_buffer_map (buf, &inmap, GST_MAP_READ)) {
    GST_ERROR ("input buffer mapinfo failed");
    return GST_PAD_PROBE_DROP;
  }
  NvBufSurface *ip_surf = (NvBufSurface *) inmap.data;

  const char* user_data = static_cast<const char*>(u_data);
  NvDsMetaList *l_user_meta = NULL;
  NvDsUserMeta *user_meta = NULL;
  for (l_user_meta = batch_meta->batch_user_meta_list; l_user_meta != NULL; l_user_meta = l_user_meta->next) {
    user_meta = (NvDsUserMeta *)(l_user_meta->data);
    if (user_meta->base_meta.meta_type == NVDS_PREPROCESS_BATCH_META) {
      GstNvDsPreProcessBatchMeta *preprocess_batchmeta = (GstNvDsPreProcessBatchMeta *)(user_meta->user_meta_data);
      printf("[%s]: %d\n", user_data, preprocess_batchmeta->roi_vector.size());
      for (auto &roi_meta : preprocess_batchmeta->roi_vector) {
        NvDsFrameMeta * fm = roi_meta.frame_meta;
        NvBufSurfaceParams* converted_buffer = roi_meta.converted_buffer;
        NvBufSurfaceMappedAddr mappedAddr = converted_buffer->mappedAddr;
        printf("test: %d\n", sizeof(mappedAddr.addr) / sizeof(mappedAddr.addr[0]));
        // cv::Mat bgr_frame = cv::Mat (cv::Size(960, 544), CV_8UC3);
        // cv::Mat in_mat =cv::Mat (544, 960, CV_8UC4, mappedAddr.addr[0], 1920);
        // cv::cvtColor (in_mat, bgr_frame, cv::COLOR_RGB2BGR);
        // cv::imwrite("./img1.jpg", in_mat);
      }
    }
  }

  return GST_PAD_PROBE_OK;
}



static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
  GMainLoop *loop = (GMainLoop *)data;
  switch (GST_MESSAGE_TYPE(msg))
  {
  case GST_MESSAGE_EOS:
    g_print("End of stream\n");
    g_main_loop_quit(loop);
    break;
  case GST_MESSAGE_WARNING:
  {
    gchar *debug;
    GError *error;
    gst_message_parse_warning(msg, &error, &debug);
    g_printerr("WARNING from element %s: %s\n",
               GST_OBJECT_NAME(msg->src), error->message);
    g_free(debug);
    g_printerr("Warning: %s\n", error->message);
    g_error_free(error);
    break;
  }
  case GST_MESSAGE_ERROR:
  {
    gchar *debug;
    GError *error;
    gst_message_parse_error(msg, &error, &debug);
    g_printerr("ERROR from element %s: %s\n",
               GST_OBJECT_NAME(msg->src), error->message);
    if (debug)
      g_printerr("Error details: %s\n", debug);
    g_free(debug);
    g_error_free(error);
    g_main_loop_quit(loop);
    break;
  }
#ifndef PLATFORM_TEGRA
  case GST_MESSAGE_ELEMENT:
  {
    if (gst_nvmessage_is_stream_eos(msg))
    {
      guint stream_id;
      if (gst_nvmessage_parse_stream_eos(msg, &stream_id))
      {
        g_print("Got EOS from stream %d\n", stream_id);
      }
    }
    break;
  }
#endif
  default:
    break;
  }
  return TRUE;
}

static void cb_newpad(GstElement *decodebin, GstPad *decoder_src_pad, gpointer data) {
  g_print("In cb_newpad\n");
  GstCaps *caps = gst_pad_get_current_caps(decoder_src_pad);
  const GstStructure *str = gst_caps_get_structure(caps, 0);
  const gchar *name = gst_structure_get_name(str);
  GstElement *source_bin = (GstElement *)data;
  GstCapsFeatures *features = gst_caps_get_features(caps, 0);

  if (!strncmp(name, "video", 5)) {
    if (gst_caps_features_contains(features, GST_CAPS_FEATURES_NVMM)) {
      GstPad *bin_ghost_pad = gst_element_get_static_pad(source_bin, "src");
      if (!gst_ghost_pad_set_target(GST_GHOST_PAD(bin_ghost_pad), decoder_src_pad)) {
        g_printerr("Failed to link decoder src pad to source bin ghost pad\n");
      }
      gst_object_unref(bin_ghost_pad);
    } else {
      g_printerr("Error: Decodebin did not pick nvidia decoder plugin.\n");
    }
  }
}

static void decodebin_child_added(GstChildProxy *child_proxy, GObject *object, gchar *name, gpointer user_data) {
  g_print("Decodebin child added: %s\n", name);
  if (g_strrstr(name, "decodebin") == name) {
    g_signal_connect(G_OBJECT(object), "child-added", G_CALLBACK(decodebin_child_added), user_data);
  }
}

static GstElement * create_source_bin(guint index, gchar *uri) {
  GstElement *bin = NULL, *uri_decode_bin = NULL;
  gchar bin_name[16] = {};

  g_snprintf(bin_name, 15, "source-bin-%02d", index);
  bin = gst_bin_new(bin_name);
  uri_decode_bin = gst_element_factory_make("uridecodebin", "uri-decode-bin");

  if (!bin || !uri_decode_bin) {
    g_printerr("One element in source bin could not be created.\n");
    return NULL;
  }
  g_object_set(G_OBJECT(uri_decode_bin), "uri", uri, NULL);
  g_signal_connect(G_OBJECT(uri_decode_bin), "pad-added", G_CALLBACK(cb_newpad), bin);
  g_signal_connect(G_OBJECT(uri_decode_bin), "child-added", G_CALLBACK(decodebin_child_added), bin);
  gst_bin_add(GST_BIN(bin), uri_decode_bin);
  if (!gst_element_add_pad(bin, gst_ghost_pad_new_no_target("src", GST_PAD_SRC))) {
    g_printerr("Failed to add ghost pad in source bin\n");
    return NULL;
  }

  return bin;
}

int main(int argc, char *argv[]) {
  GMainLoop *loop = NULL;
  GstElement *pipeline = NULL, *streammux = NULL, *sink = NULL, *pgie = NULL, *sgie = NULL,
             *preprocess = NULL, *queue1, *queue2, *queue3, *queue4, *queue5, *queue6, *queue7,
             *nvvidconv = NULL, *nvosd = NULL, *tiler = NULL;
  GstBus *bus = NULL;
  guint bus_watch_id;
  GstPad *src_pad = NULL, *sink_pad = NULL;
  guint i, num_sources = 0;
  guint tiler_rows, tiler_columns;
  gboolean is_nvinfer_server = FALSE;
  gchar *nvdspreprocess_config_file = NULL;
  gchar *infer_config_file = NULL;

  int current_device = -1;
  cudaGetDevice(&current_device);
  struct cudaDeviceProp prop;
  cudaGetDeviceProperties(&prop, current_device);

  num_sources = argc - 1;
  gst_init(&argc, &argv);
  loop = g_main_loop_new(NULL, FALSE);
  pipeline = gst_pipeline_new("preprocess-test-pipeline");
  streammux = gst_element_factory_make("nvstreammux", "stream-muxer");
  if (!pipeline || !streammux) {
    g_printerr("One element could not be created 1. Exiting.\n");
    return -1;
  }
  gst_bin_add(GST_BIN(pipeline), streammux);

  for (i = 0; i < num_sources; i++) {
    GstPad *sinkpad, *srcpad;
    gchar pad_name[16] = {};
    GstElement *source_bin = create_source_bin(i, argv[i + 1]);
    if (!source_bin) {
      g_printerr("Failed to create source bin. Exiting.\n");
      return -1;
    }

    gst_bin_add(GST_BIN(pipeline), source_bin);

    g_snprintf(pad_name, 15, "sink_%u", i);
    sinkpad = gst_element_request_pad_simple(streammux, pad_name);
    if (!sinkpad) {
      g_printerr("Streammux request sink pad failed. Exiting.\n");
      return -1;
    }

    srcpad = gst_element_get_static_pad(source_bin, "src");
    if (!srcpad) {
      g_printerr("Failed to get src pad of source bin. Exiting.\n");
      return -1;
    }

    if (gst_pad_link(srcpad, sinkpad) != GST_PAD_LINK_OK) {
      g_printerr("Failed to link source bin to stream muxer. Exiting.\n");
      return -1;
    }

    gst_object_unref(srcpad);
    gst_object_unref(sinkpad);
  }

  preprocess = gst_element_factory_make("nvdspreprocess", "preprocess-plugin");
  pgie = gst_element_factory_make("nvinfer", "primary-nvinference-engine");
  sgie = gst_element_factory_make("nvinfer", "secondary-nvinference-engine");

  queue1 = gst_element_factory_make("queue", "queue1");
  queue2 = gst_element_factory_make("queue", "queue2");
  queue3 = gst_element_factory_make("queue", "queue3");
  queue4 = gst_element_factory_make("queue", "queue4");
  queue5 = gst_element_factory_make("queue", "queue5");
  queue6 = gst_element_factory_make("queue", "queue6");
  queue7 = gst_element_factory_make("queue", "queue7");

  tiler = gst_element_factory_make("nvmultistreamtiler", "nvtiler");
  nvvidconv = gst_element_factory_make("nvvideoconvert", "nvvideo-converter");
  nvosd = gst_element_factory_make("nvdsosd", "nv-onscreendisplay");
  sink = gst_element_factory_make("nveglglessink", "nvvideo-renderer");

  if (!preprocess || !pgie || !tiler || !nvvidconv || !nvosd || !sink) {
    g_printerr("One element could not be created 2. Exiting.\n");
    return -1;
  }

  g_object_set(G_OBJECT(streammux),
              "batch-size", num_sources, NULL);

  g_object_set(G_OBJECT(streammux),
              "width", MUXER_OUTPUT_WIDTH,
              "height", MUXER_OUTPUT_HEIGHT,
              "batched-push-timeout", MUXER_BATCH_TIMEOUT_USEC,
              NULL);

  g_object_set(G_OBJECT(preprocess),
              "config-file", "config_preprocess_sgie.txt",
              NULL);

  g_object_set(G_OBJECT(pgie),
              "config-file-path", "config_infer.txt",
              NULL);

  g_object_set(G_OBJECT(sgie),
              "input-tensor-meta", TRUE,
              "config-file-path", "config_infer_sgie.txt",
              NULL);
  
  g_object_set(G_OBJECT(nvvidconv),
              "nvbuf-memory-type", 1,
              NULL);

  g_print("num-sources = %d\n", num_sources);

  tiler_rows = (guint)sqrt(num_sources);
  tiler_columns = (guint)ceil(1.0 * num_sources / tiler_rows);
  
  g_object_set(G_OBJECT(tiler),
              "rows", tiler_rows,
              "columns", tiler_columns,
              "width", TILED_OUTPUT_WIDTH,
              "height", TILED_OUTPUT_HEIGHT,
              NULL);

  g_object_set(G_OBJECT(nvosd),
              "process-mode", OSD_PROCESS_MODE,
              "display-text", OSD_DISPLAY_TEXT,
              NULL);

  g_object_set(G_OBJECT(sink),
              "qos", 0,
              NULL);

  bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
  gst_object_unref(bus);

  gst_bin_add_many(GST_BIN(pipeline), queue1, pgie, queue2, preprocess, queue3, sgie, queue4, nvvidconv, queue5, tiler, queue6, nvosd, queue7, sink, NULL);
  if (!gst_element_link_many(streammux, queue1, pgie, queue2, preprocess, queue3, sgie, queue4, nvvidconv, queue5, tiler, queue6, nvosd, queue7, sink, NULL)) {
    g_printerr("Elements could not be linked. Exiting.\n");
    return -1;
  }

  // --------------------------------- probe --------------------------------- //
  GstElement * target_element = nvvidconv;

  sink_pad = gst_element_get_static_pad (target_element, "sink");
  if (!sink_pad) {
    g_print ("Unable to get sink pad\n");
  } else {
    char* user_data = "sink";
    gst_pad_add_probe (sink_pad, GST_PAD_PROBE_TYPE_BUFFER, get_preprocessed_image_buffer_probe, static_cast<gpointer>(user_data), NULL);
  }
  gst_object_unref (sink_pad);

  src_pad = gst_element_get_static_pad(target_element, "src");
  if (!src_pad) {
    g_print("Unable to get src pad\n");
  } else {
    char* user_data = "src";
    gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER, get_preprocessed_image_buffer_probe, static_cast<gpointer>(user_data), NULL);
  }
  gst_object_unref(src_pad);
  // --------------------------------- probe --------------------------------- //

  /* Set the pipeline to "playing" state */
  g_print("Now playing...\n");
  gst_element_set_state(pipeline, GST_STATE_PLAYING);

  /* Wait till pipeline encounters an error or EOS */
  g_print("Running...\n");
  g_main_loop_run(loop);

  /* Out of the main loop, clean up nicely */
  g_print("Returned, stopping playback\n");
  gst_element_set_state(pipeline, GST_STATE_NULL);
  g_print("Deleting pipeline\n");
  gst_object_unref(GST_OBJECT(pipeline));
  g_source_remove(bus_watch_id);
  g_main_loop_unref(loop);
  return 0;
}
