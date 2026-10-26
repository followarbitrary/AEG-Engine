cmake_minimum_required(VERSION 3.25)

foreach(parameter GENERATOR WORK_DIR)
    if(NOT DEFINED ${parameter} OR "${${parameter}}" STREQUAL "")
        message(FATAL_ERROR "Embedded shader generator acceptance requires ${parameter}")
    endif()
endforeach()
if(NOT EXISTS "${GENERATOR}" OR IS_DIRECTORY "${GENERATOR}")
    message(FATAL_ERROR "Embedded shader converter is missing: ${GENERATOR}")
endif()
if(NOT IS_ABSOLUTE "${WORK_DIR}")
    message(FATAL_ERROR "WORK_DIR must be an absolute build directory")
endif()

# Retain unique inputs and outputs for evidence; never clear a caller-provided path.
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef run_id)
set(stage "${WORK_DIR}/generator-${run_id}")
if(EXISTS "${stage}")
    message(FATAL_ERROR "Generator acceptance directory already exists: ${stage}")
endif()
file(MAKE_DIRECTORY "${stage}")
message(STATUS "Generator acceptance evidence: ${stage}")

# Use only nonzero binary bytes so this fixture is portable in CMake strings.
# Five words: the magic followed by four distinctly ordered little-endian words.
string(ASCII 3 2 35 7 magic)
string(ASCII 1 2 3 4 word)
set(valid "${magic}${word}${word}${word}${word}")
file(WRITE "${stage}/valid.spv" "${valid}")
file(WRITE "${stage}/empty.spv" "")
file(WRITE "${stage}/unaligned.spv" "${valid}x")
file(WRITE "${stage}/short.spv" "${magic}")
file(WRITE "${stage}/badmagic.spv" "${word}${word}${word}${word}${word}")

# First prove the converter can run, avoiding a missing/broken script making all
# rejection cases pass just because every invocation fails.
execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DINPUT=${stage}/valid.spv" "-DOUTPUT=${stage}/valid.hpp" "-DSYMBOL=acceptance_shader"
    -P "${GENERATOR}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
if(NOT "${result}" STREQUAL "0" OR NOT EXISTS "${stage}/valid.hpp")
    message(FATAL_ERROR "Valid converter input failed: ${result}\n${output}\n${errors}")
endif()
file(READ "${stage}/valid.hpp" header)
string(TOLOWER "${header}" header_lower)
if(NOT header MATCHES "namespace aeg::embedded_shaders" OR
   NOT header MATCHES "inline constexpr std::array<std::uint32_t, *5> acceptance_shader" OR
   NOT header_lower MATCHES "0x07230203" OR NOT header_lower MATCHES "0x04030201")
    message(FATAL_ERROR "Generated array does not preserve namespace, size, symbol, or little-endian words:\n${header}")
endif()

function(expect_rejection name input symbol)
    set(destination "${stage}/${name}.hpp")
    set(sentinel "existing output must survive rejected input\n")
    file(WRITE "${destination}" "${sentinel}")
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DINPUT=${input}" "-DOUTPUT=${destination}" "-DSYMBOL=${symbol}" -P "${GENERATOR}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
    file(WRITE "${stage}/${name}.log" "result: ${result}\n${output}\n${errors}")
    if(NOT "${result}" MATCHES "^[0-9]+$")
        message(FATAL_ERROR "Converter did not reject ${name} normally: ${result}\n${output}\n${errors}")
    endif()
    if("${result}" STREQUAL "0")
        message(FATAL_ERROR "Converter accepted ${name}:\n${output}\n${errors}")
    endif()
    file(READ "${destination}" contents)
    if(NOT contents STREQUAL sentinel)
        message(FATAL_ERROR "Converter changed existing output when rejecting ${name}")
    endif()
    message(STATUS "PASS generator rejects ${name}")
endfunction()

expect_rejection(missing "${stage}/missing.spv" acceptance_shader)
expect_rejection(empty "${stage}/empty.spv" acceptance_shader)
expect_rejection(unaligned "${stage}/unaligned.spv" acceptance_shader)
expect_rejection(short-header "${stage}/short.spv" acceptance_shader)
expect_rejection(bad-magic "${stage}/badmagic.spv" acceptance_shader)
expect_rejection(invalid-symbol-dot "${stage}/valid.spv" "bad.symbol")
expect_rejection(invalid-symbol-leading-digit "${stage}/valid.spv" "1bad")
