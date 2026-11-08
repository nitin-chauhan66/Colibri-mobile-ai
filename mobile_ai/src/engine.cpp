#include "colibri_mobile/engine.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

namespace colibri::mobile {
namespace {

void logMessage(const Logger& logger, const std::string& message) {
    if (logger) {
        logger(message);
    } else {
        std::fprintf(stderr, "[colibri-mobile] %s\n", message.c_str());
    }
}

std::size_t processRssBytes() noexcept {
#if defined(__linux__)
    std::ifstream smaps("/proc/self/smaps_rollup");
    std::string key;
    std::size_t value_kb = 0;
    while (smaps >> key >> value_kb) {
        if (key == "Rss:") {
            return value_kb * 1024;
        }
        std::string unit;
        smaps >> unit;
    }
#endif
    struct rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(__APPLE__)
        return static_cast<std::size_t>(usage.ru_maxrss);
#else
        return static_cast<std::size_t>(usage.ru_maxrss) * 1024;
#endif
    }
    return 0;
}

std::string errorMessage(const char* operation, const std::string& path) {
    return std::string(operation) + " '" + path + "': " + std::strerror(errno);
}

}  // namespace

MemoryManager::MemoryManager(std::size_t cap_bytes, Logger logger)
    : cap_bytes_(cap_bytes), logger_(std::move(logger)) {
    if (cap_bytes == 0) {
        throw std::invalid_argument("memory cap must be non-zero");
    }
}

void MemoryManager::registerBaseResident(std::size_t bytes) {
    std::lock_guard lock(mutex_);
    if (bytes > cap_bytes_ || bytes + processRssBytes() > cap_bytes_) {
        throw std::runtime_error("base model exceeds the configured memory cap");
    }
    base_bytes_ = bytes;
    logMessage(logger_, "base resident bytes=" + std::to_string(bytes));
}

bool MemoryManager::tryReserveDynamic(std::size_t bytes) {
    std::size_t current = dynamic_reserved_bytes_.load(std::memory_order_relaxed);
    for (;;) {
        const std::size_t rss = processRssBytes();
        if (base_bytes_ > cap_bytes_ || bytes > cap_bytes_ - base_bytes_ ||
            current > cap_bytes_ - base_bytes_ - bytes ||
            rss > cap_bytes_ - bytes) {
            logMessage(logger_, "dynamic mapping rejected: memory cap reached");
            return false;
        }
        if (dynamic_reserved_bytes_.compare_exchange_weak(
                current, current + bytes, std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            return true;
        }
    }
}

void MemoryManager::releaseDynamic(std::size_t bytes) noexcept {
    dynamic_reserved_bytes_.fetch_sub(bytes, std::memory_order_acq_rel);
}

MemorySnapshot MemoryManager::snapshot() const noexcept {
    return {cap_bytes_, base_bytes_,
            dynamic_reserved_bytes_.load(std::memory_order_acquire),
            processRssBytes()};
}

MappedExpert::MappedExpert(int fd, void* mapping, std::size_t mapping_length,
                           const Byte* data, std::size_t length,
                           MemoryManager& memory, Logger logger, std::string id)
    : fd_(fd), mapping_(mapping), mapping_length_(mapping_length), data_(data),
      length_(length), memory_(&memory), logger_(std::move(logger)),
      id_(std::move(id)) {}

MappedExpert::~MappedExpert() { reset(); }

MappedExpert::MappedExpert(MappedExpert&& other) noexcept
    : fd_(other.fd_), mapping_(other.mapping_),
      mapping_length_(other.mapping_length_), data_(other.data_),
      length_(other.length_), memory_(other.memory_),
      logger_(std::move(other.logger_)), id_(std::move(other.id_)) {
    other.fd_ = -1;
    other.mapping_ = nullptr;
    other.mapping_length_ = 0;
    other.data_ = nullptr;
    other.length_ = 0;
    other.memory_ = nullptr;
}

MappedExpert& MappedExpert::operator=(MappedExpert&& other) noexcept {
    if (this != &other) {
        reset();
        fd_ = other.fd_;
        mapping_ = other.mapping_;
        mapping_length_ = other.mapping_length_;
        data_ = other.data_;
        length_ = other.length_;
        memory_ = other.memory_;
        logger_ = std::move(other.logger_);
        id_ = std::move(other.id_);
        other.fd_ = -1;
        other.mapping_ = nullptr;
        other.mapping_length_ = 0;
        other.data_ = nullptr;
        other.length_ = 0;
        other.memory_ = nullptr;
    }
    return *this;
}

std::span<const Byte> MappedExpert::bytes() const noexcept {
    return {data_, length_};
}

void MappedExpert::reset() noexcept {
    if (mapping_ != nullptr) {
        munmap(mapping_, mapping_length_);
        mapping_ = nullptr;
    }
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    if (memory_ != nullptr) {
        memory_->releaseDynamic(mapping_length_);
        memory_ = nullptr;
    }
}

TensorRouter::TensorRouter(MemoryManager& memory, std::vector<ExpertSpec> specs,
                           std::size_t ring_slots, Logger logger)
    : memory_(memory), specs_(std::move(specs)),
      ring_(ring_slots == 0 ? 1 : ring_slots), logger_(std::move(logger)) {
    for (const auto& spec : specs_) {
        if (spec.id.empty() || spec.path.empty() || spec.length == 0) {
            throw std::invalid_argument("expert specs require id, path, length");
        }
    }
    prefetch_thread_ = std::thread(&TensorRouter::prefetchLoop, this);
}

TensorRouter::~TensorRouter() {
    {
        std::lock_guard lock(queue_mutex_);
        stopping_ = true;
    }
    queue_cv_.notify_all();
    if (prefetch_thread_.joinable()) {
        prefetch_thread_.join();
    }
    std::lock_guard lock(ring_mutex_);
    ring_.clear();
}

const ExpertSpec& TensorRouter::findSpec(const std::string& id) const {
    for (const auto& spec : specs_) {
        if (spec.id == id) {
            return spec;
        }
    }
    throw std::out_of_range("unknown expert: " + id);
}

std::shared_ptr<MappedExpert> TensorRouter::findLoaded(
    const std::string& id) const {
    std::lock_guard lock(ring_mutex_);
    for (const auto& entry : ring_) {
        if (entry && entry->id() == id) {
            return entry;
        }
    }
    return {};
}

std::shared_ptr<MappedExpert> TensorRouter::route(const std::string& id) {
    if (auto loaded = findLoaded(id)) {
        return loaded;
    }
    const ExpertSpec& spec = findSpec(id);
    const auto started = std::chrono::steady_clock::now();
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        throw std::runtime_error("unable to determine page size");
    }
    const std::uint64_t page = static_cast<std::uint64_t>(page_size);
    const std::uint64_t aligned_offset = spec.file_offset / page * page;
    const std::size_t delta =
        static_cast<std::size_t>(spec.file_offset - aligned_offset);
    if (delta > static_cast<std::size_t>(-1) - spec.length) {
        throw std::overflow_error("expert mapping length overflow");
    }
    const std::size_t mapping_length = delta + spec.length;
    if (!memory_.tryReserveDynamic(mapping_length)) {
        throw std::runtime_error("expert mapping would exceed memory cap");
    }

    int fd = open(spec.path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        memory_.releaseDynamic(mapping_length);
        throw std::runtime_error(errorMessage("open", spec.path));
    }
    struct stat file_stat {};
    if (fstat(fd, &file_stat) != 0 ||
        spec.file_offset > static_cast<std::uint64_t>(file_stat.st_size) ||
        spec.length > static_cast<std::uint64_t>(file_stat.st_size) -
                          spec.file_offset) {
        close(fd);
        memory_.releaseDynamic(mapping_length);
        throw std::runtime_error("expert range is outside file: " + spec.path);
    }
    void* mapping =
        mmap(nullptr, mapping_length, PROT_READ, MAP_PRIVATE, fd, aligned_offset);
    if (mapping == MAP_FAILED) {
        close(fd);
        memory_.releaseDynamic(mapping_length);
        throw std::runtime_error(errorMessage("mmap", spec.path));
    }
    auto* data = reinterpret_cast<const Byte*>(
        static_cast<const std::byte*>(mapping) + delta);
    madvise(mapping, mapping_length, MADV_WILLNEED);
    auto result = std::shared_ptr<MappedExpert>(new MappedExpert(
        fd, mapping, mapping_length, data, spec.length, memory_, logger_, id));

    {
        std::lock_guard lock(ring_mutex_);
        ring_[ring_next_] = result;
        ring_next_ = (ring_next_ + 1) % ring_.size();
    }
    const auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started);
    logMessage(logger_, "mapped expert=" + id +
                           " bytes=" + std::to_string(spec.length) +
                           " map_us=" + std::to_string(latency.count()));
    return result;
}

void TensorRouter::requestPrefetch(std::string id) {
    {
        std::lock_guard lock(queue_mutex_);
        if (stopping_) {
            return;
        }
        if (queue_.size() >= ring_.size() * 2) {
            queue_.erase(queue_.begin());
        }
        queue_.push_back(std::move(id));
    }
    queue_cv_.notify_one();
}

void TensorRouter::waitForPrefetchIdle() {
    std::unique_lock lock(queue_mutex_);
    idle_cv_.wait(lock, [this] {
        return queue_.empty() && active_prefetch_ == 0;
    });
}

void TensorRouter::prefetchLoop() {
    for (;;) {
        std::string id;
        {
            std::unique_lock lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) {
                return;
            }
            id = std::move(queue_.front());
            queue_.erase(queue_.begin());
            ++active_prefetch_;
        }
        try {
            (void)route(id);
            logMessage(logger_, "prefetched expert=" + id);
        } catch (const std::exception& error) {
            logMessage(logger_, "prefetch failed: " + std::string(error.what()));
        }
        {
            std::lock_guard lock(queue_mutex_);
            --active_prefetch_;
        }
        idle_cv_.notify_all();
    }
}

TokenMetrics DualThreadInference::run(
    std::span<const std::uint32_t> tokens, BaseStep base_step,
    RoutePredictor route_predictor) {
    if (!base_step || !route_predictor) {
        throw std::invalid_argument("inference callbacks must be provided");
    }
    const auto started = std::chrono::steady_clock::now();
    std::thread router_thread([&] {
        for (const std::uint32_t token : tokens) {
            router_.requestPrefetch(route_predictor(token));
        }
    });
    for (const std::uint32_t token : tokens) {
        base_step(token);
    }
    router_thread.join();
    router_.waitForPrefetchIdle();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const auto elapsed_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed);
    const double seconds = std::chrono::duration<double>(elapsed).count();
    const double rate = seconds > 0.0
                            ? static_cast<double>(tokens.size()) / seconds
                            : 0.0;
    return {tokens.size(), elapsed_ns, rate};
}

}  // namespace colibri::mobile
