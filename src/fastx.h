#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <atomic>
#include <memory>
#include <vector>

// Reading FASTA and FASTQ, plain or gzipped. zlib reads both: gzread passes
// an uncompressed file straight through, so the format is found by content,
// never by the file name.

enum class Format { Unknown, Fasta, Fastq };
enum class Alphabet { Nucleotide, Protein };

// Line by line over a file through zlib, with the uncompressed offset of
// every line, so the index can point back into the stream.
class LineReader {
public:
    LineReader();
    ~LineReader();
    LineReader(const LineReader &) = delete;
    LineReader &operator=(const LineReader &) = delete;

    bool open(const QString &path, QString *error);
    // Seeking only makes sense in an uncompressed file.
    bool seek(qint64 offset);
    // The next line without its line ending (\n or \r\n), and false at the end.
    bool readLine(QByteArray &line);
    qint64 lineStart() const { return m_lineStart; }
    bool compressed() const { return m_compressed; }
    // Bytes of the file itself consumed so far, for progress.
    qint64 fileOffset() const;
    bool failed() const { return m_failed; }
    // Every uncompressed byte read is also written here when set, so a
    // gzipped file can be read back at random from a plain copy.
    void teeTo(FILE *file) { m_tee = file; }

private:
    bool fill();

    void *m_gz = nullptr;
    QByteArray m_buffer;
    int m_begin = 0, m_end = 0;
    qint64 m_bufferStart = 0;
    qint64 m_lineStart = 0;
    bool m_eof = false, m_failed = false, m_compressed = false;
    FILE *m_tee = nullptr;
};

struct FastxRecord {
    QByteArray id, description, sequence, quality;
    qint64 offset = 0;
};

// Parses records one after another. FASTA sequences may span lines; FASTQ
// may too, with the quality read until it is as long as the sequence.
class FastxParser {
public:
    bool open(const QString &path, QString *error);
    bool openAt(const QString &path, qint64 offset, Format format, QString *error);
    bool next(FastxRecord &record);
    Format format() const { return m_format; }
    QString error() const { return m_error; }
    LineReader &reader() { return m_reader; }

private:
    bool nextLine(QByteArray &line);

    LineReader m_reader;
    Format m_format = Format::Unknown;
    QByteArray m_pending;
    bool m_hasPending = false;
    qint64 m_pendingStart = 0;
    QString m_error;
};

// One entry per record, small enough to hold millions of them.
struct IndexEntry {
    qint64 offset = 0;
    quint32 idOffset = 0;
    quint16 idLength = 0;
    quint32 length = 0;
    quint32 gc = 0, at = 0;
    float meanQuality = -1;
    float weight = 0;
};

struct SummaryStats {
    qint64 records = 0, totalLength = 0, minLength = 0, maxLength = 0, n50 = 0;
    double meanLength = 0, gcPercent = 0, meanQuality = -1;
    // Counts in bins of lengthBinWidth lengths each, from minLength.
    QVector<double> lengthHistogram;
    qint64 lengthBinWidth = 1;
    // Reads by mean Phred quality, one bin per whole score 0..maxQuality.
    QVector<double> qualityHistogram;
    // Letters A..Z over the whole file, for protein composition.
    QVector<qint64> composition;
};

class FastxIndex {
public:
    // Reads the whole file once. cachePath, when the file is gzipped, receives
    // the uncompressed stream so records can be read back at random.
    bool build(const QString &path, const QString &cachePath, std::atomic<int> *progress = nullptr,
               const std::atomic<bool> *cancel = nullptr);

    QString path() const { return m_path; }
    // The plain file records are read back from: the source, or its cache.
    QString readablePath() const { return m_readablePath; }
    QString error() const { return m_error; }
    Format format() const { return m_format; }
    Alphabet alphabet() const { return m_alphabet; }
    qsizetype size() const { return qsizetype(m_entries.size()); }
    const IndexEntry &entry(qsizetype i) const { return m_entries[size_t(i)]; }
    QByteArray id(qsizetype i) const;
    bool read(qsizetype i, FastxRecord &record) const;
    SummaryStats stats(int bins = 40) const;
    const std::vector<IndexEntry> &entries() const { return m_entries; }

private:
    QString m_path, m_readablePath, m_error;
    Format m_format = Format::Unknown;
    Alphabet m_alphabet = Alphabet::Nucleotide;
    std::vector<IndexEntry> m_entries;
    QByteArray m_ids;
    std::vector<qint64> m_composition = std::vector<qint64>(26, 0);
};

// Guesses the alphabet from letters: mostly A, C, G, T, U and N is nucleotide.
Alphabet detectAlphabet(const std::vector<qint64> &letterCounts);
qint64 n50(std::vector<quint32> lengths);
double gcPercent(qint64 gc, qint64 at);
double meanQuality(const QByteArray &quality);
// Average residue mass sum minus water per peptide bond, in daltons.
double molecularWeight(const QByteArray &protein);
void writeRecord(QByteArray &out, const FastxRecord &record, Format format, int width = 60);
QString formatName(Format format);
