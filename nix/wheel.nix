# One wheel, cross-compiled with `zig cc`.
#
# zig is the cross compiler because it bundles the libc headers and stubs for
# every target here, so a single Linux builder reaches Linux, macOS and Windows
# without an SDK. The extension resolves Py* through the interpreter at load
# time on ELF and Mach-O, so only Windows links an import library.
#
# Only the payload is ours: the metadata comes from a wheel setuptools built,
# and the unpack/repack/retag is the `wheel` project's own. Nothing here writes
# a RECORD or a WHEEL by hand.
{
  lib,
  stdenvNoCC,
  fetchurl,
  zig,
  python3,
  python3Packages,
  src,
  baseWheel,
  release,
  pythonMinor,
  python,
  platformName,
  platform,
}:

let
  version = (lib.importTOML (src + "/pyproject.toml")).project.version;

  nodot = lib.replaceStrings [ "." ] [ "" ] pythonMinor;
  moduleName = lib.replaceStrings [ "{nodot}" ] [ nodot ] platform.moduleName;

  distribution = fetchurl {
    url =
      "https://github.com/astral-sh/python-build-standalone/releases/download/"
      + "${release}/cpython-${python.version}+${release}-"
      + "${platform.pbsTriple}${python.pbsVariant}-install_only_stripped.tar.gz";
    hash = python.hashes.${platformName};
  };
in
stdenvNoCC.mkDerivation {
  pname = "salix-wheel-${pythonMinor}-${platformName}";
  inherit version src;

  nativeBuildInputs = [
    zig
    python3
    python3Packages.wheel
  ];

  dontConfigure = true;
  dontInstall = true;

  buildPhase = ''
    runHook preBuild

    # zig writes to its cache unconditionally and $HOME is not writable here.
    export ZIG_GLOBAL_CACHE_DIR="$NIX_BUILD_TOP/zig-cache"

    mkdir -p "$NIX_BUILD_TOP/python"
    tar xzf ${distribution} --strip-components=1 -C "$NIX_BUILD_TOP/python"

    mapfile -t cFlags < <(python3 build_config.py c-flags)
    mapfile -t sources < <(python3 build_config.py sources)

    zig cc \
      -target ${platform.zigTarget} \
      "''${cFlags[@]}" \
      -fPIC -shared \
      -I"$NIX_BUILD_TOP/python/include" \
      -I"$NIX_BUILD_TOP/python/include/python${pythonMinor}" \
      "''${sources[@]}" \
      ${lib.optionalString platform.linkPythonLibrary ''"$NIX_BUILD_TOP/python/libs/python${nodot}.lib"''} \
      ${lib.escapeShellArgs platform.extraFlags} \
      -o "$NIX_BUILD_TOP/${moduleName}"

    # The base wheel carries the metadata and a payload built for this builder;
    # swap in the one just cross-compiled, then let `wheel` restate the tags and
    # recompute RECORD.
    wheel unpack --dest "$NIX_BUILD_TOP/unpacked" ${baseWheel}/*.whl
    unpacked=("$NIX_BUILD_TOP"/unpacked/*/)

    rm "''${unpacked[0]}"/salix/__init__.*.so
    cp "$NIX_BUILD_TOP/${moduleName}" "''${unpacked[0]}/salix/${moduleName}"

    mkdir -p "$out"
    wheel pack --dest-dir "$out" "''${unpacked[0]}"
    wheel tags --remove \
      --python-tag ${python.tag} \
      --abi-tag ${python.abiTag} \
      --platform-tag ${platform.platformTag} \
      "$out"/*.whl

    runHook postBuild
  '';

  meta = {
    description = "salix wheel for CPython ${pythonMinor} on ${platformName}";
    homepage = "https://github.com/JPHutchins/salix";
    platforms = lib.platforms.all;
  };
}
