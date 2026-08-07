/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

/**
 * @file Common.h
 * @brief Backend-neutral media IO primitives shared by audio, video, and raw payloads.
 */

#pragma once

#include "pek/Types.h"

#include <cstddef>
#include <cstdint>

namespace pek::mediaio {

/**
 * @brief Presentation timestamp in nanoseconds.
 *
 * Values are backend-neutral presentation times. Backends should convert their
 * native timestamp representation to nanoseconds before exposing media data.
 */
using TimestampNs = std::uint64_t;

/**
 * @brief Sentinel nanosecond timestamp used when no presentation time is available.
 */
inline constexpr TimestampNs InvalidTimestampNs = static_cast<TimestampNs>(-1);

/**
 * @brief Lightweight DMA-BUF synchronization metadata.
 *
 * The file descriptors are borrowed. Ownership should stay with the backend
 * object that produced the media data unless a future integration explicitly
 * adopts or duplicates them.
 */
class DmaBufSync {
  public:
    /// Creates synchronization metadata without fences.
    DmaBufSync() = default;

    /**
     * @brief Creates synchronization metadata from borrowed fence descriptors.
     * @param acquireFenceFd Fence to wait on before reading or importing the buffer.
     * @param releaseFenceFd Fence signalled after downstream processing, if available.
     */
    DmaBufSync(int acquireFenceFd, int releaseFenceFd = -1) noexcept;

    /**
     * @brief Returns the borrowed acquire fence file descriptor.
     * @return File descriptor, or -1 when no acquire fence is present.
     */
    int acquireFenceFd() const noexcept;

    /**
     * @brief Returns the borrowed release fence file descriptor.
     * @return File descriptor, or -1 when no release fence is present.
     */
    int releaseFenceFd() const noexcept;

    /**
     * @brief Returns true when acquireFenceFd() contains a valid descriptor.
     */
    bool hasAcquireFence() const noexcept;

    /**
     * @brief Returns true when releaseFenceFd() contains a valid descriptor.
     */
    bool hasReleaseFence() const noexcept;

  private:
    /// Borrowed acquire fence file descriptor, or -1 when absent.
    int acquireFd = -1;
    /// Borrowed release fence file descriptor, or -1 when absent.
    int releaseFd = -1;
};

/**
 * @brief Generic non-owning view over host or DMA-BUF backed data.
 *
 * DataView is intentionally not video-specific. It describes a single readable
 * or writable data plane for image, audio, or raw payloads. For image/video
 * planes, format identifies the raw frame pixel layout.
 * The pointed-to memory or descriptor must remain valid for the owner object's
 * lifetime.
 */
class DataView {
  public:
    /// Creates an empty view with unknown memory, access, and raw image format.
    DataView() = default;

    /**
     * @brief Creates a view over CPU-addressable memory.
     * @param data Pointer to the first byte of the data plane.
     * @param byteSize Number of bytes available from data.
     * @param format Raw image/video pixel layout for image planes.
     * @param strideBytes Row stride in bytes for image data, or 0 when not applicable.
     * @param accessMode Permitted read/write access for the memory.
     * @param offsetBytes Byte offset of this plane inside the backing allocation.
     */
    static DataView host(void *data,
                         size_t byteSize,
                         pek::RawImagePixelFormat format,
                         uint32_t strideBytes = 0,
                         pek::AccessMode accessMode = pek::AccessMode::ReadWrite,
                         size_t offsetBytes = 0) noexcept;

    /**
     * @brief Creates a view over DMA-BUF backed memory.
     * @param fd Borrowed DMA-BUF file descriptor.
     * @param byteSize Number of bytes in this plane.
     * @param format Raw image/video pixel layout for image planes.
     * @param strideBytes Row stride in bytes for image data, or 0 when not applicable.
     * @param offsetBytes Byte offset of this plane inside the DMA-BUF allocation.
     * @param accessMode Permitted read/write access for the memory.
     * @param sync Borrowed synchronization fence metadata.
     */
    static DataView dmaBuf(int fd,
                           size_t byteSize,
                           pek::RawImagePixelFormat format,
                           uint32_t strideBytes = 0,
                           size_t offsetBytes = 0,
                           pek::AccessMode accessMode = pek::AccessMode::ReadWrite,
                           DmaBufSync sync = {}) noexcept;

    /** @brief Returns the backing memory type. */
    pek::MemoryType memoryType() const noexcept;

    /** @brief Returns the permitted access mode for this view. */
    pek::AccessMode accessMode() const noexcept;

    /** @brief Returns the raw image/video pixel layout for image planes. */
    pek::RawImagePixelFormat kind() const noexcept;

    /**
     * @brief Returns a read pointer for host-backed data.
     * @return Host pointer, or nullptr for descriptor-only views.
     */
    const void *data() const noexcept;

    /**
     * @brief Returns a writable host pointer when write access is allowed.
     * @return Host pointer, or nullptr for read-only and descriptor-only views.
     */
    void *mutableData() const noexcept;

    /**
     * @brief Returns the borrowed DMA-BUF file descriptor.
     * @return File descriptor, or -1 for non-DMA-BUF views.
     */
    int fd() const noexcept;

    /** @brief Returns the byte offset inside the backing allocation. */
    size_t offset() const noexcept;

    /** @brief Returns the byte size of this view. */
    size_t byteSize() const noexcept;

    /** @brief Returns row stride in bytes for image data, or 0 when unknown. */
    uint32_t strideBytes() const noexcept;

    /** @brief Returns borrowed DMA-BUF synchronization metadata. */
    const DmaBufSync &sync() const noexcept;

    /** @brief Returns true when this view has CPU-addressable host data. */
    bool hasHostData() const noexcept;

    /** @brief Returns true when this view has a DMA-BUF file descriptor. */
    bool hasDmaBuf() const noexcept;

    /** @brief Returns true when read access is permitted. */
    bool canRead() const noexcept;

    /** @brief Returns true when write access is permitted. */
    bool canWrite() const noexcept;

  private:
    /// Backing memory kind for this view.
    pek::MemoryType memory = pek::MemoryType::Unknown;
    /// Permitted access mode for this view.
    pek::AccessMode access = pek::AccessMode::Unknown;
    /// Raw image/video pixel layout for image planes.
    pek::RawImagePixelFormat rawImageFormat = pek::RawImagePixelFormat::Unknown;

    /// CPU-addressable pointer for host-backed views.
    void *hostData = nullptr;
    /// Borrowed DMA-BUF file descriptor, or -1 for non-DMA-BUF views.
    int dmaBufFd = -1;
    /// Byte offset of this view inside the backing allocation.
    size_t dataOffset = 0;
    /// Number of bytes exposed by this view.
    size_t dataByteSize = 0;
    /// Row stride in bytes for image data, or 0 when unknown/not applicable.
    uint32_t dataStrideBytes = 0;
    /// Optional borrowed DMA-BUF fence metadata.
    DmaBufSync dmaBufSync;
};

} // namespace pek::mediaio
