#ifndef __AMPSINK_H__
#define __AMPSINK_H__

#include <memory>
#include <mutex>

#include <amp/Tools.h>

#include <gst/audio/audio.h>
#include <gst/gst.h>
#include <gst/gstelement.h>
#include <gst/gstutils.h>
#include <gst/video/video.h>
#include <gst/webrtc/webrtc.h>

#include "http_server.h"
#include "model_reg.h"
#include "webrtc_ws.h"

struct _GstAmpSinkClass {
    GstBinClass parent_class;
};

typedef struct _GstAmpSink GstAmpSink;
typedef struct _GstAmpSinkClass GstAmpSinkClass;

struct ToggleStateRequest {
    GstElement *element = nullptr;
    GstState resulting = GST_STATE_NULL;

    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    bool ok = false;
};

struct ToggleInvokeBox {
    std::shared_ptr<ToggleStateRequest> req;
};

struct GstAmpPrivate {
    std::unique_ptr<AmpSinkHttpServer> http_server;
    std::unique_ptr<WebRtcWebSocket> webrtc_websocket;
    std::unique_ptr<ModelRegistry> model_registry;
};

struct _GstAmpSink {
    GstBin parent;

    GstElement *vconv;
    GstElement *queue;
    GstElement *vp8enc;
    GstElement *rtpvp8pay;
    GstElement *tee;

    // Drain branch to make the pipeline complete
    GstElement *drain_queue;
    GstElement *drain_fakesink;
    GstPad *drain_tee_src_pad;

    /* properties */
    gchar *host;
    gchar *static_files_location;
    gint http_port;
    gint ws_port;

    GstAmpPrivate *private_data;
};

#endif // ! __AMPSINK_H__
