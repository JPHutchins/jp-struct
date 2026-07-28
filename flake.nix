{
  description = "jp-struct — a C-backed, inheritable Struct base class for Python";

  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      inherit (nixpkgs) lib;

      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      forAllSystems = lib.genAttrs systems;

      matrix = import ./nix/python-targets.nix;

      # .python-version is the single source of truth for the interpreter set;
      # drift between it and the generated pins is a build error, not a
      # silently smaller wheel set.
      declaredPythons = lib.sort (a: b: a < b) (
        lib.splitString "\n" (lib.removeSuffix "\n" (lib.fileContents ./.python-version + "\n"))
      );
      pinnedPythons = lib.sort (a: b: a < b) (builtins.attrNames matrix.pythons);

      wheelIds = lib.concatMap (
        pythonMinor:
        map (platformName: { inherit pythonMinor platformName; }) (
          builtins.attrNames matrix.pythons.${pythonMinor}.hashes
        )
      ) pinnedPythons;

      # Only what a wheel is built from, so editing the README or the tests does
      # not invalidate every cross build.
      buildSource = lib.fileset.toSource {
        root = ./.;
        fileset = lib.fileset.unions [
          ./src
          ./tools/pack_wheel.py
          ./build_config.py
          ./pyproject.toml
          ./README.md
          ./LICENSE
        ];
      };

      # The in-file tests read src/ and tests/c/ only, so touching them does not
      # invalidate a single cross build.
      testSource = lib.fileset.toSource {
        root = ./.;
        fileset = lib.fileset.unions [
          ./src
          ./tests/c
          ./build_config.py
        ];
      };

      perSystem =
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};

          wheels = lib.listToAttrs (
            map (
              { pythonMinor, platformName }:
              lib.nameValuePair "${pythonMinor}-${platformName}" (
                pkgs.callPackage ./nix/wheel.nix {
                  src = buildSource;
                  inherit (matrix) release;
                  inherit pythonMinor platformName;
                  python = matrix.pythons.${pythonMinor};
                  platform = matrix.platforms.${platformName};
                }
              )
            ) wheelIds
          );

          all = pkgs.symlinkJoin {
            name = "jp-struct-wheels";
            paths = lib.attrValues wheels;
          };

          named = lib.mapAttrs' (name: wheel: lib.nameValuePair "wheel-${name}" wheel) wheels;

          jphfmt = pkgs.callPackage ./nix/jphfmt.nix { };

          cTests = pkgs.callPackage ./nix/c-tests.nix { src = testSource; };
        in
        {
          inherit
            pkgs
            wheels
            all
            named
            jphfmt
            cTests
            ;
        };

      forSystem = forAllSystems perSystem;
    in
    assert lib.assertMsg (declaredPythons == pinnedPythons) (
      ".python-version lists ${toString declaredPythons} but nix/python-targets.nix pins "
      + "${toString pinnedPythons}; regenerate with tools/update_python_targets.py"
    );
    {
      packages = forAllSystems (
        system:
        forSystem.${system}.named
        // {
          default = forSystem.${system}.all;
          c-tests = forSystem.${system}.cTests;
        }
      );

      # The only supported environment: enter it once, then run camas (and any
      # editor or agent) from inside. Every tool the tasks invoke is here, so a
      # task command is bare -- nothing pays `nix develop` per invocation.
      devShells = forAllSystems (
        system:
        let
          inherit (forSystem.${system}) pkgs jphfmt;
        in
        {
          default = pkgs.mkShell {
            # No python here on purpose: uv owns the interpreters, driven by
            # .python-version, which is the single source of truth for which
            # versions this project builds and tests against.
            packages = [
              pkgs.uv
              pkgs.zig
              pkgs.nixfmt
              pkgs.clang-tools
              pkgs.gdb
              pkgs.git
              jphfmt
            ];

            # stdenv exports its own CC during setup, after `env` is applied,
            # so the compiler choice has to be made here to survive.
            shellHook = ''
              unset PYTHONPATH

              # The wheels are cross-compiled with zig, so the local build uses
              # it too -- one compiler, one warning set, no -Werror surprise
              # that only shows up at release time.
              export CC="zig cc"
              export LDSHARED="zig cc -shared"
            '';
          };
        }
      );

      checks = forAllSystems (
        system:
        let
          inherit (forSystem.${system})
            pkgs
            all
            named
            cTests
            ;
        in
        named
        // {
          c-tests = cTests;

          nixfmt = pkgs.runCommand "nixfmt-check" { nativeBuildInputs = [ pkgs.nixfmt ]; } ''
            nixfmt --check ${./flake.nix} ${./nix/wheel.nix} ${./nix/python-targets.nix}
            touch $out
          '';

          # Five of the six targets cannot run here, so every wheel is checked
          # for internal consistency and for a payload whose architecture and
          # container match the tag it is published under. twine is the gate
          # PyPI itself applies; nothing off the shelf checks the payload.
          wheels-verified =
            pkgs.runCommand "wheels-verified"
              {
                nativeBuildInputs = [
                  pkgs.python314
                  pkgs.python3Packages.twine
                ];
              }
              ''
                python ${./tools/check_wheel.py} ${all}/*.whl
                twine check --strict ${all}/*.whl
                touch $out
              '';
        }
        # The native wheel is the only one this builder can execute, so it is
        # the only one whose importability can actually be demonstrated.
        // lib.optionalAttrs (system == "x86_64-linux") {
          wheel-smoke =
            pkgs.runCommand "wheel-smoke"
              {
                nativeBuildInputs = [
                  (pkgs.python314.withPackages (ps: [
                    ps.pip
                    ps.pytest
                    ps.hypothesis
                  ]))
                ];
              }
              ''
                pip install --no-index --no-deps --target=site \
                  ${forSystem.${system}.wheels."3.14-manylinux-x86_64"}/*.whl
                export PYTHONPATH=$PWD/site
                export JPSTRUCT_REQUIRE_INSTALLED=1
                python -m pytest -q -p no:cacheprovider ${./tests}
                touch $out
              '';
        }
      );

      formatter = forAllSystems (system: forSystem.${system}.pkgs.nixfmt);
    };
}
