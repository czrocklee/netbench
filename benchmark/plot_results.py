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
                   impl_filter: Optional[List[str]] = None) -> Tuple[Dict[str, Dict[int, Dict[str, RunPoint]]], Dict[str, List[int]]]:
    # Returns: scenarios -> var_val -> impl -> RunPoint
    # and scenarios -> sorted var values
    scenarios: Dict[str, Dict[int, Dict[str, RunPoint]]] = {}
    var_order: Dict[str, List[int]] = {}

    for sc_dir in sorted(results_dir.iterdir()):
        if not sc_dir.is_dir():
            continue
        sc_manifest = sc_dir / "scenario.json"
        if not sc_manifest.exists():
            continue

        scj = json.loads(sc_manifest.read_text())
        sc_name = scj.get("name")
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
    return scenarios, var_order


def plot_matplotlib(scenarios: Dict[str, Dict[int, Dict[str, RunPoint]]], var_order: Dict[str, List[int]],
                    impl_order: List[str], metric: str, out_dir: Path,
                    baseline_impl: Optional[str] = None) -> List[Path]:
    import matplotlib.pyplot as plt
    out_files: List[Path] = []
    out_dir.mkdir(parents=True, exist_ok=True)

    for sc_name, vmap in scenarios.items():
        xs = var_order[sc_name]
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

        for idx, impl in enumerate(impl_order):
            vals: List[float] = []
            texts: List[str] = []
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

            if baseline_impl:
                for i, v in enumerate(vals):
                    b = baseline_vals[i] if i < len(baseline_vals) else None
                    if b is not None and b > 0:
                        texts.append(f"{(v / b) * 100:.0f}%")
                    else:
                        texts.append("")

            positions = [p + idx * width for p in base_positions]
            #color = COLOR_MAP.get(impl, None)
            #ax.bar(positions, vals, width=width, label=impl, color=color, edgecolor="#333333")
            bars = ax.bar(positions, vals, width=width, label=impl, edgecolor="#333333")
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
        ax.set_xticklabels([f"{next(iter(vmap.values())).get(next(iter(next(iter(vmap.values())).keys()))).var_key if vmap else 'var'} = {x}" if vmap else str(x) for x in xs])

        y_label = {
            "msgs_per_sec": "Messages per Second",
        }.get(metric, "Throughput (Gbps)")
        ax.set_ylabel(y_label)
        ax.set_title(sc_name)
        ax.legend()
        ax.grid(axis='y', linestyle='--', alpha=0.3)
        fig.tight_layout()

        out_file = out_dir / f"{sc_name}.svg"
        fig.savefig(out_file, format="svg")
        plt.close(fig)
        out_files.append(out_file)

    return out_files


def plot_html(scenarios: Dict[str, Dict[int, Dict[str, RunPoint]]], var_order: Dict[str, List[int]],
              impl_order: List[str], metric: str, out_dir: Path,
              baseline_impl: Optional[str] = None) -> List[Path]:
    # Try Plotly for interactive HTML; fallback to embedding SVGs only if Plotly is missing
    out_dir.mkdir(parents=True, exist_ok=True)
    try:
        import plotly.graph_objects as go  # type: ignore
    except ImportError:
        print("Plotly is not installed; falling back to static SVG wrapped in HTML.")
        svgs = plot_matplotlib(scenarios, var_order, impl_order, metric, out_dir, baseline_impl=baseline_impl)
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
        for impl in impl_order:
            ys: List[float] = []
            texts: List[str] = []
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

            if baseline_impl:
                for i, x in enumerate(xs):
                    rp_b = vmap.get(x, {}).get(baseline_impl)
                    b = rp_b.msgs_per_sec() if (rp_b and metric == "msgs_per_sec") else None
                    if b is not None and b > 0:
                        texts.append(f"{(ys[i] / b) * 100:.0f}%")
                    else:
                        texts.append("")
            #fig.add_bar(name=impl, x=[str(x) for x in xs], y=ys, marker_color=COLOR_MAP.get(impl))
            if baseline_impl:
                fig.add_bar(name=impl, x=[str(x) for x in xs], y=ys, text=texts, textposition='outside')
            else:
                fig.add_bar(name=impl, x=[str(x) for x in xs], y=ys)

        y_label = {
            "throughput_gbps": "Throughput (Gbps)",
            "throughput_mbps": "Throughput (Mbps)",
            "mb_per_sec": "MB/s",
            "msgs": "Messages",
            "ops": "Ops",
        }.get(metric, "Throughput (Gbps)")
        fig.update_layout(
            barmode='group',
            title=sc_name,
            xaxis_title=vmap[next(iter(vmap))][next(iter(vmap[next(iter(vmap))]))].var_key if vmap else "var",
            yaxis_title=y_label,
            legend_title="Implementation",
        )
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
    args = ap.parse_args(argv)

    scenarios, var_order = gather_results(args.results_dir, args.scenario, args.impl)
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
        files = plot_matplotlib(scenarios, var_order, impl_order, args.metric, args.out_dir, baseline_impl=baseline_impl)
    else:
        files = plot_html(scenarios, var_order, impl_order, args.metric, args.out_dir, baseline_impl=baseline_impl)

    print("Wrote:")
    for f in files:
        print(" -", f)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
