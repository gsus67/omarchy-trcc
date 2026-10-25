// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

inline QString uiText(const char *source) {
    QString locale=qEnvironmentVariable("LC_ALL");
    if(locale.isEmpty()) locale=qEnvironmentVariable("LC_MESSAGES");
    if(locale.isEmpty()) locale=qEnvironmentVariable("LANG","en");
    if(locale.toLower().startsWith("es_")||locale.toLower().startsWith("es-")||locale.toLower().section('.',0,0)=="es") return QString::fromUtf8(source);
    static const QJsonObject english=[] {
        QFile file(":/i18n/messages.json");
        if(!file.open(QIODevice::ReadOnly)) return QJsonObject{};
        return QJsonDocument::fromJson(file.readAll()).object()["en"].toObject();
    }();
    QString key=QString::fromUtf8(source);
    return english[key].toString(key);
}
