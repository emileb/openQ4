#!/usr/bin/env bash
#
# render_shot.sh -- load a real level on a chosen renderer, wait a fixed
# wall-clock interval into gameplay, and capture one screenshot.
#
#   tools/debug/render_shot.sh arb2|glesd3|vulkan [name-suffix]
#
#   arb2    stock desktop compatibility context, legacy ARB2 -- what the frame
#           should look like
#   glesd3  OpenGL ES 3.0 through ANGLE, Doom 3-shaped backend (BE_GLES_D3)
#   vulkan  native Vulkan module -- the parity target for glesd3
#
# The two comparisons answer different questions: arb2 is retail truth, vulkan
# is what a modern backend in this tree has actually achieved. vulkan shares
# gles_d3's absences (no bloom/SSAO/motion blur/cel/light grid), so a
# difference against it is a real gap rather than a known one.
#
# Why the pieces are the way they are:
#
#   * com_skipLoadingContinue 1 removes the "press any key" gate after the load
#     completes; without it every run blocks forever on a headless machine.
#   * waitMsec, not wait. wait counts frames, and the three renderers do not
#     run at comparable frame rates (ES spends minutes in ANGLE shader
#     translation), so a frame count captures a different moment on each one.
#     waitMsec is real time, so all three are sampled at the same point.
#   * r_renderApi is always passed explicitly. It is CVAR_ARCHIVE, so a value
#     left in the config silently redirects which backend a run exercises --
#     that has already produced one false "regression" and one false "pass".
#   * The archived config is saved and restored around the run, so testing does
#     not change the renderer a later manual launch picks.

set -u

usage() {
	echo "usage: $(basename "$0") arb2|glesd3|vulkan [name-suffix]" >&2
	exit 2
}

[ $# -ge 1 ] || usage

BACKEND="$1"
SUFFIX="${2:-}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$REPO_ROOT/builddir"
CLIENT="$BUILD_DIR/openQ4-client_arm64"
MOD_DIR="$BUILD_DIR/baseoq4"
SAVE_DIR="$HOME/Library/Application Support/openQ4/baseoq4"
CONFIG="$SAVE_DIR/openQ4Config.cfg"

[ -x "$CLIENT" ] || { echo "no client binary at $CLIENT (build first)" >&2; exit 1; }

# hangar1 by default: the spawn faces depth, coloured lights, a lit GUI panel
# and both near and far surfaces, so most lighting faults are visible in one
# frame. Loading a level directly keeps the sample deterministic -- same spawn,
# same time offset, every run. Override to compare a different scene:
#   RENDER_SHOT_MAP=game/mcc_landing tools/debug/render_shot.sh arb2
MAP="${RENDER_SHOT_MAP:-game/hangar1}"
SHOT_MSEC="${RENDER_SHOT_MSEC:-2000}"

# RENDER_SHOT_VIEWPOS makes a capture reproducible on levels that open with a
# scripted cinematic, where a fixed waitMsec lands on a different camera every
# run depending on load time. Find a spot by playing with ./run_glesd3.sh and
# pressing F9 (bound to getviewpos), then paste what it prints:
#
#   RENDER_SHOT_VIEWPOS="512 -1344 72 10 90 0" \
#   RENDER_SHOT_MAP=game/airdefense1 tools/debug/render_shot.sh glesd3 fire
#
# Accepts "x y z", "x y z yaw", or "x y z pitch yaw roll" -- the same forms
# setviewpos itself takes. RENDER_SHOT_SETTLE is the pause after the teleport
# before the shot, which particle effects need to build up to a steady state.
VIEWPOS="${RENDER_SHOT_VIEWPOS:-}"
SETTLE_MSEC="${RENDER_SHOT_SETTLE:-1500}"

# RENDER_SHOT_PRECMDS runs console commands once the level is up but BEFORE the
# teleport, e.g. RENDER_SHOT_PRECMDS="noclip;god".
#
# noclip is what makes an arbitrary RENDER_SHOT_VIEWPOS trustworthy. setviewpos
# moves the player ENTITY, not just the camera, so a coordinate chosen to frame
# some piece of geometry -- a fog volume, a light, a model -- routinely drops
# the player through a floor or into a pit. They then fall for the whole
# RENDER_SHOT_SETTLE and the shot captures wherever they landed, or the death
# screen. Both look like a renderer fault and are not one: it cost two runs and
# a wrong reading on the fog comparison before this existed.
PRECMDS="${RENDER_SHOT_PRECMDS:-}"

case "$BACKEND" in
	arb2)
		# Stock desktop path: compatibility context, legacy ARB2 back end.
		BACKEND_ARGS=( +set r_renderApi gl +set r_glTier auto )
		;;
	glesd3)
		# The Doom 3-shaped ES backend. No env vars are needed for the context;
		# the engine resolves the staged ANGLE from the already-loaded dyld
		# image. r_renderer glesd3 selects BE_GLES_D3, so the view is rendered
		# by src/renderer/GLES_D3/ rather than by the ModernGL executor. The
		# modern executor cvars are deliberately NOT passed -- this backend does
		# not use the executor, and setting them would suggest a shared
		# configuration that does not exist.
		BACKEND_ARGS=( +set r_renderApi gles +set r_renderer glesd3 )
		;;
	vulkan)
		# The native Vulkan renderer module (MoltenVK on this Mac). This is the
		# parity target for gles_d3: unlike arb2 it shares this backend's
		# absences (no bloom, SSAO, motion blur, cel or light grid), so
		# a difference against it is a real gap rather than a known one.
		BACKEND_ARGS=( +set r_renderApi vulkan )
		;;
	*)
		usage
		;;
esac

SHOT_NAME="shot_${BACKEND}${SUFFIX:+_$SUFFIX}"
CFG_NAME="render_shot.cfg"

mkdir -p "$MOD_DIR"
{
	echo "// generated by tools/debug/render_shot.sh -- safe to overwrite"
	echo "map $MAP"
	echo "waitMsec $SHOT_MSEC"
	# before the teleport: the level exists, the player has not been moved yet
	if [ -n "$PRECMDS" ]; then
		echo "${PRECMDS//;/$'\n'}"
	fi
	if [ -n "$VIEWPOS" ]; then
		echo "setviewpos $VIEWPOS"
		echo "waitMsec $SETTLE_MSEC"
	fi
	# RENDER_SHOT_CMDS runs console commands at the sample point, once the view
	# is in place, e.g. RENDER_SHOT_CMDS="r_glesD3Report 1;gfxInfo". Diagnostics
	# that report on the frame have to run here, not at startup, or they describe
	# the loading screen instead of the scene.
	if [ -n "${RENDER_SHOT_CMDS:-}" ]; then
		echo "${RENDER_SHOT_CMDS//;/$'\n'}"
	fi
	echo "screenshot $SHOT_NAME"
	echo "waitMsec 250"
	echo "echo \"@@@ RENDER_SHOT DONE @@@\""
	echo "quit"
} > "$MOD_DIR/$CFG_NAME"

# Preserve the player's archived renderer selection across the test run.
CONFIG_BACKUP=""
if [ -f "$CONFIG" ]; then
	CONFIG_BACKUP="$(mktemp -t openq4cfg)"
	cp "$CONFIG" "$CONFIG_BACKUP"
fi
restore_config() {
	if [ -n "$CONFIG_BACKUP" ] && [ -f "$CONFIG_BACKUP" ]; then
		cp "$CONFIG_BACKUP" "$CONFIG"
		rm -f "$CONFIG_BACKUP"
	elif [ -z "$CONFIG_BACKUP" ]; then
		# There was no config before this run, so the archived cvars this
		# script passes (g_autoSkipCinematics among them) would otherwise be
		# left behind in a config the player never had, and would silently
		# apply to every later launch.
		rm -f "$CONFIG"
	fi
}
trap restore_config EXIT

LOG="$(mktemp -t openq4shot)"
rm -f "$SAVE_DIR/$SHOT_NAME" 2>/dev/null

# Extra cvars for A/B runs, e.g. RENDER_SHOT_SET="r_useLightGrid 0 r_skipFogLights 1".
# Given as bare "name value" pairs; each pair becomes a +set. These go on the
# command line rather than into a cfg so they cannot persist into openQ4Config.cfg.
EXTRA_ARGS=()
if [[ -n "${RENDER_SHOT_SET:-}" ]]; then
	read -ra EXTRA_WORDS <<< "$RENDER_SHOT_SET"
	if (( ${#EXTRA_WORDS[@]} % 2 != 0 )); then
		echo "RENDER_SHOT_SET needs an even number of words (name value ...)" >&2
		exit 1
	fi
	for (( i = 0; i < ${#EXTRA_WORDS[@]}; i += 2 )); do
		EXTRA_ARGS+=( +set "${EXTRA_WORDS[i]}" "${EXTRA_WORDS[i+1]}" )
	done
	echo "extra cvars    : $RENDER_SHOT_SET"
fi

"$CLIENT" \
	+set fs_basepath "$BUILD_DIR" \
	+set in_tty 0 \
	+set r_fullscreen 0 \
	+set com_skipIntroVideos 1 \
	+set com_skipLoadingContinue 1 \
	+set g_autoSkipCinematics 1 \
	"${BACKEND_ARGS[@]}" \
	"${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}" \
	+exec "$CFG_NAME" \
	> "$LOG" 2>&1
RUN_STATUS=$?

echo "backend        : $BACKEND"
echo "map            : $MAP"
echo "exit status    : $RUN_STATUS"
grep -oE "Active renderer path: [A-Za-z0-9]+" "$LOG" | head -1
grep -oE "reported OpenGL context attributes: version=[0-9.]+ profile=[a-z]+" "$LOG" | head -1

if ! grep -q "RENDER_SHOT DONE" "$LOG"; then
	echo "FAILED: the run did not reach the screenshot" >&2
	echo "--- last 20 log lines ---" >&2
	tail -20 "$LOG" >&2
	echo "full log: $LOG" >&2
	exit 1
fi

SHOT_TGA="$SAVE_DIR/$SHOT_NAME"
if [ ! -f "$SHOT_TGA" ]; then
	echo "FAILED: no screenshot written at $SHOT_TGA" >&2
	echo "full log: $LOG" >&2
	exit 1
fi

OUT_DIR="$REPO_ROOT/.tmp/render-shots"
mkdir -p "$OUT_DIR"
OUT_PNG="$OUT_DIR/$SHOT_NAME.png"
if sips -s format png "$SHOT_TGA" --out "$OUT_PNG" >/dev/null 2>&1; then
	echo "screenshot     : $OUT_PNG"
else
	echo "screenshot     : $SHOT_TGA (png conversion failed)"
fi
echo "log            : $LOG"
