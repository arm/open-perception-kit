/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Tracker.h"

#include "algo/Hungarian.h"
#include "algo/IoU.h"

#include <algorithm>
#include <cmath>
#include <fmt/core.h>

namespace amp::tracker {

static float cosineSimilarity(const std::vector<float> &a, const std::vector<float> &b) {
    if (a.empty() || b.empty() || a.size() != b.size()) {
        return 0.0f;
    }

    float dot = 0.0f;
    float normA = 0.0f;
    float normB = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }

    if (normA <= 1e-12f || normB <= 1e-12f) {
        return 0.0f;
    }

    return dot / (std::sqrt(normA) * std::sqrt(normB));
}

static bool isValidEmbedding(const std::vector<float> &embedding) {
    if (embedding.empty()) {
        return false;
    }

    float normSq = 0.0f;
    for (const float value : embedding) {
        if (!std::isfinite(value)) {
            return false;
        }
        normSq += value * value;
    }

    return normSq > 1e-12f;
}

void Processor::reset() {
    activeTracks.clear();
    dormantTracks.clear();
    nextTrackId = 1;
    frameCounter = 0;
}

void Processor::pruneDormantTracks(const Config &config) {
    if (dormantTracks.empty()) {
        return;
    }

    const float dt = std::max(config.kalmanDt, 1e-4f);
    const uint64_t maxDormantFrames = static_cast<uint64_t>(
        std::max(1.0f, std::ceil(std::max(config.dormantTrackHistorySeconds, 0.0f) / dt)));

    std::vector<uint64_t> toErase;
    toErase.reserve(dormantTracks.size());
    for (const auto &[trackId, dormant] : dormantTracks) {
        if ((frameCounter - dormant.storedAtFrame) > maxDormantFrames) {
            toErase.push_back(trackId);
        }
    }

    for (const auto trackId : toErase) {
        dormantTracks.erase(trackId);
    }
}

amp::Perception::TrackTrace::Point Processor::predictCenterPoint(Track &track,
                                                                 const Config &config) {
    using StateVector = TrackKalman::StateVector;
    using StateMatrix = TrackKalman::StateMatrix;

    if (!track.kalmanInitialized) {
        const float centerX = track.lastDetection.x + (track.lastDetection.width * 0.5f);
        const float centerY = track.lastDetection.y + (track.lastDetection.height * 0.5f);

        StateVector initialState{};
        initialState[0][0] = centerX;
        initialState[1][0] = centerY;
        initialState[2][0] = 0.0f;
        initialState[3][0] = 0.0f;

        StateMatrix initialCovariance{};
        initialCovariance[0][0] = config.kalmanInitialCovariancePos;
        initialCovariance[1][1] = config.kalmanInitialCovariancePos;
        initialCovariance[2][2] = config.kalmanInitialCovarianceVel;
        initialCovariance[3][3] = config.kalmanInitialCovarianceVel;

        track.kalman.setState(initialState);
        track.kalman.setCovariance(initialCovariance);
        track.kalmanInitialized = true;
        return {centerX, centerY};
    }

    StateMatrix transition{};
    transition[0][0] = 1.0f;
    transition[0][2] = config.kalmanDt;
    transition[1][1] = 1.0f;
    transition[1][3] = config.kalmanDt;
    transition[2][2] = 1.0f;
    transition[3][3] = 1.0f;

    StateMatrix processNoise{};
    processNoise[0][0] = config.kalmanProcessNoisePos;
    processNoise[1][1] = config.kalmanProcessNoisePos;
    processNoise[2][2] = config.kalmanProcessNoiseVel;
    processNoise[3][3] = config.kalmanProcessNoiseVel;

    track.kalman.predict(transition, processNoise);

    const auto &state = track.kalman.state();
    return {state[0][0], state[1][0]};
}

amp::Perception::TrackTrace::Point Processor::updateCenterPointWithMeasurement(
    Track &track, const amp::Perception::Rect &detection, const Config &config) {
    using MeasurementVector = TrackKalman::MeasurementVector;
    using MeasurementMatrix = TrackKalman::MeasurementMatrix;
    using ObservationMatrix = TrackKalman::ObservationMatrix;

    if (!track.predictedThisFrame) {
        predictCenterPoint(track, config);
        track.predictedThisFrame = true;
    }

    const float measX = detection.x + (detection.width * 0.5f);
    const float measY = detection.y + (detection.height * 0.5f);

    MeasurementVector measurement{};
    measurement[0][0] = measX;
    measurement[1][0] = measY;

    ObservationMatrix observation{};
    observation[0][0] = 1.0f;
    observation[1][1] = 1.0f;

    MeasurementMatrix measurementNoise{};
    measurementNoise[0][0] = config.kalmanMeasurementNoisePos;
    measurementNoise[1][1] = config.kalmanMeasurementNoisePos;

    track.kalman.update(measurement, observation, measurementNoise);

    const auto &state = track.kalman.state();
    return {state[0][0], state[1][0]};
}

void Processor::appendTracePoint(Track &track,
                                 const amp::Perception::TrackTrace::Point &point,
                                 const Config &config) {
    track.tracePoints.push_back(point);

    int historyPoints = 1;
    if (config.traceHistorySeconds > 0.0f && config.kalmanDt > 0.0f) {
        historyPoints = static_cast<int>(std::ceil(config.traceHistorySeconds / config.kalmanDt));
    }

    const auto maxHistorySize = static_cast<size_t>(std::max(1, historyPoints));
    while (track.tracePoints.size() > maxHistorySize) {
        track.tracePoints.pop_front();
    }
}

void Processor::matchDetectionsToTracks(
    const std::vector<amp::Perception::Rect> &detections,
    const std::map<size_t, std::vector<float>> &detectionEmbeddings,
    std::vector<std::pair<size_t, uint64_t>> &matches,
    std::map<size_t, std::string> &matchDiagnosticsByDetection,
    std::vector<size_t> &unmatchedDetections,
    const Config &config) {
    matches.clear();
    matchDiagnosticsByDetection.clear();
    unmatchedDetections.clear();

    if (detections.empty()) {
        return;
    }

    if (activeTracks.empty()) {
        unmatchedDetections.resize(detections.size());
        for (size_t detIdx = 0; detIdx < detections.size(); ++detIdx) {
            unmatchedDetections[detIdx] = detIdx;
        }
        return;
    }

    std::vector<uint64_t> trackIds;
    trackIds.reserve(activeTracks.size());
    for (const auto &[trackId, track] : activeTracks) {
        (void)track;
        trackIds.push_back(trackId);
    }

    std::vector<amp::Perception::Rect> predictedTrackBoxes(trackIds.size());
    for (size_t trackIdx = 0; trackIdx < trackIds.size(); ++trackIdx) {
        auto trackIt = activeTracks.find(trackIds[trackIdx]);
        if (trackIt == activeTracks.end()) {
            continue;
        }

        auto &track = trackIt->second;
        const auto predictedPoint = predictCenterPoint(track, config);
        track.predictedThisFrame = true;

        auto pred = track.lastDetection;
        pred.x = predictedPoint.x - (pred.width * 0.5f);
        pred.y = predictedPoint.y - (pred.height * 0.5f);
        predictedTrackBoxes[trackIdx] = pred;
    }

    std::vector<std::vector<float>> iouMatrix(detections.size(),
                                              std::vector<float>(trackIds.size(), 0.0f));
    std::vector<std::vector<float>> costMatrix(detections.size(),
                                               std::vector<float>(trackIds.size(), 1.0f));

    for (size_t detIdx = 0; detIdx < detections.size(); ++detIdx) {
        const auto &det = detections[detIdx];
        for (size_t trackIdx = 0; trackIdx < trackIds.size(); ++trackIdx) {
            const float iou = amp::algo::computeIoU(det, predictedTrackBoxes[trackIdx]);
            iouMatrix[detIdx][trackIdx] = iou;

            float cost = 1.0f - iou;

            if (config.useEmbeddings && config.embeddingWeight > 0.0f) {
                const auto detEmbIt = detectionEmbeddings.find(detIdx);
                auto trackIt = activeTracks.find(trackIds[trackIdx]);

                if (detEmbIt != detectionEmbeddings.end() && trackIt != activeTracks.end() &&
                    trackIt->second.hasEmbedding) {
                    const float similarity =
                        cosineSimilarity(detEmbIt->second, trackIt->second.lastEmbedding);

                    if (similarity >= config.minCosineSimilarity) {
                        const float embeddingCost = 1.0f - ((similarity + 1.0f) * 0.5f);
                        const float w = std::clamp(config.embeddingWeight, 0.0f, 1.0f);
                        cost = ((1.0f - w) * cost) + (w * embeddingCost);
                    }
                }
            }

            costMatrix[detIdx][trackIdx] = cost;
        }
    }

    const auto assignment = amp::algo::solveHungarian(costMatrix);
    std::vector<bool> matchedDetection(detections.size(), false);

    for (size_t detIdx = 0; detIdx < assignment.size() && detIdx < detections.size(); ++detIdx) {
        const int trackIdx = assignment[detIdx];
        if (trackIdx < 0 || static_cast<size_t>(trackIdx) >= trackIds.size()) {
            continue;
        }

        const float iou = iouMatrix[detIdx][static_cast<size_t>(trackIdx)];
        if (iou >= config.iouThreshold) {
            const uint64_t matchedTrackId = trackIds[static_cast<size_t>(trackIdx)];
            matches.push_back({detIdx, matchedTrackId});

            std::string diagnostic = fmt::format("IOU:{:.2f}", iou);
            if (config.useEmbeddings && config.embeddingWeight > 0.0f) {
                const auto detEmbIt = detectionEmbeddings.find(detIdx);
                const auto trackIt = activeTracks.find(matchedTrackId);
                if (detEmbIt != detectionEmbeddings.end() && trackIt != activeTracks.end() &&
                    trackIt->second.hasEmbedding) {
                    const float similarity =
                        cosineSimilarity(detEmbIt->second, trackIt->second.lastEmbedding);
                    if (similarity >= config.minCosineSimilarity) {
                        diagnostic = fmt::format("REID:{:.2f}", similarity);
                    }
                }
            }

            matchDiagnosticsByDetection[detIdx] = diagnostic;
            matchedDetection[detIdx] = true;
        }
    }

    for (size_t detIdx = 0; detIdx < detections.size(); ++detIdx) {
        if (!matchedDetection[detIdx]) {
            unmatchedDetections.push_back(detIdx);
        }
    }
}

void Processor::updateTracks(const std::vector<amp::Perception::Rect> &detections,
                             const std::map<size_t, std::vector<float>> &detectionEmbeddings,
                             const std::vector<std::pair<size_t, uint64_t>> &matches,
                             const std::map<size_t, std::string> &matchDiagnosticsByDetection,
                             const std::vector<size_t> &unmatchedDetections,
                             std::map<size_t, uint64_t> &assignedTrackByDetection,
                             std::vector<uint64_t> &predictedOnlyTrackIds,
                             const Config &config) {
    assignedTrackByDetection.clear();
    predictedOnlyTrackIds.clear();

    for (const auto &[detIdx, trackId] : matches) {
        auto &track = activeTracks[trackId];
        track.lastDetection = detections[detIdx];
        track.missedFrames = 0;
        track.hitStreak++;
        track.lastUpdateFrame = frameCounter;

        const auto smoothedPoint =
            updateCenterPointWithMeasurement(track, track.lastDetection, config);
        appendTracePoint(track, smoothedPoint, config);

        const auto diagIt = matchDiagnosticsByDetection.find(detIdx);
        if (diagIt != matchDiagnosticsByDetection.end()) {
            track.lastMatchDiagnostic = diagIt->second;
        } else {
            track.lastMatchDiagnostic = "IOU:N/A";
        }

        const auto embeddingIt = detectionEmbeddings.find(detIdx);
        if (embeddingIt != detectionEmbeddings.end() && isValidEmbedding(embeddingIt->second)) {
            track.lastEmbedding = embeddingIt->second;
            track.hasEmbedding = true;
        }

        track.lastDetection.x = smoothedPoint.x - (track.lastDetection.width * 0.5f);
        track.lastDetection.y = smoothedPoint.y - (track.lastDetection.height * 0.5f);
        assignedTrackByDetection[detIdx] = trackId;
    }

    for (size_t detIdx : unmatchedDetections) {
        if (config.useEmbeddings && config.reidReassociateThreshold > 0.0f) {
            const auto embeddingIt = detectionEmbeddings.find(detIdx);

            if (embeddingIt != detectionEmbeddings.end() && isValidEmbedding(embeddingIt->second) &&
                !dormantTracks.empty()) {
                float bestSimilarity = -1.0f;
                uint64_t bestDormantTrackId = 0;

                for (const auto &[dormantTrackId, dormant] : dormantTracks) {
                    if (!isValidEmbedding(dormant.lastEmbedding)) {
                        continue;
                    }

                    const float similarity =
                        cosineSimilarity(embeddingIt->second, dormant.lastEmbedding);
                    if (similarity > bestSimilarity) {
                        bestSimilarity = similarity;
                        bestDormantTrackId = dormantTrackId;
                    }
                }

                if (bestDormantTrackId > 0 && bestSimilarity >= config.reidReassociateThreshold) {
                    const auto dormantIt = dormantTracks.find(bestDormantTrackId);
                    if (dormantIt != dormantTracks.end()) {
                        Track restoredTrack;
                        restoredTrack.trackId = bestDormantTrackId;
                        restoredTrack.lastDetection = detections[detIdx];
                        restoredTrack.lastMatchDiagnostic =
                            fmt::format("REID-R:{:.2f}", bestSimilarity);
                        restoredTrack.lastEmbedding = embeddingIt->second;
                        restoredTrack.hasEmbedding = true;
                        restoredTrack.missedFrames = 0;
                        restoredTrack.hitStreak = std::max(1, config.minHitsToConfirm);
                        restoredTrack.lastUpdateFrame = frameCounter;

                        const auto initPoint = predictCenterPoint(restoredTrack, config);
                        restoredTrack.predictedThisFrame = true;
                        appendTracePoint(restoredTrack, initPoint, config);
                        restoredTrack.lastDetection.x =
                            initPoint.x - (restoredTrack.lastDetection.width * 0.5f);
                        restoredTrack.lastDetection.y =
                            initPoint.y - (restoredTrack.lastDetection.height * 0.5f);

                        activeTracks[bestDormantTrackId] = std::move(restoredTrack);
                        assignedTrackByDetection[detIdx] = bestDormantTrackId;
                        nextTrackId = std::max(nextTrackId, bestDormantTrackId + 1);
                        dormantTracks.erase(dormantIt);
                        continue;
                    }
                }
            }
        }

        Track newTrack;
        newTrack.trackId = nextTrackId++;
        newTrack.lastDetection = detections[detIdx];
        newTrack.lastMatchDiagnostic = "NEW";
        newTrack.missedFrames = 0;
        newTrack.hitStreak = 1;
        newTrack.lastUpdateFrame = frameCounter;

        const auto embeddingIt = detectionEmbeddings.find(detIdx);
        if (embeddingIt != detectionEmbeddings.end() && isValidEmbedding(embeddingIt->second)) {
            newTrack.lastEmbedding = embeddingIt->second;
            newTrack.hasEmbedding = true;
        }

        const auto initPoint = predictCenterPoint(newTrack, config);
        newTrack.predictedThisFrame = true;

        appendTracePoint(newTrack, initPoint, config);

        newTrack.lastDetection.x = initPoint.x - (newTrack.lastDetection.width * 0.5f);
        newTrack.lastDetection.y = initPoint.y - (newTrack.lastDetection.height * 0.5f);

        const auto newTrackId = newTrack.trackId;
        activeTracks.emplace(newTrackId, std::move(newTrack));
        assignedTrackByDetection[detIdx] = newTrackId;
    }

    std::vector<uint64_t> tracksToRemove;

    for (auto &[trackId, track] : activeTracks) {
        if (track.lastUpdateFrame < frameCounter) {
            amp::Perception::TrackTrace::Point predictedPoint;
            if (track.predictedThisFrame && track.kalmanInitialized) {
                const auto &state = track.kalman.state();
                predictedPoint = {state[0][0], state[1][0]};
            } else {
                predictedPoint = predictCenterPoint(track, config);
            }

            track.lastDetection.x = predictedPoint.x - (track.lastDetection.width * 0.5f);
            track.lastDetection.y = predictedPoint.y - (track.lastDetection.height * 0.5f);
            appendTracePoint(track, predictedPoint, config);

            track.missedFrames++;
            if (track.missedFrames <= config.maxMissedFrames) {
                track.lastMatchDiagnostic = "PRED";
                predictedOnlyTrackIds.push_back(trackId);
            } else {
                tracksToRemove.push_back(trackId);
            }
        }
    }

    for (const auto trackId : tracksToRemove) {
        const auto trackIt = activeTracks.find(trackId);
        if (trackIt != activeTracks.end() && trackIt->second.hasEmbedding &&
            isValidEmbedding(trackIt->second.lastEmbedding)) {
            DormantTrack dormant;
            dormant.trackId = trackId;
            dormant.lastDetection = trackIt->second.lastDetection;
            dormant.lastEmbedding = trackIt->second.lastEmbedding;
            dormant.storedAtFrame = frameCounter;
            dormantTracks[trackId] = std::move(dormant);
        }
        activeTracks.erase(trackId);
    }
}

void Processor::process(amp::Perception &perception, const Config &config) {
    frameCounter++;
    pruneDormantTracks(config);

    for (auto &[trackId, track] : activeTracks) {
        (void)trackId;
        track.predictedThisFrame = false;
    }

    std::vector<amp::Perception::Rect> currentDetections;
    std::vector<uint64_t> currentDetectionUuids;
    std::vector<amp::Perception::Layer *> targetLayers;
    std::map<uint64_t, std::vector<float>> embeddingByParentUuid;

    if (config.useEmbeddings) {
        for (const auto &layer : perception.layers) {
            if (layer.contentType != config.embeddingContentType) {
                continue;
            }

            for (const auto &det : layer.detections) {
                if (const auto *embedding = std::get_if<amp::Perception::ObjectEmbedding>(&det)) {
                    if (isValidEmbedding(embedding->values)) {
                        embeddingByParentUuid[embedding->parentUuid] = embedding->values;
                    }
                }
            }
        }
    }

    for (auto &layer : perception.layers) {
        if (layer.contentType == config.contentType) {
            for (auto &det : layer.detections) {
                if (auto *rect = std::get_if<amp::Perception::Rect>(&det)) {
                    currentDetections.push_back(*rect);
                    currentDetectionUuids.push_back(rect->uuid);
                }
            }
            targetLayers.push_back(&layer);
        }
    }

    std::map<size_t, std::vector<float>> detectionEmbeddingsByIndex;
    for (size_t i = 0; i < currentDetectionUuids.size(); ++i) {
        auto embeddingIt = embeddingByParentUuid.find(currentDetectionUuids[i]);
        if (embeddingIt != embeddingByParentUuid.end()) {
            detectionEmbeddingsByIndex[i] = embeddingIt->second;
        }
    }

    std::vector<std::pair<size_t, uint64_t>> matches;
    std::map<size_t, std::string> matchDiagnosticsByDetection;
    std::vector<size_t> unmatchedDetections;
    matchDetectionsToTracks(currentDetections,
                            detectionEmbeddingsByIndex,
                            matches,
                            matchDiagnosticsByDetection,
                            unmatchedDetections,
                            config);

    std::map<size_t, uint64_t> assignedTrackByDetection;
    std::vector<uint64_t> predictedOnlyTrackIds;
    updateTracks(currentDetections,
                 detectionEmbeddingsByIndex,
                 matches,
                 matchDiagnosticsByDetection,
                 unmatchedDetections,
                 assignedTrackByDetection,
                 predictedOnlyTrackIds,
                 config);

    size_t detectionIdx = 0;
    for (auto *layer : targetLayers) {
        for (auto &det : layer->detections) {
            if (auto *rect = std::get_if<amp::Perception::Rect>(&det)) {
                uint64_t assignedTrackId = 0;
                const auto assignedIt = assignedTrackByDetection.find(detectionIdx);
                if (assignedIt != assignedTrackByDetection.end()) {
                    assignedTrackId = assignedIt->second;
                }

                if (assignedTrackId > 0) {
                    const auto it = activeTracks.find(assignedTrackId);
                    if (it != activeTracks.end() &&
                        it->second.hitStreak >= config.minHitsToConfirm) {
                        rect->x = it->second.lastDetection.x;
                        rect->y = it->second.lastDetection.y;

                        if (config.appendTrackIdToText) {
                            const std::string trackDiag = it->second.lastMatchDiagnostic;
                            if (!rect->text.empty()) {
                                rect->text = fmt::format(
                                    "{} [ID:{} {}]", rect->text, assignedTrackId, trackDiag);
                            } else {
                                rect->text = fmt::format("ID:{} {}", assignedTrackId, trackDiag);
                            }
                        }
                    }
                }

                detectionIdx++;
            }
        }
    }

    amp::Perception::Layer *predictionOutputLayer = nullptr;
    if (!targetLayers.empty()) {
        predictionOutputLayer = targetLayers.front();
    } else {
        amp::Perception::Layer predictedLayer;
        predictedLayer.model = "Tracker";
        predictedLayer.engine = "std";
        predictedLayer.tags = "tracking-prediction";
        predictedLayer.contentType = config.contentType;
        perception.layers.push_back(std::move(predictedLayer));
        predictionOutputLayer = &perception.layers.back();
    }

    for (const auto trackId : predictedOnlyTrackIds) {
        const auto it = activeTracks.find(trackId);
        if (it == activeTracks.end()) {
            continue;
        }

        const auto &track = it->second;
        if (track.hitStreak < config.minHitsToConfirm) {
            continue;
        }

        auto predictedRect = track.lastDetection;
        if (config.appendTrackIdToText) {
            const std::string trackDiag = track.lastMatchDiagnostic;
            if (!predictedRect.text.empty()) {
                predictedRect.text =
                    fmt::format("{} [ID:{} {}]", predictedRect.text, trackId, trackDiag);
            } else {
                predictedRect.text = fmt::format("ID:{} {}", trackId, trackDiag);
            }
        }

        predictionOutputLayer->detections.push_back(predictedRect);
    }

    amp::Perception::Layer traceLayer;
    traceLayer.model = "Tracker";
    traceLayer.engine = "std";
    traceLayer.tags = "tracking";
    traceLayer.contentType = "trackTrace";

    for (const auto &[trackId, track] : activeTracks) {
        if (track.hitStreak < config.minHitsToConfirm || track.tracePoints.size() < 2) {
            continue;
        }

        amp::Perception::TrackTrace trace;
        trace.trackId = trackId;
        trace.parentUuid = track.lastDetection.parentUuid;
        trace.points.assign(track.tracePoints.begin(), track.tracePoints.end());

        traceLayer.detections.push_back(trace);
    }

    if (!traceLayer.detections.empty()) {
        perception.layers.push_back(std::move(traceLayer));
    }
}

} // namespace amp::tracker