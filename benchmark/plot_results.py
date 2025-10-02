#!/usr/bin/env python3
"""
Aggregate netbench results and plot grouped bar charts.

Features
- Scans results/<scenario_ts>/<impl>/<var_key>_<val>/ for metrics.json + metadata.json
- Uses scenario.json (one level up) to learn var_key and default sorting
- Computes throughput from metrics (sum bytes / duration) or falls back to scenario duration
- Outputs SVG by default; optional HTML (interactive with Plotly if available, else wraps the SVG)

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


COLOR_MAP = {
    "uring": "#e41a1c",   # red
    "asio": "#ff7f00",    # orange
    "asio_uring": "#984ea3",
    "bsd": "#377eb8",     # blue
}

@dataclass
class RunPoint:
    scenario: str
    impl: str
    var_key: str
    var_val: int
    bytes_total: int
    msgs_total: int
    ops_total: int
    duration_sec: float  # derived from timestamps or scenario fallback

    def msgs_per_sec(self) -> float:
        if self.duration_sec <= 0:
            return float("nan")
        return self.msgs_total / self.duration_sec


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


def infer_var_from_dir(name: str) -> Tuple[str, int]:
    # pattern like workers_4 or msg_size_1024
    m = re.match(r"([a-zA-Z0-9_]+)_([0-9]+)$", name)
    if not m:
        raise ValueError(f"Cannot infer var from directory name: {name}")
    return m.group(1), int(m.group(2))


def gather_results(results_dir: Path, scenario_filter: Optional[List[str]] = None,
                   impl_filter: Optional[List[str]] = None) -> Tuple[
                       Dict[str, Dict[int, Dict[str, RunPoint]]],
                       Dict[str, List[int]],
                       Dict[str, str],
                       Dict[str, str]
                   ]:
    # Returns:
    #  - scenarios -> var_val -> impl -> RunPoint
    #  - scenarios -> sorted var values
    #  - scenarios -> subtitle (e.g., "CPU | kernel")
    scenarios: Dict[str, Dict[int, Dict[str, RunPoint]]] = {}
    var_order: Dict[str, List[int]] = {}
    subtitles: Dict[str, str] = {}
    titles: Dict[str, str] = {}

    for sc_dir in sorted(results_dir.iterdir()):
        if not sc_dir.is_dir():
            continue
        sc_manifest = sc_dir / "scenario.json"
        if not sc_manifest.exists():
            continue

        scj = json.loads(sc_manifest.read_text())
        sc_name = scj.get("name")
        # Optional friendly title for plots
        sc_title = scj.get("title") or sc_name
        if sc_title:
            titles[sc_name] = sc_title
        if scenario_filter and sc_name not in scenario_filter:
            continue
        var_key = scj.get("var_key")
        expected_vars = scj.get("var_values", [])
        expected_vals = [int(v) for v in expected_vars]

        scenarios.setdefault(sc_name, {})
        if sc_name not in var_order:
            var_order[sc_name] = expected_vals or []

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
                    if sc_name not in subtitles:
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
                            subtitles[sc_name] = " | ".join([s for s in [cpu, kernel_label] if s])
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
                    scenario=sc_name,
                    impl=impl,
                    var_key=var_key,
                    var_val=var_val,
                    bytes_total=total_bytes,
                    msgs_total=total_msgs,
                    ops_total=total_ops,
                    duration_sec=duration_sec,
                )
                scenarios[sc_name].setdefault(var_val, {})[impl] = rp

    # Fill var order from discovered values if not in manifest
    for sc_name, vmap in scenarios.items():
        if not var_order.get(sc_name):
            var_order[sc_name] = sorted(vmap.keys())
    return scenarios, var_order, subtitles, titles


def plot_matplotlib(scenarios: Dict[str, Dict[int, Dict[str, RunPoint]]], var_order: Dict[str, List[int]],
                    scenario_subtitles: Dict[str, str], scenario_titles: Dict[str, str], impl_order: List[str], metric: str, out_dir: Path,
                    baseline_impl: Optional[str] = None, debug_layout: bool = False) -> List[Path]:
    import matplotlib.pyplot as plt
    out_files: List[Path] = []
    out_dir.mkdir(parents=True, exist_ok=True)

    for sc_name, vmap in scenarios.items():
        xs = var_order.get(sc_name, sorted(vmap.keys()))
        n_groups = len(xs)
        n_impls = len(impl_order)
        width = 0.8 / max(n_impls, 1)

        fig, ax = plt.subplots(figsize=(max(6, n_groups * 2.0), 4))
        base_positions = list(range(n_groups))

        # Precompute baseline values per x for annotations
        baseline_vals: List[Optional[float]] = []
        if baseline_impl:
            for x in xs:
                rp_b = vmap.get(x, {}).get(baseline_impl)
                if rp_b and metric == "msgs_per_sec":
                    baseline_vals.append(rp_b.msgs_per_sec())
                else:
                    baseline_vals.append(None)

        # First pass: compute raw values per impl to determine y-axis scale
        raw_vals_by_impl: Dict[str, List[float]] = {}
        for impl in impl_order:
            vals: List[float] = []
            for x in xs:
                rp = vmap.get(x, {}).get(impl)
                if not rp:
                    v = 0.0
                else:
                    if metric == "msgs_per_sec":
                        v = rp.msgs_per_sec()
                    else:
                        v = 0.0
                vals.append(v)
            raw_vals_by_impl[impl] = vals

        # Decide scale unit: '', 'k', 'm', 'b'
        y_max = 0.0
        for vals in raw_vals_by_impl.values():
            for v in vals:
                if math.isfinite(v):
                    y_max = max(y_max, v)
        if y_max >= 1e9:
            scale = 1e9; unit = 'b'
        elif y_max >= 1e6:
            scale = 1e6; unit = 'm'
        elif y_max >= 1e3:
            scale = 1e3; unit = 'k'
        else:
            scale = 1.0; unit = ''

        for idx, impl in enumerate(impl_order):
            raw_vals = raw_vals_by_impl.get(impl, [])
            vals_scaled: List[float] = [(v / scale) if math.isfinite(v) else 0.0 for v in raw_vals]
            texts: List[str] = []

            if baseline_impl:
                for i, v in enumerate(raw_vals):
                    b = baseline_vals[i] if i < len(baseline_vals) else None
                    if b is not None and b > 0:
                        texts.append(f"{(v / b) * 100:.0f}%")
                    else:
                        texts.append("")

            positions = [p + idx * width for p in base_positions]
            bars = ax.bar(positions, vals_scaled, width=width, label=impl, edgecolor="#333333")
            if baseline_impl:
                for rect, txt in zip(bars, texts):
                    if not txt:
                        continue
                    height = rect.get_height()
                    ax.text(rect.get_x() + rect.get_width() / 2.0, height,
                            txt, ha='center', va='bottom', fontsize=8)

        # X ticks centered under each group
        centers = [p + (n_impls - 1) * width / 2 for p in base_positions]
        ax.set_xticks(centers)
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
        ax.set_xticklabels([f"{var_key_name} = {x}" for x in xs])

        y_label = {
            "msgs_per_sec": "Messages per Second",
        }.get(metric, "Throughput (Gbps)")
        if unit:
            y_label = f"{y_label} ({unit})"
        ax.set_ylabel(y_label)
        try:
            ax.ticklabel_format(style='plain', axis='y')
        except Exception:
            pass

        subtitle = scenario_subtitles.get(sc_name)
        supt: Optional[object] = None
        subtxt: Optional[object] = None
        # Larger main title centered; subtitle centered directly beneath, above the plot
        try:
            main_title = scenario_titles.get(sc_name, sc_name)
            supt = fig.suptitle(main_title, fontsize=16, y=0.975)
            if subtitle:
                # Subtitle centered below main title
                subtxt = fig.text(0.98, 0.855, subtitle, ha='right', va='top', fontsize=10, color='#555')
                # Bring plot area closer to titles (larger 'top' means less reserved space)
                fig.subplots_adjust(top=0.92)
            else:
                fig.subplots_adjust(top=0.95)
        except Exception:
            # Fallback: use axes title only
            ax.set_title(scenario_titles.get(sc_name, sc_name))

        ax.legend()
        ax.grid(axis='y', linestyle='--', alpha=0.3)
        try:
            # Reserve just enough space for the titles
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
                pos = ax.get_position()
                add_rect(pos.x0, pos.y0, pos.width, pos.height, '#0a0')
                # Legend box
                leg = ax.get_legend()
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

        out_file = out_dir / f"{sc_name}.svg"
        fig.savefig(out_file, format="svg")
        plt.close(fig)
        out_files.append(out_file)

    return out_files


def plot_html(scenarios: Dict[str, Dict[int, Dict[str, RunPoint]]], var_order: Dict[str, List[int]],
              scenario_subtitles: Dict[str, str], scenario_titles: Dict[str, str], impl_order: List[str], metric: str, out_dir: Path,
              baseline_impl: Optional[str] = None, debug_layout: bool = False) -> List[Path]:
    # Try Plotly for interactive HTML; fallback to embedding SVGs only if Plotly is missing
    out_dir.mkdir(parents=True, exist_ok=True)
    try:
        import plotly.graph_objects as go  # type: ignore
    except ImportError:
        print("Plotly is not installed; falling back to static SVG wrapped in HTML.")
        svgs = plot_matplotlib(scenarios, var_order, scenario_subtitles, impl_order, metric, out_dir, baseline_impl=baseline_impl, debug_layout=debug_layout)
        out_files: List[Path] = []
        for svg in svgs:
            html_path = svg.with_suffix('.html')
            html_path.write_text(f"""
<!doctype html>
<html><head><meta charset=\"utf-8\"><title>{svg.stem}</title></head>
<body><img src=\"{svg.name}\" alt=\"{svg.stem}\"/></body></html>
"""
            )
            out_files.append(html_path)
        return out_files

    out_files: List[Path] = []
    for sc_name, vmap in scenarios.items():
        xs = var_order[sc_name]
        fig = go.Figure()

        # First pass: gather raw values by implementation
        raw_by_impl: Dict[str, List[float]] = {}
        for impl in impl_order:
            ys: List[float] = []
            for x in xs:
                rp = vmap.get(x, {}).get(impl)
                if not rp:
                    v = 0.0
                else:
                    if metric == "msgs_per_sec":
                        v = rp.msgs_per_sec()
                    else:
                        v = 0.0
                ys.append(v)
            raw_by_impl[impl] = ys

        # Decide scale unit based on max
        y_max = 0.0
        for ys in raw_by_impl.values():
            for v in ys:
                if math.isfinite(v):
                    y_max = max(y_max, v)
        if y_max >= 1e9:
            scale = 1e9; unit = 'b'
        elif y_max >= 1e6:
            scale = 1e6; unit = 'm'
        elif y_max >= 1e3:
            scale = 1e3; unit = 'k'
        else:
            scale = 1.0; unit = ''

        # Plot scaled bars per implementation
        for impl in impl_order:
            ys_raw = raw_by_impl.get(impl, [])
            ys_scaled = [(v / scale) if math.isfinite(v) else 0.0 for v in ys_raw]
            texts: List[str] = []
            if baseline_impl:
                for i, x in enumerate(xs):
                    rp_b = vmap.get(x, {}).get(baseline_impl)
                    b = rp_b.msgs_per_sec() if (rp_b and metric == "msgs_per_sec") else None
                    if b is not None and b > 0:
                        texts.append(f"{(ys_raw[i] / b) * 100:.0f}%")
                    else:
                        texts.append("")
            if baseline_impl:
                fig.add_bar(name=impl, x=[str(x) for x in xs], y=ys_scaled, text=texts, textposition='outside')
            else:
                fig.add_bar(name=impl, x=[str(x) for x in xs], y=ys_scaled)

        y_label = {
            "throughput_gbps": "Throughput (Gbps)",
            "throughput_mbps": "Throughput (Mbps)",
            "mb_per_sec": "MB/s",
            "msgs": "Messages",
            "ops": "Ops",
        }.get(metric, "Throughput (Gbps)")
        if unit:
            y_label = f"{y_label} ({unit})"
        subtitle = scenario_subtitles.get(sc_name)
        main_title = scenario_titles.get(sc_name, sc_name)
        # Use two-line title for Plotly: main title then subtitle beneath
        if subtitle:
            title_text = f"{main_title}<br><span style='font-size:0.9em;color:#555'>{subtitle}</span>"
        else:
            title_text = main_title
        fig.update_layout(
            barmode='group',
            title={'text': title_text},
            xaxis_title=vmap[next(iter(vmap))][next(iter(vmap[next(iter(vmap))]))].var_key if vmap else "var",
            yaxis_title=y_label,
            legend_title="Implementation",
            margin=dict(t=70)
        )
        if debug_layout:
            # Paper border
            fig.add_shape(type='rect', xref='paper', yref='paper', x0=0, y0=0, x1=1, y1=1,
                          line=dict(color='red', width=1, dash='dot'))
            # Plot (axes) domain
            try:
                dx = getattr(fig.layout.xaxis, 'domain', [0, 1])
                dy = getattr(fig.layout.yaxis, 'domain', [0, 1])
                fig.add_shape(type='rect', xref='paper', yref='paper', x0=dx[0], y0=dy[0], x1=dx[1], y1=dy[1],
                              line=dict(color='green', width=1, dash='dot'))
            except Exception:
                pass
            # Legend border
            fig.update_layout(legend=dict(bordercolor='#f0f', borderwidth=1))
        out_file = out_dir / f"{sc_name}.html"
        fig.write_html(out_file, include_plotlyjs='cdn', full_html=True)
        out_files.append(out_file)
    return out_files


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-dir", type=Path, default=Path("results"))
    ap.add_argument("--scenario", action="append", help="Scenario(s) to include; default all")
    ap.add_argument("--impl", action="append", help="Implementations to include and order")
    ap.add_argument("--metric", default="msgs_per_sec", choices=["msgs_per_sec"])
    ap.add_argument("--relative-to", default="bsd", help="Baseline implementation for percentage labels; use 'none' to disable")
    ap.add_argument("--output", default="svg", choices=["svg", "html"])
    ap.add_argument("--out-dir", type=Path, default=Path("results/plots"))
    ap.add_argument("--debug-layout", action="store_true", help="Draw bounding boxes for figure parts (titles/axes/legend) to debug layout")
    args = ap.parse_args(argv)

    scenarios, var_order, scenario_subtitles, scenario_titles = gather_results(args.results_dir, args.scenario, args.impl)
    if not scenarios:
        print(f"No results found under {args.results_dir}")
        return 1

    # Compute impl order from union of impls seen, respecting filter order if given
    impls_seen = []
    for sc_map in scenarios.values():
        for impl_map in sc_map.values():
            for impl in impl_map.keys():
                if impl not in impls_seen:
                    impls_seen.append(impl)
    impl_order = args.impl or impls_seen

    baseline_impl = None if (args.relative_to and args.relative_to.lower() == 'none') else args.relative_to
    if args.output == "svg":
        files = plot_matplotlib(scenarios, var_order, scenario_subtitles, scenario_titles, impl_order, args.metric, args.out_dir, baseline_impl=baseline_impl, debug_layout=args.debug_layout)
    else:
        files = plot_html(scenarios, var_order, scenario_subtitles, scenario_titles, impl_order, args.metric, args.out_dir, baseline_impl=baseline_impl, debug_layout=args.debug_layout)

    print("Wrote:")
    for f in files:
        print(" -", f)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
