#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

namespace forge::detail {
// Immutable derived DSP data; the existing AudioClip resource owns the asset.
// Both native pause domains can share this kernel without sharing delay state.
class AudioConvolutionKernel {
  public:
    static constexpr std::size_t block = 256, bins = 2 * block, max_frames = 96000;
    static constexpr std::size_t spectrum_bins = block + 1;
    using Complex = std::complex<double>;
    using Spectrum = std::array<Complex, bins>;
    AudioConvolutionKernel(std::span<const float> impulse, unsigned channels)
        : channels_(channels) {
        if ((channels != 1 && channels != 2) || impulse.empty() || impulse.size() % channels)
            throw std::runtime_error("Impulse response requires complete mono or stereo frames");
        frames_ = impulse.size() / channels;
        if (frames_ > max_frames)
            throw std::runtime_error("Impulse response exceeds 96000 frames at 48000 Hz");
        for (unsigned channel = 0; channel < channels; ++channel) {
            double magnitude = 0;
            for (std::size_t frame = 0; frame < frames_; ++frame) {
                const auto sample = impulse[frame * channels + channel];
                if (!std::isfinite(sample))
                    throw std::runtime_error("Impulse response contains nonfinite samples");
                magnitude += std::abs(double(sample));
            }
            if (magnitude > 64)
                throw std::runtime_error("Impulse response absolute-sample sum exceeds 64");
        }
        for (std::size_t i = 0; i < bins; ++i) {
            std::size_t input = i, reversed = 0;
            for (std::size_t width = bins; width > 1; width >>= 1) {
                reversed = (reversed << 1) | (input & 1);
                input >>= 1;
            }
            reversal_[i] = reversed;
            const auto angle = -2 * std::numbers::pi * double(i) / double(bins);
            roots_[i] = {std::cos(angle), std::sin(angle)};
        }
        partitions_ = frames_ > block ? (frames_ - block + block - 1) / block : 0;
        for (unsigned channel = 0; channel < channels; ++channel) {
            for (std::size_t i = 0; i < std::min(frames_, block); ++i)
                head_[channel][i] = impulse[i * channels + channel];
            auto& spectra = tail_[channel];
            spectra.resize(partitions_ * spectrum_bins);
            for (std::size_t part = 0; part < partitions_; ++part) {
                Spectrum data{};
                const auto offset = (part + 1) * block;
                for (std::size_t i = 0; i < block && offset + i < frames_; ++i)
                    data[i] = impulse[(offset + i) * channels + channel];
                transform(data, false);
                std::copy_n(data.begin(), spectrum_bins, spectra.begin() + part * spectrum_bins);
            }
        }
    }
    std::size_t frames() const noexcept { return frames_; }
    std::size_t partitions() const noexcept { return partitions_; }
    std::size_t bytes() const noexcept {
        return sizeof(*this) + (tail_[0].capacity() + tail_[1].capacity()) * sizeof(Complex);
    }
    const auto& head(unsigned channel) const noexcept {
        return head_[channels_ == 1 ? 0 : channel];
    }
    const auto& tail(unsigned channel) const noexcept {
        return tail_[channels_ == 1 ? 0 : channel];
    }
    void transform(Spectrum& values, bool inverse) const noexcept {
        for (std::size_t i = 0; i < bins; ++i)
            if (i < reversal_[i])
                std::swap(values[i], values[reversal_[i]]);
        for (std::size_t width = 2; width <= bins; width <<= 1)
            for (std::size_t start = 0; start < bins; start += width)
                for (std::size_t j = 0; j < width / 2; ++j) {
                    const auto root = roots_[j * (bins / width)];
                    const auto odd =
                        values[start + j + width / 2] * (inverse ? std::conj(root) : root);
                    const auto even = values[start + j];
                    values[start + j] = even + odd;
                    values[start + j + width / 2] = even - odd;
                }
        if (inverse)
            for (auto& value : values)
                value /= double(bins);
    }

  private:
    unsigned channels_;
    std::size_t frames_, partitions_;
    std::array<std::array<float, block>, 2> head_{};
    std::array<std::vector<Complex>, 2> tail_;
    Spectrum roots_;
    std::array<std::size_t, bins> reversal_;
};

// Stereo processing has a direct head and frequency-domain delayed partitions.
// The head avoids an added block delay. Every allocation precedes processing.
class AudioConvolution {
  public:
    explicit AudioConvolution(std::shared_ptr<const AudioConvolutionKernel> kernel, float wet)
        : kernel_(std::move(kernel)), wet_(wet) {
        if (!kernel_ || !std::isfinite(wet) || wet < 0 || wet > 1)
            throw std::runtime_error("Invalid impulse response processor configuration");
        for (auto& channel : history_)
            channel.resize(kernel_->partitions() * Kernel::spectrum_bins);
    }
    AudioConvolution(const AudioConvolution&) = delete;
    AudioConvolution& operator=(const AudioConvolution&) = delete;
    static std::size_t storage_bytes(std::size_t partitions) noexcept {
        return sizeof(AudioConvolution) +
               2 * partitions * Kernel::spectrum_bins * sizeof(Kernel::Complex);
    }
    std::size_t bytes() const noexcept {
        return sizeof(*this) +
               (history_[0].capacity() + history_[1].capacity()) * sizeof(Kernel::Complex);
    }
    std::uint64_t tail_frames() const noexcept { return kernel_->frames() - 1; }
    void clear() noexcept {
        cursor_ = partition_ = 0;
        remaining_ = 0;
        idle_ = true;
        for (auto& channel : direct_)
            channel.fill(0);
        for (auto& channel : ready_)
            channel.fill(0);
        for (auto& channel : overlap_)
            channel.fill(0);
        for (auto& channel : history_)
            std::fill(channel.begin(), channel.end(), Kernel::Complex{});
    }
    // Null input drains silence. Exact input/output aliasing is supported.
    void process(const float* input, float* output, std::size_t frames) noexcept {
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const std::array samples{input ? finite(input[2 * frame]) : 0.f,
                                     input ? finite(input[2 * frame + 1]) : 0.f};
            if (samples[0] != 0 || samples[1] != 0) {
                remaining_ = kernel_->frames() - 1;
                idle_ = false;
            } else if (!remaining_) {
                if (!idle_)
                    clear();
                output[2 * frame] = output[2 * frame + 1] = 0;
                continue;
            } else
                --remaining_;
            for (unsigned channel = 0; channel < 2; ++channel) {
                const auto index = 2 * frame + channel;
                const auto sample = samples[channel];
                direct_[channel][cursor_] = sample;
                double result = ready_[channel][cursor_];
                const auto& head = kernel_->head(channel);
                for (std::size_t tap = 0; tap < std::min(kernel_->frames(), Kernel::block); ++tap)
                    result += double(head[tap]) *
                              direct_[channel][(cursor_ + Kernel::block - tap) % Kernel::block];
                output[index] = finite(float((1 - wet_) * sample + wet_ * result));
            }
            if (++cursor_ == Kernel::block) {
                if (kernel_->partitions())
                    advance();
                cursor_ = 0;
            }
        }
    }

  private:
    using Kernel = AudioConvolutionKernel;
    std::shared_ptr<const Kernel> kernel_;
    float wet_;
    std::size_t cursor_ = 0, partition_ = 0;
    std::size_t remaining_ = 0;
    bool idle_ = true;
    std::array<std::array<float, Kernel::block>, 2> direct_{};
    std::array<std::array<double, Kernel::block>, 2> ready_{}, overlap_{};
    std::array<std::vector<Kernel::Complex>, 2> history_;
    Kernel::Spectrum transformed_{}, summed_{};
    static float finite(float value) noexcept {
        return std::isfinite(value) && std::abs(value) >= 1e-20f ? value : 0;
    }
    void advance() noexcept {
        const auto count = kernel_->partitions();
        for (unsigned channel = 0; channel < 2; ++channel) {
            transformed_.fill({});
            for (std::size_t i = 0; i < Kernel::block; ++i)
                transformed_[i] = direct_[channel][i];
            kernel_->transform(transformed_, false);
            auto& history = history_[channel];
            std::copy_n(transformed_.begin(), Kernel::spectrum_bins,
                        history.begin() + partition_ * Kernel::spectrum_bins);
            summed_.fill({});
            const auto& response = kernel_->tail(channel);
            for (std::size_t part = 0; part < count; ++part) {
                const auto past = (partition_ + count - part) % count;
                for (std::size_t bin = 0; bin < Kernel::spectrum_bins; ++bin)
                    summed_[bin] += history[past * Kernel::spectrum_bins + bin] *
                                    response[part * Kernel::spectrum_bins + bin];
            }
            // Real input and kernels have conjugate negative frequencies.
            for (std::size_t bin = 1; bin < Kernel::block; ++bin)
                summed_[Kernel::bins - bin] = std::conj(summed_[bin]);
            kernel_->transform(summed_, true);
            for (std::size_t i = 0; i < Kernel::block; ++i) {
                ready_[channel][i] = summed_[i].real() + overlap_[channel][i];
                overlap_[channel][i] = summed_[i + Kernel::block].real();
            }
        }
        partition_ = (partition_ + 1) % count;
    }
};
} // namespace forge::detail
