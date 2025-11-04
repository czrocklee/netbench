#!/usr/bin/env bash

set -euo pipefail

# zen_memband.sh
# Stream AMD UMC DRAM bandwidth via perf uncore counters.
# Defaults: interval=1000ms, duration=30s, units=MiB/s, system-wide (-a).
# Usage:
#   ./zen_memband.sh [-d SECONDS] [-i MS] [--units MiB|GiB] [--no-header]
# Notes:
#   - Uncore PMUs often require kernel.perf_event_paranoid <= 1 (or sudo).
#   - The script tries generic amd_umc events first, falls back to per-instance.

duration=300
interval=1000
units="MiB"   # or GiB
print_header=1

usage() {
  cat <<EOF
Usage: $0 [options]
Options:
  -d, --duration SECONDS   Total duration to sample (default: ${duration})
  -i, --interval MS        Sampling interval in milliseconds (default: ${interval})
      --units MiB|GiB      Output units (default: ${units})
      --no-header          Do not print header line
  -h, --help               Show this help and exit

Examples:
  $0 -d 20 -i 500             # 0.5s intervals for 20s, print MiB/s
  sudo $0 --units GiB         # GiB/s units (may require sudo for uncore)
EOF
}

parse_args() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      -d|--duration)
        duration="$2"; shift 2;;
      -i|--interval)
        interval="$2"; shift 2;;
      --units)
        case "$2" in
          MiB|GiB) units="$2" ;;
          *) echo "Invalid --units: $2 (use MiB or GiB)" >&2; exit 2;;
        esac
        shift 2;;
      --no-header)
        print_header=0; shift;;
      -h|--help)
        usage; exit 0;;
      --) shift; break;;
      -*) echo "Unknown option: $1" >&2; usage; exit 2;;
      *) break;;
    esac
  done
}

# Build a comma-separated perf event list for UMC CAS rd/wr.
resolve_umc_events() {
  local generic="amd_umc/umc_cas_cmd.rd/,amd_umc/umc_cas_cmd.wr/"
  # Quick probe: try generic form; fallback to per-instance if it fails.
  if perf stat -x, -a -e "$generic" -- sleep 0.1 >/dev/null 2>&1; then
    echo "$generic"
    return 0
  fi

  # Fallback: enumerate instances amd_umc_*
  local evs=()
  local any=0
  for pmu in /sys/bus/event_source/devices/amd_umc_*; do
    [[ -e "$pmu" ]] || continue
    local name
    name="$(basename "$pmu")"
    evs+=("${name}/umc_cas_cmd.rd/")
    evs+=("${name}/umc_cas_cmd.wr/")
    any=1
  done
  if [[ $any -eq 0 ]]; then
    echo "Error: No amd_umc PMU instances found; cannot measure DRAM bandwidth." >&2
    exit 1
  fi
  local IFS=','
  echo "${evs[*]}"
}

main() {
  parse_args "$@"

  # Choose scale by units
  local scale label
  case "$units" in
    MiB) scale=1048576; label="MiB/s" ;;
    GiB) scale=1073741824; label="GiB/s" ;;
  esac

  local EVENTS
  EVENTS="$(resolve_umc_events)"

  # Header printed by awk, unless suppressed
  perf stat -I "$interval" -x, -a \
    -e "$EVENTS" \
    -- sleep "$duration" 2>&1 | \
  awk -F, -v scale="$scale" -v unit_label="$label" -v hdr="$print_header" '
    BEGIN {
      last=""; r=0; w=0;
      if (hdr+0==1) {
        printf("%9s  %14s  %14s  %14s\n", "time(s)", "read " unit_label, "write " unit_label, "total " unit_label);
      }
    }
    # perf CSV columns: time,count,unit,event,...
    $4 ~ /umc_cas_cmd\.rd/ && $2 ~ /^[0-9]/ {
      t=$1;
      if (last=="" || t==last) { r+=$2 }
      else { printf("%9.3f  %14.1f  %14.1f  %14.1f\n", last, (r*64)/scale, (w*64)/scale, ((r+w)*64)/scale); r=$2; w=0 }
      last=t; next
    }
    $4 ~ /umc_cas_cmd\.wr/ && $2 ~ /^[0-9]/ {
      t=$1;
      if (t==last) { w+=$2 }
      else { printf("%9.3f  %14.1f  %14.1f  %14.1f\n", last, (r*64)/scale, (w*64)/scale, ((r+w)*64)/scale); r=0; w=$2 }
      last=t; next
    }
    END {
      if (last!="") printf("%9.3f  %14.1f  %14.1f  %14.1f\n", last, (r*64)/scale, (w*64)/scale, ((r+w)*64)/scale)
    }
  '
}

main "$@"
