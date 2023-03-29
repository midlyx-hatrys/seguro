{
  description = "Seguro";

  inputs = {
    nixpkgs.url = "github:cmm/nixpkgs";

    parts = {
      url = "github:hercules-ci/flake-parts";
      inputs.nixpkgs-lib.follows = "nixpkgs";
    };

    mini-compile-commands = {
      url = "github:danielbarter/mini_compile_commands";
      flake = false;
    };
  };

  outputs = inputs @ {
    self
    , nixpkgs
    , parts
    , mini-compile-commands
  }: parts.lib.mkFlake { inherit inputs; } (let
    inherit (builtins) attrValues;
  in {
    systems = ["x86_64-linux" "aarch64-linux" "aarch64-darwin" "x86_64-darwin"];

    flake = {
      overlays.default = pkgs: _: let
        llvm = pkgs.llvmPackages_latest;
      in {
        seguro =  pkgs.stdenv.mkDerivation {
          name = "seguro";
          src = ./.;
          nativeBuildInputs = attrValues {
            inherit (pkgs)
              cargo
              gnumake
              rustc
            ;
            inherit (llvm)
              clang
            ;
          };
          buildInputs = attrValues {
            inherit (pkgs)
              foundationdb71
            ;
          };
          LIBCLANG_PATH = "${llvm.libclang.lib}/lib";
        };
      };
    };

    perSystem = { pkgs, system, ... }: {
      imports = [{ config._module.args.pkgs = import nixpkgs { inherit system; overlays = [self.overlays.default]; }; }];

      packages.default = pkgs.seguro;
      apps.default = {
        type = "app";
        program = "${pkgs.seguro}/bin/test-unit";
      };

      devShells.default = self.packages.${system}.default.overrideAttrs (super: {
        stdenv = (pkgs.callPackage mini-compile-commands { }).wrap super.stdenv;
        nativeBuildInputs = super.nativeBuildInputs ++ (attrValues {
          inherit (pkgs)
            cargo-edit
            clippy
            rustfmt
          ;
        });
      });
    };
  });
}
