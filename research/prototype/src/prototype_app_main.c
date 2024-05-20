/*
 * Copyright (c) 2019-2022, NVIDIA CORPORATION. All rights reserved.
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

#include "prototype_app.h"

#include <sys/inotify.h>

#include <unistd.h>
#include <termios.h>

//#include <X11/Xlib.h>
#include <X11/Xutil.h>


/** @{
 * imported from deepstream-app as is
 */
/** deepstream-app에서 그대로 가져옴
 */
#define MAX_INSTANCES 128
#define APP_TITLE "PrototypeApp"

#define DEFAULT_X_WINDOW_WIDTH 1920
#define DEFAULT_X_WINDOW_HEIGHT 1080

static guint cintr = FALSE;
static GMainLoop *main_loop = NULL;
static gchar **cfg_files = NULL;
static gboolean print_version = FALSE;
static gboolean show_bbox_text = FALSE;
static gboolean print_dependencies_version = FALSE;
static gboolean quit = FALSE;
static gboolean force_tcp = TRUE;           /**/
static gint return_value = 0;
static guint num_instances;
static GMutex fps_lock;
static gdouble fps[MAX_SOURCE_BINS];
static gdouble fps_avg[MAX_SOURCE_BINS];

static Display *display = NULL;
static Window windows[MAX_INSTANCES] = { 0 };

static GThread *x_event_thread = NULL;
static GMutex disp_lock;

static guint rrow, rcol, rcfg;
static gboolean rrowsel = FALSE, selecting = FALSE;


////////////////////////////////////////////////////////////////
AppCtx *appCtx[MAX_INSTANCES];
TestAppCtx *testAppCtx;
gboolean playback_utc = FALSE;
AppConfigAnalyticsModel model_used = APP_CONFIG_ANALYTICS_MODELS_UNKNOWN;


/** @}
 * imported from deepstream-app as is
 */
/** deepstream-app에서 그대로 가져옴
 */
GST_DEBUG_CATEGORY (NVDS_APP);

////////////////////////////////////////////////////////////////
GOptionEntry entries[] = {
  {"version", 'v', 0, G_OPTION_ARG_NONE, &print_version,
      "Print DeepStreamSDK version", NULL}
  ,
  {"tiledtext", 't', 0, G_OPTION_ARG_NONE, &show_bbox_text,
      "Display Bounding box labels in tiled mode", NULL}
  ,
  {"version-all", 0, 0, G_OPTION_ARG_NONE, &print_dependencies_version,
      "Print DeepStreamSDK and dependencies version", NULL}
  ,
  {"cfg-file", 'c', 0, G_OPTION_ARG_FILENAME_ARRAY, &cfg_files,
      "Set the config file", NULL}
  ,
  {"playback-utc", 'p', 0, G_OPTION_ARG_INT, &playback_utc,
        "Playback utc; default=false (base UTC from file-URL or RTCP Sender Report) =true (base UTC from file/rtsp URL)",
      NULL}
  ,
  {"pgie-model-used", 'm', 0, G_OPTION_ARG_INT, &model_used,
        "PGIE Model used; {0 - Unknown [DEFAULT]}, {1: Resnet 4-class [Car, Bicycle, Person, Roadsign]}",
      NULL}
  ,
  {"no-force-tcp", 0, G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &force_tcp,
      "Do not force TCP for RTP transport", NULL}
  ,
  {NULL}
  ,
};

////////////////////////////////////////////////////////////////
static gpointer
meta_copy_func (gpointer data, gpointer user_data)
{
  NvDsUserMeta *user_meta = (NvDsUserMeta *) data;
  NvDsEventMsgMeta *srcMeta = (NvDsEventMsgMeta *) user_meta->user_meta_data;
  NvDsEventMsgMeta *dstMeta = NULL;

  dstMeta = (NvDsEventMsgMeta *) g_memdup2 (srcMeta, sizeof (NvDsEventMsgMeta));

  if (srcMeta->ts)
    dstMeta->ts = g_strdup (srcMeta->ts);

  if (srcMeta->objSignature.size > 0) {
    dstMeta->objSignature.signature = (gdouble *) g_memdup2 (srcMeta->objSignature.signature,
        srcMeta->objSignature.size);
    dstMeta->objSignature.size = srcMeta->objSignature.size;
  }

  if (srcMeta->objectId) {
    dstMeta->objectId = g_strdup (srcMeta->objectId);
  }

  if (srcMeta->sensorStr) {
    dstMeta->sensorStr = g_strdup (srcMeta->sensorStr);
  }

  if (srcMeta->extMsgSize > 0) {
    if (srcMeta->objType == NVDS_OBJECT_TYPE_VEHICLE) {
      NvDsVehicleObject *srcObj = (NvDsVehicleObject *) srcMeta->extMsg;
      NvDsVehicleObject *obj =
          (NvDsVehicleObject *) g_malloc0 (sizeof (NvDsVehicleObject));
      if (srcObj->type)
        obj->type = g_strdup (srcObj->type);
      if (srcObj->make)
        obj->make = g_strdup (srcObj->make);
      if (srcObj->model)
        obj->model = g_strdup (srcObj->model);
      if (srcObj->color)
        obj->color = g_strdup (srcObj->color);
      if (srcObj->license)
        obj->license = g_strdup (srcObj->license);
      if (srcObj->region)
        obj->region = g_strdup (srcObj->region);

      dstMeta->extMsg = obj;
      dstMeta->extMsgSize = sizeof (NvDsVehicleObject);
    } else if (srcMeta->objType == NVDS_OBJECT_TYPE_PERSON) {
      NvDsPersonObject *srcObj = (NvDsPersonObject *) srcMeta->extMsg;
      NvDsPersonObject *obj =
          (NvDsPersonObject *) g_malloc0 (sizeof (NvDsPersonObject));

      obj->age = srcObj->age;

      if (srcObj->gender)
        obj->gender = g_strdup (srcObj->gender);
      if (srcObj->cap)
        obj->cap = g_strdup (srcObj->cap);
      if (srcObj->hair)
        obj->hair = g_strdup (srcObj->hair);
      if (srcObj->apparel)
        obj->apparel = g_strdup (srcObj->apparel);

      dstMeta->extMsg = obj;
      dstMeta->extMsgSize = sizeof (NvDsPersonObject);
    }
  }

  return dstMeta;
}

////////////////////////////////////////////////////////////////
static void
meta_free_func (gpointer data, gpointer user_data)
{
  NvDsUserMeta *user_meta = (NvDsUserMeta *) data;
  NvDsEventMsgMeta *srcMeta = (NvDsEventMsgMeta *) user_meta->user_meta_data;
  user_meta->user_meta_data = NULL;

  if (srcMeta->ts) {
    g_free (srcMeta->ts);
  }

  if (srcMeta->objSignature.size > 0) {
    g_free (srcMeta->objSignature.signature);
    srcMeta->objSignature.size = 0;
  }

  if (srcMeta->objectId) {
    g_free (srcMeta->objectId);
  }

  if (srcMeta->sensorStr) {
    g_free (srcMeta->sensorStr);
  }

  if (srcMeta->extMsgSize > 0) {
    if (srcMeta->objType == NVDS_OBJECT_TYPE_VEHICLE) {
      NvDsVehicleObject *obj = (NvDsVehicleObject *) srcMeta->extMsg;
      if (obj->type)
        g_free (obj->type);
      if (obj->color)
        g_free (obj->color);
      if (obj->make)
        g_free (obj->make);
      if (obj->model)
        g_free (obj->model);
      if (obj->license)
        g_free (obj->license);
      if (obj->region)
        g_free (obj->region);
    } else if (srcMeta->objType == NVDS_OBJECT_TYPE_PERSON) {
      NvDsPersonObject *obj = (NvDsPersonObject *) srcMeta->extMsg;

      if (obj->gender)
        g_free (obj->gender);
      if (obj->cap)
        g_free (obj->cap);
      if (obj->hair)
        g_free (obj->hair);
      if (obj->apparel)
        g_free (obj->apparel);
    }
    g_free (srcMeta->extMsg);
    srcMeta->extMsg = NULL;
    srcMeta->extMsgSize = 0;
  }
  g_free (srcMeta);
}

////////////////////////////////////////////////////////////////
/**
 * Callback function to be called once all inferences (Primary + Secondary)
 * are done. This is opportunity to modify content of the metadata.
 * e.g. Here Person is being replaced with Man/Woman and corresponding counts
 * are being maintained. It should be modified according to network classes
 * or can be removed altogether if not required.
 */
/**
 * 모든 추론 (Primary + Secondary)이 완료된 후 호출되는 콜백 함수입니다.
 * 이는 메타데이터 내용을 수정할 수 있는 기회입니다.
 * 예를 들어 여기서는 Person을 Man/Woman으로 대체하고 해당 카운트를 유지하고 있습니다.
 * 네트워크 클래스에 따라 수정되어야 하며 필요하지 않은 경우 완전히 제거될 수 있습니다.
 */
static void
bbox_generated_probe_after_analytics (AppCtx * app_ctx, GstBuffer * buf,
    NvDsBatchMeta * batch_meta)
{
  NvDsObjectMeta *obj_meta = NULL;
  GstClockTime buffer_pts = 0;
  guint32 stream_id = 0;

  for (NvDsMetaList * l_frame = batch_meta->frame_meta_list; l_frame != NULL;
      l_frame = l_frame->next) {
    NvDsFrameMeta *frame_meta = (NvDsFrameMeta *) l_frame->data;
    stream_id = frame_meta->source_id;
    GstClockTime buf_ntp_time = 0;
    if (playback_utc == FALSE) {
      /** Calculate the buffer-NTP-time
       * derived from this stream's RTCP Sender Report here:
       */
      /** 이 스트림의 RTCP 송신 보고서에서 파생된 버퍼-NTP 시간을 계산합니다:
       */
      StreamSourceInfo *src_stream = &testAppCtx->streams[stream_id];
      buf_ntp_time = frame_meta->ntp_timestamp;

      if (buf_ntp_time < src_stream->last_ntp_time) {
        NVGSTDS_WARN_MSG_V ("Source %d: NTP timestamps are backward in time."
            " Current: %lu previous: %lu", stream_id, buf_ntp_time,
            src_stream->last_ntp_time);
      }
      src_stream->last_ntp_time = buf_ntp_time;
    }

    GList *l;
    for (l = frame_meta->obj_meta_list; l != NULL; l = l->next) {
      /* Now using above information we need to form a text that should
       * be displayed on top of the bounding box, so lets form it here. */
      /* 이제 위의 정보를 사용하여 바운딩 박스 위에 표시될 텍스트를 형성해야 합니다.
       * 여기서 형성합시다. */
      obj_meta = (NvDsObjectMeta *) (l->data);

      {
        /**
         * Enable only if this callback is after tiler
         * NOTE: Scaling back code-commented
         * now that bbox_generated_probe_after_analytics() is post analytics
         * (say pgie, tracker or sgie)
         * and before tiler, no plugin shall scale metadata and will be
         * corresponding to the nvstreammux resolution
         */
        /**
         * 이 콜백이 tiler 이후에 있는 경우에만 활성화합니다.
         * 참고: 스케일링 백 코드 주석 처리
         * 이제 bbox_generated_probe_after_analytics()가 analytics 이후에 있다고 가정하면
         * (예: pgie, tracker 또는 sgie)
         * 그리고 tiler 이전에, 플러그인은 메타데이터를 스케일링하지 않으며
         * nvstreammux 해상도에 해당할 것입니다.
         */
        float scaleW = 0;
        float scaleH = 0;
        /* 메시지를 보낼 빈도는 사용 사례에 따라 달라집니다.
         * 여기서는 첫 번째 객체마다 30프레임마다 메시지를 보내는 것입니다.
         */
        buffer_pts = frame_meta->buf_pts;
        if (!app_ctx->config.streammux_config.pipeline_width
            || !app_ctx->config.streammux_config.pipeline_height) {
          g_print ("invalid pipeline params\n");
          return;
        }
        // LOGD ("stream %d==%d [%d X %d]\n", frame_meta->source_id,
        //     frame_meta->pad_index, frame_meta->source_frame_width,
        //     frame_meta->source_frame_height);
        scaleW =
            (float) frame_meta->source_frame_width /
            app_ctx->config.streammux_config.pipeline_width;
        scaleH =
            (float) frame_meta->source_frame_height /
            app_ctx->config.streammux_config.pipeline_height;

        if (playback_utc == FALSE) {
          /** 이 스트림의 RTCP 송신 보고서에서 파생된 버퍼-NTP 시간을
           * 여기에서 사용합니다:
           */
          buffer_pts = buf_ntp_time;
        }
        /** Generate NvDsEventMsgMeta for every object */
        NvDsEventMsgMeta *msg_meta =
            (NvDsEventMsgMeta *) g_malloc0 (sizeof (NvDsEventMsgMeta));
        generate_event_msg_meta (app_ctx, msg_meta, obj_meta->class_id, TRUE,
                  /**< useTs NOTE: Pass FALSE for files without base-timestamp in URI */
            buffer_pts,
            app_ctx->config.multi_source_config[stream_id].uri, stream_id,
            app_ctx->config.multi_source_config[stream_id].camera_id,
            obj_meta, scaleW, scaleH, frame_meta);
        testAppCtx->streams[stream_id].meta_number++;
        NvDsUserMeta *user_event_meta =
            nvds_acquire_user_meta_from_pool (batch_meta);
        if (user_event_meta) {
          /*
           * 생성된 이벤트 메타데이터에는 동적으로 할당된 차량/사람과 같은
           * 사용자 지정 객체가 있으므로, 두 구성 요소 간에 메타데이터 복사가
           * 발생할 때 해당 필드를 처리하는 복사 및 해제 함수를 설정합니다.
           */
          user_event_meta->user_meta_data = (void *) msg_meta;
          user_event_meta->base_meta.batch_meta = batch_meta;
          user_event_meta->base_meta.meta_type = NVDS_EVENT_MSG_META;
          user_event_meta->base_meta.copy_func =
              (NvDsMetaCopyFunc) meta_copy_func;
          user_event_meta->base_meta.release_func =
              (NvDsMetaReleaseFunc) meta_free_func;
          nvds_add_user_meta_to_frame (frame_meta, user_event_meta);
        } else {
          g_print ("Error in attaching event meta to buffer\n");
        }
      }
    }
    testAppCtx->streams[stream_id].frameCount++;
  }
}

/**
 * @{ deepstream-app에서 그대로 가져옴
 */
////////////////////////////////////////////////////////////////
/**
 * 프로그램 인터럽트 신호를 처리하는 함수입니다.
 * 인터럽트를 처리한 후에는 기본 핸들러를 설치합니다.
 */
static void
_intr_handler (int signum)
{
  struct sigaction action;

  NVGSTDS_ERR_MSG_V ("User Interrupted.. \n");

  memset (&action, 0, sizeof (action));
  action.sa_handler = SIG_DFL;

  sigaction (SIGINT, &action, NULL);

  cintr = TRUE;
}

////////////////////////////////////////////////////////////////
/**
 * 각 스트림의 성능 숫자를 출력하는 콜백 함수입니다.
 */
static void
perf_cb (gpointer context, NvDsAppPerfStruct * str)
{
  static guint header_print_cnt = 0;
  guint i;
  AppCtx *app_ctx = (AppCtx *) context;
  guint numf = str->num_instances;

  g_mutex_lock (&fps_lock);
  guint active_src_count = 0;

  for (i = 0; i < numf; i++) {
    fps[i] = str->fps[i];
    if (fps[i]){
      active_src_count++;
    }
    fps_avg[i] = str->fps_avg[i];
  }
  g_print("Active sources : %u\n", active_src_count);
  if (header_print_cnt % 20 == 0) {
    g_print ("\n**PERF:  ");
    for (i = 0; i < numf; i++) {
      g_print ("FPS %d (Avg)\t", i);
    }
    g_print ("\n");
    header_print_cnt = 0;
  }
  header_print_cnt++;

  time_t t = time (NULL);
  struct tm *tm = localtime (&t);
  printf ("%s", asctime (tm));
  if (num_instances > 1)
    g_print ("PERF(%d): ", app_ctx->index);
  else
    g_print ("**PERF:  ");

  for (i = 0; i < numf; i++) {
    g_print ("%.2f (%.2f)\t", fps[i], fps_avg[i]);
  }

  g_print ("\n");
  g_mutex_unlock (&fps_lock);
}

////////////////////////////////////////////////////////////////
/**
 * 인터럽트 상태를 확인하는 루프 함수입니다.
 * 애플리케이션이 인터럽트를 받으면 루프를 종료합니다.
 */
static gboolean
check_for_interrupt (gpointer data)
{
  if (quit) {
    return FALSE;
  }

  if (cintr) {
    cintr = FALSE;

    quit = TRUE;
    g_main_loop_quit (main_loop);

    return FALSE;
  }
  return TRUE;
}

////////////////////////////////////////////////////////////////
/*
 * 프로그램 인터럽트 신호에 대한 사용자 지정 핸들러를 설치하는 함수입니다.
 */
static void
_intr_setup (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
  action.sa_handler = _intr_handler;

  sigaction (SIGINT, &action, NULL);
}

////////////////////////////////////////////////////////////////
static gboolean
kbhit (void)
{
  struct timeval tv;
  fd_set rdfs;

  tv.tv_sec = 0;
  tv.tv_usec = 0;

  FD_ZERO (&rdfs);
  FD_SET (STDIN_FILENO, &rdfs);

  select (STDIN_FILENO + 1, &rdfs, NULL, NULL, &tv);
  return FD_ISSET (STDIN_FILENO, &rdfs);
}

////////////////////////////////////////////////////////////////
/*
 * 터미널의 정규 모드를 활성화/비활성화하는 함수입니다.
 * 비정규 모드에서는 입력이 즉시 사용 가능하며 (사용자가 행 구분자 문자를 입력할
 * 필요가 없음),
 * 정규 모드에서는 사용자가 입력한 후에만 사용 가능합니다.
 */
static void
changemode (int dir)
{
  static struct termios oldt, newt;

  if (dir == 1) {
    tcgetattr (STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON);
    tcsetattr (STDIN_FILENO, TCSANOW, &newt);
  } else
    tcsetattr (STDIN_FILENO, TCSANOW, &oldt);
}

////////////////////////////////////////////////////////////////
static void
print_runtime_commands (void)
{
  g_print ("\nRuntime commands:\n"
      "\th: Print this help\n"
      "\tq: Quit\n\n" "\tp: Pause\n" "\tr: Resume\n\n");

  if (appCtx[0]->config.tiled_display_config.enable) {
    g_print
        ("NOTE: To expand a source in the 2D tiled display and view object details,"
        " left-click on the source.\n"
        "      To go back to the tiled display, right-click anywhere on the window.\n\n");
  }
}

////////////////////////////////////////////////////////////////
/**
 * 키보드 입력 및 각 파이프라인의 상태를 확인하는 루프 함수입니다.
 */
static gboolean
event_thread_func (gpointer arg)
{
  guint i;
  gboolean ret = TRUE;

  // 모든 인스턴스가 종료되었는지 확인합니다.
  for (i = 0; i < num_instances; i++) {
    if (!appCtx[i]->quit)
      break;
  }

  if (i == num_instances) {
    quit = TRUE;
    g_main_loop_quit (main_loop);
    return FALSE;
  }
  // 키보드 입력을 확인합니다.
  if (!kbhit ()) {
    //continue;
    return TRUE;
  }
  int c = fgetc (stdin);
  g_print ("\n");

  gint source_id;
  GstElement *tiler = appCtx[rcfg]->pipeline.tiled_display_bin.tiler;

  if (appCtx[rcfg]->config.tiled_display_config.enable)
  {
    g_object_get (G_OBJECT (tiler), "show-source", &source_id, NULL);

    if (selecting) {
      if (rrowsel == FALSE) {
        if (c >= '0' && c <= '9') {
          rrow = c - '0';
          g_print ("--selecting source  row %d--\n", rrow);
          rrowsel = TRUE;
        }
      } else {
        if (c >= '0' && c <= '9') {
          int tile_num_columns = appCtx[rcfg]->config.tiled_display_config.columns;
          rcol = c - '0';
          selecting = FALSE;
          rrowsel = FALSE;
          source_id = tile_num_columns * rrow + rcol;
          g_print ("--selecting source  col %d sou=%d--\n", rcol, source_id);
          if (source_id >= (gint) appCtx[rcfg]->config.num_source_sub_bins) {
            source_id = -1;
          } else {
            appCtx[rcfg]->show_bbox_text = TRUE;
            appCtx[rcfg]->active_source_index = source_id;
            g_object_set (G_OBJECT (tiler), "show-source", source_id, NULL);
          }
        }
      }
    }
  }
  switch (c) {
    case 'h':
      print_runtime_commands ();
      break;
    case 'p':
      for (i = 0; i < num_instances; i++)
        pause_pipeline (appCtx[i]);
      break;
    case 'r':
      for (i = 0; i < num_instances; i++)
        resume_pipeline (appCtx[i]);
      break;
    case 'q':
      quit = TRUE;
      g_main_loop_quit (main_loop);
      ret = FALSE;
      break;
    case 'c':
      if (appCtx[rcfg]->config.tiled_display_config.enable && selecting == FALSE && source_id == -1) {
        g_print("--selecting config file --\n");
        c = fgetc(stdin);
        if (c >= '0' && c <= '9') {
          rcfg = c - '0';
          if (rcfg < num_instances) {
            g_print("--selecting config  %d--\n", rcfg);
          } else {
            g_print("--selected config file %d out of bound, reenter\n", rcfg);
            rcfg = 0;
          }
        }
      }
      break;
    case 'z':
      if (appCtx[rcfg]->config.tiled_display_config.enable && source_id == -1 && selecting == FALSE) {
        g_print ("--selecting source --\n");
        selecting = TRUE;
      } else {
        if (!show_bbox_text) {
          GstElement *nvosd = appCtx[rcfg]->pipeline.instance_bins[0].osd_bin.nvosd;
          g_object_set (G_OBJECT (nvosd), "display-text", FALSE, NULL);
          g_object_set (G_OBJECT (tiler), "show-source", -1, NULL);
        }
        appCtx[rcfg]->active_source_index = -1;
        selecting = FALSE;
        rcfg = 0;
        g_print("--tiled mode --\n");
      }
      break;
    default:
      break;
  }
  return ret;
}

////////////////////////////////////////////////////////////////
static int
get_source_id_from_coordinates (float x_rel, float y_rel, AppCtx *app_ctx)
{
  int tile_num_rows = app_ctx->config.tiled_display_config.rows;
  int tile_num_columns = app_ctx->config.tiled_display_config.columns;

  int source_id = (int) (x_rel * tile_num_columns);
  source_id += ((int) (y_rel * tile_num_rows)) * tile_num_columns;

  /* 비어 있는 타일을 클릭할 수 없습니다. */
  if (source_id >= (gint) app_ctx->config.num_source_sub_bins)
    source_id = -1;

  return source_id;
}

////////////////////////////////////////////////////////////////
/**
 * X 윈도우 이벤트를 모니터링하는 쓰레드입니다.
 */
static gpointer
nvds_x_event_thread (gpointer data)
{
  g_mutex_lock (&disp_lock);
  while (display) {
    XEvent e;
    guint index;
    while (XPending (display)) {
      XNextEvent (display, &e);
      switch (e.type) {
        case ButtonPress:
        {
          XWindowAttributes win_attr;
          XButtonEvent ev = e.xbutton;
          gint source_id;
          GstElement *tiler;

          XGetWindowAttributes (display, ev.window, &win_attr);

          for (index = 0; index < MAX_INSTANCES; index++)
            if (ev.window == windows[index])
              break;

          tiler = appCtx[index]->pipeline.tiled_display_bin.tiler;
          g_object_get (G_OBJECT (tiler), "show-source", &source_id, NULL);

          if (ev.button == Button1 && source_id == -1) {
            source_id =
                get_source_id_from_coordinates (ev.x * 1.0 / win_attr.width,
                ev.y * 1.0 / win_attr.height, appCtx[index]);
            if (source_id > -1) {
              g_object_set (G_OBJECT (tiler), "show-source", source_id, NULL);
              appCtx[index]->active_source_index = source_id;
              appCtx[index]->show_bbox_text = TRUE;
              GstElement *nvosd = appCtx[index]->pipeline.instance_bins[0].osd_bin.nvosd;
              g_object_set (G_OBJECT (nvosd), "display-text", TRUE, NULL);
            }
          } else if (ev.button == Button3) {
            g_object_set (G_OBJECT (tiler), "show-source", -1, NULL);
            appCtx[index]->active_source_index = -1;
            if (!show_bbox_text) {
              appCtx[index]->show_bbox_text = FALSE;
              GstElement *nvosd = appCtx[index]->pipeline.instance_bins[0].osd_bin.nvosd;
              g_object_set (G_OBJECT (nvosd), "display-text", FALSE, NULL);
            }
          }
        }
          break;
        case KeyRelease:
        {
          KeySym p, r, q;
          guint i;
          p = XKeysymToKeycode (display, XK_P);
          r = XKeysymToKeycode (display, XK_R);
          q = XKeysymToKeycode (display, XK_Q);
          if (e.xkey.keycode == p) {
            for (i = 0; i < num_instances; i++)
              pause_pipeline (appCtx[i]);
            break;
          }
          if (e.xkey.keycode == r) {
            for (i = 0; i < num_instances; i++)
              resume_pipeline (appCtx[i]);
            break;
          }
          if (e.xkey.keycode == q) {
            quit = TRUE;
            g_main_loop_quit (main_loop);
          }
        }
          break;
        case ClientMessage:
        {
          Atom wm_delete;
          for (index = 0; index < MAX_INSTANCES; index++)
            if (e.xclient.window == windows[index])
              break;

          wm_delete = XInternAtom (display, "WM_DELETE_WINDOW", 1);
          if (wm_delete != None && wm_delete == (Atom) e.xclient.data.l[0]) {
            quit = TRUE;
            g_main_loop_quit (main_loop);
          }
        }
          break;
      }
    }
    g_mutex_unlock (&disp_lock);
    g_usleep (G_USEC_PER_SEC / 20);
    g_mutex_lock (&disp_lock);
  }
  g_mutex_unlock (&disp_lock);
  return NULL;
}

////////////////////////////////////////////////////////////////
/**
 * @} deepstream-app에서 그대로 가져옴
 */
int
main (int argc, char *argv[])
{
  testAppCtx = (TestAppCtx *) g_malloc0 (sizeof (TestAppCtx));
  GOptionContext *ctx = NULL;
  GOptionGroup *group = NULL;
  GError *error = NULL;
  guint i;

  ctx = g_option_context_new ("Nvidia DeepStream Prototype");
  group = g_option_group_new ("prototype", NULL, NULL, NULL, NULL);
  g_option_group_add_entries (group, entries);

  g_option_context_set_main_group (ctx, group);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  GST_DEBUG_CATEGORY_INIT (NVDS_APP, "NVDS_APP", 0, NULL);

  if (!g_option_context_parse (ctx, &argc, &argv, &error)) {
    NVGSTDS_ERR_MSG_V ("%s", error->message);
    g_print ("%s",g_option_context_get_help (ctx, TRUE, NULL));
    return -1;
  }

  if (print_version) {
    g_print ("prototype-app version %d.%d.%d\n",
        NVDS_APP_VERSION_MAJOR, NVDS_APP_VERSION_MINOR, NVDS_APP_VERSION_MICRO);
    return 0;
  }

  if (print_dependencies_version) {
    g_print ("prototype-app version %d.%d.%d\n",
        NVDS_APP_VERSION_MAJOR, NVDS_APP_VERSION_MINOR, NVDS_APP_VERSION_MICRO);
    return 0;
  }

  if (cfg_files) {
    num_instances = g_strv_length (cfg_files);
  }

  printf(">>> num-instances: %d\n", num_instances);
  if (!cfg_files || num_instances == 0) {
    NVGSTDS_ERR_MSG_V ("Specify config file with -c option");
    return_value = -1;
    goto done;
  }

  for (i = 0; i < num_instances; i++) {
    appCtx[i] = (AppCtx *) g_malloc0 (sizeof (AppCtx));
    appCtx[i]->person_class_id = -1;
    appCtx[i]->car_class_id = -1;
    appCtx[i]->index = i;
    appCtx[i]->active_source_index = -1;
    if (show_bbox_text) {
      appCtx[i]->show_bbox_text = TRUE;
    }

    if(IS_YAML(cfg_files[i])) {
      if (!parse_config_file_yaml (&appCtx[i]->config, cfg_files[i])) {
        NVGSTDS_ERR_MSG_V ("Failed to parse config file '%s'", cfg_files[i]);
        appCtx[i]->return_value = -1;
        goto done;
      }
    } else {
      NVGSTDS_ERR_MSG_V ("Failed to parse config file '%s'", cfg_files[i]);
      appCtx[i]->return_value = -1;
      goto done;
    }
  }

  for (i = 0; i < num_instances; i++) {
    for (guint j = 0; j < appCtx[i]->config.num_source_sub_bins; j++) {
       /** 소스를 강제로 (RTSP의 경우에만 해당) RTP/RTCP 채널에 TCP를 사용하도록 합니다.
        * 도커 컨테이너 내부에서 UDP 포트 사용으로 인한 문제를 피하기 위해 TCP를 강제로
        * 사용합니다.
        * 도커 내에서 실행되는 UDP RTCP 채널은 서버로부터 RTCP 송신 보고서를 수신하는 데
        * 문제가 있었습니다.
        */
      if (force_tcp)
        appCtx[i]->config.multi_source_config[j].select_rtp_protocol = 0x04;
    }
    if (!create_pipeline (appCtx[i], bbox_generated_probe_after_analytics,
            perf_cb)) {
      NVGSTDS_ERR_MSG_V ("Failed to create pipeline");
      return_value = -1;
      goto done;
    }
    /** RTPSession 플러그인의 소스 패드에 프로브를 추가합니다. */
    for (guint j = 0; j < appCtx[i]->pipeline.multi_src_bin.num_bins; j++) {
      testAppCtx->streams[j].id = j;
    }
    /** test5 앱에서 전형적인 IoT 사용 사례에 대해 여러 소스가 연결될 수 있으므로,
     * nvstreammux의 버퍼 풀 크기를 16으로 높입니다. */
    g_object_set (appCtx[i]->pipeline.multi_src_bin.streammux,
        "buffer-pool-size", STREAMMUX_BUFFER_POOL_SIZE, NULL);
  }

  main_loop = g_main_loop_new (NULL, FALSE);

  _intr_setup ();
  g_timeout_add (400, check_for_interrupt, NULL);

  g_mutex_init (&disp_lock);
  display = XOpenDisplay (NULL);
  for (i = 0; i < num_instances; i++) {
    guint j;

    if (!show_bbox_text) {
      GstElement *nvosd = appCtx[i]->pipeline.instance_bins[0].osd_bin.nvosd;
      g_object_set(G_OBJECT(nvosd), "display-text", FALSE, NULL);
    }

    if (gst_element_set_state (appCtx[i]->pipeline.pipeline,
            GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
      NVGSTDS_ERR_MSG_V ("Failed to set pipeline to PAUSED");
      return_value = -1;
      goto done;
    }

    for (j = 0; j < appCtx[i]->config.num_sink_sub_bins; j++) {
      XTextProperty xproperty;
      gchar *title;
      guint width, height;
      XSizeHints hints = {0};

      if (!GST_IS_VIDEO_OVERLAY (appCtx[i]->pipeline.instance_bins[0].sink_bin.
              sub_bins[j].sink)) {
        continue;
      }

      if (!display) {
        NVGSTDS_ERR_MSG_V ("Could not open X Display");
        return_value = -1;
        goto done;
      }

      if (appCtx[i]->config.sink_bin_sub_bin_config[j].render_config.width)
        width =
            appCtx[i]->config.sink_bin_sub_bin_config[j].render_config.width;
      else
        width = appCtx[i]->config.tiled_display_config.width;

      if (appCtx[i]->config.sink_bin_sub_bin_config[j].render_config.height)
        height =
            appCtx[i]->config.sink_bin_sub_bin_config[j].render_config.height;
      else
        height = appCtx[i]->config.tiled_display_config.height;

      width = (width) ? width : DEFAULT_X_WINDOW_WIDTH;
      height = (height) ? height : DEFAULT_X_WINDOW_HEIGHT;

      hints.flags = PPosition | PSize;
      hints.x = appCtx[i]->config.sink_bin_sub_bin_config[j].render_config.offset_x;
      hints.y = appCtx[i]->config.sink_bin_sub_bin_config[j].render_config.offset_y;
      hints.width = width;
      hints.height = height;

      windows[i] =
          XCreateSimpleWindow (display, RootWindow (display,
              DefaultScreen (display)), hints.x, hints.y, width, height, 2,
              0x00000000, 0x00000000);

      XSetNormalHints(display, windows[i], &hints);

      if (num_instances > 1)
        title = g_strdup_printf (APP_TITLE "-%d", i);
      else
        title = g_strdup (APP_TITLE);
      if (XStringListToTextProperty ((char **) &title, 1, &xproperty) != 0) {
        XSetWMName (display, windows[i], &xproperty);
        XFree (xproperty.value);
      }

      XSetWindowAttributes attr = { 0 };
      if ((appCtx[i]->config.tiled_display_config.enable &&
              appCtx[i]->config.tiled_display_config.rows *
              appCtx[i]->config.tiled_display_config.columns == 1) ||
          (appCtx[i]->config.tiled_display_config.enable == 0)) {
        attr.event_mask = KeyRelease;
      } else if (appCtx[i]->config.tiled_display_config.enable) {
        attr.event_mask = ButtonPress | KeyRelease;
      }
      XChangeWindowAttributes (display, windows[i], CWEventMask, &attr);

      Atom wmDeleteMessage = XInternAtom (display, "WM_DELETE_WINDOW", False);
      if (wmDeleteMessage != None) {
        XSetWMProtocols (display, windows[i], &wmDeleteMessage, 1);
      }
      XMapRaised (display, windows[i]);
      XSync (display, 1);       //discard the events for now
      gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (appCtx
              [i]->pipeline.instance_bins[0].sink_bin.sub_bins[j].sink),
          (gulong) windows[i]);
      gst_video_overlay_expose (GST_VIDEO_OVERLAY (appCtx[i]->pipeline.
              instance_bins[0].sink_bin.sub_bins[j].sink));
      if (!x_event_thread)
        x_event_thread = g_thread_new ("nvds-window-event-thread",
            nvds_x_event_thread, NULL);
    }
  }
  
  /* 에러가 발생한 경우 재생 상태를 설정하지 마십시오. */
  if (return_value != -1) {
    for (i = 0; i < num_instances; i++) {
      if (gst_element_set_state (appCtx[i]->pipeline.pipeline,
              GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {

        g_print ("\ncan't set pipeline to playing state.\n");
        return_value = -1;
        goto done;
      }
    }
  }

  print_runtime_commands ();

  changemode (1);

  g_timeout_add (40, event_thread_func, NULL);
  g_main_loop_run (main_loop);

  changemode (0);

done:

  g_print ("Quitting\n");
  for (i = 0; i < num_instances; i++) {
    if (appCtx[i] == NULL)
      continue;

    if (appCtx[i]->return_value == -1)
      return_value = -1;

    destroy_pipeline (appCtx[i]);

    g_mutex_lock (&disp_lock);
    if (windows[i])
      XDestroyWindow (display, windows[i]);
    windows[i] = 0;
    g_mutex_unlock (&disp_lock);

    g_free (appCtx[i]);
  }

  g_mutex_lock (&disp_lock);
  if (display)
    XCloseDisplay (display);
  display = NULL;
  g_mutex_unlock (&disp_lock);
  g_mutex_clear (&disp_lock);

  if (main_loop) {
    g_main_loop_unref (main_loop);
  }

  if (ctx) {
    g_option_context_free (ctx);
  }

  if (return_value == 0) {
    g_print ("App run successful\n");
  } else {
    g_print ("App run failed\n");
  }

  gst_deinit ();

  return return_value;

  g_free (testAppCtx);

  return 0;
}




