#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>

// Open and save dialogs, behind an interface so tests can answer them.
class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    virtual void openSequences() = 0;
    // filterName and patterns ("*.json") describe what the save writes.
    virtual void save(const QUrl &suggestedUrl, const QString &filterName, const QStringList &patterns) = 0;

signals:
    void openSelected(const QUrl &url);
    void saveSelected(const QUrl &url);
    void failed(const QString &message);
};
