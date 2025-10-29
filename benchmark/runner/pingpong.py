from __future__ import annotations
import shlex
import subprocess
from pathlib import Path
from typing import Dict, List, Optional

from .constants import PINGPONG_BIN_NAME
from .types import FixedParams


def ensure_pingpong_binaries(receiver_app_dir: Path, impls) -> None:
    if receiver_app_dir:
        missing = []
        for i in impls:
            bin_name = PINGPONG_BIN_NAME.get(i)
            if not bin_name:
                continue
            path = receiver_app_dir / bin_name
            if not path.exists():
                missing.append(str(path))
        if missing:
            raise FileNotFoundError(f"Missing local pingpong binaries: {missing}. Set --app-root correctly.")


def start_acceptor(receiver_host: str, receiver_app_dir: Path, impl: str, fixed: FixedParams, results_dir: Path,
                   extra_impl_args: Optional[List[str]] = None) -> subprocess.Popen:
    bin_name = PINGPONG_BIN_NAME[impl]
    args: List[str] = [
        "--address", fixed.address,
        "--buffer-size", str(fixed.buffer_size),
    ]
    if fixed.pp_acceptor_cpu is not None:
        args += ["--cpu-id", str(int(fixed.pp_acceptor_cpu))]
    if impl == 'uring':
        # Use per-process sqpoll CPU only
        if fixed.pp_acceptor_sqpoll_cpu is not None:
            args += ["--sqpoll-cpu-id", str(int(fixed.pp_acceptor_sqpoll_cpu))]

    log = (results_dir / f"pingpong_acceptor_{impl}.stdout").open("w")
    cmd = (results_dir / f"pingpong_acceptor_{impl}.cmd")
    if receiver_host == "local":
        full_cmd = [str(receiver_app_dir / bin_name)] + args + list(extra_impl_args or [])
        cmd.write_text(" ".join(shlex.quote(c) for c in full_cmd))
        return subprocess.Popen(full_cmd, stdout=log, stderr=subprocess.STDOUT, cwd=str(receiver_app_dir))
    else:
        remote_cmd = f"cd {shlex.quote(str(receiver_app_dir))} && ./" + shlex.quote(bin_name)
        remote_cmd += " " + " ".join(shlex.quote(a) for a in (args + list(extra_impl_args or [])))
        full_cmd = ["ssh", receiver_host, remote_cmd]
        cmd.write_text(" ".join(shlex.quote(c) for c in full_cmd))
        return subprocess.Popen(full_cmd, stdout=log, stderr=subprocess.STDOUT)


def run_initiator(client_host: str, client_app_dir: Path, impl: str, fixed: FixedParams, results_dir: Path,
                  tags: Dict[str, str], extra_impl_args: Optional[List[str]] = None) -> int:
    bin_name = PINGPONG_BIN_NAME[impl]
    args: List[str] = [
        "--initiator",
        "--address", fixed.address,
        "--msg-size", str(getattr(fixed, 'msg_size', 0)),
        "--buffer-size", str(getattr(fixed, 'buffer_size', 0)),
        "--warmup-count", str(getattr(fixed, 'warmup_count', 0)),
        "--duration-secs", str(getattr(fixed, 'duration_sec', 0)),
        "--max-samples", str(getattr(fixed, 'max_samples', 0)),
        "--target-msg-rate", str(getattr(fixed, 'msgs_per_sec', 0)),
        "--metric-hud-interval-secs", str(getattr(fixed, 'metric_hud_interval_secs', 0)),
        "--results-dir", str(results_dir),
    ]
    if fixed.pp_initiator_cpu is not None:
        args += ["--cpu-id", str(int(fixed.pp_initiator_cpu))]
    if impl == 'uring':
        # Use per-process sqpoll CPU only
        if fixed.pp_initiator_sqpoll_cpu is not None:
            args += ["--sqpoll-cpu-id", str(int(fixed.pp_initiator_sqpoll_cpu))]
    for k, v in tags.items():
        args += ["--tag", f"{k}={v}"]

    log = (results_dir / f"pingpong_initiator_{impl}.stdout").open("w")
    cmd = (results_dir / f"pingpong_initiator_{impl}.cmd")
    if client_host == "local":
        full_cmd = [str(client_app_dir / bin_name)] + args + list(extra_impl_args or [])
        cmd.write_text(" ".join(shlex.quote(c) for c in full_cmd))
        return subprocess.call(full_cmd, stdout=log, stderr=subprocess.STDOUT, cwd=str(client_app_dir))
    else:
        remote_cmd = f"cd {shlex.quote(str(client_app_dir))} && ./" + shlex.quote(bin_name)
        remote_cmd += " " + " ".join(shlex.quote(a) for a in (args + list(extra_impl_args or [])))
        full_cmd = ["ssh", client_host, remote_cmd]
        cmd.write_text(" ".join(shlex.quote(c) for c in full_cmd))
        return subprocess.call(full_cmd, stdout=log, stderr=subprocess.STDOUT)
