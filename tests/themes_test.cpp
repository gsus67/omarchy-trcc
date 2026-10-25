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
    QTextStream(stdout)<<(ok?"PASS: import, duplicate detection, persistence, geometry, path boundaries, owned copies and removal\n":"FAIL\n");
    return ok?0:1;
}
