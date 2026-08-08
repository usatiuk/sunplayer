#pragma once

#include <memory>

class DisplayStateProvider;
class QObject;

std::unique_ptr<DisplayStateProvider> createMacDisplayStateProvider(QObject* parent);
