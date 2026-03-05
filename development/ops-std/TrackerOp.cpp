/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "TrackerOp.h"

#include <algorithm>
#include <cmath>
#include <fmt/core.h>
#include <set>

using namespace amp;

TrackerOp::TrackerOp() {}

TrackerOp::~TrackerOp() {}

Result<void> TrackerOp::configure(const AttributeMap &attributes) {
    contentType = attributes.getStringOrDefault("contentType", Defaults::contentType);
    iouThreshold = attributes.getFloatOrDefault("iouThreshold", Defaults::iouThreshold);
    maxMissedFrames = attributes.getIntOrDefault("maxMissedFrames", Defaults::maxMissedFrames);
    minHitsToConfirm = attributes.getIntOrDefault("minHitsToConfirm", Defaults::minHitsToConfirm);
    appendTrackIdToText =
        attributes.getBoolOrDefault("appendTrackIdToText", Defaults::appendTrackIdToText);
    traceHistoryLength =
        attributes.getIntOrDefault("traceHistoryLength", Defaults::traceHistoryLength);
    traceHistorySeconds =
        attributes.getFloatOrDefault("traceHistorySeconds", Defaults::traceHistorySeconds);
    traceContentType =
        attributes.getStringOrDefault("traceContentType", Defaults::traceContentType);

    // Preferred config: attributes.kalman.{...}; fallback to legacy flat attributes
    const auto kalman = attributes.getObjectOrNUll("kalman");
    const auto getKalmanFloat = [&attributes, &kalman](const char *key,
                                                       float defaultValue) -> float {
        if (kalman) {
            return kalman->getFloatOrDefault(key, defaultValue);
        }
        return attributes.getFloatOrDefault(key, defaultValue);
    };

    kalmanDt = getKalmanFloat("dt", Defaults::kalmanDt);
    if (kalmanDt <= 0.0f) {
        kalmanDt = Defaults::kalmanDt;
    }

    kalmanInitialCovariancePos =
        getKalmanFloat("initialCovariancePos", Defaults::kalmanInitialCovariancePos);
    kalmanInitialCovarianceVel =
        getKalmanFloat("initialCovarianceVel", Defaults::kalmanInitialCovarianceVel);
    kalmanProcessNoisePos = getKalmanFloat("processNoisePos", Defaults::kalmanProcessNoisePos);
    kalmanProcessNoiseVel = getKalmanFloat("processNoiseVel", Defaults::kalmanProcessNoiseVel);
    kalmanMeasurementNoisePos =
        getKalmanFloat("measurementNoisePos", Defaults::kalmanMeasurementNoisePos);

    // Backward compatibility when no nested block is present

    return {};
}

Result<void> TrackerOp::bind(size_t index, const std::vector<amp::Op *> &ops) {
    return {};
}

float TrackerOp::computeIOU(const Perception::Rect &a, const Perception::Rect &b) const {

    float x1 = std::max(a.x, b.x);
    float y1 = std::max(a.y, b.y);
    float x2 = std::min(a.x + a.width, b.x + b.width);
    float y2 = std::min(a.y + a.height, b.y + b.height);

    float intersectionWidth = std::max(0.0f, x2 - x1);
    float intersectionHeight = std::max(0.0f, y2 - y1);
    float intersectionArea = intersectionWidth * intersectionHeight;

    if (intersectionArea == 0.0f)
        return 0.0f;

    float areaA = a.width * a.height;
    float areaB = b.width * b.height;
    float unionArea = areaA + areaB - intersectionArea;

    if (unionArea == 0.0f)
        return 0.0f;

    return intersectionArea / unionArea;
}

void TrackerOp::matchDetectionsToTracks(const std::vector<Perception::Rect> &detections,
                                        std::vector<std::pair<size_t, uint64_t>> &matches,
                                        std::vector<size_t> &unmatchedDetections) {

    matches.clear();
    unmatchedDetections.clear();

    if (detections.empty()) {
        return;
    }

    // Build IOU matrix between detections and active tracks
    std::vector<std::tuple<float, size_t, uint64_t>> candidates;

    for (size_t detIdx = 0; detIdx < detections.size(); ++detIdx) {
        const auto &det = detections[detIdx];

        for (const auto &[trackId, track] : activeTracks) {
            float iou = computeIOU(det, track.lastDetection);
            if (iou >= iouThreshold) {
                candidates.push_back({iou, detIdx, trackId});
            }
        }
    }

    // Sort by IOU descending for greedy matching
    std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        return std::get<0>(a) > std::get<0>(b);
    });

    // Greedy matching: assign highest IOU pairs first
    std::set<size_t> matchedDetections;
    std::set<uint64_t> matchedTracks;

    for (const auto &[iou, detIdx, trackId] : candidates) {
        if (matchedDetections.count(detIdx) == 0 && matchedTracks.count(trackId) == 0) {
            matches.push_back({detIdx, trackId});
            matchedDetections.insert(detIdx);
            matchedTracks.insert(trackId);
        }
    }

    // Find unmatched detections
    for (size_t detIdx = 0; detIdx < detections.size(); ++detIdx) {
        if (matchedDetections.count(detIdx) == 0) {
            unmatchedDetections.push_back(detIdx);
        }
    }
}

void TrackerOp::updateTracks(const std::vector<Perception::Rect> &detections,
                             const std::vector<std::pair<size_t, uint64_t>> &matches,
                             const std::vector<size_t> &unmatchedDetections,
                             std::map<size_t, uint64_t> &assignedTrackByDetection,
                             std::vector<uint64_t> &predictedOnlyTrackIds) {

    assignedTrackByDetection.clear();
    predictedOnlyTrackIds.clear();

    // Update matched tracks
    for (const auto &[detIdx, trackId] : matches) {
        auto &track = activeTracks[trackId];
        track.lastDetection = detections[detIdx];
        track.missedFrames = 0;
        track.hitStreak++;
        track.lastUpdateFrame = frameCounter;
        const auto smoothedPoint = updateCenterPointWithMeasurement(track, track.lastDetection);
        appendTracePoint(track, smoothedPoint);

        track.lastDetection.x = smoothedPoint.x - (track.lastDetection.width * 0.5f);
        track.lastDetection.y = smoothedPoint.y - (track.lastDetection.height * 0.5f);
        assignedTrackByDetection[detIdx] = trackId;
    }

    // Create new tracks for unmatched detections
    for (size_t detIdx : unmatchedDetections) {
        Track newTrack;
        newTrack.trackId = nextTrackId++;
        newTrack.lastDetection = detections[detIdx];
        newTrack.missedFrames = 0;
        newTrack.hitStreak = 1;
        newTrack.lastUpdateFrame = frameCounter;
        const auto smoothedPoint =
            updateCenterPointWithMeasurement(newTrack, newTrack.lastDetection);
        appendTracePoint(newTrack, smoothedPoint);
        newTrack.lastDetection.x = smoothedPoint.x - (newTrack.lastDetection.width * 0.5f);
        newTrack.lastDetection.y = smoothedPoint.y - (newTrack.lastDetection.height * 0.5f);

        const auto newTrackId = newTrack.trackId;
        activeTracks[newTrackId] = newTrack;
        assignedTrackByDetection[detIdx] = newTrackId;
    }

    // Mark unmatched tracks as missed and remove old ones
    std::vector<uint64_t> tracksToRemove;

    for (auto &[trackId, track] : activeTracks) {
        if (track.lastUpdateFrame < frameCounter) {
            const auto predictedPoint = predictCenterPoint(track);
            track.lastDetection.x = predictedPoint.x - (track.lastDetection.width * 0.5f);
            track.lastDetection.y = predictedPoint.y - (track.lastDetection.height * 0.5f);
            appendTracePoint(track, predictedPoint);

            track.missedFrames++;
            if (track.missedFrames <= maxMissedFrames) {
                predictedOnlyTrackIds.push_back(trackId);
            } else {
                tracksToRemove.push_back(trackId);
            }
        }
    }

    for (uint64_t trackId : tracksToRemove) {
        activeTracks.erase(trackId);
    }
}

Perception::TrackTrace::Point TrackerOp::predictCenterPoint(Track &track) {
    using StateVector = TrackKalman::StateVector;
    using StateMatrix = TrackKalman::StateMatrix;

    if (!track.kalmanInitialized) {
        const auto centerX = track.lastDetection.x + (track.lastDetection.width * 0.5f);
        const auto centerY = track.lastDetection.y + (track.lastDetection.height * 0.5f);

        StateVector initialState{};
        initialState[0][0] = centerX;
        initialState[1][0] = centerY;
        initialState[2][0] = 0.0f;
        initialState[3][0] = 0.0f;

        StateMatrix initialCovariance{};
        initialCovariance[0][0] = kalmanInitialCovariancePos;
        initialCovariance[1][1] = kalmanInitialCovariancePos;
        initialCovariance[2][2] = kalmanInitialCovarianceVel;
        initialCovariance[3][3] = kalmanInitialCovarianceVel;

        track.kalman.setState(initialState);
        track.kalman.setCovariance(initialCovariance);
        track.kalmanInitialized = true;
        return {centerX, centerY};
    }

    StateMatrix transition{};
    transition[0][0] = 1.0f;
    transition[0][2] = kalmanDt;
    transition[1][1] = 1.0f;
    transition[1][3] = kalmanDt;
    transition[2][2] = 1.0f;
    transition[3][3] = 1.0f;

    StateMatrix processNoise{};
    processNoise[0][0] = kalmanProcessNoisePos;
    processNoise[1][1] = kalmanProcessNoisePos;
    processNoise[2][2] = kalmanProcessNoiseVel;
    processNoise[3][3] = kalmanProcessNoiseVel;

    track.kalman.predict(transition, processNoise);

    const auto &state = track.kalman.state();
    return {state[0][0], state[1][0]};
}

Perception::TrackTrace::Point
TrackerOp::updateCenterPointWithMeasurement(Track &track, const Perception::Rect &detection) {
    using MeasurementVector = TrackKalman::MeasurementVector;
    using MeasurementMatrix = TrackKalman::MeasurementMatrix;
    using ObservationMatrix = TrackKalman::ObservationMatrix;

    predictCenterPoint(track);

    const float measX = detection.x + (detection.width * 0.5f);
    const float measY = detection.y + (detection.height * 0.5f);

    MeasurementVector measurement{};
    measurement[0][0] = measX;
    measurement[1][0] = measY;

    ObservationMatrix observation{};
    observation[0][0] = 1.0f;
    observation[1][1] = 1.0f;

    MeasurementMatrix measurementNoise{};
    measurementNoise[0][0] = kalmanMeasurementNoisePos;
    measurementNoise[1][1] = kalmanMeasurementNoisePos;

    track.kalman.update(measurement, observation, measurementNoise);

    const auto &state = track.kalman.state();
    return {state[0][0], state[1][0]};
}

void TrackerOp::appendTracePoint(Track &track, const Perception::TrackTrace::Point &point) {
    track.tracePoints.push_back(point);

    int64_t historyPoints = traceHistoryLength;
    if (traceHistorySeconds > 0.0f && kalmanDt > 0.0f) {
        historyPoints = static_cast<int64_t>(std::ceil(traceHistorySeconds / kalmanDt));
    }

    const auto maxHistorySize = static_cast<size_t>(std::max<int64_t>(1, historyPoints));
    while (track.tracePoints.size() > maxHistorySize) {
        track.tracePoints.pop_front();
    }
}

Result<void> TrackerOp::process(OpChainContext &opChainContext) {
    frameCounter++;

    if (!opChainContext.perception) {
        return tl::unexpected(
            AMP_ERROR(ErrorFlag::InvalidOpChain, "TrackerOp: perception is null"));
    }

    // Collect all detections of the specified content type
    std::vector<Perception::Rect> currentDetections;
    std::vector<Perception::Layer *> targetLayers;

    for (auto &layer : opChainContext.perception->layers) {
        if (layer.contentType == contentType) {
            for (auto &det : layer.detections) {
                if (auto *rect = std::get_if<Perception::Rect>(&det)) {
                    currentDetections.push_back(*rect);
                }
            }
            targetLayers.push_back(&layer);
        }
    }

    // Match detections to existing tracks
    std::vector<std::pair<size_t, uint64_t>> matches;
    std::vector<size_t> unmatchedDetections;
    matchDetectionsToTracks(currentDetections, matches, unmatchedDetections);

    // Update track states
    std::map<size_t, uint64_t> assignedTrackByDetection;
    std::vector<uint64_t> predictedOnlyTrackIds;
    updateTracks(currentDetections,
                 matches,
                 unmatchedDetections,
                 assignedTrackByDetection,
                 predictedOnlyTrackIds);

    // Update Perception with track IDs
    size_t detectionIdx = 0;
    for (auto *layer : targetLayers) {
        for (auto &det : layer->detections) {
            if (auto *rect = std::get_if<Perception::Rect>(&det)) {
                uint64_t assignedTrackId = 0;
                const auto assignedIt = assignedTrackByDetection.find(detectionIdx);
                if (assignedIt != assignedTrackByDetection.end()) {
                    assignedTrackId = assignedIt->second;
                }

                // Update detection with track ID (only if confirmed)
                if (assignedTrackId > 0) {
                    auto it = activeTracks.find(assignedTrackId);
                    if (it != activeTracks.end() && it->second.hitStreak >= minHitsToConfirm) {
                        rect->x = it->second.lastDetection.x;
                        rect->y = it->second.lastDetection.y;

                        if (appendTrackIdToText) {
                            if (!rect->text.empty()) {
                                rect->text = fmt::format("{} [ID:{}]", rect->text, assignedTrackId);
                            } else {
                                rect->text = fmt::format("ID:{}", assignedTrackId);
                            }
                        }
                        // Store track ID in classId field as alternative (optional)
                        // rect->classId = static_cast<int>(assignedTrackId);
                    }
                }

                detectionIdx++;
            }
        }
    }

    Perception::Layer *predictionOutputLayer = nullptr;
    if (!targetLayers.empty()) {
        predictionOutputLayer = targetLayers.front();
    } else {
        Perception::Layer predictedLayer;
        predictedLayer.model = "Tracker";
        predictedLayer.engine = "std";
        predictedLayer.tags = "tracking-prediction";
        predictedLayer.contentType = contentType;
        opChainContext.perception->layers.push_back(std::move(predictedLayer));
        predictionOutputLayer = &opChainContext.perception->layers.back();
    }

    for (const auto trackId : predictedOnlyTrackIds) {
        const auto it = activeTracks.find(trackId);
        if (it == activeTracks.end()) {
            continue;
        }
        const auto &track = it->second;
        if (track.hitStreak < minHitsToConfirm) {
            continue;
        }

        auto predictedRect = track.lastDetection;
        if (appendTrackIdToText) {
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
    traceLayer.contentType = traceContentType;

    for (const auto &[trackId, track] : activeTracks) {
        if (track.hitStreak < minHitsToConfirm) {
            continue;
        }
        if (track.tracePoints.size() < 2) {
            continue;
        }

        amp::Perception::TrackTrace trace;
        trace.trackId = trackId;
        trace.parentUuid = track.lastDetection.parentUuid;
        trace.points.assign(track.tracePoints.begin(), track.tracePoints.end());

        traceLayer.detections.push_back(trace);
    }

    if (!traceLayer.detections.empty()) {
        opChainContext.perception->layers.push_back(std::move(traceLayer));
    }

    return {};
}
