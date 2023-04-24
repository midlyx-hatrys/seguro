{
  description = "Seguro";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixpkgs-unstable";

    parts = {
      url = "github:hercules-ci/flake-parts";
      inputs.nixpkgs-lib.follows = "nixpkgs";
    };

    mini-compile-commands = {
      url = "github:danielbarter/mini_compile_commands";
      flake = false;
    };

    urbit-cob = {
      url = "github:midlyx-hatrys/urbit-cob";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = inputs @ {
    self
    , nixpkgs
    , urbit-cob
    , parts
    , mini-compile-commands
  }: parts.lib.mkFlake { inherit inputs; } (let
    inherit (builtins) attrValues;

    pkgAttrs = pkgs: let
      llvm = pkgs.llvmPackages_latest;
    in {
      name = "seguro";
      src = ./.;
      nativeBuildInputs = attrValues {
        inherit (pkgs)
          gnumake
        ;
      };
      buildInputs = attrValues {
        inherit (pkgs)
          foundationdb71
          libuv
        ;
      }
      ++ [urbit-cob.packages.${pkgs.system}.default];
      LIBCLANG_PATH = "${llvm.libclang.lib}/lib";
    };
  in {
    systems = ["x86_64-linux" "aarch64-linux" "aarch64-darwin" "x86_64-darwin"];

    flake = {
      overlays.default = pkgs: _: {
        seguro = pkgs.stdenv.mkDerivation (pkgAttrs pkgs);
      };
    };

    perSystem = { pkgs, system, ... }: {
      imports = [{ config._module.args.pkgs = import nixpkgs { inherit system; overlays = [self.overlays.default]; }; }];

      packages.default = pkgs.seguro;
      apps.default = {
        type = "app";
        program = "${pkgs.seguro}/bin/test-unit";
      };

      devShells.default = let
        attrs = pkgAttrs pkgs;
      in (pkgs.mkShell.override { stdenv = (pkgs.callPackage mini-compile-commands { }).wrap pkgs.stdenv; })
        (attrs // {
          nativeBuildInputs = attrs.nativeBuildInputs ++ (attrValues {
            inherit (pkgs)
              gdb
              valgrind
            ;
          });
        });
    };
  });
}
