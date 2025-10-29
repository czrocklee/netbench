from __future__ import annotations
import shlex
import subprocess
from pathlib import Path
from typing import Dict, List, Optional

from .constants import RECEIVER_BIN_NAME, CLIENT_BIN_NAME
from .types import FixedParams
 


def ensure_receiver_binaries(receiver_app_dir: Path, impls, bin_map: Dict[str, str] = RECEIVER_BIN_NAME) -> None:
    if receiver_app_dir:
        missing = []
        for i in impls:
            path = receiver_app_dir / bin_map[i]
            if not path.exists():
                missing.append(str(path))
        if missing:
            raise FileNotFoundError(f"Missing local receiver binaries: {missing}. Set --app-root correctly.")


def start_receiver(receiver_host: str, receiver_app_dir: Path, impl: str, fixed: FixedParams, results_dir: Path,
                   extra_tags: Dict[str, str], extra_impl_args: List[str]) -> subprocess.Popen:
    bin_name = RECEIVER_BIN_NAME[impl]
    args: List[str] = [
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

    if impl == "bsd":
        if fixed.bsd_read_limit > 0:
            args += ["--read-limit", str(fixed.bsd_read_limit)]
    elif impl == "uring":
        if fixed.uring_buffer_count > 0:
            args += ["--buffer-count", str(fixed.uring_buffer_count)]
        if fixed.uring_per_conn_buffer_pool:
            args += ["--per-connection-buffer-pool", "true"]

    # CPU affinity for receiver workers: use FixedParams only
    if fixed.worker_cpus:
        args += ["--worker-cpu-ids", str(fixed.worker_cpus)]
    for k, v in extra_tags.items():
        args += ["--tag", f"{k}={v}"]
    # Pass extra impl args through as-is
    args += list(extra_impl_args or [])

    log = (results_dir / f"receiver_{impl}.stdout").open("w")
    cmd = (results_dir / f"receiver_{impl}.cmd")
    if receiver_host == "local":
        full_cmd = [str(receiver_app_dir / bin_name)] + args
        cmd.write_text(" ".join(shlex.quote(c) for c in full_cmd))
        return subprocess.Popen(full_cmd, stdout=log, stderr=subprocess.STDOUT, cwd=str(receiver_app_dir))
    else:
        remote_cmd = f"cd {shlex.quote(str(receiver_app_dir))} && ./" + shlex.quote(bin_name)
        remote_cmd += " " + " ".join(shlex.quote(a) for a in args)
        full_cmd = ["ssh", receiver_host, remote_cmd]
        cmd.write_text(" ".join(shlex.quote(c) for c in full_cmd))
        return subprocess.Popen(full_cmd, stdout=log, stderr=subprocess.STDOUT)


def run_client(client_host: str, client_app_dir: Path, fixed: FixedParams, duration_sec: int, results_dir: Path,
               client_extra_args: List[str]) -> int:
    args: List[str] = [
        "--address", fixed.address,
        "--senders", str(fixed.senders),
        "--conns", str(fixed.conns),
        "--msg-size", str(fixed.msg_size),
        "--msgs-per-sec", str(fixed.msgs_per_sec),
        "--stop-after-n-secs", str(duration_sec),
        "--max-batch-size", str(fixed.max_send_batch_size),
        "--metric-hud-interval-secs", str(fixed.metric_hud_interval_secs),
    ]
    if fixed.drain:
        args += ["--drain"]
    if fixed.nodelay:
        args += ["--nodelay"]
    # Sender CPUs: use FixedParams only
    if fixed.sender_cpus:
        args += ["--sender-cpu-ids", str(fixed.sender_cpus)]
    log = (results_dir / f"{CLIENT_BIN_NAME}.stdout").open("w")
    cmd = (results_dir / f"{CLIENT_BIN_NAME}.cmd")
    if client_host == "local":
        full_cmd = [str(client_app_dir / CLIENT_BIN_NAME)] + args + list(client_extra_args or [])
        cmd.write_text(" ".join(shlex.quote(c) for c in full_cmd))
        return subprocess.call(full_cmd, stdout=log, stderr=subprocess.STDOUT, cwd=str(client_app_dir))
    else:
        remote_cmd = f"cd {shlex.quote(str(client_app_dir))} && ./" + shlex.quote(CLIENT_BIN_NAME)
        remote_cmd += " " + " ".join(shlex.quote(a) for a in (args + list(client_extra_args or [])))
        full_cmd = ["ssh", client_host, remote_cmd]
        cmd.write_text(" ".join(shlex.quote(c) for c in full_cmd))
        return subprocess.call(full_cmd, stdout=log, stderr=subprocess.STDOUT)


def stop_receiver(proc: subprocess.Popen, timeout: float = 15.0):
    if proc.poll() is not None:
        return
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        raise RuntimeError(
            f"Receiver process {proc.pid} did not exit gracefully within {timeout}s and was forcibly killed.")
