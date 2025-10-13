#include "uniflow.h"

#include "output_types.h"

namespace uf {



}


void testmain() {

    uf::Detection d[2];
    d[0].payload = uf::Box();
    d[1].payload = uf::Confidence();

    uf::i8 tensor[128];
    uf::TensorView<uf::i8> tensorView(tensor, 128);

    tensorView.set01(5, 3.14f);
    //float v = tensorView.get01(5);

    tensorView[0] = 1.0f;
}

/*

// Suppose you’ve prepared a quantized tensor in a malloc’d block:
gpointer data = g_malloc (tensor_size);
memcpy (data, src, tensor_size);

// Wrap in GBytes so the meta owns a ref-counted view
GBytes *bytes = g_bytes_new_take (data, tensor_size); // GBytes will free it

guint32 dims[] = {1, 3, 640, 640}; // NCHW example

gst_buffer_add_infer_input_meta (buf,
    bytes,
    GST_INFER_DTYPE_I8,                   // dtype
    G_N_ELEMENTS(dims), dims, NULL,       // dims, strides=NULL => tightly packed
    128, 1.0f/255.0f,                     // zero_point, scale
    GST_INFER_RANGE_ZERO_TO_ONE, 0.f, 1.f // range hint
);
g_bytes_unref (bytes); // meta now keeps its own ref


GstInferInputMeta *m = gst_buffer_get_infer_input_meta (buf);
if (m && m->tensor) {
  gsize sz = 0;
  const guint8 *ptr = g_bytes_get_data (m->tensor, &sz);
  // use ptr, size, dtype, dims, zero_point, scale to feed your runtime
}
  
*/


