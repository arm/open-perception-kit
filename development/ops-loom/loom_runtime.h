/**
 * @file loom_runtime.h
 * @brief Proposed LOOM generated-runtime consumer ABI, draft contract version 10.
 *
 * Design example only: not an implemented or agreed Arm/LOOM API.
 * See loom-runtime-contract-proposal.md for scope and rationale, and
 * runtime-memory-use-cases.md for backend-specific support assessments.
 *
 * Scope: local Inference Device integration, including Dolphin9. No RPC,
 * service discovery, tile scheduling API, or Inference Native placement API.
 * A generated runtime implements this ABI; ONNX/ExecuTorch/MLFW need not.
 * Contexts are runtime-owned execution environments, potentially spanning CPUs
 * and accelerators. Placement, transfers and internal synchronization belong
 * to the runtime.
 *
 * Binding usage:
 * - bind_host_tensor borrows CPU-addressable tensor storage directly: no import,
 *   resource handle or release_buffer call. Use for CPU-preprocessed tensors.
 * - import_buffer + bind_tensor retains a reusable resource handle. Use for
 *   DMA-BUF/device imports, stable resource pools or multiple allocation views.
 * - allocate_buffer + bind_tensor requests reusable runtime-owned storage.
 *
 * A camera frame is not automatically a model tensor. If CPU preprocessing
 * fills a fixed tensor allocation, bind that allocation once and refill it
 * between completed submissions; changing camera buffers need no imports.
 * If a frame already contains compatible tensor data, bind its host pointer
 * per frame. Keep its owner and mapping alive until completion and unbinding.
 * Successive GStreamer frames need not have stable backing storage.
 *
 * ABI rules:
 * - C11 / C++11 compatible; native platform calling ABI and pointer width.
 *   This is not a wire format or a cross-architecture binary format.
 * - No exceptions, C++ objects, ownership of malloc allocations, or packing
 *   pragmas cross the boundary. Backend code catches its own exceptions.
 * - One global contract version covers every record, function signature and
 *   table layout, including extensions. Both sides must match exactly. Any
 *   layout or contract change requires a new LRT_RUNTIME_VERSION value.
 * - Callers zero-initialize input records. There are no per-record size or
 *   version fields and no partial-table or layout compatibility negotiation.
 * - All counts and byte ranges are checked, including arithmetic overflow.
 *   Nonzero counts require nonnull arrays. Rank zero is a scalar (one value).
 * - Unless stated otherwise, out-parameters are untouched on failure. All
 *   descriptor pointers are borrowed for the call; runtime-owned returned
 *   metadata must never be freed by the caller.
 *
 * @section lrt_sessions Model and session lifetime
 *
 * A model holds the loaded network and intrinsic input/output metadata; weights
 * and immutable compiled state may be shared. A session holds independent
 * bindings, scratch and persistent network state. create_session(model, NULL,
 * &session) uses static model shapes without repeating input count or shapes.
 * Only unresolved dynamic inputs need indexed shape overrides in options;
 * dynamic batch is just one such dimension. No override changes fixed network
 * dimensions. Query resolved tensor requirements after creating the session.
 * This replaces the earlier prepare operation; it is not an additional stage.
 *
 * @section lrt_memory_categories Memory categories at the application boundary
 *
 * The following properties describe tensor storage and its use:
 *
 * | Property | Meaning |
 * | --- | --- |
 * | Owner | Runtime or External: who manages allocation reclamation/reuse. |
 * | Accessibility | Host, runtime devices, other devices, or a combination. |
 * | Reference | Host pointer, DMA-BUF FD, or a typed native resource/context. |
 * | Transfer | Caller-selected DIRECT or STAGED execution at this boundary. |
 * | Output handoff | H1 borrowed, H2 retained lease, or H3 external storage. |
 *
 * External includes OPK, GStreamer and application-owned pools. A pool is not
 * a third ownership category. Owning a resource reference does not necessarily
 * own the backing allocation or prevent a producer from reusing its contents.
 * Runtime devices are those used internally by the execution environment;
 * they can include both CPU and AI Cluster resources. Other devices are outside
 * that environment. Physical residence is not inferred from accessibility.
 *
 * Host mapping exposes a CPU view of an allocation; it is not a memory owner
 * or separate location. DMA-BUF is a sharing reference, not a location or a
 * guarantee of host mapping, runtime compatibility, synchronization or direct
 * execution. Shared host/device access does not permit unsynchronized access.
 *
 * **Select input and output categories independently, for each tensor.**
 * Corresponding I/O row numbers below describe analogous mechanisms, not
 * required pairings. I2 input with O5 output, for example, is a possible design;
 * it is not a promise that any backend supports that combination. Context
 * capabilities describe facilities; preparation and binding validate the
 * actual tensor, allocation and selected combination. There is no scenario
 * enum or global input/output memory mode.
 *
 * @subsection lrt_category_paths Category-to-API mapping
 *
 * The table describes paths representable by this draft, not platform support.
 * A category absent from the core requires a specified extension or an explicit
 * application-side copy into a supported category. Each selected path still
 * needs compatible dtype, layout, shape, strides, bounds and access rights.
 *
 * | Case | Owner / accessibility / reference | Representation in this draft |
 * | --- | --- | --- |
 * | I1 | Runtime / Host / pointer | Select runtime POINTER configuration; allocate_buffer, get_buffer_info, bind_tensor; caller fills storage. |
 * | I2 | External / Host / pointer | Select external POINTER configuration; bind_host_tensor or reusable import_buffer plus bind_tensor. |
 * | I3 | Runtime / runtime devices / native resource | Select Vulkan option, allocate_buffer with Vulkan details, bind_tensor; get_buffer_info returns borrowed native view. |
 * | I4 | External / runtime devices / native resource | import_buffer with typed Vulkan resource, then bind_tensor. |
 * | I5 | Runtime / Host + runtime devices / mapped pointer and resource | POINTER option or native Vulkan buffer option with optional CPU mapping; image HOST access is excluded. |
 * | I6 | External / Host + runtime devices / mapped pointer and resource | Bind CPU view or import Vulkan buffer with its typed metadata and supported mapping. |
 * | I7 | External / accessibility conditional / DMA-BUF | import_buffer(DMA_BUF), bind_tensor; optional Linux extension for external fences and CPU mapping. |
 * | I8 | External / other devices / native reference, optional host view | Supported sharing/import extension, or use a valid host view/copy. No generic foreign address import. |
 * | O1 | Runtime / Host / pointer | Select runtime POINTER configuration, allocate_buffer and bind_tensor; get_outputs/get_buffer_info expose the view. H1. |
 * | O2 | External / Host / pointer | bind_host_tensor, or reusable HOST import plus bind_tensor. H3. |
 * | O3 | Runtime / runtime devices / native resource | Allocate selected Vulkan resource, bind, execute, query borrowed native view. H1. |
 * | O4 | External / runtime devices / native resource | Import caller's Vulkan destination and bind it. H3. |
 * | O5 | Runtime / Host + runtime devices / mapped pointer and resource | POINTER option or Vulkan buffer with supported CPU mapping/native view. H1. |
 * | O6 | External / Host + runtime devices / mapped pointer and resource | Bind CPU view or import compatible Vulkan buffer; wait then bracket CPU access. H3. |
 * | O7 | External / accessibility conditional / DMA-BUF | Import caller's destination before binding; this is import, not export of runtime output. H3. |
 * | O8 | External / other devices / native reference, optional host view | Supported sharing/import extension or explicit transfer to destination. H3. |
 *
 * POINTER identifies a reference, not physical placement. The configuration's
 * accessibility bits state the minimum access guaranteed for that selection.
 * DIRECT excludes boundary staging; it does not reveal native resource
 * identity, prove simultaneous host/device access or prohibit internal copies.
 * Native Vulkan identity is described in loom_runtime_vulkan.h; no Vulkan types
 * or headers are required by this core interface. No physical placement is
 * inferred from a pointer or an accessibility bit.
 *
 * @subsection lrt_negotiation Discover, select, bind and validate
 *
 * 1. Create a session using intrinsic model shapes, resolving dynamic inputs
 *    through optional shape overrides, then query tensor requirements.
 * 2. get_io_requirements returns each tensor's shape, strides and ranked memory
 *    options together. Inspect configurations, exact transfer modes, constraints and
 *    output handoff. No separate memory-discovery call is needed.
 * 3. Choose options the application can supply/consume. Copy the selected
 *    complete configuration into each binding, including its transfer mode.
 *    Runtime POINTER inputs use allocate_buffer first, then get_buffer_info
 *    to obtain storage preprocessing can fill. External POINTER inputs can
 *    use bind_host_tensor when the option permits it, with no import handle.
 * 4. Call validate_bindings once all choices/resources are bound. A successful
 *    bind validates one resource, not the Cartesian product of IO choices.
 * 5. Fill inputs, establish producer readiness, then call infer (blocking) or
 *    infer_async followed by wait. Repeat
 *    without renegotiation while bindings remain unchanged. Rebinding a ring
 *    slot requires validate_bindings again, not a new session. Changing shapes
 *    requires a new session.
 *
 * No option IDs are used. Selection compares owner/accessibility/reference/transfer.
 * Runtime ranking is advisory: only the caller selects an option. An unsupported
 * concrete resource or combination fails without selecting a different option,
 * changing transfer mode, allocating fallback output or retrying inference.
 * External producer fences are
 * invocation dependencies supplied through optional infer_async_with_fences;
 * they are not a per-buffer binding mode. Core inference calls require the
 * caller to finish external producer work before runtime access. Runtime
 * completions cannot be supplied as dependencies: execution chaining is omitted.
 * Different constraints for the same configuration must use a conservative common
 * subset in this draft; correlated alternatives are not represented. The
 * complete-set validator reports incompatible combinations before execution,
 * but there is no machine-readable list of allowed combinations or atomic
 * multi-tensor binding transaction yet. Binding/setup cost is not promised to
 * be negligible and should be measured for per-frame resource changes.
 *
 * @subsection lrt_output_handoff Output ownership, completion and reuse
 *
 * | Handoff | Meaning | Current API coverage |
 * | --- | --- | --- |
 * | H1 | Runtime storage borrowed within a validity window. | Result views are borrowed until accepted inference, binding change or session destruction. Caller explicitly allocates/binds runtime storage and prevents overwrite while consumed. |
 * | H2 | Runtime storage pinned against reuse until consumers release leases. | Not defined. A retained completion or buffer handle is not an output lease. |
 * | H3 | Runtime writes into externally managed storage. | Bind caller host storage or an imported destination; caller retains storage and controls downstream reuse. |
 *
 * Production completion and consumer completion are different events. A wait
 * establishes the documented production/visibility guarantees, not permission
 * to overwrite data still read downstream. An idle session can rebind a different
 * external output slot for its next invocation after production finishes. Copy
 * required output metadata first: rebinding invalidates get_outputs views even
 * though it does not reclaim external storage. Unbind a slot before releasing
 * its import or host mapping, and reuse it only after downstream completion.
 * Separate sessions may be needed to keep distinct borrowed runtime outputs.
 * Binding does not acquire an external pool-slot lease on the caller's behalf.
 *
 * @subsection lrt_backend_relationship Vulkan, MLWL and GStreamer
 *
 * **Vulkan:** a resource (buffer/image/tensor), its backing memory allocation,
 * and its host mapping are distinct. HOST_VISIBLE allows mapping; DEVICE_LOCAL
 * can coexist with it; HOST_COHERENT removes explicit host flush/invalidate
 * requirements, not execution/visibility dependencies. Device imports must
 * identify a typed resource and compatible logical-device/context association,
 * layout/usage and synchronization. A bare device address is insufficient for
 * such an import. See loom_runtime_vulkan.h for the optional native profile,
 * including its deliberately restricted queue and image-layout handoff rules.
 *
 * **ML Workload Library (MLWL):** its binding API borrows Vulkan resources and
 * prepared executions capture bindings. Host-visible buffer/tensor helpers fit
 * shared-access cases, but do not implement arbitrary host-pointer or DMA-BUF
 * binding. An adapter must implement staging/imports, host-read visibility and
 * the lifetime rules promised here. Changing captured resources may require
 * another prepared snapshot; this ABI's bind call does not promise zero setup
 * cost. MLWL features do not establish AI Cluster platform compatibility.
 *
 * **GStreamer:** GstBuffer carries allocator-specific GstMemory references.
 * Host views fit HOST binding; compatible typed device memory needs a native
 * extension; DMA-BUF memory fits the optional import profile. Retain the input
 * buffer/pool slot and required mapping through execution and remove borrowed
 * host bindings before unmapping. Retaining an FD preserves a resource payload,
 * but does not stop a camera/pool producer from overwriting the same slot.
 *
 * A GStreamer output wrapper must hold an actual H2 lease (not provided here),
 * use H3 external storage, or respect H1's validity window by copying/consuming
 * or delaying runtime reuse. Destroy notifications and pool return must account
 * for asynchronous device consumers. Refcounting is not device synchronization.
 * Mapping/writability operations may copy, so obtain the final storage before
 * binding and do not replace it while bound. A camera video allocation still
 * needs the network's required preprocessing/tensor representation.
 *
 * Further detail and primary references:
 * - [Memory use cases and backend tables](runtime-memory-use-cases.md)
 * - [MLWL source analysis](mlwl-vulkan-memory-analysis.md)
 * - [Vulkan memory](https://docs.vulkan.org/spec/latest/chapters/memory.html)
 * - [GStreamer allocation](https://gstreamer.freedesktop.org/documentation/plugin-development/advanced/allocation.html)
 */
#ifndef LOOM_RUNTIME_H
#define LOOM_RUNTIME_H

#include <stdint.h>

#if defined(_WIN32)
#define LRT_CALL __cdecl
#else
#define LRT_CALL
#endif

/* Override when building a shared library, e.g. __declspec(dllexport). */
#ifndef LRT_EXPORT
#define LRT_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Consumer compile-time expectation; distinct from implementation release ID. */
#define LRT_RUNTIME_VERSION UINT32_C(10)

/* Defined by the runtime library, immutable for its entire loaded lifetime.
 * Must equal both LRT_RUNTIME_VERSION and api.get_runtime_version().
 * This and lrt_get_api are the stable bootstrap exports. */
LRT_EXPORT extern const uint32_t lrt_runtime_version;
#define LRT_WAIT_FOREVER UINT64_MAX
#define LRT_DIM_DYNAMIC INT64_C(-1)

typedef struct lrt_context_impl *lrt_context;
typedef struct lrt_model_impl *lrt_model;
typedef struct lrt_session_impl *lrt_session;
typedef struct lrt_buffer_impl *lrt_buffer;
typedef struct lrt_completion_impl *lrt_completion;

/* Optional typed payloads; definitions and Vulkan includes stay in the companion. */
typedef struct lrt_vulkan_requirements lrt_vulkan_requirements;
typedef struct lrt_vulkan_resource lrt_vulkan_resource;
typedef struct lrt_vulkan_allocation_desc lrt_vulkan_allocation_desc;

/* Fixed-width tags deliberately avoid compiler-dependent C enum storage. */
typedef int32_t lrt_status;
enum {
    LRT_OK = 0,
    LRT_INVALID_ARGUMENT = 1,
    LRT_UNSUPPORTED = 2,
    LRT_INCOMPATIBLE = 3,
    LRT_OUT_OF_MEMORY = 4,
    LRT_BUSY = 5,
    LRT_TIMEOUT = 6,
    LRT_EXECUTION_FAILED = 7,
    LRT_STATE_INVALID = 8,
    LRT_BUFFER_TOO_SMALL = 9,
    LRT_IO_ERROR = 10,
    LRT_DEVICE_LOST = 11
};

typedef uint32_t lrt_dtype;
enum {
    LRT_FLOAT32 = 1, LRT_FLOAT16 = 2, LRT_BFLOAT16 = 3,
    LRT_INT8 = 4, LRT_UINT8 = 5, LRT_INT16 = 6,
    LRT_INT32 = 7, LRT_INT64 = 8, LRT_BOOL8 = 9
};

typedef uint32_t lrt_io;
enum { LRT_INPUT = 1, LRT_OUTPUT = 2 };
typedef uint32_t lrt_access;
enum { LRT_READ = 1, LRT_WRITE = 2, LRT_READ_WRITE = 3 };
/** @brief Who manages the backing allocation, relative to this ABI boundary. */
typedef uint32_t lrt_memory_owner;
enum { LRT_OWNER_RUNTIME = 1, LRT_OWNER_EXTERNAL = 2 };

/** @brief Minimum accessibility guaranteed by a selected configuration.
 * Bitmask: HOST | RUNTIME_DEVICE expresses access to the same allocation.
 * These are access guarantees, not simultaneous access or physical placement.
 * RUNTIME_DEVICE means at least one compatible runtime execution resource,
 * not all resources inside a heterogeneous runtime. OTHER_DEVICE requires a
 * defined sharing profile identifying that external access; not any device.
 * Zero is invalid. Additional access may exist; absent bits do not prohibit it.
 */
typedef uint32_t lrt_memory_accessibility;
enum {
    LRT_ACCESSIBLE_HOST = 1,
    LRT_ACCESSIBLE_RUNTIME_DEVICE = 2,
    LRT_ACCESSIBLE_OTHER_DEVICE = 4
};

/** @brief Typed reference used to name storage, not an extra location field.
 * POINTER and DMA_BUF have core payloads. VULKAN_BUFFER and VULKAN_IMAGE use
 * typed pointers defined in loom_runtime_vulkan.h. An unsupported profile must
 * not be advertised. No generic DEVICE address is accepted.
 */
typedef uint32_t lrt_memory_reference;
enum {
    LRT_REFERENCE_POINTER = 1,
    LRT_REFERENCE_DMA_BUF = 2,
    LRT_REFERENCE_VULKAN_BUFFER = 3,
    LRT_REFERENCE_VULKAN_IMAGE = 4
};

/** @brief Exact transfer behavior chosen by the caller, not a fallback policy.
 * DIRECT: no IO staging copy at the application/runtime boundary. Internal
 * network transfers, packing, scratch and partitioning remain runtime-owned.
 * STAGED: execution uses IO staging between the supplied allocation and internal
 * tensor storage. Output copies finish before successful wait returns.
 * Zero/unknown values are invalid; neither mode permits automatic substitution.
 */
typedef uint32_t lrt_memory_transfer;
enum { LRT_TRANSFER_DIRECT = 1, LRT_TRANSFER_STAGED = 2 };

/** @brief Classification and exact selection key; contains no opaque option ID.
 * Equality compares these four fields, never padding bytes. Accessibility is
 * a minimum guarantee of the selected path, not a caller's assertion. Binding
 * checks the concrete allocation. Owner does not transfer on binding/import.
 */
typedef struct lrt_memory_configuration {
    lrt_memory_owner owner;
    lrt_memory_accessibility accessibility;
    lrt_memory_reference reference;
    lrt_memory_transfer transfer;
} lrt_memory_configuration;

typedef uint32_t lrt_output_handoff;
enum {
    LRT_HANDOFF_NONE = 0,     /* Input: no output handoff. */
    LRT_HANDOFF_BORROWED = 1, /* H1, runtime-owned output. */
    LRT_HANDOFF_RETAINED = 2, /* H2 reserved; MUST NOT be advertised yet. */
    LRT_HANDOFF_EXTERNAL = 3  /* H3, externally managed output storage. */
};

typedef uint32_t lrt_compile_policy;
enum { LRT_ALLOW_COMPILE = 0, LRT_PRECOMPILED_ONLY = 1 };

/** @brief How a POINTER option can be supplied. At least one flag is required.
 * Runtime-owned POINTER options use allocate_buffer plus bind_tensor. External
 * POINTER options may permit borrowed bind_host_tensor, reusable imports, or
 * both. These flags do not change resource ownership or copy policy.
 */
typedef uint32_t lrt_pointer_binding_flags;
enum {
    LRT_POINTER_BORROWED_BIND = 1,
    LRT_POINTER_REUSABLE_IMPORT = 2,
    LRT_POINTER_RUNTIME_ALLOCATION = 4
};

/** @brief One ranked configuration and the requirements needed to select it.
 * All counts/arrays and nested metadata are immutable until session destruction.
 * Each option specifies one exact transfer mode in configuration. Neither rank
 * nor DIRECT is a measured end-to-end performance claim. The caller selects the
 * complete configuration; bind accepts that exact choice or returns an error.
 * Runtime never substitutes another configuration or transfer mode. DIRECT and
 * STAGED options with otherwise equal fields are distinct selectable options.
 *
 * If two alternatives with the same configuration cannot share these
 * constraints, advertise a conservative common subset, not duplicate keys.
 * This limits expressiveness intentionally; distinct profiles may be needed
 * later. Capacity/alignment are additional to the containing tensor requirement
 * (satisfy both). Every option uses that tensor's single required stride layout.
 * Dtype/layout come from model metadata; no implicit conversion is allowed.
 *
 * POINTER uses pointer_binding_flags, other references set that field to zero.
 * DMA_BUF covers tensor byte ranges, not arbitrary video/image plane layouts.
 * HOST accessibility for DMA_BUF requires the CPU_MAP extension capability.
 *
 * vulkan is required for Vulkan references and NULL otherwise. It identifies
 * the device/queue and concrete native constraints, with companion definitions.
 * Native options require LRT_EXTENSION_VULKAN support. Runtime-owned POINTER
 * options require HOST access; runtime-owned Vulkan options may be device-only.
 * Runtime-owned DMA-BUF export still needs a separately defined profile.
 * OTHER_DEVICE accessibility cannot be advertised without a defined profile
 * describing which external resource/device can access the allocation.
 *
 * Advertised pointer methods must agree with context capabilities: BORROWED_BIND needs HOST_BIND,
 * REUSABLE_IMPORT needs HOST_IMPORT, and RUNTIME_ALLOCATION requires that the
 * runtime can allocate the requested configuration. No capability bit alone
 * substitutes for an advertised per-tensor option.
 *
 * Handoff is NONE for inputs, BORROWED for runtime outputs, EXTERNAL for external
 * outputs. H2 is not implemented. Ranking does not depend on current bindings;
 * options are returned in tensor requirements for resolved session shapes and
 * the actual host environment, rather than generic backend capabilities.
 */
typedef struct lrt_memory_option {
    lrt_memory_configuration configuration;
    lrt_output_handoff output_handoff;
    uint64_t minimum_byte_capacity;
    uint64_t byte_alignment; /* Nonzero power of two. */
    lrt_pointer_binding_flags pointer_binding_flags;
    const lrt_vulkan_requirements *vulkan;
} lrt_memory_option;

/* Context capabilities describe available facilities, not compatibility of
 * every model/layout/allocation. Session creation/import/bind still validates. */
typedef uint64_t lrt_capability_flags;
#define LRT_CAP_HOST_IMPORT       (UINT64_C(1) << 0)
#define LRT_CAP_DMA_BUF_IMPORT    (UINT64_C(1) << 1)
#define LRT_CAP_ASYNC_INFERENCE   (UINT64_C(1) << 2)
#define LRT_CAP_PARALLEL_SESSIONS (UINT64_C(1) << 3)
#define LRT_CAP_DYNAMIC_SHAPES    (UINT64_C(1) << 4)
/* Direct pointer binding is independent of reusable HOST_IMPORT support. */
#define LRT_CAP_HOST_BIND         (UINT64_C(1) << 5)
#define LRT_CAP_SYNC_INFERENCE    (UINT64_C(1) << 6)
/* At least one inference mode is implemented. Both core entries are nonnull;
 * an unimplemented mode returns UNSUPPORTED without accepting work. There is
 * no caller-visible fallback from one execution mode to the other. */

typedef struct lrt_capabilities {
    lrt_capability_flags flags;
    uint32_t max_rank;
    /* Borrowed UTF-8 strings, valid until context destruction. */
    const char *runtime_id;
    const char *runtime_version; /* Implementation release; not ABI compatibility. */
} lrt_capabilities;

/** @brief Source of a model/package, independent of inference tensor memory. */
typedef uint32_t lrt_model_source_kind;
enum { LRT_MODEL_SOURCE_PATH = 1, LRT_MODEL_SOURCE_MEMORY = 2 };

/** @brief Load from a local path or a contiguous, host-readable byte range.
 * PATH is a nonempty NUL-terminated UTF-8 local package/file path, not an RPC
 * endpoint. MEMORY supplies nonnull data and a nonzero byte_length representable
 * in the runtime's host address space. Bytes are the backend's serialized model
 * or self-contained package format, not an executable pointer or tensor input.
 * The tag selects the only active union member; unknown tags are invalid.
 *
 * Both the descriptor and its selected storage are borrowed only for load_model.
 * Caller keeps memory readable and unchanged until the call returns. The runtime
 * owns any retained bytes/state on success (copying or decoding as needed), and
 * never frees caller memory. The caller may free/unmap the source on return,
 * including on failure. No deferred access to the original range is permitted.
 * This deliberately omits a borrowed-until-unload mode and lifetime callbacks.
 *
 * MEMORY must be self-contained: there is no implicit base path, external-file
 * resolver or automatic filesystem fallback. Unsupported package formats or
 * packages requiring external resources fail explicitly. Implementations must
 * support MEMORY for their loadable self-contained serialized format; consumers
 * need not materialize a temporary file. Format/security validation applies to
 * both sources; no generic ONNX/PTE/VGF format is implied by this ABI.
 */
typedef struct lrt_model_source {
    lrt_model_source_kind kind;
    union {
        const char *path;
        struct {
            const void *data;
            uint64_t byte_length;
        } memory;
    } source;
} lrt_model_source;

typedef struct lrt_quantization {
    uint32_t parameter_count; /* 0 = none, 1 = per-tensor, otherwise per-axis. */
    int32_t axis;             /* -1 for none/per-tensor. */
    const double *scales;
    const int64_t *zero_points;
} lrt_quantization;

/* Inputs/outputs are network tensors, not structured perception results.
 * Optional interpretation metadata belongs in the model/package descriptor.
 * OPK owns output decoding and structured results. If decoding/NMS is fused
 * into the network, the descriptor must document the resulting tensor format.
 */
typedef struct lrt_tensor_info {
    const char *name;         /* Stable name; indices follow description order. */
    lrt_dtype dtype;
    uint32_t rank;
    const int64_t *dimensions; /* LRT_DIM_DYNAMIC where runtime-selected. */
    const uint64_t *minimum_dimensions; /* rank entries, including fixed dims. */
    const uint64_t *maximum_dimensions; /* 0 = no declared upper bound. */
    const char *layout;       /* Versioned identifier, e.g. "tensor/nchw/v1". */
    lrt_quantization quantization;
} lrt_tensor_info;

typedef struct lrt_model_info {
    const char *model_id;
    uint32_t input_count;
    uint32_t output_count;
    /* Tensor descriptors are borrowed for the model lifetime. */
    const lrt_tensor_info *const *inputs;
    const lrt_tensor_info *const *outputs;
    uint32_t stateful;       /* 0 or 1; state is session-local. */
    const char *state_schema;
    const char *initial_and_reset_state_json;
} lrt_model_info;

typedef struct lrt_shape {
    uint32_t rank;
    const uint64_t *dimensions;
} lrt_shape;

/** @brief Resolve one dynamic input using its model-description index.
 * Supplies the entire concrete shape for that input; fixed dimensions must
 * match the model and dynamic ones must satisfy its constraints. Batch size
 * is adjustable only if that dimension is dynamic. This cannot redefine the
 * network's input count or dtype/layout. Repeating a static shape is allowed
 * but unnecessary; changing a static dimension is rejected.
 */
typedef struct lrt_input_shape_override {
    uint32_t input_index;
    lrt_shape shape;
} lrt_input_shape_override;

/** @brief Optional session settings; not a second model input description.
 * Zero initialization means ALLOW_COMPILE and no overrides. Each input_index
 * may occur at most once. Unmentioned inputs use their intrinsic concrete model
 * shapes. An unresolved dynamic input requires an override; no batch/dimension
 * value is guessed from minimum/maximum bounds. Counts describe only this
 * sparse override list, never the number of network inputs.
 * All descriptors/dimensions are borrowed for create_session only; the runtime
 * copies resolved shapes. There is no externally visible warmup operation.
 */
typedef struct lrt_session_options {
    lrt_compile_policy compile_policy;
    uint32_t shape_override_count;
    const lrt_input_shape_override *shape_overrides;
} lrt_session_options;

/** @brief Resolved tensor requirements, including ranked memory choices.
 * Returned by get_io_requirements, with all nested metadata immutable and
 * borrowed until session destruction. memory_options is best-to-worst runtime
 * preference, unique by all four configuration fields; the client may prefer another
 * option for its pipeline. Zero options means this tensor cannot be bound.
 * Binding checks allocation compatibility; validate_bindings checks the complete
 * set. Per-tensor support does not guarantee every combination works.
 *
 * All memory options share this tensor's required strides. A supplied binding
 * stride array must match; NULL selects these strides, not another layout.
 * Different physical layouts are not negotiated in this draft.
 * All inputs and outputs start unbound. There is no implicit output option or
 * allocation. For runtime-owned storage, caller selects a configuration, calls
 * allocate_buffer and binds its handle. NULL binding only unbinds the tensor.
 */
typedef struct lrt_tensor_requirement {
    lrt_shape shape;         /* Concrete; or rank=0 and data_dependent=1. */
    uint32_t data_dependent; /* Output shape is known only after execution. */
    uint64_t minimum_byte_capacity;
    uint64_t byte_alignment;
    /* Required strides for concrete shape (rank entries); NULL for rank zero. */
    const uint64_t *byte_strides;
    uint32_t memory_option_count;
    const lrt_memory_option *memory_options;
} lrt_tensor_requirement;

typedef struct lrt_io_requirements {
    uint32_t input_count;
    uint32_t output_count;
    const lrt_tensor_requirement *const *inputs;
    const lrt_tensor_requirement *const *outputs;
} lrt_io_requirements;

/** @brief Description of externally owned storage to import as a reusable view.
 * Import creates a runtime resource reference, not ownership of the producer's
 * pool slot. The caller prevents external reuse while execution/consumers need
 * the contents. Host storage remains alive while imported; DMA-BUF import
 * duplicates the FD but does not acquire a producer-buffer lease.
 */
typedef struct lrt_memory_desc {
    lrt_memory_reference reference; /* Tagged payload; ownership is External. */
    lrt_access access;       /* Access by inference, not CPU mapping rights. */
    uint64_t allocation_byte_size;
    uint64_t allocation_offset;
    uint64_t view_byte_length;
    union {
        void *host_base;     /* Allocation byte zero; borrowed, never freed. */
        int32_t dma_buf_fd;  /* Duplicated with close-on-exec on import success. */
        const lrt_vulkan_resource *vulkan;
    } handle;
    /* Vulkan: typed payload contains bounds; the three generic range fields
     * above must be zero. Payload graph is copied on success; native objects
     * remain externally owned and alive until the import is released. */
} lrt_memory_desc;

/** @brief Request reusable runtime-managed storage in a supported profile.
 * Configuration requests minimum accessibility and eligibility for the selected
 * transfer mode, not physical placement/export. Binding validates tensor-specific
 * compatibility. Transfer is not a permanent property of the backing allocation.
 * Unsupported allocation profiles return LRT_UNSUPPORTED; DMA-BUF import
 * support alone does not imply DMA-BUF allocation/export support.
 */
typedef struct lrt_allocation_desc {
    lrt_memory_configuration configuration; /* owner must be RUNTIME. */
    lrt_access access;
    uint64_t byte_length;
    uint64_t byte_alignment;
    /* Required for Vulkan references; NULL otherwise. Borrowed for call;
     * runtime copies any native metadata it retains. */
    const lrt_vulkan_allocation_desc *vulkan;
} lrt_allocation_desc;

/** @brief Actual owner/reference and guaranteed accessibility of a buffer view.
 * Import may initially establish only limited accessibility; tensor binding
 * validates any additional access promised by its selected configuration.
 * host_data alone does not establish coherence or consumer readiness.
 */
typedef struct lrt_buffer_info {
    /* Storage facts; transfer mode belongs to a tensor binding, not an allocation. */
    lrt_memory_owner owner;
    lrt_memory_accessibility accessibility;
    lrt_memory_reference reference;
    lrt_access access;
    uint64_t view_byte_length;
    /* Nonnull only for POINTER with HOST access. Borrowed for buffer lifetime. An idle
     * buffer still bound to a session may be filled/read by the caller. */
    void *host_data;
    /* Nonnull only for Vulkan references. Borrowed metadata for buffer lifetime;
     * native handles do not transfer destruction ownership. */
    const lrt_vulkan_resource *vulkan;
} lrt_buffer_info;

typedef struct lrt_tensor_binding {
    lrt_buffer buffer;
    uint64_t byte_offset;    /* Relative to the imported view, not allocation. */
    uint64_t byte_length;
    lrt_shape shape;
    const uint64_t *byte_strides; /* Must match requirements; NULL uses them. */
    lrt_memory_configuration configuration; /* Exact advertised configuration. */
} lrt_tensor_binding;

/** @brief Borrowed host tensor, independent of lrt_buffer.
 * data points at the first
 * tensor byte; byte_length is accessible capacity from that address, with no
 * additional allocation offset. Dtype/layout come from model metadata.
 * Inference reads inputs without modifying them; outputs must be writable.
 * Shape/stride descriptors are copied at binding; storage remains caller-owned.
 */
typedef struct lrt_host_tensor_binding {
    void *data;
    uint64_t byte_length;
    lrt_shape shape;
    const uint64_t *byte_strides; /* Must match requirements; NULL uses them. */
    lrt_memory_configuration configuration; /* EXTERNAL / includes HOST / POINTER. */
} lrt_host_tensor_binding;

typedef struct lrt_completion_info {
    uint32_t terminal;       /* 0 = pending, 1 = finished including failure. */
    lrt_status result;      /* Meaningful only when terminal=1. */
} lrt_completion_info;

/** @brief One result view; metadata lifetime is separate from external storage.
 * @see lrt_output_handoff
 */
typedef struct lrt_output {
    uint32_t tensor_index;
    lrt_tensor_binding tensor; /* Actual shape, range, strides and buffer. */
    /* Direct host output: tensor.buffer=NULL, byte_offset=0, and this points to
     * caller storage. tensor still describes actual shape/strides and capacity.
     * Otherwise NULL; access tensor.buffer normally. Reporting a direct host
     * output does not create an import handle. */
    void *host_data;
} lrt_output;

typedef struct lrt_outputs {
    uint32_t count;
    const lrt_output *const *items;
} lrt_outputs;

/* Optional Linux DMA-BUF synchronization / CPU access extension. Kernel and
 * driver support must be validated; importing a DMA-BUF does not imply
 * implicit synchronization, CPU mapping or direct binding support. */
#define LRT_EXTENSION_LINUX_DMA_BUF "loom.runtime.linux-dma-buf/1"
typedef uint64_t lrt_dma_buf_capability_flags;
/* SYNC_FILE covers asynchronous producer-fence submission and whole-invocation
 * fence export; advertise only with ASYNC_INFERENCE and both entries present. */
#define LRT_DMA_BUF_CAP_SYNC_FILE (UINT64_C(1) << 0)
#define LRT_DMA_BUF_CAP_CPU_MAP   (UINT64_C(1) << 1)

typedef struct lrt_cpu_mapping_impl *lrt_cpu_mapping;
typedef struct lrt_cpu_mapping_info {
    void *data;              /* First byte of imported view, not allocation. */
    uint64_t byte_length;
} lrt_cpu_mapping_info;

typedef struct lrt_linux_dma_buf_api {
    lrt_dma_buf_capability_flags capabilities;
    /* Same acceptance/asynchronous-return rules as core infer_async. Requires
     * ASYNC_INFERENCE and SYNC_FILE support. Runtime honors all producer fences
     * before touching IO without blocking this call until inference finishes.
     * Duplicates FDs on acceptance only; originals remain caller-owned.
     * NULL/count=0 means no fences. No runtime-completion chaining is accepted.
     * Synchronous-only callers wait for external producers before core infer. */
    lrt_status (LRT_CALL *infer_async_with_fences)(
        lrt_session session, uint32_t fence_count,
        const int32_t *acquire_sync_file_fds, lrt_completion *out_completion);
    /* Returns a new caller-owned close-on-exec sync_file FD, or UNSUPPORTED.
     * Its signalling establishes ordering, not execution success: also check
     * completion result. Export covers the whole invocation's output writes,
     * including CPU work, accelerator work and output transfers. If sync_file
     * cannot represent that ordering, return UNSUPPORTED. */
    lrt_status (LRT_CALL *export_completion_fence)(
        lrt_completion completion, int32_t *out_sync_file_fd);
    /* Caller first waits for producers/consumers as appropriate. These calls
     * bracket CPU cache access; they are not execution fences. Active mapping
     * blocks bind/inference/release of the mapped buffer. Mapping may fail. */
    lrt_status (LRT_CALL *begin_cpu_access)(
        lrt_buffer buffer, lrt_access access, lrt_cpu_mapping *out_mapping,
        lrt_cpu_mapping_info *out_info);
    lrt_status (LRT_CALL *end_cpu_access)(lrt_cpu_mapping mapping);
} lrt_linux_dma_buf_api;

typedef struct lrt_api {
    /* Context-free query of the same immutable global contract version.
     * Verify equality with LRT_RUNTIME_VERSION before calling other entries. */
    uint32_t (LRT_CALL *get_runtime_version)(void);

    /* Creates a runtime-owned environment, potentially spanning CPUs and
     * accelerators. Runtime/package owns hardware requirements and placement.
     * Validate availability here, during load_model or create_session as requirements
     * become known; unsupported hardware fails explicitly. Context outlives
     * all its models, sessions, buffers and completions. */
    lrt_status (LRT_CALL *create_context)(lrt_context *out_context);
    lrt_status (LRT_CALL *get_capabilities)(lrt_context, lrt_capabilities *out);
    /** @brief Load and validate a path-backed or memory-backed model/package.
     * Source pointers are borrowed only for this call; no ownership transfer.
     * Packaging and hardware compatibility are backend-owned. Invalid source
     * tag/null required data/zero byte length -> INVALID_ARGUMENT. Unsupported
     * format/resource mechanism -> UNSUPPORTED; incompatible model ->
     * INCOMPATIBLE. Failure retains no source references and leaves out_model
     * untouched. @see lrt_model_source
     */
    lrt_status (LRT_CALL *load_model)(lrt_context, const lrt_model_source *,
                                     lrt_model *out_model);
    /* Pointer graph is valid until unload_model. */
    lrt_status (LRT_CALL *describe_model)(lrt_model, lrt_model_info *out);

    /** @brief Create independent execution state using model shapes by default.
     * options=NULL is equivalent to zero-initialized options: ordinary static
     * networks need no repeated input count/shape array. Resolve every dynamic
     * input through an override. Invalid index/duplicate index/rank/constraint
     * mismatch/unresolved shape -> INVALID_ARGUMENT. A valid shape/policy the
     * backend cannot execute -> UNSUPPORTED. Failure leaves out_session untouched.
     *
     * Establishes concrete requirements and initializes session-local state;
     * specializes/compiles only when needed and permitted. PRECOMPILED_ONLY
     * rejects required compilation. Backend scratch setup is allowed; final
     * configuration-dependent IO staging setup happens during
     * binding/validate_bindings. Warmup is an optional backend optimization
     * using private storage and must leave the declared initial network state
     * intact; it must not access application IO or require a warmup invocation.
     *
     * One in-flight execution per session. Models may share weights/compiled
     * state across sessions, but not mutable network state or bindings. Backends
     * may reject additional sessions. Shapes are immutable for this session;
     * another shape requires another session. Data-dependent outputs require a
     * capacity bound established here; absent a bound return UNSUPPORTED. No
     * silent compilation or IO reallocation during inference.
     */
    lrt_status (LRT_CALL *create_session)(lrt_model, const lrt_session_options *options,
                                         lrt_session *out_session);
    /** @brief Query requirements independently for every input and output.
     * Returns tensor shape, capacity, alignment, required strides and ranked
     * memory options in one borrowed pointer graph, valid until session
     * destruction. Neither a common reference nor equal row numbers imply
     * that a whole binding combination is valid.
     * @see lrt_category_paths
     */
    lrt_status (LRT_CALL *get_io_requirements)(lrt_session,
                                              lrt_io_requirements *out);
    /** @brief Retain an import of external storage without taking its allocator.
     * Import once per stable allocation, then bind views/slots repeatedly.
     * For changing CPU tensor pointers, prefer bind_host_tensor instead of
     * importing and releasing a resource handle every frame.
     * Imported/allocated buffers belong to this context; bind checks actual
     * model compatibility. Cross-context use requires a supported import into
     * the destination context, not directly binding another context's handle. */
    lrt_status (LRT_CALL *import_buffer)(lrt_context, const lrt_memory_desc *,
                                        lrt_buffer *out_buffer);
    lrt_status (LRT_CALL *allocate_buffer)(lrt_context,
                                          const lrt_allocation_desc *,
                                          lrt_buffer *out_buffer);
    lrt_status (LRT_CALL *get_buffer_info)(lrt_buffer, lrt_buffer_info *out);

    /** @brief Associate one tensor with a buffer view, independently of other IO.
     * Session must be idle. Buffer/session context mismatch -> INCOMPATIBLE.
     * Checks resource compatibility, shape, stride, alignment, bounds, access
     * and overlap; IO aliasing is unsupported. Transfer is validated for this
     * tensor/resource, not merely at buffer import. DIRECT prohibits IO staging,
     * not internal network
     * transfers, scratch, weight packing or partitioning.
     * All four configuration fields must match an advertised option exactly.
     * Owner/reference must match buffer storage; access must satisfy the promised
     * minimum. Runtime either accepts that transfer mode or rejects the binding:
     * UNSUPPORTED for an unadvertised configuration, INCOMPATIBLE for a resource
     * that cannot meet the selected configuration. No alternative is selected.
     * POINTER import/allocation must be permitted by pointer_binding_flags.
     * NULL binding unbinds that input or output without selecting any replacement.
     * Failure preserves prior binding and its validation state.
     * Supplied descriptors are copied; buffer is retained while bound.
     * Successful binding replaces either binding path at this tensor, releasing
     * the previous handle association or borrowed host-pointer association.
     * Any successful binding change invalidates prior output views/results and
     * complete-set validation, even if it repeats the same arguments.
     * Binding sets up storage but does not read inputs or write outputs; any
     * required staging transfers happen as part of submitted execution. */
    lrt_status (LRT_CALL *bind_tensor)(lrt_session, lrt_io, uint32_t index,
                                      const lrt_tensor_binding *binding);

    /** @brief Borrow a CPU-addressable tensor without creating an import handle.
     * Requires HOST_BIND for non-NULL binding; otherwise UNSUPPORTED. Same
     * idle-session, shape/stride, capacity, alignment, overlap, exact selection
     * and failure-atomic replacement rules as bind_tensor. Reject overlap with
     * resource bindings too when safe non-aliasing cannot be established.
     * Binding may have validation/registration/setup overhead. Select an
     * advertised EXTERNAL / includes HOST / POINTER configuration with an exact
     * transfer mode permitting
     * BORROWED_BIND. Pointer size/alignment must satisfy the tensor and option
     * constraints; strides must match the tensor's single required layout.
     *
     * Caller keeps storage alive while bound. Input contents may be refilled
     * after previous execution completes, provided no other consumer uses them.
     * Outputs are readable after successful wait. Rebind when the pointer
     * changes. Timeout never permits early unmapping or releasing the owner.
     * NULL binding unbinds input/output, as bind_tensor does, even without
     * HOST_BIND capability. It never allocates or selects a replacement output.
     *
     * Fixed CPU tensor: discover -> bind -> validate_bindings once, then
     * [fill -> infer_async -> wait] repeatedly while the binding set is unchanged.
     * Transient host tensor: [bind -> validate_bindings -> infer_async -> wait -> unbind],
     * then unmap/release the producer's frame. No lrt_buffer release is needed.
     * Imported pool: import once -> [bind slot/view -> validate_bindings -> infer_async -> wait],
     * then unbind and release imports at teardown. Each successful rebind
     * replaces the old association; a failed rebind preserves it.
     */
    lrt_status (LRT_CALL *bind_host_tensor)(lrt_session, lrt_io, uint32_t index,
                                           const lrt_host_tensor_binding *binding);

    /** @brief Validate and finish setting up the complete current IO binding set.
     * Idle session only. Checks every tensor is bound, cross-tensor configuration
     * compatibility, aliasing and any shared resource limits.
     * May allocate/reserve staging required by explicitly selected STAGED options
     * and prepare backend binding snapshots,
     * but never compiles a new model, executes inference or reads/writes IO.
     * Missing binding -> STATE_INVALID; incompatible combination -> INCOMPATIBLE;
     * setup failure returns the relevant error. Binding associations are retained
     * on failure; the set remains invalid and inference accepts no work. Caller
     * changes its choices and retries. This is not an atomic batch bind/rollback.
     * Success preserves the caller's exact choices: no substitution or setup at inference.
     * Repeated validation of an unchanged valid set is a no-op. Filling existing
     * inputs does not invalidate it. Every successful bind/unbind does.
     */
    lrt_status (LRT_CALL *validate_bindings)(lrt_session);

    /** @brief Run inference synchronously and return a terminal completion.
     * All IO must be explicitly bound and successfully
     * validated since the last binding change; otherwise STATE_INVALID with no
     * work accepted. Both inference modes and the fence extension obey this rule.
     * Caller completes external producers/prior consumers before calling core
     * infer/infer_async. There is no runtime-completion dependency array. Optional
     * producer fences are handled only by the extension's asynchronous entry.
     * Neither a completion nor a fence transfers allocation/pool-slot ownership.
     *
     * Requires SYNC_INFERENCE; unimplemented mode -> UNSUPPORTED with no work.
     * Blocks until the runtime no longer accesses invocation IO. On accepted
     * work, returns OK and a terminal completion even for execution failure;
     * completion.result carries execution success/failure. This preserves one
     * result/lifetime interface for both modes. No wait call is required: call
     * get_outputs directly (it returns the execution error if inference failed),
     * or inspect get_completion_info. Successful execution establishes the same
     * output visibility as successful wait. Release the completion normally.
     * A non-OK return means no accepted work, no retained invocation resources,
     * and untouched out_completion. Unsupported mode never changes bindings,
     * outputs or state. If accepted work fails, state/output failure rules apply.
     * Caller may not mutate/recycle inputs until completion, nor overwrite
     * outputs while downstream consumers still use them. New acceptance
     * invalidates previous session output views. Applies to both host-pointer
     * and resource-handle bindings. */
    lrt_status (LRT_CALL *infer)(lrt_session, lrt_completion *out_completion);

    /** @brief Start inference without waiting for inference to finish.
     * Requires ASYNC_INFERENCE; otherwise UNSUPPORTED without accepted work.
     * Same validation/acceptance/failure/lifetime rules as infer, but the returned
     * completion may be pending. Does not run a blocking synchronous fallback
     * or wait for producer fences/inference completion on the calling thread.
     * Ordinary validation/enqueue overhead remains; no real-time latency bound
     * is promised. Completion may already be terminal if work finishes quickly.
     * OK means accepted, not output readiness or execution success. Use wait,
     * get_completion_info and get_outputs, then release_completion. Neither
     * execution mode is mandatory individually; unsupported entries stay nonnull.
     */
    lrt_status (LRT_CALL *infer_async)(lrt_session, lrt_completion *out_completion);
    /** @brief Wait for production completion, not downstream consumer release.
     * 0 = poll; FOREVER = no deadline. Timeout never cancels/releases work.
     * Terminal result is returned; OK also makes bound HOST outputs visible.
     * Completion covers CPU work, accelerator work and required output transfers.
     * On successful wait, all invocation inputs are no longer accessed and all
     * outputs are produced with the promised visibility. This does not map
     * device memory, replace DMA-BUF CPU-access/cache bracketing, or wait for
     * downstream consumers. infer_async returning OK is acceptance, not readiness.
     * Terminal status guarantees no further access to this invocation's IO,
     * including on failure/device loss. Until safe quiescence, remain pending. */
    lrt_status (LRT_CALL *wait)(lrt_completion, uint64_t timeout_ns);
    lrt_status (LRT_CALL *get_completion_info)(lrt_completion,
                                               lrt_completion_info *out);
    /** @brief Borrow result metadata; does not acquire an H2 output lease.
     * Only latest successful execution of this session; otherwise BUSY,
     * INVALID_ARGUMENT (wrong/stale completion), or the execution error.
     * Metadata and runtime-owned buffers borrowed until next accepted inference,
     * binding change or session destruction. After a binding change, an old
     * completion is stale for get_outputs until a new execution succeeds.
     * Do not release borrowed output buffer handles.
     * Caller-owned outputs retain normal ownership. Direct host outputs use
     * lrt_output.host_data, not a buffer handle. That borrow ends on unbinding,
     * replacement or session destruction. No truncation on overflow. */
    lrt_status (LRT_CALL *get_outputs)(lrt_session, lrt_completion,
                                      lrt_outputs *out);
    /* Idle only. Failed accepted work invalidates state until reset/recreate.
     * Stateless models accept this as a no-op. Never silently retry inference. */
    lrt_status (LRT_CALL *reset_state)(lrt_session);

    /* Destruction never implicitly waits. BUSY preserves the object:
     * completion: pending; buffer: bound/in flight/mapped; session: executing
     * or has unreleased completions; model: has sessions; context: has children.
     * Destroying an idle session removes its bindings and runtime outputs. */
    lrt_status (LRT_CALL *release_completion)(lrt_completion);
    lrt_status (LRT_CALL *release_buffer)(lrt_buffer);
    lrt_status (LRT_CALL *destroy_session)(lrt_session);
    lrt_status (LRT_CALL *unload_model)(lrt_model);
    lrt_status (LRT_CALL *destroy_context)(lrt_context);

    /* Returns a borrowed extension table, valid until context destruction.
     * Its layout is covered by the already-checked global contract version.
     * Unknown ID -> UNSUPPORTED. Check extension capability bits before using
     * optional functions; unsupported entries are NULL. Caller passes a local
     * const void * variable by address, then casts that returned pointer to
     * the table type associated with extension_id. */
    lrt_status (LRT_CALL *get_extension)(lrt_context, const char *extension_id,
                                        const void **out_table);
    /* Diagnostic for last failed call on this thread (including bootstrap).
     * Borrowed until next API call on this thread; never a global shared error.
     * Status codes, not text, drive program logic. */
    const char *(LRT_CALL *last_error_message)(void);
} lrt_api;

/* Stable bootstrap signature. Check requested_version for exact equality with
 * the library's lrt_runtime_version BEFORE writing out_api or interpreting its
 * layout. Mismatch -> LRT_INCOMPATIBLE, leaving out_api untouched. On success,
 * populate the complete table for that exact version; all core entries are
 * mandatory/non-NULL. Consumer then verifies api.get_runtime_version() equals
 * LRT_RUNTIME_VERSION before using any other entry. No context is required.
 *
 * The version covers extensions too: changing any extension layout requires a
 * global version change. This is a contract version, not a library release
 * number or proof of a platform ABI match. Both sides still require compatible
 * architecture, calling convention and compiler layout settings.
 *
 * Library must stay loaded until every object is destroyed. The global version
 * variable may also be checked before bootstrap, without reading any structs.
 *
 * Threading: serialize calls mutating the same context/model/session/buffer.
 * Concurrent waits/queries of a completion are allowed; do not release it
 * during any such call. Different sessions may execute independently. No
 * object destruction may race a call using it or its descendants.
 */
LRT_EXPORT lrt_status LRT_CALL lrt_get_api(uint32_t requested_version,
                                         lrt_api *out_api);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LOOM_RUNTIME_H */
