#!/usr/bin/env bash
#
# Launch openQ4 on the Doom 3-shaped OpenGL ES 3.0 back end (BE_GLES_D3),
# through ANGLE. The renderer-gles module still compiles the ModernGL
# translation units and BE_MODERN is still what R_PickBestBackEndRenderer
# returns on an ES context, but that path is not brought up on ES in this tree
# -- `r_renderer glesd3` is how the ES context is meant to be driven here, and
# the view is rendered by src/renderer/GLES_D3/.
#
#   ./run_glesd3.sh                       plain launch, straight to the menu
#   ./run_glesd3.sh +map game/hangar1     extra args are passed straight through
#
# Expect a pause on the first frame of a level: ANGLE translates the shaders to
# Metal up front. It is far shorter than the ModernGL path's, because this back
# end has six programs rather than twenty-eight.
#
# The two runs worth comparing:
#
#   ./run_arb2.sh      what the frame should look like (retail truth)
#   ./run_vulkan.sh    the parity target -- shares this back end's gaps
#
# Renderer-comparison conveniences, same as the other launchers:
#
#   F9                     prints the current view position and yaw. Walk to a
#                          spot worth comparing, press F9, then reproduce that
#                          exact frame on any back end with
#                            RENDER_SHOT_VIEWPOS="<x> <y> <z> <pitch> <yaw> 0" \
#                            tools/debug/render_shot.sh glesd3
#   g_autoSkipCinematics   skips scripted level intros, so a level reaches a
#                          playable state without waiting on Escape.
#
# gles_d3 console knobs worth knowing while playing:
#
#   r_glesD3Report 1       per-view telemetry with an in-frame pixel readback;
#                          2 also names the first stage-draw call to raise a
#                          GL error. Cheap, and the only measurement that has
#                          proved reliable on this context.
#   r_glesD3TestTriangle 1 draws a triangle 64 units ahead of the view through
#                          the full draw path. Separates "the draw path is
#                          broken" from "the pass is not submitting".
#   r_glesD3DebugClearColor
#                          colour the 3D view clears to before any pass draws.
#                          Non-black makes "drew nothing" visually distinct
#                          from "never got the frame" -- but it also ERASES a
#                          portal sky, which renders into this same target as a
#                          subview before the main view runs. Leave it "0 0 0"
#                          except while chasing a black frame.
#   r_glesD3ShaderPath     load shaders from a directory of <program>.vert /
#                          .frag instead of the built-in sources, then
#                          `reloadGLESD3Shaders` to rebuild without a restart.
#                          The files under src/renderer/GLES_D3/glsl/ are
#                          complete programs, so they can be copied out as-is.
#   r_showShadows 2        draw the stencil shadow volumes. Mode 1 (wireframe)
#                          renders the same as 2 here: ES has no glPolygonMode.
#   r_skipInteractions 1   drop the lighting pass, leaving ambient/emissive
#                          material stages only.
#   r_skipFogLights 1      drop the whole fog/blend pass; r_skipBlendLights 1
#                          drops only blend lights. Toggling one of these is
#                          the cheapest way to see exactly what that pass
#                          contributes to a frame. game/process1 is the map to
#                          try it on -- its spawn looks INTO a fog volume from
#                          outside, which is rare.
#
# Everything printed is also teed to $OPENQ4_LOG for reading back afterwards.
# Note that the engine block-buffers stdout when it is not a tty, so the log
# fills in chunks and is only complete once the game exits.

set -eu

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$REPO_ROOT/builddir"
CLIENT="$BUILD_DIR/openQ4-client_arm64"

[ -x "$CLIENT" ] || { echo "no client binary at $CLIENT (build first: ninja -C builddir)" >&2; exit 1; }

LOG="${OPENQ4_LOG:-/tmp/openq4-glesd3.log}"
echo "logging to $LOG" >&2

# Both r_renderApi and r_renderer are CVAR_ARCHIVE, so both are passed
# explicitly every time. A value left in openQ4Config.cfg by an earlier run
# would otherwise silently redirect this launch to a different back end --
# which has already produced one false regression and one false pass in this
# work.
#
# The modern-executor cvars (r_rendererModernExecutor and friends) are
# deliberately absent: this back end does not use the executor, and setting them
# would imply a shared configuration that does not exist.
#
# `bind` and g_autoSkipCinematics are archived and so persist into
# openQ4Config.cfg after the first run. That is intentional -- they are debug
# conveniences meant to stay put -- and it is why they are set here and not in
# tools/debug/render_shot.sh, whose measurements must not depend on leftover
# config state.
"$CLIENT" \
	+set fs_basepath "$BUILD_DIR" \
	+set r_renderApi gles \
	+set r_renderer glesd3 \
	+set in_tty 0 \
	+set com_skipIntroVideos 1 \
	+set com_skipLoadingContinue 1 \
	"$@" \
	+bind F9 getviewpos \
	+set g_autoSkipCinematics 1 \
	2>&1 | tee "$LOG"

# Reported after the fact as well as on screen, because a fallback to another
# back end is quiet and a comparison against the wrong one is worse than no
# comparison. In game, `r_actualRenderer` in the console answers the same
# question: it is read-only and holds whatever selection survived fallback.
echo
echo "--- back end actually used ---"
{
	# "Active renderer path" only appears if something ran gfxInfo; the
	# "using ... renderSystem" line always does, at back-end selection.
	grep -aoE "using .*renderSystem.*" "$LOG" | head -1
	grep -aoE "Active renderer path: [A-Za-z0-9]+" "$LOG" | head -1
	grep -aoE "gles_d3 shader library: .*" "$LOG" | head -1
} | grep . || echo "could not determine from the log -- run r_actualRenderer in the console, or check $LOG"
