################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import subprocess
import shutil
import tempfile
import textwrap
from pathlib import Path

from ..errors import fail
from ..flatbuffers_compat import FLATBUFFERS_TYPESCRIPT_REQUIREMENT
from ..types import RESERVED_PAYLOAD_ID_MIN, GenerationContext, SchemaEntry

TS_GENERATED_BANNER = (
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
file_identifier \"FLWD\";
"""


def _ts_project_root(ctx: GenerationContext) -> Path:
    return ctx.generated_root / "ts"


def _flatc_include_args(include_dirs: list[Path]) -> list[str]:
    args: list[str] = []
    for include_dir in include_dirs:
        args.extend(["-I", str(include_dir)])
    return args


def _run_flatc_ts(
    schemas: list[Path],
    output_dir: Path,
    flatc: str,
    *,
    include_dirs: list[Path] | None = None,
) -> None:
    include_dirs = include_dirs or []
    try:
        subprocess.run(
            [
                flatc,
                "--ts",
                "--gen-object-api",
                "--gen-all",
                "-o",
                str(output_dir),
                *_flatc_include_args(include_dirs),
                *(str(schema) for schema in schemas),
            ],
            check=True,
            text=True,
            capture_output=True,
        )
    except FileNotFoundError:
        fail(f"flatc not found: {flatc}")
    except subprocess.CalledProcessError as exc:
        details = "\n".join(
            part.strip() for part in [exc.stdout or "", exc.stderr or ""] if part.strip()
        )
        schema_list = ", ".join(str(schema) for schema in schemas)
        fail(
            f"flatc failed for schema set {schema_list} (exit code {exc.returncode})"
            + (f"\n{details}" if details else "")
            + "\nEnsure this flatc build supports the --ts target."
        )


def _namespace_module_path(entry: SchemaEntry) -> str:
    return "/".join(_ts_file_stem(part) for part in entry.namespace.split("::"))


def _ts_file_stem(name: str) -> str:
    result: list[str] = []
    previous_was_separator = True

    for index, char in enumerate(name):
        if char.isalnum():
            if (
                char.isupper()
                and index > 0
                and not previous_was_separator
                and (
                    index + 1 == len(name)
                    or name[index + 1].islower()
                    or not name[index - 1].isupper()
                )
            ):
                result.append("-")
            result.append(char.lower())
            previous_was_separator = False
            continue

        if not previous_was_separator:
            result.append("-")
            previous_was_separator = True

    return "".join(result).strip("-") or name.lower()


def _entrypoint_module_path(entry: SchemaEntry) -> str:
    namespace_path = _namespace_module_path(entry)
    return f"{namespace_path}/{_ts_file_stem(entry.root_type_name)}"


def _registry_text(entries: list[SchemaEntry]) -> str:
    import_lines: list[str] = [
        TS_GENERATED_BANNER.rstrip(),
        "import { ByteBuffer } from 'flatbuffers';",
    ]
    factory_lines: list[str] = []
    types_map_lines: list[str] = ["export const _TYPE_REGISTRY = new Map<bigint, TypeInfo>(["]
    class_map_lines: list[str] = ["export const _CLASS_TO_ID = new Map<PayloadClass, bigint>(["]

    for entry in entries:
        module_path = _entrypoint_module_path(entry)
        import_name = entry.root_type_name
        native_name = f"{entry.root_type_name}T"
        root_alias = f"{import_name}_{entry.numeric_id}"
        native_alias = f"{native_name}_{entry.numeric_id}"
        import_lines.append(
            f"import {{ {import_name} as {root_alias}, {native_name} as {native_alias} }} "
            f"from './fb/{module_path}.js';"
        )

        factory_name = f"decode_{entry.numeric_id}"
        factory_lines.append(
            f"const {factory_name} = (blob: Uint8Array): unknown => "
            f"{root_alias}.getRootAs{import_name}(new ByteBuffer(blob)).unpack();"
        )
        # verify function using bufferHasIdentifier
        verify_name = f"verify_{entry.numeric_id}"
        factory_lines.append(
            f"const {verify_name} = (blob: Uint8Array): boolean => "
            f"{root_alias}.bufferHasIdentifier(new ByteBuffer(blob));"
        )

        types_map_lines.extend(
            [
                f"  [{entry.numeric_id}n, {{",
                f"    name: '{entry.name}',",
                f"    root_type: '{entry.root_type_name}',",
                f"    qualified_root_type: '{entry.qualified_root_type}',",
                f"    file_identifier: '{entry.file_identifier}',",
                f"    decode: {factory_name},",
                f"    verify: {verify_name},",
                "  }],",
            ]
        )
        class_map_lines.append(f"  [{native_alias}, {entry.numeric_id}n],")

    types_map_lines.append("]);")
    class_map_lines.append("]);")

    return "\n".join(
        [
            *import_lines,
            "export type PayloadClass<T = unknown> = Function & { prototype: T };",
            "",
            "export type TypeInfo = {",
            "  name: string;",
            "  root_type: string;",
            "  qualified_root_type: string;",
            "  file_identifier: string;",
            "  decode: (blob: Uint8Array) => unknown;",
            "  verify?: (blob: Uint8Array) => boolean;",
            "};",
            "",
            *factory_lines,
            "",
            *types_map_lines,
            "",
            *class_map_lines,
        ]
    )


def _sdk_namespace_module_path(sdk_name: str) -> str:
    return "/".join(_ts_file_stem(part) for part in sdk_name.split("::"))


def _envelope_text(sdk_name: str, sdk_version: str, schema_set_digest: str) -> str:
    return TS_GENERATED_BANNER + textwrap.dedent(
        """
import { Builder, ByteBuffer } from 'flatbuffers';

import { WireEnvelope as FbEnvelope } from './fb/__SDK_INTERNAL_NS_PATH__/internalfb/wire-envelope.js';
import { WirePayload as Payload } from './fb/__SDK_INTERNAL_NS_PATH__/internalfb/wire-payload.js';
import {
  PayloadClass,
  _CLASS_TO_ID,
  _TYPE_REGISTRY,
} from './registry.js';

type PayloadEntry = {
  id: bigint;
  blob: Uint8Array;
};

type NativePayload = {
  pack(builder: Builder): number;
  constructor: Function;
};

export const SDK_NAME = '__SDK_NAME__';
export const SDK_VERSION = '__SDK_VERSION__';
export const SCHEMA_SET_SHA256 = '__SCHEMA_SET_SHA256__';
export const EXTERNAL_KEY_MIN = BigInt('__EXTERNAL_KEY_MIN__');
const EXTERNAL_KEY_MASK = EXTERNAL_KEY_MIN - BigInt(1);
const EXTERNAL_HASH_OFFSET = BigInt('14695981039346656037');
const EXTERNAL_HASH_PRIME = BigInt('1099511628211');
const UINT64_MASK = BigInt('0xffffffffffffffff');
const textEncoder = new TextEncoder();
const externalKeyToken = Symbol('external-key');
const SDK_NAME_PATTERN = /^[a-z][a-z0-9_]*$/;
const SEMANTIC_VERSION_PATTERN = /^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$/;
const SHA256_PATTERN = /^[0-9a-f]{64}$/;

export enum ProducerIdentityStatus {
  ExactMatch = 'exact_match',
  Missing = 'missing',
  Malformed = 'malformed',
  SdkNameMismatch = 'sdk_name_mismatch',
  SdkVersionMismatch = 'sdk_version_mismatch',
  SchemaSetMismatch = 'schema_set_mismatch',
}

function isExternalKeyValue(value: bigint): boolean {
  return value >= EXTERNAL_KEY_MIN && value <= UINT64_MASK;
}

export class ExternalKey {
  readonly #value: bigint;

  private constructor(value: bigint, token: symbol) {
    if (token !== externalKeyToken || !isExternalKeyValue(value)) {
      throw new Error(`${SDK_NAME} external keys must be created with external_key()`);
    }
    this.#value = value;
  }

  static fromString(key: string): ExternalKey {
    if (typeof key !== 'string') {
      throw new Error(`${SDK_NAME} external key must be a string`);
    }

    let value = EXTERNAL_HASH_OFFSET;
    for (const byte of textEncoder.encode(key)) {
      value ^= BigInt(byte);
      value = (value * EXTERNAL_HASH_PRIME) & UINT64_MASK;
    }
    return new ExternalKey((value & EXTERNAL_KEY_MASK) | EXTERNAL_KEY_MIN, externalKeyToken);
  }

  get value(): bigint {
    return this.#value;
  }

  toString(): string {
    return this.#value.toString();
  }
}

export function is_external_key(key: unknown): key is ExternalKey {
  return key instanceof ExternalKey && isExternalKeyValue(key.value);
}

export function external_key(key: string): ExternalKey {
  return ExternalKey.fromString(key);
}

function resolveExternalKey(key: unknown): bigint {
  if (!is_external_key(key)) {
    if (typeof key === 'string') {
      throw new Error(`${SDK_NAME} external envelope operations require ExternalKey; call external_key(...) first`);
    }
    throw new Error(`${SDK_NAME} external key must be a value returned by external_key()`);
  }
  return key.value;
}

function resolvePayloadClass(payloadType: PayloadClass): bigint {
  if (typeof payloadType !== 'function') {
    throw new Error(`${SDK_NAME} selector must be a generated payload class or ExternalKey`);
  }
  const id = _CLASS_TO_ID.get(payloadType);
  if (id === undefined) {
    throw new Error(`unknown ${SDK_NAME} payload type: ${payloadType.name}`);
  }
  return id;
}

function resolveNativePayload(value: NativePayload): bigint {
  const id = _CLASS_TO_ID.get(value.constructor);
  if (id === undefined) {
    throw new Error(`unknown ${SDK_NAME} payload type: ${value.constructor.name}`);
  }
  return id;
}

function copyBlob(payload: Payload): Uint8Array {
  const blobArray = payload.blobArray();
  if (blobArray) {
    return new Uint8Array(blobArray);
  }

  const len = payload.blobLength();
  const out = new Uint8Array(len);
  for (let i = 0; i < len; i += 1) {
    out[i] = payload.blob(i) ?? 0;
  }
  return out;
}

function copyBytes(data: Uint8Array | ArrayBuffer): Uint8Array {
  const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
  return new Uint8Array(bytes);
}

function packNativePayload(id: bigint, value: NativePayload): Uint8Array {
  const info = _TYPE_REGISTRY.get(id);
  if (!info) {
    throw new Error(`unknown ${SDK_NAME} payload id: ${id}`);
  }

  const builder = new Builder(1024);
  const offset = value.pack(builder);
  builder.finish(offset, info.file_identifier);
  return builder.asUint8Array();
}

export class Envelope {
  private payloadEntries: PayloadEntry[] = [];
  private validEnvelope = true;
  private errorMessage: string | null = null;
  private producerName = SDK_NAME;
  private producerVersion = SDK_VERSION;
  private producerSchemaDigest = SCHEMA_SET_SHA256;

  constructor(packet?: Uint8Array | ArrayBuffer) {
    if (packet !== undefined) {
      this.load(packet);
    }
  }

  private load(packet: Uint8Array | ArrayBuffer): void {
    this.payloadEntries = [];
    this.validEnvelope = false;
    this.errorMessage = null;
    this.producerName = '';
    this.producerVersion = '';
    this.producerSchemaDigest = '';
    const bytes = packet instanceof Uint8Array ? packet : new Uint8Array(packet);

    try {
      const bb = new ByteBuffer(bytes);
      if (!FbEnvelope.bufferHasIdentifier(bb)) {
        this.errorMessage = `invalid ${SDK_NAME} envelope file_identifier`;
        return;
      }
      const envelope = FbEnvelope.getRootAsWireEnvelope(bb);
      this.producerName = envelope.producerSdkName() ?? '';
      this.producerVersion = envelope.producerSdkVersion() ?? '';
      this.producerSchemaDigest = envelope.producerSchemaSetSha256() ?? '';

      const count = envelope.payloadsLength();
      for (let i = 0; i < count; i += 1) {
        const payload = envelope.payloads(i, new Payload());
        if (payload === null) {
          continue;
        }
        const id = payload.id();
        this.payloadEntries.push({
          id,
          blob: copyBlob(payload),
        });
      }

      this.validEnvelope = true;
    } catch (error) {
      this.errorMessage = `invalid ${SDK_NAME} envelope: ${String(error)}`;
    }
  }

  // state
  valid(): boolean {
    return this.validEnvelope;
  }

  error(): string | null {
    return this.errorMessage;
  }

  producerSdkName(): string {
    return this.producerName;
  }

  producerSdkVersion(): string {
    return this.producerVersion;
  }

  producerSchemaSetSha256(): string {
    return this.producerSchemaDigest;
  }

  producerIdentity(): ProducerIdentityStatus {
    if (!this.producerName || !this.producerVersion || !this.producerSchemaDigest) {
      return ProducerIdentityStatus.Missing;
    }
    if (!SDK_NAME_PATTERN.test(this.producerName)
        || !SEMANTIC_VERSION_PATTERN.test(this.producerVersion)
        || !SHA256_PATTERN.test(this.producerSchemaDigest)) {
      return ProducerIdentityStatus.Malformed;
    }
    if (this.producerName !== SDK_NAME) return ProducerIdentityStatus.SdkNameMismatch;
    if (this.producerVersion !== SDK_VERSION) return ProducerIdentityStatus.SdkVersionMismatch;
    if (this.producerSchemaDigest !== SCHEMA_SET_SHA256) {
      return ProducerIdentityStatus.SchemaSetMismatch;
    }
    return ProducerIdentityStatus.ExactMatch;
  }

  empty(): boolean {
    return this.size() === 0;
  }

  size(): number {
    if (!this.validEnvelope) return 0;
    return this.payloadEntries.length;
  }

  count<T>(selector: PayloadClass<T> | ExternalKey): number {
    if (!this.validEnvelope) return 0;
    if (is_external_key(selector)) {
      const id = resolveExternalKey(selector);
      return this.payloadEntries.filter((entry) => entry.id === id).length;
    }

    const id = resolvePayloadClass(selector);
    return this.payloadEntries.filter((entry) => {
      if (entry.id !== id) return false;
      return this.decodePayloadEntry<T>(id, entry.blob) !== null;
    }).length;
  }

  // query
  contains<T>(selector: PayloadClass<T> | ExternalKey): boolean {
    return this.count(selector) > 0;
  }

  // access
  private decodePayloadEntry<T>(id: bigint, blob: Uint8Array): T | null {
    const info = _TYPE_REGISTRY.get(id);
    if (!info) return null;
    const blobCopy = new Uint8Array(blob);
    if (info.verify && !info.verify(blobCopy)) return null;
    try {
      return info.decode(blobCopy) as T;
    } catch {
      return null;
    }
  }

  private valueAtId<T>(id: bigint, index: number): T | null {
    if (!this.validEnvelope) return null;
    let seen = 0;
    for (const entry of this.payloadEntries) {
      if (entry.id !== id) continue;
      const value = this.decodePayloadEntry<T>(id, entry.blob);
      if (value === null) continue;
      if (seen === index) return value;
      seen += 1;
    }
    return null;
  }

  private externalValueAtId(id: bigint, index: number): Uint8Array | null {
    if (!this.validEnvelope) return null;
    let seen = 0;
    for (const entry of this.payloadEntries) {
      if (entry.id !== id) continue;
      if (seen === index) return new Uint8Array(entry.blob);
      seen += 1;
    }
    return null;
  }

  get<T>(payloadType: PayloadClass<T>, index?: number): T | null;
  get(key: ExternalKey, index?: number): Uint8Array | null;
  get<T>(selector: PayloadClass<T> | ExternalKey, index = 0): T | Uint8Array | null {
    if (is_external_key(selector)) {
      return this.externalValueAtId(resolveExternalKey(selector), index);
    }

    const id = resolvePayloadClass(selector);
    return this.valueAtId<T>(id, index);
  }

  for_each<T>(payloadType: PayloadClass<T>): IterableIterator<T>;
  for_each(key: ExternalKey): IterableIterator<Uint8Array>;
  *for_each<T>(selector: PayloadClass<T> | ExternalKey): IterableIterator<T | Uint8Array> {
    if (is_external_key(selector)) {
      const id = resolveExternalKey(selector);
      if (!this.validEnvelope) return;
      for (const entry of this.payloadEntries) {
        if (entry.id === id) yield new Uint8Array(entry.blob);
      }
      return;
    }

    const id = resolvePayloadClass(selector);
    for (const entry of this.payloadEntries) {
      if (entry.id !== id) continue;
      const value = this.decodePayloadEntry<T>(id, entry.blob);
      if (value !== null) yield value;
    }
  }

  // mutation
  add(value: NativePayload): void;
  add(key: ExternalKey, blob: Uint8Array | ArrayBuffer): void;
  add(value: NativePayload | ExternalKey, blob?: Uint8Array | ArrayBuffer): void {
    if (!this.validEnvelope) {
      throw new Error(`cannot add to invalid ${SDK_NAME} envelope: ${this.errorMessage ?? 'unknown error'}`);
    }

    if (is_external_key(value)) {
      if (blob === undefined) {
        throw new Error(`${SDK_NAME} external add requires a byte buffer`);
      }
      this.payloadEntries.push({
        id: resolveExternalKey(value),
        blob: copyBytes(blob),
      });
      return;
    }

    if (blob !== undefined) {
      throw new Error(`${SDK_NAME} external add requires ExternalKey; call external_key(...) first`);
    }

    const id = resolveNativePayload(value);

    this.payloadEntries.push({
      id,
      blob: packNativePayload(id, value),
    });
  }

  serialize(): Uint8Array {
    if (!this.validEnvelope) {
      throw new Error(`cannot serialize invalid ${SDK_NAME} envelope: ${this.errorMessage ?? 'unknown error'}`);
    }

    const builder = new Builder(1024);
    const payloadOffsets = this.payloadEntries.map((entry) => {
      const blobOffset = Payload.createBlobVector(builder, entry.blob);
      return Payload.createWirePayload(builder, entry.id, blobOffset);
    });
    const payloadsOffset = FbEnvelope.createPayloadsVector(builder, payloadOffsets);
    const producerNameOffset = builder.createString(SDK_NAME);
    const producerVersionOffset = builder.createString(SDK_VERSION);
    const producerSchemaDigestOffset = builder.createString(SCHEMA_SET_SHA256);
    const envelopeOffset = FbEnvelope.createWireEnvelope(
      builder,
      payloadsOffset,
      producerNameOffset,
      producerVersionOffset,
      producerSchemaDigestOffset,
    );
    FbEnvelope.finishWireEnvelopeBuffer(builder, envelopeOffset);
    return builder.asUint8Array();
  }
}
    """
    ).lstrip().replace("__SDK_NAME__", sdk_name).replace(
        "__SDK_VERSION__", sdk_version
    ).replace(
        "__SCHEMA_SET_SHA256__", schema_set_digest
    ).replace(
        "__EXTERNAL_KEY_MIN__",
        str(RESERVED_PAYLOAD_ID_MIN),
    ).replace(
        "__SDK_INTERNAL_NS_PATH__",
        _sdk_namespace_module_path(sdk_name),
    )


def _index_text(sdk_name: str, sdk_version: str, schema_set_digest: str) -> str:
    return TS_GENERATED_BANNER + textwrap.dedent(
        f"""
  export const {sdk_name.upper()}_VERSION = '{sdk_version}';
  export const {sdk_name.upper()}_NAME = '{sdk_name}';
  export const SCHEMA_SET_SHA256 = '{schema_set_digest}';
  export const FLATBUFFERS_VERSION_REQUIREMENT = '{FLATBUFFERS_TYPESCRIPT_REQUIREMENT}';
  export {{
    Envelope,
    ExternalKey,
    ProducerIdentityStatus,
    external_key,
    is_external_key,
    EXTERNAL_KEY_MIN,
  }} from './envelope.js';
        """
    ).lstrip()


def _package_json_text(sdk_name: str, sdk_version: str, public_name: str | None = None) -> str:
    package_name = public_name or sdk_name
    return textwrap.dedent(
        """
        {
          "name": "__SDK_NAME__",
          "version": "__SDK_VERSION__",
          "description": "Generated __SDK_NAME__ TypeScript SDK",
          "type": "module",
          "main": "./dist/__SDK_NAME__/index.js",
          "types": "./dist/__SDK_NAME__/index.d.ts",
          "exports": {
            ".": {
              "types": "./dist/__SDK_NAME__/index.d.ts",
              "import": "./dist/__SDK_NAME__/index.js"
            },
            "./fb/*.js": {
              "types": "./dist/__SDK_NAME__/fb/*.d.ts",
              "import": "./dist/__SDK_NAME__/fb/*.js"
            }
          },
          "scripts": {
            "build": "tsc -p tsconfig.json",
            "typecheck": "tsc -p tsconfig.json --noEmit"
          },
          "dependencies": {
            "flatbuffers": "__FLATBUFFERS_VERSION_REQUIREMENT__"
          },
          "devDependencies": {
            "typescript": "^5.5.0"
          }
        }
        """
    ).lstrip().replace("__SDK_NAME__", package_name).replace("__SDK_VERSION__", sdk_version).replace(
        "__FLATBUFFERS_VERSION_REQUIREMENT__", FLATBUFFERS_TYPESCRIPT_REQUIREMENT
    ).replace(
        f'"name": "{package_name}"',
        f'"name": "{package_name.replace("_", "-")}"',
    )


def _tsconfig_text() -> str:
    return textwrap.dedent(
        """
        {
          "compilerOptions": {
            "target": "ES2020",
            "module": "ES2020",
            "moduleResolution": "Bundler",
            "strict": true,
            "declaration": true,
            "outDir": "dist",
            "rootDir": "src",
            "skipLibCheck": true,
            "forceConsistentCasingInFileNames": true
          },
          "include": ["src/**/*.ts"]
        }
        """
    ).lstrip()


def _write_text(path: Path, content: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    return path


def generate_typescript_sdk(entries: list[SchemaEntry], ctx: GenerationContext) -> list[Path]:
    from ..schema_set import schema_set_sha256

    schema_set_digest = schema_set_sha256(ctx)
    project_root = _ts_project_root(ctx)
    src_root = project_root / "src"
    package_root = src_root / ctx.effective_public_name
    fb_root = package_root / "fb"
    if src_root.exists():
        shutil.rmtree(src_root)
    package_root.mkdir(parents=True, exist_ok=True)

    generated: list[Path] = []

    with tempfile.TemporaryDirectory(prefix=f"{ctx.sdk_name}-ts-envelope-") as tmp:
        envelope_schema = Path(tmp) / "envelope.fbs"
        envelope_schema.write_text(
            ENVELOPE_SCHEMA_TEMPLATE.replace("__SDK_NAME__", ctx.sdk_name),
            encoding="utf-8",
        )
        _run_flatc_ts([envelope_schema], fb_root, ctx.flatc_bin)

    _run_flatc_ts(ctx.schema_paths, fb_root, ctx.flatc_bin, include_dirs=[ctx.schema_dir])

    generated.extend(sorted(path for path in fb_root.rglob("*.ts") if "__pycache__" not in path.parts))

    generated.append(_write_text(package_root / "registry.ts", _registry_text(entries)))
    generated.append(
        _write_text(
            package_root / "envelope.ts",
            _envelope_text(ctx.sdk_name, str(ctx.sdk_version), schema_set_digest),
        )
    )
    generated.append(
        _write_text(
            package_root / "index.ts",
            _index_text(ctx.sdk_name, str(ctx.sdk_version), schema_set_digest),
        )
    )
    generated.append(
        _write_text(
            project_root / "package.json",
            _package_json_text(ctx.sdk_name, str(ctx.sdk_version), ctx.effective_public_name),
        )
    )
    generated.append(_write_text(project_root / "tsconfig.json", _tsconfig_text()))

    return sorted(set(generated))
