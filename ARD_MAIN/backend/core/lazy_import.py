"""Defer heavy SDK imports (Whisper/torch, Gemini, OpenAI, SciPy) until first use.

Call sites keep their usual `module.attr` form; the real import happens on the
first attribute access, or earlier from the background warm-up.
"""
import importlib
import threading


class LazyModule:
    def __init__(self, name):
        self._name = name
        self._module = None
        self._lock = threading.Lock()

    def load(self):
        if self._module is None:
            with self._lock:
                if self._module is None:
                    self._module = importlib.import_module(self._name)
        return self._module

    def __getattr__(self, attr):
        return getattr(self.load(), attr)

    def __repr__(self):
        state = "loaded" if self._module is not None else "not loaded"
        return f"<LazyModule {self._name} ({state})>"
