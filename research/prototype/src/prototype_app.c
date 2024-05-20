#include "prototype_app.h"

#include <sys/stat.h>
#include <sys/inotify.h>

#include <unistd.h>

////////////////////////////////////////////////////////////////
extern AppCtx *appCtx[];
extern TestAppCtx *testAppCtx;
extern gboolean playback_utc;
extern AppConfigAnalyticsModel model_used;

////////////////////////////////////////////////////////////////
struct timespec extract_utc_from_uri (gchar * uri);

////////////////////////////////////////////////////////////////
static GstClockTime
generate_ts_rfc3339_from_ts (char *buf, int buf_size, GstClockTime ts,
    gchar * src_uri, gint stream_id)
{
  time_t tloc;
  struct tm tm_log;
  char strmsec[6];              //.nnnZ\0
  int ms;

  GstClockTime ts_generated;

  if (playback_utc
      || (appCtx[0]->config.multi_source_config[stream_id].type !=
          NV_DS_SOURCE_RTSP)) {
    if (testAppCtx->streams[stream_id].meta_number == 0) {
      testAppCtx->streams[stream_id].timespec_first_frame =
          extract_utc_from_uri (src_uri);
      memcpy (&tloc,
          (void *) (&testAppCtx->streams[stream_id].timespec_first_frame.
              tv_sec), sizeof (time_t));
      ms = testAppCtx->streams[stream_id].timespec_first_frame.tv_nsec /
          1000000;
      testAppCtx->streams[stream_id].gst_ts_first_frame = ts;
      ts_generated =
          GST_TIMESPEC_TO_TIME (testAppCtx->streams[stream_id].
          timespec_first_frame);
      if (ts_generated == 0) {
        g_print
            ("WARNING; playback mode used with URI [%s] not conforming to timestamp format;"
            " check README; using system-time\n", src_uri);
        clock_gettime (CLOCK_REALTIME,
            &testAppCtx->streams[stream_id].timespec_first_frame);
        ts_generated =
            GST_TIMESPEC_TO_TIME (testAppCtx->streams[stream_id].
            timespec_first_frame);
      }
    } else {
      GstClockTime ts_current =
          GST_TIMESPEC_TO_TIME (testAppCtx->
          streams[stream_id].timespec_first_frame) + (ts -
          testAppCtx->streams[stream_id].gst_ts_first_frame);
      struct timespec timespec_current;
      GST_TIME_TO_TIMESPEC (ts_current, timespec_current);
      memcpy (&tloc, (void *) (&timespec_current.tv_sec), sizeof (time_t));
      ms = timespec_current.tv_nsec / 1000000;
      ts_generated = ts_current;
    }
  } else {
    /** ts itself is UTC Time in ns */
    struct timespec timespec_current;
    GST_TIME_TO_TIMESPEC (ts, timespec_current);
    memcpy (&tloc, (void *) (&timespec_current.tv_sec), sizeof (time_t));
    ms = timespec_current.tv_nsec / 1000000;
    ts_generated = ts;
  }
  gmtime_r (&tloc, &tm_log);
  strftime (buf, buf_size, "%Y-%m-%dT%H:%M:%S", &tm_log);
  g_snprintf (strmsec, sizeof (strmsec), ".%.3dZ", ms);
  strncat (buf, strmsec, buf_size);
  // LOGD ("ts=%s\n", buf);

  return ts_generated;
}

////////////////////////////////////////////////////////////////
static void
generate_ts_rfc3339 (char *buf, int buf_size)
{
  time_t tloc;
  struct tm tm_log;
  struct timespec ts;
  char strmsec[6];              //.nnnZ\0

  clock_gettime (CLOCK_REALTIME, &ts);
  memcpy (&tloc, (void *) (&ts.tv_sec), sizeof (time_t));
  gmtime_r (&tloc, &tm_log);
  strftime (buf, buf_size, "%Y-%m-%dT%H:%M:%S", &tm_log);
  int ms = ts.tv_nsec / 1000000;
  g_snprintf (strmsec, sizeof (strmsec), ".%.3dZ", ms);
  strncat (buf, strmsec, buf_size);
}

////////////////////////////////////////////////////////////////
static gchar *
get_first_result_label (NvDsClassifierMeta * classifierMeta)
{
  GList *n;
  for (n = classifierMeta->label_info_list; n != NULL; n = n->next) {
    NvDsLabelInfo *labelInfo = (NvDsLabelInfo *) (n->data);
    if (labelInfo->result_label[0] != '\0') {
      return g_strdup (labelInfo->result_label);
    }
  }
  return NULL;
}

////////////////////////////////////////////////////////////////
/**
 * @brief  Fill NvDsVehicleObject with the NvDsClassifierMetaList
 *         information in NvDsObjectMeta
 *         NOTE: This function assumes the test-application is
 *         run with 3 X SGIEs sample config:
 *         test5_config_file_src_infer_tracker_sgie.txt
 *         or an equivalent config
 *         NOTE: If user is adding custom SGIEs, make sure to
 *         edit this function implementation
 * @param  obj_params [IN] The NvDsObjectMeta as detected and kept
 *         in NvDsBatchMeta->NvDsFrameMeta(List)->NvDsObjectMeta(List)
 * @param  obj [IN/OUT] The NvDSMeta-Schema defined Vehicle metadata
 *         structure
 */
/**
 * @brief  NvDsClassifierMetaList에서 NvDsVehicleObject를 채웁니다.
 *         NvDsObjectMeta의 정보를 사용합니다.
 *         주의: 이 함수는 테스트 애플리케이션이
 *         3 X SGIEs 샘플 구성으로 실행되었다고 가정합니다:
 *         test5_config_file_src_infer_tracker_sgie.txt
 *         또는 동등한 구성
 *         주의: 사용자가 사용자 정의 SGIEs를 추가하는 경우
 *         이 함수 구현을 수정해야 합니다.
 * @param  obj_params [IN] NvDsObjectMeta로 감지되고 유지된 정보
 *         NvDsBatchMeta->NvDsFrameMeta(List)->NvDsObjectMeta(List)
 * @param  obj [IN/OUT] NvDSMeta-Schema로 정의된 차량 메타데이터
 *         구조체
 */
static
void schema_fill_sample_sgie_vehicle_metadata (NvDsObjectMeta * obj_params,
    NvDsVehicleObject * obj)
{
  if (!obj_params || !obj) {
    return;
  }

  /** The JSON obj->classification, say type, color, or make
   * according to the schema shall have null (unknown)
   * classifications (if the corresponding sgie failed to provide a label)
   */
  obj->type = NULL;
  obj->make = NULL;
  obj->model = NULL;
  obj->color = NULL;
  obj->license = NULL;
  obj->region = NULL;

  GList *l;
  for (l = obj_params->classifier_meta_list; l != NULL; l = l->next) {
    NvDsClassifierMeta *classifierMeta = (NvDsClassifierMeta *) (l->data);
    switch (classifierMeta->unique_component_id) {
      case SECONDARY_GIE_VEHICLE_TYPE_UNIQUE_ID:
        obj->type = get_first_result_label (classifierMeta);
        break;
      case SECONDARY_GIE_VEHICLE_COLOR_UNIQUE_ID:
        obj->color = get_first_result_label (classifierMeta);
        break;
      case SECONDARY_GIE_VEHICLE_MAKE_UNIQUE_ID:
        obj->make = get_first_result_label (classifierMeta);
        break;
      default:
        break;
    }
  }
}

#ifdef GENERATE_DUMMY_META_EXT
////////////////////////////////////////////////////////////////
static void
generate_vehicle_meta (gpointer data)
{
  NvDsVehicleObject *obj = (NvDsVehicleObject *) data;

  obj->type = g_strdup ("sedan-dummy");
  obj->color = g_strdup ("blue");
  obj->make = g_strdup ("Bugatti");
  obj->model = g_strdup ("M");
  obj->license = g_strdup ("XX1234");
  obj->region = g_strdup ("CA");
}

////////////////////////////////////////////////////////////////
static void
generate_person_meta (gpointer data)
{
  NvDsPersonObject *obj = (NvDsPersonObject *) data;
  obj->age = 45;
  obj->cap = g_strdup ("none-dummy-person-info");
  obj->hair = g_strdup ("black");
  obj->gender = g_strdup ("male");
  obj->apparel = g_strdup ("formal");
}
#endif /*GENERATE_DUMMY_META_EXT*/

////////////////////////////////////////////////////////////////
void
generate_event_msg_meta (AppCtx * app_ctx, gpointer data, gint class_id, gboolean useTs,
    GstClockTime ts, gchar * src_uri, gint stream_id, guint sensor_id,
    NvDsObjectMeta * obj_params, float scaleW, float scaleH,
    NvDsFrameMeta * frame_meta)
{
  NvDsEventMsgMeta *meta = (NvDsEventMsgMeta *) data;
  GstClockTime ts_generated = 0;

  meta->objType = NVDS_OBJECT_TYPE_UNKNOWN; /**< object unknown */
  /* The sensor_id is parsed from the source group name which has the format
   * [source<sensor-id>]. */
  meta->sensorId = sensor_id;
  meta->placeId = sensor_id;
  meta->moduleId = sensor_id;
  meta->frameId = frame_meta->frame_num;
  meta->ts = (gchar *) g_malloc0 (MAX_TIME_STAMP_LEN + 1);
  meta->objectId = (gchar *) g_malloc0 (MAX_LABEL_SIZE);

  strncpy (meta->objectId, obj_params->obj_label, MAX_LABEL_SIZE);

  /** INFO: This API is called once for every 30 frames (now) */
  if (useTs && src_uri) {
    ts_generated =
        generate_ts_rfc3339_from_ts (meta->ts, MAX_TIME_STAMP_LEN, ts, src_uri,
        stream_id);
  } else {
    generate_ts_rfc3339 (meta->ts, MAX_TIME_STAMP_LEN);
  }

  /**
   * Valid attributes in the metadata sent over nvmsgbroker:
   * a) Sensor ID (shall be configured in nvmsgconv config file)
   * b) bbox info (meta->bbox) <- obj_params->rect_params (attr_info have sgie info)
   * c) tracking ID (meta->trackingId) <- obj_params->object_id
   */

  /** bbox - resolution is scaled by nvinfer back to
   * the resolution provided by streammux
   * We have to scale it back to original stream resolution
    */

  meta->bbox.left = obj_params->rect_params.left * scaleW;
  meta->bbox.top = obj_params->rect_params.top * scaleH;
  meta->bbox.width = obj_params->rect_params.width * scaleW;
  meta->bbox.height = obj_params->rect_params.height * scaleH;

  /** tracking ID */
  meta->trackingId = obj_params->object_id;

  /** sensor ID when streams are added using nvmultiurisrcbin REST API */
  NvDsSensorInfo* sensorInfo = get_sensor_info(app_ctx, stream_id);
  if(sensorInfo) {
    /** this stream was added using REST API; we have Sensor Info! */
    LOGD("this stream [%d:%s] was added using REST API; we have Sensor Info\n",
        sensorInfo->source_id, sensorInfo->sensor_id);
    meta->sensorStr = g_strdup (sensorInfo->sensor_id);
  }

  (void) ts_generated;

  /*
   * This demonstrates how to attach custom objects.
   * Any custom object as per requirement can be generated and attached
   * like NvDsVehicleObject / NvDsPersonObject. Then that object should
   * be handled in gst-nvmsgconv component accordingly.
   */
  if (model_used == APP_CONFIG_ANALYTICS_RESNET_PGIE_3SGIE_TYPE_COLOR_MAKE) {
    if (class_id == RESNET10_PGIE_3SGIE_TYPE_COLOR_MAKECLASS_ID_CAR) {
      meta->type = NVDS_EVENT_MOVING;
      meta->objType = NVDS_OBJECT_TYPE_VEHICLE;
      meta->objClassId = RESNET10_PGIE_3SGIE_TYPE_COLOR_MAKECLASS_ID_CAR;

      NvDsVehicleObject *obj =
          (NvDsVehicleObject *) g_malloc0 (sizeof (NvDsVehicleObject));
      schema_fill_sample_sgie_vehicle_metadata (obj_params, obj);

      meta->extMsg = obj;
      meta->extMsgSize = sizeof (NvDsVehicleObject);
    }
#ifdef GENERATE_DUMMY_META_EXT
    else if (class_id == RESNET10_PGIE_3SGIE_TYPE_COLOR_MAKECLASS_ID_PERSON) {
      meta->type = NVDS_EVENT_ENTRY;
      meta->objType = NVDS_OBJECT_TYPE_PERSON;
      meta->objClassId = RESNET10_PGIE_3SGIE_TYPE_COLOR_MAKECLASS_ID_PERSON;

      NvDsPersonObject *obj =
          (NvDsPersonObject *) g_malloc0 (sizeof (NvDsPersonObject));
      generate_person_meta (obj);

      meta->extMsg = obj;
      meta->extMsgSize = sizeof (NvDsPersonObject);
    }
#endif /**GENERATE_DUMMY_META_EXT*/
  }
}