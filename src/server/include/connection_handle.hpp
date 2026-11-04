#pragma once

#include "../../network/rpc.hpp"
#include "../../network/socket_io.hpp"
#include "../chunk_server/chunk_store.hpp"

namespace dfg::server {

void handle_client_connection(int client_fd, dfg::storage::ChunkStore& store);

} // namespace dfg::server
