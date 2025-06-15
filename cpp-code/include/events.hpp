#pragma once
#include <map>
#include <vector>
#pragma once
#include <rapidjson/document.h>

#include <string>

struct AddOrderRequest {
  std::string userReqId;
  std::string instrumentiD;
  int price;
  int expiry;
  bool buy;
  int quantity;

  AddOrderRequest() = default;
  std::string toJsonString() const;
};

struct AddOrderResponse {
  std::string type;
  std::string user_request_id;
  bool success = false;
  int order_id = 0;

  AddOrderResponse() = default;
  AddOrderResponse(const rapidjson::Value& json) {
    if (json.HasMember("type") && json["type"].IsString()) {
      type = json["type"].GetString();
    }
    if (json.HasMember("user_request_id") &&
        json["user_request_id"].IsString()) {
      user_request_id = json["user_request_id"].GetString();
    }
    if (json.HasMember("success") && json["success"].IsBool()) {
      success = json["success"].GetBool();
    }
    if (json.HasMember("data") && json["data"].IsObject()) {
      const auto& data = json["data"];
      if (data.HasMember("order_id") && data["order_id"].IsInt()) {
        order_id = data["order_id"].GetInt();
      }
    }
  }
};

struct GetPendingOrdersRequest {
  std::string type = "get_pending_orders";
  std::string user_request_id;

  std::string toJsonString() const;
};

struct PendingOrder {
  int orderID = 0;
  int teamID = 0;
  int price = 0;
  long time = 0;
  long expiry = 0;
  std::string side;
  int unfilled_quantity = 0;
  int total_quantity = 0;
  bool live = false;

  PendingOrder() = default;
  PendingOrder(const rapidjson::Value& json) {
    if (json.HasMember("orderID") && json["orderID"].IsInt()) {
      orderID = json["orderID"].GetInt();
    }
    if (json.HasMember("teamID") && json["teamID"].IsInt()) {
      teamID = json["teamID"].GetInt();
    }
    if (json.HasMember("price") && json["price"].IsInt()) {
      price = json["price"].GetInt();
    }
    if (json.HasMember("time") && json["time"].IsInt64()) {
      time = json["time"].GetInt64();
    }
    if (json.HasMember("expiry") && json["expiry"].IsInt64()) {
      expiry = json["expiry"].GetInt64();
    }
    if (json.HasMember("side") && json["side"].IsString()) {
      side = json["side"].GetString();
    }
    if (json.HasMember("unfilled_quantity") &&
        json["unfilled_quantity"].IsInt()) {
      unfilled_quantity = json["unfilled_quantity"].GetInt();
    }
    if (json.HasMember("total_quantity") && json["total_quantity"].IsInt()) {
      total_quantity = json["total_quantity"].GetInt();
    }
    if (json.HasMember("live") && json["live"].IsBool()) {
      live = json["live"].GetBool();
    }
  }
};

struct GetPendingOrdersResponse {
  std::string type;
  std::string user_request_id;
  std::map<std::string,
           std::pair<std::vector<PendingOrder>, std::vector<PendingOrder>>>
      data;  // instrument -> (bids, asks)

  GetPendingOrdersResponse() = default;
  GetPendingOrdersResponse(const rapidjson::Value& json) {
    if (json.HasMember("type") && json["type"].IsString()) {
      type = json["type"].GetString();
    }
    if (json.HasMember("user_request_id") &&
        json["user_request_id"].IsString()) {
      user_request_id = json["user_request_id"].GetString();
    }
    if (json.HasMember("data") && json["data"].IsObject()) {
      const auto& dataObj = json["data"];
      for (auto& member : dataObj.GetObject()) {
        const char* instrumentName = member.name.GetString();
        const auto& instrumentData = member.value;

        if (instrumentData.IsArray() && instrumentData.Size() >= 2) {
          std::vector<PendingOrder> bids, asks;

          // Parse bids (first array)
          if (instrumentData[0].IsArray()) {
            for (rapidjson::SizeType i = 0; i < instrumentData[0].Size(); ++i) {
              if (instrumentData[0][i].IsObject()) {
                bids.emplace_back(instrumentData[0][i]);
              }
            }
          }

          // Parse asks (second array)
          if (instrumentData[1].IsArray()) {
            for (rapidjson::SizeType i = 0; i < instrumentData[1].Size(); ++i) {
              if (instrumentData[1][i].IsObject()) {
                asks.emplace_back(instrumentData[1][i]);
              }
            }
          }

          data[instrumentName] =
              std::make_pair(std::move(bids), std::move(asks));
        }
      }
    }
  }
};

struct GetInventoryRequest {
  std::string type = "get_inventory";
  std::string user_request_id;

  std::string toJsonString() const;
};

struct InventoryItem {
  int reserved = 0;
  int total = 0;

  InventoryItem() = default;
  InventoryItem(int res, int tot) : reserved(res), total(tot) {}
  InventoryItem(const rapidjson::Value& json) {
    if (json.IsArray() && json.Size() >= 2) {
      if (json[0].IsInt()) {
        reserved = json[0].GetInt();
      }
      if (json[1].IsInt()) {
        total = json[1].GetInt();
      }
    }
  }
};

struct GetInventoryResponse {
  std::string type;
  std::string user_request_id;
  std::map<std::string, InventoryItem> data;  // instrument -> [reserved, total]

  GetInventoryResponse() = default;
  GetInventoryResponse(const rapidjson::Value& json) {
    if (json.HasMember("type") && json["type"].IsString()) {
      type = json["type"].GetString();
    }
    if (json.HasMember("user_request_id") &&
        json["user_request_id"].IsString()) {
      user_request_id = json["user_request_id"].GetString();
    }
    if (json.HasMember("data") && json["data"].IsObject()) {
      const auto& dataObj = json["data"];
      for (auto& member : dataObj.GetObject()) {
        const char* instrumentName = member.name.GetString();
        const auto& inventoryArray = member.value;

        if (inventoryArray.IsArray() && inventoryArray.Size() >= 2 &&
            inventoryArray[0].IsInt() && inventoryArray[1].IsInt()) {
          data[instrumentName] = InventoryItem(inventoryArray[0].GetInt(),
                                               inventoryArray[1].GetInt());
        }
      }
    }
  }
};

struct TradeEvent {
  std::string instrumentID;
  int passiveOrderID = 0;
  int activeOrderID = 0;
  int quantity = 0;
  int price = 0;
  long time = 0;

  TradeEvent() = default;
  TradeEvent(const rapidjson::Value& data) {
    if (data.HasMember("instrumentID") && data["instrumentID"].IsString()) {
      instrumentID = data["instrumentID"].GetString();
    }
    if (data.HasMember("passiveOrderID") && data["passiveOrderID"].IsInt()) {
      passiveOrderID = data["passiveOrderID"].GetInt();
    }
    if (data.HasMember("activeOrderID") && data["activeOrderID"].IsInt()) {
      activeOrderID = data["activeOrderID"].GetInt();
    }
    if (data.HasMember("quantity") && data["quantity"].IsInt()) {
      quantity = data["quantity"].GetInt();
    }
    if (data.HasMember("price") && data["price"].IsInt()) {
      price = data["price"].GetInt();
    }
    if (data.HasMember("time") && data["time"].IsInt64()) {
      time = data["time"].GetInt64();
    }
  }
};

struct CancelEvent {
  int orderID = 0;
  std::string instrumentID;
  long time = 0;
  bool expired = false;

  CancelEvent() = default;
  CancelEvent(const rapidjson::Value& data) {
    if (data.HasMember("orderID") && data["orderID"].IsInt())
      orderID = data["orderID"].GetInt();
    if (data.HasMember("instrumentID") && data["instrumentID"].IsString())
      instrumentID = data["instrumentID"].GetString();
    if (data.HasMember("time") && data["time"].IsInt64())
      time = data["time"].GetInt64();
    if (data.HasMember("expired") && data["expired"].IsBool())
      expired = data["expired"].GetBool();
  }
};