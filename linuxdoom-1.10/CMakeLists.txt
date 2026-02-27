# DOOM 1.10 Linux/macOS Port

# Platform detection and backend selection
option(USE_SDL2 "Use SDL2 backend instead of X11" OFF)

if(APPLE)
    # macOS always uses SDL2
    set(USE_SDL2 ON CACHE BOOL "Use SDL2 backend" FORCE)
    message(STATUS "macOS detected - using SDL2 backend")
elseif(UNIX AND NOT APPLE)
    # Linux defaults to X11 but can opt into SDL2
    if(USE_SDL2)
        message(STATUS "Linux - using SDL2 backend (user requested)")
    else()
        message(STATUS "Linux - using X11 backend (default)")
    endif()
endif()

# Find required libraries based on platform
if(USE_SDL2)
    # Use pkg-config to find SDL2
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    message(STATUS "SDL2_LIBRARIES: ${SDL2_LIBRARIES}")
    message(STATUS "SDL2_LIBRARY_DIRS: ${SDL2_LIBRARY_DIRS}")
    message(STATUS "SDL2_INCLUDE_DIRS: ${SDL2_INCLUDE_DIRS}")
    message(STATUS "SDL2_LDFLAGS: ${SDL2_LDFLAGS}")
    add_compile_definitions(USE_SDL2)
    if(APPLE)
        add_compile_definitions(NORMALUNIX)
    endif()
else()
    # X11 backend (Linux)
    find_package(X11 REQUIRED)
    if(NOT X11_Xext_FOUND)
        message(FATAL_ERROR "X11 extensions library not found")
    endif()
    add_compile_definitions(NORMALUNIX LINUX)
endif()

# Common source files (platform-independent game logic)
set(COMMON_SOURCES
    doomdef.c
    doomstat.c
    dstrings.c
    i_system.c
    i_net.c
    tables.c
    f_finale.c
    f_wipe.c
    d_main.c
    d_net.c
    d_items.c
    g_game.c
    m_menu.c
    m_misc.c
    m_argv.c
    m_bbox.c
    m_fixed.c
    m_swap.c
    m_cheat.c
    m_random.c
    am_map.c
    p_ceilng.c
    p_doors.c
    p_enemy.c
    p_floor.c
    p_inter.c
    p_lights.c
    p_map.c
    p_maputl.c
    p_plats.c
    p_pspr.c
    p_setup.c
    p_sight.c
    p_spec.c
    p_switch.c
    p_mobj.c
    p_telept.c
    p_tick.c
    p_saveg.c
    p_user.c
    r_bsp.c
    r_data.c
    r_draw.c
    r_main.c
    r_plane.c
    r_segs.c
    r_sky.c
    r_things.c
    w_wad.c
    wi_stuff.c
    v_video.c
    st_lib.c
    st_stuff.c
    hu_stuff.c
    hu_lib.c
    s_sound.c
    z_zone.c
    info.c
    sounds.c
    i_main.c
)

# Platform-specific sources
if(USE_SDL2)
    set(PLATFORM_SOURCES
        i_video_sdl.c
        i_sound_sdl.c
    )
    # Disable SDL2 MMX on ARM64 (Apple Silicon)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        add_compile_definitions(SDL_DISABLE_IMMINTRIN_H)
    endif()
else()
    set(PLATFORM_SOURCES
        i_video.c
        i_sound.c
    )
endif()

# Create executable
if(USE_SDL2)
    set(EXECUTABLE_NAME doom)
else()
    set(EXECUTABLE_NAME linuxxdoom)
endif()

add_executable(${EXECUTABLE_NAME} ${COMMON_SOURCES} ${PLATFORM_SOURCES})

# Include directories
target_include_directories(${EXECUTABLE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    $<$<BOOL:${USE_SDL2}>:${SDL2_INCLUDE_DIRS}>
)

# Link directories and libraries
if(USE_SDL2)
    target_link_directories(${EXECUTABLE_NAME} PRIVATE ${SDL2_LIBRARY_DIRS})
    target_link_options(${EXECUTABLE_NAME} PRIVATE ${SDL2_LDFLAGS})
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE m)
else()
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE ${X11_LIBRARIES} ${X11_Xext_LIB} m)
endif()

# Compiler flags
target_compile_options(${EXECUTABLE_NAME} PRIVATE
    -Wall
    -Wno-unused-result  # Suppress warnings from legacy code
)

# Debug build flags
if(CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_options(${EXECUTABLE_NAME} PRIVATE -g -O0)
else()
    target_compile_options(${EXECUTABLE_NAME} PRIVATE -O2)
endif()

# Install target
install(TARGETS ${EXECUTABLE_NAME} DESTINATION bin)
