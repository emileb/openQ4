#!/usr/bin/env bash
#
# Launch openQ4 normally (straight to the main menu, play from there) on the
# stock desktop path: compatibility context, legacy ARB2 back end.
#
# This is the reference renderer. When something looks wrong on core 4.1 or ES,
# run the same scene here to see what it is supposed to look like.
#
#   ./run_arb2.sh                       plain launch
#   ./run_arb2.sh +map game/hangar1     extra args are passed straight through
#
# For a scripted screenshot instead of interactive play, see
# tools/debug/render_shot.sh.

set -eu

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$REPO_ROOT/builddir"
CLIENT="$BUILD_DIR/openQ4-client_arm64"

[ -x "$CLIENT" ] || { echo "no client binary at $CLIENT (build first: ninja -C builddir)" >&2; exit 1; }

# r_renderApi and r_glTier are CVAR_ARCHIVE, so they are always passed
# explicitly -- a value left in openQ4Config.cfg by an earlier run would
# otherwise silently redirect this launch to a different backend.
# Renderer-comparison conveniences:
#
#   F9                     prints the current view position and yaw. Walk to a
#                          spot worth comparing, press F9, then reproduce that
#                          exact frame on any backend with
#                            RENDER_SHOT_VIEWPOS="<x> <y> <z> <pitch> <yaw> 0" \
#                            tools/debug/render_shot.sh glesd3
#   g_autoSkipCinematics   skips scripted level intros, so a level reaches a
#                          playable state without waiting on Escape.
#
# Everything printed is also teed to $OPENQ4_LOG for reading back afterwards.
LOG="${OPENQ4_LOG:-/tmp/openq4-arb2.log}"
echo "logging to $LOG" >&2

# `bind` and g_autoSkipCinematics are archived, so they persist into
# openQ4Config.cfg after the first run. That is intentional here -- these are
# debug conveniences meant to stay put -- but it is why they are set on this
# script and not on tools/debug/render_shot.sh, whose measurements must not
# depend on leftover config state.
"$CLIENT" \
	+set fs_basepath "$BUILD_DIR" \
	+set r_renderApi gl \
	+set r_glTier auto \
	+set in_tty 0 \
	+set com_skipIntroVideos 1 \
	+set com_skipLoadingContinue 1 \
	"$@" \
	+bind F9 getviewpos \
	+set g_autoSkipCinematics 1 \
	2>&1 | tee "$LOG"
