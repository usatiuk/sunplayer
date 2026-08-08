#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QtLogging>

class QFile;

struct ApplicationLogOptions {
    bool fileEnabled = true;
    bool debugEnabled = false;
    QString filePath;
    qint64 maximumFileBytes = 8 * 1024 * 1024;
    int retainedFileCount = 10;
    qint64 maximumQueuedBytes = 256 * 1024;
    int maximumQueuedRecords = 1024;

    bool isValid() const;
};

// Installs one process-wide Qt message handler while preserving the handler
// that was active before Sunroom. The handler always forwards to that previous
// handler and optionally mirrors bounded, formatted records into a session
// file. It does not replace QLoggingCategory filtering.
class ApplicationLog final {
  public:
    static std::unique_ptr<ApplicationLog> install(ApplicationLogOptions const& options, QString* error = nullptr);

    ~ApplicationLog();

    ApplicationLog(ApplicationLog const&) = delete;
    ApplicationLog& operator=(ApplicationLog const&) = delete;

    QString filePath() const;
    bool debugEnabled() const;
    void flush();

    static QString defaultLogDirectory();

  private:
    explicit ApplicationLog(ApplicationLogOptions options);

    bool initialize(QString* error);
    void enqueueMessage(QtMsgType type, QMessageLogContext const& context, QString const& message);
    void writerLoop(std::stop_token stopToken, std::promise<QString> initialized);
    void writeRecord(QFile& file, QByteArray const& record);
    void writeDroppedRecordMarker(QFile& file, std::uint64_t count);
    void recordDropLocked(std::uint64_t sequence);
    void stopWriter();
    void pruneRetainedFiles();
    static void messageHandler(QtMsgType type, QMessageLogContext const& context, QString const& message);

    struct PendingWrite {
        QByteArray record;
        std::uint64_t sequence = 0;
    };

    ApplicationLogOptions m_options;
    QString m_filePath;
    qint64 m_writtenBytes = 0;
    bool m_truncationRecorded = false;
    QtMessageHandler m_previousHandler = nullptr;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;
    std::condition_variable m_flushCondition;
    std::deque<PendingWrite> m_pendingWrites;
    qint64 m_queuedBytes = 0;
    int m_queuedRecords = 0;
    std::uint64_t m_droppedRecords = 0;
    std::uint64_t m_firstDroppedSequence = 0;
    std::uint64_t m_lastEnqueuedSequence = 0;
    std::uint64_t m_flushRequestedSequence = 0;
    std::uint64_t m_flushedSequence = 0;
    std::jthread m_writer;

    static QMutex s_handlerMutex;
    static ApplicationLog* s_active;
};
