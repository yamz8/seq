#include "fastx.h"

#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <zlib.h>

namespace {
constexpr int ChunkSize = 1 << 20;

gzFile gz(void *handle) { return static_cast<gzFile>(handle); }
}

LineReader::LineReader() = default;

LineReader::~LineReader() {
    if (m_gz)
        gzclose(gz(m_gz));
}

bool LineReader::open(const QString &path, QString *error) {
    if (m_gz)
        gzclose(gz(m_gz));
    m_gz = gzopen(QFile::encodeName(path).constData(), "rb");
    m_begin = m_end = 0;
    m_bufferStart = m_lineStart = 0;
    m_eof = m_failed = false;
    if (!m_gz) {
        if (error)
            *error = QString("Could not open %1: %2").arg(path, QString::fromLocal8Bit(strerror(errno)));
        return false;
    }
    gzbuffer(gz(m_gz), 256 * 1024);
    // gzdirect only knows once something has been read, so peek first.
    const int c = gzgetc(gz(m_gz));
    if (c != -1)
        gzungetc(c, gz(m_gz));
    m_compressed = !gzdirect(gz(m_gz));
    return true;
}

bool LineReader::seek(qint64 offset) {
    if (!m_gz || gzseek(gz(m_gz), z_off_t(offset), SEEK_SET) < 0)
        return false;
    m_begin = m_end = 0;
    m_bufferStart = m_lineStart = offset;
    m_eof = false;
    return true;
}

qint64 LineReader::fileOffset() const {
    return m_gz ? qint64(gzoffset(gz(m_gz))) : 0;
}

bool LineReader::fill() {
    if (m_eof)
        return false;
    // Keep the unread tail at the front, and grow when one line fills it all.
    if (m_begin > 0) {
        const int tail = m_end - m_begin;
        if (tail > 0)
            memmove(m_buffer.data(), m_buffer.constData() + m_begin, size_t(tail));
        m_bufferStart += m_begin;
        m_end = tail;
        m_begin = 0;
    }
    if (m_buffer.size() - m_end < ChunkSize)
        m_buffer.resize(m_end + ChunkSize);
    const int read = gzread(gz(m_gz), m_buffer.data() + m_end, unsigned(m_buffer.size() - m_end));
    if (read < 0) {
        m_failed = true;
        m_eof = true;
        return false;
    }
    if (read == 0) {
        m_eof = true;
        return false;
    }
    if (m_tee)
        fwrite(m_buffer.constData() + m_end, 1, size_t(read), m_tee);
    m_end += read;
    return true;
}

bool LineReader::readLine(QByteArray &line) {
    if (!m_gz)
        return false;
    int searchFrom = m_begin;
    for (;;) {
        const char *start = m_buffer.constData() + searchFrom;
        const void *newline = searchFrom < m_end ? memchr(start, '\n', size_t(m_end - searchFrom)) : nullptr;
        if (newline || (m_eof && m_begin < m_end)) {
            const int stop = newline ? int(static_cast<const char *>(newline) - m_buffer.constData()) : m_end;
            int length = stop - m_begin;
            if (length > 0 && m_buffer[m_begin + length - 1] == '\r')
                --length;
            m_lineStart = m_bufferStart + m_begin;
            line = QByteArray(m_buffer.constData() + m_begin, length);
            m_begin = newline ? stop + 1 : m_end;
            return true;
        }
        if (m_eof)
            return false;
        const int searched = m_end - m_begin;
        fill();
        searchFrom = m_begin + searched;
    }
}

bool FastxParser::open(const QString &path, QString *error) {
    m_format = Format::Unknown;
    m_hasPending = false;
    return m_reader.open(path, error);
}

bool FastxParser::openAt(const QString &path, qint64 offset, Format format, QString *error) {
    if (!open(path, error))
        return false;
    m_format = format;
    if (!m_reader.seek(offset)) {
        if (error)
            *error = QString("Could not seek in %1").arg(path);
        return false;
    }
    return true;
}

bool FastxParser::nextLine(QByteArray &line) {
    if (m_hasPending) {
        line = m_pending;
        m_hasPending = false;
        return true;
    }
    return m_reader.readLine(line);
}

namespace {
void splitHeader(const QByteArray &line, FastxRecord &record) {
    int i = 1;
    while (i < line.size() && line[i] != ' ' && line[i] != '\t')
        ++i;
    record.id = line.mid(1, i - 1);
    record.description = i < line.size() ? line.mid(i + 1).trimmed() : QByteArray();
}

// Sequence lines lose any spaces or tabs inside them, as other tools do.
void appendSequence(QByteArray &sequence, const QByteArray &line) {
    if (!line.contains(' ') && !line.contains('\t')) {
        sequence += line;
        return;
    }
    for (const char c : line)
        if (c != ' ' && c != '\t')
            sequence += c;
}
}

bool FastxParser::next(FastxRecord &record) {
    QByteArray line;
    qint64 lineStart = 0;
    for (;;) {
        const bool pending = m_hasPending;
        if (!nextLine(line)) {
            if (m_reader.failed())
                m_error = "The file could not be decompressed; it may be truncated or corrupt.";
            return false;
        }
        // A line peeked by the previous record keeps its own start.
        lineStart = pending ? m_pendingStart : m_reader.lineStart();
        if (!line.trimmed().isEmpty())
            break;
    }
    if (m_format == Format::Unknown)
        m_format = line.startsWith('>') ? Format::Fasta : line.startsWith('@') ? Format::Fastq : Format::Unknown;
    if (m_format == Format::Unknown) {
        m_error = "This is not a FASTA or FASTQ file: it should start with > or @.";
        return false;
    }
    const char marker = m_format == Format::Fasta ? '>' : '@';
    if (!line.startsWith(marker)) {
        m_error = QString("Expected a record starting with %1 at byte %2.").arg(QChar(marker)).arg(lineStart);
        return false;
    }
    record.offset = lineStart;
    splitHeader(line, record);
    record.sequence.clear();
    record.quality.clear();

    if (m_format == Format::Fasta) {
        while (m_reader.readLine(line)) {
            if (line.startsWith('>')) {
                m_pending = line;
                m_hasPending = true;
                m_pendingStart = m_reader.lineStart();
                break;
            }
            appendSequence(record.sequence, line);
        }
        if (m_reader.failed()) {
            m_error = "The file could not be decompressed; it may be truncated or corrupt.";
            return false;
        }
        return true;
    }

    bool plus = false;
    while (m_reader.readLine(line)) {
        if (line.startsWith('+')) {
            plus = true;
            break;
        }
        appendSequence(record.sequence, line);
    }
    if (!plus) {
        m_error = QString("Record %1 has no + line.").arg(QString::fromUtf8(record.id));
        return false;
    }
    // Quality may start with @ or +, so it is read by length, not by marker.
    while (record.quality.size() < record.sequence.size() && m_reader.readLine(line))
        record.quality += line.trimmed();
    if (record.quality.size() != record.sequence.size()) {
        m_error = QString("Record %1 has %2 bases but %3 quality scores.")
                      .arg(QString::fromUtf8(record.id)).arg(record.sequence.size()).arg(record.quality.size());
        return false;
    }
    return true;
}

Alphabet detectAlphabet(const std::vector<qint64> &letters) {
    qint64 total = 0;
    for (const qint64 n : letters)
        total += n;
    if (total == 0)
        return Alphabet::Nucleotide;
    const qint64 nucleotide = letters['A' - 'A'] + letters['C' - 'A'] + letters['G' - 'A'] + letters['T' - 'A'] +
                              letters['U' - 'A'] + letters['N' - 'A'];
    return nucleotide >= total * 9 / 10 ? Alphabet::Nucleotide : Alphabet::Protein;
}

qint64 n50(std::vector<quint32> lengths) {
    qint64 total = 0;
    for (const quint32 length : lengths)
        total += length;
    std::sort(lengths.begin(), lengths.end(), std::greater<>());
    qint64 sum = 0;
    for (const quint32 length : lengths) {
        sum += length;
        if (sum * 2 >= total)
            return length;
    }
    return 0;
}

double gcPercent(qint64 gc, qint64 at) {
    return gc + at > 0 ? 100.0 * double(gc) / double(gc + at) : 0;
}

double meanQuality(const QByteArray &quality) {
    if (quality.isEmpty())
        return -1;
    qint64 sum = 0;
    for (const char c : quality)
        sum += qMax(0, int(uchar(c)) - 33);
    return double(sum) / double(quality.size());
}

namespace {
// Average residue masses (the amino acid less one water), in daltons.
struct Masses {
    double table[256] = {};
    Masses() {
        const std::pair<char, double> residues[] = {
            {'A', 71.0788},  {'R', 156.1875}, {'N', 114.1038}, {'D', 115.0886}, {'C', 103.1388},
            {'E', 129.1155}, {'Q', 128.1307}, {'G', 57.0519},  {'H', 137.1411}, {'I', 113.1594},
            {'L', 113.1594}, {'K', 128.1741}, {'M', 131.1926}, {'F', 147.1766}, {'P', 97.1167},
            {'S', 87.0782},  {'T', 101.1051}, {'W', 186.2132}, {'Y', 163.1760}, {'V', 99.1326},
            {'U', 150.0388}, {'O', 237.3018}, {'B', 114.5962}, {'Z', 128.6231}, {'J', 113.1594},
            {'X', 110.0},
        };
        for (const auto &[letter, mass] : residues) {
            table[uchar(letter)] = mass;
            table[uchar(letter + 32)] = mass;
        }
    }
};
const Masses masses;
constexpr double Water = 18.01528;
}

double molecularWeight(const QByteArray &protein) {
    double sum = 0;
    for (const char c : protein)
        sum += masses.table[uchar(c)];
    return sum > 0 ? sum + Water : 0;
}

bool FastxIndex::build(const QString &path, const QString &cachePath, std::atomic<int> *progress,
                       const std::atomic<bool> *cancel) {
    m_path = path;
    m_readablePath = path;
    m_entries.clear();
    m_ids.clear();
    m_composition.assign(26, 0);
    m_error.clear();

    FastxParser parser;
    if (!parser.open(path, &m_error))
        return false;
    FILE *cache = nullptr;
    if (parser.reader().compressed()) {
        if (cachePath.isEmpty()) {
            // Without a cache records can't be read back, but the index and
            // its statistics are still complete.
            m_readablePath.clear();
        } else {
            cache = fopen(QFile::encodeName(cachePath).constData(), "wb");
            if (!cache) {
                m_error = QString("Could not write the decompressed copy to %1.").arg(cachePath);
                return false;
            }
            parser.reader().teeTo(cache);
            m_readablePath = cachePath;
        }
    }
    const double fileSize = qMax<qint64>(1, QFileInfo(path).size());

    FastxRecord record;
    qint64 count = 0;
    while (parser.next(record)) {
        IndexEntry entry;
        entry.offset = record.offset;
        entry.idOffset = quint32(m_ids.size());
        entry.idLength = quint16(qMin<qsizetype>(record.id.size(), 0xffff));
        m_ids.append(record.id.constData(), entry.idLength);
        entry.length = quint32(record.sequence.size());
        quint32 gc = 0, at = 0;
        double weight = 0;
        for (const char c : record.sequence) {
            const uchar upper = uchar(c) & 0xdf;
            if (upper >= 'A' && upper <= 'Z')
                ++m_composition[upper - 'A'];
            gc += upper == 'G' || upper == 'C';
            at += upper == 'A' || upper == 'T' || upper == 'U';
            weight += masses.table[uchar(c)];
        }
        entry.gc = gc;
        entry.at = at;
        entry.weight = float(weight > 0 ? weight + Water : 0);
        entry.meanQuality = float(meanQuality(record.quality));
        m_entries.push_back(entry);
        if ((++count & 0xfff) == 0) {
            if (cancel && cancel->load()) {
                m_error = "Cancelled.";
                break;
            }
            if (progress)
                progress->store(int(1000.0 * double(parser.reader().fileOffset()) / fileSize));
        }
    }
    if (cache)
        fclose(cache);
    if (m_error.isEmpty())
        m_error = parser.error();
    m_format = parser.format();
    if (m_error.isEmpty() && m_entries.empty())
        m_error = "There are no records in this file.";
    // FASTQ is always reads; FASTA is whatever its letters say.
    m_alphabet = m_format == Format::Fastq ? Alphabet::Nucleotide : detectAlphabet(m_composition);
    if (progress)
        progress->store(1000);
    return m_error.isEmpty();
}

QByteArray FastxIndex::id(qsizetype i) const {
    const IndexEntry &e = m_entries[size_t(i)];
    return QByteArray::fromRawData(m_ids.constData() + e.idOffset, e.idLength);
}

bool FastxIndex::read(qsizetype i, FastxRecord &record) const {
    if (i < 0 || i >= size() || m_readablePath.isEmpty())
        return false;
    FastxParser parser;
    return parser.openAt(m_readablePath, m_entries[size_t(i)].offset, m_format, nullptr) && parser.next(record);
}

SummaryStats FastxIndex::stats(int bins) const {
    SummaryStats s;
    s.records = size();
    s.composition = QVector<qint64>(m_composition.begin(), m_composition.end());
    if (m_entries.empty())
        return s;
    std::vector<quint32> lengths;
    lengths.reserve(m_entries.size());
    qint64 gc = 0, at = 0, minLength = LLONG_MAX, maxLength = 0;
    double qualitySum = 0, qualityBases = 0;
    int maxQuality = 41;
    for (const IndexEntry &e : m_entries) {
        lengths.push_back(e.length);
        s.totalLength += e.length;
        minLength = qMin<qint64>(minLength, e.length);
        maxLength = qMax<qint64>(maxLength, e.length);
        gc += e.gc;
        at += e.at;
        if (e.meanQuality >= 0) {
            qualitySum += double(e.meanQuality) * e.length;
            qualityBases += e.length;
            maxQuality = qMax(maxQuality, int(e.meanQuality));
        }
    }
    s.minLength = minLength;
    s.maxLength = maxLength;
    s.meanLength = double(s.totalLength) / double(s.records);
    s.n50 = n50(std::move(lengths));
    s.gcPercent = gcPercent(gc, at);

    // Every bin covers the same whole number of lengths, so no bin is short
    // just because fewer integers fell into it.
    const qint64 span = maxLength - minLength + 1;
    const qint64 binWidth = (span + bins - 1) / bins;
    const int lengthBins = int((span + binWidth - 1) / binWidth);
    s.lengthHistogram = QVector<double>(lengthBins, 0);
    for (const IndexEntry &e : m_entries)
        s.lengthHistogram[int((e.length - minLength) / binWidth)] += 1;
    s.lengthBinWidth = binWidth;

    if (m_format == Format::Fastq) {
        s.meanQuality = qualityBases > 0 ? qualitySum / qualityBases : 0;
        s.qualityHistogram = QVector<double>(maxQuality + 1, 0);
        for (const IndexEntry &e : m_entries)
            if (e.meanQuality >= 0)
                s.qualityHistogram[qMin(maxQuality, int(e.meanQuality))] += 1;
    }
    return s;
}

void writeRecord(QByteArray &out, const FastxRecord &record, Format format, int width) {
    const bool fastq = format == Format::Fastq && record.quality.size() == record.sequence.size();
    out += fastq ? '@' : '>';
    out += record.id;
    if (!record.description.isEmpty()) {
        out += ' ';
        out += record.description;
    }
    out += '\n';
    if (fastq) {
        out += record.sequence;
        out += "\n+\n";
        out += record.quality;
        out += '\n';
        return;
    }
    for (qsizetype i = 0; i < record.sequence.size(); i += width) {
        out += record.sequence.mid(i, width);
        out += '\n';
    }
}

QString formatName(Format format) {
    return format == Format::Fasta ? "FASTA" : format == Format::Fastq ? "FASTQ" : "unknown";
}
