find_package(Doxygen)
if (DOXYGEN_FOUND)

    # set input and output files
    get_target_property(PUBLIC_HEADER_DIR_RPC rpc INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(PUBLIC_HEADER_DIR_FABRIC fabricBased INTERFACE_INCLUDE_DIRECTORIES)

    file(GLOB_RECURSE PUBLIC_HEADERS_FABRIC ${PUBLIC_HEADER_DIR_FABRIC}/*.hh)
    file(GLOB_RECURSE PUBLIC_HEADERS_RPC ${PUBLIC_HEADER_DIR_RPC}/*.hh)

    set(DOXYGEN_INPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Mainpage.md ${PUBLIC_HEADER_DIR_FABRIC}/networklayer ${PUBLIC_HEADER_DIR_RPC}/networklayer")
    set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/doxygen)
    set(DOXYGEN_INDEX_FILE ${DOXYGEN_OUTPUT_DIR}/html/index.html)
    set(DOXYFILE_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)

    file(MAKE_DIRECTORY ${DOXYGEN_OUTPUT_DIR}) #Doxygen won't create this for us

    add_custom_command(OUTPUT ${DOXYGEN_INDEX_FILE}
            DEPENDS ${PUBLIC_HEADERS_FABRIC} ${PUBLIC_HEADERS_RPC} ${DOXYFILE_IN} Mainpage.md
            COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
            MAIN_DEPENDENCY ${DOXYFILE_OUT} ${DOXYFILE_IN}
            COMMENT "Generating docs")

    add_custom_target(Doxygen ALL DEPENDS ${DOXYGEN_INDEX_FILE})

else (DOXYGEN_FOUND)
    message("Doxygen need to be installed to generate the doxygen documentation")
endif (DOXYGEN_FOUND)