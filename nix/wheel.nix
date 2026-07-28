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
      + "${platform.pbsTriple}-install_only_stripped.tar.gz";
    hash = python.hashes.${platformName};
  };
in
stdenvNoCC.mkDerivation {
  pname = "jp-struct-wheel-${pythonMinor}-${platformName}";
  inherit version src;

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

    mkdir -p "$out"
    python3 tools/pack_wheel.py \
      --extension "$NIX_BUILD_TOP/${moduleName}" \
      --extension-name ${lib.escapeShellArg moduleName} \
      --python-tag ${python.tag} \
      --abi-tag ${python.tag} \
      --platform-tag ${platform.platformTag} \
      --outdir "$out"

    runHook postBuild
  '';

  meta = {
    description = "jp-struct wheel for CPython ${pythonMinor} on ${platformName}";
    homepage = "https://github.com/JPHutchins/jp-struct";
    platforms = lib.platforms.all;
  };
}
