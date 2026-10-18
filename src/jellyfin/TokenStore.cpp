#include "jellyfin/TokenStore.h"
#include <qt6keychain/keychain.h>

namespace {
constexpr auto service = "SunPlayer";
void configure(QKeychain::Job& job, QString const& entry) {
    job.setKey(entry);
    job.setInsecureFallback(false);
}
} // namespace
void TokenStore::read(std::function<void(QString, QString)> done) {
    auto* job = new QKeychain::ReadPasswordJob(QString::fromLatin1(service), this);
    configure(*job, m_entry);
    connect(job, &QKeychain::Job::finished, this, [job, done = std::move(done)] {
        done(job->error() == QKeychain::NoError ? job->textData() : QString{},
             job->error() == QKeychain::NoError ? QString{}
                                                : tr("Secure storage is unavailable. Please sign in again."));
    });
    job->start();
}
void TokenStore::write(QString const& token, std::function<void(QString)> done) {
    auto* job = new QKeychain::WritePasswordJob(QString::fromLatin1(service), this);
    configure(*job, m_entry);
    job->setTextData(token);
    connect(job, &QKeychain::Job::finished, this, [job, done = std::move(done)] {
        done(job->error() == QKeychain::NoError ? QString{}
                                                : tr("Login could not be saved securely. This login is session-only."));
    });
    job->start();
}
void TokenStore::remove(std::function<void(QString)> done) {
    auto* job = new QKeychain::DeletePasswordJob(QString::fromLatin1(service), this);
    configure(*job, m_entry);
    connect(job, &QKeychain::Job::finished, this, [job, done = std::move(done)] {
        done(job->error() == QKeychain::NoError || job->error() == QKeychain::EntryNotFound
                 ? QString{}
                 : tr("The saved token could not be deleted. Check secure storage before signing in again."));
    });
    job->start();
}
