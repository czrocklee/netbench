#!/usr/bin/env bash

set -euo pipefail

# Globals filled by resolve_imc_events
EVENTS=""
PAT_READ=""
PAT_WRITE=""

# memband_intel.sh
# Stream Intel IMC DRAM bandwidth via perf uncore counters.
# Defaults: interval=1000ms, duration=30s, units=MiB/s, system-wide (-a).
# Usage:
#   ./memband_intel.sh [-d SECONDS] [-i MS] [--units MiB|GiB] [--no-header]
# Notes:
#   - Uncore PMUs often require kernel.perf_event_paranoid <= 1 (or sudo).
#   - Tries generic uncore_imc events first, then falls back to per-instance uncore_imc_* devices.

duration=300
interval=1000
units="MiB"   # or GiB
precision=1    # number of decimal places in output
debug=0
print_header=1

usage() {
  cat <<EOF
Usage: $0 [options]
Options:
  -d, --duration SECONDS   Total duration to sample (default: ${duration})
  -i, --interval MS        Sampling interval in milliseconds (default: ${interval})
      --units MiB|GiB      Output units (default: ${units})
  --precision N        Decimals in output (default: ${precision})
  --debug              Print resolved events and patterns to stderr
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
      --precision)
        precision="$2"; shift 2;;
      --debug)
        debug=1; shift;;
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

# Build a comma-separated perf event list for IMC read/write.
# Also set global patterns PAT_READ and PAT_WRITE for awk matching.
resolve_imc_events() {
  # Try multiple alias pairs used across generations/kernels
  local pairs=(
    "cas_count_read cas_count_write"
    "data_reads data_writes"
    "unc_m_cas_count.rd unc_m_cas_count.wr"
    "UNC_M_CAS_COUNT.RD UNC_M_CAS_COUNT.WR"
  )

  for pair in "${pairs[@]}"; do
    local read_ev write_ev
    read read_ev write_ev <<<"$pair"

    # Generic uncore_imc/* form
    local generic="uncore_imc/${read_ev}/,uncore_imc/${write_ev}/"
    if perf stat -x, -a -e "$generic" -- sleep 0.1 >/dev/null 2>&1; then
      PAT_READ="$read_ev"; PAT_WRITE="$write_ev"; EVENTS="$generic"
      return 0
    fi

    # Fallback: enumerate instances uncore_imc_*
    local evs=()
    local any=0
    for pmu in /sys/bus/event_source/devices/uncore_imc_*; do
      [[ -e "$pmu" ]] || continue
      local name
      name="$(basename "$pmu")"
      evs+=("${name}/${read_ev}/")
      evs+=("${name}/${write_ev}/")
      any=1
    done
    if [[ $any -eq 1 ]]; then
      PAT_READ="$read_ev"; PAT_WRITE="$write_ev"
      local IFS=','
      EVENTS="${evs[*]}"
      return 0
    fi
  done

  echo "Error: No usable uncore_imc read/write events found; DRAM bandwidth may not be supported on this CPU/Kernel." >&2
  echo "Hints:" >&2
  echo " - Check 'perf list | grep -i imc' for available IMC events" >&2
  echo " - Ensure intel_uncore module is loaded and BIOS doesn't lock uncore PMUs" >&2
  echo " - Some older Xeon-D/Broadwell-DE platforms expose different names" >&2
  exit 1
}

main() {
  parse_args "$@"

  # Choose scale by units
  local scale label
  case "$units" in
    MiB) scale=1048576; label="MiB/s" ;;
    GiB) scale=1073741824; label="GiB/s" ;;
  esac

  resolve_imc_events

  if [[ "$debug" -eq 1 ]]; then
    echo "[debug] EVENTS=$EVENTS" >&2
    echo "[debug] PAT_READ=$PAT_READ PAT_WRITE=$PAT_WRITE" >&2
  fi

  # Header printed by awk, unless suppressed
  perf stat -I "$interval" -x, -a \
    -e "$EVENTS" \
    -- sleep "$duration" 2>&1 | \
  awk -F, -v scale="$scale" -v unit_label="$label" -v hdr="$print_header" -v pr="$PAT_READ" -v pw="$PAT_WRITE" -v prec="$precision" '
    function to_bytes(val, unit,   u) {
      u = unit
      if (u == "" || u == "counts" || u == "events") return val * 64;  # CAS count -> bytes
      if (u == "B" || u == "bytes") return val;
      if (u == "KiB") return val * 1024;
      if (u == "kB" || u == "KB") return val * 1000;
      if (u == "MiB") return val * 1024 * 1024;
      if (u == "GiB") return val * 1024 * 1024 * 1024;
      # Unknown unit: assume already bytes
      return val;
    }
    BEGIN {
      last=""; rb=0; wb=0;  # accumulate bytes per interval for read/write
      if (hdr+0==1) {
        printf("%9s  %14s  %14s  %14s\n", "time(s)", "read " unit_label, "write " unit_label, "total " unit_label);
      }
    }
    # perf CSV columns: time,count,unit,event,...
    $4 ~ pr && $2 ~ /^[0-9]/ {
      t=$1;
      bytes = to_bytes($2, $3);
      if (last=="" || t==last) { rb += bytes }
      else {
        printf("%9.3f  %14.*f  %14.*f  %14.*f\n", last, prec, rb/scale, prec, wb/scale, prec, (rb+wb)/scale);
        rb = bytes; wb = 0
      }
      last=t; next
    }
    $4 ~ pw && $2 ~ /^[0-9]/ {
      t=$1;
      bytes = to_bytes($2, $3);
      if (t==last) { wb += bytes }
      else {
        printf("%9.3f  %14.*f  %14.*f  %14.*f\n", last, prec, rb/scale, prec, wb/scale, prec, (rb+wb)/scale);
        rb = 0; wb = bytes
      }
      last=t; next
    }
    END {
      if (last!="") printf("%9.3f  %14.*f  %14.*f  %14.*f\n", last, prec, rb/scale, prec, wb/scale, prec, (rb+wb)/scale)
    }
  '
}

main "$@"
