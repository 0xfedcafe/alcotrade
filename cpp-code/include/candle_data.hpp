#pragma once
#include <rapidjson/document.h>

#include <array>
#include <format>
#include <optional>
#include <string>

struct CandleData {
  int open = 0;
  int close = 0;
  int high = 0;
  int low = 0;
  int volume = 0;
  int index = 0;
  int mid = 0;

  CandleData() = default;
  CandleData(const rapidjson::Value& json) {
    if (json.HasMember("open") && json["open"].IsInt())
      open = json["open"].GetInt();
    if (json.HasMember("close") && json["close"].IsInt())
      close = json["close"].GetInt();
    if (json.HasMember("high") && json["high"].IsInt())
      high = json["high"].GetInt();
    if (json.HasMember("low") && json["low"].IsInt())
      low = json["low"].GetInt();
    if (json.HasMember("volume") && json["volume"].IsInt())
      volume = json["volume"].GetInt();
    if (json.HasMember("index") && json["index"].IsInt())
      index = json["index"].GetInt();
    if (json.HasMember("mid") && json["mid"].IsInt())
      mid = json["mid"].GetInt();
  }

  static void dumpCandles(std::ostream& os, int timestamp,
                          std::array<std::optional<CandleData>, 6>& candle) {
    os << std::to_string(timestamp) << std::string(",");
    if (candle[0].has_value()) {
      os << std::format("{},{},{},{},{},{},{},", candle[0]->open,
                        candle[0]->close, candle[0]->high, candle[0]->low,
                        candle[0]->volume, candle[0]->index, candle[0]->mid);
    } else {
      os << std::string(",,,,,,,");
    }
    if (candle[1].has_value()) {
      os << std::format("{},{},{},{},{},{},{},", candle[1]->open,
                        candle[1]->close, candle[1]->high, candle[1]->low,
                        candle[1]->volume, candle[1]->index, candle[1]->mid);
    } else {
      os << std::string(",,,,,,,");
    }
    if (candle[2].has_value()) {
      os << std::format("{},{},{},{},{},{},{},", candle[2]->open,
                        candle[2]->close, candle[2]->high, candle[2]->low,
                        candle[2]->volume, candle[2]->index, candle[2]->mid);
    } else {
      os << std::string(",,,,,,,");
    }
    if (candle[3].has_value()) {
      os << std::format("{},{},{},{},{},{},{},", candle[3]->open,
                        candle[3]->close, candle[3]->high, candle[3]->low,
                        candle[3]->volume, candle[3]->index, candle[3]->mid);
    } else {
      os << std::string(",,,,,,,");
    }
    if (candle[4].has_value()) {
      os << std::format("{},{},{},{},{},{},{},", candle[4]->open,
                        candle[4]->close, candle[4]->high, candle[4]->low,
                        candle[4]->volume, candle[4]->index, candle[4]->mid);
    } else {
      os << std::string(",,,,,,,");
    }
    if (candle[5].has_value()) {
      os << std::format("{},{},{},{},{},{},{},", candle[5]->open,
                        candle[5]->close, candle[5]->high, candle[5]->low,
                        candle[5]->volume, candle[5]->index, candle[5]->mid);
    } else {
      os << std::string(",,,,,,,");
    }
    os << std::string("\n");
  }
};