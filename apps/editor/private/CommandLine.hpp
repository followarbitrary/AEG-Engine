#pragma once
#include <EditorBackend.hpp>

namespace aeg::app {
int runCommandLine(int argc, char** argv, const editor::EditorBackendFactory& factory);
}
