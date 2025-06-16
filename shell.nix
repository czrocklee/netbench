{ pkgs ? import (builtins.fetchTarball {
    # Pinning to the NixOS 25.05 channel from GitHub.
    url = "https://github.com/NixOS/nixpkgs/archive/70c74b02eac46f4e4aa071e45a6189ce0f6d9265.tar.gz";
    # Replace this with the correct sha256 for that tarball.
    sha256 = "0b4jz58kkm7dbq6c6fmbgrh29smchhs6d96czhms7wddlni1m71p";
  }) {} }:

let
  # Import the Nix Packages collection from your system's channels.
  pkgs = import <nixpkgs> {};

  # Define our custom package for the HdrHistogram_c library.
  # Nix will build this from source and make it available as a dependency.
  hdrhistogram-c-lib = pkgs.stdenv.mkDerivation {
    pname = "hdrhistogram-c";
    version = "0.11.5"; # A specific version/tag from GitHub

    # Fetch the source code directly from GitHub.
    src = pkgs.fetchFromGitHub {
      owner = "HdrHistogram";
      repo = "HdrHistogram_c";
      rev = "0.11.5"; # The git tag or commit hash to use
      # This hash ensures the source code is what we expect.
      # It's a security and reproducibility feature.
      sha256 = "sha256-29if+0H8wdpQBN48lt0ylGgtUCv/tJYZnG5LzcIqXDs=";
    };

    # The library uses cmake, so we need it as a build tool.
    nativeBuildInputs = [ pkgs.cmake ];

    # It also needs the zlib library to build its log support.
    buildInputs = [ pkgs.zlib ];

    # We can pass flags to cmake, e.g., to disable building the example programs.
    cmakeFlags = [ "-DHDR_HISTOGRAM_BUILD_PROGRAMS=OFF" ];
  };

in 
  pkgs.mkShell {
    name = "cpp-dev-env";

    buildInputs = with pkgs; [
      cmake
      gcc
      gdb
      asio
      liburing
      mimalloc
      cli11
      hdrhistogram-c-lib
    ];
    shellHook = ''
      echo "Using nixpkgs pinned to NixOS 25.05 (stable release)"
    '';
  }
