#include "problems.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace {
const char *const Templates[] = {
    Problem::CouldNotOpen, Problem::CouldNotSeek, Problem::NoGzip, Problem::NoGzipOut, Problem::Corrupt,
    Problem::NotSequences, Problem::ExpectedRecord, Problem::NoPlusLine, Problem::QualityLength,
    Problem::CouldNotCache, Problem::Cancelled, Problem::NoRecords, Problem::CouldNotWrite, Problem::CouldNotSave,
    Problem::CouldNotWriteTo, Problem::CouldNotSaveFile, Problem::WouldOverwrite, Problem::EmptyMotif,
    Problem::NotIupac,
};

// Fills %1, %2, %3 at once, so an argument that itself holds %2 stays as it is.
QString fill(QString text, const QStringList &arguments) {
    switch (arguments.size()) {
    case 0: return text;
    case 1: return text.arg(arguments[0]);
    case 2: return text.arg(arguments[0], arguments[1]);
    default: return text.arg(arguments[0], arguments[1], arguments[2]);
    }
}
}

QString problem(const char *text, const QStringList &arguments) {
    return fill(QString::fromUtf8(text), arguments);
}

QString translatedProblem(const QString &message) {
    for (const char *text : Templates) {
        // The template as a pattern, each %n capturing its argument.
        static const QRegularExpression placeholder(R"(%\d)");
        QStringList pieces = QString::fromUtf8(text).split(placeholder);
        for (QString &piece : pieces)
            piece = QRegularExpression::escape(piece);
        const QString pattern = pieces.join("(.*)");
        const auto match = QRegularExpression("^" + pattern + "$", QRegularExpression::DotMatchesEverythingOption)
                               .match(message);
        if (match.hasMatch())
            return fill(QCoreApplication::translate("Problem", text), match.capturedTexts().mid(1));
    }
    return message;
}
