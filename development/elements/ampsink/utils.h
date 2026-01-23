#ifndef __UTILS_H__

#include <gst/gstelement.h>

#include <string>

GstElement *get_top_pipeline(GstElement *elem);

void dump_sink_pads(GstElement *element);

// set this environment variable to get this to work: GST_DEBUG_DUMP_DOT_DIR
void dump_pipeline_graph(GstElement *element, const std::string &file_name);

void release_request_pad_and_unref(GstElement *elem, GstPad **ppad);
void remove_pad_if_present(GstElement *elem, GstPad **ppad);

#endif // !__UTILS_H__
