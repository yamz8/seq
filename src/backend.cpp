#include "backend.h"

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QtConcurrentRun>
#include <algorithm>
#include <numeric>

#include "filepicker.h"
#include "portalfilepicker.h"
#include "seqtools.h"

namespace {
constexpr qsizetype MaxMatches = 200000;
constexpr int MaxOrfs = 2000;
constexpr int MaxQualityBars = 4000;
constexpr int MaxMotif = 1000;

QVariantList toList(const QVector<double> &values) {
    QVariantList list;
    list.reserve(values.size());
    for (const double v : values)
        list << v;
    return list;
}
}

Backend::Backend(FilePicker *picker, QObject *parent)
    : QObject(parent), m_picker(picker ? picker : new PortalFilePicker(this)) {
    connect(m_picker, &FilePicker::openSelected, this, &Backend::openUrl);
    connect(m_picker, &FilePicker::failed, this, &Backend::setError);
    connect(m_picker, &FilePicker::saveSelected, this, [this](const QUrl &url) {
        const QString kind = std::exchange(m_pendingExport, QString());
        if (!kind.isEmpty())
            exportTo(kind, url.toLocalFile());
    });
    m_progressTimer.setInterval(100);
    connect(&m_progressTimer, &QTimer::timeout, this, [this] {
        m_progressValue = m_progress.load() / 1000.0;
        emit progressChanged();
    });
    connect(&m_indexWatcher, &QFutureWatcher<IndexResult>::finished, this, &Backend::indexFinished);
    connect(&m_searchWatcher, &QFutureWatcher<SearchResult>::finished, this, &Backend::searchFinished);
    connect(&m_exportWatcher, &QFutureWatcher<ExportResult>::finished, this, [this] {
        const ExportResult result = m_exportWatcher.result();
        if (result.generation != m_generation)
            return;
        setBusy({});
        if (!result.stream.error.isEmpty())
            setError(result.stream.error);
        else
            setNotice(tr("Exported %n records to %1", "", int(result.stream.written)).arg(result.path));
    });
}

Backend::~Backend() {
    cancelWork();
}

QString Backend::fileName() const {
    return QFileInfo(m_path).fileName();
}

QString Backend::format() const {
    return m_index ? formatName(m_index->format()) : QString();
}

bool Backend::compressed() const {
    return m_index && m_index->readablePath() != m_index->path();
}

void Backend::setError(const QString &message) {
    if (message == m_error)
        return;
    m_error = message;
    emit errorChanged();
}

void Backend::setNotice(const QString &message) {
    m_notice = message;
    emit noticeChanged();
}

void Backend::dismissError() { setError({}); }
void Backend::dismissNotice() { setNotice({}); }

void Backend::setBusy(const QString &kind) {
    m_busyKind = kind;
    m_progress = 0;
    m_progressValue = 0;
    if (kind.isEmpty())
        m_progressTimer.stop();
    else
        m_progressTimer.start();
    emit busyChanged();
    emit progressChanged();
}

void Backend::cancelWork() {
    m_cancel = true;
    m_indexWatcher.waitForFinished();
    m_searchWatcher.waitForFinished();
    m_exportWatcher.waitForFinished();
    m_cancel = false;
}

void Backend::waitForIdle() {
    while (busy()) {
        m_indexWatcher.waitForFinished();
        m_searchWatcher.waitForFinished();
        m_exportWatcher.waitForFinished();
        QCoreApplication::processEvents();
    }
}

void Backend::open() {
    m_picker->openSequences();
}

void Backend::openUrl(const QUrl &url) {
    openPath(url.isLocalFile() ? url.toLocalFile() : url.toString());
}

void Backend::openPath(const QString &path) {
    if (!QFileInfo(path).isFile()) {
        setError(tr("%1 is not a file.").arg(path));
        return;
    }
    // Opening another file abandons whatever the last one was doing: the
    // worker stops, and a result it still delivers belongs to an older
    // generation and is dropped.
    ++m_generation;
    if (busy()) {
        cancelWork();
        setBusy({});
    }
    setError({});
    setNotice({});

    // A gzipped file is decompressed once into the cache while it is indexed,
    // so records can be read back at random without touching the original.
    const QString cacheDirectory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cacheDirectory);
    m_nextCache = std::make_unique<QTemporaryFile>(cacheDirectory + "/index-XXXXXX.fastx");
    QString cachePath;
    if (m_nextCache->open()) {
        cachePath = m_nextCache->fileName();
        m_nextCache->close();
    }
    const QString absolute = QFileInfo(path).absoluteFilePath();
    m_pendingExport.clear();
    setBusy("index");
    const int generation = m_generation;
    m_indexWatcher.setFuture(QtConcurrent::run([this, absolute, cachePath, generation] {
        IndexResult result;
        result.generation = generation;
        result.index = std::make_shared<FastxIndex>();
        result.ok = result.index->build(absolute, cachePath, &m_progress, &m_cancel);
        if (result.ok)
            result.stats = result.index->stats();
        return result;
    }));
}

void Backend::indexFinished() {
    const IndexResult result = m_indexWatcher.result();
    if (result.generation != m_generation)
        return;
    setBusy({});
    if (!result.ok) {
        m_nextCache.reset();
        setError(tr("Could not open %1: %2").arg(QFileInfo(result.index->path()).fileName(), result.index->error()));
        return;
    }
    m_cache = std::move(m_nextCache);
    m_index = result.index;
    m_path = m_index->path();
    m_stats = result.stats;

    const SummaryStats &s = m_stats;
    QVariantList composition;
    qint64 letters = 0;
    for (const qint64 n : s.composition)
        letters += n;
    QVector<int> order(26);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return s.composition[a] > s.composition[b]; });
    for (const int i : order)
        if (s.composition[i] > 0)
            composition << QVariantMap{{"letter", QString(QChar('A' + i))},
                                       {"count", s.composition[i]},
                                       {"percent", 100.0 * double(s.composition[i]) / double(qMax<qint64>(1, letters))}};
    m_summary = QVariantMap{
        {"records", s.records}, {"totalLength", s.totalLength}, {"minLength", s.minLength},
        {"maxLength", s.maxLength}, {"meanLength", s.meanLength}, {"n50", s.n50},
        {"gcPercent", s.gcPercent}, {"meanQuality", s.meanQuality},
        {"lengthHistogram", toList(s.lengthHistogram)}, {"qualityHistogram", toList(s.qualityHistogram)},
        {"qualityMax", qMax(0, int(s.qualityHistogram.size()) - 1)}, {"composition", composition},
    };

    m_searchActive = false;
    m_motifSearch = false;
    m_searchText.clear();
    m_matches.clear();
    m_matchIndex = -1;
    m_matchesTruncated = false;
    m_records.reset(m_index);
    m_currentRow = -1;
    m_currentRecord = -1;
    m_reversed = false;
    emit fileChanged();
    emit searchChanged();
    select(0);
}

void Backend::select(int row) {
    if (!m_index || m_records.rowCount() == 0)
        return;
    row = qBound(0, row, m_records.rowCount() - 1);
    const qsizetype record = m_records.recordAt(row);
    m_currentRow = row;
    if (record != m_currentRecord) {
        loadRecord(record);
        // N continues from the first match in the record the user moved to.
        const auto it = std::lower_bound(m_matches.begin(), m_matches.end(), record,
                                         [](const SearchMatch &m, qsizetype r) { return m.record < r; });
        if (it != m_matches.end() && it->record == record) {
            m_matchIndex = int(it - m_matches.begin());
            emit searchChanged();
        }
        refreshHighlights();
    }
    emit currentChanged();
}

void Backend::move(int delta) {
    select(m_currentRow < 0 ? 0 : m_currentRow + delta);
}

void Backend::loadRecord(qsizetype record) {
    m_currentRecord = record;
    m_record = FastxRecord();
    if (!m_index->read(record, m_record))
        setError(tr("Could not read record %1 back from the file.").arg(record + 1));
    const IndexEntry &e = m_index->entry(record);
    m_current = QVariantMap{
        {"id", QString::fromUtf8(m_record.id)},
        {"description", QString::fromUtf8(m_record.description)},
        {"length", qint64(e.length)},
        {"gc", gcPercent(e.gc, e.at)},
        {"weight", double(e.weight)},
        {"quality", double(e.meanQuality)},
        {"number", qint64(record + 1)},
    };
    m_qualities.clear();
    if (!m_record.quality.isEmpty()) {
        // Long reads get one bar per bucket of bases, averaged.
        const qsizetype n = m_record.quality.size();
        const qsizetype bars = qMin<qsizetype>(n, MaxQualityBars);
        for (qsizetype b = 0; b < bars; ++b) {
            const qsizetype from = b * n / bars, to = qMax(from + 1, (b + 1) * n / bars);
            double sum = 0;
            for (qsizetype i = from; i < to; ++i)
                sum += qMax(0, int(uchar(m_record.quality[i])) - 33);
            m_qualities << sum / double(to - from);
        }
    }
    m_reversed = false;
    m_orfHighlight.clear();
    m_sequenceLines.setSegments({{QString(), m_record.sequence, {}}}, m_lineWidth);
    m_translationLines.setSegments({}, m_lineWidth);
    m_orfList.clear();
    m_orfs.clear();
    m_orfsFound = false;
    // The tab on screen follows the record; the others wait until shown.
    if (m_detailTab == 1)
        translateCurrent();
    if (m_detailTab == 2)
        findOrfs();
    else
        emit orfsChanged();
}

void Backend::setDetailTab(int tab) {
    m_detailTab = tab;
    if (tab == 1)
        translateCurrent();
    if (tab == 2 && !m_orfsFound)
        findOrfs();
}

// Matches and the shown ORF, mapped onto the strand on screen.
void Backend::refreshHighlights() {
    std::vector<Highlight> highlights;
    const qint64 n = m_record.sequence.size();
    auto add = [&](qint64 start, int length, bool current) {
        highlights.push_back({m_reversed ? n - start - length : start, length, current});
    };
    if (m_motifSearch) {
        const auto [first, last] = std::equal_range(
            m_matches.begin(), m_matches.end(), SearchMatch{m_currentRecord},
            [](const SearchMatch &a, const SearchMatch &b) { return a.record < b.record; });
        for (auto it = first; it != last; ++it)
            add(it->position, m_motifLength, int(it - m_matches.begin()) == m_matchIndex);
    }
    for (const Highlight &h : m_orfHighlight)
        add(h.start, h.length, true);
    std::sort(highlights.begin(), highlights.end(), [](const Highlight &a, const Highlight &b) { return a.start < b.start; });
    m_sequenceLines.setHighlights(0, std::move(highlights));
}

void Backend::toggleReverse() {
    if (!m_index || m_currentRecord < 0 || protein())
        return;
    m_reversed = !m_reversed;
    m_sequenceLines.setSegments({{QString(), m_reversed ? reverseComplement(m_record.sequence) : m_record.sequence, {}}},
                                m_lineWidth);
    refreshHighlights();
    emit currentChanged();
}

void Backend::toggleLineWidth() {
    m_lineWidth = m_lineWidth == 60 ? 80 : 60;
    m_sequenceLines.setWidth(m_lineWidth);
    m_translationLines.setWidth(m_lineWidth);
    emit lineWidthChanged();
}

void Backend::translateCurrent() {
    if (m_currentRecord < 0 || protein() || m_translationLines.rowCount() > 0)
        return;
    std::vector<LinesModel::Segment> frames;
    for (const int frame : {1, 2, 3, -1, -2, -3})
        frames.push_back({(frame > 0 ? "+" : "−") + QString::number(qAbs(frame)), translate(m_record.sequence, frame), {}});
    m_translationLines.setSegments(std::move(frames), m_lineWidth);
}

void Backend::setOrfMinLength(int length) {
    length = qBound(3, length, 1000000);
    if (length == m_orfMinLength)
        return;
    m_orfMinLength = length;
    // Search again whenever there was a search, even one that found nothing.
    if (m_orfsFound)
        findOrfs();
    else
        emit orfsChanged();
}

void Backend::findOrfs() {
    if (m_currentRecord < 0 || protein())
        return;
    m_orfList = ::findOrfs(m_record.sequence, m_orfMinLength);
    m_orfsFound = true;
    m_orfs.clear();
    for (int i = 0; i < qMin(int(m_orfList.size()), MaxOrfs); ++i) {
        const Orf &o = m_orfList[i];
        m_orfs << QVariantMap{{"frame", (o.frame > 0 ? "+" : "−") + QString::number(qAbs(o.frame))},
                              {"start", o.start}, {"end", o.end}, {"length", o.length()},
                              {"residues", o.protein.size()},
                              {"protein", QString::fromLatin1(o.protein.left(80)) + (o.protein.size() > 80 ? "…" : "")}};
    }
    emit orfsChanged();
}

int Backend::showOrf(int index) {
    if (index < 0 || index >= m_orfList.size())
        return 0;
    const Orf &o = m_orfList[index];
    if (m_reversed)
        toggleReverse();
    m_orfHighlight = {{o.start - 1, int(o.length()), true}};
    refreshHighlights();
    const int row = m_sequenceLines.rowFor(0, o.start - 1);
    emit jumpToLine(row);
    return row;
}

void Backend::search(const QString &text, bool motif, bool bothStrands) {
    if (!m_index || busy())
        return;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        clearSearch();
        return;
    }
    Motif compiled;
    QString error;
    if (motif && (trimmed.size() > MaxMotif || !compiled.compile(trimmed.toUtf8(), m_index->alphabet(), &error))) {
        setError(error.isEmpty() ? tr("A motif can be at most %1 letters.").arg(MaxMotif) : error);
        return;
    }
    setError({});
    setBusy("search");
    const auto index = m_index;
    const bool both = bothStrands && index->alphabet() == Alphabet::Nucleotide;
    const int generation = m_generation;
    m_searchWatcher.setFuture(QtConcurrent::run([this, index, trimmed, motif, compiled, both, generation] {
        // The current search stays in effect until this one finishes.
        SearchResult result;
        result.generation = generation;
        result.text = trimmed;
        result.motif = motif;
        result.motifLength = compiled.length();
        if (!motif) {
            const QByteArray needle = trimmed.toUtf8().toLower();
            for (qsizetype i = 0; i < index->size() && !m_cancel; ++i) {
                if (index->id(i).toLower().contains(needle))
                    result.matches.push_back({i, -1, '+'});
                if ((i & 0xffff) == 0)
                    m_progress = int(1000 * i / qMax<qsizetype>(1, index->size()));
            }
            return result;
        }
        FastxParser parser;
        if (!parser.open(index->readablePath(), &result.error))
            return result;
        FastxRecord record;
        qsizetype i = 0;
        while (!result.truncated && !m_cancel && parser.next(record)) {
            compiled.search(record.sequence, both, [&](qint64 position, char strand) {
                if (qsizetype(result.matches.size()) >= MaxMatches) {
                    result.truncated = true;
                    return false;
                }
                result.matches.push_back({i, position, strand});
                return true;
            });
            if ((++i & 0xfff) == 0)
                m_progress = int(1000 * i / qMax<qsizetype>(1, index->size()));
        }
        // Matches on both strands come out by position; keep that order.
        return result;
    }));
}

void Backend::searchFinished() {
    SearchResult result = m_searchWatcher.result();
    if (result.generation != m_generation)
        return;
    setBusy({});
    if (!result.error.isEmpty()) {
        setError(result.error);
        return;
    }
    m_searchText = result.text;
    m_motifSearch = result.motif;
    m_motifLength = result.motifLength;
    m_matches = std::move(result.matches);
    m_matchesTruncated = result.truncated;
    m_searchActive = true;
    std::vector<qsizetype> rows;
    for (const SearchMatch &m : m_matches)
        if (rows.empty() || rows.back() != m.record)
            rows.push_back(m.record);
    m_records.setFilter(std::move(rows), true);
    m_currentRecord = -1;
    m_matchIndex = -1;
    emit searchChanged();
    if (m_matches.empty()) {
        m_currentRow = -1;
        m_record = FastxRecord();
        m_current.clear();
        m_qualities.clear();
        m_sequenceLines.setSegments({}, m_lineWidth);
        m_translationLines.setSegments({}, m_lineWidth);
        emit currentChanged();
        return;
    }
    focusMatch(0);
}

void Backend::clearSearch() {
    if (!m_searchActive)
        return;
    const qsizetype record = m_currentRecord;
    m_searchActive = false;
    m_motifSearch = false;
    m_searchText.clear();
    m_matches.clear();
    m_matchIndex = -1;
    m_matchesTruncated = false;
    m_records.setFilter({}, false);
    emit searchChanged();
    m_currentRecord = -1;
    select(record < 0 ? 0 : int(record));
}

void Backend::focusMatch(int index) {
    if (m_matches.empty())
        return;
    m_matchIndex = (index % matchCount() + matchCount()) % matchCount();
    const SearchMatch &m = m_matches[size_t(m_matchIndex)];
    const int keep = m_matchIndex;
    select(m_records.rowOf(m.record));
    m_matchIndex = keep;
    refreshHighlights();
    emit searchChanged();
    if (m.position >= 0) {
        const qint64 n = m_record.sequence.size();
        emit jumpToLine(m_sequenceLines.rowFor(0, m_reversed ? n - m.position - m_motifLength : m.position));
    }
}

void Backend::nextMatch() { focusMatch(m_matchIndex + 1); }
void Backend::previousMatch() { focusMatch(m_matchIndex - 1); }

QStringList Backend::sources() const {
    return m_index ? QStringList{m_index->path(), m_index->readablePath()} : QStringList{};
}

QString Backend::suggestedPath(const QString &kind) const {
    const QFileInfo source(m_path);
    QString base = source.fileName();
    if (base.endsWith(".gz"))
        base.chop(3);
    base = QFileInfo(base).completeBaseName();
    const QString extension = fastq() ? "fastq" : protein() ? "faa" : "fa";
    QString name = kind == "selected"      ? QString(m_current.value("id").toString()).replace('/', '_') + "." + extension
                   : kind == "filtered"    ? base + (m_searchActive ? ".matches." : ".copy.") + extension
                   : kind == "json"        ? base + ".summary.json"
                   : kind == "csv"         ? base + ".summary.csv"
                   : kind == "length-svg"  ? base + ".lengths.svg"
                                           : base + ".quality.svg";
    return source.absoluteDir().filePath(name);
}

ChartData Backend::chartFor(const QString &kind) const {
    ChartData chart;
    chart.bar = QColor(m_chartColor);
    chart.background = QColor("#16161a");
    if (kind == "length-svg") {
        chart.values = m_stats.lengthHistogram;
        chart.xMin = double(m_stats.minLength);
        chart.xMax = double(m_stats.maxLength);
        chart.title = tr("Sequence lengths");
        chart.xLabel = tr("Length");
    } else {
        chart.values = m_stats.qualityHistogram;
        chart.xMin = 0;
        chart.xMax = qMax(0, int(m_stats.qualityHistogram.size()) - 1);
        chart.title = tr("Reads by mean quality");
        chart.xLabel = tr("Mean Phred quality");
        chart.lowThreshold = 20;
        chart.midThreshold = 30;
        chart.thresholdsOnX = true;
    }
    return chart;
}

void Backend::exportAs(const QString &kind) {
    if (!m_index || busy())
        return;
    if ((kind == "selected" && m_currentRecord < 0) || (kind == "quality-svg" && !fastq()))
        return;
    m_pendingExport = kind;
    const QString path = suggestedPath(kind);
    const QString suffix = QFileInfo(path).suffix();
    const QString filterName = kind == "json" ? "JSON" : kind == "csv" ? "CSV" : kind.endsWith("svg") ? "SVG" : format();
    QStringList patterns{"*." + suffix};
    if (kind == "selected" || kind == "filtered")
        patterns << "*." + suffix + ".gz";
    m_picker->save(QUrl::fromLocalFile(path), filterName, patterns);
}

bool Backend::exportTo(const QString &kind, const QString &path) {
    if (!m_index || path.isEmpty())
        return false;
    QString error;
    if (sameFile(path, sources())) {
        setError(tr("Refusing to overwrite the file being read; choose a new file."));
        return false;
    }
    if (kind == "json" || kind == "csv") {
        const QByteArray data = kind == "json" ? QJsonDocument(summaryJson(*m_index, m_stats)).toJson()
                                               : summaryCsv(*m_index, m_stats);
        if (!writeFile(path, data, sources(), &error)) {
            setError(error);
            return false;
        }
        setNotice(tr("Saved the summary to %1").arg(path));
        return true;
    }
    if (kind == "length-svg" || kind == "quality-svg") {
        if (!writeChartSvg(path, chartFor(kind), {720, 360}, &error)) {
            setError(error);
            return false;
        }
        setNotice(tr("Saved the chart to %1").arg(path));
        return true;
    }
    if (kind == "selected") {
        QByteArray data;
        writeRecord(data, m_record, m_index->format());
        if (!writeFile(path, data, sources(), &error)) {
            setError(error);
            return false;
        }
        setNotice(tr("Exported %n records to %1", "", 1).arg(path));
        return true;
    }
    if (kind != "filtered" || busy())
        return false;
    // Streams the readable copy in order, keeping the rows the list shows.
    const std::vector<qsizetype> filter = m_records.filter();
    const bool filtered = m_records.filtered();
    const QString input = m_index->readablePath();
    const QStringList protectedPaths = sources();
    setBusy("export");
    const qint64 total = m_index->size();
    const int generation = m_generation;
    m_exportWatcher.setFuture(QtConcurrent::run([this, filter, filtered, input, path, protectedPaths, total, generation] {
        size_t next = 0;
        ExportResult result;
        result.generation = generation;
        result.path = path;
        result.stream = streamRecords(input, path, protectedPaths, [&](qint64 index, FastxRecord &) {
            if ((index & 0xfff) == 0)
                m_progress = int(1000 * index / qMax<qint64>(1, total));
            if (!filtered)
                return true;
            while (next < filter.size() && filter[next] < index)
                ++next;
            return next < filter.size() && filter[next] == index;
        }, &m_cancel);
        return result;
    }));
    return true;
}

void Backend::copyCurrent() {
    if (m_currentRecord < 0)
        return;
    QByteArray data;
    FastxRecord shown = m_record;
    if (m_reversed) {
        shown.sequence = reverseComplement(shown.sequence);
        std::reverse(shown.quality.begin(), shown.quality.end());
    }
    writeRecord(data, shown, m_index->format());
    QGuiApplication::clipboard()->setText(QString::fromUtf8(data));
    setNotice(tr("Copied %1").arg(QString::fromUtf8(m_record.id)));
}
