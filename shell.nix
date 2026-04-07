let
    # Use nixos-24.11 for Qt 6.8.x and CMake 3.31.x
    pkgs = import (fetchTarball "https://github.com/NixOS/nixpkgs/archive/nixos-24.11.tar.gz") {};
in
    pkgs.mkShell {
        packages = [
            pkgs.clang-tools
            pkgs.cmake
            pkgs.pkg-config
            pkgs.SDL2
            pkgs.ffmpeg
            pkgs.lua5_1
            pkgs.minizip
            pkgs.qt6.qttools
            pkgs.x264
        ];
    }
