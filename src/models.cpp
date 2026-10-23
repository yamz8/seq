#include "models.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <algorithm>

int RecordModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid() || !m_index)
        return 0;
    return int(m_filtered ? m_filter.size() : size_t(m_index->size()));
}

qsizetype RecordModel::recordAt(int row) const {
    if (row < 0 || row >= rowCount())
        return -1;
    return m_filtered ? m_filter[size_t(row)] : row;
}

int RecordModel::rowOf(qsizetype record) const {
    if (!m_index || record < 0 || record >= m_index->size())
        return -1;
    if (!m_filtered)
        return int(record);
    const auto it = std::lower_bound(m_filter.begin(), m_filter.end(), record);
    return it != m_filter.end() && *it == record ? int(it - m_filter.begin()) : -1;
}

QVariant RecordModel::data(const QModelIndex &index, int role) const {
    const qsizetype record = recordAt(index.row());
    if (record < 0)
        return {};
    const IndexEntry &e = m_index->entry(record);
    switch (role) {
    case IdRole:
        return QString::fromUtf8(m_index->id(record));
    case LengthRole:
        return qint64(e.length);
    case GcRole:
        return gcPercent(e.gc, e.at);
    case WeightRole:
        return double(e.weight);
    case QualityRole:
        return double(e.meanQuality);
    case NumberRole:
        return qint64(record + 1);
    }
    return {};
}

QHash<int, QByteArray> RecordModel::roleNames() const {
    return {{IdRole, "recordId"}, {LengthRole, "recordLength"}, {GcRole, "recordGc"},
            {WeightRole, "recordWeight"}, {QualityRole, "recordQuality"}, {NumberRole, "recordNumber"}};
}

void RecordModel::reset(std::shared_ptr<const FastxIndex> index) {
    beginResetModel();
    m_index = std::move(index);
    m_filter.clear();
    m_filtered = false;
    endResetModel();
    emit countChanged();
}

void RecordModel::setFilter(std::vector<qsizetype> rows, bool filtered) {
    beginResetModel();
    m_filter = std::move(rows);
    m_filtered = filtered;
    endResetModel();
    emit countChanged();
}

int LinesModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_rows;
}

void LinesModel::layout() {
    m_firstRow.clear();
    m_rows = 0;
    for (const Segment &s : m_segments) {
        m_firstRow.push_back(m_rows);
        m_rows += qMax(1, int((s.sequence.size() + m_width - 1) / m_width));
    }
}

void LinesModel::setSegments(std::vector<Segment> segments, int width) {
    beginResetModel();
    m_segments = std::move(segments);
    m_width = qMax(10, width);
    layout();
    endResetModel();
    emit changed();
}

void LinesModel::setWidth(int width) {
    if (width == m_width)
        return;
    beginResetModel();
    m_width = qMax(10, width);
    layout();
    endResetModel();
    emit changed();
}

void LinesModel::setHighlights(int segment, std::vector<Highlight> highlights) {
    if (segment < 0 || segment >= int(m_segments.size()))
        return;
    Segment &s = m_segments[size_t(segment)];
    s.highlights = std::move(highlights);
    s.longest = 0;
    for (const Highlight &h : s.highlights)
        s.longest = qMax(s.longest, h.length);
    if (m_rows > 0)
        emit dataChanged(index(0), index(m_rows - 1), {HighlightsRole});
}

int LinesModel::rowFor(int segment, qint64 position) const {
    if (segment < 0 || segment >= int(m_segments.size()))
        return 0;
    return m_firstRow[size_t(segment)] + int(position / m_width);
}

QVariant LinesModel::data(const QModelIndex &index, int role) const {
    const int row = index.row();
    if (row < 0 || row >= m_rows)
        return {};
    const int segment = int(std::upper_bound(m_firstRow.begin(), m_firstRow.end(), row) - m_firstRow.begin()) - 1;
    const Segment &s = m_segments[size_t(segment)];
    const qint64 start = qint64(row - m_firstRow[size_t(segment)]) * m_width;
    switch (role) {
    case LabelRole:
        return s.label;
    case FirstRole:
        return row == m_firstRow[size_t(segment)];
    case StartRole:
        return start + 1;
    case TextRole:
        return QString::fromLatin1(s.sequence.mid(start, m_width));
    case HighlightsRole: {
        // Matches overlapping this row, clipped to it, as [from, length, current].
        QVariantList list;
        const qint64 end = start + m_width;
        auto it = std::lower_bound(s.highlights.begin(), s.highlights.end(), start - s.longest,
                                   [](const Highlight &h, qint64 v) { return h.start < v; });
        for (; it != s.highlights.end() && it->start < end; ++it) {
            const qint64 from = qMax(start, it->start), to = qMin(end, it->start + it->length);
            if (to > from)
                list.append(QVariant(QVariantList{int(from - start), int(to - from), it->current}));
        }
        return list;
    }
    }
    return {};
}

QHash<int, QByteArray> LinesModel::roleNames() const {
    return {{LabelRole, "label"}, {StartRole, "start"}, {TextRole, "bases"},
            {HighlightsRole, "highlights"}, {FirstRole, "first"}};
}

SequenceLine::SequenceLine(QQuickItem *parent) : QQuickPaintedItem(parent) {
    connect(this, &SequenceLine::changed, this, [this] {
        resize();
        update();
    });
    resize();
}

QFont SequenceLine::font() const {
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPixelSize(m_pixelSize);
    return f;
}

// A space's width between groups of ten, as sequence viewers print them.
void SequenceLine::resize() {
    const QFontMetricsF metrics(font());
    const double advance = metrics.horizontalAdvance('M');
    setImplicitWidth(advance * (m_columns + (m_columns - 1) / 10) + 2);
    setImplicitHeight(std::ceil(metrics.height() + 4));
}

namespace {
QColor baseColor(QChar c, bool protein) {
    const char u = c.toUpper().toLatin1();
    if (protein) {
        if (QByteArrayView("AVLIMFWC").contains(u))
            return QColor("#a9c7e8"); // hydrophobic
        if (QByteArrayView("KRH").contains(u))
            return QColor("#e8a9b8"); // positive
        if (QByteArrayView("DE").contains(u))
            return QColor("#c3a9e8"); // negative
        if (QByteArrayView("STNQGPY").contains(u))
            return QColor("#a9e8c0"); // polar and small
        return QColor("#8a8a90");
    }
    switch (u) {
    case 'A': return QColor("#8fd4a0");
    case 'C': return QColor("#8fb8e0");
    case 'G': return QColor("#e6cf8a");
    case 'T':
    case 'U': return QColor("#e69a9a");
    default: return QColor("#7a7a80");
    }
}
}

void SequenceLine::paint(QPainter *painter) {
    const QFont f = font();
    painter->setFont(f);
    const QFontMetricsF metrics(f);
    const double advance = metrics.horizontalAdvance('M');
    auto x = [&](int column) { return 1 + advance * (column + column / 10); };
    const double baseline = (height() - metrics.height()) / 2 + metrics.ascent();

    if (m_ruler) {
        painter->setPen(QColor("#6a6a70"));
        for (int column = 10; column <= m_columns; column += 10) {
            const QString label = QString::number(column);
            painter->drawText(QPointF(x(column - 1) + advance - metrics.horizontalAdvance(label), baseline), label);
        }
        return;
    }

    for (const QVariant &value : m_highlights) {
        const QVariantList h = value.toList();
        if (h.size() < 3)
            continue;
        const int from = h[0].toInt(), length = h[1].toInt();
        QColor fill = m_accent;
        fill.setAlphaF(h[2].toBool() ? 0.55 : 0.22);
        // Each group of ten is its own box, so the gaps stay visible.
        for (int c = from; c < from + length;) {
            const int groupEnd = qMin(from + length, (c / 10 + 1) * 10);
            painter->fillRect(QRectF(x(c), 1, x(groupEnd - 1) + advance - x(c), height() - 2), fill);
            c = groupEnd;
        }
    }
    for (int i = 0; i < m_text.size(); ++i) {
        painter->setPen(baseColor(m_text[i], m_protein));
        painter->drawText(QPointF(x(i), baseline), QString(m_text[i]));
    }
}
