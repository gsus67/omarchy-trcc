// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QMap>
#include <QString>

struct CustomTheme {
    QString id, name, backgroundPath;
    QJsonObject spec;
};

class ThemeStore {
    QString directory;
    QMap<QString, CustomTheme> entries;
public:
    void initialize(const QString &path);
    bool contains(const QString &id) const { return entries.contains(id); }
    CustomTheme get(const QString &id) const { return entries.value(id); }
    QList<CustomTheme> all() const { return entries.values(); }
    QJsonObject importFile(const QString &path);
    bool remove(const QString &id);
    static QString validate(const QJsonObject &spec);
};
