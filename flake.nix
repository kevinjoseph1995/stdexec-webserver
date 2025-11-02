{
  # A brief description of what this flake provides
  description = "A minimal C++ development environment with LLVM";

  # Inputs are dependencies for your flake
  inputs = {
    # nixpkgs is the main package repository for Nix
    # We use the unstable channel to get the latest packages
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  # Outputs define what this flake produces (dev environments, packages, etc.)
  outputs =
    { self, nixpkgs }:
    let
      # Target x86_64 Linux only
      system = "x86_64-linux";
      # Get the package set for x86_64-linux
      pkgs = nixpkgs.legacyPackages.${system};
    in
    {
      # devShells.default is the development environment
      # Activate it with: nix develop
      devShells.${system}.default = pkgs.mkShell {
        # buildInputs lists all the packages available in this shell
        buildInputs = with pkgs; [
          gcc15
          llvmPackages_latest.clang-tools # Includes clangd language server
          cmake # CMake build system
          ninja # Ninja build system
          gdb # GNU Debugger
          liburing # liburing for io_uring support
          lsof
          jujutsu # Source control management tool
        ];

        # shellHook runs when you enter the development shell
        shellHook = ''
          echo "C++ development environment loaded"
          echo "g++ version: $(g++ --version | head -n 1)"
          echo "cmake version: $(cmake --version | head -n 1)"
          fish
        '';
      };
    };
}
