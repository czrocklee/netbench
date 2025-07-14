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
    src = pkgs.fetchFromGitHub {
      owner = "HdrHistogram";
      repo = "HdrHistogram_c";
      rev = "0.11.5"; # The git tag or commit hash to use
      sha256 = "sha256-29if+0H8wdpQBN48lt0ylGgtUCv/tJYZnG5LzcIqXDs=";
    };
    nativeBuildInputs = [ pkgs.cmake ];
    buildInputs = [ pkgs.zlib ];
    cmakeFlags = [ "-DHDR_HISTOGRAM_BUILD_PROGRAMS=OFF" ];
  };

  liburing-lib = pkgs.stdenv.mkDerivation {
    pname = "liburing";
    version = "2.11"; # A specific version/tag from GitHub
    src = pkgs.fetchFromGitHub {
      owner = "axboe";
      repo = "liburing";
      rev = "liburing-2.11"; # The git tag or commit hash to use
      # This hash ensures the source code is what we expect.
      # It's a security and reproducibility feature.
      sha256 = "sha256-V73QP89WMrL2fkPRbo/TSkfO7GeDsCudlw2Ut5baDzA=";
    };
  };

  asio-lib = pkgs.stdenv.mkDerivation {
    pname = "asio";
    version = "1.34.2"; # A specific version/tag from GitHub
    src = pkgs.fetchFromGitHub {
      owner = "chriskohlhoff";
      repo = "asio";
      rev = "asio-1-34-2"; # The git tag or commit hash to use
      sha256 = "sha256-B9tFXcmBn7n4wEdnfjw5o90fC/cG5+WMdu/K4T6Y+jI=";
    };
    installPhase = ''
      runHook preInstall
      mkdir -p $out/include
      cp -r $src/asio/include/* $out/include/
      runHook postInstall
    '';
  };

in 
  pkgs.mkShell {
    name = "cpp-dev-env";

    buildInputs = with pkgs; [
      cmake
      pkg-config
      ninja
      gcc
      gdb
      boost.dev
      #liburing
      mimalloc
      cli11
      hdrhistogram-c-lib
      zlib
      liburing-lib
      asio-lib
    ];
    shellHook = ''
      echo "Using nixpkgs pinned to NixOS 25.05 (stable release)"
    '';
  }
