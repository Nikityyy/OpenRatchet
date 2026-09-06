include_guard(GLOBAL)

function(openratchet_prepare_ps2recomp upstream_dir patch_file revision_file output_dir)
    find_package(Git REQUIRED)

    if(NOT EXISTS "${upstream_dir}/CMakeLists.txt")
        message(FATAL_ERROR "PS2Recomp checkout missing at '${upstream_dir}'.")
    endif()
    if(NOT EXISTS "${patch_file}")
        message(FATAL_ERROR "OpenRatchet PS2Recomp compatibility patch missing: '${patch_file}'.")
    endif()
    if(NOT EXISTS "${revision_file}")
        message(FATAL_ERROR "OpenRatchet PS2Recomp revision file missing: '${revision_file}'.")
    endif()

    file(READ "${revision_file}" expected_revision)
    string(STRIP "${expected_revision}" expected_revision)
    if(expected_revision STREQUAL "")
        message(FATAL_ERROR "PS2Recomp revision file is empty: '${revision_file}'.")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${upstream_dir}" rev-parse HEAD
        RESULT_VARIABLE rev_result
        OUTPUT_VARIABLE actual_revision
        ERROR_VARIABLE rev_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT rev_result EQUAL 0)
        message(FATAL_ERROR
            "PS2Recomp must be a Git checkout so OpenRatchet can build from an immutable HEAD archive. "
            "git rev-parse failed: ${rev_error}")
    endif()
    if(NOT actual_revision STREQUAL expected_revision)
        message(FATAL_ERROR
            "Unsupported PS2Recomp revision. Expected ${expected_revision}, got ${actual_revision}. "
            "Checkout the pinned revision before building OpenRatchet.")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${upstream_dir}" status --porcelain --untracked-files=all
        RESULT_VARIABLE status_result
        OUTPUT_VARIABLE status_output
        ERROR_VARIABLE status_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT status_result EQUAL 0)
        message(FATAL_ERROR "Could not inspect PS2Recomp checkout status: ${status_error}")
    endif()
    if(NOT status_output STREQUAL "")
        message(FATAL_ERROR
            "third_party/PS2Recomp must remain clean. OpenRatchet never builds from modified third-party sources.\n"
            "Current PS2Recomp status:\n${status_output}\n"
            "Restore/remove those local changes, then configure again. The OpenRatchet compatibility patch is applied only to a build-local copy.")
    endif()

    file(SHA256 "${patch_file}" patch_sha256)
    set(state_file "${output_dir}.openratchet-state")
    set(expected_state "revision=${actual_revision}\npatch=${patch_sha256}\n")

    set(reuse_existing FALSE)
    if(EXISTS "${output_dir}/CMakeLists.txt" AND EXISTS "${state_file}")
        file(READ "${state_file}" existing_state)
        if(existing_state STREQUAL expected_state)
            set(reuse_existing TRUE)
        endif()
    endif()

    if(reuse_existing)
        message(STATUS "Using prepared PS2Recomp compatibility source: ${output_dir}")
        return()
    endif()

    message(STATUS
        "Preparing immutable PS2Recomp ${actual_revision} with OpenRatchet compatibility patch")

    get_filename_component(output_parent "${output_dir}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_parent}")
    file(REMOVE_RECURSE "${output_dir}")
    file(MAKE_DIRECTORY "${output_dir}")

    set(archive_file "${output_dir}.upstream.tar")
    file(REMOVE "${archive_file}")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${upstream_dir}" archive --format=tar -o "${archive_file}" HEAD
        RESULT_VARIABLE archive_result
        ERROR_VARIABLE archive_error)
    if(NOT archive_result EQUAL 0)
        file(REMOVE_RECURSE "${output_dir}")
        message(FATAL_ERROR "Failed to archive pristine PS2Recomp HEAD: ${archive_error}")
    endif()

    file(ARCHIVE_EXTRACT INPUT "${archive_file}" DESTINATION "${output_dir}")
    file(REMOVE "${archive_file}")

    # `git apply` provides a strict context check. The temporary repository exists
    # only inside build/ and is deleted immediately after the patch is applied.
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" init -q
        WORKING_DIRECTORY "${output_dir}"
        RESULT_VARIABLE init_result
        ERROR_VARIABLE init_error)
    if(NOT init_result EQUAL 0)
        file(REMOVE_RECURSE "${output_dir}")
        message(FATAL_ERROR "Failed to initialize build-local patch workspace: ${init_error}")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --check "${patch_file}"
        WORKING_DIRECTORY "${output_dir}"
        RESULT_VARIABLE patch_check_result
        ERROR_VARIABLE patch_check_error)
    if(NOT patch_check_result EQUAL 0)
        file(REMOVE_RECURSE "${output_dir}")
        message(FATAL_ERROR
            "OpenRatchet's PS2Recomp compatibility patch no longer applies cleanly to ${actual_revision}:\n${patch_check_error}")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply "${patch_file}"
        WORKING_DIRECTORY "${output_dir}"
        RESULT_VARIABLE patch_result
        ERROR_VARIABLE patch_error)
    if(NOT patch_result EQUAL 0)
        file(REMOVE_RECURSE "${output_dir}")
        message(FATAL_ERROR "Failed to apply OpenRatchet PS2Recomp compatibility patch: ${patch_error}")
    endif()

    file(REMOVE_RECURSE "${output_dir}/.git")
    file(WRITE "${state_file}" "${expected_state}")
endfunction()
