/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "ValidatorInternal.h"

#include <algorithm>
#include <format>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace opk::config::detail {
namespace {

constexpr std::string_view Controller = "opk-std-ops/InferenceController";
constexpr std::string_view Preprocess = "opk-std-ops/GenericImagePreprocess";
constexpr std::string_view Postprocess = "opk-std-ops/GenericPostprocess";
constexpr std::string_view PythonScript = "opk-python-ops/PythonScript";
using Ops = std::vector<opk::op::OpChainDescriptor::Op>;

bool isBuiltInStageOp(std::string_view id) {
    return id == Controller || id == Preprocess || id == Postprocess ||
           opk::op::isInferenceOpId(id);
}

bool isTerminalPostprocess(std::string_view id) {
    return id == Postprocess || id == PythonScript;
}

std::optional<std::string> stringAttribute(const opk::AttributeMap &attributes,
                                           std::string_view key) {
    if (const auto value = attributes.raw().find(std::string(key));
        value != attributes.raw().end() && value->second.isString())
        return value->second.asString();
    return std::nullopt;
}

std::optional<double> numberAttribute(const opk::AttributeMap &attributes, std::string_view key) {
    if (const auto value = attributes.raw().find(std::string(key));
        value != attributes.raw().end() && (value->second.isInt() || value->second.isDouble()))
        return value->second.asDouble();
    return std::nullopt;
}

void addStageIssue(ValidationReport &report,
                   std::string_view source,
                   std::size_t index,
                   std::string message) {
    report.issues.push_back(makeIssue("opchain.v1.builtin-stage",
                                      ValidationPhase::Descriptor,
                                      source,
                                      std::format("/ops/{}", index),
                                      std::move(message)));
}

void validateControlCharacters(ValidationReport &report,
                               const opk::op::OpChainDescriptor &descriptor,
                               std::string_view source) {
    validateControlFreeString(report, descriptor.name, source, "/name", "common.v1.name-control");
    for (std::size_t index = 0; index < descriptor.ops.size(); ++index) {
        const auto &op = descriptor.ops[index];
        const std::string base = std::format("/ops/{}", index);
        validateControlFreeString(report, op.id, source, base + "/id", "opchain.v1.op-id-control");
        if (opk::op::isInferenceOpId(op.id)) {
            const auto modelDescriptor = stringAttribute(op.attributes, "modelDescriptor");
            if (modelDescriptor.has_value()) {
                validateControlFreeString(report,
                                          *modelDescriptor,
                                          source,
                                          base + "/attributes/modelDescriptor",
                                          "opchain.v1.model-descriptor-control");
            }
        }
    }
}

void validateLoopGroups(ValidationReport &report,
                        const opk::op::OpChainDescriptor &descriptor,
                        std::string_view source) {
    struct Run {
        std::size_t first;
        std::size_t count;
    };

    std::unordered_map<std::size_t, Run> runs;
    std::unordered_set<std::size_t> closed;
    std::optional<std::size_t> previous;

    for (std::size_t index = 0; index < descriptor.ops.size(); ++index) {
        std::optional<std::size_t> loopId = descriptor.ops[index].loopId;
        if (loopId.has_value() && *loopId == 0) {
            report.issues.push_back(makeIssue("opchain.v1.loop-group",
                                              ValidationPhase::Descriptor,
                                              source,
                                              std::format("/ops/{}/loopId", index),
                                              "loop ID must be positive when present"));
            loopId.reset();
        }

        if (loopId != previous && previous.has_value())
            closed.insert(*previous);

        if (loopId.has_value()) {
            auto run = runs.try_emplace(*loopId, Run{index, 0}).first;
            if (closed.contains(*loopId) && loopId != previous) {
                report.issues.push_back(
                    makeIssue("opchain.v1.loop-group",
                              ValidationPhase::Descriptor,
                              source,
                              std::format("/ops/{}/loopId", index),
                              "loop ID is reused after its contiguous group ended",
                              std::format("/ops/{}/loopId", run->second.first)));
            }
            ++run->second.count;
        }
        previous = loopId;
    }

    for (const auto &[loopId, run] : runs) {
        if (run.count >= 2)
            continue;
        report.issues.push_back(makeIssue("opchain.v1.loop-group",
                                          ValidationPhase::Descriptor,
                                          source,
                                          std::format("/ops/{}/loopId", run.first),
                                          std::format("loop group {} must contain at least two "
                                                      "operations",
                                                      loopId)));
    }
}

void validateInstanceIds(ValidationReport &report,
                         const opk::op::OpChainDescriptor &descriptor,
                         std::string_view source) {
    std::unordered_map<std::string, std::size_t> firstById;
    std::unordered_map<std::string, std::size_t> occurrences;
    for (std::size_t index = 0; index < descriptor.ops.size(); ++index) {
        const auto &op = descriptor.ops[index];
        auto &occurrenceCount = occurrences[op.id];
        const std::size_t occurrence = occurrenceCount;
        ++occurrenceCount;
        const std::string instanceId = op.instanceId.empty()
                                           ? opk::op::makeDefaultInstanceId(op.id, occurrence)
                                           : op.instanceId;
        const auto [iterator, inserted] = firstById.try_emplace(instanceId, index);
        if (inserted)
            continue;
        report.issues.push_back(
            makeIssue("opchain.v1.instance-id",
                      ValidationPhase::Descriptor,
                      source,
                      std::format("/ops/{}/instanceId", index),
                      std::format("operation instance ID '{}' is duplicated", instanceId),
                      std::format("/ops/{}/instanceId", iterator->second)));
    }
}

void validateStageStart(ValidationReport &report,
                        const Ops &ops,
                        std::size_t controller,
                        std::string_view source) {
    const auto loopId = ops[controller].loopId;
    if (loopId.has_value() && *loopId != 0 && controller > 0 &&
        ops[controller - 1].loopId == loopId) {
        report.issues.push_back(
            makeIssue("opchain.v1.stage-loop",
                      ValidationPhase::Descriptor,
                      source,
                      std::format("/ops/{}/loopId", controller),
                      "InferenceController must be the first operation in its loop group",
                      std::format("/ops/{}/loopId", controller - 1)));
    }
}

bool validateStageShape(ValidationReport &report,
                        const Ops &ops,
                        std::size_t controller,
                        std::size_t end,
                        std::string_view source) {
    if (controller + 3 >= end) {
        addStageIssue(report, source, controller, "built-in stage is incomplete");
        return false;
    }
    if (ops[controller + 1].id != Preprocess) {
        addStageIssue(
            report, source, controller + 1, "built-in stage must begin with preprocessing");
    }
    if (!opk::op::isInferenceOpId(ops[controller + 2].id)) {
        addStageIssue(report,
                      source,
                      controller + 2,
                      "preprocessing must be immediately followed by backend inference");
    }
    if (!isTerminalPostprocess(ops[end - 1].id)) {
        addStageIssue(report,
                      source,
                      end - 1,
                      "built-in stage must end with GenericPostprocess or PythonScript");
    }

    for (std::size_t index = controller + 3; index + 1 < end; ++index) {
        if (isBuiltInStageOp(ops[index].id)) {
            addStageIssue(report,
                          source,
                          index,
                          "only custom operations may occur between inference and postprocess");
        }
    }

    const auto first = ops.begin() + static_cast<std::ptrdiff_t>(controller + 1);
    const auto last = ops.begin() + static_cast<std::ptrdiff_t>(end);
    if (const auto count =
            [first, last](const auto &predicate) { return std::count_if(first, last, predicate); };
        count([](const auto &op) { return op.id == Preprocess; }) != 1 ||
        count([](const auto &op) { return opk::op::isInferenceOpId(op.id); }) != 1 ||
        count([](const auto &op) { return op.id == Postprocess; }) !=
            (ops[end - 1].id == Postprocess ? 1 : 0)) {
        addStageIssue(report,
                      source,
                      controller,
                      "built-in stage must contain exactly one preprocess and inference "
                      "operation followed by one terminal postprocess operation");
    }
    return true;
}

void validateStageLoop(ValidationReport &report,
                       const Ops &ops,
                       std::size_t controller,
                       std::size_t end,
                       std::string_view source) {
    if (const bool stageUsesLoop =
            std::any_of(ops.begin() + static_cast<std::ptrdiff_t>(controller),
                        ops.begin() + static_cast<std::ptrdiff_t>(end),
                        [](const auto &op) { return op.loopId.value_or(0) != 0; });
        !stageUsesLoop &&
        stringAttribute(ops[controller].attributes, "contentType").value_or("").empty()) {
        return;
    }

    const auto loopId = ops[controller].loopId;
    for (std::size_t index = controller; index < end; ++index) {
        if (loopId.has_value() && *loopId != 0 && ops[index].loopId == loopId)
            continue;
        report.issues.push_back(makeIssue(
            "opchain.v1.stage-loop",
            ValidationPhase::Descriptor,
            source,
            std::format("/ops/{}/loopId", index),
            "every operation in a looped built-in stage must share the controller's nonzero "
            "loop ID",
            std::format("/ops/{}/loopId", controller)));
    }
}

void validateStages(ValidationReport &report,
                    const opk::op::OpChainDescriptor &descriptor,
                    std::string_view source) {
    const auto &ops = descriptor.ops;
    if (!ops.empty() && ops.back().id == Preprocess)
        addStageIssue(
            report, source, ops.size() - 1, "GenericImagePreprocess must have a successor");

    for (std::size_t controller = 0; controller < ops.size(); ++controller) {
        if (ops[controller].id != Controller)
            continue;

        validateStageStart(report, ops, controller, source);
        const auto nextController =
            std::find_if(ops.begin() + static_cast<std::ptrdiff_t>(controller + 1),
                         ops.end(),
                         [](const auto &op) { return op.id == Controller; });
        const auto end = static_cast<std::size_t>(std::distance(ops.begin(), nextController));
        if (!validateStageShape(report, ops, controller, end, source))
            continue;
        validateStageLoop(report, ops, controller, end, source);
    }
}

void validateThresholds(ValidationReport &report,
                        const opk::op::OpChainDescriptor &descriptor,
                        std::string_view source) {
    for (std::size_t index = 0; index < descriptor.ops.size(); ++index) {
        const auto &op = descriptor.ops[index];
        if (op.id != Postprocess)
            continue;

        const auto parser = stringAttribute(op.attributes, "parser");
        if (!parser.has_value())
            continue;

        double defaultLow;
        double defaultHigh;
        if (*parser == "ModNetSegmentationParser") {
            defaultLow = 0.2;
            defaultHigh = 0.8;
        } else if (*parser == "PaddleOcrDetectionParser") {
            defaultLow = 0.6;
            defaultHigh = 0.8;
        } else {
            continue;
        }

        const double low = numberAttribute(op.attributes, "thresholdLow").value_or(defaultLow);
        if (const double high =
                numberAttribute(op.attributes, "thresholdHigh").value_or(defaultHigh);
            low < high) {
            continue;
        }

        report.issues.push_back(
            makeIssue("opchain.v1.threshold-order",
                      ValidationPhase::Descriptor,
                      source,
                      std::format("/ops/{}/attributes/thresholdHigh", index),
                      "effective thresholdLow must be lower than effective thresholdHigh",
                      std::format("/ops/{}/attributes/thresholdLow", index)));
    }
}

} // namespace

ValidationReport validateOpChainV1(const opk::op::OpChainDescriptor &descriptor,
                                   std::string_view source) {
    ValidationReport report;
    validateControlCharacters(report, descriptor, source);
    validateInstanceIds(report, descriptor, source);
    validateLoopGroups(report, descriptor, source);
    validateStages(report, descriptor, source);
    validateThresholds(report, descriptor, source);
    report.sort();
    return report;
}

} // namespace opk::config::detail
