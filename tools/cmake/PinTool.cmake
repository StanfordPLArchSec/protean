function(add_pin_tool name source)
  set(headers ${ARGN})
  message("PIN_DIR=${PIN_DIR}")
  if(NOT PIN_DIR)
    message(FATAL_ERROR "PIN_DIR EMPTY")
  endif()
  get_filename_component(source ${source} ABSOLUTE)
  get_filename_component(source_dir ${source} DIRECTORY)
  set(pintooldir "${CMAKE_CURRENT_BINARY_DIR}/${name}.pintool.d")
  make_directory("${pintooldir}")
  set(MyPinTool "${PIN_DIR}/source/tools/MyPinTool")
  foreach(filename IN ITEMS makefile makefile.rules)
    file(COPY_FILE "${MyPinTool}/${filename}" "${pintooldir}/${filename}")
  endforeach()

  set(file_dependencies)
  foreach(header IN LISTS headers)
    get_filename_component(filename ${header} NAME)
    add_custom_command(OUTPUT ${pintooldir}/${filename}
      COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/${filename} ${pintooldir}/${filename}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${filename}
      COMMENT "Copying file ${filename}"
    )
    list(APPEND file_dependencies ${pintooldir}/${filename})
  endforeach()

  get_filename_component(base ${source} NAME_WLE)
  set(so "${CMAKE_CURRENT_BINARY_DIR}/${name}.so")
  set(obj obj-intel64/${base}.so)
  add_custom_command(OUTPUT ${so}
    COMMAND cp "${source}" "${pintooldir}/${base}.cpp"
    COMMAND make -C "${pintooldir}" "PIN_ROOT=${PIN_DIR}" "${obj}" CXX=${CMAKE_CXX_COMPILER} CXXFLAGS="-std=c++20" CPPFLAGS="-I${source_dir}"
    COMMAND cp "${pintooldir}/${obj}" "${so}"
    DEPENDS ${source} ${file_dependencies}
    BYPRODUCTS ${pintooldir}/${obj}
    COMMENT "Building Pintool ${name}"
  )
  add_custom_target(${name} ALL DEPENDS ${so})
  set_target_properties(${name} PROPERTIES LIBRARY ${so})
endfunction()

