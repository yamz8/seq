#include "chart.h"

#include <QDir>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QLocale>
#include <QPainter>
#include <QSaveFile>
#include <QSvgGenerator>
#include <QTemporaryFile>

namespace {
// Whole numbers once the axis spans ten or more, so labels stay short.
QString axisNumber(double value, double span) {
    return QLocale().toString(value, 'f', span >= 10 || value == qRound64(value) ? 0 : 1);
}
}

void paintChart(QPainter &painter, const QRectF &rect, const ChartData &chart) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    if (chart.background.alpha() > 0)
        painter.fillRect(rect, chart.background);

    QFont font = painter.font();
    if (!chart.fontFamily.isEmpty())
        font.setFamily(chart.fontFamily);
    font.setPixelSize(11);
    painter.setFont(font);
    const QFontMetricsF metrics(font);
    const double line = metrics.height();

    double max = 0;
    for (const double v : chart.values)
        max = qMax(max, v);

    const double top = rect.top() + (chart.title.isEmpty() ? 4 : line + 8);
    const double bottom = rect.bottom() - line * (chart.xLabel.isEmpty() ? 1.4 : 2.6);
    double labelWidth = 0;
    for (int i = 0; i <= 4; ++i)
        labelWidth = qMax(labelWidth, metrics.horizontalAdvance(axisNumber(max * i / 4, max)));
    const double left = rect.left() + labelWidth + 10;
    const QRectF plot(left, top, qMax(1.0, rect.right() - left - 4), qMax(1.0, bottom - top));

    painter.setPen(chart.text);
    if (!chart.title.isEmpty())
        painter.drawText(QRectF(rect.left(), rect.top(), rect.width(), line + 4), Qt::AlignLeft | Qt::AlignVCenter,
                         chart.title);

    // Four grid lines, labelled with the counts they mark.
    for (int i = 0; i <= 4; ++i) {
        const double y = plot.bottom() - plot.height() * i / 4;
        painter.setPen(chart.grid);
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        if (max > 0) {
            painter.setPen(chart.text);
            painter.drawText(QRectF(rect.left(), y - line / 2, left - rect.left() - 6, line),
                             Qt::AlignRight | Qt::AlignVCenter, axisNumber(max * i / 4, max));
        }
    }

    // More values than pixels are averaged into one bar per pixel, so a long
    // read's per-base qualities still fit.
    const int count = int(chart.values.size());
    const int bars = qMax(1, qMin(count, int(plot.width())));
    const double width = plot.width() / bars;
    for (int b = 0; b < bars && count > 0 && max > 0; ++b) {
        const int from = int(qint64(b) * count / bars), to = qMax(from + 1, int(qint64(b + 1) * count / bars));
        double sum = 0;
        for (int i = from; i < to; ++i)
            sum += chart.values[i];
        const double value = sum / (to - from);
        QColor color = chart.bar;
        if (chart.lowThreshold >= 0 && value < chart.lowThreshold)
            color = chart.low;
        else if (chart.midThreshold >= 0 && value < chart.midThreshold)
            color = chart.mid;
        const double h = plot.height() * value / max;
        const double gap = width > 3 ? 1 : 0;
        painter.fillRect(QRectF(plot.left() + b * width, plot.bottom() - h, qMax(0.5, width - gap), h), color);
    }

    painter.setPen(chart.text);
    const QRectF labels(plot.left(), plot.bottom() + 2, plot.width(), line);
    if (chart.xMax > chart.xMin || count > 0) {
        const double span = chart.xMax - chart.xMin;
        painter.drawText(labels, Qt::AlignLeft | Qt::AlignTop, axisNumber(chart.xMin, span));
        painter.drawText(labels, Qt::AlignHCenter | Qt::AlignTop, axisNumber((chart.xMin + chart.xMax) / 2, span));
        painter.drawText(labels, Qt::AlignRight | Qt::AlignTop, axisNumber(chart.xMax, span));
    }
    if (!chart.xLabel.isEmpty())
        painter.drawText(QRectF(plot.left(), labels.bottom() + 2, plot.width(), line), Qt::AlignHCenter | Qt::AlignTop,
                         chart.xLabel);
    painter.restore();
}

bool writeChartSvg(const QString &path, const ChartData &chart, QSize size, QString *error) {
    // Painted to a temporary file beside the target and renamed over it, so
    // a failed export never leaves half a file.
    QTemporaryFile temporary(QFileInfo(path).absolutePath() + "/.seqview-XXXXXX.svg");
    if (!temporary.open()) {
        if (error)
            *error = QString("Could not write to %1.").arg(QFileInfo(path).absolutePath());
        return false;
    }
    temporary.close();
    {
        QSvgGenerator generator;
        generator.setFileName(temporary.fileName());
        generator.setSize(size);
        generator.setViewBox(QRect(QPoint(0, 0), size));
        generator.setTitle(chart.title);
        QPainter painter(&generator);
        paintChart(painter, QRectF(QPointF(16, 16), QSizeF(size) - QSizeF(32, 32)), chart);
    }
    QFile::remove(path);
    if (!QFile::rename(temporary.fileName(), path)) {
        if (error)
            *error = QString("Could not save %1.").arg(path);
        return false;
    }
    temporary.setAutoRemove(false);
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
    return true;
}

ChartItem::ChartItem(QQuickItem *parent) : QQuickPaintedItem(parent) {
    connect(this, &ChartItem::changed, this, [this] { update(); });
}

QVariantList ChartItem::values() const {
    QVariantList list;
    for (const double v : m_values)
        list << v;
    return list;
}

void ChartItem::setValues(const QVariantList &values) {
    m_values.clear();
    m_values.reserve(values.size());
    for (const QVariant &v : values)
        m_values << v.toDouble();
    emit changed();
}

void ChartItem::paint(QPainter *painter) {
    ChartData chart;
    chart.values = m_values;
    chart.xMin = m_xMin;
    chart.xMax = m_xMax;
    chart.title = m_title;
    chart.xLabel = m_xLabel;
    chart.bar = m_color;
    if (m_phred) {
        chart.lowThreshold = 20;
        chart.midThreshold = 30;
    }
    paintChart(*painter, boundingRect(), chart);
}
