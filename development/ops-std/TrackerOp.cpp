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
                             const std::vector<size_t> &unmatchedDetections) {

    // Update matched tracks
    for (const auto &[detIdx, trackId] : matches) {
        auto &track = activeTracks[trackId];
        track.lastDetection = detections[detIdx];
        track.missedFrames = 0;
        track.hitStreak++;
        track.lastUpdateFrame = frameCounter;
    }

    // Create new tracks for unmatched detections
    for (size_t detIdx : unmatchedDetections) {
        Track newTrack;
        newTrack.trackId = nextTrackId++;
        newTrack.lastDetection = detections[detIdx];
        newTrack.missedFrames = 0;
        newTrack.hitStreak = 1;
        newTrack.lastUpdateFrame = frameCounter;

        activeTracks[newTrack.trackId] = newTrack;
    }

    // Mark unmatched tracks as missed and remove old ones
    std::vector<uint64_t> tracksToRemove;

    for (auto &[trackId, track] : activeTracks) {
        if (track.lastUpdateFrame < frameCounter) {
            track.missedFrames++;
            if (track.missedFrames > maxMissedFrames) {
                tracksToRemove.push_back(trackId);
            }
        }
    }

    for (uint64_t trackId : tracksToRemove) {
        activeTracks.erase(trackId);
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
    updateTracks(currentDetections, matches, unmatchedDetections);

    // Update Perception with track IDs
    size_t detectionIdx = 0;
    for (auto *layer : targetLayers) {
        for (auto &det : layer->detections) {
            if (auto *rect = std::get_if<Perception::Rect>(&det)) {
                // Find matching track for this detection
                uint64_t assignedTrackId = 0;

                // Check matched detections
                for (const auto &[matchDetIdx, trackId] : matches) {
                    if (detectionIdx == matchDetIdx) {
                        assignedTrackId = trackId;
                        break;
                    }
                }

                // Check unmatched detections (newly created tracks)
                if (assignedTrackId == 0) {
                    for (size_t unmatchedIdx : unmatchedDetections) {
                        if (detectionIdx == unmatchedIdx) {
                            // Find the newly created track
                            for (const auto &[trackId, track] : activeTracks) {
                                if (track.lastUpdateFrame == frameCounter && track.hitStreak == 1) {
                                    float iou = computeIOU(*rect, track.lastDetection);
                                    if (iou > 0.99f) { // Nearly identical
                                        assignedTrackId = trackId;
                                        break;
                                    }
                                }
                            }
                            break;
                        }
                    }
                }

                // Update detection with track ID (only if confirmed)
                if (assignedTrackId > 0) {
                    auto it = activeTracks.find(assignedTrackId);
                    if (it != activeTracks.end() && it->second.hitStreak >= minHitsToConfirm) {
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

    return {};
}
