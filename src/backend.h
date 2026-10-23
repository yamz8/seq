#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <atomic>
#include <memory>

#include "chart.h"
#include "exporter.h"
#include "fastx.h"
#include "models.h"
#include "seqtools.h"

class FilePicker;

struct SearchMatch {
    qsizetype record = 0;
    // 0-based on the forward strand; -1 when the record matched by its ID.
    qint64 position = -1;
    char strand = '+';
};

// The open file, its index, the selected record, and the search, for QML.
// Indexing, searching and exporting run on a worker thread; one at a time.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loaded READ loaded NOTIFY fileChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY fileChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileChanged)
    Q_PROPERTY(QString format READ format NOTIFY fileChanged)
    Q_PROPERTY(bool protein READ protein NOTIFY fileChanged)
    Q_PROPERTY(bool fastq READ fastq NOTIFY fileChanged)
    Q_PROPERTY(bool compressed READ compressed NOTIFY fileChanged)
    Q_PROPERTY(int totalRecords READ totalRecords NOTIFY fileChanged)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY fileChanged)
    Q_PROPERTY(RecordModel *records READ records CONSTANT)

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString busyKind READ busyKind NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)

    Q_PROPERTY(int currentRow READ currentRow NOTIFY currentChanged)
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
    Q_PROPERTY(QVariantList currentQualities READ currentQualities NOTIFY currentChanged)
    Q_PROPERTY(bool reversed READ reversed NOTIFY currentChanged)
    Q_PROPERTY(int lineWidth READ lineWidth NOTIFY lineWidthChanged)
    Q_PROPERTY(LinesModel *sequenceLines READ sequenceLines CONSTANT)
    Q_PROPERTY(LinesModel *translationLines READ translationLines CONSTANT)
    Q_PROPERTY(QVariantList orfs READ orfs NOTIFY orfsChanged)
    Q_PROPERTY(int orfMinLength READ orfMinLength WRITE setOrfMinLength NOTIFY orfsChanged)

    Q_PROPERTY(bool searchActive READ searchActive NOTIFY searchChanged)
    Q_PROPERTY(QString searchText READ searchText NOTIFY searchChanged)
    Q_PROPERTY(bool motifSearch READ motifSearch NOTIFY searchChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY searchChanged)
    Q_PROPERTY(int matchIndex READ matchIndex NOTIFY searchChanged)
    Q_PROPERTY(bool matchesTruncated READ matchesTruncated NOTIFY searchChanged)

    Q_PROPERTY(QString chartColor MEMBER m_chartColor NOTIFY chartColorChanged)

public:
    explicit Backend(FilePicker *picker = nullptr, QObject *parent = nullptr);
    ~Backend() override;

    bool loaded() const { return bool(m_index); }
    QString filePath() const { return m_path; }
    QString fileName() const;
    QString format() const;
    bool protein() const { return m_index && m_index->alphabet() == Alphabet::Protein; }
    bool fastq() const { return m_index && m_index->format() == Format::Fastq; }
    bool compressed() const;
    int totalRecords() const { return m_index ? int(m_index->size()) : 0; }
    QVariantMap summary() const { return m_summary; }
    RecordModel *records() { return &m_records; }

    bool busy() const { return !m_busyKind.isEmpty(); }
    QString busyKind() const { return m_busyKind; }
    double progress() const { return m_progressValue; }
    QString error() const { return m_error; }
    QString notice() const { return m_notice; }

    int currentRow() const { return m_currentRow; }
    QVariantMap current() const { return m_current; }
    QVariantList currentQualities() const { return m_qualities; }
    bool reversed() const { return m_reversed; }
    int lineWidth() const { return m_lineWidth; }
    LinesModel *sequenceLines() { return &m_sequenceLines; }
    LinesModel *translationLines() { return &m_translationLines; }
    QVariantList orfs() const { return m_orfs; }
    int orfMinLength() const { return m_orfMinLength; }
    void setOrfMinLength(int length);

    bool searchActive() const { return m_searchActive; }
    QString searchText() const { return m_searchText; }
    bool motifSearch() const { return m_motifSearch; }
    int matchCount() const { return int(m_matches.size()); }
    int matchIndex() const { return m_matchIndex; }
    bool matchesTruncated() const { return m_matchesTruncated; }
    const std::vector<SearchMatch> &matches() const { return m_matches; }
    QByteArray currentSequence() const { return m_record.sequence; }

    Q_INVOKABLE void open();
    Q_INVOKABLE void openUrl(const QUrl &url);
    Q_INVOKABLE void openPath(const QString &path);
    Q_INVOKABLE void select(int row);
    Q_INVOKABLE void move(int delta);
    Q_INVOKABLE void toggleReverse();
    Q_INVOKABLE void toggleLineWidth();
    // 0 sequence, 1 translation, 2 ORFs: the tab shown is kept up to date.
    Q_INVOKABLE void setDetailTab(int tab);
    Q_INVOKABLE void translateCurrent();
    Q_INVOKABLE void findOrfs();
    // Highlights an ORF in the sequence and returns the row to scroll to.
    Q_INVOKABLE int showOrf(int index);
    Q_INVOKABLE void search(const QString &text, bool motif, bool bothStrands);
    Q_INVOKABLE void clearSearch();
    Q_INVOKABLE void nextMatch();
    Q_INVOKABLE void previousMatch();
    // kind is selected, filtered, json, csv, length-svg or quality-svg.
    Q_INVOKABLE void exportAs(const QString &kind);
    Q_INVOKABLE void copyCurrent();
    Q_INVOKABLE void dismissError();
    Q_INVOKABLE void dismissNotice();

    // Writes an export straight to path; record exports finish on the worker.
    bool exportTo(const QString &kind, const QString &path);
    // For tests and headless checks: waits for the worker to finish.
    void waitForIdle();

signals:
    void fileChanged();
    void busyChanged();
    void progressChanged();
    void errorChanged();
    void noticeChanged();
    void currentChanged();
    void lineWidthChanged();
    void orfsChanged();
    void searchChanged();
    void chartColorChanged();
    // The sequence view scrolls here, after a jump to a match or an ORF.
    void jumpToLine(int row);

private:
    // Every result carries the generation it was started in; opening another
    // file starts a new one, so late results of the old file are dropped.
    struct IndexResult {
        int generation = 0;
        std::shared_ptr<FastxIndex> index;
        SummaryStats stats;
        bool ok = false;
    };
    struct SearchResult {
        int generation = 0;
        QString text;
        bool motif = false;
        int motifLength = 0;
        std::vector<SearchMatch> matches;
        bool truncated = false;
        QString error;
    };
    struct ExportResult {
        int generation = 0;
        QString path;
        StreamResult stream;
    };

    void setError(const QString &message);
    void setNotice(const QString &message);
    void setBusy(const QString &kind);
    void cancelWork();
    void indexFinished();
    void searchFinished();
    void loadRecord(qsizetype record);
    void refreshHighlights();
    void focusMatch(int index);
    ChartData chartFor(const QString &kind) const;
    QString suggestedPath(const QString &kind) const;
    QStringList sources() const;

    FilePicker *m_picker = nullptr;
    QString m_path, m_pendingExport;
    std::shared_ptr<FastxIndex> m_index;
    SummaryStats m_stats;
    QVariantMap m_summary;
    std::unique_ptr<QTemporaryFile> m_cache, m_nextCache;
    RecordModel m_records;
    LinesModel m_sequenceLines, m_translationLines;

    QString m_busyKind, m_error, m_notice, m_chartColor = "#FFD60A";
    std::atomic<int> m_progress{0};
    std::atomic<bool> m_cancel{false};
    double m_progressValue = 0;
    QTimer m_progressTimer;
    QFutureWatcher<IndexResult> m_indexWatcher;
    QFutureWatcher<SearchResult> m_searchWatcher;
    QFutureWatcher<ExportResult> m_exportWatcher;
    int m_generation = 0;

    int m_currentRow = -1;
    qsizetype m_currentRecord = -1;
    FastxRecord m_record;
    QVariantMap m_current;
    QVariantList m_qualities, m_orfs;
    QVector<Orf> m_orfList;
    bool m_reversed = false, m_orfsFound = false;
    int m_detailTab = 0;
    int m_lineWidth = 60, m_orfMinLength = 300;
    std::vector<Highlight> m_orfHighlight;

    bool m_searchActive = false, m_motifSearch = false, m_matchesTruncated = false;
    QString m_searchText;
    int m_motifLength = 0, m_matchIndex = -1;
    std::vector<SearchMatch> m_matches;
};
