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
from typing import Dict, List, Optional, Sequence, get_type_hints, Set, Iterable
import os

from .constants import IMPL_BIN_NAME, CLIENT_BIN_NAME
from .types import FixedParams, Scenario
from .linkage import eval_link_expr


REPO_ROOT = Path(__file__).resolve().parents[2]


def ts_utc_compact() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H-%M-%SZ")


def _parse_cpulist_spec(spec: str) -> List[int]:
    # Parse cpulist strings like "0-3,8,10-11"
    out: List[int] = []
    for part in spec.split(","):
        p = part.strip()
        if not p:
            continue
        if "-" in p:
            a, b = p.split("-", 1)
            start = int(a); end = int(b)
            if start <= end:
                out.extend(range(start, end + 1))
            else:
                out.extend(range(start, end - 1, -1))
        else:
            out.append(int(p))
    return out


def _available_cpus() -> List[int]:
    # Prefer current process affinity if available
    try:
        if hasattr(os, "sched_getaffinity"):
            return sorted(int(c) for c in os.sched_getaffinity(0))
    except Exception:
        pass
    try:
        n = os.cpu_count() or 1
        return list(range(n))
    except Exception:
        return [0]


def _thread_sibling_groups(avail: Set[int]) -> List[List[int]]:
    # Build SMT sibling groups from sysfs; each group is a list of CPU ids (primary first)
    groups: List[List[int]] = []
    seen: Set[int] = set()
    base = "/sys/devices/system/cpu"
    for cpu in sorted(avail):
        if cpu in seen:
            continue
        path = os.path.join(base, f"cpu{cpu}", "topology", "thread_siblings_list")
        try:
            with open(path, "r") as f:
                spec = f.read().strip()
            sibs = [c for c in _parse_cpulist_spec(spec) if c in avail]
            if not sibs:
                sibs = [cpu]
        except Exception:
            sibs = [cpu]
        for c in sibs:
            seen.add(c)
        groups.append(sorted(sibs))
    # Sort groups by their primary (lowest id)
    groups.sort(key=lambda g: g[0] if g else 1 << 30)
    return groups


def choose_worker_cpus(n_workers: int) -> List[int]:
    """Choose up to n_workers CPU ids prioritizing one hardware thread per core first.

    Strategy: use sysfs thread_siblings_list to get SMT groups, pick one per group, then fill siblings if needed.
    Falls back to simple ascending CPU ids.
    """
    avail_list = _available_cpus()
    avail: Set[int] = set(avail_list)
    if not avail:
        return []
    groups = _thread_sibling_groups(avail)
    # Prefer non-zero CPUs first: stable-partition groups so the group whose primary is 0 comes last
    groups_nonzero = [g for g in groups if g and g[0] != 0]
    groups_zero = [g for g in groups if g and g[0] == 0]
    ordered_groups = groups_nonzero + groups_zero

    order: List[int] = []
    # First pass: pick one hardware thread per core (exclude CPU 0 primary until the end)
    for g in ordered_groups:
        if not g:
            continue
        c = g[0]
        if c == 0 and len(groups_zero) == 1:
            # Defer CPU 0 until we really need it
            continue
        if len(order) >= n_workers:
            break
        order.append(c)

    if len(order) < n_workers:
        # If still short, fill with siblings, skipping CPU 0 until the very end
        for g in ordered_groups:
            # start from siblings
            for c in g[1:]:
                if c == 0:
                    continue
                if len(order) >= n_workers:
                    break
                order.append(c)
            if len(order) >= n_workers:
                break

    if len(order) < n_workers and 0 in avail:
        # Finally include CPU 0 if we still need more
        order.append(0)

    return order[:n_workers]


def choose_cpus(n: int, exclude: Optional[Set[int]] = None) -> List[int]:
    """Choose up to n CPUs similar to choose_worker_cpus, with optional exclusion set."""
    avail_list = _available_cpus()
    avail: Set[int] = set(avail_list)
    if exclude:
        avail -= set(exclude)
    if not avail:
        return []
    groups = _thread_sibling_groups(avail)
    groups_nonzero = [g for g in groups if g and g[0] != 0]
    groups_zero = [g for g in groups if g and g[0] == 0]
    ordered_groups = groups_nonzero + groups_zero

    order: List[int] = []
    for g in ordered_groups:
        if not g:
            continue
        c = g[0]
        if c == 0 and len(groups_zero) == 1:
            continue
        if len(order) >= n:
            break
        order.append(c)
    if len(order) < n:
        for g in ordered_groups:
            for c in g[1:]:
                if c == 0:
                    continue
                if len(order) >= n:
                    break
                order.append(c)
            if len(order) >= n:
                break
    if len(order) < n and 0 in avail:
        order.append(0)
    return order[:n]


def _has_flag_or_kv(tokens: Iterable[str], name: str) -> bool:
    name_eq = name + "="
    for tok in tokens or []:
        if tok == name or tok.startswith(name_eq):
            return True
    return False


class Runner:
    def __init__(self,
                 app_dir: Path,
                 receiver_host: str = "local",
                 client_host: str = "local",
                 receiver_app_dir: Optional[Path] = None,
                 client_app_dir: Optional[Path] = None,
                 impl_extra_args: Optional[Dict[str, List[str]]] = None,
                 auto_pin_workers: bool = True,
                 client_extra_args: Optional[List[str]] = None):
        self.app_dir = app_dir
        self.receiver_host = receiver_host
        self.client_host = client_host
        self.receiver_app_dir = receiver_app_dir or app_dir
        self.client_app_dir = client_app_dir or app_dir
        self.impl_extra_args = impl_extra_args or {}
        self.auto_pin_workers = auto_pin_workers
        self.client_extra_args = client_extra_args or []

    def _preset_sender_cpus(self, fixed: FixedParams) -> List[int]:
        """Compute sender CPU list, avoiding overlap with receiver when both local."""
        senders = int(getattr(fixed, 'senders', 0))
        if senders <= 0:
            return []
        if self.client_host == "local" and self.receiver_host == "local":
            workers = int(getattr(fixed, 'workers', 0))
            recv = choose_worker_cpus(workers) if (self.auto_pin_workers and workers > 0) else []
            return choose_cpus(senders, exclude=set(recv))
        return choose_cpus(senders)

    def ensure_binaries(self, impls: Sequence[str]):
        if self.receiver_host == "local":
            missing = []
            for i in impls:
                path = self.receiver_app_dir / IMPL_BIN_NAME[i]
                if not path.exists():
                    missing.append(str(path))
            if missing:
                raise FileNotFoundError(f"Missing local receiver binaries: {missing}. Set --app-root correctly.")
        if self.client_host == "local":
            cpath = self.client_app_dir / CLIENT_BIN_NAME
            if not cpath.exists():
                raise FileNotFoundError(f"Missing local client binary: {cpath}. Set --app-root correctly.")

    def _start_receiver(self, impl: str, fixed: FixedParams, results_dir: Path,
                        extra_tags: Dict[str, str], extra_impl_args: List[str],
                        preset_worker_cpus: Optional[List[int]] = None) -> subprocess.Popen:
        bin_name = IMPL_BIN_NAME[impl]
        args = [
            "--address", fixed.address,
            "--buffer-size", str(fixed.buffer_size),
            "--workers", str(fixed.workers),
            "--results-dir", str(results_dir),
            "--metric-hud-interval-secs", str(fixed.metric_hud_interval_secs),
            "--collect-latency-every-n-samples", str(fixed.collect_latency_every_n_samples),
            "--shutdown-on-disconnect",
        ]
        if fixed.busy_spin:
            args += ["--busy-spin", "true"]
        if fixed.recv_so_rcvbuf > 0:
            args += ["--so-rcvbuf", str(fixed.recv_so_rcvbuf)]
        if fixed.send_so_sndbuf > 0:
            args += ["--so-sndbuf", str(fixed.send_so_sndbuf)]
        if fixed.echo != "none":
            args += ["--echo", fixed.echo]
        # Auto CPU pinning: compute a comma-separated CPU list for workers, unless user provided one
        user_specified_worker_cpus = _has_flag_or_kv(extra_impl_args, "--worker-cpus")
        if self.auto_pin_workers and fixed.workers > 0 and not user_specified_worker_cpus:
            cpus = list(preset_worker_cpus or choose_worker_cpus(int(fixed.workers)))
            if cpus:
                args += ["--worker-cpus", ",".join(str(c) for c in cpus)]
        for k, v in extra_tags.items():
            args += ["--tag", f"{k}={v}"]
        args += list(extra_impl_args or [])

        log = (results_dir / f"receiver_{impl}.log").open("w")
        if self.receiver_host == "local":
            full_cmd = [str(self.receiver_app_dir / bin_name)] + args
            (results_dir / "receiver_cmd.log").write_text(" ".join(shlex.quote(c) for c in full_cmd))
            return subprocess.Popen(full_cmd, stdout=log, stderr=subprocess.STDOUT, cwd=str(self.receiver_app_dir))
        else:
            remote_cmd = f"cd {shlex.quote(str(self.receiver_app_dir))} && ./" + shlex.quote(bin_name)
            remote_cmd += " " + " ".join(shlex.quote(a) for a in args)
            full_cmd = ["ssh", self.receiver_host, remote_cmd]
            (results_dir / "receiver_cmd.log").write_text(" ".join(shlex.quote(c) for c in full_cmd))
            return subprocess.Popen(full_cmd, stdout=log, stderr=subprocess.STDOUT)

    def _run_client(self, fixed: FixedParams, duration_sec: int, results_dir: Path) -> int:
        args = [
            "--address", fixed.address,
            "--senders", str(getattr(fixed, 'senders', 0)),
            "--conns-per-sender", str(getattr(fixed, 'conns_per_sender', 1)),
            "--msg-size", str(getattr(fixed, 'msg_size', 0)),
            "--msgs-per-sec", str(getattr(fixed, 'msgs_per_sec', 0)),
            "--stop-after-n-secs", str(duration_sec),
            "--max-batch-size", str(getattr(fixed, 'max_batch_size', 0)),
            "--metric-hud-interval-secs", str(getattr(fixed, 'metric_hud_interval_secs', 0)),
        ]
        if getattr(fixed, 'drain', False):
            args += ["--drain"]
        # Auto-inject sender CPUs unless user provided their own in client args
        if not _has_flag_or_kv(self.client_extra_args, "--sender-cpus"):
            scpus = self._preset_sender_cpus(fixed)
            if scpus:
                args += ["--sender-cpus", ",".join(str(c) for c in scpus)]
        log = (results_dir / "client.log").open("w")
        if self.client_host == "local":
            full_cmd = [str(self.client_app_dir / CLIENT_BIN_NAME)] + args + list(self.client_extra_args)
            (results_dir / "client_cmd.log").write_text(" ".join(shlex.quote(c) for c in full_cmd))
            return subprocess.call(full_cmd, stdout=log, stderr=subprocess.STDOUT, cwd=str(self.client_app_dir))
        else:
            remote_cmd = f"cd {shlex.quote(str(self.client_app_dir))} && ./" + shlex.quote(CLIENT_BIN_NAME)
            remote_cmd += " " + " ".join(shlex.quote(a) for a in (args + list(self.client_extra_args)))
            full_cmd = ["ssh", self.client_host, remote_cmd]
            (results_dir / "client_cmd.log").write_text(" ".join(shlex.quote(c) for c in full_cmd))
            return subprocess.call(full_cmd, stdout=log, stderr=subprocess.STDOUT)

    def _stop_receiver(self, proc: subprocess.Popen, timeout: float = 15.0):
        """
        Waits for the receiver process to exit gracefully.
        The receiver is expected to shut down on its own when the client disconnects.
        If it doesn't exit within the timeout, it will be forcibly killed.
        """
        if proc.poll() is not None:
            return
        try:
            proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            proc.kill()
            raise RuntimeError(
                f"Receiver process {proc.pid} did not exit gracefully within {timeout}s and was forcibly killed.")

    def run_scenario(self, sc: Scenario, out_root: Path, cli_args: Optional[argparse.Namespace] = None) -> Path:
        self.ensure_binaries(sc.implementations)
        run_id = ts_utc_compact()
        sc_dir = out_root / f"{sc.name}_{run_id}"
        sc_dir.mkdir(parents=True, exist_ok=True)
        if cli_args:
            (sc_dir / "cli_args.json").write_text(json.dumps(vars(cli_args), indent=2, default=str))
        (sc_dir / "scenario.json").write_text(json.dumps(dc.asdict(sc), indent=2))

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
                        result = eval_link_expr(expr, fixed, sc.var_key, val)
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

                rproc = self._start_receiver(impl, fixed, run_dir, tags, extra_impl)
                time.sleep(1)
                rc = self._run_client(fixed, sc.fixed.duration_sec, run_dir)
                if rc != 0:
                    print(f"Client exited with {rc} for {impl} {sc.var_key}={val}", file=sys.stderr)
                self._stop_receiver(rproc)

        print(f"Scenario '{sc.name}' finished: {sc_dir}")
        return sc_dir


def _run_plot(results_root: Path, scenario_name: str, relative_to: str,
              impls: Optional[List[str]] = None, run_dir: Optional[Path] = None) -> int:
    plot_script = REPO_ROOT / "benchmark" / "plot_results.py"
    cmd = [
        sys.executable, str(plot_script),
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
        if impl not in IMPL_BIN_NAME:
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
    )

    args.out.mkdir(parents=True, exist_ok=True)

    # FixedParams overrides helpers
    def _parse_bool(s: str) -> bool:
        sl = s.strip().lower()
        if sl in ("1", "true", "yes", "y", "on"): return True
        if sl in ("0", "false", "no", "n", "off"): return False
        raise ValueError(f"Invalid boolean value: {s}")

    fixed_types = get_type_hints(FixedParams)

    def _coerce_value(key: str, value: str):
        t = fixed_types.get(key)
        if t is None:
            return value
        if t is bool:
            return _parse_bool(value)
        if t is int:
            return int(value)
        if t is str:
            return value
        try:
            return int(value)
        except Exception:
            try:
                return _parse_bool(value)
            except Exception:
                return value

    # Global overrides key=value
    global_overrides: Dict[str, str] = {}
    for item in getattr(args, 'fixed_params', []) or []:
        if "=" not in item:
            print(f"Ignoring --fixed-params without '=': {item}", file=sys.stderr)
            continue
        k, v = item.split("=", 1)
        global_overrides[k.strip()] = v.strip()

    # Per-scenario overrides scenario:key=value
    scenario_overrides: Dict[str, Dict[str, str]] = {}
    for item in getattr(args, 'scenario_fixed', []) or []:
        if ":" not in item or "=" not in item:
            print(f"Ignoring --scenario-fixed (need scenario:key=value): {item}", file=sys.stderr)
            continue
        scen_part, kv = item.split(":", 1)
        if "=" not in kv:
            print(f"Ignoring --scenario-fixed (missing '='): {item}", file=sys.stderr)
            continue
        k, v = kv.split("=", 1)
        scenario_overrides.setdefault(scen_part.strip(), {})[k.strip()] = v.strip()

    # Apply to scenarios
    for sc in scs:
        # Global
        for k, v in global_overrides.items():
            if not hasattr(sc.fixed, k):
                print(f"Warning: FixedParams has no field '{k}', ignoring --fixed-params {k}", file=sys.stderr)
                continue
            try:
                setattr(sc.fixed, k, _coerce_value(k, v))
            except Exception as e:
                print(f"Warning: failed to apply --fixed-params {k}={v}: {e}", file=sys.stderr)
        # Scenario-specific
        per = scenario_overrides.get(sc.name, {})
        for k, v in per.items():
            if not hasattr(sc.fixed, k):
                print(f"Warning: FixedParams has no field '{k}', ignoring --scenario-fixed for {sc.name}", file=sys.stderr)
                continue
            try:
                setattr(sc.fixed, k, _coerce_value(k, v))
            except Exception as e:
                print(f"Warning: failed to apply --scenario-fixed {sc.name}:{k}={v}: {e}", file=sys.stderr)

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

    for item in getattr(args, 'scenario_var_values', []) or []:
        if ":" not in item:
            print(f"Ignoring --scenario-var-values (need scenario:spec): {item}", file=sys.stderr)
            continue
        scen, spec = item.split(":", 1)
        try:
            vals = _parse_int_list_spec(spec)
        except Exception as e:
            print(f"Warning: failed to parse --scenario-var-values for {scen}: {e}", file=sys.stderr)
            continue
        matched = False
        for sc in scs:
            if sc.name == scen:
                sc.var_values = list(vals)
                matched = True
        if not matched:
            print(f"Warning: scenario '{scen}' not found for --scenario-var-values", file=sys.stderr)

    if getattr(args, 'dry_run', False):
        print("Planned runs:")
        for s in scs:
            print(json.dumps(dc.asdict(s), indent=2))
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
