include(CheckCCompilerFlag)

function(check_compiler_flag language flag result)
  if(NOT language STREQUAL "C")
    message(FATAL_ERROR "Unsupported compiler flag check language: ${language}")
  endif()
  check_c_compiler_flag("${flag}" "${result}")
endfunction()
