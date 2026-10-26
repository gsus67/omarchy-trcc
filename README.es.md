# omarchy-trcc · v0.0.9

[English](README.md) · **Español**

Plugin de Omarchy para la **Thermalright Trofeo Vision 1280×480**. Motor USB propio en C++/Qt: **no requiere instalar ni ejecutar TRCC**.

![Panel actual de omarchy-trcc con datos en vivo](preview.png)

## Galería

Las pestañas del panel (Inicio está arriba; Consumo, en [Compatibilidad](#compatibilidad)):

<table>
  <tr>
    <td><img src="assets/gallery/panel-display.png" alt="Pantalla" width="260"></td>
    <td><img src="assets/gallery/panel-themes.png" alt="Temas" width="260"></td>
    <td><img src="assets/gallery/panel-visuals.png" alt="Visuales" width="260"></td>
  </tr>
  <tr>
    <td align="center">Pantalla</td>
    <td align="center">Temas</td>
    <td align="center">Visuales</td>
  </tr>
</table>

Los diseños propios tal como los muestra la Trofeo (los animados son grabaciones de la pantalla real) (1280×480, aquí a la mitad):

<table>
  <tr>
    <td><img src="assets/gallery/lcd-focus.png" alt="Focus" width="400"></td>
    <td><img src="assets/gallery/lcd-terminal.png" alt="Terminal" width="400"></td>
  </tr>
  <tr><td align="center">Focus</td><td align="center">Terminal</td></tr>
  <tr>
    <td><img src="assets/gallery/lcd-rings.png" alt="Rings" width="400"></td>
    <td><img src="assets/gallery/lcd-dots.png" alt="Dots" width="400"></td>
  </tr>
  <tr><td align="center">Rings</td><td align="center">Dots</td></tr>
  <tr>
    <td><img src="assets/gallery/lcd-gauges.png" alt="Gauges" width="400"></td>
    <td><img src="assets/gallery/lcd-clock.png" alt="Clock" width="400"></td>
  </tr>
  <tr><td align="center">Gauges</td><td align="center">Clock</td></tr>
  <tr>
    <td><img src="assets/gallery/lcd-matrix.gif" alt="Matrix" width="400"></td>
    <td><img src="assets/gallery/lcd-omarchy.gif" alt="Omarchy" width="400"></td>
  </tr>
  <tr><td align="center">Matrix</td><td align="center">Omarchy</td></tr>
  <tr>
    <td><img src="assets/gallery/lcd-fx.gif" alt="Métricas FX" width="400"></td>
    <td><img src="assets/gallery/lcd-picture.gif" alt="Picture FX" width="400"></td>
  </tr>
  <tr><td align="center">Métricas FX</td><td align="center">Picture FX</td></tr>
</table>

## Funciones

- Botón **🖥 TR** en la barra; panel con los colores, fuentes, bordes y espaciado del tema activo de Omarchy.
- Idioma automático según `LC_ALL`, `LC_MESSAGES` y `LANG`: inglés/español, con inglés como respaldo para otros idiomas. Panel, mensajes y etiquetas nativas comparten el catálogo; los textos de tus propios temas se conservan.
- Inicio con temperatura CPU/GPU/RAM (si hay sensores), carga, vatios, memoria usada y vista de la pantalla; actualización cada dos segundos mientras el panel está abierto.
- Pestaña Temas con lista compacta y una sola vista previa desplegable que sigue los datos en vivo; ver un tema no lo aplica.
- Pestaña Consumo en PCs de escritorio sin fuente con sensor: estima el resto del PC (placa, RAM, discos, ventiladores, refrigeración, RGB) y la pérdida de la fuente.
- Importación de imágenes 1280×480 y temas JSON con métricas; puedes quitar las copias importadas sin borrar tus originales.
- Diez diseños nativos: **Focus, Terminal, Rings, Dots, Gauges, Clock, Matrix, Omarchy, Métricas FX y Picture FX**. Los cuatro últimos son animados, con los datos encima: Matrix (lluvia de caracteres); Omarchy (tu logo de `~/.config/omarchy/branding/screensaver.txt` armándose con un efecto distinto en cada ciclo, como el protector de pantalla de Omarchy); Métricas FX (las filas de CPU/GPU/RAM armándose con esos efectos); y Picture FX (una imagen que eliges en Temas → Picture FX → **Elegir imagen…**, convertida en arte de texto con la herramienta propia de Omarchy, `omarchy-transcode-ascii`, igual que `omarchy branding screensaver image`; los logos con fondo transparente quedan mejor. Solo se guarda el arte de texto, no una copia de la imagen; hasta 20 MB).
- Estos tres usan el **ttfx** del sistema, el motor del protector de pantalla de Omarchy, así que tienen todos sus efectos con colores (fireworks, blackhole, matrix, rings…); tras cada efecto quedan unos segundos con los datos en vivo en sus colores finales. No se instala nada: sin ttfx usan efectos propios. La pantalla también sigue automáticamente la paleta del sistema.
- FPS de animación en Pantalla: Auto (recomendado), 4, 8, 15 o 30, solo para los visuales y los diseños animados; los estáticos envían un fotograma por segundo. Auto mide cuánto cambia la imagen entre fotogramas y va de 2 a 30 FPS (30 con mucho movimiento, 2 con la imagen quieta): sube al momento y baja un escalón por segundo; con la CPU ocupada pone un tope de 12 o 4. El panel muestra los FPS actuales y el motivo.
- Temperatura de CPU, GPU y RAM con carga y vatios, frecuencia de la CPU, VRAM y RAM en uso, consumo total, fecha y reloj. Un sensor no disponible se muestra como `—`.
- Brillo por software, giro de 0/90/180/270°, métricas sobre los visuales y modo juego (vuelve al panel de métricas con CPU >75%).
- Cuatro visuales propios: **Bars, Wave, Rings y Particles**, analizados desde la salida de audio del sistema. No usa el micrófono ni guarda audio.
- Seguimiento opcional del acento, activado inicialmente; cuando se desactiva se conserva un color fijo.
- Reconexión automática USB y servicio de usuario. El motor sigue actualizando la pantalla aunque cierres el panel.

## Compatibilidad

v0.0.9 está probada con **USB `0416:5302`, PM 128, SUB 1, 1280×480**. El motor valida la identidad del panel al conectarse y rechaza otras geometrías. No es compatible con la Trofeo 9,16″/11,3″ ni con todas las pantallas Thermalright que comparten USB ID.

Las temperaturas se leen desde hwmon (`coretemp`, `k10temp`, `amdgpu`, `nouveau`, `i915`, `xe`), la carga de CPU/RAM desde `/proc` y la carga de GPU desde el nodo DRM `gpu_busy_percent`. La temperatura de RAM se obtiene de sensores `spd5118`/`jc42` y muestra el máximo disponible. Con el driver propietario de NVIDIA, consumo, temperatura y carga de la GPU salen de NVML, la librería que Omarchy instala con `nvidia-utils` (la misma que usa `nvidia-smi`).

El consumo en vatios se lee sin root y sin cambiar permisos. Para el total de la PC se usa, por orden: una fuente Corsair HXi/RMi con USB (hwmon `corsairpsu`), la batería de un portátil mientras se descarga, o la suma CPU + GPU. La GPU sale de `amdgpu`/`nouveau`, del contador de energía de las GPU Intel dedicadas (`i915`/`xe`) o de NVML; se suman todas. Una GPU NVIDIA de portátil dormida cuenta como 0 W y no se despierta. Omarchy reserva a root el contador RAPL de la CPU por seguridad; el paso opcional `setup.sh` (más abajo) instala un servicio del sistema encerrado que publica solo vatios enteros una vez por segundo, así el plugin obtiene el consumo de la CPU sin acceso al contador. Sin él, la CPU muestra `—` y el total aparece como `≥ X W`. El panel muestra el total en Inicio y los vatios de cada componente en su cuadro; la Trofeo muestra el total en la cabecera de los diseños, junto a la fecha. La suma CPU + GPU no incluye placa, RAM, discos ni pérdidas de la fuente.

Todos los diseños propios comparten un mismo diseño: la hora arriba (grande en Clock) con el consumo total y la fecha, y debajo CPU, GPU y RAM. Cada fila muestra la temperatura como valor y, a su lado, la carga y los vatios (RAM: uso). Sin sensor de RAM, la RAM muestra su uso. La línea de abajo muestra la frecuencia de la CPU (media de los núcleos / máxima), la VRAM de la GPU y la RAM en uso frente a sus totales.

En un PC de escritorio sin fuente con sensor ni batería, el panel añade la pestaña **Consumo** para estimar el resto del PC. Detecta los módulos de RAM (sensores DDR4/DDR5), los NVMe y los discos SATA internos; tú marcas el tamaño de la placa base, los ventiladores sin RGB y con RGB (cada uno en uno solo), la refrigeración por aire o líquida, las tiras RGB y la certificación 80 PLUS de la fuente. Con "≈ Incluir en el total" activado, el total pasa a `≈ X W`: CPU + GPU más esas piezas, dividido entre la eficiencia de la fuente (consumo en el enchufe); la pestaña desglosa cuánto aporta cada pieza. La fila de RAM muestra siempre los vatios estimados de sus módulos. Son valores típicos (DDR5 4 W, DDR4 3 W, NVMe 4 W, SSD SATA 2 W, HDD 6 W, ventilador 1,5 W, con RGB 3 W, bomba 5–7 W, tira RGB 4 W, placa 20–40 W), no mediciones: ningún sensor del PC los da.

![Pestaña Consumo estimando el resto del PC](assets/gallery/panel-power.png)

Esta versión dibuja diseños y visuales propios; no importa temas `.dc`, shaders KarmaViz ni presets de TRCC.

## Instalación

La instalación utiliza las herramientas de Omarchy: un compilador C++ (`g++` o `clang++`), `pkg-config`, Qt 6.7+ con `rcc`, libusb, Python estándar y PipeWire/libpulse. **No requiere CMake, paquetes de pip ni entornos virtuales.** Omarchy Shell debe ofrecer el sistema de plugins y los componentes `qs.Ui`.

En la lista de paquetes de Omarchy, `clang` y `quickshell` aportan el compilador y Qt; `python-gobject` aporta Python; `ddcutil` aporta libusb. La imagen de instalación incluye `base-devel` (GCC y pkgconf) y libpulse. No se instalan paquetes automáticamente ni se modifica la instalación de Omarchy.

```sh
omarchy plugin add https://github.com/gsus67/omarchy-trcc.git --enable
```

Al activarlo por primera vez, el plugin compila automáticamente su motor y prepara el servicio de usuario. Las actualizaciones recompilan el motor solo si cambió el código o la versión de sus bibliotecas. Las carpetas temporales de compilación se eliminan al terminar. No instala paquetes automáticamente, no usa sudo y no modifica `/usr/share/omarchy`.

Para actualizar:

```sh
omarchy plugin update io.github.gsus67.omarchy-trcc
```

Para el permiso USB y el consumo de la CPU, ejecuta una vez desde una terminal:

```sh
bash ~/.config/omarchy/plugins/io.github.gsus67.omarchy-trcc/setup.sh
```

Ese paso opcional pide sudo una sola vez e instala únicamente copias propiedad de root:

- una regla `uaccess` limitada a `0416:5302`. No concede acceso global de escritura al USB. Cierra cualquier controlador que ya esté usando la pantalla antes de iniciar el motor.
- si la CPU tiene RAPL, `omarchy-trcc-power.service` y su script `/usr/local/libexec/omarchy-trcc-power`. Lee el contador de energía una vez por segundo y escribe solo vatios enteros en `/run/omarchy-trcc-power/cpu-watts`, lo que inutiliza los ataques que necesitan lecturas finas (Platypus, CVE-2020-8694); el contador sigue siendo solo de root. Corre sin capacidades, sin red, sin carpeta personal ni dispositivos, con el sistema en solo lectura (exposición de systemd 0,7/10), y nunca ejecuta archivos de la carpeta del plugin.

## Uso

Pulsa **🖥 TR** para abrir/cerrar el panel. Desde una terminal:

```sh
omarchy-shell omarchy-trcc open
omarchy-shell omarchy-trcc view visuals
python ~/.config/omarchy/plugins/io.github.gsus67.omarchy-trcc/bin/trofeo status
python ~/.config/omarchy/plugins/io.github.gsus67.omarchy-trcc/bin/trofeo theme focus
python ~/.config/omarchy/plugins/io.github.gsus67.omarchy-trcc/bin/trofeo visual bars
systemctl --user status omarchy-trcc
```

Los temas personalizados se copian a `~/.local/state/omarchy-trcc/themes/`. Ajustes y vistas previas se guardan en `~/.local/state/omarchy-trcc/`. El socket está en `$XDG_RUNTIME_DIR/omarchy-trcc.sock` y solo permite acceso a tu usuario. El motor detecta cambios en `~/.local/state/omarchy/current/theme/colors.toml` cada segundo, incluso con el panel cerrado.

## Añadir tus propios temas

En **Temas → Añadir tema…**, selecciona una imagen PNG/JPG/WebP de **1280×480**, o un archivo JSON de tema. Seleccionar un tema despliega su vista previa; **Aplicar en la Trofeo** lo activa. **Quitar copia** borra únicamente la copia de la biblioteca. La importación no cambia lo que está mostrando la pantalla.

Para crear un panel con métricas, copia [examples/mi-tema.json](examples/mi-tema.json), edita su nombre/textos/posiciones y añádelo desde el selector. También puedes importarlo desde la terminal:

```sh
python ~/.config/omarchy/plugins/io.github.gsus67.omarchy-trcc/bin/trofeo import-theme /ruta/a/mi-tema.json
```

El formato usa `schema: 1`, `width: 1280`, `height: 480`, un `name` y una lista `elements`. Cada elemento `type: "text"` define `text`, `x`, `y`, `width`, `height` y `fontSize`; admite `bold`, `align` (`left`, `center`, `right`) y `color`. Los colores pueden ser `background`, `foreground`, `accent`, `muted`, `green` (siguen Omarchy) o `#RRGGBB`.

Los textos pueden incluir `{cpu_temp}`, `{gpu_temp}`, `{ram_temp}`, `{cpu_load}`, `{gpu_load}`, `{ram}`, `{cpu_power}`, `{gpu_power}`, `{power}`, `{time}`, `{date}` y `{theme}`. Las temperaturas son números en °C; `{ram}` es el porcentaje de memoria usada; los consumos están en vatios; `{power}` es el total de la PC (lleva `≥` delante si falta un componente). Los sensores ausentes se muestran como `—`.

Opcionalmente, `background` indica una imagen de 1280×480 dentro de la misma carpeta del JSON, por ejemplo `"background": "fondo.png"`. Se guarda una copia normalizada; el tema importado no depende del archivo original. `backgroundColor` acepta los mismos colores que los textos. Los temas solo contienen datos, sin scripts ni código ejecutable. No son temas `.dc` de TRCC.

## Sistema limpio y desinstalación

La instalación usa únicamente:

- `~/.config/omarchy/plugins/io.github.gsus67.omarchy-trcc/` (checkout administrado por Omarchy)
- `~/.config/systemd/user/omarchy-trcc.service` y su enlace de activación
- Una entrada `io.github.gsus67.omarchy-trcc` en la barra de `shell.json`
- `~/.local/state/omarchy-trcc/` para ajustes, temas importados, vistas previas y una única imagen en vivo que se sobrescribe
- Socket/bloqueo temporales en `$XDG_RUNTIME_DIR`
- Opcionalmente, si ejecutaste `setup.sh`: `/etc/udev/rules.d/70-omarchy-trcc.rules`, `/etc/systemd/system/omarchy-trcc-power.service` y `/usr/local/libexec/omarchy-trcc-power` (`uninstall.sh --purge` los quita)

Respeta `XDG_CONFIG_HOME` y `XDG_STATE_HOME`. El binario compilado queda en `~/.local/state/omarchy-trcc/bin/`; la compilación temporal se borra. La eliminación normal del plugin también retira el motor y su unidad de usuario, conservando tus ajustes y temas personalizados.

```sh
omarchy plugin remove io.github.gsus67.omarchy-trcc
# Alternativa para borrar también tus ajustes y temas importados:
bash ~/.config/omarchy/plugins/io.github.gsus67.omarchy-trcc/uninstall.sh --purge
```

No elimina dependencias compartidas, configuraciones de TRCC ni otros plugins. Al detener el motor la pantalla conserva la última imagen enviada.

## Desarrollo

```sh
# Compila, ejecuta las pruebas nativas y elimina la compilación temporal:
python native_build.py --test
PYTHONDONTWRITEBYTECODE=1 python -m unittest discover -s tests -v
omarchy plugin validate .

# Opcional, para conservar un binario de desarrollo:
python native_build.py --output build
```

La compilación se realiza únicamente con `native_build.py`, con argumentos separados, sin ejecutar un shell, y funciona con rutas que contienen espacios.

La validación de esta versión incluye compilar y ejecutar las pruebas con un `PATH` limitado sin CMake, además de la actualización del plugin en el equipo real. No se ha ejecutado una instalación completa de Omarchy desde ISO para esta comprobación.

La prueba nativa verifica estructura/alineación de paquetes, geometría, renderizadores y validación de entradas sin acceder al USB. Las pruebas del cliente verifican respuestas fragmentadas, límites y errores de conexión. La conexión real está comprobada mediante handshake y envío completo de frames.

Licencia **GPL-3.0-or-later**. Créditos del protocolo en [NOTICE.md](NOTICE.md).
