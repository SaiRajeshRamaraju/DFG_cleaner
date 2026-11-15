#include "../utils/arg_parser.hpp"
#include "./chunk_server/server.cpp"
#include "./head_server/server.cpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

int main(int argc, char **argv) {
  argparse::ArgumentParser program("dfg-server");
  program.add_argument("--type", "-t")
      .help("Type of server head/chunk")
      .choices("head", "chunk") // come with better names
      .required();

  program.add_argument("--port", "-p")
      .help("TCP port to listen on")
      .required()
      .scan<'i', int>();

  program.add_argument("--storage", "-s")
      .help("Directory for on-disk chunk storage.Default path to use "
            "/var/dfg/chunks.")
      .default_value(std::string("/var/dfg/chunks"));

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    std::cerr << program;
    return EXIT_FAILURE;
  }

  auto port = static_cast<uint16_t>(program.get<int>("--port"));
  auto storage = program.get<std::string>("--storage");
  auto server_type = program.get<std::string>("--type");

  if (server_type == "head") {
    int status = dfg::head_server::run_head_server(port);
    if (status != 0) {
      std::cerr << "Head Server stopped. Error:" << std::strerror(errno)
                << std::endl;
      return -1;
    }
  } else {
    int status = dfg::cluster_server::run_chunk_server(port, storage);
    if (status != 0) {
      std::cerr << "Chunk Server stopped. Error:" << std::strerror(errno)
                << std::endl;
      return -1;
    }
  }

  return 0;
}
