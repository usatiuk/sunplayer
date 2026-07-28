#include "video/VideoTargetInterop.h"

bool VideoTargetInteropDiagnostics::isValid() const {
    if (synchronizationMode.isEmpty()) {
        return false;
    }

    switch (outputPath) {
    case VideoOutputPath::DirectRenderTarget:
        return knownGpuCopiesPerRender == 0
            && knownCpuTransfersPerRender == 0
            && fallbackReason.isEmpty();
    case VideoOutputPath::SameDeviceGpuCopy:
        return knownGpuCopiesPerRender > 0
            && knownCpuTransfersPerRender == 0
            && !fallbackReason.isEmpty();
    case VideoOutputPath::CpuRoundTrip:
        return knownCpuTransfersPerRender >= 2
            && !fallbackReason.isEmpty();
    case VideoOutputPath::Unavailable:
        return knownGpuCopiesPerRender == 0
            && knownCpuTransfersPerRender == 0
            && !fallbackReason.isEmpty();
    }
    return false;
}

QString videoOutputPathName(VideoOutputPath path) {
    switch (path) {
    case VideoOutputPath::DirectRenderTarget:
        return QStringLiteral("Direct render target");
    case VideoOutputPath::SameDeviceGpuCopy:
        return QStringLiteral("Same-device GPU copy");
    case VideoOutputPath::CpuRoundTrip:
        return QStringLiteral("CPU round trip");
    case VideoOutputPath::Unavailable:
        return QStringLiteral("Unavailable");
    }
    return QStringLiteral("Unavailable");
}
