#pragma once

#include <memory>

class GraphicsDeviceDomain;

std::unique_ptr<GraphicsDeviceDomain>
createD3D11GraphicsDeviceDomain();
