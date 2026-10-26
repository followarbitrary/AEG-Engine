# Keep an owned rotation/scale gesture alive outside its ImGui window. The pinned
# upstream handlers otherwise return before both movement and release handling.
# Full-source hashes reject an unreviewed dependency update and make this patch
# idempotent across shared-source Debug, Release and MinGW configurations.
if(NOT DEFINED imguizmo_SOURCE_DIR)
    message(FATAL_ERROR "The ImGuizmo patch requires populated sources")
endif()
set(_aeg_gizmo_path "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp")
file(READ "${_aeg_gizmo_path}" _aeg_gizmo_source)
string(SHA256 _aeg_gizmo_hash "${_aeg_gizmo_source}")
if(_aeg_gizmo_hash STREQUAL "ca5f67e9fbafeac9940df18a141d87ddfd93d2b10a5ce6a5e8e0938d2a71bcfa")
    return()
endif()
if(NOT _aeg_gizmo_hash STREQUAL "ad7ffeeacc9753e804d3b77f427c6629983ed19dcc9e15f77799568199551ba0")
    message(FATAL_ERROR "Re-review the AEG gesture patch: ImGuizmo 1.10 differs from the pinned original or patch")
endif()
string(REPLACE "type != MT_NONE || !gContext.mbMouseOver)"
    "type != MT_NONE || (!gContext.mbMouseOver && !gContext.mbUsing))"
    _aeg_gizmo_source "${_aeg_gizmo_source}")
string(SHA256 _aeg_gizmo_updated_hash "${_aeg_gizmo_source}")
if(NOT _aeg_gizmo_updated_hash STREQUAL "ca5f67e9fbafeac9940df18a141d87ddfd93d2b10a5ce6a5e8e0938d2a71bcfa")
    message(FATAL_ERROR "AEG ImGuizmo gesture patch did not produce the expected source")
endif()
file(WRITE "${_aeg_gizmo_path}" "${_aeg_gizmo_source}")
unset(_aeg_gizmo_path)
unset(_aeg_gizmo_source)
unset(_aeg_gizmo_hash)
unset(_aeg_gizmo_updated_hash)
