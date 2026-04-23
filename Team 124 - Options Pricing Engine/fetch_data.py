import yfinance as yf

def get_bool(prompt, default):
    """Helper to convert user input to boolean."""
    val = input(f"{prompt} (True/False) [Default: {default}]: ").strip().lower()
    if val == "": return default
    return val == 'true'

print("--- yfinance Data Downloader ---")

# 1. Required Parameters
tickers = input("Enter Ticker(s) (e.g., AAPL MSFT): ")

# 2. Date/Time Range (Mutual Exclusivity)
use_dates = get_bool("Do you want to use specific Start/End dates?", False)
if use_dates:
    start_date = input("Start Date (YYYY-MM-DD): ")
    end_date = input("End Date (YYYY-MM-DD): ")
    period = None
else:
    start_date = None
    end_date = None
    period = input("Enter Period (1d, 5d, 1mo, 1y, max) [Default: 1mo]: ") or "1mo"

# 3. Technical Parameters
interval = input("Enter Interval (1m, 5m, 1h, 1d, 1wk) [Default: 1d]: ") or "1d"
group_by = input("Group by (ticker/column) [Default: column]: ") or "column"

# 4. Boolean Flags
auto_adj = get_bool("Auto Adjust OHLC?", True)
actions = get_bool("Download Dividends/Splits?", False)
prepost = get_bool("Include Pre/Post Market?", False)
repair = get_bool("Attempt currency repair?", False)

print("\nDownloading data...")

# Execution
data = yf.download(
    tickers=tickers,
    period=period,
    interval=interval,
    start=start_date,
    end=end_date,
    group_by=group_by,
    auto_adjust=auto_adj,
    prepost=prepost,
    actions=actions,
    repair=repair,
    threads=True,
    progress=True
)

# Save to CSV
filename = f"{tickers.replace(' ', '_')}_data.csv"
data.to_csv(filename)
print(f"\nSuccess! Data saved to {filename}")