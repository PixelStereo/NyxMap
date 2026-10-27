#!/usr/bin/env bash
# Build Pinol from source and open it: macOS, Linux (Ubuntu/Debian), and Windows from Git Bash (it runs build.ps1;
# without Git Bash, double-click build.cmd).
#
#   ./build.sh                 install what is missing, build, open Pinol
#   ./build.sh --no-install    do not install anything (the requirements are there)
#   ./build.sh --no-run        build only
#   ./build.sh --accept-touchengine-license | --no-touchengine  (macOS/Windows; see licenses/touchengine.txt)
#   ./build.sh --accept-ndi-license | --no-ndi
#                              include NDI (you accept the NDI SDK License Agreement) or not, without asking
#   ./build.sh --clean         start from an empty build folder
#   ./build.sh --debug         a Debug build
#   ./build.sh --tests         also build the tests (off by default: much slower; CI runs them)
#
# What it installs: macOS, with Homebrew: qt ffmpeg cmake pkg-config dotnet git. Ubuntu/Debian, with apt: the packages
# listed in docs/build.md. Windows, with winget: see build.ps1. OMT (built from source) needs dotnet, clang and git:
# without them Pinol is built without it.
set -euo pipefail

cd "$(dirname "$0")"
INSTALL=1 RUN=1 CLEAN=0 NDI="" TOX="" TYPE=Release TESTS=OFF
for arg in "$@"; do
  case "$arg" in
    --no-install) INSTALL=0 ;;
    --no-run) RUN=0 ;;
    --clean) CLEAN=1 ;;
    --debug) TYPE=Debug ;;
    --tests) TESTS=ON ;;
    --accept-ndi-license) NDI=ON ;;
    --no-ndi) NDI=OFF ;;
    --accept-touchengine-license) TOX=ON ;;
    --no-touchengine) TOX=OFF ;;
    -h|--help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "Unknown option: $arg (see ./build.sh --help)" >&2; exit 2 ;;
  esac
done

# Windows (Git Bash, MSYS2, Cygwin): the PowerShell script does the work, with the same options
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    exec powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$(pwd)/build.ps1")" "$@" ;;
esac

step() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
fail() { printf '\n\033[31mError: %s\033[0m\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }
# Homebrew's environment, where it installs itself (/opt/homebrew on Apple silicon, /usr/local on Intel)
brew_shellenv() {
  local b
  for b in /opt/homebrew/bin/brew /usr/local/bin/brew; do
    if [[ -x $b ]]; then eval "$("$b" shellenv)"; return; fi
  done
}

OS=$(uname -s)
CMAKE_ARGS=(-DCMAKE_BUILD_TYPE="$TYPE" -DPINOL_TESTS="$TESTS")

# --- Requirements
if [[ $OS == Darwin ]]; then
  if ! xcode-select -p >/dev/null 2>&1; then
    step "Installing Apple's command line tools (a window opens: accept, wait for the end, then run ./build.sh again)"
    xcode-select --install || true
    exit 1
  fi
  if ! have brew; then
    brew_shellenv
  fi
  if ! have brew; then
    [[ $INSTALL == 1 ]] || fail "Homebrew is needed (https://brew.sh)"
    step "Installing Homebrew (https://brew.sh): it asks for your password"
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    brew_shellenv
    have brew || fail "Homebrew is not available after its installation: open a new terminal and run ./build.sh again"
  fi
  if [[ $INSTALL == 1 ]]; then
    step "Installing the requirements with Homebrew"
    brew install qt ffmpeg cmake pkg-config dotnet git
  fi
  CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="$(brew --prefix qt)")
elif [[ $OS == Linux ]]; then
  if [[ $INSTALL == 1 ]]; then
    if have apt-get; then
      step "Installing the requirements with apt (it asks for your password)"
      SUDO=""
      [[ $(id -u) == 0 ]] || SUDO=sudo
      $SUDO apt-get update
      $SUDO apt-get install -y build-essential cmake pkg-config git qt6-base-dev libqt6opengl6-dev \
        qt6-multimedia-dev qt6-svg-dev qt6-webengine-dev libgl1-mesa-dev libavformat-dev libavcodec-dev libswscale-dev libswresample-dev \
        libavutil-dev libasound2-dev clang
      # .NET for OMT: in Ubuntu's own packages (22.04 and later); elsewhere OMT is left out
      $SUDO apt-get install -y dotnet-sdk-8.0 || echo "No dotnet-sdk-8.0 package here: Pinol is built without OMT."
    else
      echo "No apt here: install the equivalents of the packages listed in docs/build.md, then run ./build.sh --no-install."
      echo "Trying with what is installed."
    fi
  fi
else
  fail "Unsupported system $OS"
fi
have cmake || fail "cmake is not installed"
have git || fail "git is not installed"

# --- Sources: the submodules (third_party/)
if [[ -d .git ]]; then
  step "Fetching the submodules"
  git submodule update --init --recursive
elif [[ ! -f third_party/miniaudio/miniaudio.h ]]; then
  fail "third_party/ is empty: clone the repository with git (git clone --recurse-submodules https://github.com/PixelStereo/Pinol.git)"
fi

# --- NDI: only with your agreement to the NDI SDK licence
if [[ -z $NDI ]]; then
  if [[ -t 0 ]]; then
    echo
    echo "NDI (video over the network) comes from the NDI SDK, under the NDI SDK License Agreement:"
    echo "  https://ndi.video/sdk/"
    read -r -p "Do you accept it, to include NDI in Pinol? [y/N] " answer
    if [[ $answer =~ ^[YyOo] ]]; then NDI=ON; else NDI=OFF; fi
  else
    NDI=OFF
  fi
fi
CMAKE_ARGS+=(-DPINOL_BUNDLE_RUNTIMES=ON -DPINOL_ACCEPT_NDI_SDK_LICENSE="$NDI")

# TouchEngine's Shared Use License requires that the builder has accepted the TouchDesigner licence.
if [[ $OS == Darwin && -z $TOX && -t 0 ]]; then
  echo "TouchEngine SDK: licenses/touchengine.txt (https://derivative.ca/UserGuide/TouchEngine)"
  read -r -p "Have you accepted the TouchDesigner licence and do you accept the SDK terms? [y/N] " answer
  if [[ $answer =~ ^[Yy] ]]; then TOX=ON; else TOX=OFF; fi
fi
CMAKE_ARGS+=(-DPINOL_ACCEPT_TOUCHENGINE_LICENSE="${TOX:-OFF}")

# --- Build
if [[ $CLEAN == 1 ]]; then rm -rf build; fi
step "Configuring (cmake ${CMAKE_ARGS[*]})"
cmake -S . -B build "${CMAKE_ARGS[@]}"
step "Building"
JOBS=$( (have nproc && nproc) || sysctl -n hw.ncpu 2>/dev/null || echo 4)
if [[ $TESTS == ON ]]; then
  cmake --build build -j "$JOBS"
else
  cmake --build build --target Pinol -j "$JOBS"
fi

# --- Open
if [[ $OS == Darwin ]]; then
  APP=build/Pinol.app
  [[ -d $APP ]] || fail "$APP was not built"
  echo; echo "Pinol is built: $(pwd)/$APP"
  if [[ $RUN == 1 ]]; then open "$APP"; fi
else
  APP=build/Pinol
  [[ -x $APP ]] || fail "$APP was not built"
  echo; echo "Pinol is built: $(pwd)/$APP"
  if [[ $RUN == 1 ]]; then (cd build && nohup ./Pinol >/dev/null 2>&1 &); fi
fi
exit 0
