/**
 * @file backend_client.h
 * @brief Client for the local backend command protocol.
 */

#pragma once
#include "hub.h"

namespace client {
/**
 * @brief Send a framed command to the local backend and decode its response.
 * @return Reply containing response fields, or an error describing transport/protocol failure.
 */
Reply SendBackendCommand(std::string_view command, Arguments arguments);
}
