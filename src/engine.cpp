#include "i18n.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
// HID Type 2 protocol attribution: see NOTICE.md.
#include <QGuiApplication>
#include <QBuffer>
#include <QDateTime>
#include <QLocale>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtEndian>
#include <QUrl>
#include <libusb.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include "themes.hpp"

static constexpr int W = 1280, H = 480;
static const QStringList layouts = {"focus", "terminal", "rings", "dots"};
static const QStringList visualIds = {"bars", "wave", "rings", "particles"};
static QString readText(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}
// The plugin version from its manifest, so the LCD never shows a stale one.
static QString pluginVersion() {
    QString dir = qEnvironmentVariable("OMARCHY_TRCC_PLUGIN_DIR");
    if (dir.isEmpty()) return {};
    return QJsonDocument::fromJson(readText(dir + "/manifest.json").toUtf8()).object()["version"].toString();
}
static QByteArray framePacket(const QByteArray &jpeg) {
    QByteArray out(20, '\0');
    out.replace(0, 4, QByteArray::fromHex("dadbdcdd"));
    out[4] = 2;
    qToLittleEndian<quint16>(W, out.data() + 8);
    qToLittleEndian<quint16>(H, out.data() + 10);
    out[12] = 2;
    qToLittleEndian<quint32>(jpeg.size(), out.data() + 16);
    out += jpeg;
    out.resize(((out.size() + 511) / 512) * 512, '\0');
    return out;
}

class Usb {
    libusb_context *context = nullptr;
    libusb_device_handle *handle = nullptr;
    int interface = 0;
    unsigned char epIn = 0x83, epOut = 0x02;
    bool interruptIn = true, interruptOut = true;
    int transfer(unsigned char ep, unsigned char *data, int size, int *sent, unsigned timeout) {
        bool interrupt = (ep & 0x80) ? interruptIn : interruptOut;
        return interrupt ? libusb_interrupt_transfer(handle, ep, data, size, sent, timeout)
                         : libusb_bulk_transfer(handle, ep, data, size, sent, timeout);
    }
public:
    QString error;
    int pm = 0, sub = 0;
    bool connected() const { return handle != nullptr; }
    Usb() { libusb_init(&context); }
    ~Usb() { close(); if (context) libusb_exit(context); }
    void close() {
        if (handle) { libusb_release_interface(handle, interface); libusb_close(handle); }
        handle = nullptr;
    }
    bool open() {
        close();
        if (!context) { error = uiText("No se pudo iniciar libusb"); return false; }
        libusb_device **devices = nullptr;
        ssize_t count = libusb_get_device_list(context, &devices);
        if (count < 0) { error = libusb_error_name(int(count)); return false; }
        int rc = LIBUSB_ERROR_NO_DEVICE;
        for (ssize_t i = 0; i < count; ++i) {
            libusb_device_descriptor descriptor{};
            libusb_get_device_descriptor(devices[i], &descriptor);
            if (descriptor.idVendor != 0x0416 || descriptor.idProduct != 0x5302) continue;
            rc = libusb_open(devices[i], &handle);
            if (rc != 0) break;
            libusb_config_descriptor *config = nullptr;
            if (libusb_get_active_config_descriptor(devices[i], &config) == 0) {
                bool found = false;
                for (int n = 0; n < config->bNumInterfaces && !found; ++n) {
                    const auto &iface = config->interface[n];
                    for (int a = 0; a < iface.num_altsetting && !found; ++a) {
                        const auto &alt = iface.altsetting[a];
                        for (int e = 0; e < alt.bNumEndpoints; ++e) {
                            const auto &endpoint = alt.endpoint[e];
                            if (endpoint.bEndpointAddress == 0x02) {
                                interface = alt.bInterfaceNumber;
                                interruptOut = (endpoint.bmAttributes & 3) == LIBUSB_TRANSFER_TYPE_INTERRUPT;
                                for (int j = 0; j < alt.bNumEndpoints; ++j) {
                                    const auto &in = alt.endpoint[j];
                                    if (in.bEndpointAddress & 0x80) {
                                        epIn = in.bEndpointAddress;
                                        interruptIn = (in.bmAttributes & 3) == LIBUSB_TRANSFER_TYPE_INTERRUPT;
                                    }
                                }
                                found = true;
                                break;
                            }
                        }
                    }
                }
                libusb_free_config_descriptor(config);
            }
            libusb_set_auto_detach_kernel_driver(handle, 1);
            rc = libusb_claim_interface(handle, interface);
            break;
        }
        libusb_free_device_list(devices, 1);
        if (rc != 0 || !handle) {
            error = rc == LIBUSB_ERROR_ACCESS ? uiText("Sin permiso USB: ejecuta setup.sh") : QString::fromLatin1(libusb_error_name(rc));
            close(); return false;
        }
        QByteArray init(512, '\0'); init.replace(0, 4, QByteArray::fromHex("dadbdcdd")); init[12] = 1;
        for (int attempt = 0; attempt < 3; ++attempt) {
            int sent = 0;
            rc = transfer(epOut, reinterpret_cast<unsigned char *>(init.data()), init.size(), &sent, 1500);
            if (rc != 0) continue;
            QByteArray reply(512, '\0'); int got = 0;
            rc = transfer(epIn, reinterpret_cast<unsigned char *>(reply.data()), reply.size(), &got, 1500);
            if (rc == 0 && got >= 20 && reply.left(4) == init.left(4) && reply[12] == 1) {
                pm = static_cast<unsigned char>(reply[5]); sub = static_cast<unsigned char>(reply[4]);
                if (pm != 128 && pm != 68) {
                    error = QString(uiText("Panel PM=%1 SUB=%2 no compatible: se requiere Trofeo 1280×480")).arg(pm).arg(sub);
                    close(); return false;
                }
                error.clear(); return true;
            }
        }
        error = uiText("La Trofeo no respondió al handshake; comprueba que otro controlador no esté usándola");
        close(); return false;
    }
    bool send(const QImage &image, int orientation) {
        if (!handle) return false;
        int angle = ((sub == 2 ? 90 : 0) - orientation + 360) % 360;
        QImage wire = angle ? image.transformed(QTransform().rotate(angle)) : image;
        QByteArray jpeg; QBuffer buffer(&jpeg); buffer.open(QIODevice::WriteOnly);
        if (!wire.save(&buffer, "JPG", 82)) { error = uiText("No hay codificador JPEG disponible"); return false; }
        QByteArray packet = framePacket(jpeg);
        for (int pos = 0; pos < packet.size(); pos += 512) {
            int sent = 0;
            int rc = transfer(epOut, reinterpret_cast<unsigned char *>(packet.data() + pos), 512, &sent, 1500);
            if (rc != 0 || sent != 512) {
                error = QString(uiText("Error de envío USB: %1")).arg(libusb_error_name(rc));
                close(); return false;
            }
        }
        return true;
    }
};

// NVIDIA's proprietary driver has no hwmon power node. NVML ships with the
// nvidia-utils package that Omarchy installs for NVIDIA GPUs and is readable
// by any user (it is what nvidia-smi uses); it is loaded only when present.
struct Nvml {
    struct Utilization { unsigned gpu, memory; };
    using Init = int (*)();
    using Count = int (*)(unsigned *);
    using Handle = int (*)(unsigned, void **);
    using Reading = int (*)(void *, unsigned *);
    using Temperature = int (*)(void *, int, unsigned *);
    using Busy = int (*)(void *, Utilization *);
    QLibrary library;
    Init init = nullptr, shutdown = nullptr; Count count = nullptr; Handle handle = nullptr;
    Reading power = nullptr; Temperature temperature = nullptr; Busy busy = nullptr;
    bool tried = false;
    bool load(const QString &path) {
        if (tried) return init;
        tried = true; library.setFileName(path);
        if (!library.load()) return false;
        init = reinterpret_cast<Init>(library.resolve("nvmlInit_v2"));
        shutdown = reinterpret_cast<Init>(library.resolve("nvmlShutdown"));
        count = reinterpret_cast<Count>(library.resolve("nvmlDeviceGetCount_v2"));
        handle = reinterpret_cast<Handle>(library.resolve("nvmlDeviceGetHandleByIndex_v2"));
        power = reinterpret_cast<Reading>(library.resolve("nvmlDeviceGetPowerUsage"));
        temperature = reinterpret_cast<Temperature>(library.resolve("nvmlDeviceGetTemperature"));
        busy = reinterpret_cast<Busy>(library.resolve("nvmlDeviceGetUtilizationRates"));
        if (!init || !shutdown || !count || !handle || !power) init = nullptr;
        return init;
    }
};

struct Metrics {
    struct Gpu { double watts = -1, temp = -1, busy = -1; };
    double cpu = 0, gpu = -1, cpuTemp = -1, gpuTemp = -1, ramTemp = -1, ram = 0, ramUsedGiB = 0, ramTotalGiB = 0;
    // Watts; -1 = no sensor. powerSource: "psu", "battery", "components" or "".
    double cpuPower = -1, gpuPower = -1, power = -1;
    QString powerSource; bool powerPartial = false;
    QString sysRoot = "/sys", procRoot = "/proc", runRoot = "/run", nvmlLibrary = "libnvidia-ml.so.1";
    quint64 previousTotal = 0, previousIdle = 0;
    // Energy counters (µJ) by path, to turn them into watts between samples.
    QHash<QString, QPair<qint64, qint64>> energy;
    Nvml nvml; QList<Gpu> nvidia; qint64 nvidiaAt = 0;
    // hwmon powerN_* values are in microwatts; amdgpu exposes power1_average
    // on older GPUs and power1_input on newer ones.
    static double hwmonPower(const QString &path) {
        for (const QString &kind : {"/power1_average", "/power1_input"}) {
            QString value = readText(path + kind);
            if (!value.isEmpty()) return value.toDouble() / 1e6;
        }
        return -1;
    }
    double energyPower(const QString &file, qint64 nowMs, qint64 range = 0) {
        QString value = readText(file);
        if (value.isEmpty()) { energy.remove(file); return -1; }
        qint64 microjoules = value.toLongLong(); double watts = -1;
        if (energy.contains(file) && nowMs > energy[file].second) {
            qint64 delta = microjoules - energy[file].first;
            if (delta < 0 && range > 0) delta += range;
            if (delta >= 0) watts = delta / 1000.0 / double(nowMs - energy[file].second);
        }
        energy[file] = {microjoules, nowMs};
        return watts;
    }
    QList<Gpu> nvidiaGpus(qint64 nowMs) {
        // Only for a GPU bound to the proprietary driver; nouveau uses hwmon.
        QString pci = sysRoot + "/bus/pci/devices";
        bool present = false, autosuspend = false, asleep = false;
        for (const QString &device : QDir(pci).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString path = pci + "/" + device;
            if (readText(path + "/vendor") != "0x10de" || !readText(path + "/class").startsWith("0x03")) continue;
            if (QFileInfo(QFileInfo(path + "/driver").symLinkTarget()).fileName() != "nvidia") continue;
            present = true;
            if (readText(path + "/power/control") == "auto") autosuspend = true;
            if (readText(path + "/power/runtime_status") == "suspended") asleep = true;
        }
        if (!present) return {};
        // A sleeping laptop GPU draws ~0 W; querying it would wake it, so
        // report 0 and leave it asleep. With runtime power management, read
        // every 10 s so polling never keeps the GPU awake.
        if (asleep) { nvidia = {Gpu{0, -1, 0}}; nvidiaAt = nowMs; return nvidia; }
        if (autosuspend && nowMs - nvidiaAt < 10000 && !nvidia.isEmpty()) return nvidia;
        nvidiaAt = nowMs; nvidia.clear();
        if (!nvml.load(nvmlLibrary) || nvml.init() != 0) return {};
        unsigned total = 0; nvml.count(&total);
        for (unsigned i = 0; i < total; ++i) {
            void *device = nullptr; unsigned milliwatts = 0, celsius = 0; Nvml::Utilization use{0, 0};
            if (nvml.handle(i, &device) != 0) continue;
            Gpu reading;
            if (nvml.power(device, &milliwatts) == 0) reading.watts = milliwatts / 1000.0;
            if (nvml.temperature && nvml.temperature(device, 0, &celsius) == 0) reading.temp = celsius;
            if (nvml.busy && nvml.busy(device, &use) == 0) reading.busy = use.gpu;
            nvidia << reading;
        }
        // Closing the session releases the device so it can power down.
        nvml.shutdown();
        return nvidia;
    }
    void sample(qint64 nowMs = QDateTime::currentMSecsSinceEpoch()) {
        auto fields = readText(procRoot + "/stat").section('\n', 0, 0).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        quint64 total = 0;
        for (int i = 1; i < std::min(9, int(fields.size())); ++i) total += fields[i].toULongLong();
        quint64 idle = fields.value(4).toULongLong() + fields.value(5).toULongLong();
        if (previousTotal && total > previousTotal) cpu = 100.0 * (1.0 - double(idle - previousIdle) / double(total - previousTotal));
        previousTotal = total; previousIdle = idle;
        QString memory = readText(procRoot + "/meminfo");
        QRegularExpression available("MemAvailable:\\s+(\\d+)"), memTotal("MemTotal:\\s+(\\d+)");
        double t = memTotal.match(memory).captured(1).toDouble(), a = available.match(memory).captured(1).toDouble();
        ram = t > 0 ? (t-a)*100/t : 0;
        ramTotalGiB=t/1048576.0; ramUsedGiB=(t-a)/1048576.0;
        cpuTemp = -1; ramTemp = -1;
        double psuPower = -1;
        QList<Gpu> gpus;
        QString hwmonDir = sysRoot + "/class/hwmon";
        for (const QString &name : QDir(hwmonDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString path = hwmonDir + "/" + name;
            QString driver = readText(path + "/name");
            if (driver == "coretemp" || driver == "k10temp") {
                double temp = readText(path + "/temp1_input").toDouble() / 1000;
                if (temp > 0) cpuTemp = std::max(cpuTemp, temp);
            }
            if (driver == "amdgpu" || driver == "nouveau" || driver == "i915" || driver == "xe") {
                Gpu reading;
                reading.watts = hwmonPower(path);
                // Intel discrete GPUs publish an energy counter instead of power.
                if (reading.watts < 0 && (driver == "i915" || driver == "xe")) reading.watts = energyPower(path + "/energy1_input", nowMs);
                for (const QString &file : {"/temp1_input", "/temp2_input"}) {
                    QString value = readText(path + file);
                    if (!value.isEmpty()) { reading.temp = value.toDouble() / 1000; break; }
                }
                QString busy = readText(path + "/device/gpu_busy_percent");
                if (!busy.isEmpty()) reading.busy = busy.toDouble();
                gpus << reading;
            }
            // Corsair HXi/RMi report the real delivered power over USB.
            if (driver == "corsairpsu" && psuPower < 0) psuPower = hwmonPower(path);
            if(driver=="spd5118"||driver=="jc42") {
                QString value=readText(path+"/temp1_input");
                if(!value.isEmpty()) ramTemp=std::max(ramTemp,value.toDouble()/1000.0);
            }
        }
        gpus << nvidiaGpus(nowMs);
        // Every GPU counts towards the total; temperature and load come from
        // the one drawing the most, so they describe the same card.
        gpuPower = -1; gpuTemp = -1; gpu = -1;
        const Gpu *busiest = nullptr;
        for (const Gpu &reading : gpus) {
            if (reading.watts >= 0) gpuPower = std::max(0.0, gpuPower) + reading.watts;
            if (!busiest || reading.watts > busiest->watts) busiest = &reading;
        }
        if (busiest) { gpuTemp = busiest->temp; gpu = busiest->busy; }
        if (gpu < 0)
            for (const QString &card : QDir(sysRoot + "/class/drm").entryList({"card*"}, QDir::Dirs)) {
                QString value = readText(sysRoot + "/class/drm/" + card + "/device/gpu_busy_percent");
                if (!value.isEmpty()) { gpu = value.toDouble(); break; }
            }
        // CPU package power. setup.sh can install omarchy-trcc-power.service,
        // which publishes whole watts from the root-only RAPL counter; a value
        // older than 5 s means the service stopped. Otherwise read the counter
        // directly, which only works where the system already allows it.
        QString relay = runRoot + "/omarchy-trcc-power/cpu-watts";
        QFileInfo relayInfo(relay);
        QString relayed = relayInfo.exists() && relayInfo.lastModified().msecsTo(QDateTime::currentDateTime()) < 5000 ? readText(relay) : QString();
        bool relayOk = false; int relayWatts = relayed.toInt(&relayOk);
        QString rapl = sysRoot + "/class/powercap/intel-rapl:0";
        double direct = energyPower(rapl + "/energy_uj", nowMs, readText(rapl + "/max_energy_range_uj").toLongLong());
        cpuPower = relayOk && relayWatts >= 0 ? relayWatts : direct;
        // A laptop battery only measures the whole system while discharging.
        double batteryPower = -1;
        QString supplyDir = sysRoot + "/class/power_supply";
        for (const QString &name : QDir(supplyDir).entryList({"BAT*"}, QDir::Dirs)) {
            QString path = supplyDir + "/" + name;
            if (readText(path + "/status") != "Discharging") continue;
            QString now = readText(path + "/power_now");
            double watts = !now.isEmpty() ? now.toDouble() / 1e6
                : readText(path + "/current_now").toDouble() * readText(path + "/voltage_now").toDouble() / 1e12;
            if (watts > 0) batteryPower = std::max(0.0, batteryPower) + watts;
        }
        powerPartial = false;
        if (psuPower > 0) { power = psuPower; powerSource = "psu"; }
        else if (batteryPower > 0) { power = batteryPower; powerSource = "battery"; }
        else if (cpuPower >= 0 || gpuPower >= 0) {
            power = std::max(0.0, cpuPower) + std::max(0.0, gpuPower);
            powerSource = "components"; powerPartial = cpuPower < 0 || gpuPower < 0;
        } else { power = -1; powerSource.clear(); }
    }
    // "45", "≥45" when a component is missing, "—" without sensors.
    QString powerText() const {
        if (power < 0) return QString::fromUtf8("—");
        return (powerPartial ? QString::fromUtf8("≥") : QString()) + QString::number(qRound(power));
    }
};

struct Palette {
    QColor background = QColor("#1a1b26"), foreground = QColor("#a9b1d6"), accent = QColor("#7aa2f7"), muted = QColor("#565f89"), green = QColor("#9ece6a");
    QString name = "Omarchy", font = "monospace";
    void load() {
        QString base = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + "/omarchy/current/";
        QString data = readText(base + "theme/colors.toml");
        auto color = [&](const QString &key, QColor fallback) {
            auto match = QRegularExpression("(?:^|\\n)" + key + "\\s*=\\s*[\"'](#[A-Fa-f0-9]{6})[\"']").match(data);
            return match.hasMatch() ? QColor(match.captured(1)) : fallback;
        };
        background = color("background", background); foreground = color("foreground", foreground);
        accent = color("accent", accent); muted = color("dark_foreground", muted); green = color("green", green);
        QString current = readText(base + "theme.name"); if (!current.isEmpty()) name = current;
        QString configured = readText(QDir::homePath() + "/.config/omarchy/fonts/mono");
        if (!configured.isEmpty()) font = configured;
    }
};

class Engine {
public:
    Usb usb;
    Metrics metrics;
    Palette palette;
    ThemeStore customThemes;
    QJsonObject settings;
    QString stateDir, socketPath;
    QLocalServer server;
    QProcess audio;
    QByteArray samples;
    std::array<double, 32> levels{};
    double phase = 0;
    qint64 lastSample = 0, lastConnect = 0, lastFrame = 0, lastAudioRetry = 0;
    QString audioError;
    quint64 framesSent = 0;
    bool offline;
    Engine(bool off = false) : offline(off) {
        stateDir = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + "/omarchy-trcc";
        QDir().mkpath(stateDir + "/previews");
        customThemes.initialize(stateDir+"/themes");
        socketPath = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/omarchy-trcc.sock";
        settings = {{"brightness", 100}, {"orientation", 0}, {"layout", "focus"}, {"visual", ""},
                    {"overlay", true}, {"game", false}, {"followAccent", true}, {"visualHue", 225}};
        QJsonDocument stored = QJsonDocument::fromJson(readText(stateDir + "/settings.json").toUtf8());
        QJsonObject existing = stored.object();
        for (auto it = existing.begin(); it != existing.end(); ++it) if (settings.contains(it.key())) settings[it.key()] = it.value();
        if (!layouts.contains(settings["layout"].toString())&&!customThemes.contains(settings["layout"].toString())) settings["layout"] = "focus";
        if (!visualIds.contains(settings["visual"].toString())) settings["visual"] = "";
        settings["brightness"] = std::clamp(settings["brightness"].toInt(100), 0, 100);
        if (!QList<int>{0, 90, 180, 270}.contains(settings["orientation"].toInt())) settings["orientation"] = 0;
        palette.load(); metrics.sample();
        QObject::connect(&audio, &QProcess::readyReadStandardOutput, [&] {
            samples += audio.readAllStandardOutput();
            if (samples.size() > 32768) samples = samples.right(32768);
        });
        QObject::connect(&audio, &QProcess::readyReadStandardError, [&] { audioError = QString::fromUtf8(audio.readAllStandardError()).trimmed(); });
        QObject::connect(&audio, &QProcess::errorOccurred, [&](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) audioError = uiText("No se pudo iniciar parec: ") + audio.errorString();
        });
    }
    ~Engine() { audio.kill(); audio.waitForFinished(1000); }
    void save() {
        QSaveFile file(stateDir + "/settings.json");
        if (file.open(QIODevice::WriteOnly)) { file.write(QJsonDocument(settings).toJson()); file.commit(); }
    }
    QString visual() const { return settings["visual"].toString(); }
    void startAudio() {
        if (visual().isEmpty() || audio.state() != QProcess::NotRunning || offline) return;
        QProcess sink;
        sink.start("pactl", {"get-default-sink"});
        if (!sink.waitForFinished(1000) || sink.exitCode() != 0) { audioError = uiText("No se encontró la salida de audio de PulseAudio/PipeWire"); return; }
        QString target = QString::fromUtf8(sink.readAllStandardOutput()).trimmed() + ".monitor";
        audioError.clear(); samples.clear();
        audio.start("parec", {"--device=" + target, "--format=float32le", "--rate=22050", "--channels=1", "--raw"});
    }
    void analyze() {
        constexpr int count = 1024;
        if (samples.size() < count * 4) { for (double &x : levels) x *= .78; return; }
        QByteArray block = samples.right(count * 4); samples.clear();
        std::array<double, count> values{};
        for (int n = 0; n < count; ++n) {
            float sample; std::memcpy(&sample, block.constData() + n * 4, 4);
            values[n] = std::isfinite(sample) ? sample * (.5 - .5*std::cos(2*M_PI*n/(count-1))) : 0;
        }
        for (int b = 0; b < 32; ++b) {
            int bin = std::clamp(int(std::pow(400.0, b/31.0)), 1, 400);
            double real = 0, imaginary = 0;
            for (int n = 0; n < count; ++n) {
                double angle = 2*M_PI*bin*n/count;
                real += values[n]*std::cos(angle); imaginary -= values[n]*std::sin(angle);
            }
            double magnitude = std::clamp(std::log1p(std::hypot(real,imaginary)*3)/5.0, 0.0, 1.0);
            levels[b] = std::max(magnitude, levels[b]*.75);
        }
    }
    static QString temperature(double value) { return value < 0 ? "—" : QString::number(qRound(value)) + "°"; }
    void text(QPainter &p, QRectF rect, const QString &value, int size, QColor color, bool bold = false, int alignment = Qt::AlignLeft | Qt::AlignVCenter) {
        QFont font(palette.font); font.setPixelSize(size); font.setBold(bold);
        p.setFont(font); p.setPen(color); p.drawText(rect, alignment, value);
    }
    // The rows every built-in layout shows: temperature as the value, load
    // and watts (RAM: use and size) beside it. ring = load for the rings layout.
    struct Field { QString label, value, detail; double ring; };
    QList<Field> fieldData() const {
        auto watts = [](double value) { return value < 0 ? QString() : QString::number(qRound(value)) + " W"; };
        auto join = [](QStringList parts) { parts.removeAll(QString()); return parts.join(QString::fromUtf8("  ·  ")); };
        QString gpuLoad = metrics.gpu < 0 ? uiText("SIN SENSOR DE CARGA") : uiText("%1% CARGA").arg(qRound(metrics.gpu));
        QString ramSize = metrics.ramTotalGiB <= 0 ? QString() : QString("%1 / %2 GiB").arg(metrics.ramUsedGiB,0,'f',1).arg(metrics.ramTotalGiB,0,'f',1);
        // Without a RAM sensor its use takes the value slot.
        bool ramSensor = metrics.ramTemp >= 0;
        return {
            {"CPU", temperature(metrics.cpuTemp), join({uiText("%1% CARGA").arg(qRound(metrics.cpu)), watts(metrics.cpuPower)}), metrics.cpu},
            {"GPU", temperature(metrics.gpuTemp), join({gpuLoad, watts(metrics.gpuPower)}), std::max(0., metrics.gpu)},
            {"RAM", ramSensor ? temperature(metrics.ramTemp) : QString::number(qRound(metrics.ram)) + "%",
                join({ramSensor ? uiText("%1% EN USO").arg(qRound(metrics.ram)) : QString(), ramSize}), metrics.ram}};
    }
    // Total watts and date for the header, next to the time.
    QString headerInfo() const {
        QString date = QLocale::system().toString(QDate::currentDate(), "dd MMM yyyy");
        return metrics.power < 0 ? date : metrics.powerText() + " W  ·  " + date;
    }
    QImage render(const QString &layout, const QString &audioVisual, bool preview = false) {
        int orientation = preview ? 0 : settings["orientation"].toInt();
        bool portrait = orientation == 90 || orientation == 270;
        QImage image(portrait ? H : W, portrait ? W : H, QImage::Format_RGB32);
        image.fill(palette.background);
        QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing);
        // Draw on a fixed design coordinate system; portrait stays readable
        // using a stacked layout before wire rotation.
        if (audioVisual.isEmpty()&&customThemes.contains(layout)) {
            CustomTheme theme=customThemes.get(layout);
            auto color=[&](QString role) {
                if(role=="background") return palette.background;
                if(role=="foreground") return palette.foreground;
                if(role=="accent") return palette.accent;
                if(role=="muted") return palette.muted;
                if(role=="green") return palette.green;
                return QColor(role);
            };
            painter.fillRect(image.rect(),color(theme.spec["backgroundColor"].toString("background")));
            painter.scale(image.width()/double(W),image.height()/double(H));
            if(!theme.backgroundPath.isEmpty()) painter.drawImage(QRect(0,0,W,H),QImage(theme.backgroundPath));
            const QMap<QString,QString> tokens={
                {"cpu_temp",metrics.cpuTemp<0?"—":QString::number(qRound(metrics.cpuTemp))},
                {"gpu_temp",metrics.gpuTemp<0?"—":QString::number(qRound(metrics.gpuTemp))},
                {"cpu_load",QString::number(qRound(metrics.cpu))},
                {"gpu_load",metrics.gpu<0?"—":QString::number(qRound(metrics.gpu))},
                {"ram",QString::number(qRound(metrics.ram))},
                {"ram_temp",metrics.ramTemp<0?"—":QString::number(qRound(metrics.ramTemp))},
                {"cpu_power",metrics.cpuPower<0?"—":QString::number(qRound(metrics.cpuPower))},
                {"gpu_power",metrics.gpuPower<0?"—":QString::number(qRound(metrics.gpuPower))},
                {"power",metrics.powerText()},
                {"time",QTime::currentTime().toString("HH:mm")},
                {"date",QLocale::system().toString(QDate::currentDate(),"dd MMM yyyy")},{"theme",palette.name}};
            for(auto item:theme.spec["elements"].toArray()) {
                QJsonObject e=item.toObject(); QString value=e["text"].toString();
                for(auto it=tokens.begin();it!=tokens.end();++it) value.replace("{"+it.key()+"}",it.value());
                int align=Qt::AlignVCenter;
                QString horizontal=e["align"].toString("left");
                align|=horizontal=="center"?Qt::AlignHCenter:(horizontal=="right"?Qt::AlignRight:Qt::AlignLeft);
                text(painter,{e["x"].toDouble(),e["y"].toDouble(),e["width"].toDouble(),e["height"].toDouble()},value,e["fontSize"].toInt(),color(e["color"].toString("foreground")),e["bold"].toBool(),align);
            }
            painter.resetTransform();
        } else if (portrait) {
            text(painter, {30,25,420,60}, "TROFEO", 34, palette.accent, true);
            text(painter, {30,25,420,60}, QTime::currentTime().toString("HH:mm"), 34, palette.foreground, true, Qt::AlignRight|Qt::AlignVCenter);
            text(painter, {30,85,420,40}, palette.name + "  ·  " + headerInfo(), 20, palette.muted);
            const QList<Field> data = fieldData();
            for (int i = 0; i < data.size(); ++i) {
                double y = 170 + i * 350;
                text(painter, {30,y,420,45}, data[i].label, 27, palette.accent);
                text(painter, {30,y+45,420,140}, data[i].value, 92, palette.foreground, true);
                text(painter, {30,y+190,420,40}, data[i].detail, 20, palette.muted);
            }
        } else if (!audioVisual.isEmpty()) {
            QColor accent = settings["followAccent"].toBool() ? palette.accent : QColor::fromHsv(settings["visualHue"].toInt(), 170, 240);
            double power = *std::max_element(levels.begin(), levels.end());
            painter.setPen(Qt::NoPen);
            if (audioVisual == "bars") {
                for (int i = 0; i < 32; ++i) {
                    double height = 8 + levels[i]*300;
                    QColor c = accent; c.setAlphaF(.4+.6*i/31.0); painter.setBrush(c);
                    painter.drawRoundedRect(QRectF(40+i*37.5, 395-height, 25, height), 5, 5);
                }
            } else if (audioVisual == "wave") {
                QPainterPath line;
                for (int x = 0; x <= 1200; x += 5) {
                    int band = std::min(31, x*32/1201);
                    double y = 240+std::sin(x*.018+phase*3)*levels[band]*150;
                    if (x == 0) line.moveTo(40+x,y); else line.lineTo(40+x,y);
                }
                painter.setPen(QPen(accent, 6)); painter.drawPath(line);
            } else if (audioVisual == "rings") {
                painter.setBrush(Qt::NoBrush);
                for (int r = 0; r < 7; ++r) {
                    double radius = 50+r*20+levels[r*4]*40;
                    QColor c = accent; c.setAlphaF(1-r*.12);
                    painter.setPen(QPen(c, 4+levels[r*4]*8));
                    painter.drawEllipse(QPointF(640,240),radius,radius);
                }
            } else if (audioVisual == "particles") {
                for (int i = 0; i < 90; ++i) {
                    double angle = i*2.39996+phase*.2;
                    double radius = 25+(i%30)*8+levels[i%32]*85;
                    QColor c = accent; c.setAlphaF(.3+.7*((i%13)/12.0)); painter.setBrush(c);
                    painter.drawEllipse(QPointF(640+std::cos(angle)*radius*2,240+std::sin(angle)*radius*.65),3+power*8,3+power*8);
                }
            }
            if (settings["overlay"].toBool()) {
                text(painter,{40,15,850,50},"CPU "+temperature(metrics.cpuTemp)+"   GPU "+temperature(metrics.gpuTemp)+"   RAM "+QString::number(qRound(metrics.ram))+"%"+(metrics.power<0?QString():"   "+metrics.powerText()+" W"),25,palette.foreground);
                text(painter,{930,15,310,50},QTime::currentTime().toString("HH:mm"),30,palette.foreground,true,Qt::AlignRight|Qt::AlignVCenter);
                text(painter,{40,410,1200,45},audioVisual.toUpper()+" / "+palette.name,20,palette.muted);
            }
        } else {
            text(painter, {38,20,800,50}, "TROFEO / " + palette.name.toUpper(), 24, palette.accent, true);
            // The time leads the header, with the PC total and the date beside it.
            text(painter, {560,20,520,50}, headerInfo(), 22, palette.muted, false, Qt::AlignRight|Qt::AlignVCenter);
            text(painter, {1090,12,150,62}, QTime::currentTime().toString("HH:mm"), 40, palette.foreground, true, Qt::AlignRight|Qt::AlignVCenter);
            const QList<Field> data = fieldData();
            int count = data.size();
            if (layout == "terminal") {
                text(painter,{40,85,1200,55},"> omarchy-trcc --status",28,palette.green);
                for (int i=0;i<count;++i) {
                    double y = 155.+i*80;
                    text(painter,{55,y,200,60},data[i].label.toLower(),28,palette.accent);
                    text(painter,{300,y,350,60},data[i].value,40,palette.foreground,true);
                    text(painter,{690,y,530,60},data[i].detail,24,palette.muted);
                }
            } else for (int i=0;i<count;++i) {
                double column = 1240.0 / count, x = 40+i*column;
                if (layout == "rings") {
                    painter.setPen(QPen(palette.muted,9)); painter.setBrush(Qt::NoBrush); painter.drawEllipse(QPointF(x+135,240),105,105);
                    painter.setPen(QPen(palette.accent,9)); painter.drawArc(QRectF(x+30,135,210,210),90*16,-int(data[i].ring*3.6*16));
                    text(painter,{x,190,270,100},data[i].value,55,palette.foreground,true,Qt::AlignCenter);
                } else if (layout == "dots") {
                    QImage mask(280,120,QImage::Format_ARGB32); mask.fill(Qt::transparent);
                    QPainter mp(&mask); QFont font(palette.font); font.setPixelSize(83); font.setBold(true); mp.setFont(font); mp.setPen(Qt::white); mp.drawText(mask.rect(),Qt::AlignCenter,data[i].value); mp.end();
                    painter.setPen(Qt::NoPen); painter.setBrush(palette.foreground);
                    for (int yy=0;yy<120;yy+=5) for(int xx=0;xx<280;xx+=5)
                        if(qAlpha(mask.pixel(xx,yy))>80) painter.drawEllipse(QPointF(x+xx,185+yy),1.9,1.9);
                } else text(painter,{x,170,280,140},data[i].value,88,palette.foreground,true);
                text(painter,{x,90,280,50},data[i].label,24,palette.accent,true);
                text(painter,{x,350,380,50},data[i].detail,20,palette.muted);
                if (layout != "rings") { painter.setPen(QPen(palette.accent,3)); painter.drawLine(QPointF(x,330),QPointF(x+260,330)); }
            }
            QString version = pluginVersion();
            text(painter,{40,420,1200,38},"OMARCHY TROFEO  ·  NATIVE USB"+(version.isEmpty()?QString():"  ·  v"+version),16,palette.muted);
        }
        if (!preview && settings["brightness"].toInt()<100)
            painter.fillRect(image.rect(),QColor(0,0,0,qRound(255*(1-settings["brightness"].toInt()/100.0))));
        return image;
    }
    void previews() {
        for (const QString &layout : layouts) render(layout, "", true).scaled(640,240,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).save(stateDir+"/previews/"+layout+".png");
        for(const auto &theme:customThemes.all()) render(theme.id,"",true).scaled(640,240,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).save(stateDir+"/previews/"+theme.id+".png");
    }
    QJsonObject status() {
        QSaveFile liveFile(stateDir+"/live.png");
        if(liveFile.open(QIODevice::WriteOnly)) {
            if(render(settings["layout"].toString(),settings["game"].toBool()&&metrics.cpu>75?QString():visual(),true).scaled(640,240,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).save(&liveFile,"PNG")) liveFile.commit();
            else liveFile.cancelWriting();
        }
        QJsonArray themes, visuals;
        const QStringList names = {"Focus", "Terminal", "Rings", "Dots"};
        for(int i=0;i<layouts.size();++i) themes.append(QJsonObject{{"name",names[i]},{"path",layouts[i]},{"custom",false}, {"preview",QUrl::fromLocalFile(stateDir+"/previews/"+layouts[i]+".png").toString()}});
        for(const auto &theme:customThemes.all()) themes.append(QJsonObject{{"name",theme.name},{"path",theme.id},{"custom",true},{"preview",QUrl::fromLocalFile(stateDir+"/previews/"+theme.id+".png").toString()}});
        const QMap<QString,QString> visualNames={{"bars",uiText("Barras")},{"wave",uiText("Onda")},{"rings",uiText("Anillos")},{"particles",uiText("Partículas")}};
        for(const QString &id:visualIds) visuals.append(QJsonObject{{"id",id},{"title",visualNames[id]},{"presets",QJsonArray{}}});
        QJsonObject state{{"brightness",settings["brightness"]},{"orientation",settings["orientation"]},
            {"current_theme",settings["layout"]},{"overlay_enabled",settings["overlay"]},{"game_enabled",settings["game"]}};
        return {{"ok",true},{"key","0416:5302"},{"usbPresent",usb.connected()}, {"deviceError",usb.error},
            {"state",state},{"themes",themes},{"visuals",visuals},{"playing",visual()}, {"resolution","1280 × 480"},
            {"followAccent",settings["followAccent"]},{"preset",""},{"audioError",audioError},{"framesSent",qint64(framesSent)},
            {"livePreview",QUrl::fromLocalFile(stateDir+"/live.png").toString()},{"previewRevision",QString::number(QDateTime::currentMSecsSinceEpoch())},
            {"palette",QJsonObject{{"name",palette.name},{"accent",palette.accent.name()}}},
            {"metrics",QJsonObject{{"cpu",metrics.cpu},{"gpu",metrics.gpu},{"cpuTemp",metrics.cpuTemp},{"gpuTemp",metrics.gpuTemp},{"ramTemp",metrics.ramTemp},{"ram",metrics.ram},{"ramUsedGiB",metrics.ramUsedGiB},{"ramTotalGiB",metrics.ramTotalGiB},{"cpuPower",metrics.cpuPower},{"gpuPower",metrics.gpuPower},{"power",metrics.power},{"powerSource",metrics.powerSource},{"powerPartial",metrics.powerPartial}}}};
    }
    QJsonObject command(const QJsonObject &request) {
        QString op=request["command"].toString(); QJsonArray args=request["args"].toArray();
        QString value=args.isEmpty()?QString():args[0].toString();
        if(op=="status") return status();
        auto invalid=[] { return QJsonObject{{"ok",false},{"message",uiText("Acción o valor no válido")}}; };
        if(op=="import-theme"&&args.size()==1) {
            auto result=customThemes.importFile(value);
            if(result["ok"].toBool()) previews();
            return result;
        }
        if(op=="remove-theme"&&args.size()==1) {
            if(!customThemes.remove(value)) return invalid();
            QFile::remove(stateDir+"/previews/"+value+".png");
            if(settings["layout"].toString()==value) { settings["layout"]="focus"; lastFrame=0; }
            save(); return {{"ok",true},{"message",uiText("Tema eliminado de la biblioteca.")}};
        }
        if(op=="brightness") {
            bool ok; int n=value.toInt(&ok); if(!ok||n<0||n>100) return invalid(); settings["brightness"]=n;
        } else if(op=="orientation") {
            bool ok; int n=value.toInt(&ok); if(!ok||!QList<int>{0,90,180,270}.contains(n)) return invalid(); settings["orientation"]=n;
        } else if(op=="theme") {
            if(!layouts.contains(value)&&!customThemes.contains(value)) return invalid();
            settings["layout"]=value; settings["visual"]="";
        } else if(op=="visual") {
            if(value!="off"&&!visualIds.contains(value)) return invalid();
            settings["visual"]=value=="off"?QString():value;
        } else if(op=="overlay"||op=="game"||op=="follow-accent") {
            if(value!="on"&&value!="off") return invalid();
            settings[op=="follow-accent"?"followAccent":op]=value=="on";
        } else if(op=="accent") {
            palette.load(); settings["visualHue"]=qRound(palette.accent.hsvHueF()*359);
        } else if(op=="reconnect") { usb.close(); lastConnect=0; }
        else return invalid();
        if(visual().isEmpty()) { audio.kill(); samples.clear(); }
        else startAudio();
        save(); lastFrame=0;
        return {{"ok",true},{"message",uiText("Cambio aplicado")}};
    }
    void tick() {
        qint64 now=QDateTime::currentMSecsSinceEpoch();
        if(now-lastSample>=1000) {
            QString plugin=qEnvironmentVariable("OMARCHY_TRCC_PLUGIN_DIR");
            if(!plugin.isEmpty()&&!QFileInfo::exists(plugin+"/manifest.json")) {
                // `omarchy plugin remove` deletes the checkout. Remove only
                // our runtime/unit; preserve user settings and custom themes.
                QProcess::execute("systemctl",{"--user","disable","omarchy-trcc.service"});
                QString config=qEnvironmentVariable("XDG_CONFIG_HOME",QDir::homePath()+"/.config");
                QFile::remove(config+"/systemd/user/omarchy-trcc.service");
                QProcess::startDetached("systemctl",{"--user","daemon-reload"});
                QFile::remove(stateDir+"/bin/trofeo-engine");
                QDir(stateDir).rmdir("bin");
                QFile::remove(stateDir+"/.engine-version");
                QFile::remove(stateDir+"/.bootstrap.lock");
                QFile::remove(socketPath+".lock");
                server.close(); QLocalServer::removeServer(socketPath);
                QCoreApplication::quit(); return;
            }
            QString oldPalette=palette.name+palette.accent.name()+palette.background.name()+palette.foreground.name();
            palette.load(); metrics.sample(); lastSample=now;
            if(oldPalette!=palette.name+palette.accent.name()+palette.background.name()+palette.foreground.name()) previews();
        }
        if(!offline&&!usb.connected()&&now-lastConnect>=5000) { lastConnect=now; usb.open(); }
        if(!visual().isEmpty()&&now-lastAudioRetry>=5000) { lastAudioRetry=now; startAudio(); }
        bool game=settings["game"].toBool()&&metrics.cpu>75;
        QString active=game?QString():visual();
        if(now-lastFrame<(active.isEmpty()?1000:125)) return;
        if(!active.isEmpty()) analyze();
        phase+=.125;
        QImage image=render(settings["layout"].toString(),active);
        if(!offline&&usb.connected()&&usb.send(image,settings["orientation"].toInt())) ++framesSent;
        lastFrame=now;
    }
    bool listen() {
        QLocalServer::removeServer(socketPath);
        server.setSocketOptions(QLocalServer::UserAccessOption);
        if(!server.listen(socketPath)) return false;
        QObject::connect(&server,&QLocalServer::newConnection,[&] {
            while(server.hasPendingConnections()) {
                QLocalSocket *client=server.nextPendingConnection();
                auto received=std::make_shared<QByteArray>();
                QObject::connect(client,&QLocalSocket::readyRead,client,[&,client,received] {
                    *received+=client->readAll();
                    if(received->size()>8192) { client->disconnectFromServer(); return; }
                    if(!received->contains('\n')) return;
                    QJsonParseError error; auto request=QJsonDocument::fromJson(received->split('\n').first(),&error);
                    QJsonObject response=error.error==QJsonParseError::NoError&&request.isObject()?command(request.object()):QJsonObject{{"ok",false},{"message",uiText("JSON no válido")}};
                    client->write(QJsonDocument(response).toJson(QJsonDocument::Compact)+'\n');
                    client->disconnectFromServer();
                });
                QObject::connect(client,&QLocalSocket::disconnected,client,&QObject::deleteLater);
                QTimer::singleShot(3000,client,[client] { if(client->state()!=QLocalSocket::UnconnectedState) client->disconnectFromServer(); });
            }
        });
        return true;
    }
};

int main(int argc,char **argv) {
    qputenv("QT_QPA_PLATFORM","offscreen");
    qunsetenv("QT_QPA_PLATFORMTHEME");
    QGuiApplication app(argc,argv);
    app.setApplicationName("omarchy-trcc");
    QStringList args=app.arguments();
    if(args.contains("--self-test")) {
        QTemporaryDir testState;
        qputenv("XDG_STATE_HOME", testState.path().toUtf8());
        QByteArray jpeg=QByteArray::fromHex("ffd8ff00ffd9"); auto packet=framePacket(jpeg);
        bool ok=packet.size()==512&&packet.left(4)==QByteArray::fromHex("dadbdcdd")&&qFromLittleEndian<quint16>(packet.constData()+8)==W&&qFromLittleEndian<quint16>(packet.constData()+10)==H&&qFromLittleEndian<quint32>(packet.constData()+16)==quint32(jpeg.size())&&packet.mid(20,jpeg.size())==jpeg;
        // A second packet must span multiple full 512-byte reports.
        ok=ok&&framePacket(QByteArray(1000,'x')).size()==1024;
        Engine engine(true);
        for(const QString &layout:layouts) {
            QImage image=engine.render(layout,""); ok=ok&&!image.isNull()&&image.size()==QSize(W,H);
        }
        for(const QString &id:visualIds) ok=ok&&!engine.render("focus",id).isNull();
        ok=ok&&!engine.command({{"command","brightness"},{"args",QJsonArray{"101"}}})["ok"].toBool();
        ok=ok&&!engine.command({{"command","theme"},{"args",QJsonArray{"../../unknown"}}})["ok"].toBool();
        // Re-read a changed system theme without restarting the renderer.
        QString themePath=testState.path()+"/omarchy/current/theme";
        QDir().mkpath(themePath);
        QFile paletteFile(themePath+"/colors.toml");
        ok=paletteFile.open(QIODevice::WriteOnly)&&ok; paletteFile.write("accent = \"#123456\"\nbackground = \"#eeeeee\"\n"); paletteFile.close();
        engine.palette.load();
        ok=ok&&engine.palette.accent.name()=="#123456"&&engine.palette.background.name()=="#eeeeee";
        ok=paletteFile.open(QIODevice::WriteOnly|QIODevice::Truncate)&&ok; paletteFile.write("accent = \"#abcdef\"\nbackground = \"#111111\"\n"); paletteFile.close();
        engine.palette.load();
        ok=ok&&engine.palette.accent.name()=="#abcdef"&&engine.palette.background.name()=="#111111";
        // Fixed rows: CPU, GPU and RAM in every layout and orientation.
        ok=ok&&engine.fieldData().size()==3;
        ok=ok&&!engine.command({{"command","fields"},{"args",QJsonArray{"cpu"}}})["ok"].toBool();
        for(const QString &layout:layouts) {
            engine.settings["orientation"]=90;
            ok=ok&&engine.render(layout,"").size()==QSize(H,W);
        }
        engine.settings["orientation"]=0;
        for(const QString &layout:layouts) {
            QImage image=engine.render(layout,""); ok=ok&&!image.isNull()&&image.size()==QSize(W,H);
        }
        for(int angle: {90,180,270,0}) {
            engine.settings["orientation"]=angle;
            QImage image=engine.render("focus","");
            ok=ok&&image.size()==((angle==90||angle==270)?QSize(H,W):QSize(W,H));
        }
        qputenv("LC_ALL","en_US.UTF-8");
        ok=ok&&uiText("%1% CARGA")=="%1% LOAD";
        qputenv("LC_ALL","es_MX.UTF-8");
        ok=ok&&uiText("%1% CARGA")=="%1% CARGA";
        // Power sources against a fake sysfs tree.
        QTemporaryDir fakeSys;
        auto put=[&](const QString &file,const QByteArray &value) {
            QString path=fakeSys.path()+"/"+file; QDir().mkpath(QFileInfo(path).path());
            QFile out(path); if(!out.open(QIODevice::WriteOnly|QIODevice::Truncate)) ok=false; out.write(value);
        };
        auto drop=[&](const QString &file) { QFile::remove(fakeSys.path()+"/"+file); };
        // runRoot too: the real power relay may be installed on the test machine.
        Metrics m; m.sysRoot=fakeSys.path(); m.procRoot=fakeSys.path(); m.runRoot=fakeSys.path()+"/run";
        m.sample(); ok=ok&&m.power<0&&m.powerText()==QString::fromUtf8("—");
        put("class/hwmon/hwmon0/name","coretemp"); put("class/hwmon/hwmon0/temp1_input","50000");
        put("class/hwmon/hwmon1/name","amdgpu"); put("class/hwmon/hwmon1/power1_average","45000000");
        put("class/hwmon/hwmon1/temp1_input","60000"); put("class/hwmon/hwmon1/device/gpu_busy_percent","80");
        put("class/hwmon/hwmon2/name","amdgpu"); put("class/hwmon/hwmon2/power1_input","5000000");
        put("class/hwmon/hwmon2/temp1_input","40000"); put("class/hwmon/hwmon2/device/gpu_busy_percent","3");
        m.sample(1000);
        // Coretemp has no power: GPU-only total is a lower bound. Both GPUs add up;
        // temperature and load follow the busiest one.
        ok=ok&&m.cpuPower<0&&qFuzzyCompare(m.gpuPower,50.0)&&m.gpuTemp==60&&m.gpu==80&&m.powerPartial&&m.powerText()==QString::fromUtf8("≥50");
        put("class/powercap/intel-rapl:0/max_energy_range_uj","1000000000");
        put("class/powercap/intel-rapl:0/energy_uj","999000000"); m.sample(2000); ok=ok&&m.cpuPower<0;
        // The counter wraps at max_energy_range_uj: 999 J -> 1 J is 2 J in 1 s.
        put("class/powercap/intel-rapl:0/energy_uj","1000000"); m.sample(3000);
        ok=ok&&qFuzzyCompare(m.cpuPower,2.0)&&!m.powerPartial&&m.powerSource=="components"&&m.powerText()=="52";
        drop("class/powercap/intel-rapl:0/energy_uj");
        // The power service's whole watts win; a stale file (service stopped) is ignored.
        put("run/omarchy-trcc-power/cpu-watts","7\n"); m.sample(3500); ok=ok&&m.cpuPower==7&&!m.powerPartial;
        QFile stale(fakeSys.path()+"/run/omarchy-trcc-power/cpu-watts");
        ok=stale.open(QIODevice::ReadWrite)&&stale.setFileTime(QDateTime::currentDateTime().addSecs(-60),QFileDevice::FileModificationTime)&&ok; stale.close();
        m.sample(3600); ok=ok&&m.cpuPower<0;
        put("run/omarchy-trcc-power/cpu-watts","garbage"); m.sample(3700); ok=ok&&m.cpuPower<0;
        drop("run/omarchy-trcc-power/cpu-watts");
        // Intel discrete GPUs: energy counter turned into watts (3 J in 1 s).
        put("class/hwmon/hwmon3/name","xe"); put("class/hwmon/hwmon3/energy1_input","1000000"); m.sample(4000);
        put("class/hwmon/hwmon3/energy1_input","4000000"); m.sample(5000); ok=ok&&qFuzzyCompare(m.gpuPower,53.0);
        drop("class/hwmon/hwmon3/energy1_input");
        // NVIDIA proprietary driver through NVML (fake library from the build).
        QString fakeNvml=qEnvironmentVariable("TROFEO_TEST_NVML");
        if(!fakeNvml.isEmpty()) {
            put("bus/pci/devices/0000:01:00.0/vendor","0x10de"); put("bus/pci/devices/0000:01:00.0/class","0x030000");
            put("bus/pci/devices/0000:01:00.0/power/control","on"); put("bus/pci/devices/0000:01:00.0/power/runtime_status","active");
            QDir().mkpath(fakeSys.path()+"/bus/drivers/nvidia");
            QFile::link(fakeSys.path()+"/bus/drivers/nvidia",fakeSys.path()+"/bus/pci/devices/0000:01:00.0/driver");
            m.nvmlLibrary=fakeNvml; m.sample(6000);
            // 120 W NVIDIA becomes the busiest card: its temperature and load win.
            ok=ok&&qFuzzyCompare(m.gpuPower,170.0)&&m.gpuTemp==65&&m.gpu==90;
            // A sleeping laptop GPU is not woken up and counts as 0 W.
            put("bus/pci/devices/0000:01:00.0/power/control","auto"); put("bus/pci/devices/0000:01:00.0/power/runtime_status","suspended");
            auto sessions=reinterpret_cast<int(*)()>(QLibrary(fakeNvml).resolve("fakeNvmlSessions"));
            int before=sessions?sessions():-1;
            m.sample(7000); ok=ok&&sessions&&sessions()==before&&qFuzzyCompare(m.gpuPower,50.0)&&m.gpuTemp==60;
            QDir(fakeSys.path()+"/bus").removeRecursively();
        }
        m.sample(8000); ok=ok&&qFuzzyCompare(m.gpuPower,50.0)&&qFuzzyCompare(m.power,50.0)&&m.powerPartial;
        put("class/power_supply/BAT0/status","Charging"); put("class/power_supply/BAT0/power_now","20000000");
        m.sample(9000); ok=ok&&m.powerSource=="components";
        put("class/power_supply/BAT0/status","Discharging"); m.sample(10000); ok=ok&&m.powerSource=="battery"&&m.powerText()=="20";
        drop("class/power_supply/BAT0/power_now"); put("class/power_supply/BAT0/current_now","2000000"); put("class/power_supply/BAT0/voltage_now","12000000");
        m.sample(11000); ok=ok&&qFuzzyCompare(m.power,24.0);
        put("class/hwmon/hwmon4/name","corsairpsu"); put("class/hwmon/hwmon4/power1_input","180000000");
        m.sample(12000); ok=ok&&m.powerSource=="psu"&&m.powerText()=="180"&&!m.powerPartial;
        QTextStream(stdout)<<(ok?"PASS: packet alignment, geometry, native layouts, audio renderers, power sources and input validation\n":"FAIL\n");
        return ok?0:1;
    }
    if(args.contains("--metrics")) {
        // Two samples one second apart, so load and RAPL have a delta.
        Metrics m; m.sample(); QThread::sleep(1); m.sample();
        QTextStream(stdout)<<QJsonDocument(QJsonObject{{"cpu",m.cpu},{"gpu",m.gpu},{"cpuTemp",m.cpuTemp},{"gpuTemp",m.gpuTemp},{"cpuPower",m.cpuPower},{"gpuPower",m.gpuPower},{"power",m.power},{"powerText",m.powerText()},{"powerSource",m.powerSource},{"powerPartial",m.powerPartial}}).toJson(QJsonDocument::Compact)<<'\n';
        return 0;
    }
    if(args.contains("--probe")) {
        Usb usb; bool ok=usb.open();
        QTextStream(stdout)<<QJsonDocument(QJsonObject{{"ok",ok},{"pm",usb.pm},{"sub",usb.sub},{"error",usb.error}}).toJson(QJsonDocument::Compact)<<'\n';
        return ok?0:1;
    }
    Engine engine(args.contains("--offline"));
    if(args.contains("--previews")) { engine.previews(); return 0; }
    QLockFile lock(engine.socketPath+".lock"); lock.setStaleLockTime(0);
    if(!lock.tryLock(100)) { QTextStream(stderr)<<"El motor Trofeo ya está ejecutándose\n"; return 1; }
    engine.previews();
    if(!engine.listen()) { QTextStream(stderr)<<engine.server.errorString()<<'\n'; return 1; }
    QTimer timer; QObject::connect(&timer,&QTimer::timeout,[&]{engine.tick();}); timer.start(125);
    return app.exec();
}
