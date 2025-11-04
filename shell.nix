{ pkgs ? import (builtins.fetchTarball {
    # Pinning to the NixOS 25.05 channel from GitHub.
    url = "https://github.com/NixOS/nixpkgs/archive/daf6dc47aa4b44791372d6139ab7b25269184d55.tar.gz";
    sha256 = "sha256:0ddhdypgkg4cs5zy7y5wjl62y8nrfx7xh6b95l4rkbpnl2xzn5f3";
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

  pythonPackages = pkgs.python3.pkgs;
  hdrhistogram-py-lib = pythonPackages.buildPythonPackage rec {
    pname = "hdrhistogram"; 
    version = "0.10.3";
    src = pythonPackages.fetchPypi {
      inherit pname version;
      sha256 = "sha256-84kN8KbzxYKgqLKkmlaHKcsxnxYAaD5EWMyYtoyjKEE=";
    };
    nativeBuildInputs = with pythonPackages; [
      pbr
    ];
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
    version = "1.36.0"; # A specific version/tag from GitHub
    src = pkgs.fetchFromGitHub {
      owner = "chriskohlhoff";
      repo = "asio";
      rev = "asio-1-36-0"; # The git tag or commit hash to use
      sha256 = "sha256-BhJpE5+t0WXsuQ5CtncU0P8Kf483uFoV+OGlFLc7TpQ=";
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
      htop

      # python for benchmark scripts
      (python3.withPackages (ps: [
        ps.matplotlib
        ps.plotly
        hdrhistogram-py-lib
      ]))

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
