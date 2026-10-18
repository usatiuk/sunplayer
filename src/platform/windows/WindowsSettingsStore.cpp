#include "settings/SettingsStore.h"

#include "diagnostics/LogCategories.h"
#include "platform/windows/WindowsRuntimeApartment.h"
#include "settings/QSettingsStore.h"

#include <QMetaType>
#include <QStringList>
#include <appmodel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>

using winrt::Windows::Foundation::IPropertyValue;
using winrt::Windows::Foundation::PropertyType;
using winrt::Windows::Foundation::Collections::IPropertySet;
using winrt::Windows::Storage::ApplicationData;

namespace {
winrt::hstring nativeKey(QAnyStringView key) { return winrt::hstring(key.toString().toStdWString()); }

winrt::Windows::Foundation::IInspectable boxValue(QVariant const& value) {
    switch (value.metaType().id()) {
    case QMetaType::Bool:
        return winrt::box_value(value.toBool());
    case QMetaType::Int:
        return winrt::box_value(int32_t(value.toInt()));
    case QMetaType::UInt:
        return winrt::box_value(uint32_t(value.toUInt()));
    case QMetaType::LongLong:
        return winrt::box_value(int64_t(value.toLongLong()));
    case QMetaType::ULongLong:
        return winrt::box_value(uint64_t(value.toULongLong()));
    case QMetaType::Float:
        return winrt::box_value(value.toFloat());
    case QMetaType::Double:
        return winrt::box_value(value.toDouble());
    case QMetaType::QString:
        return winrt::box_value(winrt::hstring(value.toString().toStdWString()));
    default:
        qFatal("Unsupported application settings value type: %d", value.metaType().id());
    }
}

class WindowsSettingsStore final : public SettingsStore {
  public:
    WindowsSettingsStore() : m_values(ApplicationData::Current().LocalSettings().Values()) {
        qCInfo(sunplayerLogApplication, "event=application.settings_store backend=windows_localsettings");
    }

    bool contains(QAnyStringView key) const override {
        try {
            return m_values.HasKey(nativeKey(key));
        } catch (winrt::hresult_error const& error) {
            failed("contains", error);
            return false;
        }
    }
    QVariant value(QAnyStringView key, QVariant const& fallback) const override {
        try {
            auto const item = m_values.TryLookup(nativeKey(key));
            if (!item) {
                return fallback;
            }
            auto const property = item.try_as<IPropertyValue>();
            if (property) {
                switch (property.Type()) {
                case PropertyType::Boolean:
                    return property.GetBoolean();
                case PropertyType::Int32:
                    return int(property.GetInt32());
                case PropertyType::UInt32:
                    return uint(property.GetUInt32());
                case PropertyType::Int64:
                    return QVariant::fromValue(qlonglong(property.GetInt64()));
                case PropertyType::UInt64:
                    return QVariant::fromValue(qulonglong(property.GetUInt64()));
                case PropertyType::Single:
                    return property.GetSingle();
                case PropertyType::Double:
                    return property.GetDouble();
                case PropertyType::String: {
                    auto const text = property.GetString();
                    return QString::fromWCharArray(text.c_str(), text.size());
                }
                default:
                    break;
                }
            }
            if (m_status == Status::NoError) {
                m_status = Status::FormatError;
            }
            qCWarning(sunplayerLogApplication, "event=application.settings_failure backend=windows_localsettings "
                                               "operation=read reason=unsupported_type");
        } catch (winrt::hresult_error const& error) { failed("read", error); }
        return fallback;
    }
    void setValue(QAnyStringView key, QVariant const& value) override {
        try {
            m_values.Insert(nativeKey(key), boxValue(value));
        } catch (winrt::hresult_error const& error) { failed("write", error); }
    }
    void remove(QAnyStringView key) override {
        try {
            QString const name = key.toString();
            QString const prefix = name + u'/';
            QStringList matches;
            for (auto const& item : m_values) {
                auto const native = item.Key();
                QString const candidate = QString::fromWCharArray(native.c_str(), native.size());
                if (candidate == name || candidate.startsWith(prefix)) {
                    matches.append(candidate);
                }
            }
            // Removing while enumerating invalidates the native iterator.
            for (auto const& match : matches) {
                m_values.Remove(nativeKey(match));
            }
        } catch (winrt::hresult_error const& error) { failed("remove", error); }
    }
    void sync() override {} // LocalSettings writes are immediate and OS-managed.
    Status status() const override { return m_status; }
    char const* backendName() const override { return "windows_localsettings"; }

  private:
    void failed(char const* operation, winrt::hresult_error const& error) const {
        if (m_status == Status::NoError) {
            m_status = Status::AccessError;
        }
        qCWarning(sunplayerLogApplication,
                  "event=application.settings_failure backend=windows_localsettings operation=%s hresult=0x%08X",
                  operation, static_cast<unsigned int>(error.code().value));
    }

    WindowsRuntimeApartment m_apartment;
    IPropertySet m_values;
    mutable Status m_status = Status::NoError;
};
} // namespace

std::unique_ptr<SettingsStore> createSettingsStore() {
    UINT32 length = 0;
    LONG const result = GetCurrentPackageFullName(&length, nullptr);
    if (result == APPMODEL_ERROR_NO_PACKAGE) {
        return std::make_unique<QSettingsStore>();
    }
    if (result != ERROR_INSUFFICIENT_BUFFER) {
        qFatal("Cannot query settings package identity: error=%ld", result);
    }
    try {
        return std::make_unique<WindowsSettingsStore>();
    } catch (winrt::hresult_error const& error) {
        qFatal("Cannot initialize package LocalSettings: hresult=0x%08X",
               static_cast<unsigned int>(error.code().value));
    }
}
