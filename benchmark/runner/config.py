from __future__ import annotations
import dataclasses as dc
from typing import Any, Dict, List, Optional, get_type_hints

from .types import FixedParams, Scenario


def _parse_bool(s: str) -> bool:
    sl = s.strip().lower()
    if sl in ("1", "true", "yes", "y", "on"):
        return True
    if sl in ("0", "false", "no", "n", "off"):
        return False
    raise ValueError(f"Invalid boolean value: {s}")


def _coerce_value(key: str, value: str, fixed_types: Dict[str, Any]):
    t = fixed_types.get(key)
    if t is None:
        return value
    if t is bool:
        return _parse_bool(value)
    if t is int:
        return int(value)
    if t is str:
        return value
    # Best-effort fallback
    try:
        return int(value)
    except Exception:
        try:
            return _parse_bool(value)
        except Exception:
            return value


def apply_fixedparams_overrides(scenarios: List[Scenario], global_fixed: Optional[List[str]] = None,
                                scenario_fixed: Optional[List[str]] = None) -> None:
    """Apply FixedParams overrides to scenarios.

    global_fixed: list of 'key=value'
    scenario_fixed: list of 'scenario:key=value'
    """
    global_fixed = global_fixed or []
    scenario_fixed = scenario_fixed or []

    fixed_types = get_type_hints(FixedParams)

    # Parse globals
    global_overrides: Dict[str, str] = {}
    for item in global_fixed:
        if "=" not in item:
            # ignore silently; caller may have logged
            continue
        k, v = item.split("=", 1)
        global_overrides[k.strip()] = v.strip()

    # Parse per-scenario
    scenario_overrides: Dict[str, Dict[str, str]] = {}
    for item in scenario_fixed:
        if ":" not in item or "=" not in item:
            continue
        scen_part, kv = item.split(":", 1)
        if "=" not in kv:
            continue
        k, v = kv.split("=", 1)
        scenario_overrides.setdefault(scen_part.strip(), {})[k.strip()] = v.strip()

    # Apply
    for sc in scenarios:
        # Global
        for k, v in global_overrides.items():
            if not hasattr(sc.fixed, k):
                continue
            try:
                setattr(sc.fixed, k, _coerce_value(k, v, fixed_types))
            except Exception:
                # ignore bad override
                pass
        # Scenario-specific
        per = scenario_overrides.get(sc.name, {})
        for k, v in per.items():
            if not hasattr(sc.fixed, k):
                continue
            try:
                setattr(sc.fixed, k, _coerce_value(k, v, fixed_types))
            except Exception:
                pass
