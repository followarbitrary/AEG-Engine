# Targeted, reproducible fixes for the pinned Assimp 5.4.3 MMD converter.
# Include after FetchContent_MakeAvailable while holding the dependency lock.
# Both sides of every replacement live here so the source change is reversible.
if(NOT DEFINED assimp_SOURCE_DIR)
    message(FATAL_ERROR "The Assimp MMD patch requires populated Assimp sources")
endif()
file(STRINGS "${assimp_SOURCE_DIR}/CMakeLists.txt" _aeg_assimp_version
    REGEX "^PROJECT\\(Assimp VERSION 5\\.4\\.3\\)$")
if(NOT _aeg_assimp_version)
    message(FATAL_ERROR "Re-review the AEG MMD patch before changing Assimp 5.4.3")
endif()

set(_aeg_mmd_path "${assimp_SOURCE_DIR}/code/AssetLib/MMD/MMDImporter.cpp")
file(READ "${_aeg_mmd_path}" _aeg_mmd_source)
set(_aeg_mmd_original "${_aeg_mmd_source}")

# Accept one exact original anchor or one exact already-patched anchor. A
# partially applied, duplicated, or changed patch fails configuration visibly.
function(_aeg_patch_mmd before after label)
    string(FIND "${_aeg_mmd_source}" "${after}" patched_at)
    if(NOT patched_at EQUAL -1)
        string(REPLACE "${after}" "" remainder "${_aeg_mmd_source}")
        string(LENGTH "${_aeg_mmd_source}" source_length)
        string(LENGTH "${remainder}" remainder_length)
        string(LENGTH "${after}" anchor_length)
        math(EXPR removed_length "${source_length} - ${remainder_length}")
        string(FIND "${remainder}" "${before}" original_at)
        if(NOT removed_length EQUAL anchor_length OR NOT original_at EQUAL -1)
            message(FATAL_ERROR "Ambiguous Assimp MMD patch anchor: ${label}")
        endif()
        return()
    endif()
    string(FIND "${_aeg_mmd_source}" "${before}" original_at)
    if(original_at EQUAL -1)
        message(FATAL_ERROR "Missing Assimp MMD patch anchor: ${label}")
    endif()
    string(REPLACE "${before}" "" remainder "${_aeg_mmd_source}")
    string(LENGTH "${_aeg_mmd_source}" source_length)
    string(LENGTH "${remainder}" remainder_length)
    string(LENGTH "${before}" anchor_length)
    math(EXPR removed_length "${source_length} - ${remainder_length}")
    if(NOT removed_length EQUAL anchor_length)
        message(FATAL_ERROR "Duplicate Assimp MMD patch anchor: ${label}")
    endif()
    string(REPLACE "${before}" "${after}" updated "${_aeg_mmd_source}")
    set(_aeg_mmd_source "${updated}" PARENT_SCOPE)
endfunction()

_aeg_patch_mmd(
    "#include \"AssetLib/MMD/MMDPmxParser.h\"\n"
    "#include \"AssetLib/MMD/MMDPmxParser.h\"\n#include \"AssetLib/MMD/AEGMmdValidation.hpp\"\n"
    "validation header")

_aeg_patch_mmd([=[    if (pModel == nullptr) {
        return;
    }

    aiNode *pNode = new aiNode;]=]
    [=[    if (pModel == nullptr) {
        return;
    }

    aeg_assimp_patch::validate(*pModel);

    aiNode *pNode = new aiNode;]=]
    "validate before scene conversion")

_aeg_patch_mmd(
    "        pMesh->mNumUVComponents[i] = 4;"
    "        pMesh->mNumUVComponents[i] = 3;"
    "additional UV component count")

_aeg_patch_mmd([=[            // TODO: wrong here? use quaternion transform?
            pMesh->mTextureCoords[i][index].x = v->uva[i][0];
            pMesh->mTextureCoords[i][index].y = v->uva[i][1];]=]
    [=[            // aiVector3D preserves xyz; PMX additional UV w is unused here.
            pMesh->mTextureCoords[i][index].x = v->uva[i - 1][0];
            pMesh->mTextureCoords[i][index].y = v->uva[i - 1][1];
            pMesh->mTextureCoords[i][index].z = v->uva[i - 1][2];]=]
    "additional UV array indexing")

configure_file("${CMAKE_CURRENT_LIST_DIR}/patches/AssimpMmdValidation.hpp"
    "${assimp_SOURCE_DIR}/code/AssetLib/MMD/AEGMmdValidation.hpp" COPYONLY)
if(NOT _aeg_mmd_source STREQUAL _aeg_mmd_original)
    file(WRITE "${_aeg_mmd_path}" "${_aeg_mmd_source}")
endif()

unset(_aeg_assimp_version)
unset(_aeg_mmd_path)
unset(_aeg_mmd_source)
unset(_aeg_mmd_original)
