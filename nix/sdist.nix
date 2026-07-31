# The source distribution.
#
# PyPI takes one alongside the wheels, and it is the only way in for a platform
# the matrix does not cover. Windows is not such a platform: MSVC has no
# __attribute__((cleanup)), so there is no source install there at any time,
# which is why the wheel matrix covers both Windows architectures it can.
{
  lib,
  stdenv,
  python3,
  src,
}:

let
  interpreter = python3.withPackages (packages: [
    packages.build
    packages.setuptools
    packages.wheel
  ]);
in
stdenv.mkDerivation {
  pname = "salix-sdist";
  version = (lib.importTOML (src + "/pyproject.toml")).project.version;
  inherit src;

  nativeBuildInputs = [ interpreter ];

  dontConfigure = true;
  dontInstall = true;

  buildPhase = ''
    runHook preBuild

    mkdir -p "$out"
    python -m build --sdist --no-isolation --outdir "$out"

    runHook postBuild
  '';
}
