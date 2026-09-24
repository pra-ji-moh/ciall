"""
Starting up cost more than playing.

Bringing in the games' toolkit pulls in a drawing library and a web server, five
seconds of it, and a game is a fresh program each time. Neither is touched when the
child simply plays: the drawing is for watching a recording back, the server is for
handing the games out over the network. So they are left as stand-ins, made only if
something truly asks for them. CIALL_HEAVY=1 brings the real ones back.
"""
import sys
import types


class _Stub(types.ModuleType):
    """Answers to any name with another stand-in, and to any call with one too."""

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        child = _Stub(self.__name__ + "." + name)
        sys.modules[child.__name__] = child
        setattr(self, name, child)
        return child

    def __call__(self, *a, **k):
        return _Stub(self.__name__ + "()")


def light():
    """Leave stand-ins where the heavy parts would go. Returns what it stood in for."""
    import os
    if os.environ.get("CIALL_HEAVY"):
        return []
    stood = []
    for name in ("matplotlib", "matplotlib.pyplot", "matplotlib.animation",
                 "matplotlib.colors", "matplotlib.patches", "flask"):
        if name not in sys.modules:
            sys.modules[name] = _Stub(name)
            stood.append(name)
    return stood
