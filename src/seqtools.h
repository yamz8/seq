#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <functional>

#include "fastx.h"

// The sequence work behind both the window and the command line: reverse
// complement, translation, ORFs, and IUPAC motif search.

// IUPAC codes complement too, case is kept, and RNA (U, no T) stays RNA.
QByteArray reverseComplement(const QByteArray &sequence);
// Frames 1, 2, 3 read the forward strand from offsets 0, 1, 2; -1, -2, -3
// read the reverse complement the same way. Standard genetic code; a codon
// with any ambiguous base is X, a stop is *.
QByteArray translate(const QByteArray &sequence, int frame);
// Frames 1, 2, 3, -1, -2, -3, in that order.
QList<QByteArray> translateSixFrames(const QByteArray &sequence);

struct Orf {
    int frame = 1;
    // 1-based, inclusive, on the forward strand, stop codon included.
    qint64 start = 0, end = 0;
    qint64 length() const { return end - start + 1; }
    QByteArray protein;
};

// ATG to the first stop in frame, in all six frames, at least minLength
// bases long with the stop. Longest first.
QVector<Orf> findOrfs(const QByteArray &sequence, int minLength);

class Motif {
public:
    bool compile(const QByteArray &text, Alphabet alphabet, QString *error = nullptr);
    int length() const { return int(m_forward.size()); }
    bool isEmpty() const { return m_forward.isEmpty(); }
    // Calls found(position, strand) for every match, overlapping ones too.
    // Positions are 0-based on the forward strand, '+' or '-'. A motif that is
    // its own reverse complement is only reported once, on '+'.
    void search(const QByteArray &sequence, bool bothStrands,
                const std::function<bool(qint64 position, char strand)> &found) const;

private:
    QVector<quint8> m_forward, m_reverse;
    QByteArray m_protein;
    bool m_isProtein = false, m_palindrome = false;
};
