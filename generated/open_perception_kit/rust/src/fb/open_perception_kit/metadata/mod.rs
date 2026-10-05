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
use super::*;
mod audio_frame_context_generated;
pub use self::audio_frame_context_generated::*;
mod bitmap_data_generated;
pub use self::bitmap_data_generated::*;
mod bounding_box_generated;
pub use self::bounding_box_generated::*;
mod box_detection_generated;
pub use self::box_detection_generated::*;
mod box_detections_generated;
pub use self::box_detections_generated::*;
mod classification_candidate_generated;
pub use self::classification_candidate_generated::*;
mod classification_generated;
pub use self::classification_generated::*;
mod classifications_generated;
pub use self::classifications_generated::*;
mod frame_context_generated;
pub use self::frame_context_generated::*;
mod layer_info_generated;
pub use self::layer_info_generated::*;
mod object_embedding_generated;
pub use self::object_embedding_generated::*;
mod object_embeddings_generated;
pub use self::object_embeddings_generated::*;
mod object_meta_generated;
pub use self::object_meta_generated::*;
mod object_track_generated;
pub use self::object_track_generated::*;
mod object_tracks_generated;
pub use self::object_tracks_generated::*;
mod performance_overlay_generated;
pub use self::performance_overlay_generated::*;
mod person_presence_generated;
pub use self::person_presence_generated::*;
mod point_2f_generated;
pub use self::point_2f_generated::*;
mod pose_estimation_generated;
pub use self::pose_estimation_generated::*;
mod pose_estimations_generated;
pub use self::pose_estimations_generated::*;
mod producer_info_generated;
pub use self::producer_info_generated::*;
mod segmentation_mask_generated;
pub use self::segmentation_mask_generated::*;
mod segmentation_masks_generated;
pub use self::segmentation_masks_generated::*;
mod track_trace_generated;
pub use self::track_trace_generated::*;
mod track_traces_generated;
pub use self::track_traces_generated::*;
mod video_frame_context_generated;
pub use self::video_frame_context_generated::*;
