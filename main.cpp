// Zwenbabus - HTTP Yuk Test Araci
// Hizli, eszamanli HTTP kiyaslama araci
// Gereksinimler: C++17, POSIX soketler (Linux/macOS) veya Winsock (Windows)

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <map>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using sock_t = SOCKET;
  #define INVALID_SOCK INVALID_SOCKET
  #define CLOSE_SOCK(s) closesocket(s)
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  using sock_t = int;
  #define INVALID_SOCK -1
  #define CLOSE_SOCK(s) close(s)
#endif


struct Config {
    std::string url;
    std::string host;
    std::string path;
    int port        = 80;
    int requests    = 100;
    int concurrency = 10;
    std::string method = "GET";
    std::string body;
    std::map<std::string, std::string> headers;
};

struct Result {
    long long latency_us = 0;
    int status_code      = 0;
    bool success         = false;
    std::string error;
};

struct Stats {
    int total      = 0;
    int succeeded  = 0;
    int failed     = 0;
    double rps     = 0.0;
    double avg_ms  = 0.0;
    double min_ms  = 0.0;
    double max_ms  = 0.0;
    double p50_ms  = 0.0;
    double p90_ms  = 0.0;
    double p99_ms  = 0.0;
    std::map<int, int> status_codes;
};

std::mutex results_mutex;
std::vector<Result> all_results;
std::atomic<int> completed{0};
std::atomic<bool> running{true};


bool parse_url(const std::string& url, Config& cfg) {
    std::string u = url;
    if (u.substr(0, 7) == "http://")  u = u.substr(7);
    else if (u.substr(0, 8) == "https://") {
        std::cerr << "HTTPS bu surumde desteklenmiyor (HTTP kullanin).\n";
        return false;
    }
    auto slash = u.find('/');
    std::string hostport = (slash == std::string::npos) ? u : u.substr(0, slash);
    cfg.path = (slash == std::string::npos) ? "/" : u.substr(slash);
    auto colon = hostport.find(':');
    if (colon != std::string::npos) {
        cfg.host = hostport.substr(0, colon);
        cfg.port = std::stoi(hostport.substr(colon + 1));
    } else {
        cfg.host = hostport;
        cfg.port = 80;
    }
    return true;
}

std::string resolve_host(const std::string& host) {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0) return "";
    char ip[INET_ADDRSTRLEN];
    auto* addr = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
    inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
    freeaddrinfo(res);
    return std::string(ip);
}


Result do_request(const Config& cfg, const std::string& ip) {
    Result r;
    auto t_start = std::chrono::high_resolution_clock::now();

    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCK) { r.error = "socket()"; return r; }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(cfg.port));
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (connect(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        CLOSE_SOCK(s);
        r.error = "connect()";
        return r;
    }

    std::ostringstream req;
    req << cfg.method << " " << cfg.path << " HTTP/1.1\r\n"
        << "Host: " << cfg.host << "\r\n"
        << "Connection: close\r\n"
        << "User-Agent: Zwenbabus-LoadTester/1.0\r\n";
    for (auto& [k, v] : cfg.headers) req << k << ": " << v << "\r\n";
    if (!cfg.body.empty()) {
        req << "Content-Length: " << cfg.body.size() << "\r\n";
        req << "Content-Type: application/json\r\n";
    }
    req << "\r\n" << cfg.body;

    std::string raw = req.str();
    send(s, raw.c_str(), static_cast<int>(raw.size()), 0);

    char buf[4096];
    std::string response;
    int n;
    while ((n = recv(s, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        response += buf;
        if (response.size() > 8192) break;
    }
    CLOSE_SOCK(s);

    auto t_end = std::chrono::high_resolution_clock::now();
    r.latency_us = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();

    if (response.size() > 9) {
        try { r.status_code = std::stoi(response.substr(9, 3)); } catch (...) {}
    }
    r.success = (r.status_code >= 200 && r.status_code < 400);
    return r;
}


void worker(const Config& cfg, const std::string& ip, int count) {
    for (int i = 0; i < count && running; ++i) {
        Result r = do_request(cfg, ip);
        {
            std::lock_guard<std::mutex> lock(results_mutex);
            all_results.push_back(r);
        }
        ++completed;
    }
}

Stats compute_stats(const std::vector<Result>& results, double elapsed_sec) {
    Stats s;
    s.total = static_cast<int>(results.size());
    std::vector<double> latencies;
    for (auto& r : results) {
        if (r.success) ++s.succeeded;
        else           ++s.failed;
        latencies.push_back(r.latency_us / 1000.0);
        if (r.status_code > 0) s.status_codes[r.status_code]++;
    }
    if (latencies.empty()) return s;
    std::sort(latencies.begin(), latencies.end());
    s.min_ms = latencies.front();
    s.max_ms = latencies.back();
    s.avg_ms = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
    auto pct = [&](double p) {
        size_t idx = static_cast<size_t>(p / 100.0 * latencies.size());
        return latencies[std::min(idx, latencies.size() - 1)];
    };
    s.p50_ms = pct(50);
    s.p90_ms = pct(90);
    s.p99_ms = pct(99);
    s.rps    = s.total / elapsed_sec;
    return s;
}


namespace color {
    const char* reset  = "\033[0m";
    const char* bold   = "\033[1m";
    const char* green  = "\033[32m";
    const char* red    = "\033[31m";
    const char* yellow = "\033[33m";
    const char* cyan   = "\033[36m";
    const char* gray   = "\033[90m";
}

void print_bar(double value, double max_val, int width = 30) {
    int filled = (max_val > 0) ? static_cast<int>((value / max_val) * width) : 0;
    std::cout << color::cyan << "[";
    for (int i = 0; i < width; ++i)
        std::cout << (i < filled ? "#" : " ");
    std::cout << "]" << color::reset;
}

void print_stats(const Stats& s) {
    std::cout << "\n" << color::bold << "  Sonuclar" << color::reset
              << color::gray << "  -----------------------------------------------\n" << color::reset;

    std::cout << "  Toplam istek  " << color::bold << s.total << color::reset << "\n";
    std::cout << "  Basarili      " << color::green << s.succeeded << color::reset << "\n";
    std::cout << "  Basarisiz     " << (s.failed > 0 ? color::red : color::gray)
              << s.failed << color::reset << "\n";
    std::cout << "  Istek/sn      " << color::bold << std::fixed << std::setprecision(2)
              << s.rps << color::reset << "\n\n";

    std::cout << color::bold << "  Gecikme" << color::reset
              << color::gray << "  ------------------------------------------------\n" << color::reset;

    auto row = [&](const char* label, double val) {
        std::cout << "  " << std::left << std::setw(12) << label
                  << std::right << std::setw(8) << std::fixed << std::setprecision(2) << val << " ms  ";
        print_bar(val, s.max_ms);
        std::cout << "\n";
    };

    row("ort",  s.avg_ms);
    row("min",  s.min_ms);
    row("maks", s.max_ms);
    row("p50",  s.p50_ms);
    row("p90",  s.p90_ms);
    row("p99",  s.p99_ms);

    if (!s.status_codes.empty()) {
        std::cout << "\n" << color::bold << "  Durum Kodlari" << color::reset
                  << color::gray << "  -------------------------------------------\n" << color::reset;
        for (auto& [code, cnt] : s.status_codes) {
            const char* c = (code >= 200 && code < 300) ? color::green
                          : (code >= 400)               ? color::red
                          : color::yellow;
            std::cout << "  " << c << code << color::reset
                      << "  " << cnt << " istek\n";
        }
    }
    std::cout << "\n";
}

void progress_thread(int total) {
    while (running) {
        int done = completed.load();
        double pct = (total > 0) ? (done * 100.0 / total) : 0;
        std::cout << "\r  " << color::cyan << std::setw(3) << static_cast<int>(pct) << "%" << color::reset
                  << "  [" << done << "/" << total << "]" << std::flush;
        if (done >= total) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "\r  100%  [" << total << "/" << total << "]\n";
}


void print_usage(const char* prog) {
    std::cout << color::bold << "\n  Zwenbabus HTTP Yuk Test Araci\n\n" << color::reset
              << "  Kullanim:\n"
              << "    " << prog << " [secenekler] <url>\n\n"
              << "  Secenekler:\n"
              << "    -n <sayi>    Toplam istek sayisi     (varsayilan: 100)\n"
              << "    -c <sayi>    Eszamanli is parcacigi  (varsayilan: 10)\n"
              << "    -m <str>     HTTP metodu             (varsayilan: GET)\n"
              << "    -b <str>     Istek govdesi\n"
              << "    -H <str>     Baslik (Anahtar:Deger)\n\n"
              << "  Ornekler:\n"
              << "    " << prog << " -n 500 -c 20 http://localhost:8080/api\n"
              << "    " << prog << " -n 100 -c 5 -m POST -b '{\"x\":1}' http://localhost:3000/veri\n\n";
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    if (argc < 2) { print_usage(argv[0]); return 1; }

    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-n" && i + 1 < argc) cfg.requests    = std::stoi(argv[++i]);
        else if (a == "-c" && i + 1 < argc) cfg.concurrency = std::stoi(argv[++i]);
        else if (a == "-m" && i + 1 < argc) cfg.method      = argv[++i];
        else if (a == "-b" && i + 1 < argc) cfg.body        = argv[++i];
        else if (a == "-H" && i + 1 < argc) {
            std::string h = argv[++i];
            auto colon = h.find(':');
            if (colon != std::string::npos)
                cfg.headers[h.substr(0, colon)] = h.substr(colon + 1);
        } else if (a[0] != '-') cfg.url = a;
    }

    if (cfg.url.empty()) { print_usage(argv[0]); return 1; }
    if (!parse_url(cfg.url, cfg)) return 1;

    std::string ip = resolve_host(cfg.host);
    if (ip.empty()) {
        std::cerr << color::red << "  Hata: sunucu adi cozumlenemedi '" << cfg.host << "'\n" << color::reset;
        return 1;
    }

    std::cout << "\n" << color::bold << "  Hedef      " << color::reset << cfg.url << "\n"
              << color::bold << "  Sunucu     " << color::reset << cfg.host << " (" << ip << ":" << cfg.port << ")\n"
              << color::bold << "  Istekler   " << color::reset << cfg.requests
              << "  Eszamanlilik " << cfg.concurrency << "\n\n";

    all_results.reserve(cfg.requests);

    int per_worker = cfg.requests / cfg.concurrency;
    int remainder  = cfg.requests % cfg.concurrency;

    auto t_start = std::chrono::high_resolution_clock::now();

    std::thread prog(progress_thread, cfg.requests);

    std::vector<std::thread> workers;
    workers.reserve(cfg.concurrency);
    for (int i = 0; i < cfg.concurrency; ++i) {
        int count = per_worker + (i < remainder ? 1 : 0);
        workers.emplace_back(worker, std::cref(cfg), std::cref(ip), count);
    }
    for (auto& t : workers) t.join();

    running = false;
    prog.join();

    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t_end - t_start).count();

    Stats s = compute_stats(all_results, elapsed);
    print_stats(s);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
