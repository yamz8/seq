#pragma once

#include <QStringList>

// Headless commands for scripts and agents: seqview stats, grep, filter, and so on.
bool isCliCommand(const QString &word);
QString cliSummary();
int runCli(const QStringList &arguments);
