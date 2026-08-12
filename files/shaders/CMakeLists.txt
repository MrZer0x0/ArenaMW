if (NOT DEFINED OPENMW_SHADERS_ROOT)
    return()
endif()

# Copy resource files into the build directory
set(SDIR ${CMAKE_CURRENT_SOURCE_DIR})
set(DDIRRELATIVE resources/shaders)

set(SHADER_FILES
    atmosphere.glsl
    lib/water/ripples.glsl
    fullscreen_tri.vert
    bloom_extract_horizontal.frag
    bloom_vertical.frag
    bloom_composite.frag
    native_effects_composite.frag
    native_smaa_edge.frag
    native_smaa_weights.frag
    native_final.frag
    ripples_blobber.frag
    ripples_simulate.frag
    groundcover_vertex.glsl
    groundcover_fragment.glsl
    water_vertex.glsl
    water_fragment.glsl
    water_pbr_vertex.glsl
    water_pbr_fragment.glsl
    water_pbr_compat.glsl
    water_pbr_rain_ripples.glsl
    water_pbr_data.glsl
    water_nm.png
    water_waves.glsl
    alpha.glsl
    HDR.glsl
    helpsettings.glsl
    objects_vertex.glsl
    objects_fragment.glsl
    terrain_vertex.glsl
    terrain_fragment.glsl
    lighting.glsl
    lighting_pbr_compat.glsl
    lighting_enhanced_pbr.glsl
    lighting_util.glsl
    parallax.glsl
    s360_fragment.glsl
    s360_vertex.glsl
    shadows_vertex.glsl
    shadows_fragment.glsl
    shadowcasting_vertex.glsl
    shadowcasting_fragment.glsl
    vertexcolors.glsl
    nv_default_vertex.glsl
    nv_default_fragment.glsl
    nv_nolighting_vertex.glsl
    nv_nolighting_fragment.glsl
    openmw_vertex.glsl
    openmw_vertex.h.glsl
    openmw_vertex_multiview.glsl
    openmw_fragment.glsl
    openmw_fragment.h.glsl
    openmw_fragment_multiview.glsl
)

copy_all_resource_files(${CMAKE_CURRENT_SOURCE_DIR} ${OPENMW_SHADERS_ROOT} ${DDIRRELATIVE} "${SHADER_FILES}")
