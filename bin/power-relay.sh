#!/usr/bin/env bash
# CPU power relay for omarchy-trcc. setup.sh installs a root-owned copy as
# /usr/local/libexec/omarchy-trcc-power, run by omarchy-trcc-power.service.
#
# The kernel keeps the RAPL energy counter root-only because fine-grained
# readings leak data (Platypus, CVE-2020-8694). This relay reads it once per
# second and publishes only the average over that second, rounded to whole
# watts, which is useless for that attack. The counter itself stays root-only.
#
# It reads no user input: the paths below are fixed by the service unit.
set -euo pipefail
umask 022

rapl=${OMARCHY_TRCC_RAPL_DIR:-/sys/class/powercap/intel-rapl:0}
out=${OMARCHY_TRCC_POWER_DIR:-/run/omarchy-trcc-power}
interval=1

# No RAPL package domain on this CPU: nothing to publish.
[[ -r $rapl/energy_uj ]] || exit 0
range=$(<"$rapl/max_energy_range_uj")
[[ $range =~ ^[0-9]+$ ]] || range=0

# Microseconds since the epoch, from bash itself (no subprocess per sample).
now_us() { local t=${EPOCHREALTIME/[.,]/}; echo "$((10#$t))"; }

previous=$(<"$rapl/energy_uj")
previous_at=$(now_us)
while sleep "$interval"; do
  energy=$(<"$rapl/energy_uj")
  at=$(now_us)
  [[ $energy =~ ^[0-9]+$ && $previous =~ ^[0-9]+$ ]] || { previous=$energy; previous_at=$at; continue; }
  delta=$((energy - previous))
  # The counter wraps at max_energy_range_uj.
  ((delta < 0 && range > 0)) && delta=$((delta + range))
  elapsed=$((at - previous_at))
  previous=$energy previous_at=$at
  ((delta >= 0 && elapsed > 0)) || continue
  # µJ / µs = W; round to the nearest whole watt.
  watts=$(((delta + elapsed / 2) / elapsed))
  # Atomic replace, so a reader never sees a half-written value.
  printf '%d\n' "$watts" >"$out/cpu-watts.tmp"
  mv -f "$out/cpu-watts.tmp" "$out/cpu-watts"
done
