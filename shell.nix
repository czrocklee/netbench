{ pkgs ? import (builtins.fetchTarball {
    # Pinning to the NixOS 25.05 channel from GitHub.
    url = "https://github.com/NixOS/nixpkgs/archive/d179d77c139e0a3f5c416477f7747e9d6b7ec315.tar.gz";
    sha256 = "sha256:0kgsm80vwh1kbir1gwb7lmi5fmgjfvh3sjik1npm256d2bh0la39";
  }) {} }:

let
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
    version = "2.12"; # A specific version/tag from GitHub
    src = pkgs.fetchFromGitHub {
      owner = "axboe";
      repo = "liburing";
      rev = "liburing-2.12"; # The git tag or commit hash to use
      # This hash ensures the source code is what we expect.
      # It's a security and reproducibility feature.
      sha256 = "sha256-sEMzkyjrCc49ogfUnzdgNtEXmW0Tz/PUKo99C965428=";
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
      # build tools
      cmake
      pkg-config
      ninja
      gcc
      gdb

      # tooling
      bmon
      clang-tools
      bpftrace

      # python for benchmark scripts
      python3
      python3Packages.matplotlib
      python3Packages.plotly

      # dependencies
      liburing-lib
      boost.dev
      mimalloc
      cli11
      magic-enum
      spdlog
      hdrhistogram-c-lib
      zlib
      asio-lib
      nlohmann_json
    ];
    shellHook = ''
      echo "Using nixpkgs pinned to NixOS 25.05 (stable release)"
    '';
  }
