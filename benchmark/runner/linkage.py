from __future__ import annotations
import dataclasses as dc
from typing import Dict

from .types import FixedParams


def eval_link_expr(expr: str, fixed: FixedParams, var_name: str, var_val: int) -> float:
    """Safely evaluate a linkage expression.
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
