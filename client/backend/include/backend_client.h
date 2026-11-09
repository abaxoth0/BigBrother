#pragma once
#include "hub.h"

namespace client {
Reply SendBackendCommand(std::string_view command, Arguments arguments);
}
