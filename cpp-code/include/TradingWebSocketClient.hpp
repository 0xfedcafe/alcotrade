#pragma once
#include <drogon/WebSocketClient.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <trantor/utils/Logger.h>

#include <fstream>
#include <map>
#include <optional>

#include "candle_data.hpp"
#include "consts.hpp"
#include "events.hpp"
#include "orderbook_level.hpp"

class TradingWebSocketClient {
 private:
  drogon::WebSocketClientPtr wsClient;
  std::ofstream outputFile;
  std::ofstream csvUnderlyingPrices;

 public:
  TradingWebSocketClient(const std::string& server, uint16_t port)
      : outputFile("trading_log.json"), csvUnderlyingPrices("dataframe.csv") {
    std::string serverString = server + ":" + std::to_string(port);
    wsClient = drogon::WebSocketClient::newWebSocketClient(serverString);
    csvUnderlyingPrices << "Tick,";
    csvUnderlyingPrices << "$CARD open,$CARD close,$CARD high,$CARD low,$CARD "
                           "volume,$CARD index,$CARD mid,";
    csvUnderlyingPrices << "$LOGN open,$LOGN close,$LOGN high,$LOGN low,$LOGN "
                           "volume,$LOGN index,$LOGN mid,";
    csvUnderlyingPrices << "$HEST open,$HEST close,$HEST high,$HEST low,$HEST "
                           "volume,$HEST index,$HEST mid,";
    csvUnderlyingPrices << "$JUMP open,$JUMP close,$JUMP high,$JUMP low,$JUMP "
                           "volume,$JUMP index,$JUMP mid,";
    csvUnderlyingPrices << "$GARR open,$GARR close,$GARR high,$GARR low,$GARR "
                           "volume,$GARR index,$GARR mid,";
    csvUnderlyingPrices << "$SIMP open,$SIMP close,$SIMP high,$SIMP low,$SIMP "
                           "volume,$SIMP index,$SIMP mid\n";

    wsClient->setMessageHandler(
        [this](const std::string& message, const drogon::WebSocketClientPtr&,
               const drogon::WebSocketMessageType& type) {
          this->onMessage(message, type);
        });

    wsClient->setConnectionClosedHandler(
        [this](const drogon::WebSocketClientPtr&) {
          this->onConnectionClosed();
        });
  }

  void connect(const std::string& path) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setPath(path);

    wsClient->connectToServer(
        req, [this](drogon::ReqResult r, const drogon::HttpResponsePtr&,
                    const drogon::WebSocketClientPtr& wsPtr) {
          if (r != drogon::ReqResult::Ok) {
            wsPtr->stop();
            return;
          }
          this->onConnected(wsPtr);
        });
  }

 protected:
  virtual void onMessage(const std::string& message,
                         const drogon::WebSocketMessageType& type) {
    if (type == drogon::WebSocketMessageType::Text) {
      outputFile << message << "\n";
      outputFile.flush();
      parseJsonMessage(message);
    }
  }

  virtual void onConnected(const drogon::WebSocketClientPtr& wsPtr) {
    LOG_INFO << "WebSocket connected!";
    wsPtr->getConnection()->setPingMessage("", std::chrono::seconds(2));
    wsPtr->getConnection()->send("hello!");
  }

  virtual void onConnectionClosed() {
    LOG_INFO << "WebSocket connection closed!";
  }

  std::array<std::optional<CandleData>, 6> candlesTick;

 private:
  void parseJsonMessage(const std::string& message) {
    try {
      rapidjson::Document doc;

      // In-situ parsing for maximum speed (modifies the input string)
      char* buffer = const_cast<char*>(message.c_str());
      doc.ParseInsitu(buffer);

      if (doc.HasParseError()) {
        LOG_WARN << "JSON parse error: "
                 << rapidjson::GetParseError_En(doc.GetParseError());
        return;
      }

      processMarketDataUpdate(doc);
    } catch (const std::exception& e) {
      LOG_ERROR << "JSON parsing error: " << e.what();
    }
  }

  void processMarketDataUpdate(const rapidjson::Document& json) {
    if (!json.HasMember("type") || !json["type"].IsString() ||
        strcmp(json["type"].GetString(), "market_data_update") != 0) {
      LOG_WARN << "Unknown message type or missing type field";
      return;
    }

    long timestamp = json.HasMember("time") && json["time"].IsInt64()
                         ? json["time"].GetInt64()
                         : 0;
    LOG_INFO << "Processing market data update at time: " << timestamp;

    // Process candles data
    if (json.HasMember("candles") && json["candles"].IsObject()) {
      processCandlesData(json["candles"], timestamp);
    }

    // Process orderbook depths
    if (json.HasMember("orderbook_depths") &&
        json["orderbook_depths"].IsObject()) {
      processOrderbookData(json["orderbook_depths"], timestamp);
    }

    // Process events
    if (json.HasMember("events") && json["events"].IsArray()) {
      processEventsData(json["events"], timestamp);
    }
  }

  void processCandlesData(const rapidjson::Value& candles, long timestamp) {
    // Process tradeable instruments
    if (candles.HasMember("tradeable") && candles["tradeable"].IsObject()) {
      const rapidjson::Value& tradeable = candles["tradeable"];

      for (auto& member : tradeable.GetObject()) {
        const char* instrumentName = member.name.GetString();
        const rapidjson::Value& candleArray = member.value;

        if (candleArray.IsArray() && candleArray.Size() > 0 &&
            candleArray[0].IsObject()) {
          CandleData candle(candleArray[0]);
          onCandleUpdate(instrumentName, candle, timestamp, true);
        }
      }
    }

    // Process untradeable instruments
    if (candles.HasMember("untradeable") && candles["untradeable"].IsObject()) {
      std::fill(candlesTick.begin(), candlesTick.end(), std::nullopt);
      const rapidjson::Value& untradeable = candles["untradeable"];

      for (auto& member : untradeable.GetObject()) {
        const char* instrumentName = member.name.GetString();
        const rapidjson::Value& candleArray = member.value;

        if (candleArray.IsArray() && candleArray.Size() > 0 &&
            candleArray[0].IsObject()) {
          CandleData candle(candleArray[0]);
          onCandleUpdate(instrumentName, candle, timestamp, false);
          candlesTick[getIndex(instrumentName)] = candle;
        }
      }
    }
    CandleData::dumpCandles(csvUnderlyingPrices, timestamp, candlesTick);
  }

  void processOrderbookData(const rapidjson::Value& orderbooks,
                            long timestamp) {
    for (auto& member : orderbooks.GetObject()) {
      const char* instrumentName = member.name.GetString();
      const rapidjson::Value& orderbookData = member.value;

      if (orderbookData.IsObject()) {
        OrderBookLevel orderbook(orderbookData);
        onOrderbookUpdate(instrumentName, orderbook, timestamp);
      }
    }
  }

  void processEventsData(const rapidjson::Value& events, long timestamp) {
    for (rapidjson::SizeType i = 0; i < events.Size(); ++i) {
      const rapidjson::Value& event = events[i];

      if (!event.IsObject() || !event.HasMember("event_type") ||
          !event["event_type"].IsString()) {
        continue;
      }

      const char* eventType = event["event_type"].GetString();

      if (strcmp(eventType, "trade") == 0 && event.HasMember("data") &&
          event["data"].IsObject()) {
        TradeEvent trade(event["data"]);
        onTradeEvent(trade);
      } else if (strcmp(eventType, "cancel") == 0 && event.HasMember("data") &&
                 event["data"].IsObject()) {
        CancelEvent cancel(event["data"]);
        onCancelEvent(cancel);
      }
    }
  }

  // Override these methods to handle specific data
  virtual void onCandleUpdate(const std::string& instrument,
                              const CandleData& candle, long timestamp,
                              bool tradeable) {
    LOG_INFO << "Candle update for " << instrument << " - O:" << candle.open
             << " H:" << candle.high << " L:" << candle.low
             << " C:" << candle.close << " V:" << candle.volume
             << (tradeable ? " (tradeable)" : " (untradeable)");
  }

  virtual void onOrderbookUpdate(const std::string& instrument,
                                 const OrderBookLevel& orderbook,
                                 long timestamp) {
    LOG_INFO << "Orderbook update for " << instrument
             << " - Bids:" << orderbook.bids.size()
             << " Asks:" << orderbook.asks.size();

    // Log best bid/ask if available
    if (!orderbook.bids.empty()) {
      auto bestBid = orderbook.bids.rbegin();  // highest price
      LOG_INFO << "  Best bid: " << bestBid->first << " x " << bestBid->second;
    }
    if (!orderbook.asks.empty()) {
      auto bestAsk = orderbook.asks.begin();  // lowest price
      LOG_INFO << "  Best ask: " << bestAsk->first << " x " << bestAsk->second;
    }
  }

  virtual void onTradeEvent(const TradeEvent& trade) {
    LOG_INFO << "Trade: " << trade.instrumentID << " " << trade.quantity << "@"
             << trade.price << " (passive:" << trade.passiveOrderID
             << " active:" << trade.activeOrderID << ")";
  }

  virtual void onCancelEvent(const CancelEvent& cancel) {
    LOG_INFO << "Cancel: Order " << cancel.orderID << " for "
             << cancel.instrumentID
             << (cancel.expired ? " (expired)" : " (manual)");
  }
};