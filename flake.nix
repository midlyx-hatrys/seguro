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
    inherit (builtins) attrValues map filter;

    inherit (nixpkgs) lib;

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
          argtable
          gmp
          foundationdb71
          libuv
          mmh3
          urbit-cob
        ;
      }
      ++ [urbit-cob.packages.${pkgs.system}.default];
      LIBCLANG_PATH = "${llvm.libclang.lib}/lib";
    };
  in {
    systems = ["x86_64-linux" "aarch64-linux" "aarch64-darwin" "x86_64-darwin"];

    flake = {
      overlays.default = nixpkgs.lib.composeManyExtensions [
        urbit-cob.overlays.default
        (pkgs: _: {
          seguro = pkgs.stdenv.mkDerivation (pkgAttrs pkgs);
        })
      ];
    };

    perSystem = { pkgs, system, ... }: let
      stdenv = pkgs.stdenv;
      devShellInputs = attrValues {
        inherit (pkgs)
          gdb
          valgrind
        ;
      };
    in {
      imports = [{ config._module.args.pkgs = import nixpkgs { inherit system; overlays = [self.overlays.default]; }; }];

      packages.default = pkgs.seguro;
      apps.default = {
        type = "app";
        program = "${pkgs.seguro}/bin/test-unit";
      };

      devShells.default = let
        attrs = pkgAttrs pkgs;
      in (pkgs.mkShell.override { stdenv = (pkgs.callPackage mini-compile-commands { }).wrap stdenv; })
        (attrs // {
          nativeBuildInputs = attrs.nativeBuildInputs ++ devShellInputs;

          shellHook = let
            allPossibleInputs = stdenv.defaultBuildInputs ++ stdenv.defaultNativeBuildInputs
                                ++ stdenv.allowedRequisites ++ devShellInputs
                                ++ attrs.buildInputs ++ attrs.nativeBuildInputs
                                ++ [stdenv.cc.cc];
            allowedInputs = lib.subtractLists stdenv.disallowedRequisites allPossibleInputs;
            inputDrvs = lib.unique (filter lib.isDerivation allowedInputs);
            infoOuts = map (lib.getOutput "info") inputDrvs;
            infoPaths = filter builtins.pathExists (map (out: builtins.toPath "${out}/share/info") infoOuts);
          in ''
            export INFOPATH=${builtins.concatStringsSep ":" infoPaths}:$INFOPATH
          '';
        });
    };
  });
}
