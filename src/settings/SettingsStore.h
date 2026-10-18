#pragma once

#include <memory>

#include <QAnyStringView>
#include <QString>
#include <QVariant>

// Storage only: consumers own keys, validation, defaults and credential policy.
// Instances and their consumers stay on their owning thread.
class SettingsStore {
  public:
    enum class Status { NoError, AccessError, FormatError };
    virtual ~SettingsStore() = default;

    virtual bool contains(QAnyStringView key) const = 0;
    virtual QVariant value(QAnyStringView key, QVariant const& fallback = {}) const = 0;
    // Keys are nonempty, application-owned names. Values are scalar booleans, numbers and strings.
    virtual void setValue(QAnyStringView key, QVariant const& value) = 0;
    // Remove the exact key and slash-delimited descendants, preserving siblings.
    virtual void remove(QAnyStringView key) = 0;
    // Complete pending writes/import external changes; immediate stores need no flush.
    virtual void sync() = 0;
    // Preserve the first failure for this instance's lifetime.
    virtual Status status() const = 0;
    virtual char const* backendName() const = 0;
};

std::unique_ptr<SettingsStore> createSettingsStore();
std::unique_ptr<SettingsStore> createTemporarySettingsStore(QString const& filePath);
