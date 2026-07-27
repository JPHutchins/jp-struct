# One wheel, cross-compiled with `zig cc`.
#
# zig is the cross compiler because it bundles the libc headers and stubs for
# every target here, so a single Linux builder reaches Linux, macOS and Windows
# without an SDK. The extension resolves Py* through the interpreter at load
# time on ELF and Mach-O, so only Windows links an import library.
{
  lib,
  stdenvNoCC,
  fetchurl,
  zig,
  python3,
  src,
  matrix,
  targetName,
  target,
}:

let
  project = (lib.importTOML (src + "/pyproject.toml")).project;
  build = (lib.importTOML (src + "/pyproject.toml")).tool.jp-struct;

  pythonMajorMinor = lib.versions.majorMinor matrix.pythonVersion;
  pythonNoDot = lib.replaceStrings [ "." ] [ "" ] pythonMajorMinor;

  distribution = fetchurl {
    url =
      "https://github.com/astral-sh/python-build-standalone/releases/download/"
      + "${matrix.release}/cpython-${matrix.pythonVersion}+${matrix.release}-"
      + "${target.pbsTriple}-install_only_stripped.tar.gz";
    inherit (target) hash;
  };
in
stdenvNoCC.mkDerivation {
  pname = "jp-struct-wheel-${targetName}";
  inherit (project) version;
  inherit src;

  nativeBuildInputs = [
    zig
    python3
  ];

  dontConfigure = true;
  dontInstall = true;

  buildPhase = ''
    runHook preBuild

    # zig writes to its cache unconditionally and $HOME is not writable here.
    export ZIG_GLOBAL_CACHE_DIR="$NIX_BUILD_TOP/zig-cache"

    mkdir -p "$NIX_BUILD_TOP/python"
    tar xzf ${distribution} --strip-components=1 -C "$NIX_BUILD_TOP/python"

    zig cc \
      -target ${target.zigTarget} \
      ${lib.escapeShellArgs build.c-flags} \
      -fPIC -shared \
      -I"$NIX_BUILD_TOP/python/include" \
      -I"$NIX_BUILD_TOP/python/include/python${pythonMajorMinor}" \
      ${lib.escapeShellArgs build.sources} \
      ${lib.optionalString target.linkPythonLibrary ''"$NIX_BUILD_TOP/python/libs/python${pythonNoDot}.lib"''} \
      ${lib.escapeShellArgs target.extraFlags} \
      -o "$NIX_BUILD_TOP/${target.moduleName}"

    mkdir -p "$out"
    python3 tools/pack_wheel.py \
      --extension "$NIX_BUILD_TOP/${target.moduleName}" \
      --extension-name ${lib.escapeShellArg target.moduleName} \
      --python-tag ${matrix.pythonTag} \
      --abi-tag ${matrix.abiTag} \
      --platform-tag ${target.platformTag} \
      --outdir "$out"

    runHook postBuild
  '';

  meta = {
    description = "jp-struct wheel for ${targetName}";
    homepage = "https://github.com/JPHutchins/jp-struct";
    platforms = lib.platforms.all;
  };
}
