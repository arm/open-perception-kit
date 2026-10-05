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

#include "op/Op.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

class DelayOp;

struct DelayOpHandle {
    std::atomic<DelayOp *> instance{nullptr};
    std::atomic<unsigned int> references{1};
};

void releaseHandle(DelayOpHandle *handle) {
    if (handle->references.fetch_sub(1) == 1)
        delete handle;
}

// A 30 FPS frame has a roughly 33 ms budget. Sleeping for 50 ms makes one following
// frame become stale, exercising opkinfer's proactive skip policy without loading a model.
class DelayOp final : public opk::op::Op,
                      public opk::op::OpInterfacePostprocessor,
                      public opk::op::OpInterfaceContentConsumer {
  public:
    ~DelayOp() override {
        if (handle_ != nullptr) {
            auto *instance = this;
            handle_->instance.compare_exchange_strong(instance, nullptr);
            releaseHandle(handle_);
        }
    }

    opk::Result<void> configure(const opk::AttributeMap &attributes) override {
        providedContentType_ = attributes.getStringOrDefault("provided-content-type", "");
        requiredContentType_ = attributes.getStringOrDefault("required-content-type", "");
        if (attributes.contains("control-handle")) {
            handle_ = reinterpret_cast<DelayOpHandle *>(
                static_cast<std::uintptr_t>(attributes.getInt("control-handle")));
            handle_->references.fetch_add(1);
            handle_->instance.store(this);
        }
        return {};
    }

    opk::Result<void> bind(size_t, const std::vector<opk::op::Op *> &) override {
        return {};
    }

    opk::Result<opk::op::OpSignal> process(opk::op::OpChainContext &) override {
        {
            std::unique_lock lock(gateMutex_);
            if (blockNextProcess_) {
                blockNextProcess_ = false;
                processingBlocked_ = true;
                gateCondition_.notify_all();
                gateCondition_.wait(lock, [this] { return releaseProcessing_; });
                processingBlocked_ = false;
                releaseProcessing_ = false;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMilliseconds_.load()));
        return opk::op::OpSignal::Continue;
    }

    std::vector<std::string_view> getProvidedContentTypes() const override {
        return providedContentType_.empty() ? std::vector<std::string_view>{}
                                            : std::vector<std::string_view>{providedContentType_};
    }

    std::vector<std::string_view> getRequiredContentTypes() const override {
        return requiredContentType_.empty() ? std::vector<std::string_view>{}
                                            : std::vector<std::string_view>{requiredContentType_};
    }

    void setDelay(std::uint64_t milliseconds) {
        delayMilliseconds_.store(milliseconds);
    }

    void blockNextProcess() {
        std::lock_guard lock(gateMutex_);
        blockNextProcess_ = true;
        processingBlocked_ = false;
        releaseProcessing_ = false;
    }

    bool waitUntilBlocked(std::uint64_t timeoutMilliseconds) {
        std::unique_lock lock(gateMutex_);
        return gateCondition_.wait_for(lock,
                                       std::chrono::milliseconds(timeoutMilliseconds),
                                       [this] { return processingBlocked_; });
    }

    void releaseProcess() {
        std::lock_guard lock(gateMutex_);
        releaseProcessing_ = true;
        gateCondition_.notify_all();
    }

  private:
    std::string providedContentType_;
    std::string requiredContentType_;
    DelayOpHandle *handle_ = nullptr;
    std::atomic<std::uint64_t> delayMilliseconds_{50};
    std::mutex gateMutex_;
    std::condition_variable gateCondition_;
    bool blockNextProcess_ = false;
    bool processingBlocked_ = false;
    bool releaseProcessing_ = false;
};

DelayOp *getInstance(void *opaqueHandle) {
    if (opaqueHandle == nullptr)
        return nullptr;
    return static_cast<DelayOpHandle *>(opaqueHandle)->instance.load();
}

} // namespace

extern "C" void opk_delete_op_instance(void *instance) {
    std::unique_ptr<opk::op::Op> owner( // NOSONAR: adopt the instance returned by the plugin ABI.
        static_cast<opk::op::Op *>(instance));
}

extern "C" void *opk_create_op_instance(const char *opName) {
    if (opName == nullptr || std::strcmp(opName, "Delay") != 0)
        return nullptr;
    return std::make_unique<DelayOp>().release();
}

// Test-only controls used by qos_pipeline_test.py to make lifecycle races deterministic.
extern "C" void *opk_test_qos_delay_create_handle() {
    return new DelayOpHandle;
}

extern "C" void opk_test_qos_delay_destroy_handle(void *handle) {
    releaseHandle(static_cast<DelayOpHandle *>(handle));
}

extern "C" bool opk_test_qos_delay_set_milliseconds(void *handle, std::uint64_t milliseconds) {
    auto *instance = getInstance(handle);
    if (instance == nullptr)
        return false;
    instance->setDelay(milliseconds);
    return true;
}

extern "C" bool opk_test_qos_delay_block_next_process(void *handle) {
    auto *instance = getInstance(handle);
    if (instance == nullptr)
        return false;
    instance->blockNextProcess();
    return true;
}

extern "C" bool opk_test_qos_delay_wait_until_blocked(void *handle,
                                                      std::uint64_t timeoutMilliseconds) {
    auto *instance = getInstance(handle);
    return instance != nullptr && instance->waitUntilBlocked(timeoutMilliseconds);
}

extern "C" bool opk_test_qos_delay_release_process(void *handle) {
    auto *instance = getInstance(handle);
    if (instance == nullptr)
        return false;
    instance->releaseProcess();
    return true;
}
