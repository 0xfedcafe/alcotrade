#include <drogon/HttpAppFramework.h>
#include <drogon/WebSocketClient.h>
#include <trantor/utils/Logger.h>

#include <fstream>

using namespace std::chrono_literals;

int main(int argc, char *argv[]) {
  std::string server;
  std::string path;
  std::optional<uint16_t> port;
  // Connect to a public echo server
  server = "ws://192.168.100.10";
  port = 9001;
  path = "/trade?team_secret=1b65ebbc-eacd-4bd0-a215-e3be980f38d0";
  std::string serverString;
  serverString = server + ":" + std::to_string(port.value());
  auto wsPtr = drogon::WebSocketClient::newWebSocketClient(serverString);
  auto req = drogon::HttpRequest::newHttpRequest();
  req->setPath(path);
  std::ofstream outputFile("trading_log.json");

  wsPtr->setMessageHandler([&](const std::string &message,
                               const drogon::WebSocketClientPtr &,
                               const drogon::WebSocketMessageType &type) {
    std::string messageType = "Unknown";
    if (type == drogon::WebSocketMessageType::Text) {
      messageType = "text";
      outputFile << message << "\n";
    } else if (type == drogon::WebSocketMessageType::Pong)
      messageType = "pong";
    else if (type == drogon::WebSocketMessageType::Ping)
      messageType = "ping";
    else if (type == drogon::WebSocketMessageType::Binary)
      messageType = "binary";
    else if (type == drogon::WebSocketMessageType::Close)
      messageType = "Close";
  });

  wsPtr->setConnectionClosedHandler([](const drogon::WebSocketClientPtr &) {
    LOG_INFO << "WebSocket connection closed!";
  });

  // LOG_DEBUG << "Connecting to WebSocket at " << server;
  wsPtr->connectToServer(
      req, [](drogon::ReqResult r, const drogon::HttpResponsePtr &,
              const drogon::WebSocketClientPtr &wsPtr) {
        if (r != drogon::ReqResult::Ok) {
          // LOG_ERROR << "Failed to establish WebSocket connection!";
          wsPtr->stop();
          return;
        }
        // LOG_DEBUG << "WebSocket connected!";
        wsPtr->getConnection()->setPingMessage("", 2s);
        wsPtr->getConnection()->send("hello!");
      });

  // Quit the application after 15 seconds
  // drogon::app().getLoop()->runAfter(15, []() { drogon::app().quit(); });

  drogon::app().setLogLevel(trantor::Logger::kInfo);
  drogon::app().run();
  LOG_ERROR << "bye!";
  return 0;
  return 0;
}
