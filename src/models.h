#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QQuickPaintedItem>
#include <memory>
#include <vector>

#include "fastx.h"

// The record list, over the index and an optional filter, so the window only
// ever asks for the rows it shows.
class RecordModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role { IdRole = Qt::UserRole + 1, LengthRole, GcRole, WeightRole, QualityRole, NumberRole };

    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void reset(std::shared_ptr<const FastxIndex> index);
    // Indices into the index, ascending; empty with filtered false shows all.
    void setFilter(std::vector<qsizetype> rows, bool filtered);
    bool filtered() const { return m_filtered; }
    // The record a row shows, and the row a record is on, or -1.
    qsizetype recordAt(int row) const;
    int rowOf(qsizetype record) const;
    const std::vector<qsizetype> &filter() const { return m_filter; }

signals:
    void countChanged();

private:
    std::shared_ptr<const FastxIndex> m_index;
    std::vector<qsizetype> m_filter;
    bool m_filtered = false;
};

struct Highlight {
    qint64 start = 0;
    int length = 0;
    bool current = false;
};

// A sequence (or several, for six frames) wrapped into rows of a fixed width.
class LinesModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
    Q_PROPERTY(int width READ width NOTIFY changed)

public:
    enum Role { LabelRole = Qt::UserRole + 1, StartRole, TextRole, HighlightsRole, FirstRole };

    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    struct Segment {
        QString label;
        QByteArray sequence;
        std::vector<Highlight> highlights;
        // The longest highlight, so a row finds every one reaching into it.
        int longest = 0;
    };
    void setSegments(std::vector<Segment> segments, int width);
    void setWidth(int width);
    void setHighlights(int segment, std::vector<Highlight> highlights);
    int width() const { return m_width; }
    // The row showing a 0-based position of a segment.
    Q_INVOKABLE int rowFor(int segment, qint64 position) const;

signals:
    void changed();

private:
    void layout();

    std::vector<Segment> m_segments;
    std::vector<int> m_firstRow;
    int m_rows = 0, m_width = 60;
};

// One row of sequence in groups of ten, bases tinted by kind and matches
// underlaid, or the ruler of column numbers above them.
class SequenceLine : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString text MEMBER m_text NOTIFY changed)
    Q_PROPERTY(QVariantList highlights MEMBER m_highlights NOTIFY changed)
    Q_PROPERTY(bool protein MEMBER m_protein NOTIFY changed)
    Q_PROPERTY(bool ruler MEMBER m_ruler NOTIFY changed)
    Q_PROPERTY(int columns MEMBER m_columns NOTIFY changed)
    Q_PROPERTY(QColor accent MEMBER m_accent NOTIFY changed)
    Q_PROPERTY(int pixelSize MEMBER m_pixelSize NOTIFY changed)

public:
    explicit SequenceLine(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;

signals:
    void changed();

private:
    void resize();
    QFont font() const;

    QString m_text;
    QVariantList m_highlights;
    bool m_protein = false, m_ruler = false;
    int m_columns = 60, m_pixelSize = 14;
    QColor m_accent = QColor("#FFD60A");
};
