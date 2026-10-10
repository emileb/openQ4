#!/usr/bin/env bash
#
# See what the gles_d3 visual-parity work changes, on this Mac through ANGLE.
#
# Same back end as ./run_glesd3.sh (r_renderApi gles, r_renderer glesd3), plus
# a switch for each feature so the before and after can be compared on the
# same frame:
#
#   feature                        cvar                  live key
#   -----------------------------  --------------------  --------
#   Raven GLSL material programs   r_glesD3GLSLPrograms  F4
#     bullet holes, fluid tanks, blood pools, fuzz shields, scope blur
#   weapon / model depth hack      r_glesD3DepthHack     F8
#     the view weapon no longer sinks into walls and floors
#   brightness (no hardware gamma  r_brightness          F10  cycles 1 / 1.25 / 1.5
#     ramp on ES, so this is now a shader pass)
#   gamma                          r_gamma               F11  cycles 1 / 1.2 / 1.4
#   shadow maps (soft, filtered)   r_useShadowMap        O
#     instead of the hard-edged stencil shadows; lights the maps cannot serve
#     keep stencil, so turning this on never removes a shadow
#
# Two optional looks, off in both presets because neither is stock Quake 4 --
# they match what diii4a (idTech4A++) shows by default:
#
#   wet specular (broad Phong)     r_glesD3SpecularModel P
#     highlights spread across lit surfaces; r_glesD3SpecularExponent sets
#     how wet (3 = diii4a, lower = wetter)
#   fill light                     r_glesD3FillLight     L
#     a fixed-direction light on every surface, so dark corners stay
#     visible; r_glesD3FillLightBrightness sets it (0.3 = diii4a)
#
# Decals are not on a switch: the material pass used to drop every projected
# decal outright, and that was a bug, not a look.
#
# Usage:
#
#   ./run_gles.sh                       everything on, straight to the menu
#   ./run_gles.sh --old                 start with the new features off
#   ./run_gles.sh +map game/core1       extra args are passed through
#   ./run_gles.sh --ab <scene>          capture an old/new screenshot pair
#   ./run_gles.sh --ab all              ... for every scene
#   ./run_gles.sh --list                list the --ab scenes
#
# The live keys print the new value in the console notify area (top left).
# Like run_glesd3.sh's F9, the binds are archived into openQ4Config.cfg and
# stay after the run; F9 prints the view position for reproducing a frame.
#
# Where to look in game:
#
#   game/core1     the orange fluid tanks just past the start (Displacement)
#   game/waste     the blood pools on the floors (DisplacementCube)
#   any map        shoot a wall with the machine gun: bullet holes
#                  (MultiplyBlend); zoom it (right mouse) for the scope blur
#                  (SniperStretch2)
#   any map        walk into a wall and look down: the gun stays in front
#
# --ab writes .tmp/render-shots/shot_glesd3_ab_<scene>_{old,new}.png and opens
# both. "old" means only that the switches above are off -- the decal fix and
# every earlier gles_d3 change are still in. The look scenes (wet, fill)
# compare against the current renderer instead, so only the look changes.

set -eu

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$REPO_ROOT/builddir"
CLIENT="$BUILD_DIR/openQ4-client_arm64"
SHOT_DIR="$REPO_ROOT/.tmp/render-shots"

[ -x "$CLIENT" ] || { echo "no client binary at $CLIENT (build first: ninja -C builddir)" >&2; exit 1; }

# the looks are pinned off in both: they are archived, and a value left in the
# config would otherwise leak into every comparison
NEW_CVARS=( r_glesD3GLSLPrograms 1 r_glesD3DepthHack 1 r_brightness 1 r_gamma 1 r_useShadowMap 1
	r_glesD3SpecularModel 0 r_glesD3FillLight 0 )
OLD_CVARS=( r_glesD3GLSLPrograms 0 r_glesD3DepthHack 0 r_brightness 1 r_gamma 1 r_useShadowMap 0
	r_glesD3SpecularModel 0 r_glesD3FillLight 0 )

# --ab scenes: map | setviewpos | console commands before the teleport |
# settle msec | cvars for the "new" shot on top of NEW_CVARS | what "old" is
# ("new" = the current renderer, for the looks; empty = OLD_CVARS). Each view
# was chosen while verifying the feature, so it frames what changes.
scene_spec() {
	case "$1" in
		tanks)
			echo "game/core1|-1434 2240 776 0 0 0|noclip;god;notarget|2500|" ;;
		bloodpool)
			echo "game/waste|7627 -600 -30 35 90 0|noclip;god;notarget|2500|" ;;
		bulletholes)
			# nine machine-gun impacts spawned against the left wall of the
			# hangar1 spawn corridor; projected decals, so they persist
			local fx="effects/weapons/machinegun/impact_default.fx" spawns="" i=0 y
			for y in 2880 2886 2892 2898 2904 2910 2916 2922 2928; do
				spawns="$spawns;spawn func_fx fx $fx start_on 1 angle 270 origin \"$(( -5300 + i * 11 )) $y -5790\""
				spawns="$spawns;spawn func_fx fx $fx start_on 1 angle 90 origin \"$(( -5300 + i * 11 )) $y -5770\""
				i=$(( i + 1 ))
			done
			echo "game/hangar1|-5256 2850 -5819.75 -25 90 0|god${spawns};waitMsec 500|4000|" ;;
		gun)
			# eye just above the floor plane: without the depth hack the
			# floor cuts through the pistol
			echo "game/hangar1|-5256 2864 -5888 0 0 0|noclip|1500|" ;;
		brightness)
			echo "game/hangar1|||1500|r_brightness 1.5" ;;
		gamma)
			echo "game/hangar1|||1500|r_gamma 1.4" ;;
		shadows)
			# the spawn corridor: the truss ahead darkens under its own map
			echo "game/hangar1|||1500|" ;;
		shadows_marine)
			# an animated character lit by point lights: cube-map casters
			echo "game/hangar1|-5256 2864 -5819.75 15 -90 0|noclip|1500|" ;;
		wet)
			# the gun, the floor grating and the walls pick up the broad sheen
			echo "game/hangar1|||1500|r_glesD3SpecularModel 1|new" ;;
		fill)
			echo "game/hangar1|||1500|r_glesD3FillLight 1|new" ;;
		*)
			return 1 ;;
	esac
}
SCENES="tanks bloodpool bulletholes gun brightness gamma shadows shadows_marine wet fill"

run_ab() {
	local scene="$1" spec map viewpos precmds settle extra oldbase
	spec="$(scene_spec "$scene")" || { echo "unknown scene '$scene' (try --list)" >&2; return 1; }
	IFS='|' read -r map viewpos precmds settle extra oldbase <<< "$spec"

	local variant cvars
	for variant in old new; do
		if [ "$variant" = old ] && [ "$oldbase" = new ]; then
			cvars="${NEW_CVARS[*]}"
		elif [ "$variant" = old ]; then
			cvars="${OLD_CVARS[*]}"
		else
			cvars="${NEW_CVARS[*]}${extra:+ $extra}"
		fi
		echo "== $scene: $variant ($cvars)"
		RENDER_SHOT_MAP="$map" \
		RENDER_SHOT_VIEWPOS="$viewpos" \
		RENDER_SHOT_PRECMDS="$precmds" \
		RENDER_SHOT_SETTLE="$settle" \
		RENDER_SHOT_SET="$cvars" \
			"$REPO_ROOT/tools/debug/render_shot.sh" glesd3 "ab_${scene}_${variant}" \
			| grep -E "exit status|screenshot" || true
	done
	if command -v open >/dev/null 2>&1; then
		open "$SHOT_DIR/shot_glesd3_ab_${scene}_old.png" "$SHOT_DIR/shot_glesd3_ab_${scene}_new.png" || true
	fi
}

case "${1:-}" in
	--list)
		echo "$SCENES" | tr ' ' '\n'
		exit 0
		;;
	--ab)
		[ $# -ge 2 ] || { echo "usage: $(basename "$0") --ab <scene>|all" >&2; exit 2; }
		if [ "$2" = all ]; then
			for scene in $SCENES; do run_ab "$scene"; done
		else
			run_ab "$2"
		fi
		exit 0
		;;
esac

START_CVARS=( "${NEW_CVARS[@]}" )
if [ "${1:-}" = "--old" ]; then
	START_CVARS=( "${OLD_CVARS[@]}" )
	shift
fi

SET_ARGS=()
for (( i = 0; i < ${#START_CVARS[@]}; i += 2 )); do
	SET_ARGS+=( +set "${START_CVARS[i]}" "${START_CVARS[i+1]}" )
done

LOG="${OPENQ4_LOG:-/tmp/openq4-gles.log}"
echo "logging to $LOG" >&2
echo "live keys: F4 GLSL programs  F8 depth hack  F10 brightness  F11 gamma  O shadow maps  P wet specular  L fill light  F9 view position" >&2

# r_renderApi and r_renderer are archived; pass both so a stale config cannot
# redirect the run (see run_glesd3.sh). con_noPrint 0 puts the toggles' echo
# in the notify area, so a key press shows what it switched to.
"$CLIENT" \
	+set fs_basepath "$BUILD_DIR" \
	+set r_renderApi gles \
	+set r_renderer glesd3 \
	+set in_tty 0 \
	+set com_skipIntroVideos 1 \
	+set com_skipLoadingContinue 1 \
	+set con_noPrint 0 \
	"${SET_ARGS[@]}" \
	"$@" \
	+bind F4 "toggle r_glesD3GLSLPrograms" \
	+bind F8 "toggle r_glesD3DepthHack" \
	+bind F10 "toggle r_brightness 1 1.25 1.5" \
	+bind F11 "toggle r_gamma 1 1.2 1.4" \
	+bind o "toggle r_useShadowMap" \
	+bind p "toggle r_glesD3SpecularModel" \
	+bind l "toggle r_glesD3FillLight" \
	+bind F9 getviewpos \
	+set g_autoSkipCinematics 1 \
	2>&1 | tee "$LOG"

echo
echo "--- back end actually used ---"
{
	grep -aoE "using .*renderSystem.*" "$LOG" | head -1
	grep -aoE "gles_d3 shader library: .*" "$LOG" | head -1
} | grep . || echo "could not determine from the log -- run r_actualRenderer in the console, or check $LOG"
