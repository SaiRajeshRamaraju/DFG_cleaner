#include "../utils/arg_parser.hpp"
#include "connection.hpp"

#include <cstdlib>
#include <iostream>

int main(int argc, char **argv) {
  argparse::ArgumentParser program("dfg-client");

  program.add_argument("--action", "-a")
      .help("Action to perform: upload, download, ping")
      .required()
      .choices("upload", "download", "ping");

  program.add_argument("--file", "-f")
      .help("File path (upload) or File name (download)");

  program.add_argument("--host").help("Server host address").required();

  program.add_argument("--port", "-p")
      .help("Server port number")
      .required()
      .scan<'i', int>();
  program.add_argument("--list", "-l").help("List all files stored in server");

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception &err) {
    std::cerr << err.what() << "\n";
    std::cerr << program;
    return EXIT_FAILURE;
  }

  auto action = program.get<std::string>("--action");
  auto host = program.get<std::string>("--host");
  auto port = program.get<int>("--port");

  if (action == "upload" || action == "download") {
    if (!program.present("--file")) {
      std::cerr << "Error: --file is required when --action is '" << action
                << "'\n";
      return EXIT_FAILURE;
    }

    auto file = program.get<std::string>("--file");

    if (action == "upload")
      return client::file_upload(file, host, port);
    else
      return client::file_download(file, host, port);
  }

  client::ping(host, port);
  return EXIT_SUCCESS;
}
