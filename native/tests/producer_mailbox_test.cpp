#include "../audio/producer_mailbox.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <limits>
#include <thread>

int main() {
    voxmask::ProducerMailbox mailbox;
    std::array<float, 32> out{};
    mailbox.read(100, out.size(), 1000, out.data());
    for (float sample : out) assert(sample == 0);

    voxmask::ProducerBlock block{};
    block.epoch = 12;
    block.frames = 16;
    block.capture_time_ns = 1000;
    block.samples.fill(.25f);
    mailbox.publish(block, 104, 1000);
    mailbox.read(100, out.size(), 1000, out.data());
    for (size_t i = 0; i < out.size(); ++i)
        assert(out[i] == (i >= 4 && i < 20 ? .25f : 0));

    mailbox.read(100, out.size(), 1000 + voxmask::ProducerMailbox::max_age_ns + 1, out.data());
    for (float sample : out) assert(sample == 0);
    block.samples[0] = std::numeric_limits<float>::quiet_NaN();
    mailbox.publish(block, 200, 2000);
    mailbox.read(200, out.size(), 2000, out.data());
    for (float sample : out) assert(sample == 0);
    block.samples[0] = .25f;

    // Concurrent readers must see finite PCM or silence while the writer rotates slots.
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (uint64_t sequence = 1; sequence < 5000; ++sequence) {
            block.sequence = sequence;
            mailbox.publish(block, 104, 1000);
        }
        done.store(true);
    });
    auto reader = [&] {
        std::array<float, 16> local{};
        do {
            mailbox.read(104, 16, 1000, local.data());
            for (float sample : local) assert(sample == 0 || sample == .25f);
        } while (!done.load());
    };
    std::thread first(reader), second(reader);
    first.join(); second.join(); writer.join();
    mailbox.close(block.epoch);
    mailbox.read(104, 16, 1000, out.data());
    for (size_t i = 0; i < 16; ++i) assert(out[i] == 0);
}
