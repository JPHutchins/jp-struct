"""Behaviour specific to a free-threaded interpreter.

The GIL test is the one that matters: a module that fails to declare
Py_mod_gil still imports and still passes every other test here, just with the
GIL switched back on, so nothing else would notice.
"""

import sys
import sysconfig
import threading

import pytest

from salix import Struct

pytestmark = pytest.mark.skipif(
    not sysconfig.get_config_var("Py_GIL_DISABLED"), reason="not a free-threaded build"
)

THREADS = 8
ITERATIONS = 20_000


class Point(Struct):
    x: int
    y: int = 7


def test_importing_does_not_re_enable_the_gil():
    assert not sys._is_gil_enabled()


def run_on_every_thread(work):
    failures: list[BaseException] = []

    def guarded():
        try:
            work()
        except BaseException as failure:  # noqa: BLE001 -- any failure in a thread is the result
            failures.append(failure)

    threads = [threading.Thread(target=guarded) for _ in range(THREADS)]

    for thread in threads:
        thread.start()

    for thread in threads:
        thread.join()

    assert not failures, failures[:3]


def test_concurrent_construction_and_reads():
    def work():
        for i in range(ITERATIONS):
            point = Point(i)

            assert point.x == i
            assert point.y == 7
            assert point == Point(i, 7)
            assert hash(point) == hash((i, 7))
            assert repr(point) == f"Point(x={i}, y=7)"

    run_on_every_thread(work)


def test_concurrent_subclass_creation():
    def work():
        for i in range(ITERATIONS // 100):
            subclass = type(Point)("Sub", (Point,), {"__annotations__": {"z": int}, "z": i})

            assert subclass(1).z == i
            assert subclass.__match_args__ == ("x", "y", "z")

    run_on_every_thread(work)


def test_concurrent_writes_to_a_shared_mutable_struct():
    """Writes go through CPython's own member descriptor, so whatever a
    free-threaded build guarantees for __slots__ is inherited rather than
    reimplemented. This is the check on that claim."""

    class Counter(Struct, frozen=False):
        value: object

    shared = Counter(0)

    def work():
        for i in range(ITERATIONS):
            shared.value = i
            assert isinstance(shared.value, int)

    run_on_every_thread(work)


def test_a_shared_struct_is_safe_to_read_from_every_thread():
    shared = Point(1, 2)

    def work():
        for _ in range(ITERATIONS):
            assert shared.x == 1
            assert shared.__struct_fields__ == ("x", "y")

    run_on_every_thread(work)
