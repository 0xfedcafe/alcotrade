#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "../include/events.hpp"

// AddOrderRequest implementation
std::string AddOrderRequest::toJsonString() const {
  rapidjson::Document doc;
  doc.SetObject();
  auto& allocator = doc.GetAllocator();

  doc.AddMember("type", rapidjson::Value("add_order", allocator), allocator);
  doc.AddMember("instrument_id",
                rapidjson::Value(instrumentiD.c_str(), allocator), allocator);
  doc.AddMember("user_request_id",
                rapidjson::Value(userReqId.c_str(), allocator), allocator);
  doc.AddMember("price", price, allocator);
  doc.AddMember("expiry", expiry, allocator);
  doc.AddMember("side", rapidjson::Value(buy ? "bid" : "ask", allocator),
                allocator);
  doc.AddMember("quantity", quantity, allocator);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  return buffer.GetString();
}

// GetPendingOrdersRequest implementation
std::string GetPendingOrdersRequest::toJsonString() const {
  rapidjson::Document doc;
  doc.SetObject();
  auto& allocator = doc.GetAllocator();

  doc.AddMember("type", rapidjson::Value(type.c_str(), allocator), allocator);
  doc.AddMember("user_request_id",
                rapidjson::Value(user_request_id.c_str(), allocator),
                allocator);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  return buffer.GetString();
}

// GetInventoryRequest implementation
std::string GetInventoryRequest::toJsonString() const {
  rapidjson::Document doc;
  doc.SetObject();
  auto& allocator = doc.GetAllocator();

  doc.AddMember("type", rapidjson::Value(type.c_str(), allocator), allocator);
  doc.AddMember("user_request_id",
                rapidjson::Value(user_request_id.c_str(), allocator),
                allocator);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  return buffer.GetString();
}