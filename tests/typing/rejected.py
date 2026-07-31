"""What a type checker must reject.

Every ignore here is an assertion: with --warn-unused-ignores, an ignore that
stops being needed is itself an error, so this file fails if the checker ever
starts accepting one of these. That inversion is the only way to test for a
diagnostic rather than for its absence.
"""

from salix import Struct, set_field


class Point(Struct):
    x: int
    y: str


class Mutable(Struct, frozen=False):
    value: int


class Ordered(Struct, order=True):
    rank: int


class NoMatchArgs(Struct, match_args=False):
    rank: int


def a_frozen_field_may_not_be_assigned() -> None:
    Point(1, "two").x = 2  # type: ignore[misc]


def a_frozen_field_may_not_be_deleted() -> None:
    """pyright's alone: mypy does not model `del` against a read-only attribute."""

    del Point(1, "two").x  # pyright: ignore[reportAttributeAccessIssue]


def an_argument_of_the_wrong_type_is_rejected() -> None:
    Point("one", 2)  # type: ignore[arg-type]


def a_missing_argument_is_rejected() -> None:
    Point(1)  # type: ignore[call-arg]


def an_extra_argument_is_rejected() -> None:
    Point(1, "two", 3)  # type: ignore[call-arg]


def an_unknown_keyword_is_rejected() -> None:
    Point(x=1, y="two", z=3)  # type: ignore[call-arg]


def a_field_that_does_not_exist_is_rejected() -> None:
    Point(1, "two").z  # type: ignore[attr-defined]


def a_mutable_field_still_has_a_type() -> None:
    Mutable(1).value = "text"  # type: ignore[assignment]


def a_struct_without_order_has_no_comparisons() -> None:
    Point(1, "two") < Point(1, "two")  # type: ignore[operator]


def ordering_against_an_unrelated_struct_is_rejected() -> None:
    Ordered(1) < Point(1, "two")  # type: ignore[operator]


def an_unknown_class_keyword_is_rejected() -> None:
    """pyright's and ty's; mypy does not check keywords against __init_subclass__."""

    class Typo(Struct, frozn=False):  # pyright: ignore[reportGeneralTypeIssues, reportCallIssue]
        value: int


def match_args_false_leaves_no_positional_pattern() -> None:
    match NoMatchArgs(1):
        case NoMatchArgs(rank):  # type: ignore[misc]
            print(rank)


def set_field_will_not_take_a_non_struct() -> None:
    set_field(1, "x", 9)  # type: ignore[arg-type]


def set_field_will_not_take_a_name_that_is_not_a_string() -> None:
    set_field(Point(1, "two"), 5, 9)  # type: ignore[arg-type]
