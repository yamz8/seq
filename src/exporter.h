#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QString>
#include <functional>
#include <memory>

#include "fastx.h"

// Writes a new file atomically: QSaveFile writes beside the target and
// renames over it on commit, so a failure never leaves half a file. A name
// ending in .gz is gzipped on the way.
class AtomicWriter {
public:
    explicit AtomicWriter(const QString &path);
    ~AtomicWriter();
    bool open(QString *error);
    bool write(const QByteArray &data);
    bool commit(QString *error);

private:
    bool deflateOut(const char *data, qsizetype size, int flush);

    QSaveFile m_file;
    struct Deflater;
    std::unique_ptr<Deflater> m_gzip;
    bool m_failed = false;
};

// True when writing to output would touch one of the inputs, by path or link.
bool sameFile(const QString &output, const QStringList &inputs);

struct StreamResult {
    qint64 read = 0, written = 0;
    QString error;
};

// Reads every record of input and writes those keep() accepts, after it has
// had the chance to change them (index is the record's 0-based position).
// sources are every path that must not be overwritten.
StreamResult streamRecords(const QString &input, const QString &output, const QStringList &sources,
                           const std::function<bool(qint64 index, FastxRecord &record)> &keep);

QJsonObject summaryJson(const FastxIndex &index, const SummaryStats &stats);
QByteArray summaryCsv(const FastxIndex &index, const SummaryStats &stats);
bool writeFile(const QString &path, const QByteArray &data, const QStringList &sources, QString *error);
// The alphabet of a file from its first records, without indexing all of it.
Alphabet sniffAlphabet(const QString &path, Format *format = nullptr);
