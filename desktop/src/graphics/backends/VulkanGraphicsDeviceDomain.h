#pragma once

#include <memory>

class GraphicsDeviceDomain;
class QWindow;

std::unique_ptr<GraphicsDeviceDomain>
createVulkanGraphicsDeviceDomain(QWindow &window);
