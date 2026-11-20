import json
from pathlib import Path
import runpy
import sys
import traceback


class ResourceReference:
    def __init__(self, path):
        self._path = path

    def __getattr__(self, field):
        return ResourceReference((*self._path, field))


class RenderGraph:
    def __init__(self):
        self._nodes = []
        self._outputs = {}

    def input(self, name):
        return ResourceReference(("inputs", name))

    def add_pass(self, passType, name, *inputs, properties=None):
        self._nodes.append({
            "type": passType,
            "name": name,
            "inputs": [resource._path for resource in inputs],
            "properties": {} if properties is None else properties,
        })
        return ResourceReference(("passes", name))

    def mark_output(self, name, resource):
        self._outputs[name] = resource._path


def build_description(script, config):
    path = Path(script)
    searchPath = sys.path[:]
    sys.path.insert(0, str(path.parent))
    try:
        build = runpy.run_path(str(path))["build_graph"]
        graph = RenderGraph()
        build(graph, json.loads(config))
        return json.dumps({"nodes": graph._nodes, "outputs": graph._outputs})
    except Exception:
        raise RuntimeError(traceback.format_exc()) from None
    finally:
        sys.path[:] = searchPath
