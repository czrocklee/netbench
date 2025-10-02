#!/usr/bin/env python3
"""
Scenario runner for netbench.
- Define scenarios with fixed params and one variable dimension (extendable)
- Compare implementations (uring/asio/bsd/...)
- Drive receiver and client, capture run.json and logs

Key features:
- --app-root points to directory containing binaries (e.g., build/release/app)
- Impl-specific extra args via --impl-arg impl=token (repeatable) or Scenario.impl_extra
- Receiver/client can run on different machines via ssh (per-role app roots)
"""
from __future__ import annotations
import argparse
import dataclasses as dc
import json
import shlex
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone
import re
from pathlib import Path
from typing import Dict, List, Optional, Sequence, get_type_hints

REPO_ROOT = Path(__file__).resolve().parents[1]


def ts_utc_compact() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H-%M-%SZ")


def _eval_link_expr(expr: str, fixed: FixedParams, var_name: str, var_val: int) -> float:
    """Evaluate a linkage expression safely.
    Supported:
    - Numeric literals (int/float), parentheses
    - Variables: the varying key (e.g., 'workers') and current numeric fields in FixedParams
    - Operators: +, -, *, /, //, %, **
    - Functions: min(), max(), abs(), floor(), ceil(), round()
    Returns a float; caller coerces to target field type.
    """
    import ast, math

    allowed_funcs = {
        'min': min,
        'max': max,
        'abs': abs,
        'floor': math.floor,
        'ceil': math.ceil,
        'round': round,
    }

    # Build name context: varying variable and numeric FixedParams fields
    names: Dict[str, float] = {var_name: float(var_val)}
    for f in dc.fields(FixedParams):
        name = f.name
        try:
            val = getattr(fixed, name)
        except Exception:
            continue
        if isinstance(val, bool):
            names[name] = 1.0 if val else 0.0
        elif isinstance(val, int):
            names[name] = float(val)
        # ignore str and others

    node = ast.parse(expr, mode='eval')

    def _eval(n):
        if isinstance(n, ast.Expression):
            return _eval(n.body)
        if isinstance(n, ast.Constant):
            if isinstance(n.value, (int, float)):
                return float(n.value)
            raise ValueError("Only numeric constants allowed in linkage expressions")
        if isinstance(n, ast.Name):
            if n.id in names:
                return names[n.id]
            raise ValueError(f"Unknown name '{n.id}' in linkage expression")
        if isinstance(n, ast.BinOp):
            left = _eval(n.left); right = _eval(n.right)
            if isinstance(n.op, ast.Add):
                return left + right
            if isinstance(n.op, ast.Sub):
                return left - right
            if isinstance(n.op, ast.Mult):
                return left * right
            if isinstance(n.op, ast.Div):
                return left / right
            if isinstance(n.op, ast.FloorDiv):
                return left // right
            if isinstance(n.op, ast.Mod):
                return left % right
            if isinstance(n.op, ast.Pow):
                return left ** right
            raise ValueError("Operator not allowed in linkage expression")
        if isinstance(n, ast.UnaryOp):
            operand = _eval(n.operand)
            if isinstance(n.op, ast.UAdd):
                return +operand
            if isinstance(n.op, ast.USub):
                return -operand
            raise ValueError("Unary operator not allowed")
        if isinstance(n, ast.Call):
            if not isinstance(n.func, ast.Name) or n.func.id not in allowed_funcs:
                raise ValueError("Function not allowed in linkage expression")
            func = allowed_funcs[n.func.id]
            if n.keywords:
                raise ValueError("Keywords not allowed in linkage expression")
            args = [_eval(a) for a in n.args]
            return float(func(*args))
        raise ValueError("Unsupported syntax in linkage expression")

    return float(_eval(node))


# Implementation -> receiver binary name (basename)
IMPL_BIN_NAME: Dict[str, str] = {
    "uring": "uring_receiver",
    "asio": "asio_receiver",
    "asio_uring": "asio_uring_receiver",
    "bsd": "bsd_receiver",
}
CLIENT_BIN_NAME = "client"


@dc.dataclass
class FixedParams:
    address: str = "0.0.0.0:19004"
    # sender
    duration_sec: int = 10
    msg_size: int = 32
    senders: int = 1
    conns_per_sender: int = 1
    # receivers:
    workers: int = 1 
    busy_spin: bool = False
    echo: str = "none"  # none|per_op|per_msg
    buffer_size: int = 32
    recv_so_rcvbuf: int = 0
    send_so_sndbuf: int = 0

@dc.dataclass
class Scenario:
    name: str
    fixed: FixedParams
    var_key: str
    var_values: Sequence[int]
    implementations: Sequence[str]
    # Optional: per-impl extra args for receiver, e.g., {"uring": ["--zerocopy","true"]}
    impl_extra: Dict[str, List[str]] = dc.field(default_factory=dict)
    # Optional: linkages to derive fields from the varying variable, e.g., {"senders": "2*workers"}
    linkages: Dict[str, str] = dc.field(default_factory=dict)


class Runner:
    def __init__(self,
                 app_dir: Path,
                 receiver_host: str = "local",
                 client_host: str = "local",
                 receiver_app_dir: Optional[Path] = None,
                 client_app_dir: Optional[Path] = None,
                 impl_extra_args: Optional[Dict[str, List[str]]] = None):
        self.app_dir = app_dir
        self.receiver_host = receiver_host
        self.client_host = client_host
        self.receiver_app_dir = receiver_app_dir or app_dir
        self.client_app_dir = client_app_dir or app_dir
        self.impl_extra_args = impl_extra_args or {}

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
                        extra_tags: Dict[str, str], extra_impl_args: List[str]) -> subprocess.Popen:
        bin_name = IMPL_BIN_NAME[impl]
        args = [
            "--address", fixed.address,
            "--buffer-size", str(fixed.buffer_size),
            "--workers", str(fixed.workers),
            "--results-dir", str(results_dir),
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
        for k, v in extra_tags.items():
            args += ["--tag", f"{k}={v}"]
        args += list(extra_impl_args or [])

        log = (results_dir / f"receiver_{impl}.log").open("w")
        if self.receiver_host == "local":
            cmd = [str(self.receiver_app_dir / bin_name)] + args
            return subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, cwd=str(self.receiver_app_dir))
        else:
            remote_cmd = f"cd {shlex.quote(str(self.receiver_app_dir))} && ./" + shlex.quote(bin_name)
            remote_cmd += " " + " ".join(shlex.quote(a) for a in args)
            return subprocess.Popen(["ssh", self.receiver_host, remote_cmd], stdout=log, stderr=subprocess.STDOUT)

    def _run_client(self, fixed: FixedParams, duration_sec: int, results_dir: Path) -> int:
        args = [
            "--address", fixed.address,
            "--senders", str(fixed.senders),
            "--conns-per-sender", str(fixed.conns_per_sender),
            "--msg-size", str(fixed.msg_size),
            "--stop-after-n-secs", str(duration_sec),
        ]
        log = (results_dir / "client.log").open("w")
        if self.client_host == "local":
            cmd = [str(self.client_app_dir / CLIENT_BIN_NAME)] + args
            return subprocess.call(cmd, stdout=log, stderr=subprocess.STDOUT, cwd=str(self.client_app_dir))
        else:
            remote_cmd = f"cd {shlex.quote(str(self.client_app_dir))} && ./" + shlex.quote(CLIENT_BIN_NAME)
            remote_cmd += " " + " ".join(shlex.quote(a) for a in args)
            return subprocess.call(["ssh", self.client_host, remote_cmd], stdout=log, stderr=subprocess.STDOUT)

    def _stop_receiver(self, proc: subprocess.Popen, timeout: float = 5.0):
        if proc.poll() is not None:
            return
        try:
            proc.send_signal(signal.SIGINT)
            for _ in range(int(timeout * 10)):
                if proc.poll() is not None:
                    return
                time.sleep(0.1)
            proc.terminate()
            for _ in range(int(timeout * 10)):
                if proc.poll() is not None:
                    return
                time.sleep(0.1)
            proc.kill()
        except Exception:
            proc.kill()

    def run_scenario(self, sc: Scenario, out_root: Path) -> Path:
        self.ensure_binaries(sc.implementations)
        run_id = ts_utc_compact()
        sc_dir = out_root / f"{sc.name}_{run_id}"
        sc_dir.mkdir(parents=True, exist_ok=True)
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
                        result = _eval_link_expr(expr, fixed, sc.var_key, val)
                        # Coerce to the type of the target field
                        current = getattr(fixed, target)
                        if isinstance(current, bool):
                            setattr(fixed, target, bool(int(round(result))))
                        elif isinstance(current, int):
                            setattr(fixed, target, int(round(result)))
                        elif isinstance(current, str):
                            # allow expressions to produce strings via names? keep numeric only
                            setattr(fixed, target, str(int(round(result))))
                        else:
                            setattr(fixed, target, result)

                tags = {"scenario": sc.name, "impl": impl, sc.var_key: str(val)}
                extra_impl = list(sc.impl_extra.get(impl, [])) + list(self.impl_extra_args.get(impl, []))

                rproc = self._start_receiver(impl, fixed, run_dir, tags, extra_impl)
                time.sleep(0.5)
                rc = self._run_client(fixed, sc.fixed.duration_sec, run_dir)
                if rc != 0:
                    print(f"Client exited with {rc} for {impl} {sc.var_key}={val}", file=sys.stderr)
                self._stop_receiver(rproc)

        print(f"Scenario '{sc.name}' finished: {sc_dir}")
        return sc_dir


def _run_plot(results_root: Path, scenario_name: str, output: str, relative_to: str,
              out_dir: Optional[Path]) -> int:
    """Invoke the plotter to generate charts for a specific scenario."""
    plot_script = REPO_ROOT / "benchmark" / "plot_results.py"
    cmd = [
        sys.executable, str(plot_script),
        "--results-dir", str(results_root),
        "--scenario", scenario_name,
        "--output", output,
        "--relative-to", relative_to,
    ]
    if out_dir is not None:
        cmd += ["--out-dir", str(out_dir)]
    print(f"Auto-plot: {' '.join(shlex.quote(c) for c in cmd)}")
    return subprocess.call(cmd)


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
    )
    return [
        Scenario(
            name="scale_by_threads",
            fixed=fixed,
            var_key="workers",
            var_values=[1, 2, 4, 8],
            linkages={"senders": "workers"},
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
    p.add_argument("--scenario", action="append", choices=["vary_workers", "vary_msg_size"],
                   help="Scenario(s) to run; default all")
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
    if args.scenario:
        scs = [s for s in scs if s.name in args.scenario]
    if args.impl:
        for s in scs:
            s.implementations = [i for i in s.implementations if i in args.impl]

    impl_extra_args: Dict[str, List[str]] = {}
    for item in args.impl_arg:
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

    # Apply FixedParams overrides
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
        # Fallback: try int then bool then str
        try:
            return int(value)
        except Exception:
            try:
                return _parse_bool(value)
            except Exception:
                return value

    # Global overrides key=value
    global_overrides: Dict[str, str] = {}
    for item in args.fixed_params:
        if "=" not in item:
            print(f"Ignoring --fixed-params without '=': {item}", file=sys.stderr)
            continue
        k, v = item.split("=", 1)
        global_overrides[k.strip()] = v.strip()

    # Per-scenario overrides scenario:key=value
    scenario_overrides: Dict[str, Dict[str, str]] = {}
    for item in args.scenario_fixed:
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
        # Single integer
        if re.match(r"^\s*-?\d+\s*$", s):
            return [int(s)]
        raise ValueError(f"Invalid var-values spec: {spec}")

    # Apply scenario-specific var_values
    for item in args.scenario_var_values:
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

    if args.dry_run:
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
        sc_dir = runner.run_scenario(s, args.out)
        if args.auto_plot:
            # Write the plot into this scenario's results folder
            out_dir = args.plot_out_dir if args.plot_out_dir is not None else sc_dir
            _ = _run_plot(args.out, s.name, args.plot_output, args.plot_relative_to, out_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
