# Options Pricing Engine

A quantitative finance tool that prices European options using **Black-Scholes** (closed-form analytical) and **Monte Carlo simulation** (10,000 GBM paths), with a live interactive frontend and a raw C++ HTTP backend — no frameworks, no external libraries.

Demo Link: https://drive.google.com/file/d/1Oosx4bCdJ__r0HaYi7ErR9weqoAmZpzu/view?usp=drive_link

---

## Data Structures & Implementation Detail

### **1. C++ Backend (`main.cpp`)**

* **`std::vector` (Contiguous Memory):** Used as the primary container for high-performance data handling.
    * **Time-Series Storage:** Stores parsed `OHLCVRow` objects for $O(1)$ random access.
    * **Hot-Path Buffers:** Pre-allocating memory for 10,000 Monte Carlo payoff entries and 253-step Geometric Brownian Motion (GBM) paths to avoid heap churn.
    * **Matrix Data:** A 2D vector (`std::vector<std::vector<double>>`) manages sample paths for chart rendering.
* **`struct` (Value Aggregates):** Used for "Plain Old Data" (POD) objects with no private logic.
    * **Examples:** `OHLCVRow` (CSV lines), `BSResult` (Black-Scholes/Greeks), and `MCResult` (Simulation outputs).
* **`std::map` (Key-Value Store):** A red-black tree used for HTTP form-field mapping (e.g., Strike, Maturity). Chosen for reliability and simplicity given the small number of input keys.
* **`std::string` & `std::ostringstream`:** Acts as the primary buffer for raw HTTP requests, multipart boundary parsing, and manual JSON construction without external libraries.
* **`std::mt19937` (PRNG):** The Mersenne Twister engine drives the `std::normal_distribution` to generate high-entropy Gaussian increments for the Monte Carlo engine.

### **2. Python Data Fetcher (`fetch_data.py`)**

* **`pandas.DataFrame`:** The core engine for data acquisition. It handles multi-level indexing for tickers and performs vectorized operations for CSV serialization via `.to_csv()`.

---

### **3. Performance & Memory Strategy**

* **Cache Friendliness:** By using `std::vector` for price data, we ensure sequential memory layout, which is critical for fast log-return calculations.
* **Optimization:** Structs are passed by value, allowing the compiler to utilize **Named Return Value Optimization (NRVO)** to elide unnecessary copies.
* **Allocation Control:** Large numerical arrays are sized once during initialization to prevent "amortized doubling" performance hits during the simulation.
---

## What it does

- Computes **Call and Put prices** via both Black-Scholes and Monte Carlo
- Calculates all five **Greeks** — Delta (call & put), Gamma, Vega, Theta
- Estimates **historical volatility** (σ) from uploaded Yahoo Finance price data
- Simulates **Geometric Brownian Motion** paths and visualises them
- Shows **Monte Carlo convergence** vs the Black-Scholes analytical benchmark
- Plots the full **historical close price** chart from your CSV

---

## Algorithms & theory

### Black-Scholes (closed-form)

Prices a European option analytically using the Black-Scholes formula:

```
C = S·N(d₁) − K·e^(−rT)·N(d₂)
P = K·e^(−rT)·N(−d₂) − S·N(−d₁)

d₁ = [ln(S/K) + (r + σ²/2)·T] / (σ√T)
d₂ = d₁ − σ√T
```

Where `S` = spot price, `K` = strike, `r` = risk-free rate, `T` = time to maturity, `σ` = volatility. `N(·)` is the cumulative standard normal distribution, approximated via the Horner-form Abramowitz & Stegun polynomial.

### Monte Carlo (GBM simulation)

Simulates 10,000 independent stock price paths under the risk-neutral measure using Geometric Brownian Motion:

```
S(t+dt) = S(t) · exp[(r − σ²/2)·dt + σ·√dt·Z]     Z ~ N(0,1)
```

Each path runs 252 steps (one per trading day). The option price is the discounted average payoff across all paths. Standard error is reported for each estimate.

### Historical volatility

Annualised from daily log-returns of the uploaded close price series:

```
σ = std(ln(Sᵢ/Sᵢ₋₁)) · √252
```

### Greeks (Black-Scholes)

| Greek | Formula |
|-------|---------|
| Δ Call | N(d₁) |
| Δ Put | N(d₁) − 1 |
| Γ | φ(d₁) / (S·σ·√T) |
| V (Vega) | S·φ(d₁)·√T / 100 |
| Θ Call | [−S·φ(d₁)·σ/(2√T) − r·K·e^(−rT)·N(d₂)] / 365 |
| Θ Put | [−S·φ(d₁)·σ/(2√T) + r·K·e^(−rT)·N(−d₂)] / 365 |

Where φ(·) is the standard normal PDF.

---

## Project structure

```
QuantOfWallStreet/
├── main.cpp        # C++ backend — HTTP server, BS pricer, Monte Carlo, GBM
├── index.html      # Frontend — form inputs, Chart.js visualisations
├── fetch.py        # Python helper — interactive CLI to download data via yfinance
└── README.md
```

---

## Prerequisites

- **g++** via MSYS2 (Windows) or system GCC (Mac/Linux)
- A modern browser (Chrome, Edge, Firefox)
- **Python 3.8+** with `yfinance` installed (for the data fetcher)

### Install g++ on Windows (MSYS2)

1. Download and install [MSYS2](https://www.msys2.org)
2. Open the **MSYS2 UCRT64** terminal and run:
   ```
   pacman -S mingw-w64-ucrt-x86_64-gcc
   ```
3. Add `C:\msys64\ucrt64\bin` to your Windows **PATH** environment variable
4. Verify: open a new terminal and run `g++ --version`

### Install Python dependencies

```bash
pip install yfinance
```

---

## Setup & running

### 1. Compile the backend

**Windows:**
```bash
g++ -O2 -o server.exe main.cpp -lws2_32
```

**Mac / Linux:**
```bash
g++ -O2 -o server main.cpp -lm
```

### 2. Start the server

**Windows:**
```bash
.\server.exe
```

**Mac / Linux:**
```bash
./server
```

The server listens on `http://localhost:8081`.

### 3. Open the frontend

Do **not** navigate to `localhost:8081` in your browser — that's only the API endpoint. Open `index.html` as a file instead.

**Option A — simplest**, open directly:
```
Double-click index.html in File Explorer
```
The address bar should show `file:///C:/...` not `localhost`.

**Option B — recommended** (avoids potential CORS issues):
```bash
npx serve .
```
Then open the URL it prints (e.g. `http://localhost:3000`).

### 4. Fetch price data

Run the interactive data fetcher to download OHLCV data directly from Yahoo Finance:

```bash
python fetch.py
```

It will prompt you for:

| Prompt | Example |
|--------|---------|
| Ticker(s) | `AAPL` or `AAPL MSFT` for multiple |
| Use specific dates? | `False` (use a period instead) |
| Period | `1y`, `2y`, `max`, etc. |
| Interval | `1d` (recommended for BS/MC) |
| Auto adjust OHLC | `True` |
| Dividends/splits | `False` |

A CSV file named `AAPL_data.csv` (or similar) is saved in the current folder. Upload that file in the frontend.

> **Tip:** Use at least 6 months of daily data (`1d` interval) for a meaningful volatility estimate. Intraday intervals (1m, 5m) are supported but will produce noisier σ values.

### 5. Run a pricing simulation

1. Upload the generated CSV in the frontend
2. Set your parameters (strike, maturity, risk-free rate)
3. Click **▶ Run Pricing Engine**

---

## Input parameters

### fetch.py options

| Parameter | Description | Default |
|-----------|-------------|---------|
| Ticker(s) | One or more Yahoo Finance symbols, space-separated | — |
| Use dates | Toggle between date range and period mode | `False` |
| Start / End date | `YYYY-MM-DD` format, only if use dates = `True` | — |
| Period | `1d` `5d` `1mo` `1y` `2y` `5y` `max` | `1mo` |
| Interval | `1m` `5m` `15m` `1h` `1d` `1wk` `1mo` | `1d` |
| Group by | `ticker` or `column` | `column` |
| Auto adjust | Adjust OHLC for splits/dividends | `True` |
| Actions | Include dividend and split events | `False` |
| Pre/post market | Include extended hours data | `False` |
| Repair | Attempt currency unit correction | `False` |

### Pricing engine options

| Parameter | Description | Default |
|-----------|-------------|---------|
| CSV file | Output from `fetch.py` or any Yahoo Finance OHLCV CSV | — |
| Strike (K) | Option strike price | ATM (current spot) |
| Time to maturity (T) | In years — 0.5 = 6 months | 1.0 |
| Risk-free rate (r) | As decimal — 0.05 = 5% | 0.05 |

---

## Output

| Output | Source |
|--------|--------|
| BS Call / Put price | Black-Scholes closed-form |
| MC Call / Put price | Monte Carlo average payoff |
| Standard error | MC sampling uncertainty |
| BS vs MC difference | Convergence quality check |
| Delta, Gamma, Vega, Theta | Black-Scholes Greeks |
| Historical price chart | Uploaded CSV |
| GBM simulated paths | 8 sample paths from Monte Carlo |
| Convergence chart | MC call price vs # simulations |

---

## Technical notes

- The C++ backend is a raw TCP socket server — no web framework used
- Multipart form-data is parsed manually (boundary detection, part extraction)
- JSON is built with string streams — no external JSON library
- The Mersenne Twister (`std::mt19937`) is seeded at 42 for reproducible Monte Carlo results
- Normal CDF uses the Abramowitz & Stegun rational approximation (max error < 1.5×10⁻⁷)
- Historical paths are thinned to ≤200 points for chart performance
- GBM paths are thinned to 60 steps each for the visualisation
- `fetch.py` uses `yfinance`'s `threads=True` for parallel multi-ticker downloads; for single tickers this has no effect
- When multiple tickers are passed to `fetch.py`, the output CSV uses a multi-level column header — upload single-ticker CSVs to the pricing engine

---
