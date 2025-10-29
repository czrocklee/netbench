from __future__ import annotations
from typing import List, Optional, Set
import os


def _parse_cpulist_spec(spec: str) -> List[int]:
    """Parse Linux cpulist strings like "0-3,8,10-11" into a list of ints."""
    out: List[int] = []
    for part in spec.split(","):
        p = part.strip()
        if not p:
            continue
        if "-" in p:
            a, b = p.split("-", 1)
            start = int(a)
            end = int(b)
            if start <= end:
                out.extend(range(start, end + 1))
            else:
                out.extend(range(start, end - 1, -1))
        else:
            out.append(int(p))
    return out


def _available_cpus() -> List[int]:
    """Return the list of CPUs available to the current process (affinity-aware)."""
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
    """Build SMT sibling groups from sysfs; each group is a list of CPU ids (primary first)."""
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


def choose_cpus(n: int, exclude: Optional[Set[int]] = None) -> List[int]:
    """Choose up to n CPUs prioritizing one hardware thread per core first. with optional exclusion set."""
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
