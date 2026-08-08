#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

// Fixed-capacity callback-to-control observation ledger. Each output callback
// records media followed by optional hold silence. Readers can then translate
// cubeb's presented output-frame position without advancing media time across
// underruns. Slot fields are atomic because a callback may wrap and overwrite
// old entries while the control thread is sampling them.
struct AudioOutputPosition {
    bool operator==(AudioOutputPosition const&) const = default;

    std::uint64_t mediaFrame = 0;
    bool holding = false;
};

class AudioOutputLedger final {
  public:
    explicit AudioOutputLedger(std::size_t spanCapacity);

    void reset();
    void record(std::size_t mediaFrames, std::size_t holdFrames);
    std::optional<AudioOutputPosition> positionForOutputFrame(std::uint64_t outputFrame) const;

    std::uint64_t outputFrames() const;
    std::uint64_t mediaFrames() const;

  private:
    struct Slot {
        std::atomic<std::uint64_t> sequence{0};
        std::atomic<std::uint64_t> outputStart{0};
        std::atomic<std::uint64_t> mediaStart{0};
        std::atomic<std::uint64_t> mediaFrameCount{0};
        std::atomic<std::uint64_t> holdFrameCount{0};
    };

    std::size_t const m_spanCapacity;
    std::unique_ptr<Slot[]> m_slots;
    std::atomic<std::uint64_t> m_publishedSequence{0};
    std::atomic<std::uint64_t> m_outputFrames{0};
    std::atomic<std::uint64_t> m_mediaFrames{0};
};
