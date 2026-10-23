#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QString>
#include <QVector>

class QPainter;

// One bar chart, painted the same way in the window and in exported SVG.
struct ChartData {
    QVector<double> values;
    // The x range the bars cover, labelled at both ends and the middle.
    double xMin = 0, xMax = 0;
    QString title, xLabel;
    QColor bar = QColor("#FFD60A"), text = QColor("#c8c8cc"), grid = QColor("#2a2a2f");
    QColor background = Qt::transparent;
    // Bars below these values are drawn in the warning colors; for Phred.
    double lowThreshold = -1, midThreshold = -1;
    QColor low = QColor("#d9534f"), mid = QColor("#d9a54f");
    QString fontFamily;
};

void paintChart(QPainter &painter, const QRectF &rect, const ChartData &chart);
bool writeChartSvg(const QString &path, const ChartData &chart, QSize size = {720, 360}, QString *error = nullptr);

// The chart as a Qt Quick item; the window binds the data properties.
class ChartItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList values READ values WRITE setValues NOTIFY changed)
    Q_PROPERTY(double xMin MEMBER m_xMin NOTIFY changed)
    Q_PROPERTY(double xMax MEMBER m_xMax NOTIFY changed)
    Q_PROPERTY(QString title MEMBER m_title NOTIFY changed)
    Q_PROPERTY(QString xLabel MEMBER m_xLabel NOTIFY changed)
    Q_PROPERTY(QColor color MEMBER m_color NOTIFY changed)
    Q_PROPERTY(bool phred MEMBER m_phred NOTIFY changed)

public:
    explicit ChartItem(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;
    QVariantList values() const;
    void setValues(const QVariantList &values);

signals:
    void changed();

private:
    QVector<double> m_values;
    double m_xMin = 0, m_xMax = 0;
    QString m_title, m_xLabel;
    QColor m_color = QColor("#FFD60A");
    bool m_phred = false;
};
