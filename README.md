# Trade Simulator – Real-Time Trading Cost Estimation (C++)

A high-performance **C++ Trade Simulator** for real-time trading cost estimation using **OKX WebSocket BTC-USDT order book data**.  
The system models **market impact, slippage, and maker/taker proportions** using well-known quantitative finance models and is optimized for **low-latency trading environments**.

This project demonstrates strong expertise in **C++ performance engineering, real-time data processing, quantitative modeling, and trading systems design**.

---

## Overview

The Trade Simulator estimates the true execution cost of a trade before placing it in the market.  
It integrates:

- Live order book data via WebSocket
- Market impact modeling (Almgren–Chriss)
- Slippage estimation using regression
- Maker/Taker probability prediction
- Performance benchmarking and latency optimization

---

## Key Features

- **Almgren–Chriss Market Impact Model**
-  **Slippage Estimation via Linear Regression**
- **Maker / Taker Proportion Prediction**
-  **Real-time WebSocket Order Book (OKX)**
-  **Low-latency JSON parsing (RapidJSON)**
-  **Optimized C++ computation paths**
-  **WSL/Linux compatible**

---

## Models Implemented

### 1. Almgren–Chriss Market Impact Model

Estimates the market impact cost of executing a trade.

**Formula:**
### Market Impact = η × Q^1.5 / √T
---

Where:
- `Q` = trade quantity (e.g., 0.0021 BTC)
- `T` = trading period (86,400 seconds)
- `η = σ × mid_price`
- `σ = spread / mid_price`

**Key Implementation Details**
- Uses `std::pow` and `std::sqrt`
- Enforces a minimum threshold (0.0001 USD)
- Volatility derived from live order book spread

---

### 2. Slippage Estimation (Regression-Based)

Estimates slippage using weighted prices from the **top 5 ask levels**.

**Formula:**
### Slippage (%) = (Weighted Price − Mid Price) / Mid Price
---

**Highlights**
- O(1) complexity (fixed depth)
- Early exit when quantity is filled
- Safe fallback when liquidity is insufficient

---

### 3. Maker / Taker Proportion Prediction

- Predicts execution style probability
- Uses smoothed logistic-style estimation
- Helps estimate fees and fill behavior

---

## Performance Optimizations

- 🚀 **RapidJSON** used instead of `nlohmann::json`
  - ~70% faster JSON parsing
- 📉 Reduced heap allocations
- ⏱ Latency profiling included
- Efficient order book traversal

---

## Technology Stack

- **Language**: C++17
- **Data Source**: OKX WebSocket API
- **Parsing**: RapidJSON
- **Build System**: CMake
- **OS**: Linux / WSL
- **Math**: STL (`cmath`, `algorithm`)

---

## Prerequisites

- Linux or WSL (Ubuntu recommended)
- C++17 compatible compiler
- CMake ≥ 3.15
- Internet access (WebSocket data)
- OKX public market access

---

## Build Instructions

```bash
git clone https://github.com/yourusername/trade-simulator.git
cd trade-simulator
mkdir build && cd build
cmake ..
make -j
```
## Run
```bash
./TradeSimulator

```
---
## Project Structure
```bash
trade-simulator/
├── src/
│   ├── websocket/
│   ├── orderbook/
│   ├── models/
│   │   ├── almgren_chriss.cpp
│   │   ├── slippage.cpp
│   │   └── maker_taker.cpp
│   └── main.cpp
├── include/
├── CMakeLists.txt
└── README.md
```
---
## License

### MIT License

### Author Shyam Sundar Biswas 2025

### Built for trading systems, quantitative finance, and low-latency C++ roles.






