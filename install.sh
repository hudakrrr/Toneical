#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# =============================================================================
#  Toneical installer
#
#  Builds Toneical from this source folder and installs it into ~/Toneical:
#
#    ~/Toneical/
#      bin/Toneical            the standalone app
#      bin/toneical            launcher (starts it through PipeWire/JACK, 48 kHz)
#      plugins/Toneical.vst3   VST3 plugin   (linked into ~/.vst3)
#      plugins/Toneical.lv2    LV2 plugin    (linked into ~/.lv2)
#      Factory/factory.json    the factory presets (updated without recompiling)
#      Presets/                your saved boards (.toneical files)
#      Captures/               put your .nam captures here
#      IRs/                    put your cabinet impulse responses here
#      icon/                   app icon
#      uninstall.sh
#
#  plus an app-menu entry with the pedal icon and a `toneical` command.
#
#  UPDATES: just run the new version's ./install.sh. The build lives in
#  ~/.cache/toneical (kept between versions), only source files that really changed
#  are recompiled, and if no compiled file changed at all (new presets, docs, icons)
#  nothing is rebuilt - the update takes seconds.
#
#  Usage:  ./install.sh [options]
#    --dir PATH        install somewhere else than ~/Toneical
#    --no-deps         don't check/install system packages
#    --skip-build      install the existing build without rebuilding
#    --rebuild         build even if no source file changed
#    --clean           throw away the cached build and start from scratch
#    --generic-cpu     build without -march=native (portable binary)
#    --logo-font FILE  use your own font for the logo (e.g. a licensed Cattalague .ttf/.otf)
#    --jobs N          parallel build jobs (default: number of CPU cores)
#    --test            also build and run the offline render test
#    -y, --yes         answer "yes" to every question
#    --uninstall       remove Toneical (same as running ~/Toneical/uninstall.sh)
#    -h, --help
# =============================================================================
set -Eeuo pipefail

APP="Toneical"
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CACHE="${TONEICAL_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/toneical}"
MIRROR="$CACHE/src"          # stable copy of the sources: unchanged files keep their timestamps
BUILD="$CACHE/build"
FORCE_BUILD=0
CLEAN=0
DEST="$HOME/Toneical"
WITH_DEPS=1
DO_BUILD=1
NATIVE=ON
LOGO_FONT=""
JOBS="$(nproc 2>/dev/null || echo 2)"
RUN_TEST=0
ASSUME_YES=0
UNINSTALL=0

# ---------------------------------------------------------------- output helpers
if [[ -t 1 ]]; then
    B=$'\e[1m'; R=$'\e[31m'; G=$'\e[32m'; Y=$'\e[33m'; C=$'\e[36m'; N=$'\e[0m'
else
    B=""; R=""; G=""; Y=""; C=""; N=""
fi
step() { printf '\n%s==>%s %s%s%s\n' "$C" "$N" "$B" "$*" "$N"; }
ok()   { printf '   %s✔%s %s\n' "$G" "$N" "$*"; }
warn() { printf '   %s!%s %s\n' "$Y" "$N" "$*"; }
die()  { printf '\n%s✘ %s%s\n' "$R" "$*" "$N" >&2; exit 1; }
trap 'die "Installation failed on line $LINENO (command: $BASH_COMMAND)"' ERR

ask() { # ask "question" -> 0 for yes
    [[ $ASSUME_YES == 1 ]] && return 0
    local reply
    read -r -p "   $1 [Y/n] " reply || reply=""
    [[ -z "$reply" || "$reply" =~ ^[YyIi] ]]
}

usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; exit 0; }

# ---------------------------------------------------------------- arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --dir)         DEST="$(realpath -m "$2")"; shift ;;
        --no-deps)     WITH_DEPS=0 ;;
        --skip-build)  DO_BUILD=0 ;;
        --rebuild)     FORCE_BUILD=1 ;;
        --clean)       CLEAN=1 ;;
        --generic-cpu) NATIVE=OFF ;;
        --logo-font)   LOGO_FONT="$(realpath "$2")"; shift ;;
        --jobs)        JOBS="$2"; shift ;;
        --test)        RUN_TEST=1 ;;
        -y|--yes)      ASSUME_YES=1 ;;
        --uninstall)   UNINSTALL=1 ;;
        -h|--help)     usage ;;
        *) die "Unknown option: $1 (see --help)" ;;
    esac
    shift
done

[[ $EUID -eq 0 && -z "${TONEICAL_ALLOW_ROOT:-}" ]] && die "Run this as your normal user, not root/sudo - it installs into your home folder. (It asks for sudo itself when it needs to install packages.)"

if [[ $UNINSTALL == 1 ]]; then
    [[ -x "$DEST/uninstall.sh" ]] || die "No installation found in $DEST"
    exec "$DEST/uninstall.sh"
fi

printf '%s\n' "${B}Toneical installer${N}  ->  ${DEST}"

# ---------------------------------------------------------------- 1. dependencies
install_deps() {
    local pm="" missing=() pkgs=() check
    if command -v dnf >/dev/null; then
        pm=dnf
        pkgs=(gcc-c++ cmake ninja-build ccache git pkgconf-pkg-config alsa-lib-devel
              pipewire-jack-audio-connection-kit-devel pipewire-jack-audio-connection-kit
              freetype-devel fontconfig-devel libX11-devel libXrandr-devel libXinerama-devel
              libXcursor-devel libXext-devel libXcomposite-devel libXrender-devel libXi-devel)
        check() { rpm -q "$1" >/dev/null 2>&1; }
    elif command -v apt-get >/dev/null; then
        pm=apt
        pkgs=(build-essential cmake ninja-build ccache git pkg-config libasound2-dev libjack-jackd2-dev
              libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev
              libxcursor-dev libxext-dev libxcomposite-dev libxrender-dev libxi-dev)
        check() { dpkg -s "$1" >/dev/null 2>&1; }
    elif command -v pacman >/dev/null; then
        pm=pacman
        pkgs=(base-devel cmake ninja ccache git alsa-lib pipewire-jack freetype2 fontconfig libx11
              libxrandr libxinerama libxcursor libxext libxcomposite libxrender libxi)
        check() { pacman -Qi "$1" >/dev/null 2>&1; }
    else
        warn "Unknown package manager - make sure a C++20 compiler, CMake, Ninja, git and the ALSA/JACK/X11/freetype dev packages are installed."
        return
    fi

    for p in "${pkgs[@]}"; do check "$p" || missing+=("$p"); done
    if [[ ${#missing[@]} -eq 0 ]]; then
        ok "all build dependencies present"
        return
    fi

    warn "missing packages: ${missing[*]}"
    ask "Install them now with sudo?" || die "Cannot build without them. Install them yourself, or re-run with --no-deps if you know better."
    case "$pm" in
        dnf)    sudo dnf install -y "${missing[@]}" ;;
        apt)    sudo apt-get update && sudo apt-get install -y "${missing[@]}" ;;
        pacman) sudo pacman -S --needed --noconfirm "${missing[@]}" ;;
    esac
    ok "dependencies installed"
}

if [[ $WITH_DEPS == 1 ]]; then
    step "Checking build dependencies"
    install_deps
fi

# ---------------------------------------------------------------- 2. build
# Sources are mirrored into $MIRROR: a file is only copied when its content changed, so
# the compiler only redoes what an update really touched. Files that are just data
# (presets, docs, icons, README) are installed directly and never trigger a build.
COMPILED=(CMakeLists.txt Source tests assets presets)
HASHED=(CMakeLists.txt Source tests assets/fonts assets/irs assets/nam assets/icon)

sync_sources() { # copy changed files, delete files that disappeared; prints the number changed
    local n=0 rel f
    mkdir -p "$MIRROR"
    while IFS= read -r -d '' f; do
        rel="${f#"$SRC"/}"
        if ! cmp -s "$f" "$MIRROR/$rel"; then
            mkdir -p "$(dirname "$MIRROR/$rel")"
            cp "$f" "$MIRROR/$rel"
            n=$((n + 1))
        fi
    done < <(cd "$SRC" && find "${COMPILED[@]/#/$SRC/}" -type f -print0 2>/dev/null || true)
    while IFS= read -r -d '' f; do
        rel="${f#"$MIRROR"/}"
        [[ -e "$SRC/$rel" ]] || { rm -f "$f"; n=$((n + 1)); }
    done < <(find "$MIRROR" -type f -print0)
    echo "$n"
}

inputs_hash() { # fingerprint of everything that ends up in the binaries
    {
        (cd "$MIRROR" && find "${HASHED[@]}" -type f -print0 | sort -z | xargs -0 sha256sum)
        printf '%s\n' "${cmake_args[@]}"
        if [[ -n "$LOGO_FONT" ]]; then sha256sum "$LOGO_FONT"; fi
    } | sha256sum | cut -d' ' -f1
}

ART="$BUILD/${APP}_artefacts/Release"
if [[ $DO_BUILD == 1 ]]; then
    command -v cmake >/dev/null || die "cmake not found"
    if [[ $CLEAN == 1 ]]; then rm -rf "$CACHE"; ok "cached build removed"; fi
    step "Preparing sources"
    changed="$(sync_sources)"
    ok "$changed file(s) new or changed since the last build"

    generator=()
    command -v ninja >/dev/null && generator=(-G Ninja)
    if [[ -f "$BUILD/CMakeCache.txt" ]] && ! grep -q "CMAKE_GENERATOR:INTERNAL=${generator[1]:-Unix Makefiles}" "$BUILD/CMakeCache.txt"; then
        rm -rf "$BUILD"   # generator changed since the last build
    fi
    cmake_args=(-S "$MIRROR" -B "$BUILD" "${generator[@]}" -DCMAKE_BUILD_TYPE=Release -DTONEICAL_NATIVE="$NATIVE")
    [[ -n "$LOGO_FONT" ]] && cmake_args+=(-DTONEICAL_LOGO_FONT="$LOGO_FONT")
    # extra arguments for unusual setups, e.g. TONEICAL_CMAKE_ARGS="-DTONEICAL_JUCE_DIR=/src/JUCE"
    if [[ -n "${TONEICAL_CMAKE_ARGS:-}" ]]; then read -r -a extra <<< "$TONEICAL_CMAKE_ARGS"; cmake_args+=("${extra[@]}"); fi

    hash="$(inputs_hash)"
    if [[ $FORCE_BUILD == 0 && $RUN_TEST == 0 && -x "$ART/Standalone/$APP" && -f "$BUILD/.inputs.sha256" \
          && "$(cat "$BUILD/.inputs.sha256")" == "$hash" ]]; then
        ok "no compiled file changed - skipping the build (presets/docs/icons are updated directly)"
    else
        rm -f "$BUILD/.inputs.sha256"
        if [[ -f "$BUILD/CMakeCache.txt" ]]; then
            step "Updating the build configuration"
        else
            step "Configuring (first run downloads JUCE, Eigen and NeuralAmpModelerCore)"
        fi
        mkdir -p "$CACHE"
        cmake "${cmake_args[@]}" > "$CACHE/configure.log" 2>&1 || { tail -30 "$CACHE/configure.log"; die "Configure failed (full log: $CACHE/configure.log)"; }
        ok "configured"

        if [[ -x "$ART/Standalone/$APP" ]]; then
            step "Recompiling what changed ($JOBS jobs)"
        else
            step "Building with $JOBS jobs (the first build takes several minutes; updates are much faster)"
        fi
        command -v ccache >/dev/null || warn "tip: install ccache to make rebuilds even faster"
        targets=(--target Toneical_Standalone Toneical_VST3 Toneical_LV2)
        [[ $RUN_TEST == 1 ]] && targets+=(ToneicalRenderTest)
        start=$SECONDS
        if ! cmake --build "$BUILD" "${targets[@]}" -j "$JOBS" > "$CACHE/build.log" 2>&1; then
            grep -E "error|Error" "$CACHE/build.log" | head -20 || true
            die "Build failed (full log: $CACHE/build.log)"
        fi
        echo "$hash" > "$BUILD/.inputs.sha256"
        ok "built in $((SECONDS - start)) s"

        if [[ $RUN_TEST == 1 ]]; then
            step "Running the offline render test"
            "$BUILD/ToneicalRenderTest_artefacts/Release/ToneicalRenderTest" "$BUILD/render-test" | tail -5
        fi
    fi
fi

[[ -x "$ART/Standalone/$APP" ]] || die "No build found in $ART - run without --skip-build"

# ---------------------------------------------------------------- 3. copy into ~/Toneical
step "Installing into $DEST"
mkdir -p "$DEST"/{bin,plugins,Factory,Presets,Captures,IRs,icon}

if ! cmp -s "$ART/Standalone/$APP" "$DEST/bin/$APP"; then
    install -m755 "$ART/Standalone/$APP" "$DEST/bin/$APP"
fi
install -m644 "$SRC/presets/factory.json" "$DEST/Factory/factory.json"
for fmt in vst3 lv2; do
    dir="$ART/${fmt^^}/$APP.$fmt"
    if [[ -d "$dir" ]]; then
        rm -rf "$DEST/plugins/$APP.$fmt"
        cp -r "$dir" "$DEST/plugins/"
    else
        warn "no ${fmt^^} plugin in the build - skipped"
    fi
done
rm -f "$DEST"/icon/toneical*.svg   # old knob icon from earlier versions
cp "$SRC"/assets/icon/toneical*.png "$DEST/icon/"
cp "$SRC/README.md" "$DEST/README.md"
mkdir -p "$DEST/licenses"
cp "$SRC/assets/irs/README.txt" "$DEST/licenses/cabinet-IRs-README.txt"
cp "$SRC/assets/irs/LICENSE-BestPlugins.txt" "$SRC/assets/irs/GPL-2.txt" "$DEST/licenses/"
cp "$SRC/assets/fonts/Arizonia-OFL.txt" "$DEST/licenses/"
cp "$SRC/LICENSE" "$DEST/licenses/Toneical-AGPL-3.0.txt"
cp "$SRC/THIRD_PARTY.md" "$DEST/licenses/"
cp "$SRC/assets/nam/README.txt" "$DEST/licenses/real-amp-captures-README.txt"
cp "$SRC/assets/nam/COPYING-GPL-3.txt" "$DEST/licenses/"
cp "$SRC/assets/pedals/README.txt" "$DEST/licenses/captured-pedals-README.txt"
cp "$SRC/assets/pedals/LICENSE-ToneLibrary-GPL-3.txt" "$DEST/licenses/"
[[ -d "$SRC/docs" ]] && cp -r "$SRC/docs" "$DEST/"
if [[ -f "$SRC/VERSION" ]]; then cp "$SRC/VERSION" "$DEST/VERSION"; else date +%Y-%m-%d > "$DEST/VERSION"; fi

cat > "$DEST/Captures/README.txt" <<'EOF'
Put your Neural Amp Modeler captures (.nam) here - the NAM Capture pedal opens this folder.
Free captures: https://www.tone3000.com  ("amp head" captures need a cab IR after them).
EOF
cat > "$DEST/IRs/README.txt" <<'EOF'
Put your cabinet / room impulse responses (.wav) here - the IR Loader pedal opens this folder.
Free IRs: https://www.tone3000.com
EOF
ok "app, plugins and folders copied"

# launcher: PipeWire/JACK at 48 kHz with a 64-sample buffer (toneical 16 / 32 / 128 ...)
cat > "$DEST/bin/toneical" <<EOF
#!/usr/bin/env bash
# Toneical launcher.  toneical [buffer-size] [--preset "Hot Lead"]
#   buffer-size in samples at 48 kHz: 16 = 0.33 ms, 32 = 0.67 ms, 64 = 1.3 ms (default), 128 = 2.7 ms
QUANTUM=64
if [[ "\${1:-}" =~ ^[0-9]+\$ ]]; then QUANTUM="\$1"; shift; fi
export TONEICAL_HOME="$DEST"
BIN="$DEST/bin/$APP"
if command -v pw-jack >/dev/null; then
    export PIPEWIRE_LATENCY="\${QUANTUM}/48000"   # M-Track Solo + NAM captures: 48 kHz
    # PipeWire normally won't go below 32 samples: lower its minimum while Toneical runs
    OLDMIN=""
    if command -v pw-metadata >/dev/null; then
        OLDMIN="\$(pw-metadata -n settings 0 2>/dev/null | sed -n "s/.*'clock.min-quantum' value:'\([0-9]*\)'.*/\1/p" | head -1)"
        if [[ -n "\$OLDMIN" && "\$QUANTUM" -lt "\$OLDMIN" ]]; then
            pw-metadata -n settings 0 clock.min-quantum "\$QUANTUM" >/dev/null
        else
            OLDMIN=""
        fi
        pw-metadata -n settings 0 clock.force-rate 48000 >/dev/null
    fi
    pw-jack "\$BIN" "\$@"; rc=\$?
    if command -v pw-metadata >/dev/null; then
        [[ -n "\$OLDMIN" ]] && pw-metadata -n settings 0 clock.min-quantum "\$OLDMIN" >/dev/null
        pw-metadata -n settings 0 clock.force-quantum 0 >/dev/null
        pw-metadata -n settings 0 clock.force-rate 0 >/dev/null
    fi
    exit \$rc
fi
exec "\$BIN" "\$@"
EOF
chmod +x "$DEST/bin/toneical"

# ---------------------------------------------------------------- 4. system integration
step "Adding plugins, command and app-menu entry"
link() { # link TARGET LINKNAME (replaces old links/copies of the same name)
    mkdir -p "$(dirname "$2")"
    [[ -e "$2" || -L "$2" ]] && rm -rf "$2"
    ln -s "$1" "$2"
}
[[ -d "$DEST/plugins/$APP.vst3" ]] && link "$DEST/plugins/$APP.vst3" "$HOME/.vst3/$APP.vst3" && ok "VST3  -> ~/.vst3/$APP.vst3"
[[ -d "$DEST/plugins/$APP.lv2"  ]] && link "$DEST/plugins/$APP.lv2"  "$HOME/.lv2/$APP.lv2"   && ok "LV2   -> ~/.lv2/$APP.lv2"
link "$DEST/bin/toneical" "$HOME/.local/bin/toneical" && ok "command: toneical"
[[ ":$PATH:" == *":$HOME/.local/bin:"* ]] || warn "~/.local/bin is not in your PATH - log out and in again (Fedora adds it automatically)"

ICONS="$HOME/.local/share/icons/hicolor"
for s in 16 24 32 48 64 128 256 512; do
    mkdir -p "$ICONS/${s}x${s}/apps"
    cp "$DEST/icon/toneical-$s.png" "$ICONS/${s}x${s}/apps/toneical.png"
done
# an older version installed a scalable SVG knob icon, which desktops prefer over PNGs - remove it
rm -f "$ICONS/scalable/apps/toneical.svg"
command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -q -t -f "$ICONS" 2>/dev/null || true

APPS="$HOME/.local/share/applications"
mkdir -p "$APPS"
cat > "$APPS/toneical.desktop" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=Toneical
GenericName=Guitar Amp & Effects
Comment=Guitar rig: pedals, amps, cabinets and Neural Amp Modeler captures
Exec=$DEST/bin/toneical %F
Icon=toneical
Terminal=false
Categories=AudioVideo;Audio;Music;
Keywords=guitar;amp;pedal;effects;NAM;tuner;
StartupWMClass=$APP
EOF
command -v update-desktop-database >/dev/null && update-desktop-database -q "$APPS" 2>/dev/null || true
ok "app-menu entry with pedal icon"

# ---------------------------------------------------------------- 5. uninstaller
cat > "$DEST/uninstall.sh" <<EOF
#!/usr/bin/env bash
# Removes Toneical. Your presets, captures and IRs are kept unless you answer yes.
set -u
rm -f  "$HOME/.local/bin/toneical" "$APPS/toneical.desktop"
for s in 16 24 32 48 64 128 256 512; do rm -f "$ICONS/\${s}x\${s}/apps/toneical.png"; done
rm -f  "$ICONS/scalable/apps/toneical.svg"
[[ -L "$HOME/.vst3/$APP.vst3" ]] && rm -f "$HOME/.vst3/$APP.vst3"
[[ -L "$HOME/.lv2/$APP.lv2"  ]] && rm -f "$HOME/.lv2/$APP.lv2"
command -v update-desktop-database >/dev/null && update-desktop-database -q "$APPS" 2>/dev/null
rm -rf "$DEST/bin" "$DEST/plugins" "$DEST/Factory" "$DEST/icon" "$DEST/docs" "$DEST/licenses" "$DEST/README.md" "$DEST/VERSION"
rm -rf "$CACHE"   # cached build
echo "Toneical removed."
read -r -p "Also delete your presets, captures and IRs in $DEST? [y/N] " a
if [[ "\$a" =~ ^[YyIi] ]]; then rm -rf "$DEST"; echo "Deleted $DEST"; else rm -f "$DEST/uninstall.sh"; echo "Kept your files in $DEST"; fi
EOF
chmod +x "$DEST/uninstall.sh"
ok "uninstaller: $DEST/uninstall.sh"

# ---------------------------------------------------------------- done
cat <<EOF

${G}${B}Toneical is installed.${N}

  Start it:        app menu -> ${B}Toneical${N}   or   ${B}toneical${N}  (toneical 32 / toneical 16 = even lower latency)
  Your files:      $DEST  (Presets, Captures for .nam, IRs for cab .wav files)
  Plugins:         VST3 + LV2 - rescan in your DAW
  Remove:          $DEST/uninstall.sh

  First run: click ${B}Audio...${N}, choose JACK, 48000 Hz, enable the inputs,
  pick the input your guitar is on (the meter moves), then click ${B}Auto${N}
  next to Input Gain and play hard for 6 seconds to calibrate the level.
EOF
