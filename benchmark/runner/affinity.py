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
    """Choose up to n CPUs per policy:
    - Prefer one hardware thread per physical core first (no SMT sharing).
    - Avoid CPU 0 for primaries when possible.
    - If we'd otherwise have to start using SMT siblings, prefer taking CPU 0 next.
    - Only then, start allocating SMT siblings (second thread per core).

    The optional 'exclude' set removes CPUs from consideration upfront.
    """
    avail_list = _available_cpus()
    avail: Set[int] = set(avail_list)
    if exclude:
        avail -= set(exclude)
    if not avail:
        return []
    groups = _thread_sibling_groups(avail)

    def _full_siblings(cpu: int) -> List[int]:
        base = "/sys/devices/system/cpu"
        path = os.path.join(base, f"cpu{cpu}", "topology", "thread_siblings_list")
        try:
            with open(path, "r") as f:
                spec = f.read().strip()
            return sorted(_parse_cpulist_spec(spec))
        except Exception:
            return [cpu]

    # Build a structure mapping each available group to its core-primary (from full sysfs, not filtered by avail)
    core_groups = []  # list of tuples: (core_primary:int, available_members:List[int])
    seen_cores: Set[int] = set()
    for g in groups:
        if not g:
            continue
        full = _full_siblings(g[0])
        primary = min(full) if full else g[0]
        core_id_key = tuple(full)
        if primary in seen_cores:
            continue
        seen_cores.add(primary)
        # available members are intersection with 'avail', but preserve order: primary first if available
        avail_members = [c for c in full if c in avail]
        if not avail_members:
            continue
        core_groups.append((primary, avail_members))

    # Sort core groups by their primary id
    core_groups.sort(key=lambda t: t[0])

    order: List[int] = []
    # 1) Take primaries of non-zero cores first, but only if that primary CPU id is available
    for primary, avail_members in core_groups:
        if primary == 0:
            continue
        if primary in avail and len(order) < n:
            order.append(primary)

    # 2) If still need more, and CPU 0 primary is available, take it BEFORE any SMT siblings
    if len(order) < n:
        for primary, avail_members in core_groups:
            if primary == 0 and 0 in avail:
                order.append(0)
                break

    # 3) If still need more, start adding SMT siblings from all groups (including zero's siblings)
    if len(order) < n:
        for primary, avail_members in core_groups:
            # iterate members skipping primary (even if unavailable, list is in full order primary first)
            start_idx = 1 if (avail_members and avail_members[0] == primary) else 0
            for c in avail_members[start_idx:]:
                if len(order) >= n:
                    break
                if c not in order and c in avail:
                    order.append(c)
            if len(order) >= n:
                break

    return order[:n]
