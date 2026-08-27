// Copyright (C) 2026 Arm Limited. All rights reserved.
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.

        #![allow(dead_code, unused_imports)]

        pub mod perception;

        pub mod fb {
    pub use crate::perception;
}

        use std::error::Error;
        use std::fmt;

        use crate::perception::internalfb as wire;

        pub const PERCEPTION_NAME: &str = "perception";
        pub const PERCEPTION_VERSION: &str = "0.2.1";
        pub const SCHEMA_SET_SHA256: &str = "0ba6dfe959e1453ce12c7a8707623bc15d94d52c9235c26f7e27f31dda0775c5";
        pub const FLATBUFFERS_VERSION_REQUIREMENT: &str = "==25.9.23";
        pub const EXTERNAL_KEY_MIN: u64 = 9223372036854775808;

        const EXTERNAL_HASH_OFFSET: u64 = 14_695_981_039_346_656_037;
        const EXTERNAL_HASH_PRIME: u64 = 1_099_511_628_211;

        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        pub enum ProducerIdentityStatus {
            ExactMatch,
            Missing,
            Malformed,
            SdkNameMismatch,
            SdkVersionMismatch,
            SchemaSetMismatch,
        }

        #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
        pub struct ExternalKey(u64);

        impl ExternalKey {
            pub fn value(self) -> u64 { self.0 }
        }

        pub fn external_key(key: &str) -> ExternalKey {
            let mut value = EXTERNAL_HASH_OFFSET;
            for byte in key.as_bytes() {
                value ^= u64::from(*byte);
                value = value.wrapping_mul(EXTERNAL_HASH_PRIME);
            }
            ExternalKey((value & (EXTERNAL_KEY_MIN - 1)) | EXTERNAL_KEY_MIN)
        }

        pub fn is_external_key(value: u64) -> bool { value >= EXTERNAL_KEY_MIN }

        #[derive(Debug, Clone, PartialEq, Eq)]
        pub enum PayloadDecodeError {
            InvalidIdentifier { expected: &'static str },
            InvalidFlatbuffer(String),
        }

        impl fmt::Display for PayloadDecodeError {
            fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
                match self {
                    Self::InvalidIdentifier { expected } =>
                        write!(formatter, "invalid payload file identifier; expected {expected}"),
                    Self::InvalidFlatbuffer(error) => formatter.write_str(error),
                }
            }
        }

        impl Error for PayloadDecodeError {}

        #[derive(Debug, Clone, PartialEq, Eq)]
        pub enum EnvelopeDecodeErrorKind {
            InvalidIdentifier,
            InvalidFlatbuffer(String),
        }

        #[derive(Debug, Clone, PartialEq, Eq)]
        pub struct EnvelopeDecodeError {
            kind: EnvelopeDecodeErrorKind,
            bytes: Vec<u8>,
        }

        impl EnvelopeDecodeError {
            pub fn kind(&self) -> &EnvelopeDecodeErrorKind { &self.kind }
            pub fn bytes(&self) -> &[u8] { &self.bytes }
            pub fn into_bytes(self) -> Vec<u8> { self.bytes }
        }

        impl fmt::Display for EnvelopeDecodeError {
            fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
                match &self.kind {
                    EnvelopeDecodeErrorKind::InvalidIdentifier =>
                        formatter.write_str("invalid envelope file identifier; expected FLWD"),
                    EnvelopeDecodeErrorKind::InvalidFlatbuffer(error) => formatter.write_str(error),
                }
            }
        }

        impl Error for EnvelopeDecodeError {}

        #[doc(hidden)]
#[derive(Debug, Clone, PartialEq)]
pub enum PayloadData {
    Payload127096183275957372(crate::perception::metadata::BoxDetectionsT),
    Payload9181357636124419217(crate::perception::metadata::ClassificationsT),
    Payload6787725252958650128(crate::perception::metadata::FrameContextT),
    Payload3601053540183530964(crate::perception::metadata::ObjectEmbeddingsT),
    Payload1204340903431744882(crate::perception::metadata::ObjectTracksT),
    Payload4179744154867129599(crate::perception::metadata::PerformanceOverlayT),
    Payload6089861490284108552(crate::perception::metadata::PoseEstimationsT),
    Payload3767952910034633902(crate::perception::metadata::SegmentationMasksT),
    Payload4937615646931894804(crate::perception::metadata::TrackTracesT),
}

        #[derive(Debug, Clone, PartialEq)]
        enum Entry {
            Known(PayloadData),
            External { id: u64, bytes: Vec<u8> },
            Unknown { id: u64, bytes: Vec<u8> },
            MalformedKnown {
                id: u64,
                expected_type: &'static str,
                bytes: Vec<u8>,
                error: PayloadDecodeError,
            },
        }

        pub enum EntryRef<'a> {
            Known { id: u64, type_name: &'static str },
            External { id: u64, bytes: &'a [u8] },
            Unknown { id: u64, bytes: &'a [u8] },
            MalformedKnown {
                id: u64,
                expected_type: &'static str,
                bytes: &'a [u8],
                error: &'a PayloadDecodeError,
            },
        }

        pub trait NativePayload: Sized + Send + Sync + 'static {
            const ID: u64;
            const NAME: &'static str;
            #[doc(hidden)] fn into_payload_data(self) -> PayloadData;
            #[doc(hidden)] fn from_payload_data(data: &PayloadData) -> Option<&Self>;
            #[doc(hidden)] fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError>;
            #[doc(hidden)] fn encode_payload(&self) -> Vec<u8>;
        }

        impl NativePayload for crate::perception::metadata::BoxDetectionsT {
    const ID: u64 = 127096183275957372;
    const NAME: &'static str = "perception.metadata.BoxDetections";

    fn into_payload_data(self) -> PayloadData {
        PayloadData::Payload127096183275957372(self)
    }

    fn from_payload_data(data: &PayloadData) -> Option<&Self> {
        if let PayloadData::Payload127096183275957372(value) = data { Some(value) } else { None }
    }

    fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError> {
        use crate::perception::metadata as generated;
        if bytes.len() < 8 || !generated::box_detections_buffer_has_identifier(bytes) {
            return Err(PayloadDecodeError::InvalidIdentifier {
                expected: "BDET",
            });
        }
        generated::root_as_box_detections(bytes)
            .map(|value| value.unpack())
            .map_err(|error| PayloadDecodeError::InvalidFlatbuffer(error.to_string()))
    }

    fn encode_payload(&self) -> Vec<u8> {
        use crate::perception::metadata as generated;
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let root = self.pack(&mut builder);
        generated::finish_box_detections_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }
}

impl NativePayload for crate::perception::metadata::ClassificationsT {
    const ID: u64 = 9181357636124419217;
    const NAME: &'static str = "perception.metadata.Classifications";

    fn into_payload_data(self) -> PayloadData {
        PayloadData::Payload9181357636124419217(self)
    }

    fn from_payload_data(data: &PayloadData) -> Option<&Self> {
        if let PayloadData::Payload9181357636124419217(value) = data { Some(value) } else { None }
    }

    fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError> {
        use crate::perception::metadata as generated;
        if bytes.len() < 8 || !generated::classifications_buffer_has_identifier(bytes) {
            return Err(PayloadDecodeError::InvalidIdentifier {
                expected: "CLSF",
            });
        }
        generated::root_as_classifications(bytes)
            .map(|value| value.unpack())
            .map_err(|error| PayloadDecodeError::InvalidFlatbuffer(error.to_string()))
    }

    fn encode_payload(&self) -> Vec<u8> {
        use crate::perception::metadata as generated;
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let root = self.pack(&mut builder);
        generated::finish_classifications_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }
}

impl NativePayload for crate::perception::metadata::FrameContextT {
    const ID: u64 = 6787725252958650128;
    const NAME: &'static str = "perception.metadata.FrameContext";

    fn into_payload_data(self) -> PayloadData {
        PayloadData::Payload6787725252958650128(self)
    }

    fn from_payload_data(data: &PayloadData) -> Option<&Self> {
        if let PayloadData::Payload6787725252958650128(value) = data { Some(value) } else { None }
    }

    fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError> {
        use crate::perception::metadata as generated;
        if bytes.len() < 8 || !generated::frame_context_buffer_has_identifier(bytes) {
            return Err(PayloadDecodeError::InvalidIdentifier {
                expected: "FCTX",
            });
        }
        generated::root_as_frame_context(bytes)
            .map(|value| value.unpack())
            .map_err(|error| PayloadDecodeError::InvalidFlatbuffer(error.to_string()))
    }

    fn encode_payload(&self) -> Vec<u8> {
        use crate::perception::metadata as generated;
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let root = self.pack(&mut builder);
        generated::finish_frame_context_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }
}

impl NativePayload for crate::perception::metadata::ObjectEmbeddingsT {
    const ID: u64 = 3601053540183530964;
    const NAME: &'static str = "perception.metadata.ObjectEmbeddings";

    fn into_payload_data(self) -> PayloadData {
        PayloadData::Payload3601053540183530964(self)
    }

    fn from_payload_data(data: &PayloadData) -> Option<&Self> {
        if let PayloadData::Payload3601053540183530964(value) = data { Some(value) } else { None }
    }

    fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError> {
        use crate::perception::metadata as generated;
        if bytes.len() < 8 || !generated::object_embeddings_buffer_has_identifier(bytes) {
            return Err(PayloadDecodeError::InvalidIdentifier {
                expected: "EMBE",
            });
        }
        generated::root_as_object_embeddings(bytes)
            .map(|value| value.unpack())
            .map_err(|error| PayloadDecodeError::InvalidFlatbuffer(error.to_string()))
    }

    fn encode_payload(&self) -> Vec<u8> {
        use crate::perception::metadata as generated;
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let root = self.pack(&mut builder);
        generated::finish_object_embeddings_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }
}

impl NativePayload for crate::perception::metadata::ObjectTracksT {
    const ID: u64 = 1204340903431744882;
    const NAME: &'static str = "perception.metadata.ObjectTracks";

    fn into_payload_data(self) -> PayloadData {
        PayloadData::Payload1204340903431744882(self)
    }

    fn from_payload_data(data: &PayloadData) -> Option<&Self> {
        if let PayloadData::Payload1204340903431744882(value) = data { Some(value) } else { None }
    }

    fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError> {
        use crate::perception::metadata as generated;
        if bytes.len() < 8 || !generated::object_tracks_buffer_has_identifier(bytes) {
            return Err(PayloadDecodeError::InvalidIdentifier {
                expected: "TRKS",
            });
        }
        generated::root_as_object_tracks(bytes)
            .map(|value| value.unpack())
            .map_err(|error| PayloadDecodeError::InvalidFlatbuffer(error.to_string()))
    }

    fn encode_payload(&self) -> Vec<u8> {
        use crate::perception::metadata as generated;
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let root = self.pack(&mut builder);
        generated::finish_object_tracks_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }
}

impl NativePayload for crate::perception::metadata::PerformanceOverlayT {
    const ID: u64 = 4179744154867129599;
    const NAME: &'static str = "perception.metadata.PerformanceOverlay";

    fn into_payload_data(self) -> PayloadData {
        PayloadData::Payload4179744154867129599(self)
    }

    fn from_payload_data(data: &PayloadData) -> Option<&Self> {
        if let PayloadData::Payload4179744154867129599(value) = data { Some(value) } else { None }
    }

    fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError> {
        use crate::perception::metadata as generated;
        if bytes.len() < 8 || !generated::performance_overlay_buffer_has_identifier(bytes) {
            return Err(PayloadDecodeError::InvalidIdentifier {
                expected: "PERF",
            });
        }
        generated::root_as_performance_overlay(bytes)
            .map(|value| value.unpack())
            .map_err(|error| PayloadDecodeError::InvalidFlatbuffer(error.to_string()))
    }

    fn encode_payload(&self) -> Vec<u8> {
        use crate::perception::metadata as generated;
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let root = self.pack(&mut builder);
        generated::finish_performance_overlay_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }
}

impl NativePayload for crate::perception::metadata::PoseEstimationsT {
    const ID: u64 = 6089861490284108552;
    const NAME: &'static str = "perception.metadata.PoseEstimations";

    fn into_payload_data(self) -> PayloadData {
        PayloadData::Payload6089861490284108552(self)
    }

    fn from_payload_data(data: &PayloadData) -> Option<&Self> {
        if let PayloadData::Payload6089861490284108552(value) = data { Some(value) } else { None }
    }

    fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError> {
        use crate::perception::metadata as generated;
        if bytes.len() < 8 || !generated::pose_estimations_buffer_has_identifier(bytes) {
            return Err(PayloadDecodeError::InvalidIdentifier {
                expected: "POSE",
            });
        }
        generated::root_as_pose_estimations(bytes)
            .map(|value| value.unpack())
            .map_err(|error| PayloadDecodeError::InvalidFlatbuffer(error.to_string()))
    }

    fn encode_payload(&self) -> Vec<u8> {
        use crate::perception::metadata as generated;
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let root = self.pack(&mut builder);
        generated::finish_pose_estimations_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }
}

impl NativePayload for crate::perception::metadata::SegmentationMasksT {
    const ID: u64 = 3767952910034633902;
    const NAME: &'static str = "perception.metadata.SegmentationMasks";

    fn into_payload_data(self) -> PayloadData {
        PayloadData::Payload3767952910034633902(self)
    }

    fn from_payload_data(data: &PayloadData) -> Option<&Self> {
        if let PayloadData::Payload3767952910034633902(value) = data { Some(value) } else { None }
    }

    fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError> {
        use crate::perception::metadata as generated;
        if bytes.len() < 8 || !generated::segmentation_masks_buffer_has_identifier(bytes) {
            return Err(PayloadDecodeError::InvalidIdentifier {
                expected: "SGMS",
            });
        }
        generated::root_as_segmentation_masks(bytes)
            .map(|value| value.unpack())
            .map_err(|error| PayloadDecodeError::InvalidFlatbuffer(error.to_string()))
    }

    fn encode_payload(&self) -> Vec<u8> {
        use crate::perception::metadata as generated;
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let root = self.pack(&mut builder);
        generated::finish_segmentation_masks_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }
}

impl NativePayload for crate::perception::metadata::TrackTracesT {
    const ID: u64 = 4937615646931894804;
    const NAME: &'static str = "perception.metadata.TrackTraces";

    fn into_payload_data(self) -> PayloadData {
        PayloadData::Payload4937615646931894804(self)
    }

    fn from_payload_data(data: &PayloadData) -> Option<&Self> {
        if let PayloadData::Payload4937615646931894804(value) = data { Some(value) } else { None }
    }

    fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError> {
        use crate::perception::metadata as generated;
        if bytes.len() < 8 || !generated::track_traces_buffer_has_identifier(bytes) {
            return Err(PayloadDecodeError::InvalidIdentifier {
                expected: "TRCE",
            });
        }
        generated::root_as_track_traces(bytes)
            .map(|value| value.unpack())
            .map_err(|error| PayloadDecodeError::InvalidFlatbuffer(error.to_string()))
    }

    fn encode_payload(&self) -> Vec<u8> {
        use crate::perception::metadata as generated;
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let root = self.pack(&mut builder);
        generated::finish_track_traces_buffer(&mut builder, root);
        builder.finished_data().to_vec()
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
        127096183275957372 => decode_entry::<crate::perception::metadata::BoxDetectionsT>(id, bytes),
9181357636124419217 => decode_entry::<crate::perception::metadata::ClassificationsT>(id, bytes),
6787725252958650128 => decode_entry::<crate::perception::metadata::FrameContextT>(id, bytes),
3601053540183530964 => decode_entry::<crate::perception::metadata::ObjectEmbeddingsT>(id, bytes),
1204340903431744882 => decode_entry::<crate::perception::metadata::ObjectTracksT>(id, bytes),
4179744154867129599 => decode_entry::<crate::perception::metadata::PerformanceOverlayT>(id, bytes),
6089861490284108552 => decode_entry::<crate::perception::metadata::PoseEstimationsT>(id, bytes),
3767952910034633902 => decode_entry::<crate::perception::metadata::SegmentationMasksT>(id, bytes),
4937615646931894804 => decode_entry::<crate::perception::metadata::TrackTracesT>(id, bytes),
        _ if id >= EXTERNAL_KEY_MIN => Entry::External { id, bytes },
        _ => Entry::Unknown { id, bytes },
    }
}


        fn known_identity(data: &PayloadData) -> (u64, &'static str) {
    match data {
            PayloadData::Payload127096183275957372(_) => (127096183275957372, "perception.metadata.BoxDetections"),
    PayloadData::Payload9181357636124419217(_) => (9181357636124419217, "perception.metadata.Classifications"),
    PayloadData::Payload6787725252958650128(_) => (6787725252958650128, "perception.metadata.FrameContext"),
    PayloadData::Payload3601053540183530964(_) => (3601053540183530964, "perception.metadata.ObjectEmbeddings"),
    PayloadData::Payload1204340903431744882(_) => (1204340903431744882, "perception.metadata.ObjectTracks"),
    PayloadData::Payload4179744154867129599(_) => (4179744154867129599, "perception.metadata.PerformanceOverlay"),
    PayloadData::Payload6089861490284108552(_) => (6089861490284108552, "perception.metadata.PoseEstimations"),
    PayloadData::Payload3767952910034633902(_) => (3767952910034633902, "perception.metadata.SegmentationMasks"),
    PayloadData::Payload4937615646931894804(_) => (4937615646931894804, "perception.metadata.TrackTraces"),
    }
}


        #[derive(Debug, Clone, PartialEq)]
        pub struct Envelope {
            entries: Vec<Entry>,
            producer_sdk_name: String,
            producer_sdk_version: String,
            producer_schema_set_sha256: String,
        }

        impl Default for Envelope {
            fn default() -> Self { Self::new() }
        }

        impl Envelope {
            pub fn new() -> Self {
                Self {
                    entries: Vec::new(),
                    producer_sdk_name: PERCEPTION_NAME.to_owned(),
                    producer_sdk_version: PERCEPTION_VERSION.to_owned(),
                    producer_schema_set_sha256: SCHEMA_SET_SHA256.to_owned(),
                }
            }

            pub fn decode(bytes: Vec<u8>) -> Result<Self, EnvelopeDecodeError> {
                if bytes.len() < 8 || !wire::wire_envelope_buffer_has_identifier(&bytes) {
                    return Err(EnvelopeDecodeError {
                        kind: EnvelopeDecodeErrorKind::InvalidIdentifier,
                        bytes,
                    });
                }
                let envelope = match wire::root_as_wire_envelope(&bytes) {
                    Ok(envelope) => envelope,
                    Err(error) => return Err(EnvelopeDecodeError {
                        kind: EnvelopeDecodeErrorKind::InvalidFlatbuffer(error.to_string()),
                        bytes,
                    }),
                };
                let entries = envelope.payloads().map(|payloads| {
                    payloads.iter().map(|payload| {
                        let blob = payload.blob()
                            .map(|value| value.iter().collect())
                            .unwrap_or_default();
                        decode_known(payload.id(), blob)
                    }).collect()
                }).unwrap_or_default();
                Ok(Self {
                    entries,
                    producer_sdk_name: envelope.producer_sdk_name().unwrap_or_default().to_owned(),
                    producer_sdk_version: envelope.producer_sdk_version().unwrap_or_default().to_owned(),
                    producer_schema_set_sha256: envelope.producer_schema_set_sha256().unwrap_or_default().to_owned(),
                })
            }

            pub fn producer_sdk_name(&self) -> &str { &self.producer_sdk_name }
            pub fn producer_sdk_version(&self) -> &str { &self.producer_sdk_version }
            pub fn producer_schema_set_sha256(&self) -> &str { &self.producer_schema_set_sha256 }

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
                if name != PERCEPTION_NAME { return ProducerIdentityStatus::SdkNameMismatch; }
                if version != PERCEPTION_VERSION { return ProducerIdentityStatus::SdkVersionMismatch; }
                if digest != SCHEMA_SET_SHA256 { return ProducerIdentityStatus::SchemaSetMismatch; }
                ProducerIdentityStatus::ExactMatch
            }

            pub fn is_empty(&self) -> bool { self.entries.is_empty() }
            pub fn len(&self) -> usize { self.entries.len() }

            pub fn add<T: NativePayload>(&mut self, value: T) {
                self.entries.push(Entry::Known(value.into_payload_data()));
            }

            pub fn add_external(&mut self, key: ExternalKey, bytes: impl Into<Vec<u8>>) {
                self.entries.push(Entry::External { id: key.value(), bytes: bytes.into() });
            }

            pub fn count<T: NativePayload>(&self) -> usize {
                self.iter::<T>().count()
            }

            pub fn contains<T: NativePayload>(&self) -> bool { self.get::<T>(0).is_some() }

            pub fn get<T: NativePayload>(&self, index: usize) -> Option<&T> {
                self.iter::<T>().nth(index)
            }

            pub fn iter<T: NativePayload>(&self) -> impl Iterator<Item = &T> {
                self.entries.iter().filter_map(|entry| match entry {
                    Entry::Known(data) => T::from_payload_data(data),
                    _ => None,
                })
            }

            pub fn external_count(&self, key: ExternalKey) -> usize {
                self.iter_external(key).count()
            }

            pub fn get_external(&self, key: ExternalKey, index: usize) -> Option<&[u8]> {
                self.iter_external(key).nth(index)
            }

            pub fn iter_external(&self, key: ExternalKey) -> impl Iterator<Item = &[u8]> {
                self.entries.iter().filter_map(move |entry| match entry {
                    Entry::External { id, bytes } if *id == key.value() => Some(bytes.as_slice()),
                    _ => None,
                })
            }

            pub fn entries(&self) -> impl Iterator<Item = EntryRef<'_>> {
                self.entries.iter().map(|entry| match entry {
                    Entry::Known(data) => {
                        let (id, type_name) = known_identity(data);
                        EntryRef::Known { id, type_name }
                    },
                    Entry::External { id, bytes } => EntryRef::External { id: *id, bytes },
                    Entry::Unknown { id, bytes } => EntryRef::Unknown { id: *id, bytes },
                    Entry::MalformedKnown { id, expected_type, bytes, error } =>
                        EntryRef::MalformedKnown {
                            id: *id,
                            expected_type,
                            bytes,
                            error,
                        },
                })
            }

            pub fn serialize(&self) -> Vec<u8> {
                let encoded: Vec<(u64, Vec<u8>)> = self.entries.iter().map(|entry| match entry {
                    Entry::Known(data) => encode_known(data),
                    Entry::External { id, bytes } |
                    Entry::Unknown { id, bytes } |
                    Entry::MalformedKnown { id, bytes, .. } => (*id, bytes.clone()),
                }).collect();
                let mut builder = flatbuffers::FlatBufferBuilder::new();
                let payload_offsets: Vec<_> = encoded.iter().map(|(id, bytes)| {
                    let blob = builder.create_vector(bytes);
                    wire::WirePayload::create(&mut builder, &wire::WirePayloadArgs {
                        id: *id,
                        blob: Some(blob),
                    })
                }).collect();
                let payloads = builder.create_vector(&payload_offsets);
                let producer_name = builder.create_string(PERCEPTION_NAME);
                let producer_version = builder.create_string(PERCEPTION_VERSION);
                let producer_digest = builder.create_string(SCHEMA_SET_SHA256);
                let root = wire::WireEnvelope::create(&mut builder, &wire::WireEnvelopeArgs {
                    payloads: Some(payloads),
                    producer_sdk_name: Some(producer_name),
                    producer_sdk_version: Some(producer_version),
                    producer_schema_set_sha256: Some(producer_digest),
                });
                wire::finish_wire_envelope_buffer(&mut builder, root);
                builder.finished_data().to_vec()
            }
        }

        fn valid_sdk_name(value: &str) -> bool {
            let mut chars = value.chars();
            matches!(chars.next(), Some('a'..='z'))
                && chars.all(|character| character.is_ascii_lowercase()
                    || character.is_ascii_digit() || character == '_')
        }

        fn valid_semver(value: &str) -> bool {
            let parts: Vec<_> = value.split('.').collect();
            parts.len() == 3 && parts.iter().all(|part| {
                !part.is_empty()
                    && part.chars().all(|character| character.is_ascii_digit())
                    && (part == &"0" || !part.starts_with('0'))
            })
        }

        fn valid_sha256(value: &str) -> bool {
            value.len() == 64 && value.chars().all(|character| character.is_ascii_hexdigit()
                && !character.is_ascii_uppercase())
        }

fn encode_known(data: &PayloadData) -> (u64, Vec<u8>) {
    match data {
        PayloadData::Payload127096183275957372(value) => (127096183275957372, value.encode_payload()),
PayloadData::Payload9181357636124419217(value) => (9181357636124419217, value.encode_payload()),
PayloadData::Payload6787725252958650128(value) => (6787725252958650128, value.encode_payload()),
PayloadData::Payload3601053540183530964(value) => (3601053540183530964, value.encode_payload()),
PayloadData::Payload1204340903431744882(value) => (1204340903431744882, value.encode_payload()),
PayloadData::Payload4179744154867129599(value) => (4179744154867129599, value.encode_payload()),
PayloadData::Payload6089861490284108552(value) => (6089861490284108552, value.encode_payload()),
PayloadData::Payload3767952910034633902(value) => (3767952910034633902, value.encode_payload()),
PayloadData::Payload4937615646931894804(value) => (4937615646931894804, value.encode_payload()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn assert_send_sync_static<T: Send + Sync + 'static>() {}

    fn raw_packet(id: u64, bytes: &[u8]) -> Vec<u8> {
        let mut builder = flatbuffers::FlatBufferBuilder::new();
        let blob = builder.create_vector(bytes);
        let payload = wire::WirePayload::create(&mut builder, &wire::WirePayloadArgs {
            id,
            blob: Some(blob),
        });
        let payloads = builder.create_vector(&[payload]);
        let producer_name = builder.create_string(PERCEPTION_NAME);
        let producer_version = builder.create_string(PERCEPTION_VERSION);
        let producer_digest = builder.create_string(SCHEMA_SET_SHA256);
        let root = wire::WireEnvelope::create(&mut builder, &wire::WireEnvelopeArgs {
            payloads: Some(payloads),
            producer_sdk_name: Some(producer_name),
            producer_sdk_version: Some(producer_version),
            producer_schema_set_sha256: Some(producer_digest),
        });
        wire::finish_wire_envelope_buffer(&mut builder, root);
        builder.finished_data().to_vec()
    }

    #[test]
    fn owned_roundtrip_and_external_payloads() {
        assert_send_sync_static::<Envelope>();
        assert_send_sync_static::<EnvelopeDecodeError>();
        assert_send_sync_static::<crate::perception::metadata::BoxDetectionsT>();

        let key = external_key("com.example.generated-test");
        let mut envelope = Envelope::new();
        envelope.add(crate::perception::metadata::BoxDetectionsT::default());
        envelope.add_external(key, b"external".to_vec());
        let decoded = Envelope::decode(envelope.serialize()).expect("valid round trip");
        assert_eq!(decoded.count::<crate::perception::metadata::BoxDetectionsT>(), 1);
        assert!(decoded.get::<crate::perception::metadata::BoxDetectionsT>(0).is_some());
        assert_eq!(decoded.get_external(key, 0), Some(b"external".as_slice()));
        assert_eq!(decoded.producer_identity(), ProducerIdentityStatus::ExactMatch);
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
        assert!(matches!(unknown_roundtrip.entries().next(), Some(EntryRef::Unknown {
            id: 1, bytes
        }) if bytes == unknown_bytes));

        let malformed_bytes = b"not-a-flatbuffer";
        let malformed = Envelope::decode(raw_packet(127096183275957372, malformed_bytes))
            .expect("malformed known payload keeps envelope valid");
        assert!(matches!(malformed.entries().next(), Some(EntryRef::MalformedKnown {
            id: 127096183275957372, bytes, ..
        }) if bytes == malformed_bytes));
        let malformed_roundtrip = Envelope::decode(malformed.serialize())
            .expect("malformed known round trip");
        assert!(matches!(malformed_roundtrip.entries().next(), Some(EntryRef::MalformedKnown {
            id: 127096183275957372, bytes, ..
        }) if bytes == malformed_bytes));
    }

    #[test]
    fn malformed_envelope_returns_original_bytes_without_panicking() {
        for bytes in [Vec::new(), b"bad".to_vec(), vec![0; 32]] {
            let original = bytes.clone();
            let result = std::panic::catch_unwind(|| Envelope::decode(bytes));
            let error = result.expect("decode must not panic").expect_err("invalid packet");
            assert_eq!(error.bytes(), original);
        }
    }
}
