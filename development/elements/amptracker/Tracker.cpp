/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Tracker.h"

#include "algo/Hungarian.h"
#include "algo/IoU.h"

#include <cmath>
#include <fmt/core.h>

namespace amp::tracker {

void Processor::reset() {
    activeTracks.clear();
    nextTrackId = 1;
    frameCounter = 0;
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

void Processor::matchDetectionsToTracks(const std::vector<amp::Perception::Rect> &detections,
                                        std::vector<std::pair<size_t, uint64_t>> &matches,
                                        std::vector<size_t> &unmatchedDetections,
                                        const Config &config) {
    matches.clear();
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
            costMatrix[detIdx][trackIdx] = 1.0f - iou;
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
            matches.push_back({detIdx, trackIds[static_cast<size_t>(trackIdx)]});
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
                             const std::vector<std::pair<size_t, uint64_t>> &matches,
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

        track.lastDetection.x = smoothedPoint.x - (track.lastDetection.width * 0.5f);
        track.lastDetection.y = smoothedPoint.y - (track.lastDetection.height * 0.5f);
        assignedTrackByDetection[detIdx] = trackId;
    }

    for (size_t detIdx : unmatchedDetections) {
        Track newTrack;
        newTrack.trackId = nextTrackId++;
        newTrack.lastDetection = detections[detIdx];
        newTrack.missedFrames = 0;
        newTrack.hitStreak = 1;
        newTrack.lastUpdateFrame = frameCounter;

        const auto initPoint = predictCenterPoint(newTrack, config);
        newTrack.predictedThisFrame = true;

        appendTracePoint(newTrack, initPoint, config);

        newTrack.lastDetection.x = initPoint.x - (newTrack.lastDetection.width * 0.5f);
        newTrack.lastDetection.y = initPoint.y - (newTrack.lastDetection.height * 0.5f);

        const auto newTrackId = newTrack.trackId;
        activeTracks[newTrackId] = newTrack;
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
                predictedOnlyTrackIds.push_back(trackId);
            } else {
                tracksToRemove.push_back(trackId);
            }
        }
    }

    for (const auto trackId : tracksToRemove) {
        activeTracks.erase(trackId);
    }
}

void Processor::process(amp::Perception &perception, const Config &config) {
    frameCounter++;

    for (auto &[trackId, track] : activeTracks) {
        (void)trackId;
        track.predictedThisFrame = false;
    }

    std::vector<amp::Perception::Rect> currentDetections;
    std::vector<amp::Perception::Layer *> targetLayers;

    for (auto &layer : perception.layers) {
        if (layer.contentType == config.contentType) {
            for (auto &det : layer.detections) {
                if (auto *rect = std::get_if<amp::Perception::Rect>(&det)) {
                    currentDetections.push_back(*rect);
                }
            }
            targetLayers.push_back(&layer);
        }
    }

    std::vector<std::pair<size_t, uint64_t>> matches;
    std::vector<size_t> unmatchedDetections;
    matchDetectionsToTracks(currentDetections, matches, unmatchedDetections, config);

    std::map<size_t, uint64_t> assignedTrackByDetection;
    std::vector<uint64_t> predictedOnlyTrackIds;
    updateTracks(currentDetections,
                 matches,
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
                            if (!rect->text.empty()) {
                                rect->text = fmt::format("{} [ID:{}]", rect->text, assignedTrackId);
                            } else {
                                rect->text = fmt::format("ID:{}", assignedTrackId);
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
            if (!predictedRect.text.empty()) {
                predictedRect.text = fmt::format("{} [ID:{}]", predictedRect.text, trackId);
            } else {
                predictedRect.text = fmt::format("ID:{}", trackId);
            }
        }

        predictionOutputLayer->detections.push_back(predictedRect);
    }

    amp::Perception::Layer traceLayer;
    traceLayer.model = "Tracker";
    traceLayer.engine = "std";
    traceLayer.tags = "tracking";
    traceLayer.contentType = config.traceContentType;

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