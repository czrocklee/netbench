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
from pathlib import Path
from typing import Dict, List, Optional, Sequence

REPO_ROOT = Path(__file__).resolve().parents[1]


def ts_utc_compact() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H-%M-%SZ")


def _eval_link_expr(expr: str, var_name: str, var_val: int) -> int:
    """Evaluate a simple linkage expression referencing the varying var.
    Supported forms:
    - var_name
    - K*var_name  (integer K)
    - var_name*K
    - K           (integer constant)
    Returns int result or raises ValueError on invalid expr.
    """
    e = expr.replace(" ", "")
    if e.isdigit() or (e.startswith("-") and e[1:].isdigit()):
        return int(e)
    if e == var_name:
        return var_val
    if "*" in e:
        a, b = e.split("*", 1)
        if a == var_name and (b.isdigit() or (b.startswith("-") and b[1:].isdigit())):
            return var_val * int(b)
        if b == var_name and (a.isdigit() or (a.startswith("-") and a[1:].isdigit())):
            return int(a) * var_val
    raise ValueError(f"Unsupported linkage expression: '{expr}' (var='{var_name}')")


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

    def run_scenario(self, sc: Scenario, out_root: Path):
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
                        computed = _eval_link_expr(expr, sc.var_key, val)
                        if not hasattr(fixed, target):
                            raise AttributeError(f"FixedParams has no field '{target}' (from linkage)")
                        setattr(fixed, target, computed)

                tags = {"scenario": sc.name, "impl": impl, sc.var_key: str(val)}
                extra_impl = list(sc.impl_extra.get(impl, [])) + list(self.impl_extra_args.get(impl, []))

                rproc = self._start_receiver(impl, fixed, run_dir, tags, extra_impl)
                time.sleep(0.5)
                rc = self._run_client(fixed, sc.fixed.duration_sec, run_dir)
                if rc != 0:
                    print(f"Client exited with {rc} for {impl} {sc.var_key}={val}", file=sys.stderr)
                self._stop_receiver(rproc)

        print(f"Scenario '{sc.name}' finished: {sc_dir}")


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
            name="vary_workers",
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
        runner.run_scenario(s, args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
