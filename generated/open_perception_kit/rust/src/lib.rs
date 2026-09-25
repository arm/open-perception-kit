// Copyright (C) 2026 Arm Limited. All rights reserved.
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.

#![allow(dead_code, unused_imports, non_camel_case_types, non_snake_case)]
#![allow(
    clippy::derivable_impls,
    clippy::extra_unused_lifetimes,
    clippy::missing_safety_doc,
    clippy::needless_lifetimes
)]
#![warn(missing_docs)]

//! Owning Rust API for the generated `open_perception_kit` FlowData SDK.
//!
//! Use [`Envelope`] to decode, inspect, modify, and serialize FlowData packets.
//! Known payloads use [`payload`], while application-defined byte payloads use
//! [`external_key`] and [`external_payload`]. Raw FlatBuffers object types are
//! available under [`fb`].

#[allow(missing_docs)]
/// Raw FlatBuffers types, organized by their schema namespaces.
pub mod fb;
mod flowdata_internal;

use ::std::error::Error;
use ::std::fmt;
use ::std::marker::PhantomData;
use ::std::slice;

use crate::flowdata_internal::internalfb as wire;

/// Generated SDK name written into serialized producer identity metadata.
pub const OPEN_PERCEPTION_KIT_NAME: &str = "open_perception_kit";
/// Generated SDK version written into serialized producer identity metadata.
pub const OPEN_PERCEPTION_KIT_VERSION: &str = "0.1.0";
/// SHA-256 identity of the complete schema set used for generation.
pub const SCHEMA_SET_SHA256: &str =
    "1b19418d8a0d34038a3c99895fa93a1140c25af910f6bc63c0e11978e12c2876"; // pragma: allowlist secret
/// Exact FlatBuffers runtime version required by this crate.
pub const FLATBUFFERS_VERSION_REQUIREMENT: &str = "==25.9.23";
/// Lowest numeric key reserved for application-defined external payloads.
pub const EXTERNAL_KEY_MIN: u64 = 9223372036854775808;

const EXTERNAL_HASH_OFFSET: u64 = 14_695_981_039_346_656_037;
const EXTERNAL_HASH_PRIME: u64 = 1_099_511_628_211;

mod private {
    pub trait SealedPayload {}
    pub trait SealedSelector {}
    pub trait SealedEnvelopeValue {}
}

/// Relationship between packet producer metadata and this generated SDK.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProducerIdentityStatus {
    /// SDK name, version, and schema digest all match exactly.
    ExactMatch,
    /// At least one producer identity field is absent.
    Missing,
    /// At least one producer identity field has invalid syntax.
    Malformed,
    /// The producer used a different SDK name.
    SdkNameMismatch,
    /// The producer used a different SDK version.
    SdkVersionMismatch,
    /// The producer used a different schema set.
    SchemaSetMismatch,
}

/// Opaque, deterministic selector for an application-defined byte payload.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct ExternalKey(u64);

impl ExternalKey {
    /// Returns the encoded numeric key used on the wire.
    pub fn value(self) -> u64 {
        self.0
    }
}

/// Derives a stable external payload key from an application-owned string.
pub fn external_key(key: &str) -> ExternalKey {
    let mut value = EXTERNAL_HASH_OFFSET;
    for byte in key.as_bytes() {
        value ^= u64::from(*byte);
        value = value.wrapping_mul(EXTERNAL_HASH_PRIME);
    }
    ExternalKey((value & (EXTERNAL_KEY_MIN - 1)) | EXTERNAL_KEY_MIN)
}

/// Returns whether a key belongs to the reserved external payload range.
pub fn is_external_key(key: ExternalKey) -> bool {
    key.value() >= EXTERNAL_KEY_MIN
}

/// Owned application-defined payload ready to add to an [`Envelope`].
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ExternalPayload {
    key: ExternalKey,
    bytes: Vec<u8>,
}

/// Creates an owned external payload for use with [`Envelope::add`].
pub fn external_payload(key: ExternalKey, bytes: impl Into<Vec<u8>>) -> ExternalPayload {
    ExternalPayload {
        key,
        bytes: bytes.into(),
    }
}

/// Error encountered while decoding a known payload body.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PayloadDecodeError {
    /// The payload does not carry its schema's required file identifier.
    InvalidIdentifier {
        /// Required FlatBuffers file identifier.
        expected: &'static str,
    },
    /// FlatBuffers rejected the payload bytes.
    InvalidFlatbuffer(String),
}

impl fmt::Display for PayloadDecodeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidIdentifier { expected } => write!(
                formatter,
                "invalid payload file identifier; expected {expected}"
            ),
            Self::InvalidFlatbuffer(error) => formatter.write_str(error),
        }
    }
}

impl Error for PayloadDecodeError {}

/// Category of an outer envelope decoding failure.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum EnvelopeDecodeErrorKind {
    /// The packet does not carry the `FLWD` envelope identifier.
    InvalidIdentifier,
    /// FlatBuffers rejected the envelope bytes.
    InvalidFlatbuffer(String),
}

/// Envelope decoding failure that retains the original input bytes.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EnvelopeDecodeError {
    kind: EnvelopeDecodeErrorKind,
    bytes: Vec<u8>,
}

impl EnvelopeDecodeError {
    /// Returns the failure category.
    pub fn kind(&self) -> &EnvelopeDecodeErrorKind {
        &self.kind
    }
    /// Borrows the original undecoded input bytes.
    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }
    /// Returns ownership of the original undecoded input bytes.
    pub fn into_bytes(self) -> Vec<u8> {
        self.bytes
    }
}

impl fmt::Display for EnvelopeDecodeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match &self.kind {
            EnvelopeDecodeErrorKind::InvalidIdentifier => {
                formatter.write_str("invalid envelope file identifier; expected FLWD")
            }
            EnvelopeDecodeErrorKind::InvalidFlatbuffer(error) => formatter.write_str(error),
        }
    }
}

impl Error for EnvelopeDecodeError {}

#[doc(hidden)]
#[derive(Debug, Clone, PartialEq)]
pub enum PayloadData {
    Payload0(crate::fb::open_perception_kit::metadata::BoxDetectionsT),
    Payload1(crate::fb::open_perception_kit::metadata::ClassificationsT),
    Payload2(crate::fb::open_perception_kit::metadata::FrameContextT),
    Payload3(crate::fb::open_perception_kit::metadata::ObjectEmbeddingsT),
    Payload4(crate::fb::open_perception_kit::metadata::ObjectTracksT),
    Payload5(crate::fb::open_perception_kit::metadata::PerformanceOverlayT),
    Payload6(crate::fb::open_perception_kit::metadata::PoseEstimationsT),
    Payload7(crate::fb::open_perception_kit::metadata::SegmentationMasksT),
    Payload8(crate::fb::open_perception_kit::metadata::TrackTracesT),
}

#[derive(Debug, Clone, PartialEq)]
enum Entry {
    Known(PayloadData),
    External {
        id: u64,
        bytes: Vec<u8>,
    },
    Unknown {
        id: u64,
        bytes: Vec<u8>,
    },
    MalformedKnown {
        id: u64,
        expected_type: &'static str,
        bytes: Vec<u8>,
        error: PayloadDecodeError,
    },
}

/// Read-only diagnostic view of an envelope entry.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum EntryRef<'a> {
    /// Successfully decoded generated payload.
    Known {
        /// Fully qualified schema type name.
        type_name: &'static str,
    },
    /// Application-defined byte payload.
    External {
        /// Stable external selector.
        key: ExternalKey,
        /// Borrowed payload bytes.
        bytes: &'a [u8],
    },
    /// Unrecognized payload in the generated payload-ID domain.
    Unknown {
        /// Unrecognized wire payload ID.
        id: u64,
        /// Borrowed payload bytes preserved for forwarding.
        bytes: &'a [u8],
    },
    /// Known payload whose body could not be decoded.
    MalformedKnown {
        /// Known wire payload ID.
        id: u64,
        /// Fully qualified schema type expected for the ID.
        expected_type: &'static str,
        /// Borrowed malformed bytes preserved for forwarding.
        bytes: &'a [u8],
        /// Typed reason the known payload failed to decode.
        error: &'a PayloadDecodeError,
    },
}

/// Generated owned payload type accepted by the envelope API.
pub trait NativePayload: private::SealedPayload + Sized + Send + Sync + 'static {
    /// Fully qualified schema type name.
    const NAME: &'static str;
    #[doc(hidden)]
    fn into_payload_data(self) -> PayloadData;
    #[doc(hidden)]
    fn from_payload_data(data: &PayloadData) -> Option<&Self>;
    #[doc(hidden)]
    fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError>;
    #[doc(hidden)]
    fn encode_payload(&self) -> Vec<u8>;
}

/// Owned value that can be appended to an [`Envelope`].
pub trait EnvelopeValue: private::SealedEnvelopeValue {
    #[doc(hidden)]
    fn append_to(self, envelope: &mut Envelope);
}

impl private::SealedEnvelopeValue for ExternalPayload {}

impl EnvelopeValue for ExternalPayload {
    fn append_to(self, envelope: &mut Envelope) {
        envelope.entries.push(Entry::External {
            id: self.key.value(),
            bytes: self.bytes,
        });
    }
}

/// Zero-sized selector for a generated payload type.
pub struct Payload<T: NativePayload>(PhantomData<fn() -> T>);

impl<T: NativePayload> Copy for Payload<T> {}

impl<T: NativePayload> Clone for Payload<T> {
    fn clone(&self) -> Self {
        *self
    }
}

impl<T: NativePayload> fmt::Debug for Payload<T> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_tuple("Payload").field(&T::NAME).finish()
    }
}

/// Creates a reusable selector for a generated payload type.
pub const fn payload<T: NativePayload>() -> Payload<T> {
    Payload(PhantomData)
}

/// Iterator over decoded instances of a selected generated payload type.
pub struct PayloadIter<'a, T: NativePayload> {
    inner: slice::Iter<'a, Entry>,
    marker: PhantomData<fn() -> T>,
}

impl<'a, T: NativePayload> Iterator for PayloadIter<'a, T> {
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        self.inner.find_map(|entry| match entry {
            Entry::Known(data) => T::from_payload_data(data),
            _ => None,
        })
    }
}

/// Iterator over byte slices for a selected external payload key.
pub struct ExternalIter<'a> {
    inner: slice::Iter<'a, Entry>,
    key: ExternalKey,
}

impl<'a> Iterator for ExternalIter<'a> {
    type Item = &'a [u8];

    fn next(&mut self) -> Option<Self::Item> {
        self.inner.find_map(|entry| match entry {
            Entry::External { id, bytes } if *id == self.key.value() => Some(bytes.as_slice()),
            _ => None,
        })
    }
}

/// Sealed selector implemented by [`Payload`] and [`ExternalKey`].
pub trait Selector: private::SealedSelector + Copy {
    /// Item yielded when the selector matches an envelope entry.
    type Item<'a>
    where
        Self: 'a;
    /// Iterator returned by [`Envelope::for_each`].
    type Iter<'a>: Iterator<Item = Self::Item<'a>>
    where
        Self: 'a;

    #[doc(hidden)]
    fn iter<'a>(self, envelope: &'a Envelope) -> Self::Iter<'a>;
}

impl<T: NativePayload> private::SealedSelector for Payload<T> {}

impl<T: NativePayload> Selector for Payload<T> {
    type Item<'a> = &'a T;
    type Iter<'a> = PayloadIter<'a, T>;

    fn iter<'a>(self, envelope: &'a Envelope) -> Self::Iter<'a> {
        PayloadIter {
            inner: envelope.entries.iter(),
            marker: PhantomData,
        }
    }
}

impl private::SealedSelector for ExternalKey {}

impl Selector for ExternalKey {
    type Item<'a> = &'a [u8];
    type Iter<'a> = ExternalIter<'a>;

    fn iter<'a>(self, envelope: &'a Envelope) -> Self::Iter<'a> {
        ExternalIter {
            inner: envelope.entries.iter(),
            key: self,
        }
    }
}

impl private::SealedPayload for crate::fb::open_perception_kit::metadata::BoxDetectionsT {}

impl NativePayload for crate::fb::open_perception_kit::metadata::BoxDetectionsT {
    const NAME: &'static str = "open_perception_kit.metadata.BoxDetections";

    fn into_payload_data(self) -> PayloadData {
        PayloadData::Payload0(self)
    }

    fn from_payload_data(data: &PayloadData) -> Option<&Self> {
        match data {
            PayloadData::Payload0(value) => Some(value),
            _ => None,
        }
    }

    fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError> {
        use crate::fb::open_perception_kit::metadata as generated;
        if bytes.len() < 8 || !generated::box_detections_buffer_has_identifier(bytes) {
            return Err(PayloadDecodeError::InvalidIdentifier { expected: "BDET" });
        }
        generated::root_as_box_detections(bytes)
            .map(|value| value.unpack())
            .map_err(|error| PayloadDecodeError::InvalidFlatbuffer(error.to_string()))
    }

    fn encode_payload(&self) -> Vec<u8> {
        use crate::fb::open_perception_kit::metadata as generated;
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let root = self.pack(&mut builder);
        generated::finish_box_detections_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }
}

impl private::SealedEnvelopeValue for crate::fb::open_perception_kit::metadata::BoxDetectionsT {}

impl EnvelopeValue for crate::fb::open_perception_kit::metadata::BoxDetectionsT {
    fn append_to(self, envelope: &mut Envelope) {
        envelope
            .entries
            .push(Entry::Known(self.into_payload_data()));
    }
}

impl private::SealedPayload for crate::fb::open_perception_kit::metadata::ClassificationsT {}

impl NativePayload for crate::fb::open_perception_kit::metadata::ClassificationsT {
    const NAME: &'static str = "open_perception_kit.metadata.Classifications";

    fn into_payload_data(self) -> PayloadData {
        PayloadData::Payload1(self)
    }

    fn from_payload_data(data: &PayloadData) -> Option<&Self> {
        match data {
            PayloadData::Payload1(value) => Some(value),
            _ => None,
        }
    }

    fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError> {
        use crate::fb::open_perception_kit::metadata as generated;
        if bytes.len() < 8 || !generated::classifications_buffer_has_identifier(bytes) {
            return Err(PayloadDecodeError::InvalidIdentifier { expected: "CLSF" });
        }
        generated::root_as_classifications(bytes)
            .map(|value| value.unpack())
            .map_err(|error| PayloadDecodeError::InvalidFlatbuffer(error.to_string()))
    }

    fn encode_payload(&self) -> Vec<u8> {
        use crate::fb::open_perception_kit::metadata as generated;
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let root = self.pack(&mut builder);
        generated::finish_classifications_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }
}

impl private::SealedEnvelopeValue for crate::fb::open_perception_kit::metadata::ClassificationsT {}

impl EnvelopeValue for crate::fb::open_perception_kit::metadata::ClassificationsT {
    fn append_to(self, envelope: &mut Envelope) {
        envelope
            .entries
            .push(Entry::Known(self.into_payload_data()));
    }
}

impl private::SealedPayload for crate::fb::open_perception_kit::metadata::FrameContextT {}

impl NativePayload for crate::fb::open_perception_kit::metadata::FrameContextT {
    const NAME: &'static str = "open_perception_kit.metadata.FrameContext";

    fn into_payload_data(self) -> PayloadData {
        PayloadData::Payload2(self)
    }

    fn from_payload_data(data: &PayloadData) -> Option<&Self> {
        match data {
            PayloadData::Payload2(value) => Some(value),
            _ => None,
        }
    }

    fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError> {
        use crate::fb::open_perception_kit::metadata as generated;
        if bytes.len() < 8 || !generated::frame_context_buffer_has_identifier(bytes) {
            return Err(PayloadDecodeError::InvalidIdentifier { expected: "FCTX" });
        }
        generated::root_as_frame_context(bytes)
            .map(|value| value.unpack())
            .map_err(|error| PayloadDecodeError::InvalidFlatbuffer(error.to_string()))
    }

    fn encode_payload(&self) -> Vec<u8> {
        use crate::fb::open_perception_kit::metadata as generated;
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let root = self.pack(&mut builder);
        generated::finish_frame_context_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }
}

impl private::SealedEnvelopeValue for crate::fb::open_perception_kit::metadata::FrameContextT {}

impl EnvelopeValue for crate::fb::open_perception_kit::metadata::FrameContextT {
    fn append_to(self, envelope: &mut Envelope) {
        envelope
            .entries
            .push(Entry::Known(self.into_payload_data()));
    }
}

impl private::SealedPayload for crate::fb::open_perception_kit::metadata::ObjectEmbeddingsT {}

impl NativePayload for crate::fb::open_perception_kit::metadata::ObjectEmbeddingsT {
    const NAME: &'static str = "open_perception_kit.metadata.ObjectEmbeddings";

    fn into_payload_data(self) -> PayloadData {
        PayloadData::Payload3(self)
    }

    fn from_payload_data(data: &PayloadData) -> Option<&Self> {
        match data {
            PayloadData::Payload3(value) => Some(value),
            _ => None,
        }
    }

    fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError> {
        use crate::fb::open_perception_kit::metadata as generated;
        if bytes.len() < 8 || !generated::object_embeddings_buffer_has_identifier(bytes) {
            return Err(PayloadDecodeError::InvalidIdentifier { expected: "EMBE" });
        }
        generated::root_as_object_embeddings(bytes)
            .map(|value| value.unpack())
            .map_err(|error| PayloadDecodeError::InvalidFlatbuffer(error.to_string()))
    }

    fn encode_payload(&self) -> Vec<u8> {
        use crate::fb::open_perception_kit::metadata as generated;
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let root = self.pack(&mut builder);
        generated::finish_object_embeddings_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }
}

impl private::SealedEnvelopeValue for crate::fb::open_perception_kit::metadata::ObjectEmbeddingsT {}

impl EnvelopeValue for crate::fb::open_perception_kit::metadata::ObjectEmbeddingsT {
    fn append_to(self, envelope: &mut Envelope) {
        envelope
            .entries
            .push(Entry::Known(self.into_payload_data()));
    }
}

impl private::SealedPayload for crate::fb::open_perception_kit::metadata::ObjectTracksT {}

impl NativePayload for crate::fb::open_perception_kit::metadata::ObjectTracksT {
    const NAME: &'static str = "open_perception_kit.metadata.ObjectTracks";

    fn into_payload_data(self) -> PayloadData {
        PayloadData::Payload4(self)
    }

    fn from_payload_data(data: &PayloadData) -> Option<&Self> {
        match data {
            PayloadData::Payload4(value) => Some(value),
            _ => None,
        }
    }

    fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError> {
        use crate::fb::open_perception_kit::metadata as generated;
        if bytes.len() < 8 || !generated::object_tracks_buffer_has_identifier(bytes) {
            return Err(PayloadDecodeError::InvalidIdentifier { expected: "TRKS" });
        }
        generated::root_as_object_tracks(bytes)
            .map(|value| value.unpack())
            .map_err(|error| PayloadDecodeError::InvalidFlatbuffer(error.to_string()))
    }

    fn encode_payload(&self) -> Vec<u8> {
        use crate::fb::open_perception_kit::metadata as generated;
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let root = self.pack(&mut builder);
        generated::finish_object_tracks_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }
}

impl private::SealedEnvelopeValue for crate::fb::open_perception_kit::metadata::ObjectTracksT {}

impl EnvelopeValue for crate::fb::open_perception_kit::metadata::ObjectTracksT {
    fn append_to(self, envelope: &mut Envelope) {
        envelope
            .entries
            .push(Entry::Known(self.into_payload_data()));
    }
}

impl private::SealedPayload for crate::fb::open_perception_kit::metadata::PerformanceOverlayT {}

impl NativePayload for crate::fb::open_perception_kit::metadata::PerformanceOverlayT {
    const NAME: &'static str = "open_perception_kit.metadata.PerformanceOverlay";

    fn into_payload_data(self) -> PayloadData {
        PayloadData::Payload5(self)
    }

    fn from_payload_data(data: &PayloadData) -> Option<&Self> {
        match data {
            PayloadData::Payload5(value) => Some(value),
            _ => None,
        }
    }

    fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError> {
        use crate::fb::open_perception_kit::metadata as generated;
        if bytes.len() < 8 || !generated::performance_overlay_buffer_has_identifier(bytes) {
            return Err(PayloadDecodeError::InvalidIdentifier { expected: "PERF" });
        }
        generated::root_as_performance_overlay(bytes)
            .map(|value| value.unpack())
            .map_err(|error| PayloadDecodeError::InvalidFlatbuffer(error.to_string()))
    }

    fn encode_payload(&self) -> Vec<u8> {
        use crate::fb::open_perception_kit::metadata as generated;
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let root = self.pack(&mut builder);
        generated::finish_performance_overlay_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }
}

impl private::SealedEnvelopeValue
    for crate::fb::open_perception_kit::metadata::PerformanceOverlayT
{
}

impl EnvelopeValue for crate::fb::open_perception_kit::metadata::PerformanceOverlayT {
    fn append_to(self, envelope: &mut Envelope) {
        envelope
            .entries
            .push(Entry::Known(self.into_payload_data()));
    }
}

impl private::SealedPayload for crate::fb::open_perception_kit::metadata::PoseEstimationsT {}

impl NativePayload for crate::fb::open_perception_kit::metadata::PoseEstimationsT {
    const NAME: &'static str = "open_perception_kit.metadata.PoseEstimations";

    fn into_payload_data(self) -> PayloadData {
        PayloadData::Payload6(self)
    }

    fn from_payload_data(data: &PayloadData) -> Option<&Self> {
        match data {
            PayloadData::Payload6(value) => Some(value),
            _ => None,
        }
    }

    fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError> {
        use crate::fb::open_perception_kit::metadata as generated;
        if bytes.len() < 8 || !generated::pose_estimations_buffer_has_identifier(bytes) {
            return Err(PayloadDecodeError::InvalidIdentifier { expected: "POSE" });
        }
        generated::root_as_pose_estimations(bytes)
            .map(|value| value.unpack())
            .map_err(|error| PayloadDecodeError::InvalidFlatbuffer(error.to_string()))
    }

    fn encode_payload(&self) -> Vec<u8> {
        use crate::fb::open_perception_kit::metadata as generated;
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let root = self.pack(&mut builder);
        generated::finish_pose_estimations_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }
}

impl private::SealedEnvelopeValue for crate::fb::open_perception_kit::metadata::PoseEstimationsT {}

impl EnvelopeValue for crate::fb::open_perception_kit::metadata::PoseEstimationsT {
    fn append_to(self, envelope: &mut Envelope) {
        envelope
            .entries
            .push(Entry::Known(self.into_payload_data()));
    }
}

impl private::SealedPayload for crate::fb::open_perception_kit::metadata::SegmentationMasksT {}

impl NativePayload for crate::fb::open_perception_kit::metadata::SegmentationMasksT {
    const NAME: &'static str = "open_perception_kit.metadata.SegmentationMasks";

    fn into_payload_data(self) -> PayloadData {
        PayloadData::Payload7(self)
    }

    fn from_payload_data(data: &PayloadData) -> Option<&Self> {
        match data {
            PayloadData::Payload7(value) => Some(value),
            _ => None,
        }
    }

    fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError> {
        use crate::fb::open_perception_kit::metadata as generated;
        if bytes.len() < 8 || !generated::segmentation_masks_buffer_has_identifier(bytes) {
            return Err(PayloadDecodeError::InvalidIdentifier { expected: "SGMS" });
        }
        generated::root_as_segmentation_masks(bytes)
            .map(|value| value.unpack())
            .map_err(|error| PayloadDecodeError::InvalidFlatbuffer(error.to_string()))
    }

    fn encode_payload(&self) -> Vec<u8> {
        use crate::fb::open_perception_kit::metadata as generated;
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let root = self.pack(&mut builder);
        generated::finish_segmentation_masks_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }
}

impl private::SealedEnvelopeValue for crate::fb::open_perception_kit::metadata::SegmentationMasksT {}

impl EnvelopeValue for crate::fb::open_perception_kit::metadata::SegmentationMasksT {
    fn append_to(self, envelope: &mut Envelope) {
        envelope
            .entries
            .push(Entry::Known(self.into_payload_data()));
    }
}

impl private::SealedPayload for crate::fb::open_perception_kit::metadata::TrackTracesT {}

impl NativePayload for crate::fb::open_perception_kit::metadata::TrackTracesT {
    const NAME: &'static str = "open_perception_kit.metadata.TrackTraces";

    fn into_payload_data(self) -> PayloadData {
        PayloadData::Payload8(self)
    }

    fn from_payload_data(data: &PayloadData) -> Option<&Self> {
        match data {
            PayloadData::Payload8(value) => Some(value),
            _ => None,
        }
    }

    fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError> {
        use crate::fb::open_perception_kit::metadata as generated;
        if bytes.len() < 8 || !generated::track_traces_buffer_has_identifier(bytes) {
            return Err(PayloadDecodeError::InvalidIdentifier { expected: "TRCE" });
        }
        generated::root_as_track_traces(bytes)
            .map(|value| value.unpack())
            .map_err(|error| PayloadDecodeError::InvalidFlatbuffer(error.to_string()))
    }

    fn encode_payload(&self) -> Vec<u8> {
        use crate::fb::open_perception_kit::metadata as generated;
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let root = self.pack(&mut builder);
        generated::finish_track_traces_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }
}

impl private::SealedEnvelopeValue for crate::fb::open_perception_kit::metadata::TrackTracesT {}

impl EnvelopeValue for crate::fb::open_perception_kit::metadata::TrackTracesT {
    fn append_to(self, envelope: &mut Envelope) {
        envelope
            .entries
            .push(Entry::Known(self.into_payload_data()));
    }
}

fn decode_entry<T: NativePayload>(id: u64, bytes: Vec<u8>) -> Entry {
    match T::decode_payload(&bytes) {
        Ok(value) => Entry::Known(value.into_payload_data()),
        Err(error) => Entry::MalformedKnown {
            id,
            expected_type: T::NAME,
            bytes,
            error,
        },
    }
}

fn decode_known(id: u64, bytes: Vec<u8>) -> Entry {
    match id {
        556103652012567315 => {
            decode_entry::<crate::fb::open_perception_kit::metadata::BoxDetectionsT>(id, bytes)
        }
        2852697023809600655 => {
            decode_entry::<crate::fb::open_perception_kit::metadata::ClassificationsT>(id, bytes)
        }
        2065478860695108412 => {
            decode_entry::<crate::fb::open_perception_kit::metadata::FrameContextT>(id, bytes)
        }
        5972661533224817501 => {
            decode_entry::<crate::fb::open_perception_kit::metadata::ObjectEmbeddingsT>(id, bytes)
        }
        2960585987463094496 => {
            decode_entry::<crate::fb::open_perception_kit::metadata::ObjectTracksT>(id, bytes)
        }
        7749401259036278028 => {
            decode_entry::<crate::fb::open_perception_kit::metadata::PerformanceOverlayT>(id, bytes)
        }
        9114952555105892553 => {
            decode_entry::<crate::fb::open_perception_kit::metadata::PoseEstimationsT>(id, bytes)
        }
        1998909987238011535 => {
            decode_entry::<crate::fb::open_perception_kit::metadata::SegmentationMasksT>(id, bytes)
        }
        238211229389337861 => {
            decode_entry::<crate::fb::open_perception_kit::metadata::TrackTracesT>(id, bytes)
        }
        _ if id >= EXTERNAL_KEY_MIN => Entry::External { id, bytes },
        _ => Entry::Unknown { id, bytes },
    }
}

fn known_type_name(data: &PayloadData) -> &'static str {
    match data {
        PayloadData::Payload0(_) => "open_perception_kit.metadata.BoxDetections",
        PayloadData::Payload1(_) => "open_perception_kit.metadata.Classifications",
        PayloadData::Payload2(_) => "open_perception_kit.metadata.FrameContext",
        PayloadData::Payload3(_) => "open_perception_kit.metadata.ObjectEmbeddings",
        PayloadData::Payload4(_) => "open_perception_kit.metadata.ObjectTracks",
        PayloadData::Payload5(_) => "open_perception_kit.metadata.PerformanceOverlay",
        PayloadData::Payload6(_) => "open_perception_kit.metadata.PoseEstimations",
        PayloadData::Payload7(_) => "open_perception_kit.metadata.SegmentationMasks",
        PayloadData::Payload8(_) => "open_perception_kit.metadata.TrackTraces",
    }
}

/// Owned FlowData packet containing generated and application-defined payloads.
#[derive(Debug, Clone, PartialEq)]
pub struct Envelope {
    entries: Vec<Entry>,
    producer_sdk_name: String,
    producer_sdk_version: String,
    producer_schema_set_sha256: String,
}

impl Default for Envelope {
    fn default() -> Self {
        Self::new()
    }
}

impl Envelope {
    /// Creates an empty envelope stamped with this generated SDK's identity.
    pub fn new() -> Self {
        Self {
            entries: Vec::new(),
            producer_sdk_name: OPEN_PERCEPTION_KIT_NAME.to_owned(),
            producer_sdk_version: OPEN_PERCEPTION_KIT_VERSION.to_owned(),
            producer_schema_set_sha256: SCHEMA_SET_SHA256.to_owned(),
        }
    }

    /// Decodes an owned packet while preserving unknown or malformed payload entries.
    pub fn decode(bytes: Vec<u8>) -> Result<Self, EnvelopeDecodeError> {
        if bytes.len() < 8 || !wire::wire_envelope_buffer_has_identifier(&bytes) {
            return Err(EnvelopeDecodeError {
                kind: EnvelopeDecodeErrorKind::InvalidIdentifier,
                bytes,
            });
        }
        let envelope = match wire::root_as_wire_envelope(&bytes) {
            Ok(envelope) => envelope,
            Err(error) => {
                return Err(EnvelopeDecodeError {
                    kind: EnvelopeDecodeErrorKind::InvalidFlatbuffer(error.to_string()),
                    bytes,
                })
            }
        };
        let entries = envelope
            .payloads()
            .map(|payloads| {
                payloads
                    .iter()
                    .map(|payload| {
                        let blob = payload
                            .blob()
                            .map(|value| value.iter().collect())
                            .unwrap_or_default();
                        decode_known(payload.id(), blob)
                    })
                    .collect()
            })
            .unwrap_or_default();
        Ok(Self {
            entries,
            producer_sdk_name: envelope.producer_sdk_name().unwrap_or_default().to_owned(),
            producer_sdk_version: envelope
                .producer_sdk_version()
                .unwrap_or_default()
                .to_owned(),
            producer_schema_set_sha256: envelope
                .producer_schema_set_sha256()
                .unwrap_or_default()
                .to_owned(),
        })
    }

    /// Returns the producer SDK name stored in the decoded packet.
    pub fn producer_sdk_name(&self) -> &str {
        &self.producer_sdk_name
    }
    /// Returns the producer SDK version stored in the decoded packet.
    pub fn producer_sdk_version(&self) -> &str {
        &self.producer_sdk_version
    }
    /// Returns the producer schema-set digest stored in the decoded packet.
    pub fn producer_schema_set_sha256(&self) -> &str {
        &self.producer_schema_set_sha256
    }

    /// Compares producer identity metadata with this generated SDK.
    pub fn producer_identity(&self) -> ProducerIdentityStatus {
        let name = self.producer_sdk_name();
        let version = self.producer_sdk_version();
        let digest = self.producer_schema_set_sha256();
        if name.is_empty() || version.is_empty() || digest.is_empty() {
            return ProducerIdentityStatus::Missing;
        }
        if !valid_sdk_name(name) || !valid_semver(version) || !valid_sha256(digest) {
            return ProducerIdentityStatus::Malformed;
        }
        if name != OPEN_PERCEPTION_KIT_NAME {
            return ProducerIdentityStatus::SdkNameMismatch;
        }
        if version != OPEN_PERCEPTION_KIT_VERSION {
            return ProducerIdentityStatus::SdkVersionMismatch;
        }
        if digest != SCHEMA_SET_SHA256 {
            return ProducerIdentityStatus::SchemaSetMismatch;
        }
        ProducerIdentityStatus::ExactMatch
    }

    /// Returns whether the envelope has no payload entries.
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }
    /// Returns the total number of payload entries, including preserved entries.
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// Appends a generated payload or an [`ExternalPayload`].
    pub fn add<T: EnvelopeValue>(&mut self, value: T) {
        value.append_to(self);
    }

    /// Counts entries matched by a generated payload or external-key selector.
    pub fn count<S: Selector>(&self, selector: S) -> usize {
        selector.iter(self).count()
    }

    /// Returns whether at least one entry matches a selector.
    pub fn contains<S: Selector>(&self, selector: S) -> bool {
        self.get(selector, 0).is_some()
    }

    /// Returns the zero-based matching entry selected by payload type or external key.
    pub fn get<'a, S: Selector>(&'a self, selector: S, index: usize) -> Option<S::Item<'a>> {
        selector.iter(self).nth(index)
    }

    /// Iterates over all entries matched by a selector.
    pub fn for_each<S: Selector>(&self, selector: S) -> S::Iter<'_> {
        selector.iter(self)
    }

    /// Iterates over diagnostic views of every stored entry.
    pub fn entries(&self) -> impl Iterator<Item = EntryRef<'_>> {
        self.entries.iter().map(|entry| match entry {
            Entry::Known(data) => EntryRef::Known {
                type_name: known_type_name(data),
            },
            Entry::External { id, bytes } => EntryRef::External {
                key: ExternalKey(*id),
                bytes,
            },
            Entry::Unknown { id, bytes } => EntryRef::Unknown { id: *id, bytes },
            Entry::MalformedKnown {
                id,
                expected_type,
                bytes,
                error,
            } => EntryRef::MalformedKnown {
                id: *id,
                expected_type,
                bytes,
                error,
            },
        })
    }

    /// Serializes the envelope and stamps this generated SDK's producer identity.
    pub fn serialize(&self) -> Vec<u8> {
        let encoded: Vec<(u64, Vec<u8>)> = self
            .entries
            .iter()
            .map(|entry| match entry {
                Entry::Known(data) => encode_known(data),
                Entry::External { id, bytes }
                | Entry::Unknown { id, bytes }
                | Entry::MalformedKnown { id, bytes, .. } => (*id, bytes.clone()),
            })
            .collect();
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let payload_offsets: Vec<_> = encoded
            .iter()
            .map(|(id, bytes)| {
                let blob = builder.create_vector(bytes);
                wire::WirePayload::create(
                    &mut builder,
                    &wire::WirePayloadArgs {
                        id: *id,
                        blob: Some(blob),
                    },
                )
            })
            .collect();
        let payloads = builder.create_vector(&payload_offsets);
        let producer_name = builder.create_string(OPEN_PERCEPTION_KIT_NAME);
        let producer_version = builder.create_string(OPEN_PERCEPTION_KIT_VERSION);
        let producer_digest = builder.create_string(SCHEMA_SET_SHA256);
        let root = wire::WireEnvelope::create(
            &mut builder,
            &wire::WireEnvelopeArgs {
                payloads: Some(payloads),
                producer_sdk_name: Some(producer_name),
                producer_sdk_version: Some(producer_version),
                producer_schema_set_sha256: Some(producer_digest),
            },
        );
        wire::finish_wire_envelope_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }
}

fn valid_sdk_name(value: &str) -> bool {
    let mut chars = value.chars();
    matches!(chars.next(), Some('a'..='z'))
        && chars.all(|character| {
            character.is_ascii_lowercase() || character.is_ascii_digit() || character == '_'
        })
}

fn valid_semver(value: &str) -> bool {
    let parts: Vec<_> = value.split('.').collect();
    parts.len() == 3
        && parts.iter().all(|part| {
            !part.is_empty()
                && part.chars().all(|character| character.is_ascii_digit())
                && (part == &"0" || !part.starts_with('0'))
        })
}

fn valid_sha256(value: &str) -> bool {
    value.len() == 64
        && value
            .chars()
            .all(|character| character.is_ascii_hexdigit() && !character.is_ascii_uppercase())
}

fn encode_known(data: &PayloadData) -> (u64, Vec<u8>) {
    match data {
        PayloadData::Payload0(value) => (556103652012567315, value.encode_payload()),
        PayloadData::Payload1(value) => (2852697023809600655, value.encode_payload()),
        PayloadData::Payload2(value) => (2065478860695108412, value.encode_payload()),
        PayloadData::Payload3(value) => (5972661533224817501, value.encode_payload()),
        PayloadData::Payload4(value) => (2960585987463094496, value.encode_payload()),
        PayloadData::Payload5(value) => (7749401259036278028, value.encode_payload()),
        PayloadData::Payload6(value) => (9114952555105892553, value.encode_payload()),
        PayloadData::Payload7(value) => (1998909987238011535, value.encode_payload()),
        PayloadData::Payload8(value) => (238211229389337861, value.encode_payload()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn assert_send_sync_static<T: Send + Sync + 'static>() {}

    fn raw_packet(id: u64, bytes: &[u8]) -> Vec<u8> {
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let blob = builder.create_vector(bytes);
        let payload = wire::WirePayload::create(
            &mut builder,
            &wire::WirePayloadArgs {
                id,
                blob: Some(blob),
            },
        );
        let payloads = builder.create_vector(&[payload]);
        let producer_name = builder.create_string(OPEN_PERCEPTION_KIT_NAME);
        let producer_version = builder.create_string(OPEN_PERCEPTION_KIT_VERSION);
        let producer_digest = builder.create_string(SCHEMA_SET_SHA256);
        let root = wire::WireEnvelope::create(
            &mut builder,
            &wire::WireEnvelopeArgs {
                payloads: Some(payloads),
                producer_sdk_name: Some(producer_name),
                producer_sdk_version: Some(producer_version),
                producer_schema_set_sha256: Some(producer_digest),
            },
        );
        wire::finish_wire_envelope_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }

    #[test]
    fn owned_roundtrip_and_external_payloads() {
        assert_send_sync_static::<Envelope>();
        assert_send_sync_static::<EnvelopeDecodeError>();
        assert_send_sync_static::<crate::fb::open_perception_kit::metadata::BoxDetectionsT>();

        let key = external_key("com.example.generated-test");
        let known = payload::<crate::fb::open_perception_kit::metadata::BoxDetectionsT>();
        let mut envelope = Envelope::new();
        envelope.add(crate::fb::open_perception_kit::metadata::BoxDetectionsT::default());
        envelope.add(external_payload(key, b"external".to_vec()));
        let decoded = Envelope::decode(envelope.serialize()).expect("valid round trip");
        assert_eq!(decoded.count(known), 1);
        assert!(decoded.contains(known));
        assert!(decoded.get(known, 0).is_some());
        assert_eq!(decoded.count(key), 1);
        assert!(decoded.contains(key));
        assert_eq!(decoded.get(key, 0), Some(b"external".as_slice()));
        assert_eq!(decoded.for_each(key).collect::<Vec<_>>(), vec![b"external"]);
        assert_eq!(
            decoded.producer_identity(),
            ProducerIdentityStatus::ExactMatch
        );
    }

    #[test]
    fn unknown_and_malformed_known_payloads_are_preserved() {
        let unknown_bytes = b"future-payload";
        let unknown = Envelope::decode(raw_packet(1, unknown_bytes))
            .expect("unknown payload keeps envelope valid");
        assert!(matches!(unknown.entries().next(), Some(EntryRef::Unknown {
            id: 1, bytes
        }) if bytes == unknown_bytes));
        let unknown_roundtrip = Envelope::decode(unknown.serialize()).expect("unknown round trip");
        assert!(
            matches!(unknown_roundtrip.entries().next(), Some(EntryRef::Unknown {
            id: 1, bytes
        }) if bytes == unknown_bytes)
        );

        let malformed_bytes = b"not-a-flatbuffer";
        let malformed = Envelope::decode(raw_packet(556103652012567315, malformed_bytes))
            .expect("malformed known payload keeps envelope valid");
        assert!(
            matches!(malformed.entries().next(), Some(EntryRef::MalformedKnown {
            id: 556103652012567315, bytes, ..
        }) if bytes == malformed_bytes)
        );
        let malformed_roundtrip =
            Envelope::decode(malformed.serialize()).expect("malformed known round trip");
        assert!(
            matches!(malformed_roundtrip.entries().next(), Some(EntryRef::MalformedKnown {
            id: 556103652012567315, bytes, ..
        }) if bytes == malformed_bytes)
        );
    }

    #[test]
    fn malformed_envelope_returns_original_bytes_without_panicking() {
        for bytes in [Vec::new(), b"bad".to_vec(), vec![0; 32]] {
            let original = bytes.clone();
            let result = std::panic::catch_unwind(|| Envelope::decode(bytes));
            let error = result
                .expect("decode must not panic")
                .expect_err("invalid packet");
            assert_eq!(error.bytes(), original);
        }
    }
}
