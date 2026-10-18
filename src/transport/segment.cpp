// Segment protocol (M2.3). Ordering tags [G1]-[G3]: docs/memory-ordering.md section 14.
#include "fastbus/transport/segment.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

#include "fastbus/core/assert.hpp"
#include "fastbus/core/clock.hpp"
#include "fastbus/core/error.hpp"
#include "fastbus/core/result.hpp"
#include "fastbus/platform/process.hpp"
#include "fastbus/platform/shared_memory.hpp"
#include "fastbus/platform/time_source.hpp"

#include "segment_internal.hpp"

namespace fastbus::transport {
namespace {

constexpr std::int64_t kPollIntervalNs = 1'000'000;  // 1 ms while waiting for `ready`
constexpr std::size_t kHeaderSize = sizeof(SegmentHeader);

using segment_detail::creator_of;
using segment_detail::load_relaxed;
using segment_detail::remove_if_stale;
using segment_detail::store_relaxed;

}  // namespace

core::Result<Segment> Segment::create(std::string_view name, std::size_t payload_size,
                                      std::uint32_t kind) noexcept {
    if (payload_size == 0 || payload_size > std::numeric_limits<std::size_t>::max() - kHeaderSize) {
        return core::fail(core::ErrorCode::InvalidArgument, "segment payload size out of range");
    }
    const std::size_t total = kHeaderSize + payload_size;
    for (int attempt = 0; attempt < 2; ++attempt) {
        auto shm = platform::SharedMemory::create(name, total);
        if (!shm) {
            if (shm.error().code != core::ErrorCode::AlreadyExists || attempt == 1) {
                return core::Unexpected{shm.error()};
            }
            const auto stale = remove_if_stale(name);
            if (!stale && stale.error().code != core::ErrorCode::NotFound) {
                return core::Unexpected{stale.error()};
            }
            continue;  // removed (or vanished): try once more
        }
        auto* h = std::construct_at(reinterpret_cast<SegmentHeader*>(shm->data()));
        // Identity first: an opener waiting on `initializing` uses it to detect a dead creator.
        const platform::ProcessIdentity me = platform::current_process();
        store_relaxed(h->creator.pid, me.pid);
        store_relaxed(h->creator.start_time, me.start_time);
        store_relaxed(h->magic, SegmentHeader::kMagic);
        h->layout_version = SegmentHeader::kLayoutVersion;
        h->header_size = static_cast<std::uint16_t>(kHeaderSize);
        h->kind = kind;
        h->total_size = total;
        h->payload_offset = kHeaderSize;
        h->payload_size = payload_size;
        h->epoch = static_cast<std::uint64_t>(platform::wall_now_ns());
        const std::span<std::byte> payload = shm->bytes().subspan(kHeaderSize, payload_size);
        return Segment{std::move(shm).value(), h, payload};
    }
    return core::fail(core::ErrorCode::AlreadyExists, "segment name taken");  // not reached
}

core::Result<Segment> Segment::open(std::string_view name, core::Duration timeout) noexcept {
    auto shm = platform::SharedMemory::open(name);
    if (!shm) {
        return core::Unexpected{shm.error()};
    }
    if (shm->size() < kHeaderSize) {
        return core::fail(core::ErrorCode::Corrupted, "segment smaller than its header");
    }
    auto& h = *reinterpret_cast<SegmentHeader*>(shm->data());
    const core::Timestamp deadline = core::SystemMonotonicClock::read() + timeout;
    while (true) {
        const std::uint32_t state = h.state.load(std::memory_order_acquire);  // [G2]
        if (state == static_cast<std::uint32_t>(SegmentState::Ready)) {
            break;
        }
        if (state == static_cast<std::uint32_t>(SegmentState::Closing)) {
            return core::fail(core::ErrorCode::NotFound, "segment is closing");
        }
        if (state != static_cast<std::uint32_t>(SegmentState::Initializing)) {
            return core::fail(core::ErrorCode::Corrupted, "segment: invalid state value");
        }
        const platform::ProcessIdentity who = creator_of(h);
        if (load_relaxed(h.magic) == SegmentHeader::kMagic && who.pid != 0 &&
            !platform::is_alive(who)) {
            return core::fail(core::ErrorCode::NotFound, "segment creator died while initializing");
        }
        if (core::SystemMonotonicClock::read() >= deadline) {
            return core::fail(core::ErrorCode::Timeout, "segment did not become ready in time");
        }
        platform::sleep_for_ns(kPollIntervalNs);
    }
    if (const auto valid = validate_segment(shm->bytes()); !valid) {
        return core::Unexpected{valid.error()};
    }
    if (!platform::is_alive(h.creator)) {
        return core::fail(core::ErrorCode::NotFound, "segment creator is dead (stale segment)");
    }
    const std::span<std::byte> payload = shm->bytes().subspan(
        static_cast<std::size_t>(h.payload_offset), static_cast<std::size_t>(h.payload_size));
    return Segment{std::move(shm).value(), &h, payload};
}

Segment::Segment(platform::SharedMemory shm, SegmentHeader* header,
                 std::span<std::byte> payload) noexcept
    : shm_(std::move(shm)), header_(header), payload_(payload) {}

Segment::Segment(Segment&& other) noexcept
    : shm_(std::move(other.shm_)),
      header_(std::exchange(other.header_, nullptr)),
      payload_(std::exchange(other.payload_, {})) {}

Segment& Segment::operator=(Segment&& other) noexcept {
    if (this != &other) {
        mark_closing();
        shm_ = std::move(other.shm_);
        header_ = std::exchange(other.header_, nullptr);
        payload_ = std::exchange(other.payload_, {});
    }
    return *this;
}

Segment::~Segment() {
    mark_closing();
}

void Segment::mark_ready() noexcept {
    core::check(is_creator() && header_ != nullptr, "Segment::mark_ready() by a non-creator");
    header_->state.store(static_cast<std::uint32_t>(SegmentState::Ready),
                         std::memory_order_release);  // [G1]
}

void Segment::mark_closing() noexcept {
    if (header_ != nullptr && is_creator()) {
        header_->state.store(static_cast<std::uint32_t>(SegmentState::Closing),
                             std::memory_order_release);  // [G3]
    }
}

SegmentState Segment::state() const noexcept {
    return static_cast<SegmentState>(header_->state.load(std::memory_order_acquire));
}

}  // namespace fastbus::transport
