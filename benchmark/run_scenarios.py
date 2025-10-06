#!/usr/bin/env python3
"""
Scenario runner entrypoint for netbench.

This file now only defines CLI arguments and default scenarios; the
implementation details live under benchmark/runner/.
"""
from __future__ import annotations
import argparse
import dataclasses as dc
from pathlib import Path
from typing import List, Optional, Sequence
import sys

# Support running both as a module (`python -m benchmark.run_scenarios`) and as a script
if __package__ is None or __package__ == "":
    # Add repo root to sys.path so absolute package imports work
    _REPO_ROOT = Path(__file__).resolve().parents[1]
    if str(_REPO_ROOT) not in sys.path:
        sys.path.insert(0, str(_REPO_ROOT))
    from benchmark.runner.constants import IMPL_BIN_NAME
    from benchmark.runner.types import FixedParams, Scenario
    from benchmark.runner.exec import run_from_args
    REPO_ROOT = _REPO_ROOT
else:
    from .runner.constants import IMPL_BIN_NAME
    from .runner.types import FixedParams, Scenario
    from .runner.exec import run_from_args
    REPO_ROOT = Path(__file__).resolve().parents[1]


def default_scenarios() -> List[Scenario]:
    fixed = FixedParams(
        address="127.0.0.1:19004",
        duration_sec=10,
        msg_size=32,
        senders=1,
        conns_per_sender=1,
        busy_spin=False,
        echo="none",
        buffer_size=32,
        max_batch_size=1024,
    )
    return [
        Scenario(
            name="receive_throughput_by_threads",
            title="Receive Throughput by Threads (msg_sz=32, buf_size=32)",
            fixed=fixed,
            var_key="workers",
            var_values=[1, 2, 4, 8],
            linkages={"senders": "workers"},
            implementations=["bsd", "uring", "asio", "asio_uring"],
        ),
        Scenario(
            name="receive_throughput_by_connections",
            title="Receive Throughput by Connections (msg_sz=32, buf_sz=1024)",
            fixed=dc.replace(fixed, buffer_size=1024, senders=4, max_batch_size=4),
            var_key="conns_per_sender",
            var_values=[1, 32, 128, 256],
            implementations=["bsd", "uring", "asio", "asio_uring"],
        ),
    ]


def main(argv: Optional[Sequence[str]] = None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--app-root", type=Path, default=REPO_ROOT / "build" / "release" / "app",
                   help="Directory containing binaries (e.g., build/app or build/release/app)")
    p.add_argument("--receiver-app-root", type=Path, help="Receiver app dir (override app-root)")
    p.add_argument("--client-app-root", type=Path, help="Client app dir (override app-root)")
    p.add_argument("--receiver-host", default="local", help="Receiver host (ssh target or 'local')")
    p.add_argument("--client-host", default="local", help="Client host (ssh target or 'local')")
    p.add_argument("--out", type=Path, default=REPO_ROOT / "results")
    p.add_argument("--scenario", action="append",
                   help="Scenario(s) to run; filter by scenario name")
    p.add_argument("--impl", action="append", choices=list(IMPL_BIN_NAME.keys()),
                   help="Limit implementations")
    p.add_argument("--impl-arg", action="append", default=[],
                   help="Extra receiver arg for impl, format impl=token (repeatable). "
                        "Example: --impl-arg uring=--zerocopy --impl-arg uring=true")
    p.add_argument("--dry-run", action="store_true")
    # FixedParams overrides
    p.add_argument("--fixed-params", "--fixed", dest="fixed_params", action="append", default=[],
                help="Override FixedParams for all scenarios, key=value (repeatable). "
                    "Example: --fixed-params duration_sec=15 --fixed-params msg_size=64")
    p.add_argument("--scenario-fixed", action="append", default=[],
                help="Override FixedParams for a specific scenario, scenario:key=value (repeatable). "
                    "Example: --scenario-fixed vary_workers:senders=2")
    # Auto-plot options
    p.add_argument("--auto-plot", action="store_true", help="Generate plots after each scenario")
    p.add_argument("--plot-output", choices=["svg", "html"], default="svg", help="Plot format")
    p.add_argument("--plot-relative-to", default="bsd", help="Baseline impl for percentage labels; use 'none' to disable")
    p.add_argument("--plot-out-dir", type=Path, help="Output directory for plots (defaults to results/plots)")
    p.add_argument("--scenario-var-values", action="append", default=[],
                   help="Override var_values for a specific scenario: 'scenario:CSV_OR_RANGE'. Example: --scenario-var-values vary_workers:1,2,4,8")
    args = p.parse_args(argv)

    scs = default_scenarios()
    return run_from_args(args, scs)


if __name__ == "__main__":
    raise SystemExit(main())
