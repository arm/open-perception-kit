#include "Inference.h"

#include <cstdio>
#include <executorch/extension/module/module.h>
#include <executorch/extension/tensor/tensor_ptr_maker.h>

using ::executorch::aten::ScalarType; // (in many builds ScalarType lives here)
using ::executorch::extension::MethodMeta;
using ::executorch::extension::Module;
using ::executorch::runtime::Result;

// ---- helpers

static const char *scalar_type_to_cstr(ScalarType t) {
    switch (t) {
    case ScalarType::Byte:
        return "Byte (u8)";
    case ScalarType::Char:
        return "Char (i8)";
    case ScalarType::Short:
        return "Short (i16)";
    case ScalarType::Int:
        return "Int (i32)";
    case ScalarType::Long:
        return "Long (i64)";
    case ScalarType::Half:
        return "Half (f16)";
    case ScalarType::Float:
        return "Float (f32)";
    case ScalarType::Double:
        return "Double (f64)";
    case ScalarType::Bool:
        return "Bool";
    default:
        return "Unknown";
    }
}

template <class SizesT> static void print_sizes(const SizesT &sizes) {
    std::printf("[");
    for (size_t i = 0; i < sizes.size(); ++i) {
        std::printf("%zd%s", (ssize_t)sizes[i], (i + 1 < sizes.size()) ? ", " : "");
    }
    std::printf("]");
}

static void print_tensor_meta(
    const char *prefix,
    const Result<decltype(std::declval<MethodMeta>().input_tensor_meta(0))::value_type>
        & /*unused*/) {
    // (kept just to show intent; see below where we print directly)
}

// ---- main printer

void PrintExecuTorchModelInfo(const std::string &pte_path) {
    Module module(pte_path.c_str());

    // method_names() forces program load on first call
    const auto names = module.method_names();
    if (!names.ok()) {
        std::printf("Failed to query method names: error=%d\n", (int)names.error());
        return;
    }

    std::printf("Model: %s\n", pte_path.c_str());
    std::printf("Methods (%zu):\n", names->size());

    for (const auto &method_name : *names) {
        std::printf("  - %s\n", method_name.c_str());

        // method_meta() forces method load on first call
        const auto mm = module.method_meta(method_name);
        if (!mm.ok()) {
            std::printf("    method_meta() failed: error=%d\n", (int)mm.error());
            continue;
        }

        std::printf("    inputs:  %zu\n", (size_t)mm->num_inputs());
        std::printf("    outputs: %zu\n", (size_t)mm->num_outputs());

        // Inputs
        for (size_t i = 0; i < (size_t)mm->num_inputs(); ++i) {
            const auto tm = mm->input_tensor_meta(i);
            if (!tm.ok()) {
                std::printf("    in[%zu]: (not a tensor / no meta) error=%d\n", i, (int)tm.error());
                continue;
            }

            std::printf("    in[%zu]: dtype=%s shape=", i, scalar_type_to_cstr(tm->scalar_type()));
            print_sizes(tm->sizes());

            // These exist in some versions/builds; if your compile fails, delete these lines.
            std::printf(" nbytes=%zu", (size_t)tm->nbytes());
            std::printf(" memory_planned=%s", tm->is_memory_planned() ? "true" : "false");

            // Very rough “quant hint” (real scale/zp may not be exposed here)
            const bool looks_quant =
                (tm->scalar_type() == ScalarType::Byte || tm->scalar_type() == ScalarType::Char);
            std::printf(" quant_hint=%s\n", looks_quant ? "maybe(u8/i8)" : "no");
        }

        // Outputs
        for (size_t i = 0; i < (size_t)mm->num_outputs(); ++i) {
            const auto tm = mm->output_tensor_meta(i);
            if (!tm.ok()) {
                std::printf(
                    "    out[%zu]: (not a tensor / no meta) error=%d\n", i, (int)tm.error());
                continue;
            }

            std::printf("    out[%zu]: dtype=%s shape=", i, scalar_type_to_cstr(tm->scalar_type()));
            print_sizes(tm->sizes());

            // Optional fields (same caveat)
            std::printf(" nbytes=%zu", (size_t)tm->nbytes());
            std::printf(" memory_planned=%s", tm->is_memory_planned() ? "true" : "false");

            const bool looks_quant =
                (tm->scalar_type() == ScalarType::Byte || tm->scalar_type() == ScalarType::Char);
            std::printf(" quant_hint=%s\n", looks_quant ? "maybe(u8/i8)" : "no");
        }
    }
}

using namespace exct;

Inference::Inference() {
    using executorch::extension::from_blob;
    using executorch::extension::module::Module;

    PrintExecuTorchModelInfo("/work/etc/models/yolox/yolox_nano.pte");
    Module module("/work/etc/models/yolox/yolox_nano.pte");

    // Example input buffer (must match model dtype/shape)
    static float input[1 * 3 * 256 * 256] = {};

    // Create input tensor view over existing memory
    auto x = from_blob(input, {1, 3, 256, 256});

    auto result = module.forward(x);
    if (!result.ok()) {
        return;
    }

    // Get first output as a Tensor, then get typed pointer
    auto outTensor = result->at(0).toTensor();
    const float *out = outTensor.const_data_ptr<float>();

    std::printf("out[0]=%f\n", out[0]);
}

Inference::~Inference() {}
