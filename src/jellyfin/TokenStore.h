#pragma once
#include <QObject>
#include <functional>

// Persistence only; the authenticated session remains the canonical token owner.
class TokenStore final : public QObject {
    Q_OBJECT
  public:
    explicit TokenStore(QString entry, QObject* parent = nullptr) : QObject(parent), m_entry(std::move(entry)) {}
    void read(std::function<void(QString token, QString error)> done);
    void write(QString const& token, std::function<void(QString error)> done);
    void remove(std::function<void(QString error)> done);

  private:
    QString m_entry;
};
