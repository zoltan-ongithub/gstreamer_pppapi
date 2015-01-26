/*
 * video_decoder_gstreamer.h
 *
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Christophe Priouzeau <christophe.priouzeau@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */
#ifndef PPAPI_GSTREAMER_VIDEO_DECODER_H_
#define PPAPI_GSTREAMER_VIDEO_DECODER_H_


void *VideoDecoderGstreamer_create(bool hole);
void VideoDecoderGstreamer_release(void *gst);

int32_t VideoDecoderGstreamer_initialize(void *gst, const char *url);
int32_t VideoDecoderGstreamer_play(void *gst);
int32_t VideoDecoderGstreamer_stop(void *gst);
int32_t VideoDecoderGstreamer_pause(void *gst);
bool VideoDecoderGstreamer_isPlaying(void *gst);

void * VideoDecoderGstreamer_getBuffer(void *gst, int *size);


void VideoDecoderGstreamer_setWindow(void *gst, int x, int y, int w, int h);

bool VideoDecoderGstreamer_useHole(void *gst);

#endif /*  PPAPI_GSTREAMER_VIDEO_DECODER_H_ */

