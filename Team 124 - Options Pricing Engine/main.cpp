/*
  Options Pricing Engine — C++ Backend
  Compile (Mac/Linux): g++ -O2 -o server main.cpp -lm
  Compile (Windows):   g++ -O2 -o server.exe main.cpp -lws2_32
  Run: ./server        (listens on http://localhost:8081)
*/
#define _USE_MATH_DEFINES
#include <cmath>
#include <iostream>
#include <cmath>
#include <vector>
#include <random>
#include <numeric>
#include <sstream>
#include <string>
#include <map>
#include <algorithm>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib,"ws2_32.lib")
  typedef int socklen_t;
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <unistd.h>
  #define SOCKET int
  #define INVALID_SOCKET -1
  #define closesocket close
#endif

// ══════════════════════════════════════════
//  MATH HELPERS
// ══════════════════════════════════════════

double normCDF(double x) {
    double a1=0.254829592, a2=-0.284496736, a3=1.421413741;
    double a4=-1.453152027, a5=1.061405429, p=0.3275911;
    int sign = (x < 0) ? -1 : 1;
    x = std::abs(x) / std::sqrt(2.0);
    double t = 1.0 / (1.0 + p * x);
    double y = 1.0 - (((((a5*t + a4)*t) + a3)*t + a2)*t + a1)*t*std::exp(-x*x);
    return 0.5 * (1.0 + sign * y);
}

// ══════════════════════════════════════════
//  CSV PARSER  (Yahoo Finance format)
//  Columns: Date, Close, High, Low, Open, Volume
// ══════════════════════════════════════════

struct OHLCVRow {
    std::string date;
    double close, high, low, open;
    long long volume;
};

std::vector<OHLCVRow> parseCSV(const std::string& body) {
    std::vector<OHLCVRow> rows;
    std::istringstream ss(body);
    std::string line;
    bool header = true;

    // Detect column order from header
    int idxDate=-1, idxClose=-1, idxHigh=-1, idxLow=-1, idxOpen=-1, idxVol=-1;

    while (std::getline(ss, line)) {
        // strip \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        // Tokenize by comma
        std::vector<std::string> cols;
        std::istringstream ls(line);
        std::string tok;
        while (std::getline(ls, tok, ',')) cols.push_back(tok);

        if (header) {
            // Map header names to indices (case-insensitive)
            for (int i = 0; i < (int)cols.size(); i++) {
                std::string h = cols[i];
                std::transform(h.begin(), h.end(), h.begin(), ::tolower);
                if (h == "date")        idxDate  = i;
                else if (h == "close" || h == "price") idxClose = i;
                else if (h == "high")   idxHigh  = i;
                else if (h == "low")    idxLow   = i;
                else if (h == "open")   idxOpen  = i;
                else if (h == "volume") idxVol   = i;
            }
            header = false;
            continue;
        }

        if (cols.size() < 2) continue;
        OHLCVRow row;
        try {
            row.date   = (idxDate  >= 0 && idxDate  < (int)cols.size()) ? cols[idxDate]  : "";
            row.close  = (idxClose >= 0 && idxClose < (int)cols.size()) ? std::stod(cols[idxClose]) : 0.0;
            row.high   = (idxHigh  >= 0 && idxHigh  < (int)cols.size()) ? std::stod(cols[idxHigh])  : row.close;
            row.low    = (idxLow   >= 0 && idxLow   < (int)cols.size()) ? std::stod(cols[idxLow])   : row.close;
            row.open   = (idxOpen  >= 0 && idxOpen  < (int)cols.size()) ? std::stod(cols[idxOpen])  : row.close;
            row.volume = (idxVol   >= 0 && idxVol   < (int)cols.size()) ? std::stoll(cols[idxVol])  : 0;
            if (row.close > 0) rows.push_back(row);
        } catch (...) { continue; }
    }
    return rows;
}

// Compute annualised historical volatility from close prices
double historicalVolatility(const std::vector<OHLCVRow>& rows) {
    if (rows.size() < 2) return 0.2;
    std::vector<double> logReturns;
    for (size_t i = 1; i < rows.size(); i++) {
        if (rows[i-1].close > 0 && rows[i].close > 0)
            logReturns.push_back(std::log(rows[i].close / rows[i-1].close));
    }
    if (logReturns.empty()) return 0.2;
    double mean = std::accumulate(logReturns.begin(), logReturns.end(), 0.0) / logReturns.size();
    double var  = 0.0;
    for (double r : logReturns) var += (r - mean) * (r - mean);
    var /= (logReturns.size() - 1);
    return std::sqrt(var) * std::sqrt(252.0); // annualise
}

// ══════════════════════════════════════════
//  BLACK-SCHOLES
// ══════════════════════════════════════════

struct BSResult { double call, put, d1, d2, deltaCall, deltaPut, gamma, vega, thetaCall, thetaPut; };

BSResult blackScholes(double S, double K, double T, double r, double sigma) {
    double d1 = (std::log(S/K) + (r + 0.5*sigma*sigma)*T) / (sigma*std::sqrt(T));
    double d2 = d1 - sigma*std::sqrt(T);
    double Nd1 = normCDF(d1), Nd2 = normCDF(d2);
    double nd1 = std::exp(-0.5*d1*d1) / std::sqrt(2*M_PI); // standard normal PDF

    double call = S*Nd1 - K*std::exp(-r*T)*Nd2;
    double put  = K*std::exp(-r*T)*normCDF(-d2) - S*normCDF(-d1);

    // Greeks
    double deltaCall = Nd1;
    double deltaPut  = Nd1 - 1.0;
    double gamma     = nd1 / (S * sigma * std::sqrt(T));
    double vega      = S * nd1 * std::sqrt(T) / 100.0; // per 1% vol move
    double thetaCall = (-S*nd1*sigma/(2*std::sqrt(T)) - r*K*std::exp(-r*T)*Nd2) / 365.0;
    double thetaPut  = (-S*nd1*sigma/(2*std::sqrt(T)) + r*K*std::exp(-r*T)*normCDF(-d2)) / 365.0;

    return {call, put, d1, d2, deltaCall, deltaPut, gamma, vega, thetaCall, thetaPut};
}

// ══════════════════════════════════════════
//  GBM PATH GENERATOR
// ══════════════════════════════════════════

std::vector<double> gbmPath(double S0, double r, double sigma, double T,
                             int steps, std::mt19937& rng) {
    std::normal_distribution<double> norm(0.0, 1.0);
    double dt = T / steps;
    std::vector<double> path(steps + 1);
    path[0] = S0;
    for (int i = 1; i <= steps; i++)
        path[i] = path[i-1] * std::exp((r - 0.5*sigma*sigma)*dt + sigma*std::sqrt(dt)*norm(rng));
    return path;
}

// ══════════════════════════════════════════
//  MONTE CARLO PRICER
// ══════════════════════════════════════════

struct MCResult {
    double call, put, stdErrCall, stdErrPut;
    std::vector<std::vector<double>> samplePaths;
    std::vector<double> convergenceCall; // price estimate every 500 paths
};

MCResult monteCarlo(double S, double K, double T, double r, double sigma,
                    int numPaths = 10000, int steps = 252) {
    std::mt19937 rng(42);
    std::vector<double> callPayoffs(numPaths), putPayoffs(numPaths);
    std::vector<std::vector<double>> samplePaths;
    std::vector<double> convergenceCall;

    for (int i = 0; i < numPaths; i++) {
        auto path = gbmPath(S, r, sigma, T, steps, rng);
        double ST = path.back();
        callPayoffs[i] = std::max(ST - K, 0.0);
        putPayoffs[i]  = std::max(K - ST, 0.0);

        // Save 8 sample paths for chart
        if (i < 8) samplePaths.push_back(path);

        // Convergence: record average every 500 sims
        if ((i+1) % 500 == 0) {
            double sum = std::accumulate(callPayoffs.begin(), callPayoffs.begin()+i+1, 0.0);
            convergenceCall.push_back(std::exp(-r*T) * sum / (i+1));
        }
    }

    double discount = std::exp(-r*T);
    double callMean = std::accumulate(callPayoffs.begin(), callPayoffs.end(), 0.0) / numPaths;
    double putMean  = std::accumulate(putPayoffs.begin(),  putPayoffs.end(),  0.0) / numPaths;

    // Std error
    double callVar = 0, putVar = 0;
    for (int i = 0; i < numPaths; i++) {
        callVar += (callPayoffs[i]-callMean)*(callPayoffs[i]-callMean);
        putVar  += (putPayoffs[i]-putMean)*(putPayoffs[i]-putMean);
    }
    callVar /= (numPaths-1); putVar /= (numPaths-1);

    return {
        discount * callMean,
        discount * putMean,
        discount * std::sqrt(callVar/numPaths),
        discount * std::sqrt(putVar/numPaths),
        samplePaths,
        convergenceCall
    };
}

// ══════════════════════════════════════════
//  JSON BUILDER (no external libs)
// ══════════════════════════════════════════

std::string dbl(double v, int prec = 4) {
    if (std::isnan(v) || std::isinf(v)) return "0";
    std::ostringstream ss;
    ss.precision(prec);
    ss << std::fixed << v;
    return ss.str();
}

std::string jsonArray(const std::vector<double>& v, int thin = 1) {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < v.size(); i += thin) {
        if (i > 0) ss << ",";
        ss << dbl(v[i], 2);
    }
    ss << "]";
    return ss.str();
}

std::string buildResponse(const std::vector<OHLCVRow>& rows,
                           double K, double T, double r) {
    if (rows.empty()) return R"({"error":"No valid data rows"})";

    double S     = rows.back().close;
    double sigma = historicalVolatility(rows);

    auto bs = blackScholes(S, K, T, r, sigma);
    auto mc = monteCarlo(S, K, T, r, sigma, 10000, 252);

    // Historical close prices (thin to max 200 points for chart)
    int thin = std::max(1, (int)rows.size() / 200);
    std::ostringstream hist;
    hist << "[";
    for (size_t i = 0; i < rows.size(); i += thin) {
        if (i > 0) hist << ",";
        hist << "{\"d\":\"" << rows[i].date << "\",\"c\":" << dbl(rows[i].close,2) << "}";
    }
    hist << "]";

    // GBM sample paths (thin steps to 60 points each)
    int pathThin = std::max(1, (int)mc.samplePaths[0].size() / 60);
    std::ostringstream paths;
    paths << "[";
    for (size_t p = 0; p < mc.samplePaths.size(); p++) {
        if (p > 0) paths << ",";
        paths << jsonArray(mc.samplePaths[p], pathThin);
    }
    paths << "]";

    std::ostringstream j;
    j << "{"
      << "\"spot\":"        << dbl(S) << ","
      << "\"sigma\":"       << dbl(sigma) << ","
      << "\"K\":"           << dbl(K) << ","
      << "\"T\":"           << dbl(T) << ","
      << "\"r\":"           << dbl(r) << ","
      << "\"bs\":{"
        << "\"call\":"      << dbl(bs.call) << ","
        << "\"put\":"       << dbl(bs.put)  << ","
        << "\"d1\":"        << dbl(bs.d1)   << ","
        << "\"d2\":"        << dbl(bs.d2)   << ","
        << "\"deltaCall\":" << dbl(bs.deltaCall) << ","
        << "\"deltaPut\":"  << dbl(bs.deltaPut)  << ","
        << "\"gamma\":"     << dbl(bs.gamma,5)   << ","
        << "\"vega\":"      << dbl(bs.vega)      << ","
        << "\"thetaCall\":" << dbl(bs.thetaCall) << ","
        << "\"thetaPut\":"  << dbl(bs.thetaPut)
      << "},"
      << "\"mc\":{"
        << "\"call\":"        << dbl(mc.call) << ","
        << "\"put\":"         << dbl(mc.put)  << ","
        << "\"stdErrCall\":"  << dbl(mc.stdErrCall,5) << ","
        << "\"stdErrPut\":"   << dbl(mc.stdErrPut,5)  << ","
        << "\"convergence\":" << jsonArray(mc.convergenceCall,1)
      << "},"
      << "\"historical\":"   << hist.str() << ","
      << "\"gbmPaths\":"     << paths.str()
      << "}";
    return j.str();
}

// ══════════════════════════════════════════
//  URL DECODER
// ══════════════════════════════════════════

std::string urlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ) {
        if (s[i] == '%' && i+2 < s.size()) {
            int hex = std::stoi(s.substr(i+1,2), nullptr, 16);
            out += (char)hex; i += 3;
        } else if (s[i] == '+') { out += ' '; i++; }
        else { out += s[i++]; }
    }
    return out;
}

// Parse multipart/form-data — extract CSV file content and form fields
struct FormData {
    std::string csvContent;
    std::map<std::string,std::string> fields;
};

FormData parseMultipart(const std::string& body, const std::string& boundary) {
    FormData fd;
    std::string delim = "--" + boundary;
    size_t pos = 0;
    while ((pos = body.find(delim, pos)) != std::string::npos) {
        pos += delim.size();
        if (pos >= body.size()) break;
        if (body.substr(pos,2) == "--") break; // end boundary
        // skip CRLF after boundary
        if (body[pos]=='\r') pos++;
        if (body[pos]=='\n') pos++;

        // Read headers of this part
        size_t headerEnd = body.find("\r\n\r\n", pos);
        if (headerEnd == std::string::npos) break;
        std::string partHeaders = body.substr(pos, headerEnd - pos);
        pos = headerEnd + 4;

        // Find next boundary
        size_t nextBound = body.find("\r\n" + delim, pos);
        std::string content = (nextBound != std::string::npos) ? body.substr(pos, nextBound - pos) : body.substr(pos);

        // Extract name and filename
        std::string name, filename;
        auto namePos = partHeaders.find("name=\"");
        if (namePos != std::string::npos) {
            namePos += 6;
            name = partHeaders.substr(namePos, partHeaders.find('"', namePos) - namePos);
        }
        auto fnPos = partHeaders.find("filename=\"");
        if (fnPos != std::string::npos) {
            fnPos += 10;
            filename = partHeaders.substr(fnPos, partHeaders.find('"', fnPos) - fnPos);
        }

        if (!filename.empty()) fd.csvContent = content;
        else fd.fields[name] = content;
    }
    return fd;
}

// ══════════════════════════════════════════
//  HTTP SERVER
// ══════════════════════════════════════════

std::string makeResponse(const std::string& body, const std::string& contentType = "application/json", int status = 200) {
    std::string statusText = (status == 200) ? "OK" : (status == 400) ? "Bad Request" : "Internal Server Error";
    std::ostringstream r;
    r << "HTTP/1.1 " << status << " " << statusText << "\r\n"
      << "Content-Type: " << contentType << "\r\n"
      << "Access-Control-Allow-Origin: *\r\n"
      << "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
      << "Access-Control-Allow-Headers: Content-Type\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n\r\n"
      << body;
    return r.str();
}

void handleClient(SOCKET client) {
    // Read request
    std::string req;
    char buf[4096];
    int received;
    size_t contentLength = 0;
    bool headersComplete = false;
    std::string headers;

    while (true) {
        received = recv(client, buf, sizeof(buf), 0);
        if (received <= 0) break;
        req.append(buf, received);

        if (!headersComplete) {
            size_t hEnd = req.find("\r\n\r\n");
            if (hEnd != std::string::npos) {
                headers = req.substr(0, hEnd);
                headersComplete = true;
                // Parse Content-Length
                auto clPos = headers.find("Content-Length:");
                if (clPos == std::string::npos) clPos = headers.find("content-length:");
                if (clPos != std::string::npos) {
                    contentLength = std::stoul(headers.substr(clPos+15));
                }
                size_t bodyStart = hEnd + 4;
                size_t bodyReceived = req.size() - bodyStart;
                if (bodyReceived >= contentLength) break;
            }
        } else {
            size_t hEnd = req.find("\r\n\r\n");
            size_t bodyStart = hEnd + 4;
            if (req.size() - bodyStart >= contentLength) break;
        }
    }

    // Handle preflight
    if (req.find("OPTIONS") == 0) {
        std::string resp = makeResponse("", "text/plain");
        send(client, resp.c_str(), resp.size(), 0);
        closesocket(client);
        return;
    }

    // Extract body
    size_t hEnd = req.find("\r\n\r\n");
    std::string body = (hEnd != std::string::npos) ? req.substr(hEnd + 4) : "";

    // Extract boundary from Content-Type header
//     std::string boundary;
//     auto ctPos = headers.find("Content-Type:");
//     if (ctPos == std::string::npos) ctPos = headers.find("content-type:");
//     if (ctPos != std::string::npos) {
//         std::string ct = headers.substr(ctPos);
//         auto bPos = ct.find("boundary=");
// if (bPos != std::string::npos) {
//     boundary = ct.substr(bPos + 9); 
//     // Strip at first \r, \n, or end of string
//     for (auto& ch : {'\r', '\n', ' '}) {
//         auto trim = boundary.find(ch);
//         if (trim != std::string::npos) boundary = boundary.substr(0, trim);
//     }
// }
//     }
    // DEBUG — print exactly what headers we received
    std::cerr << "=== RAW HEADERS ===\n" << headers << "\n=== END HEADERS ===\n";

    // Extract boundary from Content-Type header
    std::string boundary;
    auto ctPos = headers.find("Content-Type:");
    if (ctPos == std::string::npos) ctPos = headers.find("content-type:");
    
    std::cerr << "ctPos = " << ctPos << "\n";
    
    if (ctPos != std::string::npos) {
        size_t lineEnd = headers.find("\r\n", ctPos);
        std::string ctLine = (lineEnd != std::string::npos)
            ? headers.substr(ctPos, lineEnd - ctPos)
            : headers.substr(ctPos);
        
        std::cerr << "ctLine = [" << ctLine << "]\n";
        
        auto bPos = ctLine.find("boundary=");
        std::cerr << "bPos = " << bPos << "\n";
        
        if (bPos != std::string::npos) {
            boundary = ctLine.substr(bPos + 9);
            std::cerr << "boundary raw = [" << boundary << "]\n";
            while (!boundary.empty() && (boundary.back() == ' ' || boundary.back() == '\r' || boundary.back() == '\n' || boundary.back() == '"'))
                boundary.pop_back();
            while (!boundary.empty() && (boundary.front() == ' ' || boundary.front() == '"'))
                boundary = boundary.substr(1);
        }
    }
    
    std::cerr << "final boundary = [" << boundary << "]\n";

    if (boundary.empty()) {
        std::string resp = makeResponse(R"({"error":"Missing multipart boundary"})", "application/json", 400);
        send(client, resp.c_str(), resp.size(), 0);
        closesocket(client);
        return;
    }

    auto fd = parseMultipart(body, boundary);

    if (fd.csvContent.empty()) {
        std::string resp = makeResponse(R"({"error":"No CSV file received"})", "application/json", 400);
        send(client, resp.c_str(), resp.size(), 0);
        closesocket(client);
        return;
    }

    // Parse parameters
    double K = fd.fields.count("strike")   ? std::stod(fd.fields["strike"])   : 0.0;
    double T = fd.fields.count("maturity") ? std::stod(fd.fields["maturity"]) : 1.0;
    double r = fd.fields.count("rate")     ? std::stod(fd.fields["rate"])     : 0.05;

    auto rows = parseCSV(fd.csvContent);
    if (rows.empty()) {
        std::string resp = makeResponse(R"({"error":"Could not parse CSV — check column headers"})", "application/json", 400);
        send(client, resp.c_str(), resp.size(), 0);
        closesocket(client);
        return;
    }

    if (K <= 0) K = rows.back().close; // default strike = current price (ATM)

    std::string jsonResp = buildResponse(rows, K, T, r);
    std::string resp = makeResponse(jsonResp);
    send(client, resp.c_str(), resp.size(), 0);
    closesocket(client);
}

int main() {
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
#endif
    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) { std::cerr << "Socket failed\n"; return 1; }

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(8081);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Bind failed — is port 8081 already in use?\n"; return 1;
    }
    listen(server, 10);
    std::cout << "Options Pricing Engine running on http://localhost:8081\n";
    std::cout << "Open index.html in your browser to use the frontend.\n";

    while (true) {
        SOCKET client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        handleClient(client);
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}