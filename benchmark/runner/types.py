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
    conns: int = 1
    conns: int = 0 
    drain: bool = False
    nodelay: bool = False
    max_send_batch_size: int = 0 # use default
    # Client senders: comma-separated list mapping to sender indices
    sender_cpus: Optional[str] = None
    
    # pingpong specific controls
    warmup_count: int = 10000
    max_samples: int = 0
    # Pingpong CPUs: per-process CPU id; used for both uring/asio/bsd
    pp_acceptor_cpu: Optional[int] = None
    pp_initiator_cpu: Optional[int] = None
    # Pingpong (uring): optional SQ entries override
    pp_uring_sq_entries: int = 0
    # Optional sqpoll CPUs for io_uring (applies if impl == 'uring')
    pp_acceptor_sqpoll_cpu: Optional[int] = None
    pp_initiator_sqpoll_cpu: Optional[int] = None

    # receivers:
    workers: int = 1
    busy_spin: bool = False
    echo: str = "none"  # none|per_op|per_msg
    buffer_size: int = 32
    recv_so_rcvbuf: int = 0
    send_so_sndbuf: int = 0
    collect_latency_every_n_samples: int = 0
    metric_hud_interval_secs: int = 0
    # Receiver workers: comma-separated list mapping to worker indices
    worker_cpus: Optional[str] = None
    bsd_read_limit: int = 0
    uring_buffer_count: int = 0
    uring_per_conn_buffer_pool: bool = False
    uring_zerocopy: bool = False
    # Receiver (uring): queue sizes; 0/-1 means use defaults
    uring_sq_entries: int = 0
    uring_cq_entries: int = -1

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
    # mode: 'receiver' (default) runs receiver+client; 'pingpong' runs pingpong acceptor+initiator
    mode: str = "receiver"
    # Optional: per-impl extra args for receiver, e.g., {"uring": ["--zerocopy","true"]}
    impl_extra: Dict[str, List[str]] = dc.field(default_factory=dict)
    # Optional: linkages to derive fields from the varying variable.
    # Value must be a Python function with signature:
    #   fn(fixed: FixedParams) -> Any
    linkages: Dict[str, LinkFunc] = dc.field(default_factory=dict)
