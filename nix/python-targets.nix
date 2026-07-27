# The wheel matrix: one entry per (interpreter, platform) a wheel is built for.
#
# Headers come from python-build-standalone rather than nixpkgs, because a
# cross-built extension needs the *target's* pyconfig.h and nixpkgs has no
# Windows or Darwin CPython to take it from. `moduleName` and `platformTag` are
# read off the real distributions, not derived -- see nix/README.md.
#
# Regenerate the pins with `uv run python tools/update_python_targets.py`.
{
  release = "20260718";
  pythonVersion = "3.14.6";
  pythonTag = "cp314";
  abiTag = "cp314";

  targets = {
    manylinux-x86_64 = {
      pbsTriple = "x86_64-unknown-linux-gnu";
      hash = "sha256-hr8Qf2X8MLVvKyY7Jnl/y7FmH1MVkQzb8n9zPrhzi3Q=";
      zigTarget = "x86_64-linux-gnu.2.17";
      moduleName = "record.cpython-314-x86_64-linux-gnu.so";
      platformTag = "manylinux_2_17_x86_64";
      extraFlags = [ ];
      linkPythonLibrary = false;
    };

    manylinux-aarch64 = {
      pbsTriple = "aarch64-unknown-linux-gnu";
      hash = "sha256-wqLKoDqpvrd6W7taAJyP53uWpWkl2i26GAlSN+MPrwQ=";
      zigTarget = "aarch64-linux-gnu.2.17";
      moduleName = "record.cpython-314-aarch64-linux-gnu.so";
      platformTag = "manylinux_2_17_aarch64";
      extraFlags = [ ];
      linkPythonLibrary = false;
    };

    macos-x86_64 = {
      pbsTriple = "x86_64-apple-darwin";
      hash = "sha256-LQMVS8g8bS0Ei7oju4CLAg41ICj1vVQadA5URn/JzHU=";
      zigTarget = "x86_64-macos.10.13";
      moduleName = "record.cpython-314-darwin.so";
      platformTag = "macosx_10_13_x86_64";
      extraFlags = [
        "-undefined"
        "dynamic_lookup"
      ];
      linkPythonLibrary = false;
    };

    macos-aarch64 = {
      pbsTriple = "aarch64-apple-darwin";
      hash = "sha256-Xo4HRssBCNE49RDPuyjEsDG57WO7vj5Do0x31GY8T6E=";
      zigTarget = "aarch64-macos.11.0";
      moduleName = "record.cpython-314-darwin.so";
      platformTag = "macosx_11_0_arm64";
      extraFlags = [
        "-undefined"
        "dynamic_lookup"
      ];
      linkPythonLibrary = false;
    };

    # Windows extensions must resolve Py* at link time, so these link the
    # official python314.lib. zig's mingw target uses the UCRT, which is the
    # same C runtime MSVC-built CPython uses.
    windows-x86_64 = {
      pbsTriple = "x86_64-pc-windows-msvc";
      hash = "sha256-DFyfIxcE+0kUm8jyudnb5D7q9sVPNi7w/CBmzmS00Q8=";
      zigTarget = "x86_64-windows-gnu";
      moduleName = "record.pyd";
      platformTag = "win_amd64";
      extraFlags = [ ];
      linkPythonLibrary = true;
    };

    windows-aarch64 = {
      pbsTriple = "aarch64-pc-windows-msvc";
      hash = "sha256-iay5GE3PyzSGKQVJTkpBsn34+DZJU9V5bMD5FBFg1/k=";
      zigTarget = "aarch64-windows-gnu";
      moduleName = "record.pyd";
      platformTag = "win_arm64";
      extraFlags = [ ];
      linkPythonLibrary = true;
    };
  };
}
