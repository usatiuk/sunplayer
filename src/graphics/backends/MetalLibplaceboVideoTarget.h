#pragma once

#include <memory>

class QRhi;
class VideoTargetInterop;
enum class VideoTargetReadback;
struct pl_vulkan_t;
using pl_vulkan = pl_vulkan_t const*;

std::unique_ptr<VideoTargetInterop> createMetalLibplaceboVideoTarget(QRhi& rhi, pl_vulkan vulkan,
                                                                     VideoTargetReadback readback);
