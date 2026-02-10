#include "postproc/PersonClassificationParser.h"
#include "amp/Perception.h"
#include "amp/PerceptionContext.h"

#include <cmath>
#include <cstdint>

using namespace amp;

amp::Result<void> PersonClassificationParser::parse(const amp::TensorParser::Input &input,
                                                    amp::Perception::Layer &detectionResult) {

    assert(input.tensors[0]);

    assert(input.tensors[0]->getShape().dimensionCount == 2);

    assert(input.tensors[0]->getShape().valueCount[0] == 1);
    assert(input.tensors[0]->getShape().valueCount[1] == 2);

    /*
    bool isPerson = input.tensors[0]->get(1) > input.tensors[0]->get(0);
    fmt::print("PersonClassificationParser [person: {} vs non-person: {}] says it is a {}\n",
               input.tensors[0]->get(1),
               input.tensors[0]->get(0),
               isPerson ? "person" : "not person");
    */

    return {};
}
