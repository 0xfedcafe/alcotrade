import asyncio
import json
import websockets
import math
import numpy as np
from dataclasses import dataclass, asdict, field
from typing import Optional, List, Dict, Tuple, Any, Deque
from collections import defaultdict, deque
import time

InstrumentID_t = str
Price_t = int
Time_t = int
Quantity_t = int
OrderID_t = str
TeamID_t = str

@dataclass
class BaseMessage:
    type: str

@dataclass
class AddOrderRequest(BaseMessage):
    type: str = field(default="add_order", init=False)
    user_request_id: str
    instrument_id: InstrumentID_t
    price: Price_t
    expiry: Time_t
    side: str
    quantity: Quantity_t

@dataclass
class CancelOrderRequest(BaseMessage):
    type: str = field(default="cancel_order", init=False)
    user_request_id: str
    order_id: OrderID_t
    instrument_id: InstrumentID_t

@dataclass
class GetInventoryRequest(BaseMessage):
    type: str = field(default="get_inventory", init=False)
    user_request_id: str

@dataclass
class GetPendingOrdersRequest(BaseMessage):
    type: str = field(default="get_pending_orders", init=False)
    user_request_id: str

@dataclass
class WelcomeMessage(BaseMessage):
    type: str
    message: str

@dataclass
class AddOrderResponseData:
    order_id: Optional[OrderID_t] = None
    message: Optional[str] = None
    immediate_inventory_change: Optional[Quantity_t] = None
    immediate_balance_change: Optional[Quantity_t] = None

@dataclass
class AddOrderResponse(BaseMessage):
    type: str
    user_request_id: str
    success: bool
    data: AddOrderResponseData

@dataclass
class CancelOrderResponse(BaseMessage):
    type: str
    user_request_id: str
    success: bool
    message: Optional[str] = None

@dataclass
class ErrorResponse(BaseMessage):
    type: str
    user_request_id: str
    message: str

@dataclass
class GetInventoryResponse(BaseMessage):
    type: str
    user_request_id: str
    data: Dict[InstrumentID_t, Tuple[Quantity_t, Quantity_t]]

@dataclass
class OrderJSON:
    orderID: OrderID_t
    teamID: TeamID_t
    price: Price_t
    time: Time_t
    expiry: Time_t
    side: str
    unfilled_quantity: Quantity_t
    total_quantity: Quantity_t
    live: bool

@dataclass
class GetPendingOrdersResponse:
    type: str
    user_request_id: str
    data: Dict[InstrumentID_t, Tuple[List[OrderJSON], List[OrderJSON]]]

@dataclass
class OrderbookDepth:
    bids: Dict[Price_t, Quantity_t]
    asks: Dict[Price_t, Quantity_t]

@dataclass
class CandleDataResponse:
    tradeable: Dict[InstrumentID_t, List[Dict[str, Any]]]
    untradeable: Dict[InstrumentID_t, List[Dict[str, Any]]]

Trade_t = Dict[str, Any]
Settlement_t = Dict[str, Any]
Cancel_t = Dict[str, Any]

@dataclass
class MarketDataResponse(BaseMessage):
    type: str
    time: Time_t
    candles: CandleDataResponse
    orderbook_depths: Dict[InstrumentID_t, OrderbookDepth]
    events: List[Dict[str, Any]]
    user_request_id: Optional[str] = None

@dataclass
class InstrumentData:
    """Track price history and volatility for an instrument"""
    prices: Deque[float] = field(default_factory=lambda: deque(maxlen=50))
    timestamps: Deque[float] = field(default_factory=lambda: deque(maxlen=50))
    mid_prices: Deque[float] = field(default_factory=lambda: deque(maxlen=50))
    volatility: float = 0.0
    last_volatility_update: float = 0.0
    trades_seen: int = 0
    last_trade_time: float = 0.0
    
class VolatilityMetrics:
    """Calculate various volatility measures"""
    
    @staticmethod
    def ewma_volatility(returns: List[float], alpha: float = 0.90) -> float:
        """Exponentially weighted moving average volatility"""
        if len(returns) < 2:
            return 0.0
        
        ewma_var = 0.0
        for ret in returns:
            ewma_var = alpha * ewma_var + (1 - alpha) * ret * ret
        
        return math.sqrt(ewma_var)
    
    @staticmethod
    def realized_volatility(returns: List[float]) -> float:
        """Simple realized volatility (std dev of returns)"""
        if len(returns) < 2:
            return 0.0
        
        mean_return = sum(returns) / len(returns)
        variance = sum((r - mean_return) ** 2 for r in returns) / (len(returns) - 1)
        return math.sqrt(variance)
    
    @staticmethod
    def range_volatility(highs: List[float], lows: List[float]) -> float:
        """Volatility based on high-low ranges"""
        if len(highs) != len(lows) or len(highs) < 1:
            return 0.0
        
        ranges = [(h - l) / ((h + l) / 2) for h, l in zip(highs, lows) if h + l > 0]
        if not ranges:
            return 0.0
            
        return sum(ranges) / len(ranges)

global_user_request_id = 0

class VolatilityTradingBot:
    def __init__(self, uri: str, team_secret: str, print_market_data: bool = False):
        self.uri = f"{uri}?team_secret={team_secret}"
        self.ws = None
        self._pending: Dict[str, asyncio.Future] = {}
        self.print_market_data = print_market_data
        self.fee_buffer = 0.0004

        # Volatility tracking
        self.instruments: Dict[InstrumentID_t, InstrumentData] = {}
        self.volatility_metrics = VolatilityMetrics()
        
        # Strategy parameters
        self.min_volatility_threshold = 0.01  # Minimum vol to trade
        self.max_volatility_threshold = 0.5   # Maximum vol to trade
        self.base_spread_bps = 50             # Base spread in basis points
        self.volatility_multiplier = 2.0     # How much to scale spreads by volatility
        self.max_positions = 3                # Max concurrent positions
        self.min_trade_interval = 2.0        # Minimum seconds between trades per instrument
        self.min_book_depth = 10
        
        # Risk management
        self.max_position_size = 5
        self.stop_loss_threshold = 0.02  # 2% stop loss
        self.profit_target_multiplier = 1.5  # Take profit at 1.5x the spread
        
        # Active trading state
        self.active_trades: Dict[InstrumentID_t, Dict] = {}
        self.last_trade_times: Dict[InstrumentID_t, float] = {}

    async def connect(self):
        self.ws = await websockets.connect(self.uri)
        welcome_data = json.loads(await self.ws.recv())
        welcome_message = WelcomeMessage(**welcome_data)
        
        print(json.dumps({"welcome": asdict(welcome_message)}, indent=2))
        asyncio.create_task(self._receive_loop())

    async def _receive_loop(self):
        assert self.ws, "Websocket connection not established."
        async for msg in self.ws:
            data = json.loads(msg)

            if not (data.get("type") == "market_data_update" and not self.print_market_data):
                if self.print_market_data:
                    print(json.dumps({"message": data}, indent=2))

            rid = data.get("user_request_id")
            if rid and rid in self._pending:
                self._pending[rid].set_result(data)
                del self._pending[rid]

            msg_type = data.get("type")
            if msg_type == "market_data_update":
                try:
                    # Parse orderbook depths with proper type conversion
                    parsed_orderbook_depths = {}
                    for instr_id, depth_data in data.get("orderbook_depths", {}).items():
                        # Convert price keys from strings to integers
                        bids = {}
                        asks = {}
                        
                        for price_str, quantity in depth_data.get("bids", {}).items():
                            try:
                                bids[int(float(price_str))] = quantity
                            except (ValueError, TypeError):
                                continue
                        
                        for price_str, quantity in depth_data.get("asks", {}).items():
                            try:
                                asks[int(float(price_str))] = quantity
                            except (ValueError, TypeError):
                                continue
                        
                        parsed_orderbook_depths[instr_id] = OrderbookDepth(bids=bids, asks=asks)
                    
                    parsed_candles = CandleDataResponse(**data.get("candles", {}))

                    market_data = MarketDataResponse(
                        type=data["type"],
                        time=data["time"],
                        candles=parsed_candles,
                        orderbook_depths=parsed_orderbook_depths,
                        events=data.get("events", []),
                        user_request_id=data.get("user_request_id")
                    )
                    await self._handle_market_data_update(market_data)
                except KeyError as e:
                    print(f"Error: Missing expected key in MarketDataResponse: {e}")
                except Exception as e:
                    print(f"Error deserializing MarketDataResponse: {e}")

    def _calculate_mid_price(self, orderbook: OrderbookDepth) -> Optional[float]:
        """Calculate mid price from orderbook"""
        if not orderbook.bids or not orderbook.asks:
            return None
        
        # Keys should now be integers after parsing
        try:
            best_bid = max(orderbook.bids.keys())
            best_ask = min(orderbook.asks.keys())
            
            return (best_bid + best_ask) / 2.0
        except (ValueError, TypeError) as e:
            print(f"Error calculating mid price: {e}")
            return None

    def _update_volatility(self, instrument_id: InstrumentID_t, mid_price: float, timestamp: float):
        """Update volatility estimates for an instrument"""

        # coerce timestamp to float (ms); bail out if it’s not numeric
        try:
            timestamp = float(timestamp)
        except (TypeError, ValueError):
            return

        if instrument_id not in self.instruments:
            self.instruments[instrument_id] = InstrumentData()
        
        data = self.instruments[instrument_id]
        current_time = timestamp / 1000.0  # Convert to seconds
        
        # Add new price point
        data.mid_prices.append(mid_price)
        data.timestamps.append(current_time)
        
        # Need at least 2 points to calculate returns
        if len(data.mid_prices) < 2:
            return
        
        # Calculate returns
        returns = []
        for i in range(1, len(data.mid_prices)):
            if data.mid_prices[i-1] > 0:
                ret = (data.mid_prices[i] - data.mid_prices[i-1]) / data.mid_prices[i-1]
                returns.append(ret)
        
        if len(returns) < 2:
            return
        
        # Calculate volatility using EWMA
        data.volatility = self.volatility_metrics.ewma_volatility(returns)
        data.last_volatility_update = current_time
        
        # Print volatility updates for monitoring
        if len(returns) >= 5:  # Only print after we have some data
            print(f"Volatility Update - {instrument_id}: {data.volatility:.4f} (n_returns={len(returns)})")
        # Calculate volatility using EWMA
        data.volatility = self.volatility_metrics.ewma_volatility(returns)
        data.last_volatility_update = current_time
        
        # Print volatility updates for monitoring
        if len(returns) >= 5:  # Only print after we have some data
            print(f"Volatility Update - {instrument_id}: {data.volatility:.4f} (n_returns={len(returns)})")

    def _get_volatility_regime(self, volatility: float) -> str:
        """Classify volatility regime"""
        if volatility < 0.005:
            return "VERY_LOW"
        elif volatility < 0.02:
            return "LOW"
        elif volatility < 0.05:
            return "MEDIUM"
        elif volatility < 0.1:
            return "HIGH"
        else:
            return "VERY_HIGH"

    def _calculate_optimal_spread(self, instrument_id: InstrumentID_t, mid_price: float) -> Tuple[float, float]:
        """Calculate optimal bid-ask spread based on volatility"""
        if instrument_id not in self.instruments:
            return mid_price * 0.99, mid_price * 1.01
        
        data = self.instruments[instrument_id]
        vol = data.volatility
        
        if vol == 0:
            vol = 0.01  # Default volatility
        
        # Base spread scaled by volatility
        spread_factor = self.base_spread_bps / 10000.0  # Convert bps to decimal
        volatility_adjustment = 1.0 + (vol * self.volatility_multiplier)
        
        total_spread = spread_factor * volatility_adjustment
        half_spread = total_spread / 2.0
        
        bid_price = mid_price * (1 - half_spread - self.fee_buffer/2)
        ask_price = mid_price * (1 + half_spread + self.fee_buffer/2)
        
        # Ensure prices are reasonable integers
        bid_price = max(1, int(bid_price))
        ask_price = max(bid_price + 1, int(ask_price))
        
        return float(bid_price), float(ask_price)

    def _calculate_position_size(self, instrument_id: InstrumentID_t, volatility: float) -> int:
        """Calculate position size based on volatility (Kelly-like sizing)"""
        if volatility == 0:
            return 1
        
        # Inverse relationship: higher volatility = smaller position
        base_size = 2
        vol_adjustment = min(0.05 / max(volatility, 0.001), 3.0)  # Cap the adjustment
        
        size = int(base_size * vol_adjustment)
        return max(1, min(size, self.max_position_size))

    def _should_trade(self, instrument_id: InstrumentID_t) -> bool:
        """Determine if we should trade this instrument based on volatility and timing"""
        if instrument_id not in self.instruments:
            return False
        
        data = self.instruments[instrument_id]
        vol = data.volatility
        
        # Check volatility bounds
        if vol < self.min_volatility_threshold or vol > self.max_volatility_threshold:
            return False
                
        # Check if enough time has passed since last trade
        current_time = time.time()
        if instrument_id in self.last_trade_times:
            time_since_last = current_time - self.last_trade_times[instrument_id]
            if time_since_last < self.min_trade_interval:
                return False
        
        # Check if we have too many active positions
        if len(self.active_trades) >= self.max_positions:
            return False
        
        # Check if we already have a position in this instrument
        if instrument_id in self.active_trades:
            return False
        
        return True

    async def _handle_market_data_update(self, data: MarketDataResponse):
        """Handle market data updates and make trading decisions"""
        current_time = time.time()
        
        for instrument_id, orderbook in data.orderbook_depths.items():
            if not instrument_id.startswith("$CARD"):
                continue
            
            # Calculate mid price
            mid_price = self._calculate_mid_price(orderbook)
            if mid_price is None:
                continue
            
            # Update volatility
            self._update_volatility(instrument_id, mid_price, data.time)
            
            # Check if we should trade
            if not self._should_trade(instrument_id):
                continue
            
            # Get volatility data
            vol_data = self.instruments[instrument_id]
            vol_regime = self._get_volatility_regime(vol_data.volatility)
            
            print(f"\n=== TRADING OPPORTUNITY ===")
            print(f"Instrument: {instrument_id}")
            print(f"Mid Price: {mid_price:.2f}")
            print(f"Volatility: {vol_data.volatility:.4f} ({vol_regime})")
            
            # Calculate optimal prices and position size
            bid_price, ask_price = self._calculate_optimal_spread(instrument_id, mid_price)
            position_size = self._calculate_position_size(instrument_id, vol_data.volatility)
            
            print(f"Calculated Bid: {bid_price:.2f}, Ask: {ask_price:.2f}")
            print(f"Position Size: {position_size}")
            
            # Execute volatility-based trading strategy
            asyncio.create_task(self._execute_volatility_strategy(
                instrument_id, mid_price, bid_price, ask_price, position_size
            ))

    async def _execute_volatility_strategy(self, instrument_id: InstrumentID_t, 
                                         mid_price: float, bid_price: float, 
                                         ask_price: float, position_size: int):
        """Execute the volatility-based trading strategy"""
        try:
            current_time = time.time()
            self.last_trade_times[instrument_id] = current_time
            
            print(f"Executing volatility strategy for {instrument_id}")
            
            # Place initial buy order at calculated bid price
            buy_response = await self.buy(instrument_id, int(bid_price), position_size)
            
            if not (isinstance(buy_response, AddOrderResponse) and buy_response.success):
                print(f"Buy order failed for {instrument_id}")
                return
            
            buy_order_id = buy_response.data.order_id
            print(f"Buy order placed: {buy_order_id}")
            
            # Wait a bit to see if order fills
            await asyncio.sleep(0.1)
            
            # Check if we got the position
            inventory = await self.get_inventory()
            if not isinstance(inventory, GetInventoryResponse):
                print(f"Failed to get inventory for {instrument_id}")
                return
            
            reserved, owned = inventory.data.get(instrument_id, (0, 0))
            
            if owned > 0:
                print(f"Position acquired: {owned} units of {instrument_id}")
                
                # Place sell order with volatility-adjusted pricing
                sell_price = int(ask_price)
                sell_response = await self.sell(instrument_id, sell_price, owned)
                
                if isinstance(sell_response, AddOrderResponse) and sell_response.success:
                    sell_order_id = sell_response.data.order_id
                    print(f"Sell order placed: {sell_order_id} at {sell_price}")
                    
                    # Track this trade
                    self.active_trades[instrument_id] = {
                        'buy_price': bid_price,
                        'sell_price': sell_price,
                        'position_size': owned,
                        'sell_order_id': sell_order_id,
                        'entry_time': current_time
                    }
                    
                    # Monitor the sell order
                    asyncio.create_task(self._monitor_sell_order(instrument_id, sell_order_id))
                else:
                    print(f"Failed to place sell order for {instrument_id}")
            else:
                print(f"No position acquired for {instrument_id}, cancelling buy order")
                if buy_order_id:
                    await self.cancel(instrument_id, buy_order_id)
                    
        except Exception as e:
            print(f"Error in volatility strategy execution: {e}")

    async def _monitor_sell_order(self, instrument_id: InstrumentID_t, sell_order_id: OrderID_t):
        """Monitor sell order and implement risk management"""
        try:
            max_wait_time = 30.0  # Wait up to 30 seconds
            check_interval = 0.5
            waited_time = 0.0
            
            while waited_time < max_wait_time:
                await asyncio.sleep(check_interval)
                waited_time += check_interval
                current_mid = self.instruments[instrument_id].mid_prices[-1]
                entry = self.active_trades[instrument_id]['buy_price']
                if current_mid <= entry * (1 - self.stop_loss_threshold):
                    # cancel the outstanding ask, then send a market sell at current_mid
                    await self.cancel(instrument_id, sell_order_id)
                    await self.sell(instrument_id, int(current_mid), self.active_trades[instrument_id]['position_size'])
                    return
                pending_orders = await self.get_pending_orders()
                
                # Check if order is still pending
                pending_orders = await self.get_pending_orders()
                if not isinstance(pending_orders, GetPendingOrdersResponse):
                    continue
                
                order_still_live = False
                if instrument_id in pending_orders.data:
                    _, asks = pending_orders.data[instrument_id]
                    for order in asks:
                        if order.orderID == sell_order_id:
                            order_still_live = True
                            break
                
                if not order_still_live:
                    print(f"Sell order completed for {instrument_id}")
                    if instrument_id in self.active_trades:
                        del self.active_trades[instrument_id]
                    return
                
                print(f"Sell order still pending for {instrument_id} ({waited_time:.1f}s)")
            
            # Timeout reached, cancel the order
            print(f"Sell order timeout for {instrument_id}, cancelling...")
            cancel_response = await self.cancel(instrument_id, sell_order_id)
            
            if isinstance(cancel_response, CancelOrderResponse) and cancel_response.success:
                print(f"Successfully cancelled sell order for {instrument_id}")
            
            # Clean up
            if instrument_id in self.active_trades:
                del self.active_trades[instrument_id]
                
        except Exception as e:
            print(f"Error monitoring sell order: {e}")

    async def send(self, payload: BaseMessage, timeout: int = 3):
        global global_user_request_id
        rid = str(global_user_request_id).zfill(10)
        global_user_request_id += 1

        payload.user_request_id = rid
        payload_dict = asdict(payload)

        fut = asyncio.get_event_loop().create_future()
        self._pending[rid] = fut

        await self.ws.send(json.dumps(payload_dict))

        try:
            resp = await asyncio.wait_for(fut, timeout)
            if resp.get("type") == "add_order_response":
                resp['data'] = AddOrderResponseData(**resp.get('data', {}))
                return AddOrderResponse(**resp)
            elif resp.get("type") == "cancel_order_response":
                return CancelOrderResponse(**resp)
            elif resp.get("type") == "get_inventory_response":
                return GetInventoryResponse(**resp)
            elif resp.get("type") == "get_pending_orders_response":
                parsed_data = {}
                for instr_id, (bids_raw, asks_raw) in resp.get('data', {}).items():
                    parsed_bids = [OrderJSON(**order_data) for order_data in bids_raw]
                    parsed_asks = [OrderJSON(**order_data) for order_data in asks_raw]
                    parsed_data[instr_id] = (parsed_bids, parsed_asks)
                resp['data'] = parsed_data
                return GetPendingOrdersResponse(**resp)
            elif resp.get("type") == "error":
                return ErrorResponse(**resp)
            else:
                return resp
        except asyncio.TimeoutError:
            if rid in self._pending:
                del self._pending[rid]
            return {"success": False, "user_request_id": rid, "message": "Request timed out"}

    async def buy(self, instrument_id: InstrumentID_t, price: Price_t, quantity: int = 1):
        expiry = int(instrument_id.split("_")[-1]) + 10
        buy_request = AddOrderRequest(
            user_request_id="",
            instrument_id=instrument_id,
            price=price,
            quantity=quantity,
            side="bid",
            expiry=expiry * 1000
        )
        return await self.send(buy_request)

    async def sell(self, instrument_id: InstrumentID_t, price: Price_t, quantity: int = 1):
        expiry = int(instrument_id.split("_")[-1]) + 10
        sell_request = AddOrderRequest(
            user_request_id="",
            instrument_id=instrument_id,
            price=price,
            quantity=quantity,
            side="ask",
            expiry=expiry * 1000
        )
        return await self.send(sell_request)

    async def cancel(self, instrument_id: InstrumentID_t, order_id: OrderID_t):
        cancel_request = CancelOrderRequest(
            user_request_id="",
            order_id=order_id,
            instrument_id=instrument_id
        )
        return await self.send(cancel_request)

    async def get_inventory(self):
        get_inventory_request = GetInventoryRequest(user_request_id="")
        return await self.send(get_inventory_request)

    async def get_pending_orders(self):
        get_pending_request = GetPendingOrdersRequest(user_request_id="")
        return await self.send(get_pending_request)

async def main():
    EXCHANGE_URI = "ws://192.168.100.10:9001/trade"
    TEAM_SECRET = "1b65ebbc-eacd-4bd0-a215-e3be980f38d0"

    bot = VolatilityTradingBot(
        EXCHANGE_URI,
        TEAM_SECRET,
        print_market_data=False
    )

    print("Starting Volatility-Based Trading Bot...")
    print("Strategy: Adaptive spreads and position sizing based on realized volatility")
    print("=" * 60)

    await bot.connect()
    await asyncio.Future()

if __name__ == '__main__':
    asyncio.run(main())