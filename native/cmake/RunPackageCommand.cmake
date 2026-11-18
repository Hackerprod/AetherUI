# Shared by the local gate and its failure-propagation self-test.
function(package_run stage directory)
    string(REPLACE ";" "\n  " display "${ARGN}")
    message(STATUS "${stage}:\n  ${display}")
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${directory}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    file(WRITE "${WORK_DIR}/${stage}.log" "COMMAND:\n  ${display}\nRESULT: ${result}\n${output}\n${error}")
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "Stage ${stage} failed (${result}); see ${WORK_DIR}/${stage}.log")
    endif()
    set(PACKAGE_OUTPUT "${output}\n${error}" PARENT_SCOPE)
endfunction()
