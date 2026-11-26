{
  description = "A Nix-flake-based C/C++ development environment with an aarch64 cross-compiler for the BeagleY-AI";

  inputs.nixpkgs.url = "https://flakehub.com/f/NixOS/nixpkgs/0";

  outputs = {self, ...} @ inputs: let
    supportedSystems = [
      "x86_64-linux"
      "aarch64-linux"
      "aarch64-darwin"
    ];
    forEachSupportedSystem = f:
      inputs.nixpkgs.lib.genAttrs supportedSystems (
        system:
          f {
            inherit system;
            pkgs = import inputs.nixpkgs {inherit system;};
          }
      );
  in {
    devShells = forEachSupportedSystem (
      {
        pkgs,
        system,
      }: {
        default =
          pkgs.mkShell.override
          {
            # Use the aarch64 cross stdenv so `cc`/`CC` defaults to the
            # BeagleY-AI cross-compiler instead of the host compiler.
            stdenv = pkgs.pkgsCross.aarch64-multiplatform.stdenv;
          }
          {
            packages = with pkgs;
              [
                clang-tools
                cmake
                codespell
                conan
                cppcheck
                doxygen
                gtest
                lcov
                vcpkg
                vcpkg-tool
                self.formatter.${system}
              ]
              ++ lib.optionals (!stdenv.hostPlatform.isDarwin) [gdb];

            # Ensure the executable doesn't specify the linker from the Nix store
            LDFLAGS = "-Wl,--dynamic-linker=/lib/ld-linux-aarch64.so.1 -Wl,-rpath,''";
          };
      }
    );

    formatter = forEachSupportedSystem ({pkgs, ...}: pkgs.alejandra);
  };
}
