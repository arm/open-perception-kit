################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import textwrap
from dataclasses import dataclass
from pathlib import Path

from ..errors import fail
from ..schema_set import schema_set_sha256
from ..types import RESERVED_PAYLOAD_ID_MIN, GenerationContext, SchemaEntry
from .identifiers import RUST_KEYWORDS


RUST_GENERATED_BANNER = (
    "// Generated file. Do not edit.\n"
    "// SDK users: change schemas or generator inputs, then regenerate this file.\n\n"
)

ENVELOPE_SCHEMA_TEMPLATE = """namespace __SDK_NAME__.internalfb;

table WirePayload {
  id:ulong;
  blob:[ubyte];
}

table WireEnvelope {
  payloads:[WirePayload];
  producer_sdk_name:string;
  producer_sdk_version:string;
  producer_schema_set_sha256:string;
}

root_type WireEnvelope;
file_identifier "FLWD";
"""


def _rust_identifier(value: str) -> str:
    return f"{value}_" if value in RUST_KEYWORDS else value


@dataclass(frozen=True)
class RustPayloadBinding:
    module_path: str
    object_type: str
    root_function_suffix: str

    @property
    def type_path(self) -> str:
        return f"crate::fb::{self.module_path}::{self.object_type}"


def _rust_payload_bindings(
    entries: list[SchemaEntry], flatbuffers_root: Path
) -> dict[str, RustPayloadBinding]:
    generated_sources = sorted(flatbuffers_root.rglob("*_generated.rs"))
    bindings: dict[str, RustPayloadBinding] = {}
    for entry in entries:
        identifier_pattern = re.compile(
            rf'pub const [A-Za-z_][A-Za-z0-9_]*_IDENTIFIER: &str = "{re.escape(entry.file_identifier)}";'
        )
        matches = [
            (source, text)
            for source in generated_sources
            if identifier_pattern.search(text := source.read_text(encoding="utf-8"))
        ]
        if len(matches) != 1:
            fail(
                f"cannot identify generated Rust root for {entry.qualified_root_type}: "
                f"expected one source with file identifier {entry.file_identifier}, found {len(matches)}"
            )
        source, text = matches[0]
        root_match = re.search(
            r"pub fn root_as_([A-Za-z_][A-Za-z0-9_]*)\s*\([^)]*\)\s*"
            r"->\s*Result<([A-Za-z_][A-Za-z0-9_]*)",
            text,
            re.DOTALL,
        )
        if root_match is None:
            fail(f"cannot identify generated Rust root symbols in {source}")
        module_path = "::".join(source.parent.relative_to(flatbuffers_root).parts)
        bindings[entry.qualified_root_type] = RustPayloadBinding(
            module_path=module_path,
            object_type=f"{root_match.group(2)}T",
            root_function_suffix=root_match.group(1),
        )
    return bindings


def _run_flatc_rust(
    schemas: list[Path], output_dir: Path, flatc: str, include_dirs: list[Path]
) -> None:
    command = [
        flatc,
        "--rust",
        "--gen-object-api",
        "--rust-module-root-file",
        "-o",
        str(output_dir),
    ]
    for include_dir in include_dirs:
        command.extend(["-I", str(include_dir)])
    command.extend(str(schema) for schema in schemas)
    try:
        subprocess.run(command, check=True, text=True, capture_output=True)
    except FileNotFoundError:
        fail(f"flatc not found: {flatc}")
    except subprocess.CalledProcessError as exc:
        details = "\n".join(
            part.strip() for part in (exc.stdout or "", exc.stderr or "") if part.strip()
        )
        fail(
            f"flatc failed while generating Rust SDK (exit code {exc.returncode})"
            + (f"\n{details}" if details else "")
        )


def _format_rust_crate(root: Path) -> None:
    rustfmt = shutil.which("rustfmt")
    if rustfmt is None:
        fail("rustfmt is required to format the generated Rust SDK")
    sources = sorted(root.rglob("*.rs"))
    try:
        subprocess.run(
            [rustfmt, "--edition", "2021", *map(str, sources)],
            check=True,
            text=True,
            capture_output=True,
        )
    except subprocess.CalledProcessError as exc:
        details = "\n".join(
            part.strip() for part in (exc.stdout or "", exc.stderr or "") if part.strip()
        )
        fail(
            f"rustfmt failed while formatting the Rust SDK (exit code {exc.returncode})"
            + (f"\n{details}" if details else "")
        )


def _normalize_rust_module_directories(root: Path) -> None:
    directories = sorted(
        (path for path in root.rglob("*") if path.is_dir()),
        key=lambda path: len(path.parts),
        reverse=True,
    )
    for directory in directories:
        escaped_name = _rust_identifier(directory.name)
        if escaped_name == directory.name:
            continue
        target = directory.with_name(escaped_name)
        if target.exists():
            fail(
                f"Rust module name collision after escaping keyword '{directory.name}': "
                f"{directory} and {target}"
            )
        directory.rename(target)


def _cargo_toml(ctx: GenerationContext) -> str:
    return textwrap.dedent(
        f"""\
        [package]
        name = "{ctx.effective_public_name}"
        version = "{ctx.sdk_version}"
        edition = "2021"
        description = "Generated {ctx.effective_public_name} Rust SDK"

        [dependencies]
        flatbuffers = "={ctx.flatc_version}"
        """
    )


def _variant(index: int) -> str:
    return f"Payload{index}"


def _type_path(
    entry: SchemaEntry, bindings: dict[str, RustPayloadBinding]
) -> str:
    return bindings[entry.qualified_root_type].type_path


def _payload_data(
    entries: list[SchemaEntry], bindings: dict[str, RustPayloadBinding]
) -> str:
    variants = "\n".join(
        f"    {_variant(index)}({_type_path(entry, bindings)}),"
        for index, entry in enumerate(entries)
    )
    return f"#[doc(hidden)]\n#[derive(Debug, Clone, PartialEq)]\npub enum PayloadData {{\n{variants}\n}}"


def _native_payload_impl(
    entry: SchemaEntry,
    index: int,
    single_payload: bool,
    bindings: dict[str, RustPayloadBinding],
) -> str:
    variant = _variant(index)
    binding = bindings[entry.qualified_root_type]
    type_path = binding.type_path
    namespace = binding.module_path
    root_snake = binding.root_function_suffix
    from_payload_data = (
        f"let PayloadData::{variant}(value) = data;\n                Some(value)"
        if single_payload
        else (
            f"match data {{\n"
            f"                    PayloadData::{variant}(value) => Some(value),\n"
            f"                    _ => None,\n"
            f"                }}"
        )
    )
    return textwrap.dedent(
        f"""\
        impl private::SealedPayload for {type_path} {{}}

        impl NativePayload for {type_path} {{
            const NAME: &'static str = "{entry.qualified_root_type}";

            fn into_payload_data(self) -> PayloadData {{
                PayloadData::{variant}(self)
            }}

            fn from_payload_data(data: &PayloadData) -> Option<&Self> {{
                {from_payload_data}
            }}

            fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError> {{
                use crate::fb::{namespace} as generated;
                if bytes.len() < 8 || !generated::{root_snake}_buffer_has_identifier(bytes) {{
                    return Err(PayloadDecodeError::InvalidIdentifier {{
                        expected: "{entry.file_identifier}",
                    }});
                }}
                generated::root_as_{root_snake}(bytes)
                    .map(|value| value.unpack())
                    .map_err(|error| PayloadDecodeError::InvalidFlatbuffer(error.to_string()))
            }}

            fn encode_payload(&self) -> Vec<u8> {{
                use crate::fb::{namespace} as generated;
                let mut builder = flatbuffers::FlatBufferBuilder::new();
                let root = self.pack(&mut builder);
                generated::finish_{root_snake}_buffer(&mut builder, root);
                builder.finished_data().to_vec()
            }}
        }}

        impl private::SealedEnvelopeValue for {type_path} {{}}

        impl EnvelopeValue for {type_path} {{
            fn append_to(self, envelope: &mut Envelope) {{
                envelope.entries.push(Entry::Known(self.into_payload_data()));
            }}
        }}
        """
    )


def _decode_known(
    entries: list[SchemaEntry], bindings: dict[str, RustPayloadBinding]
) -> str:
    arms = "\n".join(
        f"        {entry.numeric_id} => decode_entry::<{_type_path(entry, bindings)}>(id, bytes),"
        for entry in entries
    )
    return textwrap.dedent(
        f"""\
        fn decode_known(id: u64, bytes: Vec<u8>) -> Entry {{
            match id {{
        {arms}
                _ if id >= EXTERNAL_KEY_MIN => Entry::External {{ id, bytes }},
                _ => Entry::Unknown {{ id, bytes }},
            }}
        }}
        """
    )


def _known_entry_ref(entries: list[SchemaEntry]) -> str:
    arms = "\n".join(
        f"            PayloadData::{_variant(index)}(_) => \"{entry.qualified_root_type}\","
        for index, entry in enumerate(entries)
    )
    return textwrap.dedent(
        f"""\
        fn known_type_name(data: &PayloadData) -> &'static str {{
            match data {{
        {arms}
            }}
        }}
        """
    )


def _lib_rs(
    ctx: GenerationContext,
    entries: list[SchemaEntry],
    bindings: dict[str, RustPayloadBinding],
) -> str:
    sdk_upper = ctx.effective_public_name.upper()
    payload_data = _payload_data(entries, bindings)
    native_impls = "\n".join(
        _native_payload_impl(entry, index, len(entries) == 1, bindings)
        for index, entry in enumerate(entries)
    )
    decode_known = _decode_known(entries, bindings)
    known_entry_ref = _known_entry_ref(entries)
    body = textwrap.dedent(
        f"""\
        #![allow(dead_code, unused_imports, non_camel_case_types, non_snake_case)]
        #![allow(
            clippy::derivable_impls,
            clippy::extra_unused_lifetimes,
            clippy::missing_safety_doc,
            clippy::needless_lifetimes,
        )]
        #![warn(missing_docs)]

        //! Owning Rust API for the generated `{ctx.effective_public_name}` FlowData SDK.
        //!
        //! Use [`Envelope`] to decode, inspect, modify, and serialize FlowData packets.
        //! Known payloads use [`payload`], while application-defined byte payloads use
        //! [`external_key`] and [`external_payload`]. Raw FlatBuffers object types are
        //! available under [`fb`].

        mod flowdata_internal;
        #[allow(missing_docs)]
        /// Raw FlatBuffers types, organized by their schema namespaces.
        pub mod fb;

        use ::std::error::Error;
        use ::std::fmt;
        use ::std::marker::PhantomData;
        use ::std::slice;

        use crate::flowdata_internal::internalfb as wire;

        /// Generated SDK name written into serialized producer identity metadata.
        pub const {sdk_upper}_NAME: &str = "{ctx.effective_public_name}";
        /// Generated SDK version written into serialized producer identity metadata.
        pub const {sdk_upper}_VERSION: &str = "{ctx.sdk_version}";
        /// SHA-256 identity of the complete schema set used for generation.
        pub const SCHEMA_SET_SHA256: &str =
            "{schema_set_sha256(ctx)}"; // pragma: allowlist secret
        /// Exact FlatBuffers runtime version required by this crate.
        pub const FLATBUFFERS_VERSION_REQUIREMENT: &str = "=={ctx.flatc_version}";
        /// Lowest numeric key reserved for application-defined external payloads.
        pub const EXTERNAL_KEY_MIN: u64 = {RESERVED_PAYLOAD_ID_MIN};

        const EXTERNAL_HASH_OFFSET: u64 = 14_695_981_039_346_656_037;
        const EXTERNAL_HASH_PRIME: u64 = 1_099_511_628_211;

        mod private {{
            pub trait SealedPayload {{}}
            pub trait SealedSelector {{}}
            pub trait SealedEnvelopeValue {{}}
        }}

        /// Relationship between packet producer metadata and this generated SDK.
        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        pub enum ProducerIdentityStatus {{
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
        }}

        /// Opaque, deterministic selector for an application-defined byte payload.
        #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
        pub struct ExternalKey(u64);

        impl ExternalKey {{
            /// Returns the encoded numeric key used on the wire.
            pub fn value(self) -> u64 {{ self.0 }}
        }}

        /// Derives a stable external payload key from an application-owned string.
        pub fn external_key(key: &str) -> ExternalKey {{
            let mut value = EXTERNAL_HASH_OFFSET;
            for byte in key.as_bytes() {{
                value ^= u64::from(*byte);
                value = value.wrapping_mul(EXTERNAL_HASH_PRIME);
            }}
            ExternalKey((value & (EXTERNAL_KEY_MIN - 1)) | EXTERNAL_KEY_MIN)
        }}

        /// Returns whether a key belongs to the reserved external payload range.
        pub fn is_external_key(key: ExternalKey) -> bool {{ key.value() >= EXTERNAL_KEY_MIN }}

        /// Owned application-defined payload ready to add to an [`Envelope`].
        #[derive(Debug, Clone, PartialEq, Eq)]
        pub struct ExternalPayload {{
            key: ExternalKey,
            bytes: Vec<u8>,
        }}

        /// Creates an owned external payload for use with [`Envelope::add`].
        pub fn external_payload(
            key: ExternalKey,
            bytes: impl Into<Vec<u8>>,
        ) -> ExternalPayload {{
            ExternalPayload {{ key, bytes: bytes.into() }}
        }}

        /// Error encountered while decoding a known payload body.
        #[derive(Debug, Clone, PartialEq, Eq)]
        pub enum PayloadDecodeError {{
            /// The payload does not carry its schema's required file identifier.
            InvalidIdentifier {{
                /// Required FlatBuffers file identifier.
                expected: &'static str,
            }},
            /// FlatBuffers rejected the payload bytes.
            InvalidFlatbuffer(String),
        }}

        impl fmt::Display for PayloadDecodeError {{
            fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {{
                match self {{
                    Self::InvalidIdentifier {{ expected }} =>
                        write!(formatter, "invalid payload file identifier; expected {{expected}}"),
                    Self::InvalidFlatbuffer(error) => formatter.write_str(error),
                }}
            }}
        }}

        impl Error for PayloadDecodeError {{}}

        /// Category of an outer envelope decoding failure.
        #[derive(Debug, Clone, PartialEq, Eq)]
        pub enum EnvelopeDecodeErrorKind {{
            /// The packet does not carry the `FLWD` envelope identifier.
            InvalidIdentifier,
            /// FlatBuffers rejected the envelope bytes.
            InvalidFlatbuffer(String),
        }}

        /// Envelope decoding failure that retains the original input bytes.
        #[derive(Debug, Clone, PartialEq, Eq)]
        pub struct EnvelopeDecodeError {{
            kind: EnvelopeDecodeErrorKind,
            bytes: Vec<u8>,
        }}

        impl EnvelopeDecodeError {{
            /// Returns the failure category.
            pub fn kind(&self) -> &EnvelopeDecodeErrorKind {{ &self.kind }}
            /// Borrows the original undecoded input bytes.
            pub fn bytes(&self) -> &[u8] {{ &self.bytes }}
            /// Returns ownership of the original undecoded input bytes.
            pub fn into_bytes(self) -> Vec<u8> {{ self.bytes }}
        }}

        impl fmt::Display for EnvelopeDecodeError {{
            fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {{
                match &self.kind {{
                    EnvelopeDecodeErrorKind::InvalidIdentifier =>
                        formatter.write_str("invalid envelope file identifier; expected FLWD"),
                    EnvelopeDecodeErrorKind::InvalidFlatbuffer(error) => formatter.write_str(error),
                }}
            }}
        }}

        impl Error for EnvelopeDecodeError {{}}

        __PAYLOAD_DATA__

        #[derive(Debug, Clone, PartialEq)]
        enum Entry {{
            Known(PayloadData),
            External {{ id: u64, bytes: Vec<u8> }},
            Unknown {{ id: u64, bytes: Vec<u8> }},
            MalformedKnown {{
                id: u64,
                expected_type: &'static str,
                bytes: Vec<u8>,
                error: PayloadDecodeError,
            }},
        }}

        /// Read-only diagnostic view of an envelope entry.
        #[derive(Debug, Clone, PartialEq, Eq)]
        pub enum EntryRef<'a> {{
            /// Successfully decoded generated payload.
            Known {{
                /// Fully qualified schema type name.
                type_name: &'static str,
            }},
            /// Application-defined byte payload.
            External {{
                /// Stable external selector.
                key: ExternalKey,
                /// Borrowed payload bytes.
                bytes: &'a [u8],
            }},
            /// Unrecognized payload in the generated payload-ID domain.
            Unknown {{
                /// Unrecognized wire payload ID.
                id: u64,
                /// Borrowed payload bytes preserved for forwarding.
                bytes: &'a [u8],
            }},
            /// Known payload whose body could not be decoded.
            MalformedKnown {{
                /// Known wire payload ID.
                id: u64,
                /// Fully qualified schema type expected for the ID.
                expected_type: &'static str,
                /// Borrowed malformed bytes preserved for forwarding.
                bytes: &'a [u8],
                /// Typed reason the known payload failed to decode.
                error: &'a PayloadDecodeError,
            }},
        }}

        /// Generated owned payload type accepted by the envelope API.
        pub trait NativePayload: private::SealedPayload + Sized + Send + Sync + 'static {{
            /// Fully qualified schema type name.
            const NAME: &'static str;
            #[doc(hidden)] fn into_payload_data(self) -> PayloadData;
            #[doc(hidden)] fn from_payload_data(data: &PayloadData) -> Option<&Self>;
            #[doc(hidden)] fn decode_payload(bytes: &[u8]) -> Result<Self, PayloadDecodeError>;
            #[doc(hidden)] fn encode_payload(&self) -> Vec<u8>;
        }}

        /// Owned value that can be appended to an [`Envelope`].
        pub trait EnvelopeValue: private::SealedEnvelopeValue {{
            #[doc(hidden)] fn append_to(self, envelope: &mut Envelope);
        }}

        impl private::SealedEnvelopeValue for ExternalPayload {{}}

        impl EnvelopeValue for ExternalPayload {{
            fn append_to(self, envelope: &mut Envelope) {{
                envelope.entries.push(Entry::External {{
                    id: self.key.value(),
                    bytes: self.bytes,
                }});
            }}
        }}

        /// Zero-sized selector for a generated payload type.
        pub struct Payload<T: NativePayload>(PhantomData<fn() -> T>);

        impl<T: NativePayload> Copy for Payload<T> {{}}

        impl<T: NativePayload> Clone for Payload<T> {{
            fn clone(&self) -> Self {{ *self }}
        }}

        impl<T: NativePayload> fmt::Debug for Payload<T> {{
            fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {{
                formatter.debug_tuple("Payload").field(&T::NAME).finish()
            }}
        }}

        /// Creates a reusable selector for a generated payload type.
        pub const fn payload<T: NativePayload>() -> Payload<T> {{ Payload(PhantomData) }}

        /// Iterator over decoded instances of a selected generated payload type.
        pub struct PayloadIter<'a, T: NativePayload> {{
            inner: slice::Iter<'a, Entry>,
            marker: PhantomData<fn() -> T>,
        }}

        impl<'a, T: NativePayload> Iterator for PayloadIter<'a, T> {{
            type Item = &'a T;

            fn next(&mut self) -> Option<Self::Item> {{
                self.inner.find_map(|entry| match entry {{
                    Entry::Known(data) => T::from_payload_data(data),
                    _ => None,
                }})
            }}
        }}

        /// Iterator over byte slices for a selected external payload key.
        pub struct ExternalIter<'a> {{
            inner: slice::Iter<'a, Entry>,
            key: ExternalKey,
        }}

        impl<'a> Iterator for ExternalIter<'a> {{
            type Item = &'a [u8];

            fn next(&mut self) -> Option<Self::Item> {{
                self.inner.find_map(|entry| match entry {{
                    Entry::External {{ id, bytes }} if *id == self.key.value() =>
                        Some(bytes.as_slice()),
                    _ => None,
                }})
            }}
        }}

        /// Sealed selector implemented by [`Payload`] and [`ExternalKey`].
        pub trait Selector: private::SealedSelector + Copy {{
            /// Item yielded when the selector matches an envelope entry.
            type Item<'a> where Self: 'a;
            /// Iterator returned by [`Envelope::for_each`].
            type Iter<'a>: Iterator<Item = Self::Item<'a>> where Self: 'a;

            #[doc(hidden)]
            fn iter<'a>(self, envelope: &'a Envelope) -> Self::Iter<'a>;
        }}

        impl<T: NativePayload> private::SealedSelector for Payload<T> {{}}

        impl<T: NativePayload> Selector for Payload<T> {{
            type Item<'a> = &'a T;
            type Iter<'a> = PayloadIter<'a, T>;

            fn iter<'a>(self, envelope: &'a Envelope) -> Self::Iter<'a> {{
                PayloadIter {{ inner: envelope.entries.iter(), marker: PhantomData }}
            }}
        }}

        impl private::SealedSelector for ExternalKey {{}}

        impl Selector for ExternalKey {{
            type Item<'a> = &'a [u8];
            type Iter<'a> = ExternalIter<'a>;

            fn iter<'a>(self, envelope: &'a Envelope) -> Self::Iter<'a> {{
                ExternalIter {{ inner: envelope.entries.iter(), key: self }}
            }}
        }}

        __NATIVE_IMPLS__

        fn decode_entry<T: NativePayload>(id: u64, bytes: Vec<u8>) -> Entry {{
            match T::decode_payload(&bytes) {{
                Ok(value) => Entry::Known(value.into_payload_data()),
                Err(error) => Entry::MalformedKnown {{
                    id,
                    expected_type: T::NAME,
                    bytes,
                    error,
                }},
            }}
        }}

        __DECODE_KNOWN__

        __KNOWN_ENTRY_REF__

        /// Owned FlowData packet containing generated and application-defined payloads.
        #[derive(Debug, Clone, PartialEq)]
        pub struct Envelope {{
            entries: Vec<Entry>,
            producer_sdk_name: String,
            producer_sdk_version: String,
            producer_schema_set_sha256: String,
        }}

        impl Default for Envelope {{
            fn default() -> Self {{ Self::new() }}
        }}

        impl Envelope {{
            /// Creates an empty envelope stamped with this generated SDK's identity.
            pub fn new() -> Self {{
                Self {{
                    entries: Vec::new(),
                    producer_sdk_name: {sdk_upper}_NAME.to_owned(),
                    producer_sdk_version: {sdk_upper}_VERSION.to_owned(),
                    producer_schema_set_sha256: SCHEMA_SET_SHA256.to_owned(),
                }}
            }}

            /// Decodes an owned packet while preserving unknown or malformed payload entries.
            pub fn decode(bytes: Vec<u8>) -> Result<Self, EnvelopeDecodeError> {{
                if bytes.len() < 8 || !wire::wire_envelope_buffer_has_identifier(&bytes) {{
                    return Err(EnvelopeDecodeError {{
                        kind: EnvelopeDecodeErrorKind::InvalidIdentifier,
                        bytes,
                    }});
                }}
                let envelope = match wire::root_as_wire_envelope(&bytes) {{
                    Ok(envelope) => envelope,
                    Err(error) => return Err(EnvelopeDecodeError {{
                        kind: EnvelopeDecodeErrorKind::InvalidFlatbuffer(error.to_string()),
                        bytes,
                    }}),
                }};
                let entries = envelope.payloads().map(|payloads| {{
                    payloads.iter().map(|payload| {{
                        let blob = payload.blob()
                            .map(|value| value.iter().collect())
                            .unwrap_or_default();
                        decode_known(payload.id(), blob)
                    }}).collect()
                }}).unwrap_or_default();
                Ok(Self {{
                    entries,
                    producer_sdk_name: envelope.producer_sdk_name().unwrap_or_default().to_owned(),
                    producer_sdk_version: envelope.producer_sdk_version().unwrap_or_default().to_owned(),
                    producer_schema_set_sha256: envelope.producer_schema_set_sha256().unwrap_or_default().to_owned(),
                }})
            }}

            /// Returns the producer SDK name stored in the decoded packet.
            pub fn producer_sdk_name(&self) -> &str {{ &self.producer_sdk_name }}
            /// Returns the producer SDK version stored in the decoded packet.
            pub fn producer_sdk_version(&self) -> &str {{ &self.producer_sdk_version }}
            /// Returns the producer schema-set digest stored in the decoded packet.
            pub fn producer_schema_set_sha256(&self) -> &str {{ &self.producer_schema_set_sha256 }}

            /// Compares producer identity metadata with this generated SDK.
            pub fn producer_identity(&self) -> ProducerIdentityStatus {{
                let name = self.producer_sdk_name();
                let version = self.producer_sdk_version();
                let digest = self.producer_schema_set_sha256();
                if name.is_empty() || version.is_empty() || digest.is_empty() {{
                    return ProducerIdentityStatus::Missing;
                }}
                if !valid_sdk_name(name) || !valid_semver(version) || !valid_sha256(digest) {{
                    return ProducerIdentityStatus::Malformed;
                }}
                if name != {sdk_upper}_NAME {{ return ProducerIdentityStatus::SdkNameMismatch; }}
                if version != {sdk_upper}_VERSION {{ return ProducerIdentityStatus::SdkVersionMismatch; }}
                if digest != SCHEMA_SET_SHA256 {{ return ProducerIdentityStatus::SchemaSetMismatch; }}
                ProducerIdentityStatus::ExactMatch
            }}

            /// Returns whether the envelope has no payload entries.
            pub fn is_empty(&self) -> bool {{ self.entries.is_empty() }}
            /// Returns the total number of payload entries, including preserved entries.
            pub fn len(&self) -> usize {{ self.entries.len() }}

            /// Appends a generated payload or an [`ExternalPayload`].
            pub fn add<T: EnvelopeValue>(&mut self, value: T) {{
                value.append_to(self);
            }}

            /// Counts entries matched by a generated payload or external-key selector.
            pub fn count<S: Selector>(&self, selector: S) -> usize {{
                selector.iter(self).count()
            }}

            /// Returns whether at least one entry matches a selector.
            pub fn contains<S: Selector>(&self, selector: S) -> bool {{
                self.get(selector, 0).is_some()
            }}

            /// Returns the zero-based matching entry selected by payload type or external key.
            pub fn get<'a, S: Selector>(
                &'a self,
                selector: S,
                index: usize,
            ) -> Option<S::Item<'a>> {{
                selector.iter(self).nth(index)
            }}

            /// Iterates over all entries matched by a selector.
            pub fn for_each<S: Selector>(&self, selector: S) -> S::Iter<'_> {{
                selector.iter(self)
            }}

            /// Iterates over diagnostic views of every stored entry.
            pub fn entries(&self) -> impl Iterator<Item = EntryRef<'_>> {{
                self.entries.iter().map(|entry| match entry {{
                    Entry::Known(data) => EntryRef::Known {{
                        type_name: known_type_name(data),
                    }},
                    Entry::External {{ id, bytes }} => EntryRef::External {{
                        key: ExternalKey(*id),
                        bytes,
                    }},
                    Entry::Unknown {{ id, bytes }} => EntryRef::Unknown {{ id: *id, bytes }},
                    Entry::MalformedKnown {{ id, expected_type, bytes, error }} =>
                        EntryRef::MalformedKnown {{
                            id: *id,
                            expected_type,
                            bytes,
                            error,
                        }},
                }})
            }}

            /// Serializes the envelope and stamps this generated SDK's producer identity.
            pub fn serialize(&self) -> Vec<u8> {{
                let encoded: Vec<(u64, Vec<u8>)> = self.entries.iter().map(|entry| match entry {{
                    Entry::Known(data) => encode_known(data),
                    Entry::External {{ id, bytes }} |
                    Entry::Unknown {{ id, bytes }} |
                    Entry::MalformedKnown {{ id, bytes, .. }} => (*id, bytes.clone()),
                }}).collect();
                let mut builder = flatbuffers::FlatBufferBuilder::new();
                let payload_offsets: Vec<_> = encoded.iter().map(|(id, bytes)| {{
                    let blob = builder.create_vector(bytes);
                    wire::WirePayload::create(&mut builder, &wire::WirePayloadArgs {{
                        id: *id,
                        blob: Some(blob),
                    }})
                }}).collect();
                let payloads = builder.create_vector(&payload_offsets);
                let producer_name = builder.create_string({sdk_upper}_NAME);
                let producer_version = builder.create_string({sdk_upper}_VERSION);
                let producer_digest = builder.create_string(SCHEMA_SET_SHA256);
                let root = wire::WireEnvelope::create(&mut builder, &wire::WireEnvelopeArgs {{
                    payloads: Some(payloads),
                    producer_sdk_name: Some(producer_name),
                    producer_sdk_version: Some(producer_version),
                    producer_schema_set_sha256: Some(producer_digest),
                }});
                wire::finish_wire_envelope_buffer(&mut builder, root);
                builder.finished_data().to_vec()
            }}
        }}

        fn valid_sdk_name(value: &str) -> bool {{
            let mut chars = value.chars();
            matches!(chars.next(), Some('a'..='z'))
                && chars.all(|character| character.is_ascii_lowercase()
                    || character.is_ascii_digit() || character == '_')
        }}

        fn valid_semver(value: &str) -> bool {{
            let parts: Vec<_> = value.split('.').collect();
            parts.len() == 3 && parts.iter().all(|part| {{
                !part.is_empty()
                    && part.chars().all(|character| character.is_ascii_digit())
                    && (part == &"0" || !part.starts_with('0'))
            }})
        }}

        fn valid_sha256(value: &str) -> bool {{
            value.len() == 64 && value.chars().all(|character| character.is_ascii_hexdigit()
                && !character.is_ascii_uppercase())
        }}
        """
    )
    body = body.replace("__PAYLOAD_DATA__", payload_data)
    body = body.replace("__NATIVE_IMPLS__", native_impls)
    body = body.replace("__DECODE_KNOWN__", decode_known)
    body = body.replace("__KNOWN_ENTRY_REF__", known_entry_ref)
    return (
        RUST_GENERATED_BANNER
        + body
        + _encode_known(entries)
        + _tests(ctx, entries, bindings)
    )


def _encode_known(entries: list[SchemaEntry]) -> str:
    arms = "\n".join(
        f"        PayloadData::{_variant(index)}(value) => "
        f"({entry.numeric_id}, value.encode_payload()),"
        for index, entry in enumerate(entries)
    )
    return textwrap.dedent(
        f"""\

        fn encode_known(data: &PayloadData) -> (u64, Vec<u8>) {{
            match data {{
        {arms}
            }}
        }}
        """
    )


def _tests(
    ctx: GenerationContext,
    entries: list[SchemaEntry],
    bindings: dict[str, RustPayloadBinding],
) -> str:
    sdk_upper = ctx.effective_public_name.upper()
    first = entries[0]
    first_type = _type_path(first, bindings)
    unknown_id = next(value for value in range(1, 1024) if all(e.numeric_id != value for e in entries))
    return textwrap.dedent(
        f"""\

        #[cfg(test)]
        mod tests {{
            use super::*;

            fn assert_send_sync_static<T: Send + Sync + 'static>() {{}}

            fn raw_packet(id: u64, bytes: &[u8]) -> Vec<u8> {{
                let mut builder = flatbuffers::FlatBufferBuilder::new();
                let blob = builder.create_vector(bytes);
                let payload = wire::WirePayload::create(&mut builder, &wire::WirePayloadArgs {{
                    id,
                    blob: Some(blob),
                }});
                let payloads = builder.create_vector(&[payload]);
                let producer_name = builder.create_string({sdk_upper}_NAME);
                let producer_version = builder.create_string({sdk_upper}_VERSION);
                let producer_digest = builder.create_string(SCHEMA_SET_SHA256);
                let root = wire::WireEnvelope::create(&mut builder, &wire::WireEnvelopeArgs {{
                    payloads: Some(payloads),
                    producer_sdk_name: Some(producer_name),
                    producer_sdk_version: Some(producer_version),
                    producer_schema_set_sha256: Some(producer_digest),
                }});
                wire::finish_wire_envelope_buffer(&mut builder, root);
                builder.finished_data().to_vec()
            }}

            #[test]
            fn owned_roundtrip_and_external_payloads() {{
                assert_send_sync_static::<Envelope>();
                assert_send_sync_static::<EnvelopeDecodeError>();
                assert_send_sync_static::<{first_type}>();

                let key = external_key("com.example.generated-test");
                let known = payload::<{first_type}>();
                let mut envelope = Envelope::new();
                envelope.add({first_type}::default());
                envelope.add(external_payload(key, b"external".to_vec()));
                let decoded = Envelope::decode(envelope.serialize()).expect("valid round trip");
                assert_eq!(decoded.count(known), 1);
                assert!(decoded.contains(known));
                assert!(decoded.get(known, 0).is_some());
                assert_eq!(decoded.count(key), 1);
                assert!(decoded.contains(key));
                assert_eq!(decoded.get(key, 0), Some(b"external".as_slice()));
                assert_eq!(decoded.for_each(key).collect::<Vec<_>>(), vec![b"external"]);
                assert_eq!(decoded.producer_identity(), ProducerIdentityStatus::ExactMatch);
            }}

            #[test]
            fn unknown_and_malformed_known_payloads_are_preserved() {{
                let unknown_bytes = b"future-payload";
                let unknown = Envelope::decode(raw_packet({unknown_id}, unknown_bytes))
                    .expect("unknown payload keeps envelope valid");
                assert!(matches!(unknown.entries().next(), Some(EntryRef::Unknown {{
                    id: {unknown_id}, bytes
                }}) if bytes == unknown_bytes));
                let unknown_roundtrip = Envelope::decode(unknown.serialize()).expect("unknown round trip");
                assert!(matches!(unknown_roundtrip.entries().next(), Some(EntryRef::Unknown {{
                    id: {unknown_id}, bytes
                }}) if bytes == unknown_bytes));

                let malformed_bytes = b"not-a-flatbuffer";
                let malformed = Envelope::decode(raw_packet({first.numeric_id}, malformed_bytes))
                    .expect("malformed known payload keeps envelope valid");
                assert!(matches!(malformed.entries().next(), Some(EntryRef::MalformedKnown {{
                    id: {first.numeric_id}, bytes, ..
                }}) if bytes == malformed_bytes));
                let malformed_roundtrip = Envelope::decode(malformed.serialize())
                    .expect("malformed known round trip");
                assert!(matches!(malformed_roundtrip.entries().next(), Some(EntryRef::MalformedKnown {{
                    id: {first.numeric_id}, bytes, ..
                }}) if bytes == malformed_bytes));
            }}

            #[test]
            fn malformed_envelope_returns_original_bytes_without_panicking() {{
                for bytes in [Vec::new(), b"bad".to_vec(), vec![0; 32]] {{
                    let original = bytes.clone();
                    let result = std::panic::catch_unwind(|| Envelope::decode(bytes));
                    let error = result.expect("decode must not panic").expect_err("invalid packet");
                    assert_eq!(error.bytes(), original);
                }}
            }}
        }}
        """
    )


def generate_rust_sdk(entries: list[SchemaEntry], ctx: GenerationContext) -> list[Path]:
    root = ctx.rust_root
    source_root = root / "src"
    source_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="flowdata-rust-wire-") as temporary:
        temporary_root = Path(temporary)
        wire_schema = temporary_root / "wire_envelope.fbs"
        wire_output = temporary_root / "generated"
        wire_output.mkdir()
        wire_schema.write_text(
            ENVELOPE_SCHEMA_TEMPLATE.replace("__SDK_NAME__", ctx.sdk_name),
            encoding="utf-8",
        )
        _run_flatc_rust([wire_schema], wire_output, ctx.flatc_bin, [])
        shutil.copytree(
            wire_output / ctx.sdk_name / "internalfb",
            source_root / "flowdata_internal" / "internalfb",
        )
    flatbuffers_root = source_root / "fb"
    flatbuffers_root.mkdir()
    _run_flatc_rust(ctx.schema_paths, flatbuffers_root, ctx.flatc_bin, [ctx.schema_dir])
    _normalize_rust_module_directories(flatbuffers_root)
    bindings = _rust_payload_bindings(entries, flatbuffers_root)
    generated_root_module = flatbuffers_root / "mod.rs"
    if generated_root_module.exists():
        generated_root_module.unlink()
    _write_module_tree(source_root)

    cargo_toml = root / "Cargo.toml"
    cargo_toml.write_text(_cargo_toml(ctx), encoding="utf-8")
    lib_rs = source_root / "lib.rs"
    lib_rs.write_text(_lib_rs(ctx, entries, bindings), encoding="utf-8")
    _format_rust_crate(root)
    return sorted(path for path in root.rglob("*") if path.is_file())


def _write_module_tree(source_root: Path) -> None:
    directories = sorted(
        (path for path in source_root.rglob("*") if path.is_dir()),
        key=lambda path: len(path.parts),
        reverse=True,
    )
    for directory in directories:
        lines: list[str] = ["use super::*;"]
        for child in sorted(path for path in directory.iterdir() if path.is_dir()):
            visibility = "pub(crate)" if child.name == "internalfb" else "pub"
            lines.append(f"{visibility} mod {_rust_identifier(child.name)};")
        for generated in sorted(directory.glob("*_generated.rs")):
            module = _rust_identifier(generated.stem)
            lines.append(f"mod {module};")
            lines.append(f"pub use self::{module}::*;")
        if lines:
            (directory / "mod.rs").write_text("\n".join(lines) + "\n", encoding="utf-8")
