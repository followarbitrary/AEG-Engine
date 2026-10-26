#pragma once
#include <EditorBackend.hpp>
namespace aeg::app {
editor::EditorBackend createGraphicsBackend(const rhi::NativeWindow& window, bool validation);
}
