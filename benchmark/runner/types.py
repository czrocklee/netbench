from __future__ import annotations
import dataclasses as dc
from typing import Any, Callable, Dict, List, Optional, Sequence


@dc.dataclass
class FixedParams:
    address: str = "0.0.0.0:19004"
    # sender
    duration_sec: int = 10
    msg_size: int = 32
    senders: int = 1
    msgs_per_sec: int = 0  # 0 = as fast as possible
    conns_per_sender: int = 1
    conns: int = 0 
    drain: bool = False
    nodelay: bool = False
    # receivers:
    workers: int = 1
    busy_spin: bool = False
    echo: str = "none"  # none|per_op|per_msg
    buffer_size: int = 32
    recv_so_rcvbuf: int = 0
    send_so_sndbuf: int = 0
    max_batch_size: int = 1024
    collect_latency_every_n_samples: int = 0
    metric_hud_interval_secs: int = 0
    bsd_read_limit: int = 0
    uring_buffer_count: int = 0
    uring_per_conn_buffer_pool: bool = False

# A linkage function can compute a dependent field given the current fixed params,
# the varying field name, and its value.
"""A linkage function computes a dependent field.

Signature (only):
- fn(fixed: FixedParams) -> Any
"""
LinkFunc = Callable[["FixedParams"], Any]


@dc.dataclass
class Scenario:
    name: str
    # Optional: friendly title used in plots; if absent, fall back to 'name'
    title: Optional[str] = None
    fixed: FixedParams = dc.field(default_factory=FixedParams)
    var_key: str = "workers"
    var_values: Sequence[int] = ()
    implementations: Sequence[str] = ()
    # Optional: per-impl extra args for receiver, e.g., {"uring": ["--zerocopy","true"]}
    impl_extra: Dict[str, List[str]] = dc.field(default_factory=dict)
    # Optional: linkages to derive fields from the varying variable.
    # Value must be a Python function with signature:
    #   fn(fixed: FixedParams) -> Any
    linkages: Dict[str, LinkFunc] = dc.field(default_factory=dict)
