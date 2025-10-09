#include "uniflow.h"

namespace uf {



}


void testmain() {

    uf::i8 tensor[128];
    uf::TensorView<uf::i8> tensorView(tensor, 128);

    tensorView.set01(5, 3.14f);
    //float v = tensorView.get01(5);

    tensorView[0] = 1.0f;
}

