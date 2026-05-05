# CMakeList.txt : CMake project for OriginBlock, include source and define
# project specific logic here.
#


cmake_minimum_required(VERSION 3.16)
project(OriginBlock)

set(CMAKE_CXX_STANDARD 20)

# Enable AddressSanitizer for MSVC (Debug configuration recommended)
if (MSVC)
    # Disable Edit and Continue (incompatible with ASan)
    # Comment out or override the default if present
    string(REPLACE "/ZI" "/Zi" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/ZI" "/Zi" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

    # Add the ASan flag (applies to compile and link)
    add_compile_options($<$<CONFIG:Debug>:/fsanitize=address>)
    add_link_options($<$<CONFIG:Debug>:/fsanitize=address>)

    # Optional: Increase stack size for better detection (ASan uses more stack)
    add_link_options($<$<CONFIG:Debug>:/STACK:16777216>)
endif()

# SFML location (adjust to your path)
set(SFML_DIR "C:/SFML-2.6.0/lib/cmake/SFML")

# Find SFML
find_package(SFML 2.6 COMPONENTS system window graphics REQUIRED)
# Find OpenGL
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

# Add executable
add_executable(OriginBlock "src/OriginBlock.cpp" "src/glad/glad.c" "src/shader.cpp" "src/inputs.cpp" "src/textureManager.cpp" "src/texture.cpp" "src/stb_image.cpp" "src/chunk.cpp" "src/Worlds.cpp" "src/terrain.cpp" "src/commands.cpp" "src/ThreadedChunkSystem.cpp" "src/TextRenderer.cpp" "include/glad/glad.h" "include/glm/glm/detail/glm.cpp" "include/glm/glm/simd/common.h" "include/glm/glm/simd/exponential.h" "include/glm/glm/simd/geometric.h" "include/glm/glm/simd/integer.h" "include/glm/glm/simd/matrix.h" "include/glm/glm/simd/neon.h" "include/glm/glm/simd/packing.h" "include/glm/glm/simd/platform.h" "include/glm/glm/simd/trigonometric.h" "include/glm/glm/simd/vector_relational.h" "include/glm/test/bug/bug_ms_vec_static.cpp" "include/glm/test/cmake/test_find_glm.cpp" "include/glm/test/core/core_cpp_constexpr.cpp" "include/glm/test/core/core_cpp_defaulted_ctor.cpp" "include/glm/test/core/core_force_aligned_gentypes.cpp" "include/glm/test/core/core_force_arch_unknown.cpp" "include/glm/test/core/core_force_compiler_unknown.cpp" "include/glm/test/core/core_force_ctor_init.cpp" "include/glm/test/core/core_force_cxx03.cpp" "include/glm/test/core/core_force_cxx98.cpp" "include/glm/test/core/core_force_cxx_unknown.cpp" "include/glm/test/core/core_force_depth_zero_to_one.cpp" "include/glm/test/core/core_force_explicit_ctor.cpp" "include/glm/test/core/core_force_inline.cpp" "include/glm/test/core/core_force_left_handed.cpp" "include/glm/test/core/core_force_platform_unknown.cpp" "include/glm/test/core/core_force_pure.cpp" "include/glm/test/core/core_force_quat_wxyz.cpp" "include/glm/test/core/core_force_size_t_length.cpp" "include/glm/test/core/core_force_unrestricted_gentype.cpp" "include/glm/test/core/core_force_xyzw_only.cpp" "include/glm/test/core/core_func_common.cpp" "include/glm/test/core/core_func_exponential.cpp" "include/glm/test/core/core_func_geometric.cpp" "include/glm/test/core/core_func_integer.cpp" "include/glm/test/core/core_func_integer_bit_count.cpp" "include/glm/test/core/core_func_integer_find_lsb.cpp" "include/glm/test/core/core_func_integer_find_msb.cpp" "include/glm/test/core/core_func_matrix.cpp" "include/glm/test/core/core_func_noise.cpp" "include/glm/test/core/core_func_packing.cpp" "include/glm/test/core/core_func_swizzle.cpp" "include/glm/test/core/core_func_trigonometric.cpp" "include/glm/test/core/core_func_vector_relational.cpp" "include/glm/test/core/core_setup_force_cxx98.cpp" "include/glm/test/core/core_setup_force_size_t_length.cpp" "include/glm/test/core/core_setup_message.cpp" "include/glm/test/core/core_setup_platform_unknown.cpp" "include/glm/test/core/core_setup_precision.cpp" "include/glm/test/core/core_type_aligned.cpp" "include/glm/test/core/core_type_cast.cpp" "include/glm/test/core/core_type_ctor.cpp" "include/glm/test/core/core_type_int.cpp" "include/glm/test/core/core_type_length.cpp" "include/glm/test/core/core_type_mat2x2.cpp" "include/glm/test/core/core_type_mat2x3.cpp" "include/glm/test/core/core_type_mat2x4.cpp" "include/glm/test/core/core_type_mat3x2.cpp" "include/glm/test/core/core_type_mat3x3.cpp" "include/glm/test/core/core_type_mat3x4.cpp" "include/glm/test/core/core_type_mat4x2.cpp" "include/glm/test/core/core_type_mat4x3.cpp" "include/glm/test/core/core_type_mat4x4.cpp" "include/glm/test/core/core_type_vec1.cpp" "include/glm/test/core/core_type_vec2.cpp" "include/glm/test/core/core_type_vec3.cpp" "include/glm/test/core/core_type_vec4.cpp" "include/glm/test/ext/ext_matrix_clip_space.cpp" "include/glm/test/ext/ext_matrix_common.cpp" "include/glm/test/ext/ext_matrix_int2x2_sized.cpp" "include/glm/test/ext/ext_matrix_int2x3_sized.cpp" "include/glm/test/ext/ext_matrix_int2x4_sized.cpp" "include/glm/test/ext/ext_matrix_int3x2_sized.cpp" "include/glm/test/ext/ext_matrix_int3x3_sized.cpp" "include/glm/test/ext/ext_matrix_int3x4_sized.cpp" "include/glm/test/ext/ext_matrix_int4x2_sized.cpp" "include/glm/test/ext/ext_matrix_int4x3_sized.cpp" "include/glm/test/ext/ext_matrix_int4x4_sized.cpp" "include/glm/test/ext/ext_matrix_projection.cpp" "include/glm/test/ext/ext_matrix_relational.cpp" "include/glm/test/ext/ext_matrix_transform.cpp" "include/glm/test/ext/ext_matrix_uint2x2_sized.cpp" "include/glm/test/ext/ext_matrix_uint2x3_sized.cpp" "include/glm/test/ext/ext_matrix_uint2x4_sized.cpp" "include/glm/test/ext/ext_matrix_uint3x2_sized.cpp" "include/glm/test/ext/ext_matrix_uint3x3_sized.cpp" "include/glm/test/ext/ext_matrix_uint3x4_sized.cpp" "include/glm/test/ext/ext_matrix_uint4x2_sized.cpp" "include/glm/test/ext/ext_matrix_uint4x3_sized.cpp" "include/glm/test/ext/ext_matrix_uint4x4_sized.cpp" "include/glm/test/ext/ext_quaternion_common.cpp" "include/glm/test/ext/ext_quaternion_exponential.cpp" "include/glm/test/ext/ext_quaternion_geometric.cpp" "include/glm/test/ext/ext_quaternion_relational.cpp" "include/glm/test/ext/ext_quaternion_transform.cpp" "include/glm/test/ext/ext_quaternion_trigonometric.cpp" "include/glm/test/ext/ext_quaternion_type.cpp" "include/glm/test/ext/ext_scalar_common.cpp" "include/glm/test/ext/ext_scalar_constants.cpp" "include/glm/test/ext/ext_scalar_integer.cpp" "include/glm/test/ext/ext_scalar_int_sized.cpp" "include/glm/test/ext/ext_scalar_packing.cpp" "include/glm/test/ext/ext_scalar_relational.cpp" "include/glm/test/ext/ext_scalar_uint_sized.cpp" "include/glm/test/ext/ext_scalar_ulp.cpp" "include/glm/test/ext/ext_vec1.cpp" "include/glm/test/ext/ext_vector_bool1.cpp" "include/glm/test/ext/ext_vector_common.cpp" "include/glm/test/ext/ext_vector_iec559.cpp" "include/glm/test/ext/ext_vector_int1_sized.cpp" "include/glm/test/ext/ext_vector_int2_sized.cpp" "include/glm/test/ext/ext_vector_int3_sized.cpp" "include/glm/test/ext/ext_vector_int4_sized.cpp" "include/glm/test/ext/ext_vector_integer.cpp" "include/glm/test/ext/ext_vector_integer_sized.cpp" "include/glm/test/ext/ext_vector_packing.cpp" "include/glm/test/ext/ext_vector_relational.cpp" "include/glm/test/ext/ext_vector_uint1_sized.cpp" "include/glm/test/ext/ext_vector_uint2_sized.cpp" "include/glm/test/ext/ext_vector_uint3_sized.cpp" "include/glm/test/ext/ext_vector_uint4_sized.cpp" "include/glm/test/ext/ext_vector_ulp.cpp" "include/glm/test/gtc/gtc_bitfield.cpp" "include/glm/test/gtc/gtc_color_space.cpp" "include/glm/test/gtc/gtc_constants.cpp" "include/glm/test/gtc/gtc_epsilon.cpp" "include/glm/test/gtc/gtc_integer.cpp" "include/glm/test/gtc/gtc_matrix_access.cpp" "include/glm/test/gtc/gtc_matrix_integer.cpp" "include/glm/test/gtc/gtc_matrix_inverse.cpp" "include/glm/test/gtc/gtc_matrix_transform.cpp" "include/glm/test/gtc/gtc_noise.cpp" "include/glm/test/gtc/gtc_packing.cpp" "include/glm/test/gtc/gtc_quaternion.cpp" "include/glm/test/gtc/gtc_random.cpp" "include/glm/test/gtc/gtc_reciprocal.cpp" "include/glm/test/gtc/gtc_round.cpp" "include/glm/test/gtc/gtc_type_aligned.cpp" "include/glm/test/gtc/gtc_type_precision.cpp" "include/glm/test/gtc/gtc_type_ptr.cpp" "include/glm/test/gtc/gtc_ulp.cpp" "include/glm/test/gtc/gtc_user_defined_types.cpp" "include/glm/test/gtc/gtc_vec1.cpp" "include/glm/test/gtx/gtx.cpp" "include/glm/test/gtx/gtx_associated_min_max.cpp" "include/glm/test/gtx/gtx_closest_point.cpp" "include/glm/test/gtx/gtx_color_encoding.cpp" "include/glm/test/gtx/gtx_color_space.cpp" "include/glm/test/gtx/gtx_color_space_YCoCg.cpp" "include/glm/test/gtx/gtx_common.cpp" "include/glm/test/gtx/gtx_compatibility.cpp" "include/glm/test/gtx/gtx_component_wise.cpp" "include/glm/test/gtx/gtx_dual_quaternion.cpp" "include/glm/test/gtx/gtx_easing.cpp" "include/glm/test/gtx/gtx_euler_angle.cpp" "include/glm/test/gtx/gtx_extend.cpp" "include/glm/test/gtx/gtx_extended_min_max.cpp" "include/glm/test/gtx/gtx_extented_min_max.cpp" "include/glm/test/gtx/gtx_exterior_product.cpp" "include/glm/test/gtx/gtx_fast_exponential.cpp" "include/glm/test/gtx/gtx_fast_square_root.cpp" "include/glm/test/gtx/gtx_fast_trigonometry.cpp" "include/glm/test/gtx/gtx_functions.cpp" "include/glm/test/gtx/gtx_gradient_paint.cpp" "include/glm/test/gtx/gtx_handed_coordinate_space.cpp" "include/glm/test/gtx/gtx_integer.cpp" "include/glm/test/gtx/gtx_intersect.cpp" "include/glm/test/gtx/gtx_int_10_10_10_2.cpp" "include/glm/test/gtx/gtx_io.cpp" "include/glm/test/gtx/gtx_load.cpp" "include/glm/test/gtx/gtx_log_base.cpp" "include/glm/test/gtx/gtx_matrix_cross_product.cpp" "include/glm/test/gtx/gtx_matrix_decompose.cpp" "include/glm/test/gtx/gtx_matrix_factorisation.cpp" "include/glm/test/gtx/gtx_matrix_interpolation.cpp" "include/glm/test/gtx/gtx_matrix_major_storage.cpp" "include/glm/test/gtx/gtx_matrix_operation.cpp" "include/glm/test/gtx/gtx_matrix_query.cpp" "include/glm/test/gtx/gtx_matrix_transform_2d.cpp" "include/glm/test/gtx/gtx_mixed_product.cpp" "include/glm/test/gtx/gtx_norm.cpp" "include/glm/test/gtx/gtx_normal.cpp" "include/glm/test/gtx/gtx_normalize_dot.cpp" "include/glm/test/gtx/gtx_number_precision.cpp" "include/glm/test/gtx/gtx_optimum_pow.cpp" "include/glm/test/gtx/gtx_orthonormalize.cpp" "include/glm/test/gtx/gtx_perpendicular.cpp" "include/glm/test/gtx/gtx_polar_coordinates.cpp" "include/glm/test/gtx/gtx_projection.cpp" "include/glm/test/gtx/gtx_quaternion.cpp" "include/glm/test/gtx/gtx_random.cpp" "include/glm/test/gtx/gtx_range.cpp" "include/glm/test/gtx/gtx_rotate_normalized_axis.cpp" "include/glm/test/gtx/gtx_rotate_vector.cpp" "include/glm/test/gtx/gtx_scalar_multiplication.cpp" "include/glm/test/gtx/gtx_scalar_relational.cpp" "include/glm/test/gtx/gtx_simd_mat4.cpp" "include/glm/test/gtx/gtx_simd_vec4.cpp" "include/glm/test/gtx/gtx_spline.cpp" "include/glm/test/gtx/gtx_string_cast.cpp" "include/glm/test/gtx/gtx_texture.cpp" "include/glm/test/gtx/gtx_type_aligned.cpp" "include/glm/test/gtx/gtx_type_trait.cpp" "include/glm/test/gtx/gtx_vector_angle.cpp" "include/glm/test/gtx/gtx_vector_query.cpp" "include/glm/test/gtx/gtx_vec_swizzle.cpp" "include/glm/test/gtx/gtx_wrap.cpp" "include/glm/test/perf/perf_matrix_div.cpp" "include/glm/test/perf/perf_matrix_inverse.cpp" "include/glm/test/perf/perf_matrix_mul.cpp" "include/glm/test/perf/perf_matrix_mul_vector.cpp" "include/glm/test/perf/perf_matrix_transpose.cpp" "include/glm/test/perf/perf_vector_mul_matrix.cpp" "include/KHR/khrplatform.h" "include/stb/stb_image.h" "include/stb/stb_image_write.h" "include/stb/stb_truetype.h")

# Include headers
target_include_directories(OriginBlock PRIVATE ${PROJECT_SOURCE_DIR}/include)

add_library(glfw STATIC IMPORTED)
set_target_properties(glfw PROPERTIES
    IMPORTED_LOCATION "C:/GLFW/build/src/Release/glfw3.lib"
    INTERFACE_INCLUDE_DIRECTORIES "C:/GLFW/include"
)

# Link libraries
target_link_libraries(OriginBlock
    sfml-system
    OpenGL::GL
    glfw
    Threads::Threads
)

add_custom_command(TARGET OriginBlock POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:OriginBlock>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/res
        $<TARGET_FILE_DIR:OriginBlock>/res
)


# TODO: Add tests and install targets if needed.
