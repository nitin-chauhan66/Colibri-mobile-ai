#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace colibri::mobile {

using Byte = std::byte;
using Logger = std::function<void(const std::string&)>;

struct MemorySnapshot {
    std::size_t cap_bytes;
    std::size_t base_bytes;
    std::size_t dynamic_reserved_bytes;
    std::size_t process_rss_bytes;
};

class MemoryManager final {
public:
    explicit MemoryManager(std::size_t cap_bytes, Logger logger = {});

    // The base model is owned by the caller. This only accounts for its
    // resident footprint and keeps its lifetime pinned at the engine level.
    void registerBaseResident(std::size_t bytes);
    bool tryReserveDynamic(std::size_t bytes);
    void releaseDynamic(std::size_t bytes) noexcept;
    [[nodiscard]] MemorySnapshot snapshot() const noexcept;

private:
    std::size_t cap_bytes_;
    std::size_t base_bytes_{0};
    std::atomic<std::size_t> dynamic_reserved_bytes_{0};
    Logger logger_;
    mutable std::mutex mutex_;
};

struct ExpertSpec {
    std::string id;
    std::string path;
    std::uint64_t file_offset{0};
    std::size_t length{0};
};

class MappedExpert final {
public:
    MappedExpert(int fd, void* mapping, std::size_t mapping_length,
                 const Byte* data, std::size_t length, MemoryManager& memory,
                 Logger logger, std::string id);
    ~MappedExpert();

    MappedExpert(const MappedExpert&) = delete;
    MappedExpert& operator=(const MappedExpert&) = delete;
    MappedExpert(MappedExpert&& other) noexcept;
    MappedExpert& operator=(MappedExpert&& other) noexcept;

    [[nodiscard]] std::span<const Byte> bytes() const noexcept;
    [[nodiscard]] const std::string& id() const noexcept { return id_; }

private:
    void reset() noexcept;

    int fd_{-1};
    void* mapping_{nullptr};
    std::size_t mapping_length_{0};
    const Byte* data_{nullptr};
    std::size_t length_{0};
    MemoryManager* memory_{nullptr};
    Logger logger_;
    std::string id_;
};

class TensorRouter final {
public:
    TensorRouter(MemoryManager& memory, std::vector<ExpertSpec> specs,
                 std::size_t ring_slots = 2, Logger logger = {});
    ~TensorRouter();

    TensorRouter(const TensorRouter&) = delete;
    TensorRouter& operator=(const TensorRouter&) = delete;

    // Returns a shared mapping held by the recycling ring or creates one.
    // Weight bytes are never copied to heap memory.
    [[nodiscard]] std::shared_ptr<MappedExpert> route(const std::string& id);

    // Enqueues a route decision for the I/O worker. A full queue drops the
    // oldest prediction rather than growing unboundedly.
    void requestPrefetch(std::string id);
    void waitForPrefetchIdle();

private:
    void prefetchLoop();
    [[nodiscard]] std::shared_ptr<MappedExpert> findLoaded(
        const std::string& id) const;
    [[nodiscard]] const ExpertSpec& findSpec(const std::string& id) const;

    MemoryManager& memory_;
    std::vector<ExpertSpec> specs_;
    std::vector<std::shared_ptr<MappedExpert>> ring_;
    std::size_t ring_next_{0};
    Logger logger_;

    mutable std::mutex ring_mutex_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::condition_variable idle_cv_;
    std::vector<std::string> queue_;
    std::size_t active_prefetch_{0};
    bool stopping_{false};
    std::thread prefetch_thread_;
};

struct TokenMetrics {
    std::size_t tokens{0};
    std::chrono::nanoseconds elapsed{};
    double tokens_per_second{0.0};
};

// Runs base-layer work on the caller thread while routing the next expert on
// the worker thread. The callbacks must not retain pointers into a mapping
// after the callback returns.
class DualThreadInference final {
public:
    using BaseStep = std::function<void(std::uint32_t token)>;
    using RoutePredictor = std::function<std::string(std::uint32_t token)>;

    explicit DualThreadInference(TensorRouter& router) : router_(router) {}
    [[nodiscard]] TokenMetrics run(std::span<const std::uint32_t> tokens,
                                    BaseStep base_step,
                                    RoutePredictor route_predictor);

private:
    TensorRouter& router_;
};

}  // namespace colibri::mobile
