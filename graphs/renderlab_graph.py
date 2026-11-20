from __future__ import annotations

from collections.abc import Iterable, Mapping
from enum import Enum
import json
import math
from pathlib import Path
import re
import runpy
import sys
import traceback
from typing import TypeVar, cast


_ParameterT = TypeVar("_ParameterT", bound=bool | int | float | str | Enum)


def _parameter_value(name, value, valueType):
    if valueType is float and type(value) is int:
        value = float(value)
    if type(value) is not valueType:
        raise TypeError(f"Graph parameter {name!r} expects {valueType.__name__}")
    if valueType is int and not -(1 << 63) <= value < (1 << 63):
        raise ValueError(f"Graph parameter {name!r} must fit in a signed 64-bit integer")
    if valueType is float and not math.isfinite(value):
        raise ValueError(f"Graph parameter {name!r} must be finite")
    return value


class RenderGraph:
    def __init__(
        self, parameterOverrides: Mapping[str, object], parameterChoices: Mapping[str, Iterable[str]]
    ) -> None:
        self.showObjectList = False
        self._parameterOverrides = parameterOverrides
        self._parameterChoices = parameterChoices
        self._parameters = {}
        self._editorControls = []
        self._nodes = []
        self._edges = []
        self._outputs = {}

    def input(self, name: str) -> str:
        return f"$inputs.{name}"

    def param(
        self,
        name: str,
        default: _ParameterT,
        *,
        label: str | None = None,
        choices: Iterable[str] | None = None,
        bounds: tuple[int | float, int | float] | None = None,
        ui: bool = True,
    ) -> _ParameterT:
        if not isinstance(name, str) or not name:
            raise ValueError("Graph parameter names must be nonempty strings")
        if name in self._parameters:
            raise ValueError(f"Graph parameter {name!r} is already declared")
        if type(ui) is not bool:
            raise TypeError(f"Graph parameter {name!r} expects a boolean ui flag")

        enumType = None
        defaultValue = default
        if isinstance(default, Enum):
            if choices is not None:
                raise ValueError(f"Graph parameter {name!r} takes its choices from the enum")
            enumType = type(default)
            choices = tuple(member.name for member in enumType)
            defaultValue = default.name
        if choices is None:
            choices = self._parameterChoices.get(name)

        valueType = type(defaultValue)
        if valueType not in (bool, int, float, str):
            raise TypeError(f"Unsupported type for graph parameter {name!r}: {valueType.__name__}")
        defaultValue = _parameter_value(name, defaultValue, valueType)
        value = _parameter_value(name, self._parameterOverrides.get(name, defaultValue), valueType)

        resolvedValue = value
        if isinstance(value, str):
            if choices is None or isinstance(choices, str):
                raise ValueError(f"Graph parameter {name!r} needs a list of choices")
            choices = tuple(choices)
            if not choices or not all(isinstance(choice, str) and choice for choice in choices):
                raise ValueError(f"Graph parameter {name!r} expects nonempty string choices")
            if len(set(choices)) != len(choices):
                raise ValueError(f"Graph parameter {name!r} has duplicate choices")
            if defaultValue not in choices or value not in choices:
                raise ValueError(f"Graph parameter {name!r} expects one of {choices!r}")
            if enumType is not None:
                resolvedValue = enumType[value]
        elif choices is not None:
            raise ValueError(f"Graph parameter {name!r} only supports choices for strings and enums")

        if bounds is not None:
            if type(value) is not int and type(value) is not float:
                raise ValueError(f"Graph parameter {name!r} only supports numeric bounds")
            minimum, maximum = (_parameter_value(name, limit, valueType) for limit in bounds)
            # ImGui sliders require bounds within half of the scalar type's range.
            limit = (1 << 62) - 1 if valueType is int else sys.float_info.max / 2
            if not -limit <= minimum < maximum <= limit:
                raise ValueError(f"Invalid slider bounds for graph parameter {name!r}")
            if not minimum <= defaultValue <= maximum or not minimum <= value <= maximum:
                raise ValueError(f"Graph parameter {name!r} expects a value in [{minimum}, {maximum}]")
            bounds = (minimum, maximum)

        self._parameters[name] = value
        if ui:
            if label is None:
                words = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", name.replace("_", " "))
                label = words[0].upper() + words[1:]
            self._editorControls.append({
                "name": name,
                "label": label,
                "defaultValue": defaultValue,
                "value": value,
                "choices": choices or [],
                "bounds": bounds,
            })
        return cast(_ParameterT, resolvedValue)

    def create_pass(
        self,
        name: str,
        passType: str,
        properties: dict[str, object] | None = None,
        *,
        ui: bool = False,
    ) -> None:
        self._nodes.append({
            "type": passType,
            "name": name,
            "properties": {} if properties is None else properties,
            "ui": ui,
        })

    def add_edge(self, source: str, target: str) -> None:
        self._edges.append((source, target))

    def mark_output(self, resource: str, *, name: str) -> None:
        self._outputs[name] = resource


def build_description(script: str | Path, config: str) -> str:
    path = Path(script)
    searchPath = sys.path[:]
    sys.path.insert(0, str(path.parent))
    try:
        definition = runpy.run_path(str(path))
        inputs = json.loads(config)
        graph = RenderGraph(inputs["parameterOverrides"], inputs["parameterChoices"])
        definition["build_graph"](graph)
        return json.dumps({
            "nodes": graph._nodes,
            "edges": graph._edges,
            "outputs": graph._outputs,
            "showObjectList": graph.showObjectList,
            "parameters": graph._parameters,
            "editorControls": graph._editorControls,
        }, allow_nan=False)
    except Exception:
        raise RuntimeError(traceback.format_exc()) from None
    finally:
        sys.path[:] = searchPath
