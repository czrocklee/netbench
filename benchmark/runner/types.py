from __future__ import annotations
import dataclasses as dc
from typing import Dict, List, Optional, Sequence


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
    # Optional: friendly title used in plots; if absent, fall back to 'name'
    title: Optional[str] = None
    fixed: FixedParams = dc.field(default_factory=FixedParams)
    var_key: str = "workers"
    var_values: Sequence[int] = ()
    implementations: Sequence[str] = ()
    # Optional: per-impl extra args for receiver, e.g., {"uring": ["--zerocopy","true"]}
    impl_extra: Dict[str, List[str]] = dc.field(default_factory=dict)
    # Optional: linkages to derive fields from the varying variable, e.g., {"senders": "2*workers"}
    linkages: Dict[str, str] = dc.field(default_factory=dict)
