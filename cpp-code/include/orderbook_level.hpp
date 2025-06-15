#pragma once
#include <rapidjson/document.h>
#include <map>
#include <string>


struct OrderBookLevel {
  std::map<int, int> bids;
  std::map<int, int> asks;

  OrderBookLevel() = default;
  OrderBookLevel(const rapidjson::Value& json) {
    if (json.HasMember("bids") && json["bids"].IsObject()) {
      for (auto& member : json["bids"].GetObject()) {
        try {
          int price = std::stoi(member.name.GetString());
          int quantity = member.value.GetInt();
          bids[price] = quantity;
        } catch (const std::exception&) {
          // Skip invalid price strings
        }
      }
    }
    if (json.HasMember("asks") && json["asks"].IsObject()) {
      for (auto& member : json["asks"].GetObject()) {
        try {
          int price = std::stoi(member.name.GetString());
          int quantity = member.value.GetInt();
          asks[price] = quantity;
        } catch (const std::exception&) {
          // Skip invalid price strings
        }
      }
    }
  }
};