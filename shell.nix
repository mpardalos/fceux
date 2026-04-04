let 
    pkgs = import <nixpkgs> {};
in 
    pkgs.mkShell {
        packages = [
            pkgs.clang-tools
            pkgs.cmake
            pkgs.pkg-config
            pkgs.SDL2
            pkgs.ffmpeg
            # pkgs.libX11
            # pkgs.libXdmcp
            # pkgs.libxcb
            pkgs.lua5_1
            pkgs.minizip
            pkgs.qt6.qttools
            pkgs.x264
        ];
    }
