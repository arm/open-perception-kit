/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Inference.h"

#include <hailo/hailort.hpp>

#include <fmt/core.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#include <sys/mman.h>

#include "pek/Result.h"
#include "pek/String.h"

using namespace hailort;

Inference::Inference() {}

Inference::~Inference() {}

pek::Result<hailo_format_type_t> Inference::pekTypeToHailoType(pek::Tdt type) {
    switch (type) {
    case pek::Tdt::Uint8:
        return HAILO_FORMAT_TYPE_UINT8;
    case pek::Tdt::Float32:
        return HAILO_FORMAT_TYPE_FLOAT32;
    default:
        return tl::make_unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidData, "Unsupported tensor type for HailoRT"));
    }
}

pek::Result<pek::Tdt> Inference::hailoTypeToPekType(hailo_format_type_t type) {
    switch (type) {
    case HAILO_FORMAT_TYPE_UINT8:
        return pek::Tdt::Uint8;
    case HAILO_FORMAT_TYPE_FLOAT32:
        return pek::Tdt::Float32;
    default:
        return tl::make_unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidData, "Unsupported HailoRT format type"));
    }
}

Inference::Buffer Inference::allocateBuffer(size_t byteCount) {
    Buffer b;

    if (0 == byteCount) {
        b.data.reset();
        b.byteCount = 0;
        return b;
    }

    auto addr =
        mmap(nullptr, byteCount, PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (MAP_FAILED == addr)
        throw std::bad_alloc();

    b.data = std::shared_ptr<uint8_t>(reinterpret_cast<uint8_t *>(addr),
                                      [byteCount](void *addr) { munmap(addr, byteCount); });

    b.byteCount = byteCount;
    return b;
}

pek::Result<pek::Shape> Inference::hailoVstreamToPekSize(const hailo_vstream_info_t &info,
                                                         size_t batchSize) {
    pek::Shape s;

    // Hailo vstream_info_t exposes (height, width, features) only. Batch is implicit (we drive
    // batch size = 1). Treat these as (H, W, F).
    const size_t H = static_cast<size_t>(info.shape.height);
    const size_t W = static_cast<size_t>(info.shape.width);
    const size_t F = static_cast<size_t>(info.shape.features);
    const size_t B = batchSize;

    // Keep classifier-like outputs compact.
    if (H == 1 && W == 1) {
        s.dimensionCount = 2;
        s.valueCount[0] = B;
        s.valueCount[1] = static_cast<int>(F);
        return s;
    }

    // info.format.order: 1=NHWC, 11=NCHW
    if (info.format.order == HAILO_FORMAT_ORDER_NHWC) {
        s.dimensionCount = 4;
        s.valueCount[0] = B;
        s.valueCount[1] = static_cast<int>(H);
        s.valueCount[2] = static_cast<int>(W);
        s.valueCount[3] = static_cast<int>(F);
    } else if (info.format.order == HAILO_FORMAT_ORDER_NCHW) {
        s.dimensionCount = 4;
        s.valueCount[0] = B;
        s.valueCount[1] = static_cast<int>(F);
        s.valueCount[2] = static_cast<int>(H);
        s.valueCount[3] = static_cast<int>(W);
    } else if (info.format.order == HAILO_FORMAT_ORDER_HAILO_NMS_BY_CLASS) {
        s.dimensionCount = 3;
        s.valueCount[0] = B;
        s.valueCount[1] = static_cast<int>(H);
        s.valueCount[2] = static_cast<int>(W);
        s.valueCount[3] = static_cast<int>(F);
    } else {
        return tl::make_unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("Unsupported HailoRT vstream format order {}",
                                  static_cast<int>(info.format.order))));
    }

    return s;
}

pek::Result<void> Inference::setupFromJson(const std::string &filePath) {

    auto descResult = ModelDescriptor::fromFile(filePath);
    if (!descResult) {
        return tl::unexpected{descResult.error()};
    }

    { // setup model file name
        std::string modelRoot = filePath;
        if (pek::utf8::contains(modelRoot, '/')) {
            size_t lastSlashAt = pek::utf8::lastIndexOf(modelRoot, '/');
            modelRoot = pek::utf8::left(modelRoot, lastSlashAt + 1);
        } else {
            modelRoot = "";
        }
        (*descResult).modelFile = modelRoot + (*descResult).modelFile;
    }

    auto setupResult = setup(*descResult);
    if (!setupResult) {
        return tl::unexpected{setupResult.error()};
    }

    return {};
}

pek::Result<void> Inference::setup(const ModelDescriptor &modelDesc) {
    this->modelDescriptor = modelDesc;
    this->model = pek::Model();
    this->model.engine = "hailort";
    this->model.modelFamily = this->modelDescriptor.modelFamily;
    this->setupReady = false;

    try {
        hailo_vdevice_params_t params{};
        const hailo_status initStatus = hailo_init_vdevice_params(&params);
        if (HAILO_SUCCESS != initStatus) {
            return tl::make_unexpected(
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          fmt::format("hailo_init_vdevice_params failed, status {}",
                                      static_cast<int>(initStatus))));
        }
        // TODO: number of NPUs should be handled based on hw configuration detected on docker build
        // Setting up hailo VDevice to be able to handle multiple uses for the same device.
        params.device_count = 1;

        // HailoRT service mode is only valid on specific devices (for example Hailo15).
        // Keep it disabled by default and allow explicit opt-in via env var.
        const char *mpsEnv = std::getenv("PEK_HAILO_MULTI_PROCESS_SERVICE");
        const bool mpsRequested =
            (nullptr != mpsEnv) &&
            ((0 == std::strcmp(mpsEnv, "1")) || (0 == std::strcmp(mpsEnv, "true")) ||
             (0 == std::strcmp(mpsEnv, "TRUE")) || (0 == std::strcmp(mpsEnv, "yes")) ||
             (0 == std::strcmp(mpsEnv, "on")));

        params.multi_process_service = mpsRequested;
        static const char kGroupId[] = "SHARED";
        params.group_id = mpsRequested ? kGroupId : nullptr;

        auto vdeviceExp = hailort::VDevice::create(params);
        if (!vdeviceExp && params.multi_process_service &&
            (HAILO_INVALID_OPERATION == vdeviceExp.status())) {
            // Gracefully fall back when service mode is not supported by this device.
            params.multi_process_service = false;
            params.group_id = nullptr;

            auto fallbackVdeviceExp = hailort::VDevice::create(params);
            if (!fallbackVdeviceExp) {
                return tl::make_unexpected(
                    PEK_ERROR(pek::ErrorFlag::InvalidData,
                              fmt::format("Failed to create Hailo VDevice: {}",
                                          static_cast<int>(fallbackVdeviceExp.status()))));
            }
            this->vdevice = fallbackVdeviceExp.release();
        } else if (!vdeviceExp) {
            const char *hint = params.multi_process_service
                                   ? " (multi_process_service enabled; verify hailort_service is "
                                     "running and reachable or disable it via "
                                     "PEK_HAILO_MULTI_PROCESS_SERVICE=0)"
                                   : "";
            return tl::make_unexpected(PEK_ERROR(pek::ErrorFlag::InvalidData,
                                                 fmt::format("Failed to create Hailo VDevice: {}{}",
                                                             static_cast<int>(vdeviceExp.status()),
                                                             hint)));
        } else {
            this->vdevice = vdeviceExp.release();
        }

        auto inferModelExp = this->vdevice->create_infer_model(this->modelDescriptor.modelFile);
        if (!inferModelExp) {
            return tl::make_unexpected(
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          fmt::format("Failed to create infer model for HEF [{}]: {}",
                                      this->modelDescriptor.modelFile,
                                      static_cast<int>(inferModelExp.status()))));
        }
        this->inferModel = inferModelExp.release();

        // TODO: find a way to determine maximum batch size and only allow the user to set up to
        // that maximum. Setting up batch size based on configuration.
        size_t batchSize = 1;
        if (!this->modelDescriptor.inputTensors.empty() &&
            this->modelDescriptor.inputTensors[0].shape.isValid() &&
            this->modelDescriptor.inputTensors[0].shape.dimensionCount > 0) {
            int candidate = this->modelDescriptor.inputTensors[0].shape.valueCount[0];
            if (candidate > 0) {
                batchSize = static_cast<size_t>(candidate);
            }
        }
        this->inferModel->set_batch_size(batchSize);

        // Query HEF vstream infos early so we can pick sensible default format types.
        auto inputInfosExp = this->inferModel->hef().get_input_vstream_infos();
        if (!inputInfosExp) {
            return tl::make_unexpected(
                PEK_ERROR(pek::ErrorFlag::InvalidData, "Failed to query HEF input vstream infos"));
        }
        auto outputInfosExp = this->inferModel->hef().get_output_vstream_infos();
        if (!outputInfosExp) {
            return tl::make_unexpected(
                PEK_ERROR(pek::ErrorFlag::InvalidData, "Failed to query HEF output vstream infos"));
        }

        // Configure input and output tensors based on HEF defaults.
        const auto inputInfos = inputInfosExp.release();
        const auto outputInfos = outputInfosExp.release();

        auto inputNames = this->inferModel->get_input_names();
        auto outputNames = this->inferModel->get_output_names();

        for (size_t i = 0; i < inputNames.size(); ++i) {
            hailo_format_type_t fmt = HAILO_FORMAT_TYPE_UINT8;
            if (i < inputInfos.size()) {
                fmt = inputInfos[i].format.type;
            }
            this->inferModel->input(inputNames[i])->set_format_type(fmt);
        }

        for (size_t i = 0; i < outputNames.size(); ++i) {
            hailo_format_type_t fmt = HAILO_FORMAT_TYPE_UINT8;
            if (i < outputInfos.size()) {
                fmt = outputInfos[i].format.type;
            }
            this->inferModel->output(outputNames[i])->set_format_type(fmt);
        }

        auto configuredExp = this->inferModel->configure();
        if (!configuredExp) {
            return tl::make_unexpected(
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          fmt::format("Failed to configure infer model: {}",
                                      static_cast<int>(configuredExp.status()))));
        }
        this->configuredInferModel =
            std::make_unique<hailort::ConfiguredInferModel>(configuredExp.release());

        // Bind the inference request with the provided buffers in one object.
        auto bindingsExp = this->configuredInferModel->create_bindings();
        if (!bindingsExp) {
            return tl::make_unexpected(
                PEK_ERROR(pek::ErrorFlag::InvalidData, "Failed to create HailoRT bindings"));
        }
        this->bindings =
            std::make_unique<hailort::ConfiguredInferModel::Bindings>(bindingsExp.release());

        // Build pek::Model internal representation from HEF vstream infos and configurations.
        this->model.inputs.resize(inputInfos.size());
        this->model.outputs.resize(outputInfos.size());

        for (size_t i = 0; i < inputInfos.size(); ++i) {
            auto typeExp = hailoTypeToPekType(inputInfos[i].format.type);
            if (!typeExp) {
                return tl::unexpected{typeExp.error()};
            }
            auto shapeExp = hailoVstreamToPekSize(inputInfos[i], batchSize);
            if (!shapeExp) {
                return tl::unexpected{shapeExp.error()};
            }

            this->model.inputs[i].name = inputInfos[i].name;
            this->model.inputs[i].valueType = *typeExp;
            this->model.inputs[i].quantArguments.scale = inputInfos[i].quant_info.qp_scale;
            this->model.inputs[i].quantArguments.zeroPoint = inputInfos[i].quant_info.qp_zp;
            this->model.inputs[i].dataKind = pek::DataKind::ImageRgbHwc; // pek::DataKind::Unknown;
            this->model.inputs[i].shape = *shapeExp;
        }

        for (size_t i = 0; i < outputInfos.size(); ++i) {
            auto typeExp = hailoTypeToPekType(outputInfos[i].format.type);
            if (!typeExp) {
                return tl::unexpected{typeExp.error()};
            }

            auto shapeExp = hailoVstreamToPekSize(outputInfos[i], 1);
            if (!shapeExp) {
                return tl::unexpected{shapeExp.error()};
            }

            this->model.outputs[i].name = outputInfos[i].name;
            this->model.outputs[i].valueType = *typeExp;
            this->model.outputs[i].quantArguments.scale = outputInfos[i].quant_info.qp_scale;
            this->model.outputs[i].quantArguments.zeroPoint = outputInfos[i].quant_info.qp_zp;
            this->model.outputs[i].shape = *shapeExp;
        }

        // Allocate and bind I/O buffers.
        for (size_t i = 0; i < pek::MaxTensorCount; ++i) {
            inputBuffers[i] = Buffer();
            outputBuffers[i] = Buffer();
            outputTensorPointers[i] = nullptr;
            outputTensorFinalShapes[i] = pek::Shape();
        }

        for (size_t i = 0; i < inputNames.size(); ++i) {
            const auto &name = inputNames[i];
            size_t frameSize = this->inferModel->input(name)->get_frame_size();
            inputBuffers[i] = allocateBuffer(frameSize);

            auto st = this->bindings->input(name)->set_buffer(
                MemoryView(inputBuffers[i].data.get(), frameSize));
            if (HAILO_SUCCESS != st) {
                return tl::make_unexpected(
                    PEK_ERROR(pek::ErrorFlag::InvalidData,
                              fmt::format("Failed to set input buffer for [{}], status {}",
                                          name,
                                          static_cast<int>(st))));
            }
        }

        for (size_t i = 0; i < outputNames.size(); ++i) {
            const auto &name = outputNames[i];
            size_t frameSize = this->inferModel->output(name)->get_frame_size();
            outputBuffers[i] = allocateBuffer(frameSize);

            auto st = this->bindings->output(name)->set_buffer(
                MemoryView(outputBuffers[i].data.get(), frameSize));
            if (HAILO_SUCCESS != st) {
                return tl::make_unexpected(
                    PEK_ERROR(pek::ErrorFlag::InvalidData,
                              fmt::format("Failed to set output buffer for [{}], status {}",
                                          name,
                                          static_cast<int>(st))));
            }

            outputTensorPointers[i] = outputBuffers[i].data.get();
            outputTensorFinalShapes[i] = this->model.outputs[i].shape;
        }

        this->setupReady = true;
        fmt::print("HailoRT inference setup ready for model [{}]\n", modelDesc.modelFile);
    } catch (const std::exception &e) {
        return tl::make_unexpected(PEK_ERROR(pek::ErrorFlag::InvalidData,
                                             fmt::format("HailoRT setup exception: {}", e.what())));
    }

    return {};
}

pek::Result<void> Inference::inference(std::chrono::milliseconds timeout) {
    if (!setupReady || !configuredInferModel || !bindings) {
        return tl::make_unexpected(PEK_ERROR(pek::ErrorFlag::InvalidData,
                                             "HailoRT inference called before successful setup"));
    }

    hailo_status st = configuredInferModel->run(*bindings, timeout);
    if (HAILO_SUCCESS != st) {
        return tl::make_unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("HailoRT run failed, status {}", static_cast<int>(st))));
    }

    return {};
}
