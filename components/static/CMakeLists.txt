idf_component_register(SRCS  "static.c"
                       INCLUDE_DIRS "include")

# Set the output directory for the minified files within the build folder
set(MINIFIED_OUTPUT_DIR "${CMAKE_BINARY_DIR}/minified")

# Gather all HTML, CSS, and JS files from the 'files' directory
file(GLOB STATIC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/files/*.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/files/*.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/files/*.js"
)

# Initialize a list for the minified files
set(MINIFIED_FILES)

# Process each file individually
foreach(SRC_FILE ${STATIC_FILES})
    # Get the filename
    get_filename_component(FILENAME ${SRC_FILE} NAME)

    # Define the output file path
    set(OUT_FILE "${MINIFIED_OUTPUT_DIR}/${FILENAME}")

    # Append the output file to the list
    list(APPEND MINIFIED_FILES ${OUT_FILE})

    # Minify command requires 'minify' tool to be installed and available in PATH
    # https://github.com/tdewolff/minify/releases
    # Add a custom command to minify the file
    add_custom_command(
        OUTPUT ${OUT_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MINIFIED_OUTPUT_DIR}"
        COMMAND minify -o "${OUT_FILE}" "${SRC_FILE}"
        DEPENDS ${SRC_FILE}
        COMMENT "Minifying ${FILENAME}"
    )
    # Add the minified file to the binary
    target_add_binary_data(${COMPONENT_LIB} "${OUT_FILE}" TEXT)
endforeach()

# Add a custom target that depends on all minified files
add_custom_target(minify_static_files ALL
    DEPENDS ${MINIFIED_FILES}
)

# Ensure that the minification runs before building this component
add_dependencies(${COMPONENT_LIB} minify_static_files)

