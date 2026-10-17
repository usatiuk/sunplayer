#pragma once

#include <memory>

#include <libplacebo/gpu.h>

class LibplaceboHardwareFrameImporter;

std::unique_ptr<LibplaceboHardwareFrameImporter> createVulkanLibplaceboFrameImporter(pl_gpu gpu);
