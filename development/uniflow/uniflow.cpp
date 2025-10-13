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

