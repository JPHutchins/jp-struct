"""Forward references, which salix reads without resolving.

A field is a name and a position; the annotation beside it is never a type as
far as salix is concerned. PEP 649 is what makes that distinction reachable,
and every test in the class below is a case where resolving would have failed.
"""

import functools
import sys

import pytest

from salix import Struct


def test_a_hand_written_annotate_is_called_directly():
    """The only path an interpreter without PEP 649 has: no __annotations__ in
    the namespace, and an __annotate__ the class body wrote itself. Outside the
    class below because that pre-3.14 branch has nothing else covering it.
    """

    def annotate(format):
        return {"x": int, "y": int}

    Manual = type(Struct)("Manual", (Struct,), {"__annotate__": annotate})

    assert Manual.__struct_fields__ == ("x", "y")
    assert Manual(1, 2).x == 1


class TestANonFunctionAnnotate:
    """annotationlib's FORWARDREF path rebuilds the callable from its
    __globals__ and __code__, which only a plain function has. Anything else is
    left unescalated so it raises what a plain function raises, rather than an
    AttributeError about the missing attribute.
    """

    @staticmethod
    def protocol_correct(format):
        """PEP 649 says: implement VALUE, raise for the rest."""

        if format != 1:
            raise NotImplementedError

        return {"x": Undefined}  # noqa: F821 -- unresolvable on purpose

    @staticmethod
    def wrapped_in_an_object(annotate):
        class Wrapper:
            def __call__(self, format):
                return annotate(format)

        return Wrapper()

    @pytest.mark.parametrize("wrap", ["plain", "partial", "callable"])
    def test_an_unresolvable_name_reports_itself_whatever_the_callable_is(self, wrap):
        """All three surface the NameError rather than an AttributeError about
        __globals__, which is what the PyFunction_Check guard is for.

        The message alone cannot tell any of that: a protocol-correct annotate
        refuses VALUE_WITH_FAKE_GLOBALS too, so annotationlib falls back to a
        real-globals VALUE re-run and raises the same NameError a direct call
        would have. `__context__` is what differs, and asserting it makes both
        the guard and the escalation detectable here -- deleting either one
        moves a `__context__` that this now checks.
        """

        annotate = TestANonFunctionAnnotate.protocol_correct
        wrapped = {
            "plain": annotate,
            "partial": functools.partial(annotate),
            "callable": TestANonFunctionAnnotate.wrapped_in_an_object(annotate),
        }[wrap]

        with pytest.raises(NameError, match="Undefined") as raised:
            type(Struct)("Wrapped", (Struct,), {"__annotate__": wrapped})

        # The discriminator the message cannot carry: only the plain function
        # reaches annotationlib, and only it comes back with a __context__ from
        # having done so. Below 3.14 nothing escalates, so nobody has one.
        escalates = wrap == "plain" and sys.version_info >= (3, 14)

        assert (raised.value.__context__ is not None) is escalates

    def test_a_callable_object_is_accepted_when_its_names_resolve(self):
        class Annotate:
            def __call__(self, format):
                return {"x": int, "y": int}

        Built = type(Struct)("Built", (Struct,), {"__annotate__": Annotate()})

        assert Built.__struct_fields__ == ("x", "y")
        assert Built(1, 2).y == 2

    @pytest.mark.skipif(
        sys.version_info < (3, 14),
        reason="the escalation this drives is compiled out before 3.14",
    )
    def test_a_plain_function_can_reach_the_escalation_success_branch(self):
        """A hand-written annotate returning through `escalated != NULL`.
        `test_the_escalation_answers_with_what_forwardref_returned` is the
        other; this one is about reaching the branch, that one about what comes
        back from it.

        annotationlib rebuilds the function against fake globals and calls the
        copy with VALUE_WITH_FAKE_GLOBALS, where names resolve to stringifiers.
        A hand-written annotate reaches this only by evaluating for that format
        -- one that answers NotImplementedError to everything but VALUE drives
        the failure branch instead, which is the parametrized test above.
        """

        def fake_globals_aware(format):
            if format in (1, 2):
                return {"x": Undefined, "y": int}  # noqa: F821 -- unresolvable on purpose

            raise NotImplementedError

        Built = type(Struct)("Built", (Struct,), {"__annotate__": fake_globals_aware})

        assert Built.__struct_fields__ == ("x", "y")
        assert Built(1, 2).y == 2

    @pytest.mark.skipif(
        sys.version_info < (3, 14),
        reason="the escalation this describes is compiled out before 3.14",
    )
    def test_the_escalation_answers_with_what_forwardref_returned(self):
        """A hand-written annotate that answers different keys for the two
        formats gets the FORWARDREF ones, because that is the call that
        succeeded -- there is no VALUE dict to prefer, only the NameError it
        raised.

        Diffing the two would mean asking twice on every rescue to catch an
        annotate that contradicts itself. Recorded rather than guarded.
        """

        def inconsistent(format):
            if format == 1:
                raise NameError("nope", name="nope")

            return {"only_this": int}

        Built = type(Struct)("Built", (Struct,), {"__annotate__": inconsistent})

        assert Built.__struct_fields__ == ("only_this",)

    def test_an_empty_name_is_not_a_forward_reference(self):
        """`.name` set but empty names no symbol, so it is not the interpreter
        saying a lookup failed. The boundary of the discriminator, pinned.
        """

        def forged():
            raise NameError(name="")

        with pytest.raises(NameError):

            class Refused(Struct):
                v: forged()

    def test_an_annotate_that_refuses_VALUE_is_refused_back(self):
        """PEP 649 makes VALUE the one format an __annotate__ must implement,
        and salix asks for it first. An older flow tried another format first
        and fell back, so an annotate implementing only that one built its
        class; it does not any more, and this is the shape that changed.
        """

        def inverted(format):
            if format == 1:
                raise NotImplementedError

            return {"x": int}

        with pytest.raises(NotImplementedError):
            type(Struct)("Inverted", (Struct,), {"__annotate__": inverted})

    def test_a_partial_is_accepted_when_its_names_resolve(self):
        def annotate(format):
            return {"z": int}

        Built = type(Struct)("Built", (Struct,), {"__annotate__": functools.partial(annotate)})

        assert Built.__struct_fields__ == ("z",)
        assert Built(1).z == 1


@pytest.mark.skipif(
    sys.version_info < (3, 14),
    reason="before PEP 649 an annotation is evaluated where it is written",
)
class TestForwardReferences:
    def test_a_bare_self_reference_is_a_field(self):
        class Node(Struct):
            value: int
            nxt: Node = None  # noqa: F821

        assert Node.__struct_fields__ == ("value", "nxt")
        assert Node(1, Node(2)).nxt.value == 2

    def test_an_annotation_that_never_resolves_is_a_field_anyway(self):
        class Bare(Struct):
            x: NeverDefined  # noqa: F821

        assert Bare.__struct_fields__ == ("x",)
        assert Bare(1).x == 1

    def test_two_classes_may_refer_to_each_other(self):
        class Left(Struct):
            right: Right = None  # noqa: F821

        class Right(Struct):
            left: Left = None

        assert Left(Right()).right.left is None

    def test_the_order_survives_a_mix_of_resolvable_and_not(self):
        class Mixed(Struct):
            first: int
            second: Unresolvable  # noqa: F821
            third: str

        assert Mixed.__struct_fields__ == ("first", "second", "third")

    def test_a_subclass_may_forward_reference_too(self):
        class Base(Struct):
            x: int

        class Child(Base):
            y: Child = None  # noqa: F821

        assert Child.__struct_fields__ == ("x", "y")

    def test_the_annotations_still_resolve_once_the_class_exists(self):
        """Reading them early does not leave a ForwardRef behind for everyone else."""

        class Node(Struct):
            nxt: Node = None  # noqa: F821

        assert Node.__annotations__ == {"nxt": Node}

    def test_an_annotation_that_fails_for_its_own_reasons_says_so(self):
        """Not resolving a name is the exemption; arbitrary failure is not."""

        with pytest.raises(ZeroDivisionError):

            class Broken(Struct):
                x: 1 / 0

    def test_a_second_bad_annotation_does_not_bury_the_first(self):
        """The escalation re-evaluates every annotation, so a later one that
        fails for its own reasons raises during the rescue of the earlier one.
        The name that did not resolve is what the reader needs.
        """

        with pytest.raises(NameError, match="Missing") as raised:

            class Broken(Struct):
                x: Missing  # noqa: F821
                y: 1 / 0

        assert isinstance(raised.value.__context__, ZeroDivisionError)

    def test_a_NameError_from_inside_a_called_function_still_defers(self):
        """`.name` is filled in wherever the lookup failed, including inside a
        callee, so a buggy helper reads as a forward reference and the class
        builds. Plain 3.14 defers this too -- the divergence is the explicit
        `raise NameError`, which salix propagates and CPython would defer.

        How many times annotationlib re-runs the annotation on the way there is
        its business and not asserted: a count is not the claim, and it moves
        when annotationlib changes without the claim moving with it.
        """

        def helper():
            return undefined_inside  # noqa: F821

        class Deferred(Struct):
            x: helper()

        assert Deferred.__struct_fields__ == ("x",)

    def test_a_forged_name_attribute_reads_as_a_forward_reference(self):
        """The discriminator asks whether `.name` is set, and the keyword form
        sets it -- so this is the exemption's boundary, not a wall. Pinned
        because the positional form beside it propagates, and the difference is
        one word in the raising code.
        """

        def forged():
            raise NameError(name="x")

        class Built(Struct):
            v: forged()

        assert Built.__struct_fields__ == ("v",)

    def test_the_exemption_is_order_dependent(self):
        """The rescue is all-or-nothing: it re-evaluates every annotation and
        stringifies what it cannot resolve, so an arbitrary failure that comes
        *after* a forward reference is swallowed with it, and the same pair in
        the other order propagates.

        Not fixable while the rescue goes through `__annotate__`, which hands
        back the whole dict or nothing.
        """

        def boom():
            raise NameError("boom")

        class Rescued(Struct):
            x: Missing  # noqa: F821
            y: boom()

        assert Rescued.__struct_fields__ == ("x", "y")

        with pytest.raises(NameError, match="boom"):

            class Propagates(Struct):
                y: boom()
                x: Missing  # noqa: F821

    def test_a_raised_NameError_is_arbitrary_failure_too(self):
        """The exemption is the interpreter's failure to find a name, not the
        exception type it uses to say so.
        """

        def boom():
            raise NameError("boom")

        with pytest.raises(NameError, match="boom"):

            class Broken(Struct):
                x: boom()

    def test_the_name_that_did_not_resolve_survives_a_failed_escalation(self, monkeypatch):
        """The escalation needs annotationlib, and the NameError it displaced is
        the one worth reading if that import is what fails.
        """

        monkeypatch.setitem(sys.modules, "annotationlib", None)

        with pytest.raises(NameError, match="Missing") as raised:

            class Shadowed(Struct):
                x: Missing  # noqa: F821

        assert isinstance(raised.value.__context__, ImportError)

    def test_an_unresolved_name_inside_a_larger_expression_still_defers(self):
        """`.name` is filled in wherever the lookup was, so the exemption does
        not stop at a bare annotation.
        """

        class Deferred(Struct):
            x: Unresolvable + 1  # noqa: F821

        assert Deferred.__struct_fields__ == ("x",)
        assert Deferred(1).x == 1


@pytest.mark.skipif(sys.version_info < (3, 14), reason="no escalation before 3.14")
def test_a_pre_existing_context_is_left_alone(monkeypatch):
    """The rescue's own failure is attached only where the NameError arrived
    without a chain. Held rather than tested in place, because
    PyException_GetContext hands back a reference.
    """

    monkeypatch.setitem(sys.modules, "annotationlib", None)

    try:
        raise ValueError("earlier")
    except ValueError:
        with pytest.raises(NameError, match="Missing") as raised:

            class Chained(Struct):
                x: Missing  # noqa: F821

    assert isinstance(raised.value.__context__, ValueError)


@pytest.mark.skipif(sys.version_info < (3, 14), reason="no escalation before 3.14")
def test_an_interrupt_during_the_rescue_is_not_demoted_to_context():
    """A rescue failure becomes the NameError's `__context__`; an exit is not a
    failure to diagnose. Swallowing KeyboardInterrupt to report a name would be
    the worst of both.
    """

    class Exits:
        def __getattr__(self, name: str) -> object:
            raise KeyboardInterrupt

    def annotate(format):
        if format == 1:
            raise NameError(name="Missing")

        raise KeyboardInterrupt

    with pytest.raises(KeyboardInterrupt):
        type(Struct)("Interrupted", (Struct,), {"__annotate__": annotate})
