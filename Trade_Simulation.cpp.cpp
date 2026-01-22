
   #include <websocketpp/config/asio_client.hpp>
   #include <websocketpp/client.hpp>
   #include <rapidjson/document.h>
   #include <rapidjson/stringbuffer.h>
   #include <rapidjson/writer.h>
   #include <iostream>
   #include <deque>
   #include <chrono>
   #include <cmath>
   #include <thread>
   #include <iomanip>
   #include <mutex>
   #include <queue>
   #include <numeric>

   using websocketpp::lib::placeholders::_1;
   using websocketpp::lib::placeholders::_2;
   using websocketpp::lib::bind;

   typedef websocketpp::client<websocketpp::config::asio_tls_client> Client;

   // Orderbook structure to store bids/asks and timestamp
   struct Orderbook {
       std::deque<std::pair<double, double>> bids; // price, quantity
       std::deque<std::pair<double, double>> asks;
       std::chrono::milliseconds timestamp;
       std::mutex mutex; // Thread safety for updates
   };

   // UI data structure for queuing output parameters
   struct UIData {
       double slippage;
       double fees;
       double market_impact;
       double net_cost;
       double maker_proportion;
       double volatility;
       double latency_ms;
   };

   // Trade simulator class integrating WebSocket, orderbook processing, and models
   class TradeSimulator {
   private:
       Client client_;
       websocketpp::connection_hdl connection_;
       Orderbook orderbook_;
       // Input parameters
       double quantity_ = 0.0021; // ~100 USD at ~47,619 USD/BTC
       double volatility_ = 0.01; // Initial volatility estimate
       double maker_fee_ = 0.001; // 0.1%
       double taker_fee_ = 0.0015; // 0.15%
       std::string exchange_ = "OKX";
       std::string symbol_ = "BTC-USDT";
       std::string order_type_ = "market";
       std::string fee_tier_ = "Regular";
       // Output parameters
       double slippage_ = 0.0; // Percentage price deviation
       double fees_ = 0.0; // USD
       double market_impact_ = 0.0; // USD
       double net_cost_ = 0.0; // USD
       double maker_proportion_ = 0.5; // Maker vs. taker liquidity
       double latency_ms_ = 0.0; // Calculation latency
       // Cached values
       double cached_mid_price_ = 0.0; // (Best bid + best ask) / 2
       double cached_spread_ = 0.0; // Best ask - best bid
       // Benchmarking
       std::vector<double> json_parse_latencies_;
       std::vector<double> data_proc_latencies_;
       std::vector<double> ui_update_latencies_;
       std::vector<double> e2e_latencies_;
       size_t update_count_ = 0;
       const size_t benchmark_interval_ = 100; // Report every 100 updates
       // UI thread
       std::thread ui_thread_;
       std::mutex ui_mutex_;
       std::queue<UIData> ui_queue_;
       bool running_ = true;
       // MODIFIED FOR VIDEO: Track if JSON has been logged
       bool logged_json_ = false;

   public:
       TradeSimulator() {
           // Initialize WebSocket client
           client_.clear_access_channels(websocketpp::log::alevel::all);
           client_.set_access_channels(websocketpp::log::alevel::connect | websocketpp::log::alevel::disconnect);
           client_.set_error_channels(websocketpp::log::elevel::all);
           client_.init_asio();
           client_.set_tls_init_handler(bind(&TradeSimulator::on_tls_init, this, ::_1));
           client_.set_open_handler(bind(&TradeSimulator::on_open, this, ::_1));
           client_.set_message_handler(bind(&TradeSimulator::on_message, this, ::_1, ::_2));
           client_.set_fail_handler(bind(&TradeSimulator::on_fail, this, ::_1));
           // Pre-allocate latency vectors for performance
           json_parse_latencies_.reserve(benchmark_interval_);
           data_proc_latencies_.reserve(benchmark_interval_);
           ui_update_latencies_.reserve(benchmark_interval_);
           e2e_latencies_.reserve(benchmark_interval_);
           // Start UI thread
           ui_thread_ = std::thread(&TradeSimulator::ui_thread_func, this);
       }

       ~TradeSimulator() {
           running_ = false;
           if (ui_thread_.joinable()) {
               ui_thread_.join();
           }
       }

       // TLS initialization for WebSocket connection
       websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context> on_tls_init(websocketpp::connection_hdl) {
           auto ctx = websocketpp::lib::make_shared<websocketpp::lib::asio::ssl::context>(
               websocketpp::lib::asio::ssl::context::tls_client);
           try {
               ctx->set_verify_mode(websocketpp::lib::asio::ssl::verify_none);
               std::cout << "TLS: Verification disabled for testing" << std::endl;
           } catch (const std::exception& e) {
               std::cerr << "TLS init error: " << e.what() << std::endl;
           }
           return ctx;
       }

       // Connect to OKX WebSocket
       void connect(const std::string& uri) {
           websocketpp::lib::error_code ec;
           Client::connection_ptr con = client_.get_connection(uri, ec);
           if (ec) {
               std::cerr << "Connection setup error: " << ec.message() << std::endl;
               return;
           }
           connection_ = con->get_handle();
           client_.connect(con);
           client_.run();
       }

       // Subscribe to BTC-USDT orderbook on connection
       void on_open(websocketpp::connection_hdl hdl) {
           std::cout << "Connected to WebSocket" << std::endl;
           rapidjson::Document subscribe_msg;
           subscribe_msg.SetObject();
           rapidjson::Document::AllocatorType& allocator = subscribe_msg.GetAllocator();
           subscribe_msg.AddMember("op", "subscribe", allocator);
           rapidjson::Value args(rapidjson::kArrayType);
           rapidjson::Value arg(rapidjson::kObjectType);
           arg.AddMember("channel", "books", allocator);
           arg.AddMember("instId", "BTC-USDT", allocator);
           args.PushBack(arg, allocator);
           subscribe_msg.AddMember("args", args, allocator);
           rapidjson::StringBuffer buffer;
           rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
           subscribe_msg.Accept(writer);
           client_.send(hdl, buffer.GetString(), websocketpp::frame::opcode::text);
       }

       void on_fail(websocketpp::connection_hdl hdl) {
           auto con = client_.get_con_from_hdl(hdl);
           std::cerr << "WebSocket connection failed: " << con->get_ec().message()
                     << " (code: " << con->get_ec().value() << ")" << std::endl;
       }

       // Process incoming WebSocket messages
       void on_message(websocketpp::connection_hdl, Client::message_ptr msg) {
           auto e2e_start = std::chrono::high_resolution_clock::now();
           auto json_parse_start = e2e_start;
           try {
               // MODIFIED FOR VIDEO: Log raw JSON once for demo
               if (!logged_json_) {
                   std::cout << "Raw JSON (first update): " << msg->get_payload().substr(0, 100) << "..." << std::endl;
                   logged_json_ = true;
               }
               rapidjson::Document data;
               data.Parse(msg->get_payload().c_str()); // In-situ parsing for speed
               auto json_parse_end = std::chrono::high_resolution_clock::now();
               double json_parse_ms = std::chrono::duration<double, std::milli>(json_parse_end - json_parse_start).count();
               if (!data.IsObject() || !data.HasMember("data") || !data["data"].IsArray() || data["data"].Empty()) {
                   std::cerr << "Invalid message format" << std::endl;
                   return;
               }
               auto data_proc_start = json_parse_end;
               process_orderbook(data["data"][0]);
               auto data_proc_end = std::chrono::high_resolution_clock::now();
               double data_proc_ms = std::chrono::duration<double, std::milli>(data_proc_end - data_proc_start).count();
               auto calc_start = data_proc_end;
               calculate_outputs();
               auto calc_end = std::chrono::high_resolution_clock::now();
               double calc_ms = std::chrono::duration<double, std::milli>(calc_end - calc_start).count();
               // Queue UI update
               {
                   std::lock_guard<std::mutex> lock(ui_mutex_);
                   ui_queue_.push({slippage_, fees_, market_impact_, net_cost_, maker_proportion_, volatility_, calc_ms});
               }
               // Benchmarking
               json_parse_latencies_.push_back(json_parse_ms);
               data_proc_latencies_.push_back(data_proc_ms);
               e2e_latencies_.push_back(std::chrono::duration<double, std::milli>(calc_end - e2e_start).count());
               update_count_++;
               // MODIFIED FOR VIDEO: Simplified latency output
               std::cout << "Update #" << update_count_ << ": JSON Parse=" << json_parse_ms << " ms, E2E="
                         << std::chrono::duration<double, std::milli>(calc_end - e2e_start).count() << " ms" << std::endl;
               if (update_count_ % benchmark_interval_ == 0) {
                   report_benchmarks();
               }
           } catch (const std::exception& e) {
               std::cerr << "Error parsing message: " << e.what() << std::endl;
           }
       }

       // Process orderbook data from OKX
       void process_orderbook(const rapidjson::Value& data) {
           std::lock_guard<std::mutex> lock(orderbook_.mutex);
           orderbook_.bids.clear();
           orderbook_.asks.clear();
           if (!data.HasMember("bids") || !data["bids"].IsArray() ||
               !data.HasMember("asks") || !data["asks"].IsArray() ||
               !data.HasMember("ts") || !data["ts"].IsString()) {
               std::cerr << "Invalid orderbook format" << std::endl;
               return;
           }
           for (const auto& bid : data["bids"].GetArray()) {
               if (!bid.IsArray() || bid.Size() < 2 || !bid[0].IsString() || !bid[1].IsString()) continue;
               orderbook_.bids.emplace_back(std::stod(bid[0].GetString()), std::stod(bid[1].GetString()));
           }
           for (const auto& ask : data["asks"].GetArray()) {
               if (!ask.IsArray() || ask.Size() < 2 || !ask[0].IsString() || !ask[1].IsString()) continue;
               orderbook_.asks.emplace_back(std::stod(ask[0].GetString()), std::stod(ask[1].GetString()));
           }
           orderbook_.timestamp = std::chrono::milliseconds(std::stol(data["ts"].GetString()));
           // Log orderbook for debugging
           size_t valid_bid_idx = 0;
           size_t valid_ask_idx = 0;
           while (valid_bid_idx < orderbook_.bids.size() && orderbook_.bids[valid_bid_idx].second <= 0.0) {
               ++valid_bid_idx;
           }
           while (valid_ask_idx < orderbook_.asks.size() && orderbook_.asks[valid_ask_idx].second <= 0.0) {
               ++valid_ask_idx;
           }
           if (valid_bid_idx < orderbook_.bids.size() && valid_ask_idx < orderbook_.asks.size()) {
               std::cout << "Orderbook: Bid[" << valid_bid_idx << "]=" << orderbook_.bids[valid_bid_idx].first
                         << ", Qty=" << orderbook_.bids[valid_bid_idx].second
                         << ", Ask[" << valid_ask_idx << "]=" << orderbook_.asks[valid_ask_idx].first
                         << ", Qty=" << orderbook_.asks[valid_ask_idx].second
                         << std::endl;
           }
       }

       // Calculate output parameters (slippage, fees, market impact, etc.)
       void calculate_outputs() {
           std::lock_guard<std::mutex> lock(orderbook_.mutex);
           if (orderbook_.bids.empty() || orderbook_.asks.empty()) {
               return;
           }
           // Find first valid bid and ask
           size_t valid_bid_idx = 0;
           size_t valid_ask_idx = 0;
           while (valid_bid_idx < orderbook_.bids.size() && orderbook_.bids[valid_bid_idx].second <= 0.0) {
               ++valid_bid_idx;
           }
           while (valid_ask_idx < orderbook_.asks.size() && orderbook_.asks[valid_ask_idx].second <= 0.0) {
               ++valid_ask_idx;
           }
           if (valid_bid_idx >= orderbook_.bids.size() || valid_ask_idx >= orderbook_.asks.size()) {
               return;
           }
           // Cache mid-price and spread
           cached_mid_price_ = (orderbook_.bids[valid_bid_idx].first + orderbook_.asks[valid_ask_idx].first) / 2.0;
           cached_spread_ = orderbook_.asks[valid_ask_idx].first - orderbook_.bids[valid_bid_idx].first;
           volatility_ = cached_spread_ / cached_mid_price_;
           // Slippage: Linear regression over top 5 valid ask levels
           double cumulative_qty = 0.0;
           double weighted_price = 0.0;
           size_t max_levels = std::min<size_t>(5, orderbook_.asks.size() - valid_ask_idx);
           for (size_t i = valid_ask_idx; i < valid_ask_idx + max_levels; ++i) {
               if (cumulative_qty >= quantity_ || i >= orderbook_.asks.size()) break;
               if (orderbook_.asks[i].second <= 0.0) continue;
               double qty = std::min(quantity_ - cumulative_qty, orderbook_.asks[i].second);
               weighted_price += qty * orderbook_.asks[i].first;
               cumulative_qty += qty;
           }
           if (cumulative_qty > 0) {
               slippage_ = ((weighted_price / cumulative_qty) - cached_mid_price_) / cached_mid_price_ * 100;
           } else {
               slippage_ = 0.0;
           }
           // Fees: Rule-based, using taker fee for market orders
           fees_ = quantity_ * cached_mid_price_ * taker_fee_;
           // Market Impact: Almgren-Chriss model
           double sigma = volatility_ * cached_mid_price_;
           double eta = 1.0 * sigma;
           market_impact_ = eta * std::pow(quantity_, 1.5) / std::sqrt(86400.0);
           market_impact_ = std::max(market_impact_, 0.0001);
           // Net Cost: Sum of slippage, fees, and market impact
           net_cost_ = (slippage_ / 100 * quantity_ * cached_mid_price_) + fees_ + market_impact_;
           // Maker/Taker Proportion: Smoothed logistic regression
           double liquidity_ratio = 0.5;
           double total_qty = orderbook_.bids[valid_bid_idx].second + orderbook_.asks[valid_ask_idx].second;
           if (total_qty > 0.0) {
               liquidity_ratio = orderbook_.bids[valid_bid_idx].second / total_qty;
           }
           double raw_proportion = 1.0 / (1.0 + std::exp(-5.0 * (liquidity_ratio - 0.5)));
           maker_proportion_ = std::isfinite(maker_proportion_) ? 0.9 * maker_proportion_ + 0.1 * raw_proportion : raw_proportion;
       }

       // UI thread for non-blocking display
       void ui_thread_func() {
           while (running_) {
               UIData data;
               bool has_data = false;
               {
                   std::lock_guard<std::mutex> lock(ui_mutex_);
                   if (!ui_queue_.empty()) {
                       data = ui_queue_.front();
                       ui_queue_.pop();
                       has_data = true;
                   }
               }
               if (has_data) {
                   auto ui_start = std::chrono::high_resolution_clock::now();
                   display_ui(data);
                   std::cout.flush();
                   auto ui_end = std::chrono::high_resolution_clock::now();
                   double ui_ms = std::chrono::duration<double, std::milli>(ui_end - ui_start).count();
                   {
                       std::lock_guard<std::mutex> lock(ui_mutex_);
                       ui_update_latencies_.push_back(ui_ms);
                   }
               }
               // MODIFIED FOR VIDEO: Slower UI updates for readability
               std::this_thread::sleep_for(std::chrono::milliseconds(500));
           }
       }

       // Display UI with input/output parameters
       void display_ui(const UIData& data) {
           std::cout << "\033[2J\033[1;1H";
           std::cout << std::fixed << std::setprecision(4);
           // MODIFIED FOR VIDEO: Enhanced UI with highlights
           std::cout << "===== Trade Simulator: Real-Time Trading Costs =====" << std::endl;
           std::cout << "| Inputs                          | Outputs                       |" << std::endl;
           std::cout << "|---------------------------------|-------------------------------|" << std::endl;
           std::cout << "| Exchange: " << std::setw(16) << std::left << exchange_ 
                     << "| *Slippage: " << std::setw(14) << data.slippage << "%*" << " |" << std::endl;
           std::cout << "| Symbol:   " << std::setw(16) << symbol_ 
                     << "| *Fees:     " << std::setw(14) << data.fees << " USD*" << " |" << std::endl;
           std::cout << "| Order Type: " << std::setw(14) << order_type_ 
                     << "| Market Impact: " << std::setw(10) << data.market_impact << " USD" << " |" << std::endl;
           std::cout << "| Quantity: " << std::setw(16) << quantity_ 
                     << "| Net Cost: " << std::setw(15) << data.net_cost << " USD" << " |" << std::endl;
           std::cout << "| Volatility: " << std::setw(14) << data.volatility 
                     << "| Maker/Taker: " << std::setw(11) << data.maker_proportion << " |" << std::endl;
           std::cout << "| Fee Tier: " << std::setw(16) << fee_tier_ 
                     << "| Latency:  " << std::setw(14) << data.latency_ms << " ms" << " |" << std::endl;
           std::cout << "====================================================" << std::endl;
       }

       // Report benchmarking results
       void report_benchmarks() {
           double avg_json_parse = std::accumulate(json_parse_latencies_.begin(), json_parse_latencies_.end(), 0.0) / json_parse_latencies_.size();
           double avg_e2e = std::accumulate(e2e_latencies_.begin(), e2e_latencies_.end(), 0.0) / e2e_latencies_.size();
           // MODIFIED FOR VIDEO: Simplified benchmark output
           std::cout << "Benchmark (Update #" << update_count_ << "):" << std::endl;
           std::cout << "  *JSON Parse: " << avg_json_parse << " ms*" << std::endl;
           std::cout << "  *End-to-End: " << avg_e2e << " ms*" << std::endl;
           json_parse_latencies_.clear();
           data_proc_latencies_.clear();
           ui_update_latencies_.clear();
           e2e_latencies_.clear();
       }
   };

   int main() {
       TradeSimulator simulator;
       std::string uri = "wss://ws.okx.com:8443/ws/v5/public";
       std::thread ws_thread([&simulator, uri]() { simulator.connect(uri); });
       ws_thread.join();
       return 0;
   }
  