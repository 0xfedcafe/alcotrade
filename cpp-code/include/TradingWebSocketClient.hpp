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
  std::ofstream csvTradeableInstruments;  // New CSV for ML training
  std::ofstream csvOrderbookData;         // New CSV for orderbook data
  std::ofstream csvTradeEvents;           // New CSV for trade events

 public:
  TradingWebSocketClient(const std::string& server, uint16_t port)
      : outputFile("trading_log.json"),
        csvUnderlyingPrices("dataframe.csv"),
        csvOrderbookData("orderbook_data.csv"),
        csvTradeableInstruments("tradable_instrs.csv"),
        csvTradeEvents("trade_events.csv") {
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

    csvTradeableInstruments << "timestamp,instrument_id,instrument_type,"
                               "underlying,strike_price,expiry,";
    csvTradeableInstruments << "open,close,high,low,volume,index,";
    csvTradeableInstruments << "underlying_price,time_to_expiry,moneyness\n";

    // Headers for orderbook data
    csvOrderbookData
        << "timestamp,instrument_id,side,price_level,price,quantity,";
    csvOrderbookData << "spread,mid_price,bid_ask_ratio,depth_imbalance\n";

    // Headers for trade events
    csvTradeEvents << "timestamp,instrument_id,price,quantity,";
    csvTradeEvents << "passive_order_id,active_order_id,trade_direction\n";

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
          dumpTradeableInstrument(timestamp, instrumentName, candle);
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
        dumpOrderbookFeatures(timestamp, instrumentName, orderbook);
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
        dumpTradeFeatures(trade);
      } else if (strcmp(eventType, "cancel") == 0 && event.HasMember("data") &&
                 event["data"].IsObject()) {
        CancelEvent cancel(event["data"]);
        onCancelEvent(cancel);
      }
    }
  }

  void dumpTradeableInstrument(long timestamp,
                               const std::string& instrumentName,
                               const CandleData& candle) {
    // Parse instrument name to extract features
    auto [instrumentType, underlying, strikePrice, expiry] =
        parseInstrumentName(instrumentName);

    // Get underlying price
    int underlyingIndex = getIndex(underlying);
    double underlyingPrice = 0;
    if (underlyingIndex < candlesTick.size() &&
        candlesTick[underlyingIndex].has_value()) {
      underlyingPrice = candlesTick[underlyingIndex]->mid;
    }

    // Calculate time to expiry (in seconds)
    long timeToExpiry = expiry - timestamp;

    // Calculate moneyness (for options)
    double moneyness = 0;
    if (instrumentType != "future" && underlyingPrice > 0) {
      moneyness = static_cast<double>(strikePrice) / underlyingPrice;
    }

    csvTradeableInstruments
        << timestamp << "," << instrumentName << "," << instrumentType << ","
        << underlying << "," << strikePrice << "," << expiry << ","
        << candle.open << "," << candle.close << "," << candle.high << ","
        << candle.low << "," << candle.volume << "," << candle.index << ","
        << underlyingPrice << "," << timeToExpiry << "," << moneyness << "\n";
    csvTradeableInstruments.flush();
  }

  void dumpOrderbookFeatures(long timestamp, const std::string& instrumentName,
                             const OrderBookLevel& orderbook) {
    // Calculate orderbook features
    double spread = 0, midPrice = 0, bidAskRatio = 0, depthImbalance = 0;

    if (!orderbook.bids.empty() && !orderbook.asks.empty()) {
      int bestBid = orderbook.bids.rbegin()->first;
      int bestAsk = orderbook.asks.begin()->first;
      spread = bestAsk - bestBid;
      midPrice = (bestBid + bestAsk) / 2.0;

      // Calculate bid/ask volume ratio
      int totalBidVolume = 0, totalAskVolume = 0;
      for (const auto& [price, qty] : orderbook.bids) totalBidVolume += qty;
      for (const auto& [price, qty] : orderbook.asks) totalAskVolume += qty;

      if (totalAskVolume > 0)
        bidAskRatio = static_cast<double>(totalBidVolume) / totalAskVolume;
      depthImbalance = static_cast<double>(totalBidVolume - totalAskVolume) /
                       (totalBidVolume + totalAskVolume);
    }

    // Dump bid levels
    int level = 0;
    for (const auto& [price, quantity] : orderbook.bids) {
      csvOrderbookData << timestamp << "," << instrumentName << ",bid,"
                       << level++ << "," << price << "," << quantity << ","
                       << spread << "," << midPrice << "," << bidAskRatio << ","
                       << depthImbalance << "\n";
      if (level >= 3) break;  // Top 3 levels
    }

    // Dump ask levels
    level = 0;
    for (const auto& [price, quantity] : orderbook.asks) {
      csvOrderbookData << timestamp << "," << instrumentName << ",ask,"
                       << level++ << "," << price << "," << quantity << ","
                       << spread << "," << midPrice << "," << bidAskRatio << ","
                       << depthImbalance << "\n";
      if (level >= 3) break;  // Top 3 levels
    }
    csvOrderbookData.flush();
  }

  void dumpTradeFeatures(const TradeEvent& trade) {
    // Determine trade direction (1 for buy, -1 for sell, 0 for unknown)
    int tradeDirection = 0;  // Would need more logic to determine this

    csvTradeEvents << trade.time << "," << trade.instrumentID << ","
                   << trade.price << "," << trade.quantity << ","
                   << trade.passiveOrderID << "," << trade.activeOrderID << ","
                   << tradeDirection << "\n";
    csvTradeEvents.flush();
  }

  // Helper to parse instrument names like "$CARD_call_100350_1110"
  std::tuple<std::string, std::string, int, long> parseInstrumentName(
      const std::string& name) {
    // Split by underscore
    std::vector<std::string> parts;
    std::stringstream ss(name);
    std::string item;
    while (std::getline(ss, item, '_')) {
      parts.push_back(item);
    }

    if (parts.size() >= 4) {
      std::string underlying = parts[0];  // e.g., "$CARD"
      std::string type = parts[1];        // e.g., "call", "put", "future"
      int strike = std::stoi(parts[2]);   // e.g., 100350
      long expiry = std::stol(parts[3]);  // e.g., 1110
      return {type, underlying, strike, expiry};
    }
    return {"unknown", "", 0, 0};
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