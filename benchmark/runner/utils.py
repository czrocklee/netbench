from __future__ import annotations
from typing import Iterable, List, Optional
import re
import shlex
import subprocess
from pathlib import Path

def has_flag_or_kv(tokens: Iterable[str], name: str) -> bool:
    """Return True if name (flag) or name= (kv) appears in the token list."""
    name_eq = name + "="
    for tok in tokens or []:
        if tok == name or tok.startswith(name_eq):
            return True
    return False


# --- Scenario var_values parsing ---

def parse_number_list_spec(spec: str) -> List[float]:
    s = spec.strip()
    if not s:
        return []
    if "," in s:
        try:
            return [float(x.strip()) for x in s.split(",") if x.strip()]
        except Exception:
            raise ValueError(f"Invalid CSV list: {spec}")
    m = re.match(r"^\s*(-?\d+)\s*\.\.\s*(-?\d+)(?::\s*(-?\d+)\s*)?$", s)
    if m:
        start = int(m.group(1))
        end = int(m.group(2))
        step = int(m.group(3)) if m.group(3) else (1 if end >= start else -1)
        if step == 0:
            raise ValueError("Range step cannot be 0")
        vals: List[int] = []
        if step > 0:
            i = start
            while i <= end:
                vals.append(i)
                i += step
        else:
            i = start
            while i >= end:
                vals.append(i)
                i += step
        return [float(v) for v in vals]
    if re.match(r"^\s*-?\d+(?:\.\d+)?\s*$", s):
        return [float(s)]
    raise ValueError(f"Invalid var-values spec: {spec}")


def apply_scenario_var_values(scenarios, items):
    for item in items or []:
        if ":" not in item:
            continue
        scen, spec = item.split(":", 1)
        try:
            vals = parse_number_list_spec(spec)
        except Exception:
            continue
        for sc in scenarios:
            if sc.name == scen:
                sc.var_values = list(vals)


# --- Plotting helper ---

def run_plot(results_root: Path, scenario_name: str, relative_to: str,
             impls: Optional[List[str]] = None, run_dir: Optional[Path] = None,
             no_title: bool = False) -> int:
    plot_script = Path(__file__).resolve().parents[1] / "plot_results.py"
    cmd = [
        "python3", str(plot_script),
        "--results-dir", str(results_root),
        "--scenario", scenario_name,
        "--relative-to", relative_to,
    ]
    if impls:
        for impl in impls:
            cmd += ["--impl", impl]
    if run_dir is not None:
        cmd += ["--run-dir", str(run_dir)]
    if no_title:
        cmd += ["--no-title"]
    print(f"Auto-plot: {' '.join(shlex.quote(c) for c in cmd)}")
    return subprocess.call(cmd)
