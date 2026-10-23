#include "seqtools.h"

#include <algorithm>

namespace {
// IUPAC nucleotide codes as bits: A 1, C 2, G 4, T (or U) 8.
struct Tables {
    quint8 mask[256] = {};
    char complement[256] = {};
    qint8 codonIndex[256] = {};
    Tables() {
        const std::pair<char, quint8> codes[] = {
            {'A', 1}, {'C', 2}, {'G', 4}, {'T', 8}, {'U', 8}, {'R', 5}, {'Y', 10}, {'S', 6},
            {'W', 9}, {'K', 12}, {'M', 3}, {'B', 14}, {'D', 13}, {'H', 11}, {'V', 7}, {'N', 15},
        };
        for (const auto &[letter, bits] : codes) {
            mask[uchar(letter)] = bits;
            mask[uchar(letter + 32)] = bits;
        }
        for (int c = 0; c < 256; ++c)
            complement[c] = char(c);
        const char *pairs[] = {"AT", "CG", "RY", "KM", "BV", "DH", "SS", "WW", "NN"};
        for (const char *p : pairs) {
            complement[uchar(p[0])] = p[1];
            complement[uchar(p[1])] = p[0];
            complement[uchar(p[0] + 32)] = char(p[1] + 32);
            complement[uchar(p[1] + 32)] = char(p[0] + 32);
        }
        complement[uchar('U')] = 'A';
        complement[uchar('u')] = 'a';
        // The codon table below is ordered T, C, A, G.
        for (int c = 0; c < 256; ++c)
            codonIndex[c] = -1;
        const char *order = "TCAG";
        for (int i = 0; i < 4; ++i) {
            codonIndex[uchar(order[i])] = qint8(i);
            codonIndex[uchar(order[i] + 32)] = qint8(i);
        }
        codonIndex[uchar('U')] = codonIndex[uchar('u')] = 0;
    }
};
const Tables tables;
const char *StandardCode = "FFLLSSSSYY**CC*WLLLLPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG";

quint8 reverseMask(quint8 m) {
    return quint8(((m & 1) << 3) | ((m & 8) >> 3) | ((m & 2) << 1) | ((m & 4) >> 1));
}
}

QByteArray reverseComplement(const QByteArray &sequence) {
    QByteArray out(sequence.size(), Qt::Uninitialized);
    const qsizetype n = sequence.size();
    for (qsizetype i = 0; i < n; ++i)
        out[n - 1 - i] = tables.complement[uchar(sequence[i])];
    return out;
}

QByteArray translate(const QByteArray &sequence, int frame) {
    const QByteArray strand = frame < 0 ? reverseComplement(sequence) : sequence;
    const qsizetype offset = qAbs(frame) - 1;
    QByteArray protein;
    protein.reserve((strand.size() - offset) / 3 + 1);
    for (qsizetype i = offset; i + 2 < strand.size(); i += 3) {
        const int a = tables.codonIndex[uchar(strand[i])], b = tables.codonIndex[uchar(strand[i + 1])],
                  c = tables.codonIndex[uchar(strand[i + 2])];
        protein += a < 0 || b < 0 || c < 0 ? 'X' : StandardCode[a * 16 + b * 4 + c];
    }
    return protein;
}

QVector<Orf> findOrfs(const QByteArray &sequence, int minLength) {
    QVector<Orf> orfs;
    const qint64 n = sequence.size();
    for (const int frame : {1, 2, 3, -1, -2, -3}) {
        const QByteArray protein = translate(sequence, frame);
        const qint64 offset = qAbs(frame) - 1;
        qsizetype open = -1;
        for (qsizetype i = 0; i < protein.size(); ++i) {
            if (open < 0 && protein[i] == 'M')
                open = i;
            else if (open >= 0 && protein[i] == '*') {
                // Positions on the strand read, then mapped to the forward strand.
                const qint64 first = offset + open * 3, last = offset + i * 3 + 2;
                if (last - first + 1 >= minLength) {
                    Orf orf;
                    orf.frame = frame;
                    orf.start = frame > 0 ? first + 1 : n - last;
                    orf.end = frame > 0 ? last + 1 : n - first;
                    orf.protein = protein.mid(open, i - open);
                    orfs.append(orf);
                }
                open = -1;
            }
        }
    }
    std::stable_sort(orfs.begin(), orfs.end(), [](const Orf &a, const Orf &b) { return a.length() > b.length(); });
    return orfs;
}

bool Motif::compile(const QByteArray &text, Alphabet alphabet, QString *error) {
    m_forward.clear();
    m_reverse.clear();
    m_protein = text.trimmed().toUpper();
    m_isProtein = alphabet == Alphabet::Protein;
    if (m_protein.isEmpty()) {
        if (error)
            *error = "Type a motif to search for.";
        return false;
    }
    for (const char c : m_protein) {
        const quint8 bits = tables.mask[uchar(c)];
        if (!m_isProtein && !bits) {
            if (error)
                *error = QString("%1 is not an IUPAC nucleotide code.").arg(QChar(c));
            return false;
        }
        m_forward.append(m_isProtein ? quint8(c) : bits);
    }
    if (!m_isProtein) {
        for (qsizetype i = m_forward.size() - 1; i >= 0; --i)
            m_reverse.append(reverseMask(m_forward[i]));
        m_palindrome = m_reverse == m_forward;
    }
    return true;
}

void Motif::search(const QByteArray &sequence, bool bothStrands,
                   const std::function<bool(qint64, char)> &found) const {
    const qsizetype m = m_forward.size(), n = sequence.size();
    if (m == 0 || n < m)
        return;
    const char *s = sequence.constData();
    if (m_isProtein) {
        // X in a protein motif stands for any residue. Case folds for letters
        // only, so a stop (*) or gap (-) matches itself.
        auto upper = [](char c) { return c >= 'a' && c <= 'z' ? char(c - 32) : c; };
        for (qsizetype i = 0; i + m <= n; ++i) {
            qsizetype j = 0;
            while (j < m && (m_protein[j] == 'X' || upper(s[i + j]) == m_protein[j]))
                ++j;
            if (j == m && !found(i, '+'))
                return;
        }
        return;
    }
    // A sequence base matches when every base it may be is allowed by the
    // motif, so N in the sequence matches only N (or another code covering
    // all four) in the motif.
    auto matches = [&](const QVector<quint8> &motif, qsizetype i) {
        for (qsizetype j = 0; j < m; ++j) {
            const quint8 base = tables.mask[uchar(s[i + j])];
            if (!base || (base & ~motif[j]))
                return false;
        }
        return true;
    };
    const bool reverse = bothStrands && !m_palindrome;
    for (qsizetype i = 0; i + m <= n; ++i) {
        if (matches(m_forward, i) && !found(i, '+'))
            return;
        if (reverse && matches(m_reverse, i) && !found(i, '-'))
            return;
    }
}
