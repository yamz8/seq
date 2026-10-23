#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <zlib.h>

#include "backend.h"
#include "exporter.h"
#include "fastx.h"
#include "filepicker.h"
#include "seqtools.h"
#include "theme.h"
#include "translator.h"

// Answers every dialog with a path the test chose, without a portal.
class FakePicker : public FilePicker {
public:
    QString answer;
    QString lastSuggested;
    void openSequences() override { emit openSelected(QUrl::fromLocalFile(answer)); }
    void save(const QUrl &suggested, const QString &, const QStringList &) override {
        lastSuggested = suggested.toLocalFile();
        emit saveSelected(QUrl::fromLocalFile(answer));
    }
};

class SeqviewTests : public QObject {
    Q_OBJECT

private slots:
    void parsesMultiLineFasta()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("a.fa");
        write(path, ">one first record\nACGT\nAC GT\n\n>two\nGGGG\n>empty\n>three\nttaa\n");
        FastxParser parser;
        QVERIFY(parser.open(path, nullptr));
        FastxRecord r;
        QVERIFY(parser.next(r));
        QCOMPARE(parser.format(), Format::Fasta);
        QCOMPARE(r.id, QByteArray("one"));
        QCOMPARE(r.description, QByteArray("first record"));
        QCOMPARE(r.sequence, QByteArray("ACGTACGT"));
        QVERIFY(parser.next(r));
        QCOMPARE(r.id, QByteArray("two"));
        QCOMPARE(r.sequence, QByteArray("GGGG"));
        QVERIFY(parser.next(r));
        QCOMPARE(r.id, QByteArray("empty"));
        QVERIFY(r.sequence.isEmpty());
        QVERIFY(parser.next(r));
        QCOMPARE(r.sequence, QByteArray("ttaa"));
        QVERIFY(!parser.next(r));
        QVERIFY(parser.error().isEmpty());
    }

    void parsesWindowsLineEndings()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("crlf.fq");
        write(path, "@r1 x\r\nACGT\r\n+\r\nIIII\r\n@r2\r\nGG\r\n+r2\r\n@@\r\n");
        FastxParser parser;
        QVERIFY(parser.open(path, nullptr));
        FastxRecord r;
        QVERIFY(parser.next(r));
        QCOMPARE(parser.format(), Format::Fastq);
        QCOMPARE(r.id, QByteArray("r1"));
        QCOMPARE(r.sequence, QByteArray("ACGT"));
        QCOMPARE(r.quality, QByteArray("IIII"));
        // A quality line may start with @; it is read by length.
        QVERIFY(parser.next(r));
        QCOMPARE(r.quality, QByteArray("@@"));
        QVERIFY(!parser.next(r));
        QVERIFY(parser.error().isEmpty());
    }

    void readsGzippedInputAndIndexesIt()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("reads.fq.gz");
        QByteArray plain;
        for (int i = 0; i < 3000; ++i)
            plain += "@read" + QByteArray::number(i) + "\n" + QByteArray(100 + i % 50, i % 2 ? 'G' : 'A') +
                     "\n+\n" + QByteArray(100 + i % 50, char(33 + 30)) + "\n";
        writeGzip(path, plain);

        FastxIndex index;
        QVERIFY2(index.build(path, dir.filePath("cache.fq")), qPrintable(index.error()));
        QCOMPARE(index.size(), qsizetype(3000));
        QCOMPARE(index.format(), Format::Fastq);
        QVERIFY(index.readablePath() != path);
        FastxRecord r;
        QVERIFY(index.read(2999, r));
        QCOMPARE(r.id, QByteArray("read2999"));
        QCOMPARE(r.sequence.size(), qsizetype(100 + 2999 % 50));
        QCOMPARE(index.entry(1).meanQuality, 30.0f);
        const SummaryStats s = index.stats();
        QCOMPARE(s.meanQuality, 30.0);
        QCOMPARE(s.qualityHistogram.value(30), 3000.0);
    }

    void reportsMalformedFastq()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("bad.fq");
        write(path, "@r1\nACGT\n+\nII\n");
        FastxIndex index;
        QVERIFY(!index.build(path, {}));
        QVERIFY(index.error().contains("quality"));
        write(path, "hello\n");
        QVERIFY(!index.build(path, {}));
        QVERIFY(index.error().contains("FASTA or FASTQ"));
    }

    void computesN50AndGc()
    {
        QCOMPARE(n50({2, 3, 4, 5, 6, 7, 8, 9, 10}), qint64(8));
        QCOMPARE(n50({100}), qint64(100));
        QCOMPARE(n50({}), qint64(0));
        QCOMPARE(gcPercent(1, 3), 25.0);

        QTemporaryDir dir;
        const QString path = dir.filePath("gc.fa");
        // N is in neither count, so GC is 3 of 4.
        write(path, ">a\nGGCA\n>b\nNNNN\n>c\nACGTACGTAC\n");
        FastxIndex index;
        QVERIFY(index.build(path, {}));
        QCOMPARE(index.alphabet(), Alphabet::Nucleotide);
        QCOMPARE(gcPercent(index.entry(0).gc, index.entry(0).at), 75.0);
        const SummaryStats s = index.stats();
        QCOMPARE(s.records, qint64(3));
        QCOMPARE(s.totalLength, qint64(18));
        QCOMPARE(s.minLength, qint64(4));
        QCOMPARE(s.maxLength, qint64(10));
        QCOMPARE(s.n50, qint64(10));
        QCOMPARE(s.gcPercent, 100.0 * 8 / 14);
    }

    void detectsProtein()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("p.faa");
        write(path, ">p1\nMKWVTFISLLLLFSSAYS\n>p2\nMDEHHKRRPQ\n");
        FastxIndex index;
        QVERIFY(index.build(path, {}));
        QCOMPARE(index.alphabet(), Alphabet::Protein);
        // Glycine-glycine: two residues plus one water.
        QVERIFY(qAbs(molecularWeight("GG") - 132.117) < 0.01);
        QVERIFY(index.entry(0).weight > 2000);
    }

    void reverseComplements()
    {
        QCOMPARE(reverseComplement("ACGTN"), QByteArray("NACGT"));
        QCOMPARE(reverseComplement("aaccRY"), QByteArray("RYggtt"));
        QCOMPARE(reverseComplement("GATTACA"), QByteArray("TGTAATC"));
        QCOMPARE(reverseComplement(reverseComplement("ACGTRYKMBDHVN")), QByteArray("ACGTRYKMBDHVN"));
    }

    void translatesSixFrames()
    {
        const QByteArray s = "ATGGCCATTGTAATGGGCCGCTGAAAGGGTGCCCGATAG";
        QCOMPARE(translate(s, 1), QByteArray("MAIVMGR*KGAR*"));
        QCOMPARE(translate(s, 2), QByteArray("WPL*WAAERVPD"));
        QCOMPARE(translate(s, 3), QByteArray("GHCNGPLKGCPI"));
        QCOMPARE(translate(s, -1), QByteArray("LSGTLSAAHYNGH"));
        QCOMPARE(translate(s, -2), QByteArray("YRAPFQRPITMA"));
        QCOMPARE(translate(s, -3), QByteArray("IGHPFSGPLQWP"));
        QCOMPARE(translate("ATGNNNTAA", 1), QByteArray("MX*"));
        QCOMPARE(translate("AUGUAA", 1), QByteArray("M*"));
    }

    void findsOrfs()
    {
        // A 30 bp ORF on +1, and the reverse complement of a 24 bp one after it.
        const QByteArray forward = "ATGAAACCCGGGTTTAAACCCGGGAAATAG";
        const QByteArray reverse = reverseComplement("ATGCCCAAAGGGTTTCCCAAATGA");
        const QByteArray s = forward + "CC" + reverse;
        const QVector<Orf> orfs = findOrfs(s, 24);
        QCOMPARE(orfs.size(), 2);
        QCOMPARE(orfs[0].frame, 1);
        QCOMPARE(orfs[0].start, qint64(1));
        QCOMPARE(orfs[0].end, qint64(30));
        QCOMPARE(orfs[0].protein, QByteArray("MKPGFKPGK"));
        QVERIFY(orfs[1].frame < 0);
        QCOMPARE(orfs[1].start, qint64(33));
        QCOMPARE(orfs[1].end, qint64(56));
        QCOMPARE(orfs[1].protein, QByteArray("MPKGFPK"));
        QCOMPARE(findOrfs(s, 27).size(), 1);
    }

    void matchesIupacMotifsOnBothStrands()
    {
        Motif motif;
        QVERIFY(motif.compile("GRATTC", Alphabet::Nucleotide));
        const QByteArray s = "TTGAATTCAAGGATTCTTGAATCCTT";
        QList<QPair<qint64, char>> hits;
        motif.search(s, true, [&](qint64 p, char strand) { hits.append({p, strand}); return true; });
        // GAATTC at 2 (+, and its own reverse complement GAATTC on -),
        // GGATTC at 10 (+), and GAATCC (revcomp of GGATTC) at 18 (-).
        QCOMPARE(hits, (QList<QPair<qint64, char>>{{2, '+'}, {2, '-'}, {10, '+'}, {18, '-'}}));

        hits.clear();
        motif.search(s, false, [&](qint64 p, char strand) { hits.append({p, strand}); return true; });
        QCOMPARE(hits.size(), 2);

        // An N in the sequence matches only N in the motif.
        Motif n;
        QVERIFY(n.compile("ANT", Alphabet::Nucleotide));
        hits.clear();
        n.search("ACT ANT AGT", false, [&](qint64 p, char strand) { hits.append({p, strand}); return true; });
        QCOMPARE(hits.size(), 3);
        Motif strict;
        QVERIFY(strict.compile("ACT", Alphabet::Nucleotide));
        hits.clear();
        strict.search("ANT", false, [&](qint64 p, char strand) { hits.append({p, strand}); return true; });
        QVERIFY(hits.isEmpty());

        // A palindrome is reported once.
        Motif palindrome;
        QVERIFY(palindrome.compile("GAATTC", Alphabet::Nucleotide));
        hits.clear();
        palindrome.search("xxGAATTCxx", true, [&](qint64 p, char strand) { hits.append({p, strand}); return true; });
        QCOMPARE(hits, (QList<QPair<qint64, char>>{{2, '+'}}));

        QString error;
        QVERIFY(!motif.compile("ACGZ", Alphabet::Nucleotide, &error));
        QVERIFY(error.contains("Z"));
        Motif protein;
        QVERIFY(protein.compile("KXXR", Alphabet::Protein));
        hits.clear();
        protein.search("MKAAREKGGR", true, [&](qint64 p, char strand) { hits.append({p, strand}); return true; });
        QCOMPARE(hits, (QList<QPair<qint64, char>>{{1, '+'}, {6, '+'}}));
    }

    void filtersIntoANewFileAtomically()
    {
        QTemporaryDir dir;
        const QString input = dir.filePath("reads.fq"), output = dir.filePath("kept.fq.gz");
        write(input, "@short\nACG\n+\nIII\n@lowq\nACGTACGT\n+\n########\n@good\nACGTACGT\n+\nIIIIIIII\n");
        const auto result = streamRecords(input, output, {}, [](qint64, FastxRecord &r) {
            return r.sequence.size() >= 5 && meanQuality(r.quality) >= 30;
        });
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.read, qint64(3));
        QCOMPARE(result.written, qint64(1));
        FastxIndex index;
        QVERIFY(index.build(output, dir.filePath("cache")));
        QCOMPARE(index.size(), qsizetype(1));
        QCOMPARE(index.id(0), QByteArray("good"));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 3);

        const QByteArray before = read(input);
        const auto refused = streamRecords(input, input, {}, [](qint64, FastxRecord &) { return true; });
        QVERIFY(!refused.error.isEmpty());
        QCOMPARE(read(input), before);
    }

    void writesSummaryAndChart()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("a.fa");
        write(path, ">a\nACGTACGTAA\n>b\nGG\n");
        FastxIndex index;
        QVERIFY(index.build(path, {}));
        const QJsonObject json = summaryJson(index, index.stats());
        QCOMPARE(json.value("records").toInt(), 2);
        QCOMPARE(json.value("n50").toInt(), 10);
        QCOMPARE(json.value("format").toString(), QString("fasta"));
        QVERIFY(summaryCsv(index, index.stats()).contains("n50,10\n"));

        ChartData chart;
        chart.values = {1, 2, 3};
        chart.title = "Lengths";
        QString error;
        QVERIFY(writeChartSvg(dir.filePath("chart.svg"), chart, {400, 200}, &error));
        QVERIFY(read(dir.filePath("chart.svg")).contains("<svg"));
    }

    void backendOpensSearchesAndExports()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("genes.fa");
        write(path, ">alpha\nAAAGAATTCAAA\n>beta\nCCCCCCCC\n>gamma one\nTTGGATTCTT\n");
        FakePicker picker;
        picker.answer = path;
        Backend backend(&picker);
        backend.open();
        backend.waitForIdle();
        QVERIFY2(backend.error().isEmpty(), qPrintable(backend.error()));
        QCOMPARE(backend.totalRecords(), 3);
        QCOMPARE(backend.current().value("id").toString(), QString("alpha"));

        backend.search("GRATTC", true, true);
        backend.waitForIdle();
        QCOMPARE(backend.records()->rowCount(), 2);
        QCOMPARE(backend.matchCount(), 3);
        // alpha matches on both strands at the same place, then gamma.
        QCOMPARE(backend.matchIndex(), 0);
        backend.nextMatch();
        QCOMPARE(backend.current().value("id").toString(), QString("alpha"));
        backend.nextMatch();
        QCOMPARE(backend.current().value("id").toString(), QString("gamma"));
        QCOMPARE(backend.matchIndex(), 2);

        picker.answer = dir.filePath("matches.fa");
        backend.exportAs("filtered");
        backend.waitForIdle();
        QVERIFY2(backend.error().isEmpty(), qPrintable(backend.error()));
        QCOMPARE(read(dir.filePath("matches.fa")), QByteArray(">alpha\nAAAGAATTCAAA\n>gamma one\nTTGGATTCTT\n"));

        // Exporting over the open file is refused and leaves it alone.
        const QByteArray original = read(path);
        picker.answer = path;
        backend.exportAs("json");
        QVERIFY(!backend.error().isEmpty());
        QCOMPARE(read(path), original);

        backend.search("bet", false, false);
        backend.waitForIdle();
        QCOMPARE(backend.records()->rowCount(), 1);
        backend.clearSearch();
        QCOMPARE(backend.records()->rowCount(), 3);
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

    void shippedCatalogsAreValid()
    {
        const QStringList catalogs = QDir(":/i18n").entryList({"*.json"});
        QVERIFY(catalogs.contains("en.json"));
        for (const QString &name : catalogs) {
            QFile file(":/i18n/" + name);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QJsonParseError error;
            QJsonDocument::fromJson(file.readAll(), &error);
            QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(name + ": " + error.errorString()));
        }
    }

private:
    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
    static void writeGzip(const QString &path, const QByteArray &contents)
    {
        gzFile file = gzopen(QFile::encodeName(path).constData(), "wb");
        QVERIFY(file);
        QCOMPARE(gzwrite(file, contents.constData(), unsigned(contents.size())), int(contents.size()));
        gzclose(file);
    }
    static QByteArray read(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
};

QTEST_MAIN(SeqviewTests)
#include "seqview_tests.moc"
