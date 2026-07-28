from typing import Any, ClassVar

from typing_extensions import dataclass_transform

class StructMeta(type):
    __struct_fields__: tuple[str, ...]
    __struct_defaults__: tuple[Any, ...]

@dataclass_transform(frozen_default=True)
class Struct(metaclass=StructMeta):
    __match_args__: ClassVar[tuple[str, ...]]
    __struct_fields__: tuple[str, ...]
    __struct_defaults__: tuple[Any, ...]
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
