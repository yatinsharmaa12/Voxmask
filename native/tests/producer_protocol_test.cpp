#include "../audio/producer_protocol.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <iostream>

using namespace voxmask;

int main() {
    std::array<uint8_t, 32> token{};
    for (uint8_t i = 0; i < token.size(); ++i) token[i] = static_cast<uint8_t>(i * 3 + 1);
    ProducerHello hello;
    hello.epoch = 77;
    hello.capability_token = token;
    ProducerValidator validator;

    auto bad_token = hello;
    bad_token.capability_token[0] ^= 1;
    assert(!validator.establish(bad_token, token));
    assert(validator.last_error() == ProducerReject::bad_token);
    assert(validator.establish(hello, token));
    assert(validator.is_active() && validator.current_epoch() == 77);
    assert(!validator.establish(hello, token));

    ProducerBlock block;
    block.epoch = 77;
    block.sequence = 0;
    block.frames = 480;
    block.capture_time_ns = 1'000'000'000;
    for (uint32_t i = 0; i < block.frames; ++i) block.samples[i] = 0.25f;
    auto accepted = validator.accept_block(block, 1'010'000'000);
    assert(accepted.accepted && accepted.reject == ProducerReject::none);

    auto duplicate = validator.accept_block(block, 1'010'000'000);
    assert(!duplicate.accepted && duplicate.reject == ProducerReject::duplicate);
    block.sequence = 2;
    assert(validator.accept_block(block, 1'010'000'000).reject == ProducerReject::bad_sequence);
    block.sequence = 1;
    block.epoch = 78;
    assert(validator.accept_block(block, 1'010'000'000).reject == ProducerReject::bad_epoch);
    block.epoch = 77;
    block.capture_time_ns = 2'000'000'000;
    assert(validator.accept_block(block, 1'010'000'000).reject == ProducerReject::future_timestamp);
    block.capture_time_ns = 1;
    assert(validator.accept_block(block, 1'010'000'000).reject == ProducerReject::stale_timestamp);
    block.capture_time_ns = 1'000'000'000;
    block.samples[0] = std::numeric_limits<float>::quiet_NaN();
    assert(validator.accept_block(block, 1'010'000'000).reject == ProducerReject::non_finite);
    block.samples[0] = .25f;
    block.frames = producer_max_frames + 1;
    assert(validator.accept_block(block, 1'010'000'000).reject == ProducerReject::bad_frames);
    block.frames = 480;
    block.sample_rate = 44100;
    assert(validator.accept_block(block, 1'010'000'000).reject == ProducerReject::bad_format);
    block.sample_rate = 48000;
    block.sequence = 1;
    assert(validator.accept_block(block, 1'010'000'000).accepted);
    validator.close(77);
    assert(!validator.is_active());
    assert(validator.accept_block(block, 1'010'000'000).reject == ProducerReject::bad_epoch);
    std::cout << "producer protocol: token, epoch, format, sequence, age, finite-data and close checks passed\n";
}
