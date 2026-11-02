#include "../utils/arg_parser.hpp"
#include "./connection.cpp"
#include <iostream>

int main(int argc, char **argv) {
  argparse::ArgumentParser client("client");

  client.add_argument("--action", "-a")
      .help("Action to perform: send, download, ping")
      .required()
      .choices("upload", "download", "ping");

  client.add_argument("--file", "-f")
      .help("File path to send or remote file name to download");

  client.add_argument("--host").help("Server host address").required();

  client.add_argument("--port", "-p")
      .help("Server port number")
      .required()
      .scan<'i', int>();

  try {
    client.parse_args(argc, argv);
  } catch (const std::exception &err) {
    std::cerr << err.what() << std::endl;
    std::cerr << client;
    return 1;
  }

  auto action = client.get<std::string>("--action");
  auto host = client.get<std::string>("--host");
  auto port = client.get<int>("--port");

  if (action == "upload" || action == "download") {
    if (!client.present("--file")) {
      std::cerr << "Error: --file is required when --action is '" << action
                << "'" << std::endl;
      return 1;
    } else {
      auto file = client.get<std::string>("--file");
      if (action == "upload") {
        if (client::file_upload(file, host, port) == 1) {
          std::cerr << "File upload failed" << std::endl;
          return 1;
        }

      } else {
        if (client::file_download(file, host, port) == 1) {
          std::cerr << "File download failed" << std::endl;
          return 1;
        }
      }
    }
  }

  else {
    client::ping(host, port);
  }

  return 0;
}
