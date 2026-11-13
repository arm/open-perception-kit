/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "preproc/GenericAudioTensorBuilder.h"
#include "preproc/CpuLogMelSpectrogram.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <variant>

namespace opk::stdop::preproc {
namespace {

class PcmCopy {
  public:
    PcmCopy(const size_t sampleCount, const size_t contextSampleCount)
        : sampleCount_(sampleCount), context_(contextSampleCount, 0.0f) {}

    size_t getRequiredAudioSamples() const {
        return sampleCount_ - context_.size();
    }

    size_t getOutputCount() const {
        return sampleCount_;
    }

    bool process(const PcmRingBuffer::View &pcm,
                 const std::span<uint8_t> destination,
                 const size_t hopSampleCount,
                 const bool discontinuity) {
        if (!destination.data() || destination.size() != sampleCount_ * sizeof(float) ||
            pcm.size() != getRequiredAudioSamples())
            return false;
        const auto first = pcm.first();
        const auto second = pcm.second();
        const auto finite = [](const float sample) { return std::isfinite(sample); };
        if (!std::all_of(first.begin(), first.end(), finite) ||
            !std::all_of(second.begin(), second.end(), finite))
            return false;
        if (!context_.empty()) {
            if (hopSampleCount != getRequiredAudioSamples() ||
                (!discontinuity && nextPosition_ && *nextPosition_ != pcm.beginPosition()))
                return false;
            if (discontinuity)
                std::fill(context_.begin(), context_.end(), 0.0f);
            std::memcpy(destination.data(), context_.data(), context_.size() * sizeof(float));
        }
        // Backend tensor storage need not be float-aligned.
        auto *const fresh = destination.data() + context_.size() * sizeof(float);
        if (!first.empty())
            std::memcpy(fresh, first.data(), first.size_bytes());
        if (!second.empty())
            std::memcpy(fresh + first.size_bytes(), second.data(), second.size_bytes());
        if (!context_.empty()) {
            std::memcpy(context_.data(),
                        destination.data() + (sampleCount_ - context_.size()) * sizeof(float),
                        context_.size() * sizeof(float));
            nextPosition_ = pcm.endPosition();
        }
        return true;
    }

  private:
    size_t sampleCount_;
    std::vector<float> context_;
    std::optional<uint64_t> nextPosition_;
};

auto invalidConfiguration(const char *const message) {
    return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain, message));
}

bool isNumber(const opk::AttributeMap &attributes, const char *const name) {
    return attributes.contains(name) &&
           (attributes.at(name).isInt() || attributes.at(name).isDouble());
}

bool readCount(const opk::AttributeMap &attributes, const char *const name, size_t &value) {
    if (!attributes.contains(name) || !attributes.at(name).isInt())
        return false;
    const auto count = attributes.getInt(name);
    if (count < 0 || static_cast<uint64_t>(count) > std::numeric_limits<size_t>::max())
        return false;
    value = static_cast<size_t>(count);
    return true;
}

opk::Result<decltype(LogMelSpectrogramConfig::normalization)>
readNormalization(const opk::AttributeMap &attributes) {
    const auto normalization = attributes.getObjectOrNull("normalization");
    if (!normalization || !normalization->contains("kind") || !normalization->at("kind").isString())
        return invalidConfiguration("logMel normalization requires an object with a kind");
    const auto &kind = normalization->getString("kind");
    if (kind == "none") {
        if (normalization->raw().size() != 1)
            return invalidConfiguration("Unnormalized logMel has no normalization parameters");
        return std::monostate{};
    }
    if (kind == "window") {
        if (normalization->raw().size() != 2 || !isNumber(*normalization, "epsilon"))
            return invalidConfiguration("Window normalization requires only epsilon");
        return LogMelSpectrogramConfig::WindowNormalization{normalization->getDouble("epsilon")};
    }
    if (kind == "rolling") {
        size_t historyFrames = 0;
        if (normalization->raw().size() != 3 || !isNumber(*normalization, "epsilon") ||
            !readCount(*normalization, "historyFrames", historyFrames))
            return invalidConfiguration(
                "Rolling normalization requires epsilon and integer historyFrames");
        return LogMelSpectrogramConfig::RollingNormalization{normalization->getDouble("epsilon"),
                                                             historyFrames};
    }
    return invalidConfiguration("Unknown logMel normalization kind");
}

opk::Result<LogMelSpectrogramConfig> readLogMelConfig(const opk::AttributeMap &attributes) {
    const auto config = attributes.getObjectOrNull("logMel");
    if (!config || config->raw().size() != 9 || attributes.contains("contextSampleCount"))
        return invalidConfiguration(
            "logMel requires its complete frontend configuration without PCM context");
    LogMelSpectrogramConfig frontend{};
    if (!readCount(*config, "windowSampleCount", frontend.windowSampleCount) ||
        !readCount(*config, "fftSize", frontend.fftSize) ||
        !readCount(*config, "windowLengthSamples", frontend.windowLengthSamples) ||
        !readCount(*config, "frameHopSamples", frontend.frameHopSamples) ||
        !isNumber(*config, "minFrequencyHz") || !isNumber(*config, "maxFrequencyHz") ||
        !isNumber(*config, "logOffset") || !config->contains("layout") ||
        !config->at("layout").isString())
        return invalidConfiguration(
            "logMel requires integer sample counts, numeric frequencies/offset and layout");
    frontend.minFrequencyHz = config->getDouble("minFrequencyHz");
    frontend.maxFrequencyHz = config->getDouble("maxFrequencyHz");
    const double offset = config->getDouble("logOffset");
    if (!std::isfinite(offset) || offset <= 0 || offset > std::numeric_limits<float>::max())
        return invalidConfiguration(
            "logMel logOffset must be positive and representable as Float32");
    frontend.logOffset = static_cast<float>(offset);
    const auto &layout = config->getString("layout");
    if (layout != "time-major" && layout != "mel-major")
        return invalidConfiguration("logMel layout must be time-major or mel-major");
    frontend.layout = layout == "time-major" ? LogMelSpectrogramConfig::Layout::TimeMajor
                                             : LogMelSpectrogramConfig::Layout::MelMajor;
    const auto normalization = readNormalization(*config);
    if (!normalization)
        return tl::unexpected(normalization.error());
    frontend.normalization = *normalization;
    if (!frontend.isValid())
        return invalidConfiguration("Invalid logMel frontend configuration");
    return frontend;
}

opk::Result<size_t> readPcmContext(const opk::AttributeMap &attributes) {
    if (attributes.contains("contextSampleCount") && !attributes.at("contextSampleCount").isInt())
        return invalidConfiguration("Audio context sample count must be an integer");
    const auto count = attributes.getIntOrDefault("contextSampleCount", 0);
    if (count < 0 || count > 2147483647)
        return invalidConfiguration("Invalid audio context sample count");
    return static_cast<size_t>(count);
}

opk::Result<void> validateTensor(const opk::ModelInput &input,
                                 const opk::DataKind expectedKind,
                                 const size_t featureDimensions) {
    const auto &shape = input.shape;
    if (input.dataKind != expectedKind || input.valueType != opk::Dtype::Float32 ||
        shape.rank < featureDimensions || shape.rank > 8 ||
        input.matchShapeOutputIndex != opk::InvalidTensorIndex)
        return invalidConfiguration("Audio preprocessing requires a fixed Float32 audio tensor "
                                    "and logMel attributes only for AudioLogMel");
    for (size_t i = 0; i < shape.rank; ++i) {
        if (shape.dims[i] <= 0 || (i + featureDimensions < shape.rank && shape.dims[i] != 1))
            return invalidConfiguration("Audio tensors require positive feature dimensions and "
                                        "singleton leading dimensions");
    }
    return {};
}

struct PcmConversion {
    size_t contextSampleCount;
    std::unique_ptr<PcmCopy> kernel{};

    opk::Result<void> bind(const opk::ModelInput &input) {
        if (const auto valid = validateTensor(input, opk::DataKind::AudioPcm, 1); !valid)
            return valid;
        const auto samples = static_cast<size_t>(input.shape.dims[input.shape.rank - 1]);
        if (contextSampleCount >= samples || contextSampleCount > samples - contextSampleCount)
            return invalidConfiguration("Audio context must fit within the fresh sample window");
        kernel = std::make_unique<PcmCopy>(samples, contextSampleCount);
        return {};
    }
};

struct LogMelConversion {
    LogMelSpectrogramConfig config;
    std::unique_ptr<CpuLogMelSpectrogram> kernel{};

    opk::Result<void> bind(const opk::ModelInput &input) {
        if (const auto valid = validateTensor(input, opk::DataKind::AudioLogMel, 2); !valid)
            return valid;
        const auto &shape = input.shape;
        const bool timeMajor = config.layout == LogMelSpectrogramConfig::Layout::TimeMajor;
        const auto melBins = static_cast<size_t>(shape.dims[shape.rank - (timeMajor ? 1 : 2)]);
        const auto frames = static_cast<size_t>(shape.dims[shape.rank - (timeMajor ? 2 : 1)]);
        auto candidate = std::make_unique<CpuLogMelSpectrogram>();
        if (!candidate->configure(config, melBins))
            return invalidConfiguration("Unsupported AudioLogMel bins or frontend configuration");
        if (frames != candidate->getOutputCount() / melBins)
            return invalidConfiguration("AudioLogMel tensor frames disagree with its PCM window");
        kernel = std::move(candidate);
        return {};
    }
};

} // namespace

struct GenericAudioTensorBuilder::Impl {
    // Each alternative owns the configuration and kernel for one conversion contract.
    std::optional<std::variant<PcmConversion, LogMelConversion>> conversion{
        PcmConversion{.contextSampleCount = 0}};
};

GenericAudioTensorBuilder::GenericAudioTensorBuilder() : impl_(std::make_unique<Impl>()) {}
GenericAudioTensorBuilder::~GenericAudioTensorBuilder() = default;

void GenericAudioTensorBuilder::resetBinding() {
    if (impl_->conversion)
        std::visit([](auto &conversion) { conversion.kernel.reset(); }, *impl_->conversion);
}

opk::Result<void> GenericAudioTensorBuilder::configure(const opk::AttributeMap &attributes) {
    impl_->conversion.reset();
    if (attributes.contains("logMel")) {
        const auto config = readLogMelConfig(attributes);
        if (!config)
            return tl::unexpected(config.error());
        impl_->conversion.emplace(LogMelConversion{.config = *config});
    } else {
        const auto context = readPcmContext(attributes);
        if (!context)
            return tl::unexpected(context.error());
        impl_->conversion.emplace(PcmConversion{.contextSampleCount = *context});
    }
    return {};
}

opk::Result<void> GenericAudioTensorBuilder::bind(const opk::ModelInput &input) {
    resetBinding();
    if (!impl_->conversion)
        return invalidConfiguration("Audio tensor builder has no valid configuration");
    return std::visit([&input](auto &conversion) { return conversion.bind(input); },
                      *impl_->conversion);
}

size_t GenericAudioTensorBuilder::getRequiredAudioSamples() const {
    if (!impl_->conversion)
        return 0;
    return std::visit(
        [](const auto &conversion) {
            return conversion.kernel ? conversion.kernel->getRequiredAudioSamples() : size_t{0};
        },
        *impl_->conversion);
}

opk::Result<void> GenericAudioTensorBuilder::build(const PcmRingBuffer::View &pcm,
                                                   uint8_t *const destination,
                                                   const size_t hopSampleCount,
                                                   const bool discontinuity) {
    const bool success =
        destination && impl_->conversion &&
        std::visit(
            [&](auto &conversion) {
                return conversion.kernel &&
                       conversion.kernel->process(
                           pcm,
                           {destination, conversion.kernel->getOutputCount() * sizeof(float)},
                           hopSampleCount,
                           discontinuity);
            },
            *impl_->conversion);
    if (!success)
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "Audio preprocessing requires a complete finite PCM "
                                        "window, writable tensor and valid scheduled cadence"));
    return {};
}

} // namespace opk::stdop::preproc
