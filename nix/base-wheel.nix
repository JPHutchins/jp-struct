# One wheel, built the ordinary way, for its metadata rather than its payload.
#
# METADATA, WHEEL, the license directory and RECORD are the same for all 35
# targets, and they are a spec -- PEP 427, PEP 639, Metadata 2.4 -- better read
# from the reference implementation than reimplemented. The extension it
# compiles here is thrown away; nix/wheel.nix swaps in the cross-built one.
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
  pname = "salix-base-wheel";
  version = (lib.importTOML (src + "/pyproject.toml")).project.version;
  inherit src;

  nativeBuildInputs = [ interpreter ];

  dontConfigure = true;
  dontInstall = true;

  buildPhase = ''
    runHook preBuild

    mkdir -p "$out"
    python -m build --wheel --no-isolation --outdir "$out"

    runHook postBuild
  '';
}
