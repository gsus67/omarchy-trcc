// SPDX-License-Identifier: GPL-3.0-or-later
#include "themes.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir temp;
    QString source=temp.path()+"/source";
    QDir().mkpath(source);
    ThemeStore store; store.initialize(temp.path()+"/library");
    QJsonObject spec{{"schema",1},{"name","Mi tema"},{"width",1280},{"height",480},
        {"elements",QJsonArray{QJsonObject{{"type","text"},{"text","CPU {cpu_temp}"},{"x",10},{"y",10},{"width",600},{"height",100},{"fontSize",40},{"color","accent"}}}}};
    auto write=[&](const QJsonObject &value) {
        QFile file(source+"/theme.json");
        return file.open(QIODevice::WriteOnly|QIODevice::Truncate)&&file.write(QJsonDocument(value).toJson())>0;
    };
    bool ok=write(spec);
    auto imported=store.importFile(source+"/theme.json");
    QString id=imported["themeId"].toString();
    ok=ok&&imported["ok"].toBool()&&store.contains(id);
    ok=ok&&store.importFile(source+"/theme.json")["themeId"].toString()==id&&store.all().size()==1;
    ThemeStore reloaded; reloaded.initialize(temp.path()+"/library");
    ok=ok&&reloaded.contains(id);
    spec["width"]=1920; ok=write(spec)&&ok;
    ok=ok&&!store.importFile(source+"/theme.json")["ok"].toBool()&&store.all().size()==1;
    spec["width"]=1280;
    QImage image(1280,480,QImage::Format_RGB32); image.fill(Qt::blue);
    ok=image.save(temp.path()+"/outside.png")&&ok;
    spec["background"]="../outside.png"; ok=write(spec)&&ok;
    ok=ok&&!store.importFile(source+"/theme.json")["ok"].toBool();
    spec["background"]=temp.path()+"/outside.png"; ok=write(spec)&&ok;
    ok=ok&&!store.importFile(source+"/theme.json")["ok"].toBool();
    spec["background"]="background.png";
    ok=image.save(source+"/background.png")&&write(spec)&&ok;
    auto backed=store.importFile(source+"/theme.json");
    ok=ok&&backed["ok"].toBool();
    QString copied=store.get(backed["themeId"].toString()).backgroundPath;
    QFile::remove(source+"/background.png");
    ok=ok&&QFile::exists(copied)&&!QImage(copied).isNull();
    ok=ok&&!store.remove("../../source")&&store.remove(id)&&QFile::exists(source+"/theme.json");
    QImage wrong(320,240,QImage::Format_RGB32); wrong.fill(Qt::red);
    ok=wrong.save(source+"/wrong.png")&&ok;
    ok=ok&&!store.importFile(source+"/wrong.png")["ok"].toBool();
    // Names are shown in the panel: no markup survives import or reload.
    spec.remove("background");
    spec["name"]="<img src=\"http://example.invalid/x.png\">Remoto"; ok=write(spec)&&ok;
    auto marked=store.importFile(source+"/theme.json");
    QString markedName=store.get(marked["themeId"].toString()).name;
    ok=ok&&marked["ok"].toBool()&&!markedName.contains('<')&&!markedName.contains('>')&&markedName.endsWith("Remoto");
    spec["name"]="<>&"; ok=write(spec)&&ok;
    ok=ok&&!store.importFile(source+"/theme.json")["ok"].toBool();
    ok=image.save(source+"/<b>Foto<b>.png")&&ok;
    auto photo=store.importFile(source+"/<b>Foto<b>.png");
    ok=ok&&photo["ok"].toBool()&&store.get(photo["themeId"].toString()).name=="bFotob";
    ok=image.save(source+"/<>.png")&&ok;
    auto unnamed=store.importFile(source+"/<>.png");
    ok=ok&&unnamed["ok"].toBool()&&store.get(unnamed["themeId"].toString()).name=="Imagen";
    // A theme saved before this check is cleaned when the library loads.
    QString stored=temp.path()+"/library/"+marked["themeId"].toString()+"/theme.json";
    QFile storedFile(stored); ok=storedFile.open(QIODevice::ReadOnly)&&ok;
    QJsonObject storedSpec=QJsonDocument::fromJson(storedFile.readAll()).object(); storedFile.close();
    storedSpec["name"]="<a href=\"http://example.invalid\">Viejo</a>";
    ok=storedFile.open(QIODevice::WriteOnly|QIODevice::Truncate)&&storedFile.write(QJsonDocument(storedSpec).toJson())>0&&ok; storedFile.close();
    ThemeStore legacy; legacy.initialize(temp.path()+"/library");
    QString legacyName=legacy.get(marked["themeId"].toString()).name;
    ok=ok&&legacy.contains(marked["themeId"].toString())&&!legacyName.contains('<')&&legacyName.contains("Viejo");
    QTextStream(stdout)<<(ok?"PASS: import, duplicate detection, persistence, geometry, path boundaries, owned copies, removal and plain-text names\n":"FAIL\n");
    return ok?0:1;
}
