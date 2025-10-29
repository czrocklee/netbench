from __future__ import annotations
import dataclasses as dc
import json
import re
import shlex
import signal
import subprocess
import sys
import time
import argparse
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Optional, Sequence, get_type_hints, Set, Iterable, Callable
import os

from .constants import RECEIVER_BIN_NAME, CLIENT_BIN_NAME, PINGPONG_BIN_NAME
from .types import FixedParams, Scenario
from .affinity import choose_cpus
from .utils import has_flag_or_kv
from .config import apply_fixedparams_overrides
from .utils import apply_scenario_var_values, run_plot as _run_plot
from .receiver_client import (
    ensure_receiver_binaries as rc_ensure_binaries,
    start_receiver as rc_start_receiver,
    run_client as rc_run_client,
    stop_receiver as rc_stop_receiver,
)
from .pingpong import (
    ensure_pingpong_binaries as pp_ensure_binaries,
    start_acceptor as pp_start_acceptor,
    run_initiator as pp_run_initiator,
)


REPO_ROOT = Path(__file__).resolve().parents[2]


def ts_utc_compact() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H-%M-%SZ")

# Single helper to assign CPUs when auto_cpu is enabled. Updates FixedParams in-place.
def assign_auto_cpus_ids(
    fixed: FixedParams,
    receiver_host: str,
    client_host: str,
    mode: str,
) -> None:
    """Populate CPU-related fields in FixedParams in-place when running locally.

    - mode == 'pingpong': assigns pp_acceptor_cpu, pp_initiator_cpu, and per-process sqpoll CPUs (uring only).
    - mode == 'receiver': assigns worker_cpus and sender_cpus, avoiding overlaps when both ends are local.
    """
    if mode == 'pingpong':
        used: Set[int] = set()
        for val in (fixed.pp_acceptor_cpu, fixed.pp_initiator_cpu, fixed.pp_acceptor_sqpoll_cpu, fixed.pp_initiator_sqpoll_cpu):
            if val is not None:
                used.add(int(val))

        if receiver_host == 'local':
            if fixed.pp_acceptor_cpu is None:
                acc = choose_cpus(1, exclude=used)
                if acc:
                    fixed.pp_acceptor_cpu = acc[0]
                    used.add(acc[0])
            if fixed.pp_acceptor_sqpoll_cpu is None:
                sq = choose_cpus(1, exclude=used)
                if sq:
                    fixed.pp_acceptor_sqpoll_cpu = sq[0]
                    used.add(sq[0])

        if client_host == 'local':
            if fixed.pp_initiator_cpu is None:
                ini = choose_cpus(1, exclude=used)
                if ini:
                    fixed.pp_initiator_cpu = ini[0]
                    used.add(ini[0])
            if fixed.pp_initiator_sqpoll_cpu is None:
                sqi = choose_cpus(1, exclude=used)
                if sqi:
                    fixed.pp_initiator_sqpoll_cpu = sqi[0]
                    used.add(sqi[0])

    else:
        # receiver/client runs
        if receiver_host == 'local' and not fixed.worker_cpus and int(fixed.workers) > 0:
            rcpu = choose_cpus(int(fixed.workers))
            if rcpu:
                fixed.worker_cpus = ",".join(str(c) for c in rcpu)

        if client_host == 'local' and not fixed.sender_cpus:
            senders = int(getattr(fixed, 'senders', 0))
            if senders > 0:
                exclude: Set[int] = set()
                if receiver_host == 'local' and fixed.worker_cpus:
                    try:
                        exclude = {int(x) for x in str(fixed.worker_cpus).split(',') if x}
                    except Exception:
                        exclude = set()
                scpu = choose_cpus(senders, exclude=exclude)
                if scpu:
                    fixed.sender_cpus = ",".join(str(c) for c in scpu)

class Runner:
    def __init__(self,
                 app_dir: Path,
                 receiver_host: str = "local",
                 client_host: str = "local",
                 receiver_app_dir: Optional[Path] = None,
                 client_app_dir: Optional[Path] = None,
                 impl_extra_args: Optional[Dict[str, List[str]]] = None,
                 auto_cpu_ids: bool = False,
                 client_extra_args: Optional[List[str]] = None):
        self.app_dir = app_dir
        self.receiver_host = receiver_host
        self.client_host = client_host
        self.receiver_app_dir = receiver_app_dir or app_dir
        self.client_app_dir = client_app_dir or app_dir
        self.impl_extra_args = impl_extra_args or {}
        # controls all automatic affinity selection (workers, senders, pingpong)
        self.auto_cpu_ids = auto_cpu_ids
        self.client_extra_args = client_extra_args or []

    def ensure_binaries(self, impls: Sequence[str]):
        if self.receiver_host == "local":
            rc_ensure_binaries(self.receiver_app_dir, impls)
        if self.client_host == "local":
            cpath = self.client_app_dir / CLIENT_BIN_NAME
            if not cpath.exists():
                raise FileNotFoundError(f"Missing local client binary: {cpath}. Set --app-root correctly.")

    def ensure_pingpong_binaries(self, impls: Sequence[str]):
        if self.receiver_host == "local":
            pp_ensure_binaries(self.receiver_app_dir, impls)

    def _start_receiver(self, impl: str, fixed: FixedParams, results_dir: Path,
                        extra_tags: Dict[str, str], extra_impl_args: List[str]) -> subprocess.Popen:
        return rc_start_receiver(self.receiver_host, self.receiver_app_dir, impl, fixed, results_dir,
                                 extra_tags, extra_impl_args)

    def _run_client(self, fixed: FixedParams, duration_sec: int, results_dir: Path) -> int:
        return rc_run_client(self.client_host, self.client_app_dir, fixed, duration_sec, results_dir,
                             self.client_extra_args)

    def _stop_receiver(self, proc: subprocess.Popen, timeout: float = 15.0):
        """
        Waits for the receiver process to exit gracefully.
        The receiver is expected to shut down on its own when the client disconnects.
        If it doesn't exit within the timeout, it will be forcibly killed.
        """
        return rc_stop_receiver(proc, timeout=timeout)

    # --- Pingpong helpers ---
    def _start_pingpong_acceptor(self, impl: str, fixed: FixedParams, results_dir: Path,
                                 extra_impl_args: Optional[List[str]] = None) -> subprocess.Popen:
        return pp_start_acceptor(self.receiver_host, self.receiver_app_dir, impl, fixed, results_dir, extra_impl_args)

    def _run_pingpong_initiator(self, impl: str, fixed: FixedParams, results_dir: Path,
                                tags: Dict[str, str], extra_impl_args: Optional[List[str]] = None) -> int:
        return pp_run_initiator(self.client_host, self.client_app_dir, impl, fixed, results_dir, tags, extra_impl_args)

    def run_scenario(self, sc: Scenario, out_root: Path, cli_args: Optional[argparse.Namespace] = None) -> Path:
        if getattr(sc, 'mode', 'receiver') == 'pingpong':
            self.ensure_pingpong_binaries(sc.implementations)
        else:
            self.ensure_binaries(sc.implementations)
        run_id = ts_utc_compact()
        sc_dir = out_root / f"{sc.name}_{run_id}"
        sc_dir.mkdir(parents=True, exist_ok=True)
        if cli_args:
            (sc_dir / "cli_args.json").write_text(json.dumps(vars(cli_args), indent=2, default=str))
        def _scenario_asdict_json(s: Scenario) -> Dict:
            d = dc.asdict(s)
            # Make linkages JSON-friendly if they contain callables
            try:
                links = d.get("linkages") or {}
                for k, v in list(links.items()):
                    if callable(v):
                        name = getattr(v, "__name__", None)
                        links[k] = f"callable:{name or repr(v)}"
            except Exception:
                pass
            return d

        (sc_dir / "scenario.json").write_text(json.dumps(_scenario_asdict_json(sc), indent=2))

        for impl in sc.implementations:
            impl_dir = sc_dir / impl
            impl_dir.mkdir(parents=True, exist_ok=True)
            for val in sc.var_values:
                run_dir = impl_dir / f"{sc.var_key}_{val}"
                run_dir.mkdir(parents=True, exist_ok=True)

                fixed = dc.replace(sc.fixed)
                setattr(fixed, sc.var_key, val)
                # Apply linkages to compute dependent fields
                if sc.linkages:
                    for target, expr in sc.linkages.items():
                        if not hasattr(fixed, target):
                            raise AttributeError(f"FixedParams has no field '{target}' (from linkage)")
                        # Only single-argument callables are supported: fn(fixed) -> Any
                        if not callable(expr):
                            raise TypeError(
                                f"Unsupported linkage type for '{target}': expected callable, got {type(expr).__name__}")
                        result = expr(fixed)
                        # Coerce to the type of the target field
                        current = getattr(fixed, target)
                        if isinstance(current, bool):
                            setattr(fixed, target, bool(int(round(result))))
                        elif isinstance(current, int):
                            setattr(fixed, target, int(round(result)))
                        elif isinstance(current, str):
                            setattr(fixed, target, str(int(round(result))))
                        else:
                            setattr(fixed, target, result)

                tags = {"scenario": sc.name, "impl": impl, sc.var_key: str(val)}
                extra_impl = list(sc.impl_extra.get(impl, [])) + list(self.impl_extra_args.get(impl, []))

                if getattr(sc, 'mode', 'receiver') == 'pingpong':
                    # Start acceptor then run initiator which writes results into run_dir
                    if self.auto_cpu_ids:
                        assign_auto_cpus_ids(fixed, self.receiver_host, self.client_host, 'pingpong')

                    aproc = self._start_pingpong_acceptor(impl, fixed, run_dir, extra_impl_args=extra_impl)
                    time.sleep(3)
                    rc = self._run_pingpong_initiator(impl, fixed, run_dir, tags, extra_impl_args=extra_impl)
                    if rc != 0:
                        print(f"Pingpong initiator exited with {rc} for {impl} {sc.var_key}={val}", file=sys.stderr)
                    self._stop_receiver(aproc)
                else:
                    if self.auto_cpu_ids:
                        assign_auto_cpus_ids(fixed, self.receiver_host, self.client_host, 'receiver')

                    rproc = self._start_receiver(impl, fixed, run_dir, tags, extra_impl)
                    time.sleep(3)
                    rc = self._run_client(fixed, sc.fixed.duration_sec, run_dir)
                    if rc != 0:
                        print(f"Client exited with {rc} for {impl} {sc.var_key}={val}", file=sys.stderr)
                    self._stop_receiver(rproc)

        print(f"Scenario '{sc.name}' finished: {sc_dir}")
        return sc_dir


def run_from_args(args, scenarios: List[Scenario]) -> int:
    # Filter scenarios and implementations
    scs = list(scenarios)
    if getattr(args, 'scenario', None):
        scs = [s for s in scs if s.name in args.scenario]
    if getattr(args, 'impl', None):
        for s in scs:
            s.implementations = [i for i in s.implementations if i in args.impl]

    # Parse impl extra args
    impl_extra_args: Dict[str, List[str]] = {}
    for item in getattr(args, 'impl_arg', []) or []:
        if "=" not in item:
            print(f"Ignoring --impl-arg without '=': {item}", file=sys.stderr)
            continue
        impl, tok = item.split("=", 1)
        impl = impl.strip()
        if impl not in RECEIVER_BIN_NAME:
            print(f"Unknown impl in --impl-arg: {impl}", file=sys.stderr)
            continue
        impl_extra_args.setdefault(impl, []).append(tok)

    runner = Runner(
        app_dir=args.app_root,
        receiver_host=args.receiver_host,
        client_host=args.client_host,
        receiver_app_dir=args.receiver_app_root,
        client_app_dir=args.client_app_root,
        impl_extra_args=impl_extra_args,
        auto_cpu_ids=getattr(args, 'auto_cpu_ids', False),
    )

    args.out.mkdir(parents=True, exist_ok=True)

    # Apply FixedParams overrides
    apply_fixedparams_overrides(scs, getattr(args, 'fixed_params', None), getattr(args, 'scenario_fixed', None))

    # Parse helper for var_values specs
    def _parse_int_list_spec(spec: str) -> List[int]:
        s = spec.strip()
        if not s:
            return []
        if "," in s:
            try:
                return [int(x.strip()) for x in s.split(",") if x.strip()]
            except Exception:
                raise ValueError(f"Invalid CSV list: {spec}")
        m = re.match(r"^\s*(-?\d+)\s*\.\.\s*(-?\d+)(?::\s*(-?\d+)\s*)?$", s)
        if m:
            start = int(m.group(1)); end = int(m.group(2)); step = int(m.group(3)) if m.group(3) else (1 if end >= start else -1)
            if step == 0:
                raise ValueError("Range step cannot be 0")
            vals: List[int] = []
            if step > 0:
                i = start
                while i <= end:
                    vals.append(i); i += step
            else:
                i = start
                while i >= end:
                    vals.append(i); i += step
            return vals
        if re.match(r"^\s*-?\d+\s*$", s):
            return [int(s)]
        raise ValueError(f"Invalid var-values spec: {spec}")

    # Apply var_values overrides
    apply_scenario_var_values(scs, getattr(args, 'scenario_var_values', []))

    if getattr(args, 'dry_run', False):
        print("Planned runs:")
        def _scenario_asdict_json(s: Scenario) -> Dict:
            d = dc.asdict(s)
            links = d.get("linkages") or {}
            for k, v in list(links.items()):
                if callable(v):
                    name = getattr(v, "__name__", None)
                    links[k] = f"callable:{name or repr(v)}"
            return d
        for s in scs:
            print(json.dumps(_scenario_asdict_json(s), indent=2))
        print("Runner:", json.dumps({
            "receiver_host": args.receiver_host,
            "client_host": args.client_host,
            "receiver_app_dir": str(args.receiver_app_root or args.app_root),
            "client_app_dir": str(args.client_app_root or args.app_root),
            "impl_extra_args": impl_extra_args,
        }, indent=2))
        return 0

    for s in scs:
        sc_dir = runner.run_scenario(s, args.out, cli_args=args)
        if getattr(args, 'auto_plot', False):
            # Per-run only plotting directly inside the scenario run directory
            _ = _run_plot(
                args.out,
                s.name,
                args.plot_relative_to,
                impls=list(s.implementations),
                run_dir=sc_dir,
            )
    return 0
