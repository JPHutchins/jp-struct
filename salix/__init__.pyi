from typing import Any, Final

from typing_extensions import dataclass_transform

@dataclass_transform(frozen_default=True)
class Struct:
    __struct_fields__: Final[tuple[str, ...]]
    __struct_defaults__: Final[tuple[Any, ...]]
    def __init_subclass__(
        cls,
        *,
        frozen: bool = True,
        eq: bool = True,
        order: bool = False,
        repr: bool = True,
        match_args: bool = True,
        weakref: bool = False,
    ) -> None: ...

def set_field(instance: Struct, name: str, value: object, /) -> None: ...
