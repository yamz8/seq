#include "exporter.h"

#include <QFileInfo>
#include <QJsonArray>
#include <zlib.h>

struct AtomicWriter::Deflater {
    z_stream stream{};
};

AtomicWriter::AtomicWriter(const QString &path) : m_file(path) {}

AtomicWriter::~AtomicWriter() {
    if (m_gzip)
        deflateEnd(&m_gzip->stream);
}

bool AtomicWriter::open(QString *error) {
    if (!m_file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QString("Could not write %1: %2").arg(m_file.fileName(), m_file.errorString());
        return false;
    }
    if (m_file.fileName().endsWith(".gz", Qt::CaseInsensitive)) {
        m_gzip = std::make_unique<Deflater>();
        // 15 window bits plus 16 asks zlib for a gzip header.
        if (deflateInit2(&m_gzip->stream, 6, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
            m_gzip.reset();
            if (error)
                *error = "Could not start gzip compression.";
            return false;
        }
    }
    return true;
}

bool AtomicWriter::deflateOut(const char *data, qsizetype size, int flush) {
    z_stream &z = m_gzip->stream;
    z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data));
    z.avail_in = uInt(size);
    char out[65536];
    do {
        z.next_out = reinterpret_cast<Bytef *>(out);
        z.avail_out = sizeof out;
        if (deflate(&z, flush) == Z_STREAM_ERROR)
            return false;
        const qsizetype produced = qsizetype(sizeof out) - z.avail_out;
        if (produced && m_file.write(out, produced) != produced)
            return false;
    } while (z.avail_out == 0);
    return true;
}

bool AtomicWriter::write(const QByteArray &data) {
    if (m_failed)
        return false;
    const bool ok = m_gzip ? deflateOut(data.constData(), data.size(), Z_NO_FLUSH)
                           : m_file.write(data) == data.size();
    m_failed = !ok;
    return ok;
}

bool AtomicWriter::commit(QString *error) {
    if (!m_failed && m_gzip)
        m_failed = !deflateOut(nullptr, 0, Z_FINISH);
    if (m_failed) {
        m_file.cancelWriting();
        m_file.commit();
        if (error)
            *error = QString("Could not write %1: %2").arg(m_file.fileName(), m_file.errorString());
        return false;
    }
    if (!m_file.commit()) {
        if (error)
            *error = QString("Could not save %1: %2").arg(m_file.fileName(), m_file.errorString());
        return false;
    }
    return true;
}

bool sameFile(const QString &output, const QStringList &inputs) {
    const QFileInfo out(output);
    for (const QString &input : inputs) {
        if (input.isEmpty())
            continue;
        const QFileInfo in(input);
        if (out.absoluteFilePath() == in.absoluteFilePath())
            return true;
        if (out.exists() && !in.canonicalFilePath().isEmpty() && out.canonicalFilePath() == in.canonicalFilePath())
            return true;
    }
    return false;
}

StreamResult streamRecords(const QString &input, const QString &output, const QStringList &sources,
                           const std::function<bool(qint64, FastxRecord &)> &keep) {
    StreamResult result;
    if (sameFile(output, sources + QStringList{input})) {
        result.error = "Refusing to overwrite the file being read; choose a new file.";
        return result;
    }
    FastxParser parser;
    if (!parser.open(input, &result.error))
        return result;
    AtomicWriter writer(output);
    if (!writer.open(&result.error))
        return result;
    FastxRecord record;
    QByteArray buffer;
    while (parser.next(record)) {
        if (keep(result.read++, record)) {
            writeRecord(buffer, record, parser.format());
            ++result.written;
        }
        if (buffer.size() > (1 << 20)) {
            writer.write(buffer);
            buffer.clear();
        }
    }
    if (!parser.error().isEmpty()) {
        result.error = parser.error();
        return result;
    }
    writer.write(buffer);
    writer.commit(&result.error);
    return result;
}

QJsonObject summaryJson(const FastxIndex &index, const SummaryStats &s) {
    QJsonObject o{
        {"file", index.path()},
        {"format", formatName(index.format()).toLower()},
        {"alphabet", index.alphabet() == Alphabet::Protein ? "protein" : "nucleotide"},
        {"records", s.records},
        {"total_length", s.totalLength},
        {"min_length", s.minLength},
        {"max_length", s.maxLength},
        {"mean_length", s.meanLength},
        {"n50", s.n50},
    };
    if (index.alphabet() == Alphabet::Nucleotide)
        o.insert("gc_percent", s.gcPercent);
    if (s.meanQuality >= 0)
        o.insert("mean_quality", s.meanQuality);
    QJsonArray lengths;
    for (const double v : s.lengthHistogram)
        lengths.append(qint64(v));
    o.insert("length_histogram", QJsonObject{{"min", s.minLength}, {"bin_width", s.lengthBinWidth}, {"counts", lengths}});
    if (!s.qualityHistogram.isEmpty()) {
        QJsonArray quality;
        for (const double v : s.qualityHistogram)
            quality.append(qint64(v));
        o.insert("quality_histogram", quality);
    }
    if (index.alphabet() == Alphabet::Protein) {
        QJsonObject composition;
        for (int i = 0; i < 26; ++i)
            if (s.composition.value(i))
                composition.insert(QString(QChar('A' + i)), s.composition[i]);
        o.insert("composition", composition);
    }
    return o;
}

QByteArray summaryCsv(const FastxIndex &index, const SummaryStats &s) {
    const QJsonObject json = summaryJson(index, s);
    QByteArray csv = "key,value\n";
    // Scalars only; the histograms follow as their own rows.
    for (const char *key : {"file", "format", "alphabet", "records", "total_length", "min_length", "max_length",
                            "mean_length", "n50", "gc_percent", "mean_quality"}) {
        if (!json.contains(key))
            continue;
        const QJsonValue v = json.value(key);
        QString text = v.isString() ? v.toString() : QString::number(v.toDouble(), 'g', 12);
        if (text.contains(',') || text.contains('"'))
            text = '"' + text.replace('"', "\"\"") + '"';
        csv += QByteArray(key) + ',' + text.toUtf8() + '\n';
    }
    for (int i = 0; i < s.lengthHistogram.size(); ++i)
        csv += QString("length_bin_%1_%2,%3\n")
                   .arg(s.minLength + i * s.lengthBinWidth).arg(s.minLength + (i + 1) * s.lengthBinWidth - 1)
                   .arg(qint64(s.lengthHistogram[i])).toUtf8();
    for (int q = 0; q < s.qualityHistogram.size(); ++q)
        csv += QString("reads_mean_q%1,%2\n").arg(q).arg(qint64(s.qualityHistogram[q])).toUtf8();
    if (index.alphabet() == Alphabet::Protein)
        for (int i = 0; i < 26; ++i)
            if (s.composition.value(i))
                csv += QString("residue_%1,%2\n").arg(QChar('A' + i)).arg(s.composition[i]).toUtf8();
    return csv;
}

bool writeFile(const QString &path, const QByteArray &data, const QStringList &sources, QString *error) {
    if (sameFile(path, sources)) {
        if (error)
            *error = "Refusing to overwrite the file being read; choose a new file.";
        return false;
    }
    AtomicWriter writer(path);
    return writer.open(error) && writer.write(data) && writer.commit(error);
}

Alphabet sniffAlphabet(const QString &path, Format *format) {
    FastxParser parser;
    std::vector<qint64> letters(26, 0);
    qint64 seen = 0;
    FastxRecord record;
    if (parser.open(path, nullptr))
        while (seen < (1 << 20) && parser.next(record))
            for (const char c : record.sequence) {
                const uchar upper = uchar(c) & 0xdf;
                if (upper >= 'A' && upper <= 'Z')
                    ++letters[upper - 'A'];
                ++seen;
            }
    if (format)
        *format = parser.format();
    return parser.format() == Format::Fastq ? Alphabet::Nucleotide : detectAlphabet(letters);
}
