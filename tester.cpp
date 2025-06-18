// Each client sends as many messages as it can during the test duration (TEST_DURATION_SEC, default 15 seconds).
// There is no fixed number of messages per client; instead, each client runs a loop sending messages
// (with a random interval between each message) until the test duration elapses and stop_test is set to true.
// The number of messages per client is therefore approximately:
//   (test duration in ms) / (average interval in ms)
// For TCP: interval is random between 20 and 150 ms (avg ≈ 85 ms)
// For UDP: interval is random between 10 and 100 ms (avg ≈ 55 ms)
// So, for TCP: ~15,000 ms / 85 ms ≈ 176 messages per client (on average)
//     for UDP: ~15,000 ms / 55 ms ≈ 273 messages per client (on average)
// The actual number will vary due to randomization and system scheduling.

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <condition_variable>
#include <random>
#include <fstream>
#include <iomanip>
#include <sstream>

const int TCP_PORT = 8080;
const int UDP_PORT = 8081;
const int BUFFER_SIZE = 1024;
const char* SERVER_IP = "127.0.0.1";

// Scalability test configuration
const int MIN_CLIENTS = 10;
const int MAX_CLIENTS = 5000;  // Reduce max to avoid resource exhaustion
const int TEST_DURATION_SEC = 15;  // Duration for each client count test
const int RAMP_UP_DURATION_SEC = 5;  // Gradual ramp-up per test

struct ScalabilityResult {
    int client_count;
    std::string timestamp;
    double throughput_mbps;
    std::vector<double> percentiles;  // P1 to P100
    double connections_per_second;
    double peak_concurrent_connections;
    double success_rate;
    int total_requests;
    int successful_requests;
};

class ScalabilityTester {
private:
    std::atomic<int> connections{0};
    std::atomic<int> active_connections{0};
    std::atomic<int> peak_connections{0};
    std::atomic<long long> total_bytes{0};
    std::atomic<bool> stop_test{false};
    std::mutex results_mutex;
    std::vector<double> latencies;
    std::mt19937 rng{std::random_device{}()};
    std::ofstream log_file;
    std::string log_filename;  // Store the filename for later reference

public:
    ScalabilityTester() {
        // Generate timestamped filename
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::stringstream filename_ss;
        filename_ss << "log-" << std::put_time(std::localtime(&time_t), "%Y-%m-%d-%H-%M-%S") << ".txt";
        log_filename = filename_ss.str();
        
        log_file.open(log_filename, std::ios::app);
        if (!log_file.is_open()) {
            std::cerr << "Failed to open log file: " << log_filename << std::endl;
        } else {
            std::cout << "Logging results to: " << log_filename << std::endl;
        }
    }
    
    ~ScalabilityTester() {
        if (log_file.is_open()) {
            log_file.close();
        }
    }

    void run_scalability_tests() {
        std::cout << "Starting scalability tests from " << MIN_CLIENTS << " to " << MAX_CLIENTS << " clients..." << std::endl;
        
        // Write log header
        write_log_header();
        
        // Test both TCP and UDP
        run_tcp_scalability();
        run_udp_scalability();
        
        std::cout << "Scalability tests completed. Results logged to " << log_filename << std::endl;
    }

private:
    void write_log_header() {
        if (!log_file.is_open()) return;
        
        log_file << "\n=== SCALABILITY TEST STARTED ===\n";
        log_file << "Timestamp: " << get_timestamp() << "\n";
        log_file << "Format: Protocol,ClientCount,Timestamp,ThroughputMBps,ConnectionsPerSec,PeakConcurrent,SuccessRate,TotalReqs,SuccessfulReqs,P1,P2,...,P100\n\n";
        log_file.flush();
    }
    
    void run_tcp_scalability() {
        std::cout << "\n=== TCP Scalability Test ===" << std::endl;
        
        // Generate client count sequence: 10, 20, 50, 100, 200, 500, 1000, 2000, 5000
        std::vector<int> client_counts = {10, 20, 50, 100, 200, 500, 1000, 2000, 5000};
        
        for (int client_count : client_counts) {
            std::cout << "Testing TCP with " << client_count << " clients..." << std::endl;
            
            auto result = test_with_client_count("TCP", client_count);
            log_result(result);
            
            // Brief pause between tests
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
    
    void run_udp_scalability() {
        std::cout << "\n=== UDP Scalability Test ===" << std::endl;
        
        // Same client count sequence for UDP
        std::vector<int> client_counts = {10, 20, 50, 100, 200, 500, 1000, 2000, 5000};
        
        for (int client_count : client_counts) {
            std::cout << "Testing UDP with " << client_count << " clients..." << std::endl;
            
            auto result = test_with_client_count("UDP", client_count);
            log_result(result);
            
            // Brief pause between tests
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
    
    ScalabilityResult test_with_client_count(const std::string& protocol, int client_count) {
        reset_counters();
        latencies.reserve(client_count * 100);  // Estimate requests per client
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Start monitoring thread
        std::thread monitor_thread(&ScalabilityTester::connection_monitor, this);
        
        // Start client threads with staggered connections
        std::vector<std::thread> threads;
        threads.reserve(client_count);
        
        for (int i = 0; i < client_count; ++i) {
            if (protocol == "TCP") {
                threads.emplace_back(&ScalabilityTester::tcp_client_worker, this, i);
            } else {
                threads.emplace_back(&ScalabilityTester::udp_client_worker, this, i);
            }
            
            // Stagger connection attempts
            if (i < client_count - 1) {
                int delay_ms = (RAMP_UP_DURATION_SEC * 1000) / client_count;
                std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, delay_ms)));
            }
        }
        
        // Let the test run
        std::this_thread::sleep_for(std::chrono::seconds(TEST_DURATION_SEC));
        
        // Signal threads to stop
        stop_test = true;
        
        // Wait for all threads
        for (auto& thread : threads) {
            thread.join();
        }
        monitor_thread.join();
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        // Calculate result
        ScalabilityResult result;
        result.client_count = client_count;
        result.timestamp = get_timestamp();
        result.total_requests = latencies.size();
        result.successful_requests = latencies.size();
        result.success_rate = result.total_requests > 0 ? 100.0 : 0.0;
        result.connections_per_second = (double)connections / (duration.count() / 1000.0);
        result.peak_concurrent_connections = peak_connections;
        
        // Calculate throughput
        double duration_seconds = duration.count() / 1000.0;
        double megabytes = total_bytes / (1024.0 * 1024.0);
        result.throughput_mbps = megabytes / duration_seconds;
        
        // Calculate all percentiles P1 to P100
        result.percentiles = calculate_all_percentiles(latencies);
        
        return result;
    }
    
    void reset_counters() {
        connections = 0;
        active_connections = 0;
        peak_connections = 0;
        total_bytes = 0;
        stop_test = false;
        latencies.clear();
    }
    
    void connection_monitor() {
        while (!stop_test) {
            int current = active_connections.load();
            if (current > peak_connections) {
                peak_connections = current;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    void tcp_client_worker(int client_id) {
        // Random delay for realistic connection pattern
        std::uniform_int_distribution<int> delay_dist(0, 500);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_dist(rng)));
        
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1) return;
        
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(TCP_PORT);
        inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);
        
        if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
            close(sock);
            return;
        }
        
        connections++;
        active_connections++;
        
        char send_buffer[BUFFER_SIZE];
        char recv_buffer[BUFFER_SIZE];
        memset(send_buffer, 'A', sizeof(send_buffer));
        
        std::uniform_int_distribution<int> interval_dist(20, 150);
        
        while (!stop_test) {
            auto request_start = std::chrono::high_resolution_clock::now();
            
            ssize_t sent = send(sock, send_buffer, sizeof(send_buffer), 0);
            if (sent > 0) {
                ssize_t received = recv(sock, recv_buffer, sizeof(recv_buffer), 0);
                if (received > 0) {
                    auto request_end = std::chrono::high_resolution_clock::now();
                    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(request_end - request_start).count() / 1000.0;
                    
                    {
                        std::lock_guard<std::mutex> lock(results_mutex);
                        latencies.push_back(latency);
                    }
                    
                    total_bytes += sent + received;
                } else {
                    break;
                }
            } else {
                break;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_dist(rng)));
        }
        
        active_connections--;
        close(sock);
    }
    
    void udp_client_worker(int client_id) {
        // Random delay for realistic connection pattern
        std::uniform_int_distribution<int> delay_dist(0, 500);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_dist(rng)));
        
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == -1) return;
        
        // Set socket timeout
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(UDP_PORT);
        inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);
        
        connections++;
        active_connections++;
        
        char send_buffer[BUFFER_SIZE];
        char recv_buffer[BUFFER_SIZE];
        memset(send_buffer, 'A', sizeof(send_buffer));
        
        std::uniform_int_distribution<int> interval_dist(10, 100);
        
        while (!stop_test) {
            auto request_start = std::chrono::high_resolution_clock::now();
            
            ssize_t sent = sendto(sock, send_buffer, sizeof(send_buffer), 0, 
                                 (struct sockaddr*)&server_addr, sizeof(server_addr));
            if (sent > 0) {
                socklen_t addr_len = sizeof(server_addr);
                ssize_t received = recvfrom(sock, recv_buffer, sizeof(recv_buffer), 0,
                                          (struct sockaddr*)&server_addr, &addr_len);
                if (received > 0) {
                    auto request_end = std::chrono::high_resolution_clock::now();
                    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(request_end - request_start).count() / 1000.0;
                    
                    {
                        std::lock_guard<std::mutex> lock(results_mutex);
                        latencies.push_back(latency);
                    }
                    
                    total_bytes += sent + received;
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_dist(rng)));
        }
        
        active_connections--;
        close(sock);
    }
    
    std::vector<double> calculate_all_percentiles(const std::vector<double>& data) {
        if (data.empty()) {
            return std::vector<double>(100, 0.0);
        }
        
        std::vector<double> sorted_data = data;
        std::sort(sorted_data.begin(), sorted_data.end());
        
        std::vector<double> percentiles;
        percentiles.reserve(100);
        
        for (int p = 1; p <= 100; ++p) {
            size_t index = (sorted_data.size() * p) / 100;
            if (index > 0) index--;  // Convert to 0-based index
            if (index >= sorted_data.size()) index = sorted_data.size() - 1;
            percentiles.push_back(sorted_data[index]);
        }
        
        return percentiles;
    }
    
    std::string get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }
    
    void log_result(const ScalabilityResult& result) {
        if (!log_file.is_open()) return;
        
        // Determine protocol from context
        std::string protocol = "TCP";  // Default to TCP
        static bool tcp_done = false;
        static int tcp_tests_completed = 0;
        
        if (!tcp_done) {
            protocol = "TCP";
            tcp_tests_completed++;
            if (tcp_tests_completed >= 9) {  // 9 TCP tests (10,20,50,100,200,500,1000,2000,5000)
                tcp_done = true;
            }
        } else {
            protocol = "UDP";
        }
        
        // Print to console
        std::cout << "Clients: " << result.client_count 
                  << ", Throughput: " << std::fixed << std::setprecision(2) << result.throughput_mbps << " MB/s"
                  << ", P50: " << std::setprecision(3) << result.percentiles[49] << "ms"
                  << ", P95: " << result.percentiles[94] << "ms"
                  << ", P99: " << result.percentiles[98] << "ms" << std::endl;
        
        // Write to log file
        log_file << protocol << "," << result.client_count << "," << result.timestamp << ","
                 << std::fixed << std::setprecision(6) << result.throughput_mbps << ","
                 << result.connections_per_second << "," << result.peak_concurrent_connections << ","
                 << result.success_rate << "," << result.total_requests << "," << result.successful_requests;
        
        // Write all percentiles P1 to P100
        for (double percentile : result.percentiles) {
            log_file << "," << std::setprecision(6) << percentile;
        }
        log_file << "\n";
        log_file.flush();
    }
};

int main() {
    std::cout << "Network Scalability Testing Framework" << std::endl;
    std::cout << "=====================================" << std::endl;
    
    ScalabilityTester tester;
    tester.run_scalability_tests();
    
    return 0;
} 