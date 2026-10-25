import QtQuick
import QtQuick.Controls as Controls
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Commons as Commons
import qs.Ui

Panel {
  id: root
  moduleName: "omarchy-trcc"
  ipcTarget: "omarchy-trcc"
  manageIpc: false
  readonly property string backend: decodeURIComponent(String(Qt.resolvedUrl("bin/trofeo")).replace(/^file:\/\//, ""))
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
  property string page: "screen"
  property string selectedVisual: ""
  property bool initialized: false

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
    actionProc.command = ["python3", backend, "--device", device].concat(args)
    actionProc.running = true
  }
  function selectedInfo() {
    var entries = deviceData.visuals || []
    for (var i = 0; i < entries.length; i++)
      if (entries[i].id === selectedVisual) return entries[i]
    return ({presets: []})
  }
  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight
  Component.onCompleted: { initialized = true; refresh() }
  onOpenedChanged: if (opened) refresh()
  onDeviceChanged: if (initialized) { deviceData = ({}); refresh() }

  IpcHandler {
    enabled: !!root.QsWindow.window
    target: root.ipcTarget
    function open(): void { root.open() }
    function close(): void { root.close() }
    function toggle(): void { root.toggle() }
    function view(name: string): void {
      if (name === "screen" || name === "visuals") { root.page = name; root.open() }
    }
    function status(): string {
      return JSON.stringify({ready: root.ready, error: root.error, busy: root.busy,
        page: root.page, accent: String(Commons.Color.accent),
        background: String(Commons.Color.background),
        theme: root.lcdState.current_theme || "", playing: root.deviceData.playing || ""})
    }
  }

  Connections {
    target: Commons.Color
    function onAccentChanged() { if (root.opened) root.refresh() }
  }
  Timer { interval: 5000; repeat: true; running: root.opened; onTriggered: root.refresh() }

  Process {
    id: statusProc
    command: ["python3", root.backend, "--device", root.device, "status"]
    stdout: StdioCollector { waitForEnd: true; onStreamFinished: root.statusOutput = text }
    stderr: StdioCollector { waitForEnd: true; onStreamFinished: root.statusError = text }
    onExited: function(code) {
      try {
        var result = JSON.parse(root.statusOutput)
        if (!result.ok) throw new Error(result.message || "El motor no respondió")
        root.deviceData = result
        if (root.selectedVisual === "") root.selectedVisual = result.playing || "bars"
        root.error = result.deviceError || result.audioError || ""
      } catch (e) {
        root.deviceData = ({})
        root.error = root.statusError.trim() || String(e)
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
        if (code !== 0 || !result.ok) root.error = result.message || "No se pudo aplicar el cambio"
      } catch (e) { root.error = root.actionError.trim() || "El motor no pudo completar la acción" }
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
    onPressed: function(mouseButton) { if (mouseButton === Qt.LeftButton) root.toggle() }
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
            text: "Omarchy TRCC"
            color: root.barForeground
            font.family: Style.font.family
            font.pixelSize: Style.font.title
            font.bold: true
          }
          Text {
            width: parent.width
            text: (root.ready ? root.deviceData.resolution + " · " + (root.deviceData.usbPresent ? "USB conectado" : "USB no detectado") : "Conectando con el motor…") + (root.busy ? " · Aplicando…" : "")
            color: Commons.Color.muted
            font.family: Style.font.family
            font.pixelSize: Style.font.body
            wrapMode: Text.Wrap
          }
          PanelSeparator { foreground: root.barForeground }
          Row {
            spacing: Style.space(6)
            Button { text: "Pantalla"; selected: root.page === "screen"; onClicked: root.page = "screen" }
            Button { text: "Visuales"; selected: root.page === "visuals"; onClicked: root.page = "visuals" }
            Button { text: "Reconectar"; enabled: !root.busy; onClicked: root.run(["reconnect"]) }
          }

          Column {
            width: parent.width
            spacing: Style.space(10)
            visible: root.page === "screen" && root.ready
            Text {
              text: "Brillo · " + Math.round(brightness.value) + "%"
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
              Button { text: "Métricas"; selected: root.lcdState.overlay_enabled === true; enabled: !root.busy; onClicked: root.run(["overlay", root.lcdState.overlay_enabled ? "off" : "on"]) }
              Button { text: "Modo juego"; selected: root.lcdState.game_enabled === true; enabled: !root.busy; onClicked: root.run(["game", root.lcdState.game_enabled ? "off" : "on"]) }
            }
            PanelSectionHeader { text: "TEMAS COMPATIBLES"; foreground: root.barForeground }
            Text {
              visible: (root.deviceData.themes || []).length === 0
              text: "No hay temas locales compatibles con esta resolución."
              width: parent.width; wrapMode: Text.Wrap
              color: Commons.Color.muted; font.family: Style.font.family; font.pixelSize: Style.font.body
            }
            Repeater {
              model: root.deviceData.themes || []
              Column {
                id: themeCard
                required property var modelData
                width: column.width
                spacing: Style.space(4)
                Image {
                  width: parent.width
                  height: width * 480 / 1280
                  source: parent.modelData.preview + "?palette=" + encodeURIComponent(root.deviceData.palette ? root.deviceData.palette.name + root.deviceData.palette.accent : "")
                  cache: false
                  fillMode: Image.PreserveAspectFit
                  asynchronous: true
                  MouseArea { anchors.fill: parent; enabled: !root.busy; cursorShape: Qt.PointingHandCursor; onClicked: root.run(["theme", themeCard.modelData.path]) }
                }
                Button {
                  width: parent.width
                  text: parent.modelData.name + (selected ? " · Activo" : " · Aplicar")
                  selected: root.lcdState.current_theme === parent.modelData.path
                  enabled: !root.busy
                  onClicked: root.run(["theme", parent.modelData.path])
                }
              }
            }
          }

          Column {
            width: parent.width
            spacing: Style.space(10)
            visible: root.page === "visuals" && root.ready
            Text {
              width: parent.width
              text: root.deviceData.playing ? "En pantalla: " + root.deviceData.playing : "Ningún visual de audio activo"
              color: root.barForeground; font.family: Style.font.family; font.pixelSize: Style.font.body; wrapMode: Text.Wrap
            }
            Flow {
              width: parent.width
              spacing: Style.space(6)
              Button { text: "Detener"; enabled: !root.busy && !!root.deviceData.playing; onClicked: root.run(["visual", "off"]) }
              Button { text: "Acento de Omarchy"; enabled: !root.busy && root.colourCapable; onClicked: root.run(["accent"]) }
              Button { text: "Seguir acento"; selected: root.deviceData.followAccent === true; enabled: !root.busy; onClicked: root.run(["follow-accent", root.deviceData.followAccent ? "off" : "on"]) }
            }
            Text {
              width: parent.width
              text: "Los visuales reaccionan a la salida de audio, sin usar el micrófono. Seguir acento adapta el color al tema de Omarchy."
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
          Button { visible: !root.ready; text: "Reintentar"; enabled: !statusProc.running; onClicked: root.refresh() }
          Text {
            text: "v0.0.1 · Sigue el tema de Omarchy"
            color: Commons.Color.muted; font.family: Style.font.family; font.pixelSize: Style.font.body
          }
        }
      }
    }
  }
}
