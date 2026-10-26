#include "CommandLine.hpp"
#include "GraphicsBackend.hpp"

int main(int argc, char** argv) { return aeg::app::runCommandLine(argc, argv, aeg::app::createGraphicsBackend); }
