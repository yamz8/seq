#pragma once

#include <QString>
#include <QStringList>

// What can go wrong reading and writing sequence files, as English templates.
// The command line prints them as they are; the window shows them in the
// Omarchy language through translatedProblem, which finds the template a
// message was made from and translates that.
namespace Problem {
inline constexpr const char *CouldNotOpen = "Could not open %1: %2";
inline constexpr const char *CouldNotSeek = "Could not seek in %1";
inline constexpr const char *NoGzip = "Could not start gzip decompression.";
inline constexpr const char *NoGzipOut = "Could not start gzip compression.";
inline constexpr const char *Corrupt = "The file could not be decompressed; it may be truncated or corrupt.";
inline constexpr const char *NotSequences = "This is not a FASTA or FASTQ file: it should start with > or @.";
inline constexpr const char *ExpectedRecord = "Expected a record starting with %1 at byte %2.";
inline constexpr const char *NoPlusLine = "Record %1 has no + line.";
inline constexpr const char *QualityLength = "Record %1 has %2 bases but %3 quality scores.";
inline constexpr const char *CouldNotCache = "Could not write the decompressed copy to %1.";
inline constexpr const char *Cancelled = "Cancelled.";
inline constexpr const char *NoRecords = "There are no records in this file.";
inline constexpr const char *CouldNotWrite = "Could not write %1: %2";
inline constexpr const char *CouldNotSave = "Could not save %1: %2";
inline constexpr const char *CouldNotWriteTo = "Could not write to %1.";
inline constexpr const char *CouldNotSaveFile = "Could not save %1.";
inline constexpr const char *WouldOverwrite = "Refusing to overwrite the file being read; choose a new file.";
inline constexpr const char *EmptyMotif = "Type a motif to search for.";
inline constexpr const char *NotIupac = "%1 is not an IUPAC nucleotide code.";
}

// The English message: the template with its arguments filled in.
QString problem(const char *text, const QStringList &arguments = {});
// The message in the current language when it came from a template above,
// and unchanged otherwise.
QString translatedProblem(const QString &message);
