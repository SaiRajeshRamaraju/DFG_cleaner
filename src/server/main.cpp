#include "../utils/arg_parser.hpp"
#include "chunk_server/server.hpp"

#include <cstdlib>
#include <iostream>

int main(int argc, char **argv) {
  argparse::ArgumentParser program("dfg-server");
  program.add_argument("--type", "-t")
      .help("Type of server head/chunk")
      .required();

  program.add_argument("--port", "-p")
      .help("TCP port to listen on")
      .required()
      .scan<'i', int>();

  program.add_argument("--storage", "-s")
      .help("Directory for on-disk chunk storage")
      .default_value(std::string("./dfg_chunks"));

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    std::cerr << program;
    return EXIT_FAILURE;
  }

  auto port = static_cast<uint16_t>(program.get<int>("--port"));
  auto storage = program.get<std::string>("--storage");

  return dfg::server::run_chunk_server(port, storage);
}
