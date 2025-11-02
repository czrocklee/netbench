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
    from benchmark.runner.constants import RECEIVER_BIN_NAME
    from benchmark.runner.types import FixedParams, Scenario
    from benchmark.runner.exec import run_from_args

    REPO_ROOT = _REPO_ROOT
else:
    from .runner.constants import RECEIVER_BIN_NAME
    from .runner.types import FixedParams, Scenario
    from .runner.exec import run_from_args

    REPO_ROOT = Path(__file__).resolve().parents[1]


def get_default_ip_address() -> str:
    import socket

    s = None
    try:
        # This is a common trick to get the primary non-loopback IP.
        # It creates a UDP socket and "connects" to an external address.
        # This doesn't send any data but makes the OS select an interface.
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
    except Exception:
        # Fallback to localhost if the above fails (e.g., no network)
        ip = "127.0.0.1"
    finally:
        if s:
            s.close()
    return ip


def default_scenarios() -> List[Scenario]:
    fixed = FixedParams(
        address=f"{get_default_ip_address()}:19004",
        duration_sec=30,
        msg_size=32,
        senders=1,
        conns=1,
        busy_spin=False,
        echo="none",
        drain=False,
        buffer_size=32,
        max_send_batch_size=1024,
        metric_hud_interval_secs=0,
        collect_latency_every_n_samples=0,
    )
    scs: List[Scenario] = [
        Scenario(
            name="receive_throughput_by_buffer_size",
            title="Receive Throughput vs Buffer Size",
            fixed=fixed,
            var_key="buffer_size",
            var_values=[32, 128, 256, 512],
            implementations=["bsd", "uring", "asio", "asio_uring"],
        ),
        Scenario(
            name="echo_throughput_by_buffer_size",
            title="Echo (per_op) Throughput vs Buffer Size",
            fixed=dc.replace(fixed, echo="per_op", drain=True),
            var_key="buffer_size",
            var_values=[32, 128, 256, 512],
            implementations=["bsd", "uring", "asio", "asio_uring"],
        ),
        Scenario(
            name="receive_throughput_by_connections",
            title="Receive Throughput vs connections",
            fixed=dc.replace(fixed, buffer_size=512, bsd_read_limit = 32 * 1024),
            var_key="conns",
            var_values=[1, 4, 16, 256],
            linkages={"uring_buffer_count": lambda params: int(params.conns * 128)},
            implementations=["bsd", "uring", "asio", "asio_uring"],
        ),
        Scenario(
            name="receive_throughput_by_threads",
            title="Receive Throughput vs Threads",
            fixed=fixed,
            var_key="workers",
            var_values=[1, 2, 4, 8],
            linkages={"senders": lambda params: int(params.workers)},
            implementations=["bsd", "uring", "asio", "asio_uring"],
        ),
        Scenario(
            name="receive_latency_by_message_rate",
            title="Receive Latency vs Message Rate",
            fixed=dc.replace(fixed, nodelay=True),
            var_key="msgs_per_sec",
            var_values=[1000, 10_000, 100_000, 1000_000],
            linkages={
                "collect_latency_every_n_samples": lambda params: max(
                    int(params.msgs_per_sec) // 10_000, 1
                )
            },
            implementations=["bsd", "uring", "asio", "asio_uring"],
        ),
    ]
    # Add pingpong defaults: vary msgs_per_sec and msg_size
    pp_fixed = dc.replace(
        fixed,
        metric_hud_interval_secs=0,
        warmup_count=10000,
        max_samples=0,
    )
    scs += [
        Scenario(
            name="pingpong_latency_by_msg_rate",
            title="Pingpong RTT vs Message Rate",
            fixed=pp_fixed,
            var_key="msgs_per_sec",
            var_values=[0, 1000, 10000, 100000],
            implementations=["bsd", "uring", "uring_sqpoll", "asio", "asio_uring"],
            mode="pingpong",
        ),
        Scenario(
            name="pingpong_latency_by_msg_size",
            title="Pingpong RTT vs Message Size",
            fixed=pp_fixed,
            var_key="msg_size",
            var_values=[32, 128, 512, 2048],
            linkages={"buffer_size": lambda params: int(params.msg_size)},
            implementations=["bsd", "uring", "uring_sqpoll", "asio", "asio_uring"],
            mode="pingpong",
        ),
    ]
    return scs


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
    p.add_argument("--impl", action="append", choices=list(RECEIVER_BIN_NAME.keys()),
                   help="Limit implementations")
    p.add_argument("--impl-arg", action="append", default=[],
                   help="Extra receiver arg for impl, format impl=token (repeatable). "
                        "Example: --impl-arg uring=--zerocopy --impl-arg uring=true")
    p.add_argument("--dry-run", action="store_true")
    # CPU affinity automation: when set, the runner will auto-assign CPUs unless FixedParams overrides are provided
    p.add_argument("--auto-cpu-ids", action="store_true", help="Automatically assign CPU affinity for workers/senders and pingpong processes when not explicitly set in FixedParams")
    # FixedParams overrides
    p.add_argument("--fixed-params", "--fixed", dest="fixed_params", action="append", default=[],
                help="Override FixedParams for all scenarios, key=value (repeatable). "
                    "Example: --fixed-params duration_sec=15 --fixed-params msg_size=64")
    p.add_argument("--scenario-fixed", action="append", default=[],
                help="Override FixedParams for a specific scenario, scenario:key=value (repeatable). "
                    "Example: --scenario-fixed vary_workers:senders=2")
    # Auto-plot options
    p.add_argument("--auto-plot", action="store_true", help="Generate plots after each scenario")
    p.add_argument("--plot-relative-to", default="bsd", help="Baseline impl for percentage labels; use 'none' to disable")
    p.add_argument("--plot-no-title", action="store_true", help="Hide the main title text in plots")
    p.add_argument("--plot-out-dir", type=Path, help="Output directory for plots (defaults to results/plots)")
    p.add_argument("--scenario-var-values", action="append", default=[],
                   help="Override var_values for a specific scenario: 'scenario:CSV_OR_RANGE'. Example: --scenario-var-values vary_workers:1,2,4,8")
    args = p.parse_args(argv)

    scs = default_scenarios()
    return run_from_args(args, scs)


if __name__ == "__main__":
    raise SystemExit(main())
