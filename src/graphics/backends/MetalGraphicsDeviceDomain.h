#pragma once

#include <memory>

class GraphicsDeviceDomain;

std::unique_ptr<GraphicsDeviceDomain> createMetalGraphicsDeviceDomain();
