#!/usr/bin/env bash
# Reinicia omarchy-trcc.service al despertar de suspensión.
# Escucha la señal PrepareForSleep de logind; con argumento false = reanudación.
set -euo pipefail

readonly target_unit="omarchy-trcc.service"
readonly resume_delay="3"
readonly usb_id="0416:5302"
readonly reappear_timeout="20"
readonly poll_interval="1"
readonly settle_delay="1"
readonly disconnect_delay="2"

# Resetea el dispositivo USB y espera a que vuelva a enumerarse.
# Nunca falla: los problemas se registran en stderr y se sigue adelante.
reset_usb_device() {
    local waited=0

    if ! usbreset "$usb_id" </dev/null; then
        echo "Fallo en usbreset ${usb_id}; se continúa" >&2
    fi

    sleep "$disconnect_delay"

    until lsusb -d "$usb_id" >/dev/null </dev/null; do
        if ((waited >= reappear_timeout)); then
            echo "${usb_id} no reapareció en ${reappear_timeout}s; se reinicia ${target_unit} igualmente" >&2
            return 0
        fi
        sleep "$poll_interval"
        waited=$((waited + poll_interval))
    done

    sleep "$settle_delay"
}

gdbus monitor --system \
    --dest org.freedesktop.login1 \
    --object-path /org/freedesktop/login1 |
    while IFS= read -r line; do
        case "$line" in
            *PrepareForSleep*"(false"*)
                echo "Reanudación detectada; reiniciando ${target_unit} en ${resume_delay}s" >&2
                sleep "$resume_delay"
                reset_usb_device
                if ! systemctl --user restart "$target_unit" </dev/null; then
                    echo "Fallo al reiniciar ${target_unit}" >&2
                fi
                ;;
        esac
    done

# Si gdbus termina, salir con error para que systemd reinicie el vigilante.
echo "gdbus monitor terminó inesperadamente" >&2
exit 1
