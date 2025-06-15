#pragma once
#include <drogon/WebSocketClient.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <trantor/utils/Logger.h>

#include <fstream>
#include <iomanip>  // Required for std::fixed and std::setprecision
#include <limits>   // Required for std::numeric_limits
#include <map>
#include <numeric>
#include <optional>

#include "candle_data.hpp"
#include "consts.hpp"
#include "events.hpp"
#include "orderbook_level.hpp"
#include "random_forest.hpp"

struct InstrumentFeatures {
  // Price features
  double open = 0, close = 0, high = 0, low = 0;
  double volume = 0, underlying_price = 0;

  // Options features
  double time_to_expiry = 0;
  double moneyness = 0;

  // Orderbook features
  double spread = 0, mid_price = 0;
  double bid_ask_ratio = 0, depth_imbalance = 0;

  // Trade features (rolling window)
  double trade_volume_1min = 0, avg_trade_price_1min = 0;
  double price_volatility_1min = 0, trade_count_1min = 0;

  // Technical indicators
  double price_momentum_5s = 0, volume_momentum_5s = 0;
  double rsi_14 = 0;
  double bb_sma_20 = 0;
  double bb_stddev_20 = 0;
  double bb_upper_band = 0;
  double bb_lower_band = 0;
  double bb_position = 0;

  // Add feature scaling constants (these should match your training data)
  static constexpr double PRICE_SCALE = 100000.0;  // Scale prices to 0-1 range
  static constexpr double VOLUME_SCALE = 1000000.0;  // Scale volumes
  static constexpr double TIME_SCALE = 86400.0;      // Convert to days

  std::vector<double> toVector() const {
    // Properly scale all features to match training data format
    // std::vector<double> features = {
    //     open / PRICE_SCALE,
    //     close / PRICE_SCALE,
    //     high / PRICE_SCALE,
    //     low / PRICE_SCALE,
    //     std::min(volume / VOLUME_SCALE, 1.0),  // Cap volume at 1.0
    //     underlying_price / PRICE_SCALE,
    //     std::min(time_to_expiry / TIME_SCALE, 1.0),  // Convert to days, cap
    //     at 1 std::tanh(moneyness),  // Normalize moneyness using tanh
    //     std::min(spread / 1000.0, 1.0),  // Scale spread
    //     mid_price / PRICE_SCALE,
    //     std::min(bid_ask_ratio, 10.0) / 10.0,  // Cap and scale ratio
    //     std::tanh(depth_imbalance),  // Normalize imbalance
    //     std::min(trade_volume_1min / VOLUME_SCALE, 1.0),
    //     avg_trade_price_1min / PRICE_SCALE,
    //     std::min(price_volatility_1min, 1.0),  // Cap volatility
    //     std::min(trade_count_1min / 100.0, 1.0),  // Scale trade count
    //     std::tanh(price_momentum_5s),  // Normalize momentum
    //     std::tanh(volume_momentum_5s),
    //     rsi_14 / 100.0,  // RSI is already 0-100, scale to 0-1
    //     bb_position  // BB position should already be normalized
    // };

    // Properly scale all features to match training data format
    std::vector<double> features = {open,
                                    close,
                                    high,
                                    low,
                                    volume,
                                    underlying_price,
                                    time_to_expiry,
                                    moneyness,
                                    spread,
                                    mid_price,
                                    bid_ask_ratio,
                                    depth_imbalance,
                                    trade_volume_1min,
                                    avg_trade_price_1min,
                                    price_volatility_1min,
                                    trade_count_1min,
                                    price_momentum_5s,
                                    volume_momentum_5s,
                                    rsi_14,
                                    bb_position};

    // Validate features (replace NaN/inf with 0)
    for (auto& feature : features) {
      if (!std::isfinite(feature)) {
        feature = 0.0;
      }
    }

    return features;
  }
};

class TradingWebSocketClient {
  friend class FastMLTradingEngine;

 private:
  drogon::WebSocketClientPtr wsClient;
  std::ofstream outputFile;
  std::ofstream csvUnderlyingPrices;
  std::ofstream csvTradeableInstruments;  // New CSV for ML training
  std::ofstream csvOrderbookData;         // New CSV for orderbook data
  std::ofstream csvTradeEvents;           // New CSV for trade events
 private:
  RandomForestRegressor price_model;
  RandomForestRegressor direction_model;

  // Feature storage for rolling calculations
  std::unordered_map<std::string, std::deque<CandleData>> price_history;
  std::unordered_map<std::string, std::deque<TradeEvent>> trade_history;
  std::unordered_map<std::string, OrderBookLevel> latest_orderbook;
  std::unordered_map<std::string, InstrumentFeatures> current_features;
  std::unordered_map<std::string, double> underlying_prices;
  // Map to store client_order_id to instrument_id and other details if needed
  std::unordered_map<std::string, std::string> active_client_orders_;
  std::unordered_map<std::string, std::string> pending_client_orders_;
  std::unordered_map<std::string, std::string>
      server_to_client_orders_;  // optional

  // Rate limiting
  struct ConnectionManager {
    static constexpr int MAX_MESSAGES_PER_SECOND = 280;
    static constexpr int MAX_PENDING_ORDERS =
        580;  // Max orders allowed by exchange

    std::deque<std::chrono::steady_clock::time_point> message_timestamps;
    std::atomic<int> pending_orders{
        0};  // Made atomic for potential multithreaded access

    bool canSendMessage() {
      auto now = std::chrono::steady_clock::now();
      // Remove old timestamps (older than 1 second)
      while (!message_timestamps.empty() &&
             std::chrono::duration_cast<std::chrono::seconds>(
                 now - message_timestamps.front())
                     .count() >= 1) {
        message_timestamps.pop_front();
      }
      bool can_send = message_timestamps.size() < MAX_MESSAGES_PER_SECOND;
      if (!can_send) {
        LOG_WARN << "Cannot send message: Rate limit reached ("
                 << message_timestamps.size() << "/" << MAX_MESSAGES_PER_SECOND
                 << " msgs/sec)";
      }
      return can_send;
    }

    void recordMessage() {
      message_timestamps.push_back(std::chrono::steady_clock::now());
    }

    bool canPlaceOrder() const {
      bool can_place = pending_orders < MAX_PENDING_ORDERS;
      if (!can_place) {
        LOG_WARN << "Cannot place order: Max pending orders reached ("
                 << pending_orders << "/" << MAX_PENDING_ORDERS << ")";
      }
      return can_place;
    }

    void orderPlaced() {
      pending_orders++;
      LOG_INFO << "Order placed, pending orders: " << pending_orders;
    }

    void orderFilledOrCancelled() {  // Renamed for clarity
      if (pending_orders > 0) {
        pending_orders--;
      }
      LOG_INFO << "Order filled/cancelled, pending orders: " << pending_orders;
    }
  } connection_mgr;

 public:
  TradingWebSocketClient(const std::string& server, uint16_t port,
                         const std::string& price_model_path,
                         const std::string& direction_model_path)
      : outputFile("trading_log.json"),
        csvUnderlyingPrices("dataframe.csv"),
        csvOrderbookData("orderbook_data.csv"),
        csvTradeableInstruments("tradable_instrs.csv"),
        csvTradeEvents("trade_events.csv") {
    std::string serverString = server + ":" + std::to_string(port);
    wsClient = drogon::WebSocketClient::newWebSocketClient(serverString);

    // CSV headers setup
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

    csvOrderbookData
        << "timestamp,instrument_id,side,price_level,price,quantity,";
    csvOrderbookData << "spread,mid_price,bid_ask_ratio,depth_imbalance\n";

    csvTradeEvents << "timestamp,instrument_id,price,quantity,";
    csvTradeEvents << "passive_order_id,active_order_id,trade_direction\n";

    // Load and validate models
    try {
      LOG_INFO << "Loading price model from: " << price_model_path;
      price_model.loadModel(price_model_path);

      LOG_INFO << "Loading direction model from: " << direction_model_path;
      direction_model.loadModel(direction_model_path);

      // Test models with dummy data
      std::vector<double> test_features(20, 0.5);  // 20 features, all 0.5

      double test_price_pred = price_model.predict(test_features);
      double test_direction_pred = direction_model.predict(test_features);

      LOG_INFO << "Model validation - Price prediction: " << test_price_pred;
      LOG_INFO << "Model validation - Direction prediction: "
               << test_direction_pred;

      if (std::isnan(test_price_pred) || std::isnan(test_direction_pred)) {
        throw std::runtime_error("Models return NaN values");
      }

      if (test_price_pred == 0.0 && test_direction_pred == 0.0) {
        LOG_WARN << "Models might not be properly loaded (returning zeros)";
      }

      LOG_INFO << "Models loaded and validated successfully";

    } catch (const std::exception& e) {
      LOG_ERROR << "Failed to load ML models: " << e.what();
      LOG_ERROR << "Trading system will run in data collection mode only";
    }

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

 protected:
  void parseJsonMessage(const std::string& message) {
    try {
      rapidjson::Document doc;

      // Don't use ParseInsitu as it modifies the original string
      // Use regular Parse instead
      doc.Parse(message.c_str());

      if (doc.HasParseError()) {
        LOG_WARN << "JSON parse error at offset " << doc.GetErrorOffset()
                 << ": " << rapidjson::GetParseError_En(doc.GetParseError())
                 << " in message: " << message.substr(0, 100) << "...";
        return;
      }

      // Call the correct method name
      processMarketDataUpdate(doc);
    } catch (const std::exception& e) {
      LOG_ERROR << "JSON parsing error: " << e.what()
                << " in message: " << message.substr(0, 100) << "...";
    }
  }

  void processMarketDataUpdate(const rapidjson::Document& json) {
    if (!json.HasMember("type") || !json["type"].IsString()) {
      LOG_WARN << "Missing or invalid 'type' field in JSON message";
      return;
    }

    const char* messageType = json["type"].GetString();

    if (strcmp(messageType, "market_data_update") != 0) {
      LOG_INFO << "Skipping non-market_data_update message: " << messageType;
      return;
    }

    long timestamp = json.HasMember("time") && json["time"].IsInt64()
                         ? json["time"].GetInt64()
                         : 0;

    // Process candles data
    if (json.HasMember("candles") && json["candles"].IsObject()) {
      processCandlesData(json["candles"], timestamp);
    }

    // Process orderbook depths
    if (json.HasMember("orderbook_depths") &&
        json["orderbook_depths"].IsObject()) {
      processOrderbookData(json["orderbook_depths"], timestamp);
    }

    // Process events for both regular processing and ML
    if (json.HasMember("events") && json["events"].IsArray()) {
      processEventsData(json["events"], timestamp);
      processTradeEventsForML(json["events"], timestamp);
    }

    // Generate ML predictions after processing all data
    generatePredictionsAndTrade(timestamp);
  }

  void processTradeEventsForML(const rapidjson::Value& events, long timestamp) {
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

        // Add to history and update metrics
        auto& history = trade_history[trade.instrumentID];
        history.push_back(trade);

        // Keep only last minute
        auto cutoff_time = timestamp - 60;
        while (!history.empty() && history.front().time < cutoff_time) {
          history.pop_front();
        }

        updateTradeMetrics(trade.instrumentID, history);
      }
    }
  }

  void updateTradeMetrics(const std::string& instrument,
                          const std::deque<TradeEvent>& trades) {
    auto& features = current_features[instrument];

    if (trades.empty()) {
      features.trade_count_1min = 0;
      features.trade_volume_1min = 0;
      features.avg_trade_price_1min = 0;
      features.price_volatility_1min = 0;
      return;
    }

    features.trade_count_1min = trades.size();

    double total_volume = 0;
    double total_value = 0;
    std::vector<double> prices;
    prices.reserve(trades.size());

    for (const auto& trade : trades) {
      total_volume += trade.quantity;
      total_value += static_cast<double>(trade.price) *
                     trade.quantity;  // Ensure double arithmetic
      prices.push_back(trade.price);
    }

    features.trade_volume_1min = total_volume;
    if (total_volume > 0) {
      features.avg_trade_price_1min = total_value / total_volume;
    } else {
      features.avg_trade_price_1min = 0;  // No volume, no average price
    }

    if (prices.size() > 1) {
      double mean =
          std::accumulate(prices.begin(), prices.end(), 0.0) / prices.size();
      double variance_sum = 0;
      for (double price : prices) {
        variance_sum += (price - mean) * (price - mean);
      }
      double variance = variance_sum / prices.size();
      if (variance >= 0) {  // Ensure variance is not negative
        features.price_volatility_1min = std::sqrt(variance);
      } else {
        features.price_volatility_1min = 0;
        LOG_WARN << "updateTradeMetrics: Negative variance " << variance
                 << " for " << instrument;
      }
    } else {
      features.price_volatility_1min = 0;  // Not enough data for volatility
    }
  }

  void updateInstrumentFeatures(const std::string& instrument,
                                const CandleData& candle, long timestamp) {
    auto& features = current_features[instrument];
    auto& history = price_history[instrument];

    // Update basic price features
    features.open = candle.open;
    features.close = candle.close;
    features.high = candle.high;
    features.low = candle.low;
    features.volume = candle.volume;

    // Calculate options-specific features
    auto [type, underlying, strike, expiry_day_or_timestamp] =
        parseInstrumentName(instrument);

    // Fix time to expiry calculation
    if (expiry_day_or_timestamp > 1000000000) {
      // It's a timestamp in seconds
      features.time_to_expiry = std::max(
          0.0,
          static_cast<double>(expiry_day_or_timestamp - timestamp) / 86400.0);
    } else {
      // It's a day number, convert to days from now (assuming day 0 = today)
      features.time_to_expiry =
          std::max(0.0, static_cast<double>(expiry_day_or_timestamp));
    }

    if (type == "call" || type == "put") {
      double underlying_price = getUnderlyingPrice(underlying);
      features.underlying_price = underlying_price;

      if (underlying_price > 0 && strike > 0) {
        features.moneyness = (underlying_price - strike) / strike;
      } else {
        features.moneyness = 0.0;
      }
    } else {
      // For futures, use the instrument's own price as underlying
      features.underlying_price = features.close;
      features.moneyness = 0.0;  // Not applicable for futures
    }

    // Store history
    history.push_back(candle);
    if (history.size() > 300) {
      history.pop_front();
    }

    // Calculate technical indicators
    calculateTechnicalIndicators(features, history);
  }

  void calculateTechnicalIndicators(InstrumentFeatures& features,
                                    const std::deque<CandleData>& history) {
    if (history.empty()) {
      return;  // Guard against empty history
    }

    // Price momentum (5-second) - use percentage change
    if (history.size() >= 5) {
      double old_price = history[history.size() - 5].close;
      double new_price = history.back().close;
      if (old_price > 0) {
        features.price_momentum_5s = (new_price - old_price) / old_price;
      } else {
        features.price_momentum_5s = 0.0;
      }
    } else {
      features.price_momentum_5s = 0.0;
    }

    // Volume momentum - use ratio instead of difference
    if (history.size() >= 10) {
      double old_vol = 0.0, new_vol = 0.0;
      for (size_t i = 0; i < 5 && i < history.size(); ++i) {
        new_vol += history[history.size() - 1 - i].volume;
      }
      for (size_t i = 5; i < 10 && i < history.size(); ++i) {
        old_vol += history[history.size() - 1 - i].volume;
      }
      if (old_vol > 0) {
        features.volume_momentum_5s = (new_vol - old_vol) / old_vol;
      } else {
        features.volume_momentum_5s = 0.0;
      }
    } else {
      features.volume_momentum_5s = 0.0;
    }

    // Simple RSI calculation (14 periods)
    if (history.size() >= 15) {
      double gain = 0.0, loss = 0.0;
      for (size_t i = history.size() - 14; i < history.size(); ++i) {
        double change = history[i].close - history[i - 1].close;
        if (change > 0) {
          gain += change;
        } else {
          loss -= change;  // Make positive
        }
      }

      if (gain + loss > 0) {
        double rs = gain / loss;
        features.rsi_14 = 100.0 - (100.0 / (1.0 + rs));
      } else {
        features.rsi_14 = 50.0;  // Neutral RSI
      }
    } else {
      features.rsi_14 = 50.0;  // Default neutral RSI
    }

    // Calculate Bollinger Bands (20 period)
    if (history.size() >= 20) {
      double sum = 0.0;
      for (size_t i = history.size() - 20; i < history.size(); ++i) {
        sum += history[i].close;
      }
      features.bb_sma_20 = sum / 20.0;

      double variance = 0.0;
      for (size_t i = history.size() - 20; i < history.size(); ++i) {
        double diff = history[i].close - features.bb_sma_20;
        variance += diff * diff;
      }
      features.bb_stddev_20 = std::sqrt(variance / 20.0);

      features.bb_upper_band =
          features.bb_sma_20 + (2.0 * features.bb_stddev_20);
      features.bb_lower_band =
          features.bb_sma_20 - (2.0 * features.bb_stddev_20);

      // Calculate BB position
      double current_price = history.back().close;
      if (features.bb_upper_band > features.bb_lower_band) {
        features.bb_position =
            (current_price - features.bb_lower_band) /
            (features.bb_upper_band - features.bb_lower_band);
      } else {
        features.bb_position = 0.5;  // Neutral position
      }
    } else {
      features.bb_sma_20 = history.back().close;
      features.bb_stddev_20 = 0.0;
      features.bb_upper_band = features.bb_sma_20;
      features.bb_lower_band = features.bb_sma_20;
      features.bb_position = 0.5;
    }
  }

  void calculateOrderbookFeatures(InstrumentFeatures& features,
                                  const OrderBookLevel& orderbook,
                                  const std::string& instrument_name_for_log) {
    if (orderbook.bids.empty() || orderbook.asks.empty()) {
      LOG_WARN << "Empty orderbook for " << instrument_name_for_log;
      features.spread = 0;
      features.mid_price = 0;
      features.bid_ask_ratio = 1.0;  // Neutral
      features.depth_imbalance = 0.0;
      return;
    }

    double best_bid = static_cast<double>(orderbook.bids.rbegin()->first);
    double best_ask = static_cast<double>(orderbook.asks.begin()->first);

    features.spread = best_ask - best_bid;
    if (features.spread < 0) {
      LOG_WARN << "Negative spread for " << instrument_name_for_log << ": "
               << features.spread;
      features.spread = 0;
    }
    features.mid_price = (best_bid + best_ask) / 2.0;

    long long total_bid_vol = 0;
    long long total_ask_vol = 0;

    // Sum top 5 levels for better depth calculation
    int levels_counted = 0;
    for (auto it = orderbook.bids.rbegin();
         it != orderbook.bids.rend() && levels_counted < 5;
         ++it, ++levels_counted) {
      total_bid_vol += it->second;
    }

    levels_counted = 0;
    for (auto it = orderbook.asks.begin();
         it != orderbook.asks.end() && levels_counted < 5;
         ++it, ++levels_counted) {
      total_ask_vol += it->second;
    }

    if (total_ask_vol > 0) {
      features.bid_ask_ratio = static_cast<double>(total_bid_vol) /
                               static_cast<double>(total_ask_vol);
    } else {
      features.bid_ask_ratio = 1.0;
    }

    if (total_bid_vol + total_ask_vol > 0) {
      features.depth_imbalance =
          static_cast<double>(total_bid_vol - total_ask_vol) /
          static_cast<double>(total_bid_vol + total_ask_vol);
    } else {
      features.depth_imbalance = 0.0;
    }
  }

  // Fix the generatePredictionsAndTrade method
  void generatePredictionsAndTrade(long timestamp) {
    // Batch predictions for efficiency
    std::vector<std::string> instruments;
    std::vector<std::vector<double>> feature_vectors;

    for (const auto& [instrument, features_obj] :
         current_features) {  // Renamed 'features' to 'features_obj'
      // Only predict for instruments with complete data
      auto orderbook_it = latest_orderbook.find(instrument);
      if (orderbook_it != latest_orderbook.end() &&
          !orderbook_it->second.bids.empty() &&
          !orderbook_it->second.asks.empty()) {
        instruments.push_back(instrument);
        feature_vectors.push_back(features_obj.toVector());
      }
    }

    if (feature_vectors.empty()) return;

    try {
      // Get predictions from models
      std::vector<double> price_predictions;
      std::vector<double> direction_predictions;

      for (size_t i = 0; i < feature_vectors.size(); ++i) {
        const auto& current_features_vec =
            feature_vectors[i];  // Renamed 'features' to 'current_features_vec'

        double p_pred = price_model.predict(current_features_vec);
        double d_pred = direction_model.predict(current_features_vec);

        if (std::isnan(p_pred) || std::isnan(d_pred)) {
          LOG_WARN << "NaN prediction for instrument: " << instruments[i];
          std::stringstream ss;
          ss << "Features for " << instruments[i]
             << ": [";  // Add instrument name to feature log
          const auto& feature_names_vec =
              InstrumentFeatures{}
                  .toVector();  // Hack to get count, ideally get names
          for (size_t j = 0; j < current_features_vec.size(); ++j) {
            // You would ideally have a list of feature names to log alongside
            // values
            ss << /* feature_name[j] << "=" << */ std::fixed
               << std::setprecision(4) << current_features_vec[j]
               << (j == current_features_vec.size() - 1 ? "" : ", ");
          }
          ss << "]";
          LOG_WARN << ss.str();
        }

        price_predictions.push_back(p_pred);
        direction_predictions.push_back(d_pred);
      }

      // Evaluate trading signals
      for (size_t i = 0; i < instruments.size(); i++) {
        evaluateTradingSignal(instruments[i], price_predictions[i],
                              direction_predictions[i], timestamp);
      }
    } catch (const std::exception& e) {
      LOG_ERROR << "Prediction error: " << e.what();
    }
  }

  // Fix the evaluateTradingSignal method
  void evaluateTradingSignal(const std::string& instrument,
                             double predicted_price, double direction_prob,
                             long timestamp) {
    LOG_INFO << "Evaluating signal for " << instrument
             << ": predicted_price=" << predicted_price
             << ", direction_prob=" << direction_prob;
    const auto& features = current_features[instrument];
    auto orderbook_it = latest_orderbook.find(instrument);

    if (orderbook_it == latest_orderbook.end()) {
      LOG_WARN << "No orderbook data for " << instrument
               << " to evaluate signal.";
      return;
    }
    const auto& orderbook = orderbook_it->second;

    if (orderbook.bids.empty() || orderbook.asks.empty()) {
      LOG_WARN << "Empty orderbook for " << instrument
               << ", cannot evaluate signal.";
      return;
    }

    double current_mid = features.mid_price;
    if (current_mid ==
        0) {  // Avoid division by zero if mid_price wasn't calculated
      LOG_WARN << "Mid price is zero for " << instrument
               << ", cannot calculate expected return.";
      return;
    }
    double expected_return = (predicted_price - current_mid) / current_mid;

    // Trading thresholds
    const double MIN_EXPECTED_RETURN = 0.002;      // 0.2%
    const double MIN_DIRECTION_CONFIDENCE = 0.65;  // Example: 65% confidence

    LOG_INFO << instrument << ": Current Mid=" << current_mid
             << ", Predicted Price=" << predicted_price
             << ", Expected Return=" << expected_return
             << ", Direction Prob=" << direction_prob;

    bool should_buy = direction_prob > MIN_DIRECTION_CONFIDENCE &&
                      expected_return > MIN_EXPECTED_RETURN;
    bool should_sell =
        (1.0 - direction_prob) >
            MIN_DIRECTION_CONFIDENCE &&  // If direction_prob is P(up), then
                                         // 1-P(up) is P(down)
        expected_return < -MIN_EXPECTED_RETURN;

    if (should_buy) {
      LOG_INFO << "Signal: BUY for " << instrument;
    } else if (should_sell) {
      LOG_INFO << "Signal: SELL for " << instrument;
    } else {
      LOG_INFO << "Signal: HOLD for " << instrument
               << ". No strong signal or conditions not met.";
      return;  // No trade signal
    }

    if (!connection_mgr.canSendMessage()) {
      LOG_WARN << "Trade decision made for " << instrument
               << " but cannot send message due to rate limit.";
      return;
    }
    if (!connection_mgr.canPlaceOrder()) {
      LOG_WARN << "Trade decision made for " << instrument
               << " but cannot place order due to pending order limit.";
      return;
    }

    // If we reach here, all conditions to place an order are met
    LOG_INFO << "Proceeding to place " << (should_buy ? "BUY" : "SELL")
             << " order for " << instrument
             << " - Expected return: " << expected_return
             << ", Direction prob: " << direction_prob;
    placeOrder(instrument, should_buy, orderbook, timestamp);
    // connection_mgr.recordMessage(); // Moved to placeOrder to ensure it's
    // only called if send is attempted connection_mgr.orderPlaced(); // Moved
    // to placeOrder
  }

  // Fix the placeOrder method
  void placeOrder(const std::string& instrument, bool is_buy,
                  const OrderBookLevel& orderbook, long timestamp) {
    // Check if WebSocket connection is active
    if (!wsClient || !wsClient->getConnection() ||
        !wsClient->getConnection()->connected()) {
      LOG_ERROR << "WebSocket not connected - cannot place order";
      return;
    }

    // Double check rate limits just before sending
    if (!connection_mgr.canSendMessage() || !connection_mgr.canPlaceOrder()) {
      LOG_WARN << "Rate/order limits hit at order placement time";
      return;
    }

    // Calculate order parameters
    int target_price;
    int quantity = 1;  // Start with small quantities

    if (is_buy) {
      // Buy at best ask (market order) or slightly better
      target_price = orderbook.asks.begin()->first;
    } else {
      // Sell at best bid (market order) or slightly better
      target_price = orderbook.bids.rbegin()->first;
    }

    // Generate unique client order ID
    std::string client_order_id =
        "order_" + std::to_string(timestamp) + "_" +
        std::to_string(std::hash<std::string>{}(instrument) % 10000);

    // Create order JSON
    rapidjson::Document order_doc;
    order_doc.SetObject();
    auto& allocator = order_doc.GetAllocator();

    int exp = timestamp + 9 * 1000;
    order_doc.AddMember("type", "add_order", allocator);
    std::string user_request_id = "something" + std::to_string(rand());
    order_doc.AddMember("user_request_id",
                        rapidjson::Value(client_order_id.c_str(), allocator),
                        allocator);
    order_doc.AddMember("instrument_id",
                        rapidjson::Value(instrument.c_str(), allocator),
                        allocator);
    order_doc.AddMember("expiry", exp, allocator);
    order_doc.AddMember(
        "side", rapidjson::Value(is_buy ? "bid" : "ask", allocator), allocator);
    order_doc.AddMember("price", target_price, allocator);
    order_doc.AddMember("quantity", quantity, allocator);

    // Convert to string
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    order_doc.Accept(writer);
    std::string order_message = buffer.GetString();

    try {
      // Send the order
      LOG_INFO << "PLACING ORDER " << order_message << "\n";
      wsClient->getConnection()->send(order_message);

      // Record the order
      active_client_orders_[client_order_id] = instrument;

      // Store order request for tracking server response
      // We'll move recordMessage and orderPlaced to response handling
      LOG_INFO << "Order request sent, waiting for server confirmation: "
               << client_order_id;
      connection_mgr.recordMessage();
      connection_mgr.orderPlaced();

      LOG_INFO << "Order placed: " << (is_buy ? "BUY" : "SELL") << " "
               << quantity << " " << instrument << " at " << target_price
               << " (client_order_id: " << client_order_id << ")";

    } catch (const std::exception& e) {
      LOG_ERROR << "Failed to send order: " << e.what();
    }
  }

  void handleOrderResponse(const std::string& client_order_id, bool success,
                           const std::string& server_order_id = "") {
    auto it = pending_client_orders_.find(client_order_id);
    if (it == pending_client_orders_.end()) {
      LOG_ERROR << "Received response for unknown order: " << client_order_id;
      return;
    }

    if (success) {
      // Move from pending to active
      active_client_orders_[client_order_id] = it->second;
      pending_client_orders_.erase(it);

      connection_mgr.orderPlaced();
      LOG_INFO << "Order confirmed by server: " << client_order_id;

      if (!server_order_id.empty()) {
        // Store server order ID mapping if needed
        server_to_client_orders_[server_order_id] = client_order_id;
      }
    } else {
      // Order rejected - remove from pending
      pending_client_orders_.erase(it);
      LOG_ERROR << "Order rejected by server: " << client_order_id;
    }
  }

  // Helper methods
  std::tuple<std::string, std::string, int, long> parseInstrumentName(
      const std::string& name) {
    std::vector<std::string> parts;
    std::stringstream ss(name);
    std::string item;
    while (std::getline(ss, item, '_')) {
      parts.push_back(item);
    }

    // Expected formats:
    // Option: $UNDERLYING_TYPE_STRIKE_EXPIRYDAY ($CARD_call_1000_300) -> 4
    // parts Future: $UNDERLYING_future_EXPIRYDAY ($CARD_future_300) -> 3 parts
    // Spot: $UNDERLYING (e.g. $CARD) -> 1 part (if it comes as an instrument
    // name)

    if (parts.empty()) {
      LOG_WARN << "parseInstrumentName: Empty name provided.";
      return {"unknown", "", 0, 0};
    }

    std::string underlying = parts[0];
    std::string type = "spot";  // Default to spot if only underlying is present
    int strike = 0;
    long expiry_timestamp_or_day = 0;  // This needs to be consistently a
                                       // timestamp or a relative day number

    if (parts.size() >= 3 &&
        (parts[1] == "call" ||
         parts[1] == "put")) {  // Option: $U_T_S_E (4 parts)
      if (parts.size() >= 4) {
        type = parts[1];
        try {
          strike = std::stoi(parts[2]);
          expiry_timestamp_or_day =
              std::stol(parts[3]);  // Assuming this is a day number or similar
                                    // relative value
        } catch (const std::exception& e) {
          LOG_WARN << "parseInstrumentName (option): Error parsing "
                      "strike/expiry for "
                   << name << ": " << e.what();
          return {"unknown", underlying, 0, 0};
        }
      } else {
        LOG_WARN << "parseInstrumentName (option): Not enough parts for option "
                 << name << ". Expected 4, got " << parts.size();
        return {"unknown", underlying, 0, 0};
      }
    } else if (parts.size() >= 3 &&
               parts[1] == "future") {  // Future: $U_future_E (3 parts)
      type = parts[1];
      strike = 0;  // Futures don't have a strike in the same sense
      try {
        expiry_timestamp_or_day =
            std::stol(parts[2]);  // Assuming this is a day number or similar
      } catch (const std::exception& e) {
        LOG_WARN << "parseInstrumentName (future): Error parsing expiry for "
                 << name << ": " << e.what();
        return {"unknown", underlying, 0, 0};
      }
    } else if (parts.size() == 1) {  // Spot: $U (1 part)
      type = "spot";                 // Already set as default
      strike = 0;
      expiry_timestamp_or_day = 0;  // Spots don't expire
    } else if (parts.size() == 2 &&
               parts[1] == "future") {  // Potentially $U_future if expiry is
                                        // implicit or handled elsewhere
      type = parts[1];
      strike = 0;
      expiry_timestamp_or_day = 0;  // Needs clarity on how expiry is determined
      LOG_WARN << "parseInstrumentName (future): Ambiguous future format "
               << name << ". Assuming no explicit expiry.";
    } else {
      // Could be an underlying like "$CARD" if it's passed directly and not
      // part of a larger update Or an unrecognised format
      if (parts.size() >
          1) {  // If more than one part but not matching option/future
        LOG_WARN << "parseInstrumentName: Unrecognized instrument format for "
                 << name << ". Treating as spot/unknown.";
        type = "unknown";  // Or keep as spot if that's a safer default
      }
    }
    // IMPORTANT: 'expiry_timestamp_or_day' needs to be converted to a
    // consistent comparable value (e.g. seconds since epoch, or days from now)
    // For now, it's passed as is. The `time_to_expiry` calculation assumes it's
    // comparable with `timestamp`.
    return {type, underlying, strike, expiry_timestamp_or_day};
  }

  double getUnderlyingPrice(const std::string& underlying) {
    auto it = underlying_prices.find(underlying);
    return it != underlying_prices.end() ? it->second : 0.0;
  }

  void updateUnderlyingPrice(const std::string& underlying, double price) {
    underlying_prices[underlying] = price;
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
          // Directly update features for tradeable instruments here
          updateInstrumentFeatures(instrumentName, candle, timestamp);
          onCandleUpdate(instrumentName, candle, timestamp,
                         true);  // Call virtual method for logging/other
                                 // derived class logic
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
          // For untradeable, you might only want to update underlying prices or
          // specific features If they also need full feature updates:
          // updateInstrumentFeatures(instrumentName, candle, timestamp);
          if (getIndex(instrumentName) <
              underlying_prices
                  .size())  // Assuming getIndex maps to an underlying
            updateUnderlyingPrice(instrumentName,
                                  candle.mid);  // Or candle.close
          onCandleUpdate(instrumentName, candle, timestamp,
                         false);  // Call virtual method for logging/other
                                  // derived class logic
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
                       static_cast<double>(totalBidVolume + totalAskVolume);
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

  // Override these methods to handle specific data
  virtual void onCandleUpdate(const std::string& instrument,
                              const CandleData& candle, long timestamp,
                              bool tradeable) {
    // Base implementation now primarily for logging or extension by derived
    // classes. Feature updates are handled directly in processCandlesData or
    // via updateInstrumentFeatures.
    LOG_INFO << "Candle update for " << instrument << " - O:" << candle.open
             << " H:" << candle.high << " L:" << candle.low
             << " C:" << candle.close << " V:" << candle.volume
             << (tradeable ? " (tradeable)" : " (untradeable)");

    // If you were relying on derived classes to call updateInstrumentFeatures,
    // ensure that logic is either moved to processCandlesData or the derived
    // classes still do it. For the base class, updateInstrumentFeatures is now
    // called directly in processCandlesData for tradeable instruments.
  }

  virtual void onOrderbookUpdate(const std::string& instrument,
                                 const OrderBookLevel& orderbook,
                                 long timestamp) {
    LOG_INFO << "Orderbook update for " << instrument
             << " - Bids:" << orderbook.bids.size()
             << " Asks:" << orderbook.asks.size();

    latest_orderbook[instrument] = orderbook;
    auto& features = current_features[instrument];  // Creates if not exists,
                                                    // all members 0.0 initially
    calculateOrderbookFeatures(features, orderbook,
                               instrument);  // Pass instrument name for logging

    if (!orderbook.bids.empty()) {
      auto bestBid = orderbook.bids.rbegin();
      LOG_DEBUG << "  Best bid for " << instrument << ": " << bestBid->first
                << " x " << bestBid->second;
    }
    if (!orderbook.asks.empty()) {
      auto bestAsk = orderbook.asks.begin();
      LOG_DEBUG << "  Best ask for " << instrument << ": " << bestAsk->first
                << " x " << bestAsk->second;
    }
  }

  virtual void onTradeEvent(const TradeEvent& trade) {
    LOG_INFO << "Trade event received for instrument: " << trade.instrumentID
             << ", Price: " << trade.price << ", Qty: " << trade.quantity
             << ", Active Order ID: " << trade.activeOrderID
             << ", Passive Order ID: " << trade.passiveOrderID;

    // CRITICAL: Implement logic here to check if this trade event
    // corresponds to one of YOUR active orders.
    // You'll need to store your client-generated order IDs when you place them.
    // Example (requires you to manage `active_client_orders_`):
    // if (active_client_orders_.count(trade.activeOrderID) ||
    // active_client_orders_.count(trade.passiveOrderID)) {
    //   LOG_INFO << "Trade event matches one of our active orders. Order ID: "
    //            << (active_client_orders_.count(trade.activeOrderID) ?
    //            trade.activeOrderID : trade.passiveOrderID);
    //   connection_mgr.orderFilledOrCancelled();
    //   // Remove the order ID from your active list
    //   active_client_orders_.erase(trade.activeOrderID);
    //   active_client_orders_.erase(trade.passiveOrderID);
    // }

    dumpTradeFeatures(trade);
  }

  virtual void onCancelEvent(const CancelEvent& cancel) {
    LOG_INFO << "Cancel event received for order ID: " << cancel.orderID;

    // CRITICAL: Implement logic here to check if this cancel event
    // corresponds to one of YOUR active orders.
    // Example (requires you to manage `active_client_orders_`):
    // if (active_client_orders_.count(cancel.orderID)) {
    //   LOG_INFO << "Cancel event matches one of our active orders. Order ID: "
    //   << cancel.orderID; connection_mgr.orderFilledOrCancelled();
    //   // Remove the order ID from your active list
    //   active_client_orders_.erase(cancel.orderID);
    // }
  }
};