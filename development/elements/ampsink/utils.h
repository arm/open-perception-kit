#ifndef __UTILS_H__

#include <gst/gstelement.h>

#include <string>

GstElement *get_top_pipeline(GstElement *elem);
GstElement *get_element_by_name(GstElement *top_level, const std::string &element_name);
GstElement *get_element_by_type(GstElement *top_level, const std::string &type_name);

void dump_sink_pads(GstElement *element);

// set this environment variable to get this to work: GST_DEBUG_DUMP_DOT_DIR
void dump_pipeline_graph(GstElement *element, const std::string &file_name);

void release_request_pad_and_unref(GstElement *elem, GstPad **ppad);
void remove_pad_if_present(GstElement *elem, GstPad **ppad);

bool set_state_elements_many(GstState state, std::initializer_list<GstElement *> elems);

#endif // !__UTILS_H__
