#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace voxmask {

constexpr uint32_t producer_protocol_abi = 1;
constexpr uint32_t producer_magic = 0x564d5052; // VMPR
constexpr uint32_t producer_max_frames = 960;
constexpr uint32_t producer_max_channels = 1;

enum class ProducerReject : uint32_t {
    none,
    bad_magic,
    bad_abi,
    bad_token,
    bad_epoch,
    bad_format,
    bad_frames,
    bad_sequence,
    future_timestamp,
    stale_timestamp,
    non_finite,
    duplicate,
};

// This is the fixed control handshake. It is a wire description, not a C++ ABI:
// serialize each integer in little-endian order and reject size/version mismatch.
struct ProducerHello {
    uint32_t magic = producer_magic;
    uint32_t abi = producer_protocol_abi;
    uint32_t sample_rate = 48000;
    uint32_t channels = 1;
    uint64_t epoch = 0;
    std::array<uint8_t, 32> capability_token{};
};

struct ProducerBlock {
    uint32_t magic = producer_magic;
    uint32_t abi = producer_protocol_abi;
    uint32_t frames = 0;
    uint32_t sample_rate = 48000;
    uint64_t epoch = 0;
    uint64_t sequence = 0;
    uint64_t capture_time_ns = 0;
    std::array<float, producer_max_frames> samples{};
};

struct ProducerResult {
    bool accepted = false;
    ProducerReject reject = ProducerReject::none;
    uint64_t epoch = 0;
    uint64_t sequence = 0;
};

// Validates producer messages after transport framing has checked the message
// length. The validator stores no PCM and is safe to poll from the control thread;
// one consumer should call accept_block at a time.
class ProducerValidator final {
public:
    bool establish(const ProducerHello& hello,
                   const std::array<uint8_t, 32>& expected_token) noexcept {
        if (hello.magic != producer_magic) {
            last_reject = ProducerReject::bad_magic;
            return false;
        }
        if (hello.abi != producer_protocol_abi) {
            last_reject = ProducerReject::bad_abi;
            return false;
        }
        if (hello.sample_rate != 44100 && hello.sample_rate != 48000 && hello.sample_rate != 96000) {
            last_reject = ProducerReject::bad_format;
            return false;
        }
        if (hello.channels != 1 || hello.epoch == 0 ||
            !constant_time_equal(hello.capability_token, expected_token)) {
            last_reject = hello.epoch == 0 ? ProducerReject::bad_epoch : ProducerReject::bad_token;
            return false;
        }
        if (active) {
            // A producer can only replace itself through a new validator/session.
            last_reject = ProducerReject::bad_epoch;
            return false;
        }
        active = true;
        epoch = hello.epoch;
        rate = hello.sample_rate;
        next_sequence = 0;
        last_reject = ProducerReject::none;
        return true;
    }

    ProducerResult accept_block(const ProducerBlock& block, uint64_t now_ns,
                                uint64_t max_age_ns = 250000000) noexcept {
        ProducerResult result{false, ProducerReject::none, epoch, block.sequence};
        if (!active) return reject(result, ProducerReject::bad_epoch);
        if (block.magic != producer_magic) return reject(result, ProducerReject::bad_magic);
        if (block.abi != producer_protocol_abi) return reject(result, ProducerReject::bad_abi);
        if (block.epoch != epoch) return reject(result, ProducerReject::bad_epoch);
        if (block.sample_rate != rate || block.frames == 0 || block.frames > producer_max_frames)
            return reject(result, block.sample_rate != rate ? ProducerReject::bad_format : ProducerReject::bad_frames);
        if (block.sequence != next_sequence) return reject(result, block.sequence < next_sequence ? ProducerReject::duplicate : ProducerReject::bad_sequence);
        if (block.capture_time_ns > now_ns) return reject(result, ProducerReject::future_timestamp);
        if (now_ns - block.capture_time_ns > max_age_ns) return reject(result, ProducerReject::stale_timestamp);
        for (uint32_t i = 0; i < block.frames; ++i)
            if (!std::isfinite(block.samples[i])) return reject(result, ProducerReject::non_finite);
        next_sequence++;
        last_reject = ProducerReject::none;
        result.accepted = true;
        return result;
    }

    void close(uint64_t closing_epoch) noexcept {
        if (active && closing_epoch == epoch) active = false;
    }
    bool is_active() const noexcept { return active; }
    uint64_t current_epoch() const noexcept { return epoch; }
    uint32_t sample_rate() const noexcept { return rate; }
    ProducerReject last_error() const noexcept { return last_reject; }

private:
    static bool constant_time_equal(const std::array<uint8_t, 32>& a,
                                    const std::array<uint8_t, 32>& b) noexcept {
        uint8_t difference = 0;
        for (size_t i = 0; i < a.size(); ++i) difference |= static_cast<uint8_t>(a[i] ^ b[i]);
        return difference == 0;
    }
    ProducerResult reject(ProducerResult result, ProducerReject reason) noexcept {
        result.reject = reason;
        last_reject = reason;
        return result;
    }
    bool active = false;
    uint64_t epoch = 0;
    uint64_t next_sequence = 0;
    uint32_t rate = 48000;
    ProducerReject last_reject = ProducerReject::none;
};

}
