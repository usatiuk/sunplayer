#include "diagnostics/ApplicationLog.h"

#include <algorithm>
#include <cstdio>
#include <future>
#include <limits>
#include <string>
#include <utility>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QThread>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
QString messageTypeName(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("debug");
    case QtInfoMsg:
        return QStringLiteral("info");
    case QtWarningMsg:
        return QStringLiteral("warning");
    case QtCriticalMsg:
        return QStringLiteral("critical");
    case QtFatalMsg:
        return QStringLiteral("fatal");
    }
    return QStringLiteral("unknown");
}

QString automaticFileName() {
    return QStringLiteral("sunplayer-%1-%2.log")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")))
        .arg(QCoreApplication::applicationPid());
}

QString formattedRecord(QtMsgType type, QMessageLogContext const& context, QString const& message) {
    QString const category =
        context.category && *context.category ? QString::fromLatin1(context.category) : QStringLiteral("default");
    quintptr const threadId = reinterpret_cast<quintptr>(QThread::currentThreadId());
    return QStringLiteral("%1 level=%2 category=%3 thread=0x%4 %5\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), messageTypeName(type), category,
             QString::number(threadId, 16), message);
}

QByteArray droppedRecordMarker(std::uint64_t count) {
    return QStringLiteral("%1 level=warning category=sunplayer.application "
                          "event=log.records_dropped count=%2 reason=queue_full\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
        .arg(count)
        .toUtf8();
}

QByteArray boundedFatalRecord(QString const& record, qint64 maximumBytes) {
    QByteArray encoded = record.toUtf8();
    if (encoded.size() <= maximumBytes) {
        return encoded;
    }

    QByteArray const suffix = QByteArrayLiteral(" event=log.fatal_record_truncated\n");
    qint64 const prefixBudget = maximumBytes - suffix.size();
    qsizetype low = 0;
    qsizetype high = record.size();
    while (low < high) {
        qsizetype const midpoint = low + (high - low + 1) / 2;
        if (record.left(midpoint).toUtf8().size() <= prefixBudget) {
            low = midpoint;
        } else {
            high = midpoint - 1;
        }
    }
    encoded = record.left(low).toUtf8();
    while (encoded.endsWith('\n') || encoded.endsWith('\r')) {
        encoded.chop(1);
    }
    encoded.append(suffix);
    return encoded;
}

void forwardToDefaultHandler(QtMsgType type, QMessageLogContext const& context, QString const& message) {
    QString const formatted = qFormatLogMessage(type, context, message) + u'\n';
    QByteArray const local = formatted.toLocal8Bit();
    std::fwrite(local.constData(), 1, static_cast<std::size_t>(local.size()), stderr);
    std::fflush(stderr);
#ifdef Q_OS_WIN
    if (IsDebuggerPresent()) {
        OutputDebugStringW(formatted.toStdWString().c_str());
    }
#endif
}

bool isSupportedLocalLogPath(QFileInfo const& fileInfo) {
#ifdef Q_OS_WIN
    const QString nativePath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
    bool const extendedPath = nativePath.startsWith(QStringLiteral("\\\\?\\"));
    if (nativePath.startsWith(QStringLiteral("\\\\?\\UNC\\"), Qt::CaseInsensitive) ||
        (nativePath.startsWith(QStringLiteral("\\\\")) && !extendedPath)) {
        return false;
    }

    qsizetype const driveOffset = extendedPath ? 4 : 0;
    if (nativePath.size() < driveOffset + 3 || !nativePath[driveOffset].isLetter() ||
        nativePath[driveOffset + 1] != u':' || nativePath[driveOffset + 2] != u'\\') {
        return false;
    }
    std::wstring const volumeRoot = nativePath.left(driveOffset + 3).toStdWString();
    const UINT driveType = GetDriveTypeW(volumeRoot.c_str());
    return driveType != DRIVE_REMOTE && driveType != DRIVE_UNKNOWN && driveType != DRIVE_NO_ROOT_DIR;
#else
    Q_UNUSED(fileInfo);
    return true;
#endif
}
} // namespace

QMutex ApplicationLog::s_handlerMutex;
ApplicationLog* ApplicationLog::s_active = nullptr;

bool ApplicationLogOptions::isValid() const {
    return maximumFileBytes > 0 && retainedFileCount > 0 && maximumQueuedBytes >= 256 && maximumQueuedRecords > 0;
}

std::unique_ptr<ApplicationLog> ApplicationLog::install(ApplicationLogOptions const& options, QString* error) {
    if (!options.isValid()) {
        if (error) {
            *error = QStringLiteral("Application logging options are invalid");
        }
        return {};
    }

    auto logging = std::unique_ptr<ApplicationLog>(new ApplicationLog(options));
    if (!logging->initialize(error)) {
        return {};
    }
    if (error) {
        error->clear();
    }
    return logging;
}

ApplicationLog::ApplicationLog(ApplicationLogOptions options) : m_options(std::move(options)) {}

ApplicationLog::~ApplicationLog() {
    {
        QMutexLocker lock(&s_handlerMutex);
        if (s_active == this) {
            qInstallMessageHandler(m_previousHandler);
            s_active = nullptr;
        }
    }
    stopWriter();
}

QString ApplicationLog::filePath() const { return m_filePath; }

bool ApplicationLog::debugEnabled() const { return m_options.debugEnabled; }

void ApplicationLog::flush() {
    if (!m_writer.joinable()) {
        return;
    }

    std::unique_lock lock(m_queueMutex);
    std::uint64_t const target = m_lastEnqueuedSequence;
    if (target <= m_flushedSequence) {
        return;
    }
    m_flushRequestedSequence = std::max(m_flushRequestedSequence, target);
    lock.unlock();
    m_queueCondition.notify_one();
    lock.lock();
    m_flushCondition.wait(lock, [this, target] { return m_flushedSequence >= target; });
}

QString ApplicationLog::defaultLogDirectory() {
    QString const temporary = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(temporary).filePath(QStringLiteral("SunPlayer/logs"));
}

bool ApplicationLog::initialize(QString* error) {
    QMutexLocker lock(&s_handlerMutex);
    if (s_active) {
        if (error) {
            *error = QStringLiteral("Application logging is already installed");
        }
        return false;
    }

    if (m_options.fileEnabled) {
        QString path = m_options.filePath;
        bool const automaticPath = path.isEmpty();
        if (automaticPath) {
            path = QDir(defaultLogDirectory()).filePath(automaticFileName());
        }
        QFileInfo const fileInfo(path);
        if (!isSupportedLocalLogPath(fileInfo)) {
            if (error) {
                *error = QStringLiteral("Session logs require a local file path");
            }
            return false;
        }
        if (!QDir().mkpath(fileInfo.absolutePath())) {
            if (error) {
                *error = QStringLiteral("Could not create log directory %1").arg(fileInfo.absolutePath());
            }
            return false;
        }
        m_filePath = fileInfo.absoluteFilePath();
        if (automaticPath) {
            pruneRetainedFiles();
        }
        std::promise<QString> initialized;
        std::future<QString> initialization = initialized.get_future();
        m_writer = std::jthread([this, initialized = std::move(initialized)](std::stop_token stopToken) mutable {
            writerLoop(stopToken, std::move(initialized));
        });
        QString const initializationError = initialization.get();
        if (!initializationError.isEmpty()) {
            stopWriter();
            if (error) {
                *error = initializationError;
            }
            return false;
        }
    }

    if (m_options.debugEnabled) {
        QString rules = qEnvironmentVariable("QT_LOGGING_RULES");
        if (!rules.isEmpty() && !rules.endsWith(u'\n')) {
            rules.append(u'\n');
        }
        rules.append(QStringLiteral("sunplayer.*.debug=true"));
        QLoggingCategory::setFilterRules(rules);
    }

    s_active = this;
    m_previousHandler = qInstallMessageHandler(messageHandler);
    return true;
}

void ApplicationLog::enqueueMessage(QtMsgType type, QMessageLogContext const& context, QString const& message) {
    if (!m_writer.joinable()) {
        return;
    }

    QString const formatted = formattedRecord(type, context, message);
    QByteArray record =
        type == QtFatalMsg ? boundedFatalRecord(formatted, m_options.maximumQueuedBytes) : formatted.toUtf8();
    {
        std::lock_guard lock(m_queueMutex);
        std::uint64_t const sequence = ++m_lastEnqueuedSequence;
        if (type == QtFatalMsg) {
            while ((!m_pendingWrites.empty()) && (m_queuedBytes > m_options.maximumQueuedBytes - record.size() ||
                                                  m_queuedRecords >= m_options.maximumQueuedRecords)) {
                PendingWrite evicted = std::move(m_pendingWrites.front());
                m_pendingWrites.pop_front();
                m_queuedBytes -= evicted.record.size();
                --m_queuedRecords;
                recordDropLocked(evicted.sequence);
            }
            m_queuedBytes += record.size();
            ++m_queuedRecords;
            m_pendingWrites.push_back({
                .record = std::move(record),
                .sequence = sequence,
            });
        } else if (record.size() > m_options.maximumQueuedBytes ||
                   m_queuedBytes > m_options.maximumQueuedBytes - record.size() ||
                   m_queuedRecords >= m_options.maximumQueuedRecords) {
            recordDropLocked(sequence);
        } else {
            m_queuedBytes += record.size();
            ++m_queuedRecords;
            m_pendingWrites.push_back({
                .record = std::move(record),
                .sequence = sequence,
            });
        }
    }
    m_queueCondition.notify_one();
}

void ApplicationLog::writerLoop(std::stop_token stopToken, std::promise<QString> initialized) {
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        initialized.set_value(
            QStringLiteral("Could not open log file %1: %2").arg(file.fileName(), file.errorString()));
        return;
    }
    initialized.set_value({});

    constexpr std::size_t maximumRecordsBetweenFlushes = 64;
    std::size_t recordsSinceFlush = 0;
    for (;;) {
        enum class Action {
            Record,
            DroppedMarker,
            Flush,
            Stop,
        };
        Action action = Action::Stop;
        PendingWrite pending;
        std::uint64_t droppedRecords = 0;
        std::uint64_t flushTarget = 0;
        bool queueDrained = false;
        {
            std::unique_lock lock(m_queueMutex);
            m_queueCondition.wait(lock, [this, stopToken] {
                return stopToken.stop_requested() || !m_pendingWrites.empty() || m_droppedRecords > 0 ||
                       m_flushRequestedSequence > m_flushedSequence;
            });

            std::uint64_t const nextRecordSequence =
                m_pendingWrites.empty() ? std::numeric_limits<std::uint64_t>::max() : m_pendingWrites.front().sequence;
            std::uint64_t const nextDroppedSequence =
                m_droppedRecords == 0 ? std::numeric_limits<std::uint64_t>::max() : m_firstDroppedSequence;
            if (m_flushRequestedSequence > m_flushedSequence && nextRecordSequence > m_flushRequestedSequence &&
                nextDroppedSequence > m_flushRequestedSequence) {
                action = Action::Flush;
                flushTarget = m_flushRequestedSequence;
            } else if (!m_pendingWrites.empty() && nextRecordSequence <= nextDroppedSequence) {
                action = Action::Record;
                pending = std::move(m_pendingWrites.front());
                m_pendingWrites.pop_front();
                m_queuedBytes -= pending.record.size();
                --m_queuedRecords;
                queueDrained = m_pendingWrites.empty() && m_droppedRecords == 0;
            } else if (nextDroppedSequence != std::numeric_limits<std::uint64_t>::max()) {
                action = Action::DroppedMarker;
                droppedRecords = std::exchange(m_droppedRecords, 0);
                m_firstDroppedSequence = 0;
            } else if (stopToken.stop_requested()) {
                action = Action::Stop;
            }
        }

        if (action == Action::Record) {
            writeRecord(file, pending.record);
            ++recordsSinceFlush;
            if (queueDrained || recordsSinceFlush >= maximumRecordsBetweenFlushes) {
                file.flush();
                recordsSinceFlush = 0;
            }
        } else if (action == Action::DroppedMarker) {
            writeDroppedRecordMarker(file, droppedRecords);
            file.flush();
            recordsSinceFlush = 0;
        } else if (action == Action::Flush) {
            file.flush();
            recordsSinceFlush = 0;
            {
                std::lock_guard lock(m_queueMutex);
                m_flushedSequence = std::max(m_flushedSequence, flushTarget);
            }
            m_flushCondition.notify_all();
        } else {
            break;
        }
    }
    file.flush();
    {
        std::lock_guard lock(m_queueMutex);
        m_flushedSequence = std::max(m_flushedSequence, m_lastEnqueuedSequence);
    }
    m_flushCondition.notify_all();
}

void ApplicationLog::writeRecord(QFile& file, QByteArray const& record) {
    qint64 const remaining = m_options.maximumFileBytes - m_writtenBytes;
    if (record.size() <= remaining) {
        qint64 const written = file.write(record);
        if (written > 0) {
            m_writtenBytes += written;
        }
        return;
    }

    if (!m_truncationRecorded) {
        m_truncationRecorded = true;
        QByteArray const marker = QByteArrayLiteral("level=warning category=sunplayer.application "
                                                    "event=log.truncated reason=maximum_file_size\n");
        if (marker.size() <= remaining) {
            qint64 const written = file.write(marker);
            if (written > 0) {
                m_writtenBytes += written;
            }
        }
    }
}

void ApplicationLog::writeDroppedRecordMarker(QFile& file, std::uint64_t count) {
    writeRecord(file, droppedRecordMarker(count));
}

void ApplicationLog::recordDropLocked(std::uint64_t sequence) {
    if (m_droppedRecords == 0) {
        m_firstDroppedSequence = sequence;
    } else {
        m_firstDroppedSequence = std::min(m_firstDroppedSequence, sequence);
    }
    ++m_droppedRecords;
}

void ApplicationLog::stopWriter() {
    if (!m_writer.joinable()) {
        return;
    }
    m_writer.request_stop();
    m_queueCondition.notify_one();
    m_writer.join();
}

void ApplicationLog::pruneRetainedFiles() {
    QFileInfo const current(m_filePath);
    QDir directory(current.absolutePath());
    QFileInfoList files =
        directory.entryInfoList({QStringLiteral("sunplayer-*.log")}, QDir::Files, QDir::Time | QDir::Reversed);
    qsizetype const removeCount =
        std::max<qsizetype>(0, files.size() - static_cast<qsizetype>(m_options.retainedFileCount) + 1);
    for (qsizetype index = 0; index < removeCount; ++index) {
        QFile::remove(files[index].absoluteFilePath());
    }
}

void ApplicationLog::messageHandler(QtMsgType type, QMessageLogContext const& context, QString const& message) {
    QtMessageHandler previous = nullptr;
    {
        QMutexLocker lock(&s_handlerMutex);
        if (s_active) {
            s_active->enqueueMessage(type, context, message);
            previous = s_active->m_previousHandler;
            if (type == QtFatalMsg) {
                s_active->flush();
            }
        }
    }
    if (previous && previous != messageHandler) {
        previous(type, context, message);
    } else {
        forwardToDefaultHandler(type, context, message);
    }
}
