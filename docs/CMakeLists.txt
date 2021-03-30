if (DOXYGEN_FOUND)

    # set input and output files
    set(PUBLIC_HEADER_DIR_RPC ${CMAKE_CURRENT_BINARY_DIR}/../commonAPI/include)
    set(PUBLIC_HEADER_DIR_RPC_2 ${CMAKE_CURRENT_SOURCE_DIR}/../commonAPI/include)

    set(PUBLIC_HEADER_DIR_FABRIC ${CMAKE_CURRENT_SOURCE_DIR}/../fabricBased/include)

    file(GLOB_RECURSE PUBLIC_HEADERS_FABRIC ${PUBLIC_HEADER_DIR_FABRIC}/*.hh)
    file(GLOB_RECURSE PUBLIC_HEADERS_RPC ${PUBLIC_HEADER_DIR_RPC}/*.hh)
    file(GLOB_RECURSE PUBLIC_HEADERS_RPC_2 ${PUBLIC_HEADER_DIR_RPC}/*.hh)

    set(DOXYGEN_INPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Mainpage.md ${PUBLIC_HEADER_DIR_FABRIC}/networklayer ${PUBLIC_HEADER_DIR_RPC}/networklayer ${PUBLIC_HEADER_DIR_RPC_2}/networklayer")
    set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/doxygen)
    set(DOXYGEN_INDEX_FILE ${DOXYGEN_OUTPUT_DIR}/html/index.html)
    set(DOXYFILE_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)

    file(MAKE_DIRECTORY ${DOXYGEN_OUTPUT_DIR}) #Doxygen won't create this for us

    add_custom_command(OUTPUT ${DOXYGEN_INDEX_FILE}
            DEPENDS ${PUBLIC_HEADERS_FABRIC} ${PUBLIC_HEADERS_RPC} ${PUBLIC_HEADERS_RPC_2} ${DOXYFILE_IN} Mainpage.md
            COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
            MAIN_DEPENDENCY ${DOXYFILE_OUT} ${DOXYFILE_IN}
            COMMENT "Generating docs")

    add_custom_target(Doxygen ALL DEPENDS ${DOXYGEN_INDEX_FILE})

    install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/doxygen/html DESTINATION share/doc)

else (DOXYGEN_FOUND)
    message("Doxygen need to be installed to generate the doxygen documentation")
endif (DOXYGEN_FOUND)