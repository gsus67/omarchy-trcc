import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs as Dialogs
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Commons as Commons
import qs.Ui

Panel {
  id: root
  moduleName: "io.github.gsus67.omarchy-trcc"
  ipcTarget: "omarchy-trcc"
  manageIpc: false
  readonly property string backend: decodeURIComponent(String(Qt.resolvedUrl("bin/trofeo")).replace(/^file:\/\//, ""))
  readonly property string pluginDirectory: backend.replace(/\/bin\/trofeo$/, "")
  readonly property string device: String(setting("device", "0416:5302"))
  property var deviceData: ({})
  readonly property var lcdState: deviceData.state || ({})
  readonly property bool ready: deviceData.ok === true
  readonly property bool busy: actionProc.running || queue.length > 0
  readonly property bool colourCapable: !!deviceData.playing
  property string error: ""
  property var queue: []
  property string actionOutput: ""
  property string actionError: ""
  property string statusOutput: ""
  property string statusError: ""
  property string page: "live"
  property string selectedVisual: ""
  property string expandedTheme: ""
  property string operationError: ""
  property bool initialized: false
  property var translations: ({})
  readonly property string language: (Quickshell.env("LC_ALL") || Quickshell.env("LC_MESSAGES") || Quickshell.env("LANG") || Qt.locale().name).toLowerCase().split(/[_.-]/)[0] === "es" ? "es" : "en"
  function t(source) {
    if (language === "es") return source
    var catalog = translations.en || ({})
    return catalog[source] || source
  }
  FileView {
    path: root.pluginDirectory + "/i18n/messages.json"
    onLoaded: {
      try { root.translations = JSON.parse(text()) } catch (e) { root.translations = ({}) }
    }
  }

  function refresh() {
    if (!statusProc.running && !actionProc.running) {
      statusOutput = ""
      statusError = ""
      statusProc.running = true
    }
  }
  function run(args) {
    queue = queue.concat([args])
    pump()
  }
  function pump() {
    if (actionProc.running || statusProc.running || queue.length === 0) return
    var args = queue[0]
    queue = queue.slice(1)
    actionOutput = ""
    actionError = ""
    error = ""
    operationError = ""
    actionProc.command = ["python3", backend, "--device", device].concat(args)
    actionProc.running = true
  }
  function themeInfo() {
    var entries = deviceData.themes || []
    for (var i = 0; i < entries.length; i++)
      if (entries[i].path === expandedTheme) return entries[i]
    return ({})
  }
  function selectTheme(id) {
    expandedTheme = expandedTheme === id ? "" : id
  }
  function selectedInfo() {
    var entries = deviceData.visuals || []
    for (var i = 0; i < entries.length; i++)
      if (entries[i].id === selectedVisual) return entries[i]
    return ({presets: []})
  }
  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight
  Component.onCompleted: { initialized = true; bootstrapProc.running = true }
  onOpenedChanged: if (opened) refresh()
  onDeviceChanged: if (initialized) { deviceData = ({}); refresh() }

  IpcHandler {
    enabled: !!root.QsWindow.window
    target: root.ipcTarget
    function open(): void { root.open() }
    function close(): void { root.close() }
    function toggle(): void { root.toggle() }
    function view(name: string): void {
      if (name === "live" || name === "screen" || name === "themes" || name === "visuals") { root.page = name; root.open() }
    }
    function preview(id: string): void { root.expandedTheme = id; root.page = "themes"; root.open() }
    function status(): string {
      return JSON.stringify({ready: root.ready, error: root.error, busy: root.busy,
        page: root.page, language: root.language, expandedTheme: root.expandedTheme, accent: String(Commons.Color.accent),
        background: String(Commons.Color.background),
        theme: root.lcdState.current_theme || "", playing: root.deviceData.playing || ""})
    }
  }

  Connections {
    target: Commons.Color
    function onAccentChanged() { if (root.opened) root.refresh() }
  }
  Timer { interval: 2000; repeat: true; running: root.opened; onTriggered: root.refresh() }

  Process {
    id: bootstrapProc
    command: ["python3", root.pluginDirectory + "/bootstrap.py", root.pluginDirectory]
    property string resultText: ""
    stdout: StdioCollector { waitForEnd: true; onStreamFinished: bootstrapProc.resultText = text }
    onExited: function(code) {
      try {
        var result = JSON.parse(resultText)
        if (!result.ok) { root.operationError = result.message; root.error = result.message }
      } catch (e) { root.operationError = root.t("No se pudo preparar el motor"); root.error = root.operationError }
      root.refresh()
    }
  }

  Process {
    id: statusProc
    command: ["python3", root.backend, "--device", root.device, "status"]
    stdout: StdioCollector { waitForEnd: true; onStreamFinished: root.statusOutput = text }
    stderr: StdioCollector { waitForEnd: true; onStreamFinished: root.statusError = text }
    onExited: function(code) {
      try {
        var result = JSON.parse(root.statusOutput)
        if (!result.ok) throw new Error(result.message || root.t("El motor no respondió"))
        root.deviceData = result
        if (root.selectedVisual === "") root.selectedVisual = result.playing || "bars"
        root.error = root.operationError || result.deviceError || result.audioError || ""
        if (root.expandedTheme !== "" && !root.themeInfo().path) root.expandedTheme = ""
      } catch (e) {
        root.deviceData = ({})
        root.error = root.operationError || root.statusError.trim() || String(e)
      }
      Qt.callLater(root.pump)
    }
  }
  Process {
    id: actionProc
    stdout: StdioCollector { waitForEnd: true; onStreamFinished: root.actionOutput = text }
    stderr: StdioCollector { waitForEnd: true; onStreamFinished: root.actionError = text }
    onExited: function(code) {
      try {
        var result = JSON.parse(root.actionOutput)
        if (code !== 0 || !result.ok) {
          root.operationError = result.message || root.t("No se pudo aplicar el cambio")
          root.error = root.operationError
        } else if (result.themeId) {
          root.expandedTheme = result.themeId
          root.page = "themes"
        }
      } catch (e) { root.operationError = root.actionError.trim() || root.t("El motor no pudo completar la acción"); root.error = root.operationError }
      root.refresh()
      Qt.callLater(root.pump)
    }
  }

  WidgetButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: "󰍹 TR"
    tooltipText: "Omarchy TRCC · Thermalright Trofeo"
    onPressed: function(mouseButton) { if (mouseButton === Qt.LeftButton) { if (!root.opened) root.page = "live"; root.toggle() } }
  }

  Dialogs.FileDialog {
    id: importDialog
    title: root.t("Añadir tema a Omarchy TRCC")
    fileMode: Dialogs.FileDialog.OpenFile
    nameFilters: [root.t("Temas e imágenes (*.json *.png *.jpg *.jpeg *.webp)"), root.t("Todos los archivos (*)")]
    onAccepted: {
      var filePath = decodeURIComponent(String(selectedFile).replace(/^file:\/\//, ""))
      root.run(["import-theme", filePath])
      root.page = "themes"
      root.open()
    }
    onRejected: { root.page = "themes"; root.open() }
  }

  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    focusTarget: keys
    contentWidth: panel.fittedContentWidth(Style.space(440))
    contentHeight: panel.fittedContentHeight(column.implicitHeight)

    PanelKeyCatcher {
      id: keys
      anchors.fill: parent
      onCloseRequested: root.close()
      onTabRequested: function(direction) { root.switchPanel(direction) }

      Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
          id: column
          width: parent.width
          spacing: Style.space(12)

          Text {
            textFormat: Text.PlainText
            text: "Omarchy TRCC"
            color: root.barForeground
            font.family: Style.font.family
            font.pixelSize: Style.font.title
            font.bold: true
          }
          Text {
            textFormat: Text.PlainText
            width: parent.width
            text: (root.ready ? root.deviceData.resolution + " · " + (root.deviceData.usbPresent ? root.t("USB conectado") : root.t("USB no detectado")) : (bootstrapProc.running ? root.t("Preparando el motor…") : root.t("Conectando con el motor…"))) + (root.busy ? root.t(" · Aplicando…") : "")
            color: Commons.Color.muted
            font.family: Style.font.family
            font.pixelSize: Style.font.body
            wrapMode: Text.Wrap
          }
          PanelSeparator { foreground: root.barForeground }
          Row {
            spacing: Style.space(6)
            Button { text: root.t("Inicio"); selected: root.page === "live"; onClicked: root.page = "live" }
            Button { text: root.t("Pantalla"); selected: root.page === "screen"; onClicked: root.page = "screen" }
            Button { text: root.t("Temas"); selected: root.page === "themes"; onClicked: root.page = "themes" }
            Button { text: root.t("Visuales"); selected: root.page === "visuals"; onClicked: root.page = "visuals" }
          }

          Column {
            width: parent.width
            spacing: Style.space(12)
            visible: root.page === "live" && root.ready
            Row {
              width: parent.width
              spacing: Style.space(8)
              Repeater {
                model: [
                  {name: "CPU", temperature: "cpuTemp", load: "cpu", power: "cpuPower"},
                  {name: "GPU", temperature: "gpuTemp", load: "gpu", power: "gpuPower"},
                  {name: "RAM", temperature: "ramTemp", load: "ram"}
                ]
                Rectangle {
                  id: metricCard
                  required property var modelData
                  readonly property var readings: root.deviceData.metrics || ({})
                  readonly property real temperature: readings[modelData.temperature] === undefined ? -1 : readings[modelData.temperature]
                  readonly property real load: readings[modelData.load] === undefined ? -1 : readings[modelData.load]
                  width: (parent.width - parent.spacing * 2) / 3
                  height: Style.space(115)
                  radius: Style.cornerRadius
                  color: Qt.rgba(Commons.Color.accent.r, Commons.Color.accent.g, Commons.Color.accent.b, 0.08)
                  border.color: Commons.Color.muted
                  border.width: Math.max(1, Style.space(1))
                  Column {
                    anchors.fill: parent
                    anchors.margins: Style.space(10)
                    spacing: Style.space(5)
                    // Name, with the component's own power on the right when it has a sensor.
                    Item {
                      width: parent.width
                      height: cardName.implicitHeight
                      Text {
                        id: cardName
                        textFormat: Text.PlainText
                        text: metricCard.modelData.name
                        color: Commons.Color.accent; font.family: Style.font.family; font.pixelSize: Style.font.body; font.bold: true
                      }
                      Text {
                        readonly property var watts: metricCard.modelData.power ? metricCard.readings[metricCard.modelData.power] : undefined
                        anchors.right: parent.right
                        textFormat: Text.PlainText
                        visible: watts !== undefined && watts >= 0
                        text: Math.round(watts || 0) + " W"
                        color: Commons.Color.muted; font.family: Style.font.family; font.pixelSize: Style.font.body
                      }
                    }
                    Text {
                      textFormat: Text.PlainText
                      text: metricCard.temperature < 0 ? "—" : Math.round(metricCard.temperature) + "°C"
                      color: root.barForeground; font.family: Style.font.family; font.pixelSize: Style.font.title; font.bold: true
                    }
                    Text {
                      textFormat: Text.PlainText
                      text: metricCard.load < 0 ? root.t("Sin sensor") : Math.round(metricCard.load) + "% " + (metricCard.modelData.name === "RAM" ? root.t("en uso") : root.t("carga"))
                      color: Commons.Color.muted; font.family: Style.font.family; font.pixelSize: Style.font.body
                    }
                  }
                }
              }
            }
            Text {
              textFormat: Text.PlainText
              width: parent.width
              text: root.deviceData.metrics ? "RAM · " + Number(root.deviceData.metrics.ramUsedGiB || 0).toLocaleString(Qt.locale(), "f", 1) + " / "
                + Number(root.deviceData.metrics.ramTotalGiB || 0).toLocaleString(Qt.locale(), "f", 1) + " GiB" : ""
              color: Commons.Color.muted; font.family: Style.font.family; font.pixelSize: Style.font.body
            }
            Text {
              textFormat: Text.PlainText
              readonly property var readings: root.deviceData.metrics || ({})
              function watts(value) { return value === undefined || value < 0 ? "—" : Math.round(value) + " W" }
              width: parent.width
              // The whole PC: from a PSU or battery when there is one, otherwise
              // CPU + GPU, a lower bound ("≥") when one of them has no sensor.
              // Each component's own watts show in its card above.
              readonly property string source: readings.powerSource === "psu" ? "  (" + root.t("fuente") + ")"
                : readings.powerSource === "battery" ? "  (" + root.t("batería") + ")" : ""
              text: readings.power === undefined ? "" : root.t("Consumo") + " · "
                + (readings.powerPartial ? "≥ " : "") + watts(readings.power) + (readings.power >= 0 ? source : "")
              color: Commons.Color.muted; font.family: Style.font.family; font.pixelSize: Style.font.body
            }
            PanelSectionHeader { text: root.t("EN TU TROFEO"); foreground: root.barForeground }
            Image {
              width: parent.width
              height: width * 480 / 1280
              source: root.deviceData.livePreview ? root.deviceData.livePreview + "?frame=" + root.deviceData.previewRevision : ""
              cache: false
              fillMode: Image.PreserveAspectFit
              asynchronous: true
            }
            Button {
              text: root.t("Cambiar tema…")
              onClicked: root.page = "themes"
            }
          }

          Column {
            width: parent.width
            spacing: Style.space(10)
            visible: root.page === "screen" && root.ready
            Button { text: root.t("Reconectar USB"); enabled: !root.busy; onClicked: root.run(["reconnect"]) }
            Text {
              textFormat: Text.PlainText
              text: root.t("Brillo · ") + Math.round(brightness.value) + "%"
              color: root.barForeground
              font.family: Style.font.family
              font.pixelSize: Style.font.body
            }
            Controls.Slider {
              id: brightness
              width: parent.width
              implicitHeight: Style.space(24)
              from: 0; to: 100; stepSize: 5
              value: root.lcdState.brightness === undefined ? 100 : root.lcdState.brightness
              enabled: !root.busy
              onPressedChanged: if (!pressed) root.run(["brightness", String(Math.round(value))])
              onMoved: if (!pressed) brightnessDebounce.restart()
              background: Rectangle {
                x: brightness.leftPadding; y: brightness.topPadding + brightness.availableHeight / 2 - height / 2
                width: brightness.availableWidth; height: Style.space(4); radius: height / 2
                color: Commons.Color.muted
                Rectangle { width: brightness.visualPosition * parent.width; height: parent.height; radius: parent.radius; color: Commons.Color.accent }
              }
              handle: Rectangle {
                x: brightness.leftPadding + brightness.visualPosition * (brightness.availableWidth - width)
                y: brightness.topPadding + brightness.availableHeight / 2 - height / 2
                width: Style.space(14); height: width; radius: width / 2; color: Commons.Color.accent
              }
            }
            Timer { id: brightnessDebounce; interval: 250; onTriggered: root.run(["brightness", String(Math.round(brightness.value))]) }
            Flow {
              width: parent.width
              spacing: Style.space(6)
              Repeater {
                model: [0, 90, 180, 270]
                Button {
                  required property var modelData
                  text: modelData + "°"
                  selected: root.lcdState.orientation === modelData
                  enabled: !root.busy
                  onClicked: root.run(["orientation", String(modelData)])
                }
              }
              Button { text: root.t("Métricas"); selected: root.lcdState.overlay_enabled === true; enabled: !root.busy; onClicked: root.run(["overlay", root.lcdState.overlay_enabled ? "off" : "on"]) }
              Button { text: root.t("Modo juego"); selected: root.lcdState.game_enabled === true; enabled: !root.busy; onClicked: root.run(["game", root.lcdState.game_enabled ? "off" : "on"]) }
            }
            // What the built-in layouts show: tap to add or remove; the engine keeps catalog order.
            PanelSectionHeader { text: root.t("EN LOS DISEÑOS"); foreground: root.barForeground }
            Flow {
              id: fieldPicker
              readonly property var chosen: root.lcdState.fields || ["cpu", "gpu", "ram", "time"]
              width: parent.width
              spacing: Style.space(6)
              Repeater {
                model: [
                  {id: "cpu", name: "Temp. CPU"}, {id: "gpu", name: "Temp. GPU"},
                  {id: "ram", name: "Uso RAM"}, {id: "ramTemp", name: "Temp. RAM"},
                  {id: "time", name: "Hora"}, {id: "power", name: "Consumo total"},
                  {id: "cpuPower", name: "Consumo CPU"}, {id: "gpuPower", name: "Consumo GPU"}
                ]
                Button {
                  required property var modelData
                  readonly property bool chosen: fieldPicker.chosen.indexOf(modelData.id) >= 0
                  text: root.t(modelData.name)
                  selected: chosen
                  // At least one slot stays.
                  enabled: !root.busy && (!chosen || fieldPicker.chosen.length > 1)
                  onClicked: {
                    var next = fieldPicker.chosen.filter(function (id) { return id !== modelData.id })
                    if (!chosen) next.push(modelData.id)
                    root.run(["fields"].concat(next))
                  }
                }
              }
            }
            Text {
              textFormat: Text.PlainText
              width: parent.width
              wrapMode: Text.WordWrap
              text: root.t("Elige qué datos muestran los diseños Focus, Terminal, Rings y Dots; con más de 4 se dibujan más pequeños.")
              color: Commons.Color.muted; font.family: Style.font.family; font.pixelSize: Style.font.body
            }
          }

          Column {
            width: parent.width
            spacing: Style.space(10)
            visible: root.page === "themes" && root.ready
            Row {
              spacing: Style.space(6)
              Button {
                text: root.t("Añadir tema…")
                enabled: !root.busy
                onClicked: { root.close(); importDialog.open() }
              }
              Text {
                textFormat: Text.PlainText
                anchors.verticalCenter: parent.verticalCenter
                text: (root.deviceData.themes || []).length + root.t(" temas")
                color: Commons.Color.muted; font.family: Style.font.family; font.pixelSize: Style.font.body
              }
            }
            Text {
              textFormat: Text.PlainText
              width: parent.width
              text: root.t("Selecciona un tema para desplegar su vista previa.")
              color: Commons.Color.muted; font.family: Style.font.family; font.pixelSize: Style.font.body
              wrapMode: Text.Wrap
            }
            ListView {
              id: themeList
              width: parent.width
              height: Math.min(contentHeight, Style.space(190))
              clip: true
              spacing: Style.space(4)
              boundsBehavior: Flickable.StopAtBounds
              model: root.deviceData.themes || []
              delegate: Button {
                required property var modelData
                width: themeList.width
                text: (root.expandedTheme === modelData.path ? "▾  " : "▸  ") + modelData.name
                  + (root.lcdState.current_theme === modelData.path ? root.t(" · Activo") : "")
                  + (modelData.custom ? root.t(" · Personalizado") : "")
                leftAlign: true
                selected: root.expandedTheme === modelData.path
                onClicked: root.selectTheme(modelData.path)
              }
              Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
            }
            Column {
              width: parent.width
              spacing: Style.space(8)
              visible: !!root.themeInfo().path
              PanelSeparator { foreground: root.barForeground }
              Text {
                textFormat: Text.PlainText
                text: root.themeInfo().name || ""
                color: root.barForeground; font.family: Style.font.family; font.pixelSize: Style.font.body; font.bold: true
              }
              Image {
                width: parent.width
                height: width * 480 / 1280
                source: root.themeInfo().preview ? root.themeInfo().preview + "?palette="
                  + encodeURIComponent(root.deviceData.palette ? root.deviceData.palette.name + root.deviceData.palette.accent : "") : ""
                cache: false
                fillMode: Image.PreserveAspectFit
                asynchronous: true
              }
              Flow {
                width: parent.width
                spacing: Style.space(6)
                Button {
                  text: root.t("Aplicar en la Trofeo")
                  enabled: !root.busy
                  onClicked: root.run(["theme", root.expandedTheme])
                }
                Button {
                  text: root.t("Quitar copia")
                  visible: root.themeInfo().custom === true
                  enabled: !root.busy
                  onClicked: root.run(["remove-theme", root.expandedTheme])
                }
              }
            }
            Text {
              textFormat: Text.PlainText
              width: parent.width
              text: root.t("Añade imágenes de 1280×480 o temas JSON con métricas. Se guarda una copia; tu archivo original se conserva.")
              wrapMode: Text.Wrap; color: Commons.Color.muted; font.family: Style.font.family; font.pixelSize: Style.font.body
            }
          }

          Column {
            width: parent.width
            spacing: Style.space(10)
            visible: root.page === "visuals" && root.ready
            Text {
              textFormat: Text.PlainText
              width: parent.width
              text: root.deviceData.playing ? root.t("En pantalla: ") + root.deviceData.playing : root.t("Ningún visual de audio activo")
              color: root.barForeground; font.family: Style.font.family; font.pixelSize: Style.font.body; wrapMode: Text.Wrap
            }
            Flow {
              width: parent.width
              spacing: Style.space(6)
              Button { text: root.t("Detener"); enabled: !root.busy && !!root.deviceData.playing; onClicked: root.run(["visual", "off"]) }
              Button { text: root.t("Acento de Omarchy"); enabled: !root.busy && root.colourCapable; onClicked: root.run(["accent"]) }
              Button { text: root.t("Seguir acento"); selected: root.deviceData.followAccent === true; enabled: !root.busy; onClicked: root.run(["follow-accent", root.deviceData.followAccent ? "off" : "on"]) }
            }
            Text {
              textFormat: Text.PlainText
              width: parent.width
              text: root.t("Los visuales reaccionan a la salida de audio, sin usar el micrófono. Seguir acento adapta el color al tema de Omarchy.")
              wrapMode: Text.Wrap; color: Commons.Color.muted; font.family: Style.font.family; font.pixelSize: Style.font.body
            }
            Flow {
              width: parent.width
              spacing: Style.space(6)
              Repeater {
                model: root.deviceData.visuals || []
                Button {
                  required property var modelData
                  text: modelData.title
                  selected: root.selectedVisual === modelData.id
                  enabled: !root.busy
                  onClicked: {
                    root.selectedVisual = modelData.id
                    if (modelData.presets.length === 0) root.run(["visual", modelData.id])
                  }
                }
              }
            }
            Flow {
              width: parent.width
              spacing: Style.space(6)
              Repeater {
                model: root.selectedInfo().presets || []
                Button {
                  required property var modelData
                  text: String(modelData).replace(/\.json$/, "").replace(/_/g, " ")
                  selected: root.deviceData.playing === root.selectedVisual && root.deviceData.preset === modelData
                  enabled: !root.busy
                  onClicked: root.run(["visual", root.selectedVisual, modelData])
                }
              }
            }
          }
          Text {
            visible: root.error !== ""
            text: root.error; textFormat: Text.PlainText
            width: parent.width; wrapMode: Text.Wrap
            color: Commons.Color.urgent; font.family: Style.font.family; font.pixelSize: Style.font.body
          }
          Button { visible: !root.ready; text: root.t("Reintentar"); enabled: !statusProc.running; onClicked: root.refresh() }
        }
      }
    }
  }
}
