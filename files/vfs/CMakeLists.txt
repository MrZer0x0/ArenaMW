if (NOT DEFINED OPENMW_MYGUI_FILES_ROOT)
    return()
endif()

# Copy built-in single-player resources into the build directory.
set(DDIRRELATIVE resources/vfs/textures)

set(TEXTURE_FILES
    textures/crosshair.dds
    textures/ownership.dds
    textures/sneak.dds
    textures/omw_menu_scroll_down.dds
    textures/omw_menu_scroll_up.dds
    textures/omw_menu_scroll_left.dds
    textures/omw_menu_scroll_right.dds
    textures/omw_menu_scroll_center_h.dds
    textures/omw_menu_scroll_center_v.dds
)

# Menu/logo and legacy compass assets are optional. Older cumulative patches
# listed them unconditionally, which made CMake fail when a texture was absent.
foreach(optional_texture
    textures/menu_arenamw.dds
    textures/menu_arenamw.png
    textures/hud_compass_marker.dds
)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${optional_texture}")
        list(APPEND TEXTURE_FILES "${optional_texture}")
    endif()
endforeach()

copy_all_resource_files(${CMAKE_CURRENT_SOURCE_DIR} ${OPENMW_MYGUI_FILES_ROOT} ${DDIRRELATIVE} "${TEXTURE_FILES}")

file(GLOB_RECURSE BUILTIN_VFS_FILES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    animations/*.kf
    animations/*.nif
    animations/*.yaml
    animations/*.txt
    meshes/*.nif
    sound/*.wav
    sound/*.mp3
    textures/vfx/*.dds
    textures/vfx/*.tga
    textures/exploot/food/*.tga
    textures/ui/arenamw/*.png
    l10n/arenamp/*.ini
    icons/inventoryextender/*.dds
)

# Consuming Animated pipe/smoke models reference these texture names directly.
# Keep them at the VFS texture root to preserve the paths embedded in the NIFs.
list(APPEND BUILTIN_VFS_FILES
    "textures/cigar logo.dds"
    "textures/cigar wrap.dds"
    "textures/metal_30w.dds"
    "textures/pc_misc_pipe_cob.dds"
)

foreach (f ${BUILTIN_VFS_FILES})
    copy_resource_file("${CMAKE_CURRENT_SOURCE_DIR}/${f}" "${OPENMW_MYGUI_FILES_ROOT}" "resources/vfs/${f}")
endforeach()
