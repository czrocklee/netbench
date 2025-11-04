#!/usr/bin/env python3
"""
Aggregate netbench results and plot grouped bar charts.

Features
- Scans results/<scenario_ts>/<impl>/<var_key>_<val>/ for metrics.json + metadata.json
- Uses scenario.json (one level up) to learn var_key and default sorting
- Computes throughput from metrics (sum bytes / duration) or falls back to scenario duration

Examples
- python3 benchmark/plot_results.py --results-dir results --scenario vary_workers --output svg
- python3 benchmark/plot_results.py --results-dir results --scenario vary_workers --output html
"""
from __future__ import annotations
import argparse
import json
import math
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple


# Tableau 10 Palette. A vibrant, high-contrast, and widely-used professional color scheme.
COLOR_LIST = [
    "#1f77b4",  # blue
    "#ff7f0e",  # orange
    "#2ca02c",  # green
    "#d62728",  # red
    "#9467bd",  # purple
    "#8c564b",  # brown
]


METRIC_PROPERTIES = {
    "msgs_per_sec": {"label": "Messages per Second", "unit_family": "count"},
    "bytes_per_sec": {"label": "Bytes per Second", "unit_family": "bytes"},
    "ops_per_sec": {"label": "Ops per Second", "unit_family": "count"},
}

def _format_var_val(x: float) -> str:
    """Format numeric var value to match directory names and keep labels tidy.

    - Integers (e.g., 1.0) -> "1"
    - Decimals -> compact form using %g (e.g., 0.5, 1.25)
    """
    try:
        xf = float(x)
        if xf.is_integer():
            return str(int(xf))
        return ("%g" % xf)
    except Exception:
        return str(x)

def _get_scale_and_unit(metric_name: str, max_val: float) -> Tuple[float, str]:
    family = METRIC_PROPERTIES.get(metric_name, {}).get("unit_family", "count")
    if family == "bytes":
        if max_val >= 1e9: return 1e9, 'GB/s'
        if max_val >= 1e6: return 1e6, 'MB/s'
        if max_val >= 1e3: return 1e3, 'KB/s'
        return 1.0, 'B/s'
    # Default to count
    if max_val >= 1e9: return 1e9, 'G/s'
    if max_val >= 1e6: return 1e6, 'M/s'
    if max_val >= 1e3: return 1e3, 'K/s'
    return 1.0, 'count/s'


def _adjust_legend_alpha(fig, ax) -> None:
    """Heuristically lower legend opacity if it overlaps plotted data significantly.

    - Collects bar rectangles and line bounding boxes in display coordinates.
    - If legend bbox overlaps many targets, reduce frame alpha more aggressively.
    """
    leg = ax.get_legend()
    if leg is None:
        return
    try:
        renderer = fig.canvas.get_renderer()
    except Exception:
        try:
            fig.canvas.draw()
            renderer = fig.canvas.get_renderer()
        except Exception:
            return
    try:
        leg_bbox = leg.get_window_extent(renderer=renderer)
    except Exception:
        return

    from matplotlib.transforms import Bbox  # type: ignore
    target_bboxes: List[Bbox] = []

    # Bar rectangles (matplotlib containers)
    try:
        for cont in getattr(ax, 'containers', []) or []:
            for rect in getattr(cont, 'patches', []) or []:
                try:
                    bb = rect.get_window_extent(renderer=renderer)
                    if bb is not None:
                        target_bboxes.append(bb)
                except Exception:
                    pass
    except Exception:
        pass

    # Line objects
    try:
        for line in getattr(ax, 'lines', []) or []:
            try:
                x = line.get_xdata(); y = line.get_ydata()
                if len(x) and len(y):
                    pts = ax.transData.transform(list(zip(x, y)))
                    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
                    bb = Bbox.from_extents(min(xs), min(ys), max(xs), max(ys))
                    target_bboxes.append(bb)
            except Exception:
                pass
    except Exception:
        pass

    # Compute overlap count
    overlap_area = 0.0
    for bb in target_bboxes:
        try:
            # Calculate the area of intersection
            intersection = Bbox.intersection(leg_bbox, bb)
            if intersection is not None:
                overlap_area += intersection.width * intersection.height
        except Exception:
            continue

    # Decide alpha based on the ratio of overlapped area to the legend's total area.
    # This is more robust than counting overlapped items.
    leg_area = leg_bbox.width * leg_bbox.height
    overlap_ratio = (overlap_area / leg_area) if leg_area > 0 else 0.0

    if overlap_ratio > 0.5: alpha = 0.2
    elif overlap_ratio > 0.2: alpha = 0.4
    elif overlap_ratio > 0.01: alpha = 0.6
    else: alpha = 0.8
    try:
        frame = leg.get_frame()
        frame.set_facecolor('white')  # Ensure background is white before making it transparent
        frame.set_alpha(alpha)  # This makes the legend background transparent

        # Also make the legend handles (color patches) transparent
        for handle in leg.get_patches():
            handle.set_alpha(alpha)

    except Exception:
        # Fallback for older matplotlib or other issues
        leg.set_alpha(alpha)

@dataclass
class RunPoint:
    scenario: str
    impl: str
    var_key: str
    var_val: float
    bytes_total: int
    msgs_total: int
    ops_total: int
    duration_sec: float  # derived from timestamps or scenario fallback

    def msgs_per_sec(self) -> float:
        if self.duration_sec <= 0:
            return float("nan")
        return self.msgs_total / self.duration_sec

    def bytes_per_sec(self) -> float:
        if self.duration_sec <= 0:
            return float("nan")
        return self.bytes_total / self.duration_sec

    def ops_per_sec(self) -> float:
        if self.duration_sec <= 0:
            return float("nan")
        return self.ops_total / self.duration_sec

    def get_metric(self, name: str) -> float:
        if name == "msgs_per_sec": return self.msgs_per_sec()
        if name == "bytes_per_sec": return self.bytes_per_sec()
        if name == "ops_per_sec": return self.ops_per_sec()
        return float("nan")


def parse_tags(tag_list: List[str]) -> Dict[str, str]:
    out = {}
    for t in tag_list or []:
        if "=" in t:
            k, v = t.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def load_metrics(metrics_path: Path) -> Tuple[int, int, int, Optional[int], Optional[int]]:
    data = json.loads(metrics_path.read_text())
    total_bytes = 0
    total_msgs = 0
    total_ops = 0
    begin_min = None
    end_max = None
    for m in data:
        total_bytes += int(m.get("bytes", 0))
        total_msgs += int(m.get("msgs", 0))
        total_ops += int(m.get("ops", 0))
        b = int(m.get("begin_ts", 0))
        e = int(m.get("end_ts", 0))
        begin_min = b if begin_min is None else min(begin_min, b)
        end_max = e if end_max is None else max(end_max, e)
    return total_bytes, total_msgs, total_ops, begin_min, end_max


def infer_var_from_dir(name: str) -> Tuple[str, float]:
    # pattern like workers_4, msg_size_1024, or delay_0.5
    m = re.match(r"([a-zA-Z0-9_]+)_([0-9]+(?:\.[0-9]+)?)$", name)
    if not m:
        raise ValueError(f"Cannot infer var from directory name: {name}")
    return m.group(1), float(m.group(2))


def gather_results(results_dir: Path, scenario_filter: Optional[List[str]] = None,
                   impl_filter: Optional[List[str]] = None,
                   single_run_dir: Optional[Path] = None) -> Tuple[
                       Dict[str, Dict[float, Dict[str, RunPoint]]],  # run_dir_name -> var_val -> impl -> RunPoint
                       Dict[str, List[float]],                       # run_dir_name -> var order
                       Dict[str, str],                             # run_dir_name -> subtitle
                       Dict[str, str],                             # run_dir_name -> title
                       Dict[str, Path],                            # run_dir_name -> run_dir path
                       Dict[str, List[str]],                       # run_dir_name -> preferred impl order from scenario.json
                       Dict[str, Optional[str]]                    # run_dir_name -> var_key_label alias (optional)
                   ]:
    # Returns:
    #  - scenarios -> var_val -> impl -> RunPoint
    #  - scenarios -> sorted var values
    #  - scenarios -> subtitle (e.g., "CPU | kernel")
    scenarios: Dict[str, Dict[float, Dict[str, RunPoint]]] = {}
    var_order: Dict[str, List[float]] = {}
    subtitles: Dict[str, str] = {}
    titles: Dict[str, str] = {}
    run_dir_paths: Dict[str, Path] = {}
    impl_orders: Dict[str, List[str]] = {}
    var_key_labels: Dict[str, Optional[str]] = {}

    run_dirs: List[Path]
    if single_run_dir is not None:
        run_dirs = [single_run_dir]
    else:
        run_dirs = sorted([p for p in results_dir.iterdir() if p.is_dir()])

    for sc_dir in run_dirs:
        if not sc_dir.is_dir():
            continue
        sc_manifest = sc_dir / "scenario.json"
        if not sc_manifest.exists():
            continue

        scj = json.loads(sc_manifest.read_text())
        sc_name = scj.get("name")
        sc_title = scj.get("title") or sc_name
        scenario_key = sc_dir.name  # Always unique per run now
        if sc_title:
            titles[scenario_key] = sc_title
        impl_list = scj.get("implementations") or []
        if isinstance(impl_list, list):
            impl_orders[scenario_key] = [str(x) for x in impl_list]
        if scenario_filter and sc_name not in scenario_filter and scenario_key not in scenario_filter:
            continue
        var_key = scj.get("var_key")
        # optional alias for x-axis label
        var_key_labels[scenario_key] = scj.get("var_key_label")
        expected_vars = scj.get("var_values", [])
        expected_vals = [float(v) for v in expected_vars]
        scenarios.setdefault(scenario_key, {})
        if scenario_key not in var_order:
            var_order[scenario_key] = expected_vals or []
        run_dir_paths[scenario_key] = sc_dir

        for impl_dir in sorted([p for p in sc_dir.iterdir() if p.is_dir()]):
            impl = impl_dir.name
            if impl_filter and impl not in impl_filter:
                continue
            for run_dir in sorted([p for p in impl_dir.iterdir() if p.is_dir()]):
                try:
                    var_key2, var_val = infer_var_from_dir(run_dir.name)
                except ValueError:
                    continue
                if var_key2 != var_key:
                    continue
                metrics_path = run_dir / "metrics.json"
                meta_path = run_dir / "metadata.json"
                if not metrics_path.exists() or not meta_path.exists():
                    continue

                total_bytes, total_msgs, total_ops, bmin, emax = load_metrics(metrics_path)
                # Read a subtitle once per scenario from metadata.json (CPU and kernel/OS)
                try:
                    if scenario_key not in subtitles:
                        meta = json.loads(meta_path.read_text())
                        m = meta.get("machine", {})
                        cpu = str(m.get("cpu_model", "")).strip()
                        kernel = str(m.get("kernel", "")).strip()
                        os_name = str(m.get("os_name", "")).strip()
                        os_version = str(m.get("os_version", "")).strip()
                        # Prefer showing OS name with kernel if kernel doesn't already contain it
                        kernel_label = ""
                        if kernel:
                            if os_name and os_name.lower() not in kernel.lower():
                                kernel_label = f"{os_name} {kernel}".strip()
                            else:
                                kernel_label = kernel
                        elif os_name or os_version:
                            kernel_label = f"{os_name} {os_version}".strip()
                        if cpu or kernel_label:
                            subtitles[scenario_key] = " | ".join([s for s in [cpu, kernel_label] if s])
                except Exception:
                    pass
                duration_sec = None
                if bmin is not None and emax is not None and emax > bmin:
                    # assume steady_clock nanoseconds
                    duration_sec = (emax - bmin) / 1e9
                if not duration_sec or duration_sec <= 0:
                    fixed = scj.get("fixed", {})
                    duration_sec = float(fixed.get("duration_sec", 0))

                rp = RunPoint(
                    scenario=scenario_key,
                    impl=impl,
                    var_key=var_key,
                    var_val=var_val,
                    bytes_total=total_bytes,
                    msgs_total=total_msgs,
                    ops_total=total_ops,
                    duration_sec=duration_sec,
                )
                scenarios[scenario_key].setdefault(var_val, {})[impl] = rp

    # Fill var order from discovered values if not in manifest
    for sk, vmap in scenarios.items():
        if not var_order.get(sk):
            var_order[sk] = sorted(vmap.keys())
    return scenarios, var_order, subtitles, titles, run_dir_paths, impl_orders, var_key_labels


def _find_var_key(vmap: Dict[float, Dict[str, RunPoint]]) -> Optional[str]:
    try:
        if vmap:
            any_impl_map = next(iter(vmap.values()))
            if any_impl_map:
                any_rp = next(iter(any_impl_map.values()))
                return any_rp.var_key
    except Exception:
        pass
    return None


# Simplified: no decode fallbacks helper; we directly decode HIST tokens with hdrh only.


def _merge_hdr_histograms(var_dir: Path):
    """Read all .hdr files in var_dir and merge into a single histogram.

    Returns a merged hdrh.HdrHistogram instance, or None if not available.
    """
    files = list(sorted(var_dir.glob("*.hdr")))
    if not files:
        return None

    def _read_last_hist_hdrh_token(path: Path):
        """Parse HIST token from .hdr file and decode using hdrh.histogram.HdrHistogram (simple path)."""
        from hdrh.histogram import HdrHistogram as HdrH_Hist  # type: ignore
        last = None
        with open(path, "rt", errors="ignore") as f:
            for line in f:
                s = line.strip()
                if not s or s.startswith("#"):
                    continue
                hist_token = None
                parts = s.split(',')
                for token in parts:
                    t = token.strip().strip('"\'')
                    if t.startswith('HIST'):
                        hist_token = t
                        break
                if hist_token is None and 'HIST' in s:
                    idx = s.find('HIST')
                    hist_token = s[idx:].strip().rstrip(',')
                if not hist_token:
                    continue
                # Directly decode the token string (hdrh accepts the HIST… token)
                last = HdrH_Hist.decode(hist_token)
        return last

    def _hist_total_count(h) -> int:
        # Try a few attribute names/methods across libraries
        for attr in ("get_total_count", "total_count"):
            try:
                v = getattr(h, attr)
                return int(v() if callable(v) else v)
            except Exception:
                continue
        return 0

    merged = None
    for fp in files:
        last_hist = _read_last_hist_hdrh_token(fp)
        if last_hist is None:
            continue
        if merged is None:
            merged = last_hist
        else:
            try:
                merged.add(last_hist)  # type: ignore[attr-defined]
            except Exception:
                # Fallback: prefer the histogram with larger total count
                try:
                    if _hist_total_count(last_hist) > _hist_total_count(merged):
                        merged = last_hist
                except Exception:
                    pass
    return merged


def _aggregate_percentiles_merged(var_dir: Path, requested: List[float]) -> Optional[Dict[float, float]]:
    """Compute percentiles from merged histogram across workers.

    Units returned: microseconds.
    """
    merged = _merge_hdr_histograms(var_dir)
    if merged is None:
        return None
    values: Dict[float, float] = {}
    for p in requested:
        try:
            # Support both APIs: get_value_at_percentile (hdrh) and value_at_percentile (hdrhistogram)
            if hasattr(merged, 'get_value_at_percentile'):
                v = merged.get_value_at_percentile(p)
            else:
                v = merged.value_at_percentile(p)  # type: ignore[attr-defined]
            values[p] = float(v) / 1000.0  # ns -> us
        except Exception:
            pass
    return values if values else None


def plot_latency_percentiles(scenarios: Dict[str, Dict[float, Dict[str, RunPoint]]], var_order: Dict[str, List[float]],
                             scenario_subtitles: Dict[str, str], scenario_titles: Dict[str, str],
                             scenario_impl_orders: Dict[str, List[str]],
                             scenario_var_labels: Dict[str, Optional[str]],
                             run_dir_paths: Dict[str, Path], percentiles: Optional[List[float]] = None,
                             debug_layout: bool = False, hide_title: bool = False) -> List[Path]:
    import matplotlib.pyplot as plt
    if percentiles is None:
        percentiles = [50.0, 90.0, 99.0, 99.9, 99.99]
    out_files: List[Path] = []

    for sc_name, vmap in scenarios.items():
        # Use the specific implementation order for this scenario
        impl_order = scenario_impl_orders.get(sc_name)
        if not impl_order:
            continue  # Skip if no impl order is defined for this scenario
        x_values = var_order.get(sc_name, sorted(vmap.keys()))
        var_key_name = _find_var_key(vmap) or "var"
        var_label = scenario_var_labels.get(sc_name) or var_key_name
        sc_dir = run_dir_paths[sc_name]

        # Prepare data: for each impl, for each x, merge workers' histograms and compute percentiles
        data: Dict[str, Dict[float, Dict[float, float]]] = {impl: {} for impl in impl_order}
        for impl in impl_order:
            for x in x_values:
                var_dir = sc_dir / impl / f"{var_key_name}_{_format_var_val(x)}"
                agg = _aggregate_percentiles_merged(var_dir, percentiles)
                if agg:
                    data[impl][x] = agg

        # For each percentile, draw a line plot per impl across x
        for p in percentiles:
            fig, ax = plt.subplots(figsize=(max(6, len(x_values) * 2.0), 4))
            centers = list(range(len(x_values)))
            plotted_any = False
            for idx, impl in enumerate(impl_order):
                y = []
                for x in x_values:
                    agg = data.get(impl, {}).get(x)
                    y.append(agg.get(p) if agg and (p in agg) else float('nan'))
                if all((not math.isfinite(v) for v in y)):
                    continue
                ax.plot(centers, y, marker='o', linestyle='-', label=impl, color=COLOR_LIST[idx % len(COLOR_LIST)])
                plotted_any = True

            ax.set_xticks(centers)
            ax.set_xticklabels([f"{var_label} = {_format_var_val(x)}" for x in x_values])
            ax.set_ylabel(f"Latency at p{p} (us)")
            subtitle = scenario_subtitles.get(sc_name)
            try:
                if not hide_title:
                    main_title = scenario_titles.get(sc_name, sc_name)
                    fig.suptitle(main_title, fontsize=16, y=0.975)
                if subtitle:
                    ax.text(1.0, 1.02, subtitle, transform=ax.transAxes,
                            ha='right', va='bottom', fontsize=10, color='#555')
                else:
                    fig.subplots_adjust(top=0.95)
            except Exception:
                pass
            ax.grid(axis='y', linestyle='--', alpha=0.3)
            if plotted_any:
                ax.legend(loc='best')
                _adjust_legend_alpha(fig, ax)
            
            try:
                if hide_title:
                    fig.tight_layout(rect=[0, 0, 1, 0.98])
                else:
                    fig.tight_layout(rect=[0, 0, 1, 0.95] if subtitle else [0, 0, 1, 0.97])
            except Exception:
                fig.tight_layout()

            if plotted_any:
                out_file = sc_dir / f"latency_p{str(p).replace('.', '_')}.svg"
                fig.savefig(out_file, format="svg")
                plt.close(fig)
                out_files.append(out_file)
            else:
                plt.close(fig)

    return out_files


def plot_matplotlib(scenarios: Dict[str, Dict[float, Dict[str, RunPoint]]], var_order: Dict[str, List[float]],
                    scenario_subtitles: Dict[str, str], scenario_titles: Dict[str, str],
                    scenario_impl_orders: Dict[str, List[str]],
                    scenario_var_labels: Dict[str, Optional[str]],
                    metric_y1: str, metric_y2: Optional[str], y2_mode: str,
                    run_dir_paths: Dict[str, Path], baseline_impl: Optional[str] = None, debug_layout: bool = False,
                    hide_title: bool = False) -> List[Path]:
    import matplotlib.pyplot as plt
    out_files: List[Path] = []
    # Per-run only: each run directory gets its own plot

    for sc_name, vmap in scenarios.items():
        # Use the specific implementation order for this scenario
        impl_order = scenario_impl_orders.get(sc_name)
        if not impl_order:
            continue # Skip if no impl order is defined for this scenario
        x_values = var_order.get(sc_name, sorted(vmap.keys()))
        n_groups = len(x_values)
        n_impls = len(impl_order)
        width = 0.8 / max(n_impls, 1)

        # Precompute baseline values per x for annotations
        baseline_vals: List[Optional[float]] = []
        if baseline_impl:
            for x in x_values:
                rp_b = vmap.get(x, {}).get(baseline_impl)
                if rp_b:
                    baseline_vals.append(rp_b.get_metric(metric_y1))
                else:
                    baseline_vals.append(None)

        # First pass: compute raw values per impl to determine y-axis scale
        raw_vals_by_impl: Dict[str, List[float]] = {}
        for impl in impl_order:
            vals_y1: List[float] = []
            for x in x_values:
                rp = vmap.get(x, {}).get(impl)
                v = rp.get_metric(metric_y1) if rp else float('nan')
                vals_y1.append(v)
            raw_vals_by_impl[impl] = vals_y1

        # Decide scale unit: '', 'k', 'm', 'b'
        y_max = 0.0
        for vals in raw_vals_by_impl.values():
            for v in vals:
                if math.isfinite(v):
                    y_max = max(y_max, v)
        scale, unit = _get_scale_and_unit(metric_y1, y_max)

        fig, ax1 = plt.subplots(figsize=(max(6, n_groups * 2.0), 4))
        base_positions = list(range(n_groups))

        # Plot bars for Y1 metric
        for idx, impl in enumerate(impl_order):
            raw_vals = raw_vals_by_impl.get(impl, [])
            vals_scaled: List[float] = [(v / scale) if math.isfinite(v) else float('nan') for v in raw_vals]
            texts: List[str] = []
            # If a baseline implementation is provided, compute percentage labels
            if baseline_impl:
                for i, v in enumerate(raw_vals):  # raw (unscaled) values
                    b = baseline_vals[i] if i < len(baseline_vals) else None
                    if b is not None and b > 0:
                        texts.append(f"{(v / b) * 100:.0f}%")
                    else:
                        texts.append("")

            positions = [p + idx * width for p in base_positions]
            bars = ax1.bar(positions, vals_scaled, width=width, label=impl, edgecolor="#333333", color=COLOR_LIST[idx % len(COLOR_LIST)])
            if baseline_impl:
                for rect, txt in zip(bars, texts):
                    if not txt:
                        continue
                    height = rect.get_height()
                    ax1.text(rect.get_x() + rect.get_width() / 2.0, height,
                            txt, ha='center', va='bottom', fontsize=8)

        # X ticks centered under each group
        centers = [p + (n_impls - 1) * width / 2 for p in base_positions]
        ax1.set_xticks(centers)
        # Determine var key name for labeling
        var_key_name = "var"
        try:
            if vmap:
                any_impl_map = next(iter(vmap.values()))
                if any_impl_map:
                    any_rp = next(iter(any_impl_map.values()))
                    var_key_name = any_rp.var_key
        except Exception:
            pass
        var_label = scenario_var_labels.get(sc_name) or var_key_name
        ax1.set_xticks(centers)
        ax1.set_xticklabels([f"{var_label} = {_format_var_val(x)}" for x in x_values])

        y_label = METRIC_PROPERTIES.get(metric_y1, {}).get("label", metric_y1)
        if unit:
            y_label = f"{y_label} ({unit})"
        ax1.set_ylabel(y_label)
        try:
            ax1.ticklabel_format(style='plain', axis='y')
        except Exception:
            pass

        # Plot line for Y2 metric if requested
        if metric_y2:
            y2_label_base = METRIC_PROPERTIES.get(metric_y2, {}).get("label", metric_y2)
            # Calculate max value for Y2 to determine scale
            y2_max = 0.0
            for x in x_values:
                for impl in impl_order:
                    rp = vmap.get(x, {}).get(impl)
                    if rp:
                        y2_max = max(y2_max, rp.get_metric(metric_y2))

            y2_scale, y2_unit = _get_scale_and_unit(metric_y2, y2_max)
            y2_label = f"{y2_label_base} ({y2_unit})" if y2_unit else y2_label_base

            ax2 = ax1.twinx()
            if y2_mode == "line":
                for idx, impl in enumerate(impl_order):
                    vals_y2: List[float] = []
                    for x in x_values:
                        rp = vmap.get(x, {}).get(impl)
                        v = rp.get_metric(metric_y2) if rp else float('nan')
                        vals_y2.append(v / y2_scale)
                    ax2.plot(centers, vals_y2, marker='o', linestyle='--', label=f"{impl} ({metric_y2})", color=COLOR_LIST[idx % len(COLOR_LIST)])
                ax2.set_ylabel(y2_label)
                ax2.ticklabel_format(style='plain', axis='y')
                # Consolidate legends
                lines, labels = ax1.get_legend_handles_labels()
                lines2, labels2 = ax2.get_legend_handles_labels()
                ax1.legend(lines + lines2, labels + labels2, loc='best')
                _adjust_legend_alpha(fig, ax1)
            else:  # share-bars mode
                ax2.set_ylabel(y2_label)
                ax2.ticklabel_format(style='plain', axis='y')
                # Synchronize the axes
                y1_lim_max = ax1.get_ylim()[1]
                y2_lim_max = (y1_lim_max * scale) * (y2_max / y_max if y_max > 0 else 1.0)
                ax2.set_ylim(0, y2_lim_max / y2_scale)
                ax1.legend(loc='best')
                _adjust_legend_alpha(fig, ax1)
        else:
            ax1.legend()
            _adjust_legend_alpha(fig, ax1)

        ax1.grid(axis='y', linestyle='--', alpha=0.3)

        subtitle = scenario_subtitles.get(sc_name)
        supt: Optional[object] = None
        subtxt: Optional[object] = None
        # Larger main title centered; subtitle centered directly beneath, above the plot
        try:
            if not hide_title:
                main_title = scenario_titles.get(sc_name, sc_name)
                supt = fig.suptitle(main_title, fontsize=16, y=0.975)
            if subtitle:
                # Position subtitle precisely relative to the top-right of the axes area
                subtxt = ax1.text(1.0, 1.02, subtitle, transform=ax1.transAxes,
                                  ha='right', va='bottom', fontsize=10, color='#555')
                #fig.subplots_adjust(top=0.90) # Adjust top to make space for titles
            else:
                fig.subplots_adjust(top=0.95)
        except Exception:
            # Fallback: use axes title only
            if not hide_title:
                ax1.set_title(scenario_titles.get(sc_name, sc_name))

        try:
            # Reserve just enough space for the titles
            if hide_title:
                fig.tight_layout(rect=[0, 0, 1, 0.98])
            else:
                fig.tight_layout(rect=[0, 0, 1, 0.95] if subtitle else [0, 0, 1, 0.97])
        except Exception:
            fig.tight_layout()

        # Debug overlay: draw bounding boxes for figure, axes, legend, titles
        if debug_layout:
            try:
                from matplotlib.patches import Rectangle
                fig.canvas.draw()  # ensure renderer is ready
                renderer = fig.canvas.get_renderer()

                def add_rect(x0, y0, w, h, color):
                    rect = Rectangle((x0, y0), w, h, transform=fig.transFigure,
                                     fill=False, linewidth=1, linestyle='--', edgecolor=color, zorder=1000)
                    fig.add_artist(rect)

                # Figure border
                add_rect(0, 0, 1, 1, '#f00')
                # Axes box
                pos = ax1.get_position()
                add_rect(pos.x0, pos.y0, pos.width, pos.height, '#0a0')
                leg = ax1.get_legend() or (ax2.get_legend() if 'ax2' in locals() else None)
                if leg is not None:
                    try:
                        bbox = leg.get_window_extent(renderer=renderer)
                        inv = fig.transFigure.inverted().transform
                        (x0, y0) = inv((bbox.x0, bbox.y0))
                        (x1, y1) = inv((bbox.x1, bbox.y1))
                        add_rect(x0, y0, x1 - x0, y1 - y0, '#f0f')
                        leg.get_frame().set_linewidth(1.0)
                        leg.get_frame().set_edgecolor('#f0f')
                    except Exception:
                        pass
                # Titles
                for t, col in [(supt, '#00f'), (subtxt, '#0ff')]:
                    if t is None:
                        continue
                    try:
                        tb = t.get_window_extent(renderer=renderer)
                        inv = fig.transFigure.inverted().transform
                        (x0, y0) = inv((tb.x0, tb.y0))
                        (x1, y1) = inv((tb.x1, tb.y1))
                        add_rect(x0, y0, x1 - x0, y1 - y0, col)
                    except Exception:
                        pass
            except Exception:
                pass

        target_dir = run_dir_paths[sc_name]
        out_file = target_dir / "plot.svg"
        fig.savefig(out_file, format="svg")
        plt.close(fig)
        out_files.append(out_file)

    return out_files


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-dir", type=Path, default=Path("results"))
    ap.add_argument("--scenario", action="append", help="Scenario(s) to include; default all")
    ap.add_argument("--impl", action="append", help="Implementations to include and order")
    ap.add_argument("--metric", dest="metric_y1", default="msgs_per_sec", choices=["msgs_per_sec", "bytes_per_sec", "ops_per_sec"], help="Metric for the primary (left) Y-axis (bars)")
    ap.add_argument("--metric-y2", default="bytes_per_sec", choices=["msgs_per_sec", "bytes_per_sec", "ops_per_sec", "none"], help="Metric for the secondary (right) Y-axis. Use 'none' to disable.")
    ap.add_argument("--y2-mode", default="share-bars", choices=["share-bars", "line"], help="How to display the secondary Y-axis metric: share bars or draw a separate line.")
    ap.add_argument("--relative-to", default="bsd", help="Baseline implementation for percentage labels; use 'none' to disable")
    ap.add_argument("--debug-layout", action="store_true", help="Draw bounding boxes for figure parts (titles/axes/legend) to debug layout")
    ap.add_argument("--run-dir", type=Path, help="Limit to a single scenario run directory (results/<scenario>_<ts>)")
    ap.add_argument("--no-hyperlinks", action="store_true", help="Disable clickable OSC8 hyperlinks in output paths")
    ap.add_argument("--no-title", action="store_true", help="Hide the main title on plots")
    args = ap.parse_args(argv)

    scenarios, var_order, scenario_subtitles, scenario_titles, run_dir_paths, scenario_impl_orders, scenario_var_labels = gather_results(
        args.results_dir, args.scenario, args.impl, single_run_dir=args.run_dir)
    if not scenarios:
        print(f"No results found under {args.results_dir}")
        return 1

    # Determine implementation order.
    # If user provided --impl, use that (filtered to those actually present).
    # Else, if all selected scenarios share an identical implementations list in scenario.json, use that.
    # Otherwise fall back to discovery order (first-seen across scenarios).
    if args.impl:
        impl_order = [i for i in args.impl if any(i in m for sc in scenarios.values() for m in sc.values())]
    else:
        collected_orders = []
        for sk in scenarios.keys():
            order = scenario_impl_orders.get(sk)
            if order:
                collected_orders.append(tuple(order))
        preferred: Optional[List[str]] = None
        if collected_orders:
            first = collected_orders[0]
            if all(o == first for o in collected_orders[1:]):
                preferred = list(first)
        if preferred:
            impl_order = preferred
        else:
            impls_seen: List[str] = []
            for sc_map in scenarios.values():
                for impl_map in sc_map.values():
                    for impl in impl_map.keys():
                        if impl not in impls_seen:
                            impls_seen.append(impl)
            impl_order = impls_seen

    baseline_impl = None if (args.relative_to and args.relative_to.lower() == 'none') else args.relative_to
    metric_y2 = None if (args.metric_y2 and args.metric_y2.lower() == 'none') else args.metric_y2
    files = plot_matplotlib(scenarios, var_order, scenario_subtitles, scenario_titles, scenario_impl_orders, scenario_var_labels, args.metric_y1, metric_y2, args.y2_mode,
                            run_dir_paths, baseline_impl=baseline_impl, debug_layout=args.debug_layout, hide_title=args.no_title)
    # Also generate latency percentile plots if .hdr or percentile CSV files exist
    lat_files = plot_latency_percentiles(scenarios, var_order, scenario_subtitles, scenario_titles, scenario_impl_orders, scenario_var_labels, run_dir_paths,
                                         percentiles=[50.0, 90.0, 99.0, 99.9, 99.99], debug_layout=args.debug_layout, hide_title=args.no_title)
    files.extend(lat_files)
    # Sort files by parent directory creation time in descending order
    files.sort(key=lambda p: p.parent.stat().st_ctime, reverse=True)

    def _hyper(f: Path) -> str:
        """Return path wrapped in OSC8 hyperlink (BEL terminated) with underline+blue styling if interactive.

        The visible text is always file://... for best compatibility with tmux/terminal auto-linking.
        """
        import os, sys
        if args.no_hyperlinks:
            return str(f)
        if not sys.stdout.isatty():
            return str(f)
        if os.environ.get("NO_HYPERLINKS") is not None:
            return str(f)
        try:
            url = f.resolve().as_uri()
            text = url  # always show file://... for best compatibility
            styled = f"\033[4;34m{text}\033[0m"
            return f"\033]8;;{url}\a{styled}\033]8;;\a"
        except Exception:
            return str(f)

    # Additionally list unique run directories (parents) so user can cmd+click the folder too
    parents_printed = set()
    print("Wrote:")
    for f in files:
        parent = f.parent
        if parent not in parents_printed:
            print(" -", _hyper(parent) + "/")
            parents_printed.add(parent) 
        print("   ", _hyper(f))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
