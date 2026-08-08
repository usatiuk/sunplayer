#pragma once

#include <memory>

#include <libplacebo/gpu.h>

class LibplaceboHardwareFrameImporter;

std::unique_ptr<LibplaceboHardwareFrameImporter> createD3D11LibplaceboFrameImporter(pl_gpu gpu);
