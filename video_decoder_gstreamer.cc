/*
 * video_decoder_gstreamer.cc
 *
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Christophe Priouzeau <christophe.priouzeau@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */
#include <string.h>
#include <stdio.h>

#include <gst/gst.h>
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ppapi/c/pp_errors.h"

#include "video_decoder_gstreamer.h"

typedef struct _VideoDecoderGstreamer {
  GstElement *playbin;
  GstElement *source;
  GstElement *sink;
  bool playing;
  GstBus *bus;
  bool stop;
  bool initialized;
  bool hole;

  GAsyncQueue *queue;
} VideoDecoderGstreamer;

/* playbin flags */
typedef enum {
  GST_PLAY_FLAG_VIDEO           = (1 << 0), /* We want video output */
  GST_PLAY_FLAG_AUDIO           = (1 << 1), /* We want audio output */
  GST_PLAY_FLAG_TEXT            = (1 << 2),  /* We want subtitle output */
  GST_PLAY_FLAG_VIS             = (1 << 3),
  GST_PLAY_FLAG_SOFT_VOLUME     = (1 << 4),
  GST_PLAY_FLAG_NATIVE_AUDIO    = (1 << 5),
  GST_PLAY_FLAG_NATIVE_VIDEO    = (1 << 6),
  GST_PLAY_FLAG_DOWNLOAD        = (1 << 7),
  GST_PLAY_FLAG_BUFFERING       = (1 << 8),
  GST_PLAY_FLAG_DEINTERLACE     = (1 << 9),
  GST_PLAY_FLAG_SOFT_COLORBALANCE = (1 << 10)
} GstPlayFlags;

static void
buffers_cb (GstElement * fakesink, GstBuffer * buffer, GstPad * pad,
        gpointer user_data)
{
    VideoDecoderGstreamer *decoder = (VideoDecoderGstreamer *)user_data;
    g_print("---buffers_cb\n");

    g_async_queue_push (decoder->queue, gst_buffer_ref (buffer));

    g_print("---buffers_cb <<<<\n");
}

static gboolean gstPlayer_handle_message (GstBus *bus, GstMessage *msg, gpointer user_data)
{
  VideoDecoderGstreamer *data = (VideoDecoderGstreamer *)user_data;

  g_print("gstPlayer_handle_message msg=%d,%s \n",
                  GST_MESSAGE_TYPE(msg),
                  GST_MESSAGE_TYPE_NAME(msg));

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR: {
      GError *err;
      gchar *debug;

      gst_message_parse_error (msg, &err, &debug);
      g_print ("handle_message:ERROR: %s\n", err->message);
      g_error_free (err);
      g_free (debug);

      gst_element_set_state (data->playbin, GST_STATE_READY);
      data->stop=true;
      break;
    }
    case GST_MESSAGE_EOS:
      /* end-of-stream */
      g_print("handle_message:EOS\n");
      gst_element_set_state (data->playbin, GST_STATE_READY);
      data->stop=true;
      break;
    case GST_MESSAGE_BUFFERING: {
      gint percent = 0;

      /* If the stream is live, we do not care about buffering. */
      if (data->playing) break;

      gst_message_parse_buffering (msg, &percent);
      g_print("handle_message:BUFFERING(%3d%%)\n", percent);
      /* Wait until buffering is complete before start/resume playing */
      if (percent < 100)
        gst_element_set_state (data->playbin, GST_STATE_PAUSED);
      else
        gst_element_set_state (data->playbin, GST_STATE_PLAYING);
      break;
    }
    case GST_MESSAGE_CLOCK_LOST:
      /* Get a new clock */
      g_print("handle_message:CLOCK_LOST\n");
      gst_element_set_state (data->playbin, GST_STATE_PAUSED);
      gst_element_set_state (data->playbin, GST_STATE_PLAYING);
      break;
    case GST_MESSAGE_STATE_CHANGED:
      GstState old_state, new_state, pending_state;
      gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
      if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->playbin)) {
        g_print("handle_message:STATE_CHANGED %s to %s:\n",
            gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
        data->playing = (new_state == GST_STATE_PLAYING);
      }
      break;
    default:
      /* Unhandled message */
      break;
    }

    return true;
}




void *VideoDecoderGstreamer_create(bool hole)
{
    VideoDecoderGstreamer *decoder = new VideoDecoderGstreamer;
    g_print("---VideoDecoderGstreamer::create\n");
    memset (decoder, 0, sizeof(VideoDecoderGstreamer));
    decoder->hole = hole;

    if (!decoder->hole) {
         decoder->queue =
                g_async_queue_new_full ((GDestroyNotify) gst_mini_object_unref);
    }
    return (void*)decoder;
}

void VideoDecoderGstreamer_release(void *gst) {
    VideoDecoderGstreamer *decoder = (VideoDecoderGstreamer*)gst;
    if (!decoder->initialized)
        return;

    decoder->stop = true;
    if(NULL != decoder->bus) {
      gst_object_unref (decoder->bus);
      decoder->bus = NULL;
    }

    if(NULL != decoder->playbin) {
       gst_element_set_state (decoder->playbin, GST_STATE_NULL);
       gst_object_unref (decoder->playbin);
       decoder->playbin = NULL;
    }
    decoder->initialized = false;
}

int32_t VideoDecoderGstreamer_initialize(void *gst, const char *url)
{
    VideoDecoderGstreamer *decoder = (VideoDecoderGstreamer*)gst;
    if (decoder->initialized)
        return PP_ERROR_FAILED;

    g_print("---VideoDecoderGstreamer::initialize %s \n",url);

    /* set environment variable for gstreamer */
    g_setenv("GST_PLUGIN_PATH_1_0", "/usr/lib/gstreamer-1.0", FALSE);
    //g_setenv("GST_REGISTRY_1_0", "/tmp/registry.arm.bin", FALSE);
    //g_setenv("GST_REGISTRY_UPDATE", "yes", FALSE);
    //g_setenv("GST_REGISTRY_REUSE_PLUGIN_SCANNER", "yes", FALSE);

    /* Initialize GStreamer */
    //gst_debug_set_default_threshold((GstDebugLevel)8);
    gst_init (NULL, NULL);

    {
        gchar *version_str;

        version_str = gst_version_string ();
        g_print ("version %d.%d.%d\n",
            GST_VERSION_MAJOR, GST_VERSION_MINOR, GST_VERSION_MICRO);
        g_print ("%s\n", version_str);
        g_free (version_str);
    }

    /* Create the elements */
    decoder->playbin = gst_element_factory_make ("playbin", "player");

    if (!decoder->playbin) {
        g_printerr ("Not all elements could be created.\n");
        return PP_ERROR_FAILED;
    }
    if (decoder->hole) {
         g_print("---VideoDecoderGstreamer::initialize with hole\n");

         decoder->sink = gst_element_factory_make ("kmssink", "vsink");
         g_object_set (decoder->sink,
              "sync", TRUE,
              "qos", TRUE,
              "enable-last-sample", FALSE,
              "max-lateness", 20 * GST_MSECOND,
              "in-plane", true, NULL);

        /* Set the URI to play */
        g_object_set (decoder->playbin, "uri", url,
              "video-sink", decoder->sink, //to test with kmssink
              "flags", GST_PLAY_FLAG_NATIVE_VIDEO |
               GST_PLAY_FLAG_NATIVE_AUDIO,
               NULL);


    } else {
        GstElement *pipeline_sink, *color_conv;

        g_print("---VideoDecoderGstreamer::initialize without hole\n");

        pipeline_sink = gst_pipeline_new ("pipeline");

        decoder->sink = gst_element_factory_make ("kmssink", "vsink");
        color_conv = gst_element_factory_make ("bdisptransform", "cconv");

        g_object_set (decoder->sink,
              "sync", TRUE,
              "silent", TRUE,
              "qos", TRUE,
              "enable-last-sample", FALSE,
              "max-lateness", 20 * GST_MSECOND,
              "signal-handoffs", TRUE, NULL);
        g_signal_connect (decoder->sink, "preroll-handoff", G_CALLBACK (buffers_cb), (void*)decoder);
        g_signal_connect (decoder->sink, "handoff", G_CALLBACK (buffers_cb), (void*)decoder);
        /* change video source caps */
        GstCaps *caps = gst_caps_new_simple("video/x-raw",
                            "format", G_TYPE_STRING, "RGB",
                            "width", G_TYPE_INT, 320,
                            "height", G_TYPE_INT, 240,
                            NULL) ;

        gst_bin_add_many (GST_BIN (pipeline_sink), color_conv, decoder->sink, NULL);
        gst_element_link_filtered(color_conv, caps, decoder->sink) ;

        /* Set the URI to play */
        g_object_set (decoder->playbin, "uri", url,
              "video-sink", pipeline_sink,
              "flags", GST_PLAY_FLAG_NATIVE_VIDEO |
               GST_PLAY_FLAG_NATIVE_AUDIO,
               NULL);
        gst_caps_unref(caps) ;

    }

    /* Add a bus watch, so we get notified when a message arrives */
    decoder->bus = gst_pipeline_get_bus(GST_PIPELINE(decoder->playbin));
    gst_bus_add_watch(decoder->bus, gstPlayer_handle_message, gst);

    GstStateChangeReturn ret = gst_element_set_state (decoder->playbin, GST_STATE_READY);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("Unable to set the pipeline to the ready state.\n");
        return PP_ERROR_FAILED;
    }
    decoder->initialized = true;

    return PP_OK;
}
int32_t VideoDecoderGstreamer_play(void *gst)
{
    VideoDecoderGstreamer *decoder = (VideoDecoderGstreamer*)gst;
    if (!decoder->initialized)
        return PP_ERROR_FAILED;

    GstStateChangeReturn ret = gst_element_set_state (decoder->playbin, GST_STATE_PLAYING);
    if (GST_STATE_CHANGE_FAILURE == ret) {
        g_printerr ("Unable to set the pipeline to the playing state.\n");
        return PP_ERROR_FAILED;
    }
    decoder->playing = 1;

    return PP_OK;
}


int32_t VideoDecoderGstreamer_pause(void *gst)
{
    VideoDecoderGstreamer *decoder = (VideoDecoderGstreamer*)gst;
    if (!decoder->initialized)
        return PP_ERROR_FAILED;

    GstStateChangeReturn ret;
    if(decoder->playing) {
        g_print("---VideoDecoderGstreamer::pause set PAUSED\n");
        ret = gst_element_set_state (decoder->playbin, GST_STATE_PAUSED);
        decoder->playing = false;
    }
    else {
        g_print("---VideoDecoderGstreamer::pause set PLAYING\n");
        ret = gst_element_set_state (decoder->playbin, GST_STATE_PLAYING);
        decoder->playing = true;
    }

    if (GST_STATE_CHANGE_FAILURE == ret) {
        g_printerr ("Unable to set the pipeline to the playing state.\n");
        return PP_ERROR_FAILED;
    }

    return PP_OK;
}

int32_t VideoDecoderGstreamer_stop(void *gst)
{
    VideoDecoderGstreamer_release(gst);
    return PP_OK;
}

bool VideoDecoderGstreamer_isPlaying(void *gst) {
    VideoDecoderGstreamer *decoder = (VideoDecoderGstreamer*)gst;
    if (!decoder->initialized)
        return false;
    if (decoder->playing) {
        g_print("---VideoDecoderGstreamer::isplay %d\n",!decoder->stop);
       return  !decoder->stop;
    } else {
        return decoder->playing;
    }
}

void * VideoDecoderGstreamer_getBuffer(void *gst, int *size) {
    VideoDecoderGstreamer *decoder = (VideoDecoderGstreamer*)gst;
    if (!decoder->initialized)
        return NULL;

    if (!decoder->hole) {
        GstMiniObject *object = NULL;
        void *data;
        if (g_async_queue_length (decoder->queue) == 0) {
            return NULL;
        }
        else if ((object = (GstMiniObject *)g_async_queue_try_pop (decoder->queue))) {
            if (GST_IS_BUFFER (object)) {
                g_print("---gstPlayer_getBuffer %d\n", __LINE__);
                GstBuffer *buffer = GST_BUFFER_CAST (object);
                GstMapInfo mapinfo = { 0, };
                guint8 *gstdata, gstsize;
                g_print("---g_async_queue_try_pop\n");

                gst_buffer_map (buffer, &mapinfo, GST_MAP_READ);
                gstdata = mapinfo.data;
                gstsize = mapinfo.size;

                data =(guint8 *)malloc(gstsize);
                memcpy (data, gstdata, gstsize);
                *size = gstsize;

                gst_buffer_unmap (buffer, &mapinfo);

                gst_buffer_unref (buffer);
                object = NULL;
                return data;
            } else if (GST_IS_QUERY (object)) {
                GstQuery *query = GST_QUERY_CAST (object);
                GstStructure *s = (GstStructure *) gst_query_get_structure (query);
                 g_print ("\nQuery\n");

                if (gst_structure_has_name (s, "not-used")) {
                    g_assert_not_reached ();
                } else {
                    g_assert_not_reached ();
                }
            } else if (GST_IS_EVENT (object)) {
                GstEvent *event = GST_EVENT_CAST (object);
                g_print ("\nevent %p %s\n", event,
                        gst_event_type_get_name (GST_EVENT_TYPE (event)));

                gst_event_unref (event);
                object = NULL;
            }
        }
    }
    return NULL;
}
void VideoDecoderGstreamer_setWindow(void *gst, int x, int y, int w, int h) {
    VideoDecoderGstreamer *decoder = (VideoDecoderGstreamer*)gst;
    char rect[64];

    if (!decoder->initialized)
        return;

    sprintf(rect, "%d,%d,%d,%d", x, y, w, h);
    if (decoder->sink && decoder->hole)
        //g_object_set(decoder->sink, "rectangle", rect, NULL);
        g_object_set(decoder->sink, "in-plane", true,
                "plane-x", x,
                "plane-y", y,
                NULL);

}
bool VideoDecoderGstreamer_useHole(void *gst)
{
    VideoDecoderGstreamer *decoder = (VideoDecoderGstreamer*)gst;
    return decoder->hole;
}

