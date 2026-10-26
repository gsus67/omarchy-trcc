#include "i18n.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "themes.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <cmath>

namespace {
bool validColor(const QString &value) {
    return QStringList{"background","foreground","accent","muted","green"}.contains(value)
        || QRegularExpression("^#[0-9a-fA-F]{6}$").match(value).hasMatch();
}
QJsonObject failure(const QString &message) { return {{"ok",false},{"message",message}}; }
bool loadImage(const QString &path, QImage &image) {
    QFileInfo file(path);
    if(!file.isFile() || file.size()>20*1024*1024) return false;
    QImageReader reader(path);
    if(reader.size()!=QSize(1280,480)) return false;
    image=reader.read();
    return !image.isNull();
}
}

// Theme names are shown in the panel; drop markup and control characters so a
// shared theme cannot carry rich text (remote images, links) into the desktop.
static QString cleanName(QString name) {
    return name.remove(QRegularExpression("[<>&\\x00-\\x1f\\x7f]")).trimmed();
}

QString ThemeStore::validate(const QJsonObject &spec) {
    if(spec["schema"].toInt()!=1 || spec["width"].toInt()!=1280 || spec["height"].toInt()!=480)
        return uiText("El tema debe usar schema 1 y resolución 1280×480.");
    QString name=cleanName(spec["name"].toString());
    if(name.isEmpty() || name.size()>80) return uiText("El nombre del tema debe tener entre 1 y 80 caracteres.");
    if(spec.contains("backgroundColor")&&!validColor(spec["backgroundColor"].toString())) return uiText("Color de fondo no válido.");
    if(spec.contains("elements")&&!spec["elements"].isArray()) return uiText("elements debe ser una lista de textos.");
    auto elements=spec["elements"].toArray();
    if(elements.size()>64) return uiText("Un tema admite hasta 64 elementos.");
    for(auto item:elements) {
        auto element=item.toObject();
        if(!item.isObject() || element["type"].toString()!="text" || !element["text"].isString() || element["text"].toString().size()>512)
            return uiText("Cada elemento debe ser un texto de hasta 512 caracteres.");
        for(const QString &key: {QString("x"),QString("y"),QString("width"),QString("height"),QString("fontSize")}) {
            double value=element[key].toDouble(-1);
            if(!element[key].isDouble()||!std::isfinite(value)||value<0) return uiText("Geometría de texto no válida.");
        }
        double x=element["x"].toDouble(),y=element["y"].toDouble(),w=element["width"].toDouble(),h=element["height"].toDouble(),font=element["fontSize"].toDouble();
        if(w<1||h<1||x+w>1280||y+h>480||font<6||font>200) return uiText("Un texto queda fuera del lienzo o tiene un tamaño no válido.");
        if(element.contains("color")&&!validColor(element["color"].toString())) return uiText("Color de texto no válido.");
        if(element.contains("align")&&!QStringList{"left","center","right"}.contains(element["align"].toString())) return uiText("Alineación no válida.");
    }
    return {};
}

void ThemeStore::initialize(const QString &path) {
    directory=path;
    entries.clear();
    QDir().mkpath(directory);
    for(const QString &id:QDir(directory).entryList({"custom-*"},QDir::Dirs|QDir::NoDotAndDotDot)) {
        if(!QRegularExpression("^custom-[a-f0-9]{16}$").match(id).hasMatch()) continue;
        QString folder=directory+"/"+id;
        QFile file(folder+"/theme.json");
        if(file.size()>256*1024||!file.open(QIODevice::ReadOnly)) continue;
        auto spec=QJsonDocument::fromJson(file.readAll()).object();
        if(!validate(spec).isEmpty()) continue;
        QString background;
        if(spec.contains("background")) {
            if(spec["background"].toString()!="background.png") continue;
            background=folder+"/background.png";
            QImage image; if(!loadImage(background,image)) continue;
        }
        entries.insert(id,{id,cleanName(spec["name"].toString()),background,spec});
    }
}

QJsonObject ThemeStore::importFile(const QString &path) {
    QFileInfo input(path);
    if(!input.isFile()) return failure(uiText("No se encontró el archivo seleccionado."));
    if(input.size()>20*1024*1024) return failure(uiText("El archivo supera 20 MB."));
    QJsonObject spec;
    QImage background;
    QString suffix=input.suffix().toLower();
    if(suffix=="json") {
        if(input.size()>256*1024) return failure(uiText("El JSON supera 256 KB."));
        QFile file(path); if(!file.open(QIODevice::ReadOnly)) return failure(uiText("No se pudo leer el tema."));
        QJsonParseError parseError;
        auto document=QJsonDocument::fromJson(file.readAll(),&parseError);
        if(parseError.error!=QJsonParseError::NoError||!document.isObject()) return failure(uiText("El archivo no contiene un tema JSON válido."));
        spec=document.object();
        QString error=validate(spec); if(!error.isEmpty()) return failure(error);
        spec["name"]=cleanName(spec["name"].toString());
        if(spec.contains("background")) {
            QString relative=spec["background"].toString();
            QString base=input.canonicalPath();
            QFileInfo candidate(base+"/"+relative);
            if(relative.isEmpty()||QDir::isAbsolutePath(relative)||!candidate.canonicalFilePath().startsWith(base+"/")||!loadImage(candidate.canonicalFilePath(),background))
                return failure(uiText("El fondo debe ser una imagen 1280×480 dentro de la carpeta del JSON."));
        }
    } else if(QStringList{"png","jpg","jpeg","webp"}.contains(suffix)) {
        if(!loadImage(input.canonicalFilePath(),background)) return failure(uiText("La imagen debe tener exactamente 1280×480 píxeles."));
        QString imageName=cleanName(input.completeBaseName()).left(80);
        spec={{"schema",1},{"width",1280},{"height",480},{"name",imageName.isEmpty()?QStringLiteral("Imagen"):imageName},{"elements",QJsonArray{}}};
    } else return failure(uiText("Selecciona un tema .json o una imagen PNG/JPG/WebP."));
    if(!background.isNull()) spec["background"]="background.png";
    QByteArray identity=QJsonDocument(spec).toJson(QJsonDocument::Compact);
    if(!background.isNull()) identity+=QCryptographicHash::hash(QByteArray(reinterpret_cast<const char *>(background.constBits()),background.sizeInBytes()),QCryptographicHash::Sha256);
    QString id="custom-"+QString::fromLatin1(QCryptographicHash::hash(identity,QCryptographicHash::Sha256).toHex().left(16));
    if(entries.contains(id)) return {{"ok",true},{"message",uiText("Este tema ya está en la biblioteca.")},{"themeId",id}};
    QString folder=directory+"/"+id;
    if(!QDir().mkpath(folder)) return failure(uiText("No se pudo crear la carpeta del tema."));
    auto cleanup=[&] { QDir(folder).removeRecursively(); return failure(uiText("No se pudo guardar el tema.")); };
    if(!background.isNull()&&!background.save(folder+"/background.png","PNG")) return cleanup();
    QSaveFile file(folder+"/theme.json");
    if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(spec).toJson())<0||!file.commit()) return cleanup();
    entries.insert(id,{id,cleanName(spec["name"].toString()),background.isNull()?QString():folder+"/background.png",spec});
    return {{"ok",true},{"message",uiText("Tema añadido. Selecciónalo para verlo y pulsa Aplicar.")},{"themeId",id}};
}

bool ThemeStore::remove(const QString &id) {
    if(!entries.contains(id)) return false;
    if(!QDir(directory+"/"+id).removeRecursively()) return false;
    entries.remove(id);
    return true;
}
