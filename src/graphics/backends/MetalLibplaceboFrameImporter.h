#pragma once

#include <memory>

#import <Metal/Metal.h>

struct pl_vulkan_t;
using pl_vulkan = const pl_vulkan_t *;

class LibplaceboHardwareFrameImporter;

std::unique_ptr<LibplaceboHardwareFrameImporter>
createMetalLibplaceboFrameImporter(
    pl_vulkan vulkan,
    id<MTLDevice> metalDevice);
