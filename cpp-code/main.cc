#include <drogon/HttpAppFramework.h>
#include <drogon/WebSocketClient.h>
#include <trantor/utils/Logger.h>

#include <fstream>

#include "include/TradingWebSocketClient.hpp"

using namespace std::chrono_literals;

int main(int argc, char *argv[]) {
  // Create trading client
  // TradingWebSocketClient client("ws://192.168.100.10", 9001);
  TradingWebSocketClient client("ws://192.168.100.10", 9001, "price_model.bin", "direction_model.bin");

  // Connect
  client.connect("/trade?team_secret=1b65ebbc-eacd-4bd0-a215-e3be980f38d0");

  drogon::app().setLogLevel(trantor::Logger::kInfo);
  drogon::app().run();
  LOG_ERROR << "bye!";
  return 0;
}