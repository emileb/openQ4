# openQ4 Android build.
#
# Meson stays the build system for every desktop platform; this file exists
# only because the Android NDK/Gradle toolchain speaks CMake. It deliberately
# mirrors the desktop meson.build target-for-target rather than inventing a new
# layout, and it reuses the same source-list scripts under tools/build (and the
# source lists meson.build spells out itself) so the two builds can never drift:
#
#   libquake4.so                  the engine client, module-only renderer shape
#   librenderer-gles_arm64.so     the ES 3.0 renderer module
#   libgame-sp_arm64.so           the single-player game module
#
# The renderer and game modules are real dlopen()ed modules here, exactly as on
# Linux, so each keeps its own idlib copy and the ODR hazards the desktop build
# is careful about stay solved the same way.

cmake_minimum_required(VERSION 3.22)

if(NOT ANDROID)
    message(FATAL_ERROR "openQ4's CMake build is Android-only. Every other platform builds with Meson; see BUILDING.md.")
endif()

project(openQ4 C CXX)

set(OQ4_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(OQ4_SRC "${OQ4_ROOT}/src")
set(OQ4_GEN "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${OQ4_GEN}")

# A host can ship two openQ4 builds side by side: the suffix goes on every .so
# (and on the module names the engine looks for), the user dir names the
# folder under USER_FILES that holds configs and saves.
set(OPENQ4_ANDROID_LIB_SUFFIX "" CACHE STRING "Suffix for libquake4/librenderer-gles/libgame-sp")
set(OPENQ4_ANDROID_USER_DIR "quake4" CACHE STRING "Folder under USER_FILES for configs and saves")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Git REQUIRED)

# ---------------------------------------------------------------------------
# Source lists, from the same scripts Meson uses
# ---------------------------------------------------------------------------

function(oq4_run_script OUT_VAR)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${ARGN}
        WORKING_DIRECTORY "${OQ4_ROOT}"
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
        RESULT_VARIABLE _result
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "openQ4 build script failed (${ARGN}):\n${_stderr}")
    endif()
    set(${OUT_VAR} "${_stdout}" PARENT_SCOPE)
endfunction()

function(oq4_source_list OUT_VAR ROOT)
    oq4_run_script(_raw ${ARGN})
    string(REPLACE "\n" ";" _lines "${_raw}")
    set(_paths "")
    foreach(_line IN LISTS _lines)
        string(STRIP "${_line}" _line)
        if(_line)
            list(APPEND _paths "${ROOT}/${_line}")
        endif()
    endforeach()
    set(${OUT_VAR} "${_paths}" PARENT_SCOPE)
endfunction()

# The small static libraries meson.build declares inline. Their source lists
# are read out of meson.build itself: the block from START up to END.
file(READ "${OQ4_ROOT}/meson.build" OQ4_MESON_BUILD)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${OQ4_ROOT}/meson.build")
function(oq4_meson_block_sources OUT_VAR START END)
    string(FIND "${OQ4_MESON_BUILD}" "${START}" _begin)
    if(_begin EQUAL -1)
        message(FATAL_ERROR "meson.build no longer contains '${START}'")
    endif()
    string(SUBSTRING "${OQ4_MESON_BUILD}" ${_begin} -1 _tail)
    string(FIND "${_tail}" "${END}" _end)
    string(SUBSTRING "${_tail}" 0 ${_end} _block)
    string(REGEX MATCHALL "'src/[^']+\\.cpp'" _quoted "${_block}")
    set(_paths "")
    foreach(_q IN LISTS _quoted)
        string(REPLACE "'" "" _q "${_q}")
        list(APPEND _paths "${OQ4_ROOT}/${_q}")
    endforeach()
    if(NOT _paths)
        message(FATAL_ERROR "no sources found in meson.build block '${START}'")
    endif()
    set(${OUT_VAR} "${_paths}" PARENT_SCOPE)
endfunction()

set(OQ4_SOURCES_PY "${OQ4_ROOT}/tools/build/meson_sources.py")
set(OQ4_LIST_SOURCES_PY "${OQ4_ROOT}/tools/build/list_sources.py")

# The client sheds the renderer sources (--renderer module) because there is no
# desktop GL on Android at all: the ES module is the only renderer that exists.
oq4_source_list(OQ4_ENGINE_SOURCES "${OQ4_ROOT}" "${OQ4_SOURCES_PY}"
    --host-system android --platform-backend sdl3 --include-game false --renderer module)
oq4_source_list(OQ4_IMAGETOOLS_SOURCES "${OQ4_ROOT}" "${OQ4_SOURCES_PY}" --emit imagetools)
oq4_source_list(OQ4_RENDER_GEO_SOURCES "${OQ4_ROOT}" "${OQ4_SOURCES_PY}" --emit render_geo)
oq4_source_list(OQ4_RENDERER_GLES_SOURCES "${OQ4_ROOT}" "${OQ4_SOURCES_PY}" --emit renderer_gles)
oq4_source_list(OQ4_BSE_SOURCES "${OQ4_ROOT}" "${OQ4_LIST_SOURCES_PY}" "${OQ4_ROOT}" src/bse)
oq4_source_list(OQ4_IDLIB_SOURCES "${OQ4_ROOT}" "${OQ4_LIST_SOURCES_PY}" "${OQ4_ROOT}"
    src/idlib src/idlib/math/Simd_AltiVec.cpp)
# Game sources are in-tree since 0.14 (openQ4-game is retired).
oq4_source_list(OQ4_GAME_SP_SOURCES "${OQ4_ROOT}" "${OQ4_LIST_SOURCES_PY}"
    "${OQ4_ROOT}" src/game src/game/Callbacks.cpp src/game/gamesys/Callbacks.cpp)

# Engine sources meson.build appends to the meson_sources.py list itself: the
# settings host and display service, and the SDL3 client's native input driver.
oq4_meson_block_sources(OQ4_ENGINE_EXTRA_SOURCES "engine_source_paths += [" "]")
oq4_meson_block_sources(OQ4_ENGINE_SDL3_SOURCES "'src/sys/sdl3/NativeInputEmissionInventory.cpp'" ")")
list(APPEND OQ4_ENGINE_SOURCES ${OQ4_ENGINE_EXTRA_SOURCES} ${OQ4_ENGINE_SDL3_SOURCES})

oq4_meson_block_sources(OQ4_RETAINED_UI_SOURCES "openq4_retained_library = static_library(" "dependencies:")
oq4_meson_block_sources(OQ4_TEXT_INGRESS_SOURCES "text_ingress_library = static_library(" "dependencies:")
oq4_meson_block_sources(OQ4_NATIVE_QUEUE_SOURCES "native_queue_library = static_library(" "include_directories:")

# ---------------------------------------------------------------------------
# Meson wrap subprojects (RmlUi, jsoncpp, libtess2)
#
# Read straight from subprojects/<name>.wrap so the pin, hash and patches are
# the ones Meson uses. The tarball is cached where Meson caches it.
# ---------------------------------------------------------------------------

set(OQ4_WRAP_CACHE "${OQ4_ROOT}/subprojects/packagecache")
set(OQ4_WRAP_ROOT "${CMAKE_CURRENT_BINARY_DIR}/wraps")

function(oq4_wrap NAME OUT_DIR)
    set(_wrap "${OQ4_ROOT}/subprojects/${NAME}.wrap")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_wrap}")
    file(STRINGS "${_wrap}" _lines)
    foreach(_l IN LISTS _lines)
        if(_l MATCHES "^([a-z_]+)[ \t]*=[ \t]*(.*)$")
            set(_w_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
        endif()
    endforeach()

    set(_tar "${OQ4_WRAP_CACHE}/${_w_source_filename}")
    if(EXISTS "${_tar}")
        file(SHA256 "${_tar}" _have)
        if(NOT _have STREQUAL _w_source_hash)
            file(REMOVE "${_tar}")
        endif()
    endif()
    if(NOT EXISTS "${_tar}")
        message(STATUS "openQ4: downloading ${_w_source_filename}")
        file(MAKE_DIRECTORY "${OQ4_WRAP_CACHE}")
        file(DOWNLOAD "${_w_source_url}" "${_tar}"
            EXPECTED_HASH SHA256=${_w_source_hash} STATUS _status)
        list(GET _status 0 _code)
        if(NOT _code EQUAL 0)
            file(REMOVE "${_tar}")
            message(FATAL_ERROR "openQ4: could not download ${_w_source_url}: ${_status}")
        endif()
    endif()

    # Re-extract when the pin or a patch changes.
    set(_dir "${OQ4_WRAP_ROOT}/${_w_directory}")
    set(_identity "${_w_source_hash};${_w_diff_files}")
    string(REPLACE "," ";" _diffs "${_w_diff_files}")
    foreach(_d IN LISTS _diffs)
        string(STRIP "${_d}" _d)
        if(_d)
            file(MD5 "${OQ4_ROOT}/subprojects/packagefiles/${_d}" _dmd5)
            string(APPEND _identity ";${_dmd5}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${OQ4_ROOT}/subprojects/packagefiles/${_d}")
        endif()
    endforeach()
    set(_stamp "${_dir}.stamp")
    set(_existing "")
    if(EXISTS "${_stamp}")
        file(READ "${_stamp}" _existing)
    endif()
    if(NOT IS_DIRECTORY "${_dir}" OR NOT _existing STREQUAL _identity)
        file(REMOVE_RECURSE "${_dir}")
        file(ARCHIVE_EXTRACT INPUT "${_tar}" DESTINATION "${OQ4_WRAP_ROOT}")
        foreach(_d IN LISTS _diffs)
            string(STRIP "${_d}" _d)
            if(NOT _d)
                continue()
            endif()
            # the ceiling stops git treating the enclosing checkout as the repo
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E env GIT_CEILING_DIRECTORIES=${OQ4_WRAP_ROOT}
                        ${GIT_EXECUTABLE} apply -p1 "${OQ4_ROOT}/subprojects/packagefiles/${_d}"
                WORKING_DIRECTORY "${_dir}"
                RESULT_VARIABLE _r
                ERROR_VARIABLE _e)
            if(NOT _r EQUAL 0)
                message(FATAL_ERROR "openQ4: ${_d} does not apply to ${_w_directory}:\n${_e}")
            endif()
        endforeach()
        file(WRITE "${_stamp}" "${_identity}")
    endif()
    set(${OUT_DIR} "${_dir}" PARENT_SCOPE)
endfunction()

oq4_wrap(rmlui OQ4_RMLUI_DIR)
oq4_wrap(jsoncpp OQ4_JSONCPP_DIR)
oq4_wrap(libtess2 OQ4_LIBTESS2_DIR)

# RmlUi core only, the same file list its Meson overlay builds.
set(_rmlui_meson "${OQ4_ROOT}/subprojects/packagefiles/rmlui/meson.build")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_rmlui_meson}")
file(READ "${_rmlui_meson}" _rmlui_meson_text)
string(REGEX MATCHALL "'Source/[^']+\\.cpp'" _rmlui_quoted "${_rmlui_meson_text}")
set(OQ4_RMLUI_SOURCES "")
foreach(_q IN LISTS _rmlui_quoted)
    string(REPLACE "'" "" _q "${_q}")
    list(APPEND OQ4_RMLUI_SOURCES "${OQ4_RMLUI_DIR}/${_q}")
endforeach()

add_library(oq4_rmlui STATIC ${OQ4_RMLUI_SOURCES})
target_include_directories(oq4_rmlui PUBLIC "${OQ4_RMLUI_DIR}/Include")
target_compile_definitions(oq4_rmlui PUBLIC RMLUI_STATIC_LIB RMLUI_NO_THIRDPARTY_CONTAINERS
    PRIVATE RMLUI_VERSION="6.3")

add_library(oq4_jsoncpp STATIC
    "${OQ4_JSONCPP_DIR}/src/lib_json/json_reader.cpp"
    "${OQ4_JSONCPP_DIR}/src/lib_json/json_value.cpp"
    "${OQ4_JSONCPP_DIR}/src/lib_json/json_writer.cpp")
target_include_directories(oq4_jsoncpp PUBLIC "${OQ4_JSONCPP_DIR}/include")

file(GLOB OQ4_LIBTESS2_SOURCES "${OQ4_LIBTESS2_DIR}/Source/*.c")
add_library(oq4_libtess2 STATIC ${OQ4_LIBTESS2_SOURCES})
target_include_directories(oq4_libtess2 PUBLIC "${OQ4_LIBTESS2_DIR}/Include")

# ---------------------------------------------------------------------------
# Generated headers
# ---------------------------------------------------------------------------

# No --base-version: like Meson, the helper takes it from meson.build.
oq4_run_script(_version_out "${OQ4_ROOT}/tools/build/openq4_version.py"
    --source-root "${OQ4_ROOT}"
    --track dev
    --header-out "${OQ4_GEN}/openq4_version_generated.h")

oq4_run_script(_savegame_out "${OQ4_ROOT}/tools/build/generate_savegame_compat_header.py"
    --project-root "${OQ4_ROOT}"
    --game-stage-root "${OQ4_ROOT}"
    --header-out "${OQ4_GEN}/openq4_savegame_compat_generated.h")

# openq4_paks_generated.h carries the MD5 of every pack in
# tools/build/content_packs.json, and the engine refuses to start if the
# shipped baseoq4 packs do not match, so the Android build has to produce
# exactly the set the player gets. Point OPENQ4_PAK_DIR at a desktop
# builddir/baseoq4 to reuse those instead.
if(NOT DEFINED OPENQ4_PAK_DIR)
    set(OPENQ4_PAK_DIR "${CMAKE_CURRENT_BINARY_DIR}/baseoq4")
endif()
file(MAKE_DIRECTORY "${OPENQ4_PAK_DIR}")

# pak1 (light grids) and pak2 (loadscreens) are the two bulk packs, and Android
# can use almost none of either. The GLES module stubs the light grid entirely
# (RB_PrepareModernLightGrid), so pak1 keeps only the file its packer requires.
# Retail q4base already has loadscreens for 45 of the 46 maps; mcc_1 is the
# exception and stays, and generic.* is required by the packer and named
# directly by the engine.
#
# ~630MB becomes a few MB. Set OPENQ4_MINIMAL_BULK_PACKS to OFF for the
# desktop-identical set. The engine's checksum test follows either way: the
# generated header takes its MD5s from whatever was packed here.
option(OPENQ4_MINIMAL_BULK_PACKS "Pack only the pak1/pak2 content the GLES build can use" ON)

set(OPENQ4_PAK1_EXCLUDES "")
set(OPENQ4_PAK2_EXCLUDES "")
if(OPENQ4_MINIMAL_BULK_PACKS)
    set(OPENQ4_PAK1_EXCLUDES
        --exclude "*"
        --exclude "!maps/mp/q4dm1.lightgridpack")
    set(OPENQ4_PAK2_EXCLUDES
        --exclude "gfx/guis/loadscreens/*"
        --exclude "!gfx/guis/loadscreens/generic.*"
        --exclude "!gfx/guis/loadscreens/mcc_1*")
endif()

function(oq4_ensure_pak PAK_NAME SOURCE_SUBDIR)
    set(_pak "${OPENQ4_PAK_DIR}/${PAK_NAME}")
    # A pack's identity is its CONTENT plus its filter set, and both have to be
    # able to invalidate a cached pak.
    #
    # Filters alone are not enough. The source manifest is the only thing that
    # notices a file arriving from upstream, and a pak that silently predates
    # one is not a build error: generate_pak_header.py takes its MD5 from
    # whatever pak is sitting here, so the header always agrees with the stale
    # pack, the engine's integrity check passes, and the content is simply
    # missing at runtime. That is how a release shipped with no Russian or
    # Polish string tables -- a config directory packed before those tables
    # landed was reused for weeks, and nothing in the build could tell.
    #
    # The manifest carries a size and an mtime per entry, so this also catches
    # an edited file of the same length. Writing it is a stat-only walk of the
    # source tree, so doing it unconditionally costs nothing next to the pack
    # it guards.
    set(_stamp "${_pak}.stamp")
    string(REPLACE ";" " " _filter_text "${ARGN}")
    set(_manifest "${OQ4_GEN}/${PAK_NAME}.sources")
    oq4_run_script(_manifest_out "${OQ4_ROOT}/tools/build/write_pak_manifest.py"
        "${OQ4_ROOT}" "${PAK_NAME}" "${SOURCE_SUBDIR}" "${_manifest}")
    file(MD5 "${_manifest}" _manifest_md5)
    set(_identity "filters=${_filter_text}\ncontent=${_manifest_md5}\n")

    set(_existing "")
    if(EXISTS "${_stamp}")
        file(READ "${_stamp}" _existing)
    endif()
    if(EXISTS "${_pak}" AND _existing STREQUAL _identity)
        return()
    endif()
    # A reduced pack is ours, not the published content generation, so it is
    # packed from the source tree as it is; the packer only reads generation
    # history when it gets --source-root, and refuses that with --exclude.
    set(_source_root "")
    if(NOT ARGN)
        set(_source_root --source-root "${OQ4_ROOT}")
    endif()
    message(STATUS "openQ4: packing ${PAK_NAME}")
    oq4_run_script(_pack_out "${OQ4_ROOT}/tools/build/build_openq4_pack.py"
        --pak-name "${PAK_NAME}"
        --source-dir "${OQ4_ROOT}/${SOURCE_SUBDIR}"
        --manifest "${_manifest}"
        --out "${_pak}"
        ${_source_root}
        ${ARGN})
    file(WRITE "${_stamp}" "${_identity}")
endfunction()

# CONFIGURE_DEPENDS makes the generated build system re-run these globs on every
# build and re-configure when the result changes. That is what turns "a file was
# added to or removed from the pak content" into an actual repack.
#
# Without it CMake only reconfigures when CMakeLists.txt changes -- and AGP
# short-circuits its own configureCMake task on the same reasoning, so the task
# can run while cmake itself never re-executes. A language table added upstream
# then sits in content/ indefinitely without ever reaching a pak, which is
# exactly how a release shipped with no Russian or Polish strings.
file(GLOB_RECURSE OPENQ4_PAK0_CONTENT CONFIGURE_DEPENDS "${OQ4_ROOT}/content/baseoq4/pak0/*")
file(GLOB_RECURSE OPENQ4_PAK1_CONTENT CONFIGURE_DEPENDS "${OQ4_ROOT}/content/baseoq4/pak1/*")
file(GLOB_RECURSE OPENQ4_PAK2_CONTENT CONFIGURE_DEPENDS "${OQ4_ROOT}/content/baseoq4/pak2/*")
# The globs only notice files arriving or leaving; an edited file has to be a
# configure dependency itself, or a changed menu ships in a stale pak0.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${OPENQ4_PAK0_CONTENT} ${OPENQ4_PAK1_CONTENT} ${OPENQ4_PAK2_CONTENT})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${OQ4_ROOT}/tools/build/content_packs.json")

set(OPENQ4_PAK_NAMES pak0.pk4 pak1.pk4 pak2.pk4)
oq4_ensure_pak(pak0.pk4 content/baseoq4/pak0)
oq4_ensure_pak(pak1.pk4 content/baseoq4/pak1 ${OPENQ4_PAK1_EXCLUDES})
oq4_ensure_pak(pak2.pk4 content/baseoq4/pak2 ${OPENQ4_PAK2_EXCLUDES})

set(_pak_header_args "")
foreach(_pak_name IN LISTS OPENQ4_PAK_NAMES)
    list(APPEND _pak_header_args --pack "${_pak_name}=${OPENQ4_PAK_DIR}/${_pak_name}")
endforeach()
oq4_run_script(_paks_out "${OQ4_ROOT}/tools/build/generate_pak_header.py"
    ${_pak_header_args}
    --header-out "${OQ4_GEN}/openq4_paks_generated.h")

# baseoq4/mod.json is as required as the pk4s: idFileSystemLocal::Startup
# fatal-errors when fs_game is the openQ4 game dir and its manifest is missing
# or its requiredopenQ4Version does not match the engine. Meson generates it in
# content/baseoq4/meson.build; do the same here so the staged directory is
# complete rather than pk4-only.
string(REGEX MATCH "base_version=([^\r\n]*)" _version_match "${_version_out}")
set(OPENQ4_VERSION_BASE "${CMAKE_MATCH_1}")
if(NOT OPENQ4_VERSION_BASE)
    message(FATAL_ERROR "openq4_version.py did not report base_version:\n${_version_out}")
endif()
configure_file("${OQ4_ROOT}/content/baseoq4/mod.json.in" "${OPENQ4_PAK_DIR}/mod.json" @ONLY)

# The host app ships baseoq4 inside the APK and unpacks it on the device, so
# copy it wherever the app wants it. Set from the host build, because only it
# knows its own asset layout; unset, this does nothing and the files stay in the
# build directory.
#
# All of them travel together and this runs after all of them exist. Each is
# separately fatal if it goes missing or stale: the engine compares each pak
# against the MD5 compiled into it, and refuses to start without a mod.json
# whose requiredopenQ4Version matches. Staging here, rather than in a build
# target, keeps them in step with the header generated above.
if(OPENQ4_PAK_ASSET_DIR)
    file(MAKE_DIRECTORY "${OPENQ4_PAK_ASSET_DIR}")
    foreach(_staged_name ${OPENQ4_PAK_NAMES} mod.json)
        file(COPY_FILE
            "${OPENQ4_PAK_DIR}/${_staged_name}"
            "${OPENQ4_PAK_ASSET_DIR}/${_staged_name}"
            ONLY_IF_DIFFERENT)
        # A clean deletes the staged copy but not this configure; missing, it reconfigures.
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${OPENQ4_PAK_ASSET_DIR}/${_staged_name}")
    endforeach()
    message(STATUS "openQ4: staged ${OPENQ4_PAK_NAMES} and mod.json into ${OPENQ4_PAK_ASSET_DIR}")
endif()

# ---------------------------------------------------------------------------
# Shared compile settings (the CMake peer of meson.build's shared_*_args)
# ---------------------------------------------------------------------------

# OPENQ4_SIGMATOUCH turns on the OpenTouch host bridge across the engine and
# the mobile/ glue alike; it is Meson's -Dsigmatouch_root.
set(OQ4_SHARED_DEFS USE_OPENAL GLEW_NO_GLU ID_GL_HARDLINK USE_SDL3=1 OPENQ4_SIGMATOUCH=1)

# -fpermissive: idTech4-era implicit narrowing/pointer casts.
# -ffp-contract=off: keeps arm64 FMA contraction from changing collision, AAS
# and physics results relative to the validated x86-64 SSE2 baseline, which is
# what savegame and map compatibility depend on.
set(OQ4_SHARED_FLAGS -fpermissive -fno-strict-aliasing -ffp-contract=off)

set(OQ4_ENGINE_INCLUDES
    "${OQ4_ROOT}"
    "${OQ4_GEN}"
    "${OQ4_SRC}"
    "${OQ4_SRC}/external/stb_vorbis"
    "${OQ4_SRC}/game"
    "${OPENAL_INCLUDE_DIR}")

set(OQ4_GAME_INCLUDES
    "${OQ4_ROOT}"
    "${OQ4_GEN}"
    "${OQ4_SRC}"
    "${OQ4_SRC}/idlib"
    "${OQ4_SRC}/renderer"
    "${OQ4_SRC}/ui"
    "${OQ4_SRC}/sys"
    "${OQ4_SRC}/bse"
    "${OQ4_SRC}/mayaimport"
    "${OQ4_SRC}/game")

set(OQ4_PRECOMPILED_HEADER "${OQ4_SRC}/idlib/precompiled.h")

# Every legacy engine and SDK translation unit uses precompiled.h as its common
# prefix; the desktop build force-includes it the same way.
function(oq4_apply_common TARGET)
    target_compile_definitions(${TARGET} PRIVATE ${OQ4_SHARED_DEFS})
    target_compile_options(${TARGET} PRIVATE ${OQ4_SHARED_FLAGS})
    target_precompile_headers(${TARGET} PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${OQ4_PRECOMPILED_HEADER}>")
endfunction()

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Static libraries shared between the engine and the renderer module
# ---------------------------------------------------------------------------

# AL_SoundSample.cpp includes stb_vorbis.c header-only, so the implementation
# needs its own translation unit (the desktop build gets it from the
# subprojects/stb_vorbis wrap).
add_library(openq4_stb_vorbis STATIC "${OQ4_SRC}/external/stb_vorbis/stb_vorbis.c")
target_include_directories(openq4_stb_vorbis PRIVATE "${OQ4_SRC}/external/stb_vorbis")

add_library(openq4_bse STATIC ${OQ4_BSE_SOURCES})
target_include_directories(openq4_bse PRIVATE ${OQ4_ENGINE_INCLUDES} "${OQ4_SRC}/bse")
oq4_apply_common(openq4_bse)

add_library(openq4_imagetools STATIC ${OQ4_IMAGETOOLS_SOURCES})
target_include_directories(openq4_imagetools PRIVATE ${OQ4_ENGINE_INCLUDES})
target_compile_definitions(openq4_imagetools PRIVATE __DOOM_DLL__)
oq4_apply_common(openq4_imagetools)

add_library(openq4_render_geo STATIC ${OQ4_RENDER_GEO_SOURCES})
target_include_directories(openq4_render_geo PRIVATE ${OQ4_ENGINE_INCLUDES})
target_compile_definitions(openq4_render_geo PRIVATE __DOOM_DLL__)
oq4_apply_common(openq4_render_geo)

# The retained UI runtime and the SDL text/event plumbing. meson.build keeps
# these out of the engine PCH and platform headers, so they get none here.
add_library(openq4_retained_ui STATIC ${OQ4_RETAINED_UI_SOURCES})
target_include_directories(openq4_retained_ui PRIVATE "${OQ4_ROOT}" "${OQ4_GEN}")
target_link_libraries(openq4_retained_ui PUBLIC oq4_rmlui oq4_jsoncpp oq4_libtess2)

set(OQ4_SDL3_EXTENSION_INCLUDE "${OQ4_ROOT}/subprojects/packagefiles/sdl3/include")

add_library(openq4_text_ingress STATIC ${OQ4_TEXT_INGRESS_SOURCES})
target_include_directories(openq4_text_ingress PRIVATE "${OQ4_ROOT}" "${SDL3_INCLUDE_DIR}")
target_link_libraries(openq4_text_ingress PUBLIC openq4_retained_ui)

add_library(openq4_native_queue STATIC ${OQ4_NATIVE_QUEUE_SOURCES})
target_include_directories(openq4_native_queue PRIVATE
    "${OQ4_ROOT}" "${SDL3_INCLUDE_DIR}" "${OQ4_SDL3_EXTENSION_INCLUDE}")
target_compile_definitions(openq4_native_queue PRIVATE USE_SDL3)
target_link_libraries(openq4_native_queue PUBLIC openq4_retained_ui)

# The renderer module's own idlib copy, engine-flavoured so its codegen and
# interface layout match the renderer sources it is linked into.
add_library(openq4_renderer_idlib STATIC ${OQ4_IDLIB_SOURCES})
target_include_directories(openq4_renderer_idlib PRIVATE ${OQ4_ENGINE_INCLUDES})
target_compile_definitions(openq4_renderer_idlib PRIVATE __DOOM_DLL__)
oq4_apply_common(openq4_renderer_idlib)

# The game module's idlib copy. Separate archive because precompiled.h pulls in
# the game's own types under -DGAME_DLL, so sharing one archive would put two
# different definitions of the same types into one binary.
add_library(openq4_game_idlib STATIC ${OQ4_IDLIB_SOURCES})
target_include_directories(openq4_game_idlib PRIVATE
    "${OQ4_ROOT}" "${OQ4_GEN}" "${OQ4_SRC}" "${OQ4_SRC}/game")
target_compile_definitions(openq4_game_idlib PRIVATE GAME_DLL)
set_target_properties(openq4_game_idlib PROPERTIES
    CXX_STANDARD 17
    CXX_VISIBILITY_PRESET hidden)
oq4_apply_common(openq4_game_idlib)

# ---------------------------------------------------------------------------
# libquake4.so: the engine client plus the OpenTouch host glue
# ---------------------------------------------------------------------------

set(OQ4_MOBILE_SOURCES
    "${OQ4_ROOT}/mobile/game_interface.cpp"
    "${OQ4_ROOT}/mobile/touch_interface_quake4.cpp"
    "${CLIBS_DIR}/sigma/android_jni.cpp"
    "${CLIBS_DIR}/touch_interface_base.cpp"
    "${CLIBS_DIR}/LogWritter.c"
    "${CLIBS_DIR}/fts.c"
    "${CLIBS_DIR}/rand.c")

# The host glue is not engine code and must not get precompiled.h forced into
# it - that prefix pulls in the whole engine and collides with the touch
# library's own names.
set_source_files_properties(${OQ4_MOBILE_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

set(OQ4_ENGINE_TARGET quake4${OPENQ4_ANDROID_LIB_SUFFIX})
add_library(${OQ4_ENGINE_TARGET} SHARED ${OQ4_ENGINE_SOURCES} ${OQ4_MOBILE_SOURCES})
target_include_directories(${OQ4_ENGINE_TARGET} PRIVATE
    # mobile/ must precede CLIBS_DIR so the shared touch_interface_base.cpp and
    # JNI glue resolve *this* engine's touch_interface.h, not another engine's.
    "${OQ4_ROOT}/mobile"
    ${OQ4_ENGINE_INCLUDES}
    "${SDL3_INCLUDE_DIR}"
    # types only: every OQ4_* call is behind OPENQ4_SDL3_CHECKED_NATIVE_QUEUE,
    # which only upstream's bundled Windows SDL defines
    "${OQ4_SDL3_EXTENSION_INCLUDE}"
    "${CLIBS_DIR}"
    "${TOUCH_CONTROLS_DIR}")
target_compile_definitions(${OQ4_ENGINE_TARGET} PRIVATE
    __DOOM_DLL__
    OPENQ4_RENDERER_MODULE_ONLY
    OPENQ4_ANDROID_MODULE_SUFFIX="${OPENQ4_ANDROID_LIB_SUFFIX}"
    OPENQ4_ANDROID_USER_DIR="${OPENQ4_ANDROID_USER_DIR}"
    # names the touch-control layout XML files; per build, like the user dir
    ENGINE_NAME="${OPENQ4_ANDROID_USER_DIR}")
target_link_libraries(${OQ4_ENGINE_TARGET}
    openq4_bse openq4_imagetools openq4_render_geo openq4_stb_vorbis
    openq4_text_ingress openq4_native_queue openq4_retained_ui
    "${SDL3_LIBRARY}" openal-soft touchcontrols saffal
    log android dl m)
oq4_apply_common(${OQ4_ENGINE_TARGET})
set_target_properties(${OQ4_ENGINE_TARGET} PROPERTIES PREFIX "lib")

# ---------------------------------------------------------------------------
# librenderer-gles_arm64.so: the ES 3.0 renderer module
# ---------------------------------------------------------------------------

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR ANDROID_ABI STREQUAL "arm64-v8a")
    set(OQ4_MODULE_ARCH_TAG "arm64")
else()
    # sys_local.cpp / Common.cpp / RendererModule.cpp all derive the same tag
    # from the compiler's arch macros; anything else has no tag they agree on.
    message(FATAL_ERROR "openQ4 on Android targets arm64-v8a only (got ${ANDROID_ABI}).")
endif()

set(OQ4_RENDERER_GLES_TARGET renderer-gles_${OQ4_MODULE_ARCH_TAG}${OPENQ4_ANDROID_LIB_SUFFIX})
add_library(${OQ4_RENDERER_GLES_TARGET} SHARED ${OQ4_RENDERER_GLES_SOURCES})
target_include_directories(${OQ4_RENDERER_GLES_TARGET} PRIVATE
    ${OQ4_ENGINE_INCLUDES}
    "${OQ4_SRC}/external/gles")
target_compile_definitions(${OQ4_RENDERER_GLES_TARGET} PRIVATE
    __DOOM_DLL__
    OPENQ4_RENDERER_MODULE
    OPENQ4_RENDERER_GLES_MODULE
    OPENQ4_SDL3_CONTEXT_IMPL
    OPENQ4_RENDERER_BACKEND_NAME="gles"
    OPENQ4_RENDERER_BACKEND_DESC="openQ4 OpenGL ES renderer module")
target_link_libraries(${OQ4_RENDERER_GLES_TARGET}
    openq4_renderer_idlib openq4_imagetools openq4_render_geo
    GLESv3 EGL log m)
oq4_apply_common(${OQ4_RENDERER_GLES_TARGET})
set_target_properties(${OQ4_RENDERER_GLES_TARGET} PROPERTIES
    PREFIX "lib"
    CXX_VISIBILITY_PRESET hidden)

# ---------------------------------------------------------------------------
# libgame-sp_arm64.so: the single-player game module
# ---------------------------------------------------------------------------

set(OQ4_GAME_SP_TARGET game-sp_${OQ4_MODULE_ARCH_TAG}${OPENQ4_ANDROID_LIB_SUFFIX})
add_library(${OQ4_GAME_SP_TARGET} SHARED ${OQ4_GAME_SP_SOURCES})
target_include_directories(${OQ4_GAME_SP_TARGET} PRIVATE ${OQ4_GAME_INCLUDES})
target_compile_definitions(${OQ4_GAME_SP_TARGET} PRIVATE GAME_DLL)
target_link_libraries(${OQ4_GAME_SP_TARGET} openq4_game_idlib log m)
oq4_apply_common(${OQ4_GAME_SP_TARGET})
set_target_properties(${OQ4_GAME_SP_TARGET} PROPERTIES
    PREFIX "lib"
    CXX_STANDARD 17
    CXX_VISIBILITY_PRESET hidden)

# The engine dlopen()s both modules at runtime, but they must be built (and
# therefore packaged) whenever the engine is.
add_dependencies(${OQ4_ENGINE_TARGET} ${OQ4_RENDERER_GLES_TARGET} ${OQ4_GAME_SP_TARGET})
