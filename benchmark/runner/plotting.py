from __future__ import annotations
import shlex
import subprocess
from pathlib import Path
from typing import List, Optional


def run_plot(results_root: Path, scenario_name: str, relative_to: str,
             impls: Optional[List[str]] = None, run_dir: Optional[Path] = None) -> int:
    from pathlib import Path as _Path
    import sys as _sys
    plot_script = _Path(__file__).resolve().parents[1] / "plot_results.py"
    cmd = [
        _sys.executable, str(plot_script),
        "--results-dir", str(results_root),
        "--scenario", scenario_name,
        "--relative-to", relative_to,
    ]
    if impls:
        for impl in impls:
            cmd += ["--impl", impl]
    if run_dir is not None:
        cmd += ["--run-dir", str(run_dir)]
    print(f"Auto-plot: {' '.join(shlex.quote(c) for c in cmd)}")
    return subprocess.call(cmd)
