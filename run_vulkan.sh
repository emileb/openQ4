#!/usr/bin/env bash
#
# Launch openQ4 normally (straight to the main menu, play from there) on the
# native Vulkan renderer module.
#
#   ./run_vulkan.sh                       plain launch
#   ./run_vulkan.sh +map game/airdefense1 extra args are passed straight through
#
# Vulkan is the third full lighting implementation in the tree, alongside ARB2.
# Both evaluate a light the way Doom 3 does -- sampling the falloff image and
# the projection image per light -- where the modern GL/GLES path substitutes an
# analytic approximation. That makes this launcher useful as a reference when a
# scene looks wrong on ./run_glesd3.sh: see src/renderer/Vulkan/
# vk_Interactions.cpp, which mirrors RB_ARB2_DrawInteractions.
#
# r_renderApi vulkan is documented as bring-up and FALLS BACK TO GL when the
# module will not come up, silently. The line this script prints on startup is
# the only way to know which renderer actually ran -- check it before trusting
# a comparison, the same way run_glesd3.sh must not be assumed to be gles_d3.
#
# For a scripted screenshot instead of interactive play, see
# tools/debug/render_shot.sh (which has no vulkan backend yet -- add one there
# if these comparisons become routine).

set -eu

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$REPO_ROOT/builddir"
CLIENT="$BUILD_DIR/openQ4-client_arm64"

[ -x "$CLIENT" ] || { echo "no client binary at $CLIENT (build first: ninja -C builddir)" >&2; exit 1; }

# Renderer-comparison conveniences:
#
#   F9                     prints the current view position and yaw. Walk to a
#                          spot worth comparing, press F9, then reproduce that
#                          exact frame on the GL backends with
#                            RENDER_SHOT_VIEWPOS="<x> <y> <z> <pitch> <yaw> 0" \
#                            tools/debug/render_shot.sh glesd3
#   g_autoSkipCinematics   skips scripted level intros, so a level reaches a
#                          playable state without waiting on Escape.
#
# Everything printed is also teed to $OPENQ4_LOG for reading back afterwards.
LOG="${OPENQ4_LOG:-/tmp/openq4-vulkan.log}"
echo "logging to $LOG" >&2

# r_renderApi is CVAR_ARCHIVE, so it is always passed explicitly -- a value left
# in openQ4Config.cfg by an earlier run would otherwise silently redirect this
# launch to desktop GL, which is exactly the failure this script is meant to
# help diagnose.
"$CLIENT" \
	+set fs_basepath "$BUILD_DIR" \
	+set r_renderApi vulkan \
	+set in_tty 0 \
	+set com_skipIntroVideos 1 \
	+set com_skipLoadingContinue 1 \
	"$@" \
	+bind F9 getviewpos \
	+set g_autoSkipCinematics 1 \
	2>&1 | tee "$LOG"

# Reported after the fact as well as on screen, because the fallback to GL is
# quiet and a comparison against the wrong renderer is worse than no comparison.
# In game, `r_actualRenderApi` in the console answers the same question: it is a
# read-only cvar holding the API left active after request/fallback selection.
echo
echo "--- renderer actually used ---"
{
	grep -aoE "Active renderer path: [A-Za-z0-9]+" "$LOG" | head -1
	grep -aoE "r_actualRenderApi[^\"]*\"[A-Za-z0-9-]+\"" "$LOG" | head -1
	grep -aiE "vulkan.*(fall(ing|s|back)|unavailable|failed|not available)" "$LOG" | head -2
} | grep . || echo "could not determine from the log -- run r_actualRenderApi in the console, or check $LOG"
