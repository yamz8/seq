// The command line follows Hype's (MIT, David Heinemeier Hansson).
#include "cli.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cstdio>

#include "exporter.h"
#include "problems.h"
#include "fastx.h"
#include "seqtools.h"

namespace {
void print(FILE *stream, const QString &text) { fprintf(stream, "%s\n", qPrintable(text)); }
void print(const QJsonObject &object) { fprintf(stdout, "%s", QJsonDocument(object).toJson().constData()); }
int fail(const QString &message) {
    print(stderr, message);
    return 1;
}

struct Command {
    QCommandLineParser parser;
    Command(const QString &name, const QString &description, const QStringList &positionals = {"file"}) {
        parser.setApplicationDescription(description);
        parser.addHelpOption();
        parser.addPositionalArgument(name, description, name);
        for (const QString &p : positionals)
            parser.addPositionalArgument(p, p == "file"  ? "FASTA or FASTQ file, optionally gzipped"
                                            : p == "out" ? "New file to write; a .gz name is gzipped"
                                                         : p,
                                         "<" + p + ">");
        parser.addOption({"json", "Print the result as JSON"});
    }
    QString argument(int index) const { return parser.positionalArguments().value(index); }
    bool json() const { return parser.isSet("json"); }
    // Every positional is required.
    bool process(const QStringList &arguments, int count) {
        parser.process(arguments);
        if (parser.positionalArguments().size() < count + 1) {
            print(stderr, parser.helpText());
            return false;
        }
        if (!QFileInfo::exists(argument(1))) {
            print(stderr, argument(1) + ": no such file");
            return false;
        }
        return true;
    }
};

bool number(const QCommandLineParser &parser, const QString &name, double &value) {
    if (!parser.isSet(name))
        return true;
    bool ok = false;
    value = parser.value(name).toDouble(&ok);
    if (!ok)
        print(stderr, QString("--%1 takes a number, not %2.").arg(name, parser.value(name)));
    return ok;
}

int stats(const QStringList &arguments) {
    Command command("stats", "Count records and bases, with N50, GC% and length and quality statistics.");
    command.parser.addOption({"bins", "Bins in the length histogram (default 40)", "n", "40"});
    if (!command.process(arguments, 1))
        return 1;
    FastxIndex index;
    if (!index.build(command.argument(1), {}))
        return fail(command.argument(1) + ": " + index.error());
    const SummaryStats s = index.stats(qBound(1, command.parser.value("bins").toInt(), 1000));
    if (command.json()) {
        print(summaryJson(index, s));
        return 0;
    }
    const QLocale c = QLocale::c();
    print(stdout, QString("%1 (%2, %3)").arg(command.argument(1), formatName(index.format()),
                                             index.alphabet() == Alphabet::Protein ? "protein" : "nucleotide"));
    print(stdout, QString("records       %1").arg(s.records));
    print(stdout, QString("total length  %1").arg(s.totalLength));
    print(stdout, QString("min / max     %1 / %2").arg(s.minLength).arg(s.maxLength));
    print(stdout, QString("mean length   %1").arg(c.toString(s.meanLength, 'f', 1)));
    print(stdout, QString("N50           %1").arg(s.n50));
    if (index.alphabet() == Alphabet::Nucleotide)
        print(stdout, QString("GC            %1%").arg(c.toString(s.gcPercent, 'f', 2)));
    if (s.meanQuality >= 0)
        print(stdout, QString("mean quality  %1").arg(c.toString(s.meanQuality, 'f', 2)));
    return 0;
}

int grep(const QStringList &arguments) {
    Command command("grep", "Find a sequence motif, with IUPAC codes, and list every match.", {"file", "motif"});
    command.parser.addOption({"both-strands", "Search the reverse complement too (nucleotides)"});
    command.parser.addOption({"max", "Stop after this many matches (default 100000)", "n", "100000"});
    if (!command.process(arguments, 2))
        return 1;
    Format format = Format::Unknown;
    const Alphabet alphabet = sniffAlphabet(command.argument(1), &format);
    Motif motif;
    QString error;
    if (!motif.compile(command.argument(2).toUtf8(), alphabet, &error))
        return fail(error);
    const bool both = command.parser.isSet("both-strands") && alphabet == Alphabet::Nucleotide;
    const qint64 max = qMax<qint64>(1, command.parser.value("max").toLongLong());

    FastxParser parser;
    if (!parser.open(command.argument(1), &error))
        return fail(error);
    QJsonArray matches;
    qint64 count = 0, records = 0;
    bool truncated = false;
    FastxRecord record;
    while (!truncated && parser.next(record)) {
        motif.search(record.sequence, both, [&](qint64 position, char strand) {
            if (count >= max) {
                truncated = true;
                return false;
            }
            ++count;
            if (command.json())
                matches.append(QJsonObject{{"record", QString::fromUtf8(record.id)},
                                           {"index", records + 1},
                                           {"position", position + 1},
                                           {"end", position + motif.length()},
                                           {"strand", QString(QChar(strand))}});
            else
                print(stdout, QString("%1\t%2\t%3\t%4")
                                  .arg(QString::fromUtf8(record.id)).arg(position + 1)
                                  .arg(position + motif.length()).arg(QChar(strand)));
            return true;
        });
        ++records;
    }
    if (!parser.error().isEmpty())
        return fail(command.argument(1) + ": " + parser.error());
    if (command.json())
        print(QJsonObject{{"motif", command.argument(2)}, {"both_strands", both}, {"count", count},
                          {"truncated", truncated}, {"matches", matches}});
    else if (truncated)
        print(stderr, QString("Stopped after %1 matches.").arg(max));
    return 0;
}

int report(const Command &command, const StreamResult &result, QJsonObject extra = {}) {
    if (!result.error.isEmpty())
        return fail(result.error);
    QJsonObject o{{"input", command.argument(1)},
                  {"output", QFileInfo(command.argument(2)).absoluteFilePath()},
                  {"records_read", result.read},
                  {"records_written", result.written}};
    for (auto it = extra.begin(); it != extra.end(); ++it)
        o.insert(it.key(), it.value());
    if (command.json())
        print(o);
    else
        print(stdout, QString("Wrote %1 of %2 records to %3").arg(result.written).arg(result.read)
                          .arg(command.argument(2)));
    return 0;
}

int filter(const QStringList &arguments) {
    Command command("filter", "Write the records within length and quality limits to a new file.", {"file", "out"});
    command.parser.addOption({"min-len", "Shortest length to keep", "n"});
    command.parser.addOption({"max-len", "Longest length to keep", "n"});
    command.parser.addOption({"min-q", "Lowest mean Phred quality to keep (FASTQ)", "q"});
    if (!command.process(arguments, 2))
        return 1;
    double minLength = -1, maxLength = -1, minQuality = -1;
    if (!number(command.parser, "min-len", minLength) || !number(command.parser, "max-len", maxLength) ||
        !number(command.parser, "min-q", minQuality))
        return 1;
    const auto result = streamRecords(command.argument(1), command.argument(2), {}, [&](qint64, FastxRecord &r) {
        const qsizetype n = r.sequence.size();
        if ((minLength >= 0 && n < minLength) || (maxLength >= 0 && n > maxLength))
            return false;
        // A record without qualities can't meet a quality limit.
        return minQuality < 0 || meanQuality(r.quality) >= minQuality;
    });
    return report(command, result);
}

int revcomp(const QStringList &arguments) {
    Command command("revcomp", "Write the reverse complement of every record to a new file.", {"file", "out"});
    if (!command.process(arguments, 2))
        return 1;
    if (sniffAlphabet(command.argument(1)) == Alphabet::Protein)
        return fail(command.argument(1) + ": protein sequences have no reverse complement.");
    const auto result = streamRecords(command.argument(1), command.argument(2), {}, [](qint64, FastxRecord &r) {
        r.sequence = reverseComplement(r.sequence);
        std::reverse(r.quality.begin(), r.quality.end());
        return true;
    });
    return report(command, result);
}

int translateCommand(const QStringList &arguments) {
    Command command("translate", "Translate every record with the standard genetic code into protein FASTA.",
                    {"file", "out"});
    command.parser.addOption({"frame", "Reading frame: 1, 2, 3, -1, -2 or -3 (default 1)", "frame", "1"});
    command.parser.addOption({"six-frames", "Write all six frames, as <id>_frame<f>"});
    if (!command.process(arguments, 2))
        return 1;
    const int frame = command.parser.value("frame").toInt();
    if (frame == 0 || qAbs(frame) > 3)
        return fail("--frame is 1, 2, 3, -1, -2 or -3.");
    if (sniffAlphabet(command.argument(1)) == Alphabet::Protein)
        return fail(command.argument(1) + ": these are already protein sequences.");
    const bool six = command.parser.isSet("six-frames");
    StreamResult result;
    if (!six) {
        result = streamRecords(command.argument(1), command.argument(2), {}, [frame](qint64, FastxRecord &r) {
            r.sequence = translate(r.sequence, frame);
            r.quality.clear();
            return true;
        });
    } else {
        // Six records out for every one in, so this writes them itself.
        FastxParser parser;
        if (sameFile(command.argument(2), {command.argument(1)}))
            return fail(problem(Problem::WouldOverwrite));
        if (!parser.open(command.argument(1), &result.error))
            return fail(result.error);
        AtomicWriter writer(command.argument(2));
        if (!writer.open(&result.error))
            return fail(result.error);
        FastxRecord r;
        while (parser.next(r)) {
            ++result.read;
            QByteArray out;
            const QList<QByteArray> proteins = translateSixFrames(r.sequence);
            const int frames[] = {1, 2, 3, -1, -2, -3};
            for (int i = 0; i < 6; ++i) {
                const int f = frames[i];
                FastxRecord t;
                t.id = r.id + "_frame" + QByteArray::number(f);
                t.description = r.description;
                t.sequence = proteins[i];
                writeRecord(out, t, Format::Fasta);
                ++result.written;
            }
            writer.write(out);
        }
        if (!parser.error().isEmpty())
            return fail(parser.error());
        writer.commit(&result.error);
    }
    return report(command, result, {{"frames", six ? QJsonValue("all") : QJsonValue(frame)}});
}

// As the hey and basecamp tools do: one shared copy, linked into Claude Code, found directly by Codex.
int skill(const QStringList &arguments) {
    QCommandLineParser parser;
    parser.setApplicationDescription("Print the agent skill, or install it for your coding agents with: skill install.");
    parser.addHelpOption();
    parser.addPositionalArgument("skill", "", "skill");
    parser.addPositionalArgument("install", "Copy to ~/.agents/skills/seqview and link into ~/.claude/skills",
                                 "[install]");
    parser.process(arguments);
    QFile bundled(":/skill.md");
    if (!bundled.open(QIODevice::ReadOnly))
        return 1;
    const QByteArray text = bundled.readAll();
    const QString action = parser.positionalArguments().value(1);
    if (action.isEmpty()) {
        fputs(text.constData(), stdout);
        return 0;
    }
    if (action != "install")
        return fail("Use seqview skill to print the skill, or seqview skill install.");
    const QDir home = QDir::home();
    const QString directory = home.filePath(".agents/skills/seqview");
    QSaveFile file(directory + "/SKILL.md");
    if (!QDir().mkpath(directory) || !file.open(QIODevice::WriteOnly) || file.write(text) < 0 || !file.commit())
        return fail("Could not write " + file.fileName());
    print(stdout, "Installed " + file.fileName());
    if (!home.exists(".claude"))
        return 0;
    const QString link = home.filePath(".claude/skills/seqview");
    const QFileInfo existing(link);
    if (existing.isSymLink())
        QFile::remove(link);
    else if (existing.exists())
        return fail(link + " exists and is not a link; leaving it alone.");
    if (!home.mkpath(".claude/skills") || !QFile::link("../../.agents/skills/seqview", link))
        return fail("Could not link " + link);
    print(stdout, "Linked " + link);
    return 0;
}

int help(const QStringList &arguments) {
    const QString topic = arguments.value(2);
    if (isCliCommand(topic) && topic != "help")
        return runCli({arguments[0], topic, "--help"});
    print(stdout, "Usage: seqview <command> [options]\n\n" + cliSummary());
    return 0;
}
} // namespace

QString cliSummary() {
    return "Viewer:\n"
           "  open [file]                       Open the viewer, on a FASTA or FASTQ file\n\n"
           "Commands that need no display (seqview help <command> for options):\n"
           "  stats <file>                      Records, bases, N50, GC%, length and quality\n"
           "  grep <file> <motif>               Find an IUPAC motif, --both-strands for DNA\n"
           "  filter <file> <out>               Keep records by --min-len, --max-len, --min-q\n"
           "  revcomp <file> <out>              Reverse complement every record\n"
           "  translate <file> <out>            Translate in --frame F or --six-frames\n"
           "  skill [install]                   Print the skill for coding agents, or install it\n\n"
           "Every command takes --json, exits 0 on success and 1 on failure, and never\n"
           "changes the file it reads. Files may be gzipped.";
}

bool isCliCommand(const QString &word) {
    return QStringList{"stats", "grep", "filter", "revcomp", "translate", "skill", "help", "--help", "-h"}.contains(word);
}

int runCli(const QStringList &arguments) {
    const QString command = arguments.value(1);
    return command == "stats"       ? stats(arguments)
           : command == "grep"      ? grep(arguments)
           : command == "filter"    ? filter(arguments)
           : command == "revcomp"   ? revcomp(arguments)
           : command == "translate" ? translateCommand(arguments)
           : command == "skill"     ? skill(arguments)
                                    : help(arguments);
}
