#pragma once

#include <QMetaType>

enum class VideoToneMappingAlgorithm { Bt2446a, Spline, Bt2390 };

struct VideoToneMappingSettings {
    bool operator==(VideoToneMappingSettings const&) const = default;
    VideoToneMappingAlgorithm sdr = VideoToneMappingAlgorithm::Bt2446a;
    VideoToneMappingAlgorithm hdr = VideoToneMappingAlgorithm::Spline;
    bool peakDetectionEnabled = true;

    bool isValid() const {
        bool const sdrValid = sdr == VideoToneMappingAlgorithm::Bt2446a || sdr == VideoToneMappingAlgorithm::Spline ||
                              sdr == VideoToneMappingAlgorithm::Bt2390;
        return sdrValid && (hdr == VideoToneMappingAlgorithm::Spline || hdr == VideoToneMappingAlgorithm::Bt2390);
    }
};

Q_DECLARE_METATYPE(VideoToneMappingAlgorithm)
Q_DECLARE_METATYPE(VideoToneMappingSettings)
