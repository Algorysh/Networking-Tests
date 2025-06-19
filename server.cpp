#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <vector>
#include <chrono>
#include <atomic>
#include <signal.h>
#include <unordered_map>

const int MAX_EVENTS = 1024;
const int BUFFER_SIZE = 1024;
const int TCP_PORT = 8080;
const int UDP_PORT = 8081;
const int QUIC_PORT = 8082;

// Basic QUIC connection tracking
struct QuicConnection {
    uint32_t connection_id;
    struct sockaddr_in client_addr;
    std::chrono::steady_clock::time_point last_activity;
    bool established;
    
    QuicConnection(uint32_t id, const struct sockaddr_in& addr) 
        : connection_id(id), client_addr(addr), 
          last_activity(std::chrono::steady_clock::now()), established(false) {}
};

class EpollServer {
private:
    int epoll_fd;
    int tcp_fd;
    int udp_fd;
    int quic_fd;
    struct epoll_event events[MAX_EVENTS];
    std::atomic<int> tcp_connections{0};
    std::atomic<int> udp_packets{0};
    std::atomic<int> quic_connections{0};
    std::unordered_map<uint32_t, QuicConnection> quic_connections_map;

public:
    EpollServer() : epoll_fd(-1), tcp_fd(-1), udp_fd(-1), quic_fd(-1) {}

    ~EpollServer() {
        cleanup();
    }

    bool initialize() {
        // Ignore SIGPIPE to prevent crashes on broken connections
        signal(SIGPIPE, SIG_IGN);
        
        // Create epoll instance
        epoll_fd = epoll_create1(0);
        if (epoll_fd == -1) {
            perror("epoll_create1");
            return false;
        }

        // Setup TCP socket
        if (!setup_tcp_socket()) {
            return false;
        }

        // Setup UDP socket
        if (!setup_udp_socket()) {
            return false;
        }

        // Setup QUIC socket
        if (!setup_quic_socket()) {
            return false;
        }

        return true;
    }

    bool setup_tcp_socket() {
        tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (tcp_fd == -1) {
            perror("TCP socket");
            return false;
        }

        // Set socket to non-blocking
        int flags = fcntl(tcp_fd, F_GETFL, 0);
        fcntl(tcp_fd, F_SETFL, flags | O_NONBLOCK);

        // Set SO_REUSEADDR
        int opt = 1;
        setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(TCP_PORT);

        if (bind(tcp_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            perror("TCP bind");
            return false;
        }

        if (listen(tcp_fd, SOMAXCONN) == -1) {
            perror("TCP listen");
            return false;
        }

        // Add to epoll
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = tcp_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tcp_fd, &ev) == -1) {
            perror("epoll_ctl TCP");
            return false;
        }

        std::cout << "TCP server listening on port " << TCP_PORT << std::endl;
        return true;
    }

    bool setup_udp_socket() {
        udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_fd == -1) {
            perror("UDP socket");
            return false;
        }

        // Set socket to non-blocking
        int flags = fcntl(udp_fd, F_GETFL, 0);
        fcntl(udp_fd, F_SETFL, flags | O_NONBLOCK);

        // Set SO_REUSEADDR for UDP as well
        int opt = 1;
        setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // Increase socket buffer sizes for better UDP performance
        int buf_size = 1024 * 1024;  // 1MB buffer
        setsockopt(udp_fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
        setsockopt(udp_fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(UDP_PORT);

        if (bind(udp_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            perror("UDP bind");
            return false;
        }

        // Add to epoll
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = udp_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, udp_fd, &ev) == -1) {
            perror("epoll_ctl UDP");
            return false;
        }

        std::cout << "UDP server listening on port " << UDP_PORT << std::endl;
        return true;
    }

    bool setup_quic_socket() {
        quic_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (quic_fd == -1) {
            perror("QUIC socket");
            return false;
        }

        // Set socket to non-blocking
        int flags = fcntl(quic_fd, F_GETFL, 0);
        fcntl(quic_fd, F_SETFL, flags | O_NONBLOCK);

        // Set SO_REUSEADDR
        int opt = 1;
        setsockopt(quic_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // Increase socket buffer sizes for better QUIC performance
        int buf_size = 1024 * 1024;  // 1MB buffer
        setsockopt(quic_fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
        setsockopt(quic_fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(QUIC_PORT);

        if (bind(quic_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            perror("QUIC bind");
            return false;
        }

        // Add to epoll
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = quic_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, quic_fd, &ev) == -1) {
            perror("epoll_ctl QUIC");
            return false;
        }

        std::cout << "QUIC server listening on port " << QUIC_PORT << std::endl;
        return true;
    }

    void run() {
        std::cout << "Server started. Press Ctrl+C to stop." << std::endl;
        
        while (true) {
            int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
            if (nfds == -1) {
                if (errno == EINTR) continue;  // Interrupted by signal, continue
                perror("epoll_wait");
                break;
            }

            for (int i = 0; i < nfds; i++) {
                if (events[i].data.fd == tcp_fd) {
                    handle_tcp_connection();
                } else if (events[i].data.fd == udp_fd) {
                    handle_udp_packet();
                } else if (events[i].data.fd == quic_fd) {
                    handle_quic_connection();
                } else {
                    // Check for errors or hangup
                    if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                        close_client(events[i].data.fd);
                    } else {
                        handle_tcp_client(events[i].data.fd);
                    }
                }
            }
        }
    }

private:
    void handle_tcp_connection() {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // Accept multiple connections in one go
        while (true) {
            int client_fd = accept(tcp_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;  // No more pending connections
                }
                if (errno == EMFILE || errno == ENFILE) {
                    std::cerr << "Too many open files - rejecting connection" << std::endl;
                    break;
                }
                perror("accept");
                break;
            }

            tcp_connections++;
            if (tcp_connections % 100 == 0) {
                std::cout << "TCP connections: " << tcp_connections << std::endl;
            }

            // Set client socket to non-blocking
            int flags = fcntl(client_fd, F_GETFL, 0);
            fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

            // Add client to epoll with error detection
            struct epoll_event ev;
            ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
            ev.data.fd = client_fd;
            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
                perror("epoll_ctl client");
                close(client_fd);
                continue;
            }
        }
    }

    void handle_tcp_client(int client_fd) {
        char buffer[BUFFER_SIZE];
        ssize_t bytes_read;

        while ((bytes_read = read(client_fd, buffer, sizeof(buffer))) > 0) {
            // Echo back the data
            ssize_t bytes_written = 0;
            ssize_t total_written = 0;
            
            while (total_written < bytes_read) {
                bytes_written = write(client_fd, buffer + total_written, bytes_read - total_written);
                if (bytes_written == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    if (errno == EPIPE) {
                        // Client disconnected
                        close_client(client_fd);
                        return;
                    }
                    perror("write");
                    close_client(client_fd);
                    return;
                }
                total_written += bytes_written;
            }
        }

        if (bytes_read == 0) {
            // Client closed connection
            close_client(client_fd);
        } else if (bytes_read == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                close_client(client_fd);
            }
        }
    }

    void handle_udp_packet() {
        char buffer[BUFFER_SIZE];
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        // Process multiple packets in one go to handle high load
        while (true) {
            memset(&client_addr, 0, sizeof(client_addr));
            client_len = sizeof(client_addr);
            
            ssize_t bytes_read = recvfrom(udp_fd, buffer, sizeof(buffer), 0, 
                                         (struct sockaddr*)&client_addr, &client_len);
            if (bytes_read > 0) {
                // Echo back the data
                ssize_t bytes_sent = sendto(udp_fd, buffer, bytes_read, 0, 
                                          (struct sockaddr*)&client_addr, client_len);
                if (bytes_sent == -1) {
                    perror("UDP sendto");
                } else {
                    udp_packets++;
                    if (udp_packets % 1000 == 0) {
                        std::cout << "UDP packets processed: " << udp_packets << std::endl;
                    }
                }
            } else if (bytes_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // No more packets to read
                break;
            } else if (bytes_read == -1) {
                perror("UDP recvfrom");
                break;
            }
        }
    }

    void handle_quic_connection() {
        char buffer[BUFFER_SIZE];
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        while (true) {
            ssize_t bytes_received = recvfrom(quic_fd, buffer, BUFFER_SIZE - 1, 0,
                                            (struct sockaddr*)&client_addr, &client_len);
            if (bytes_received == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;  // No more data
                }
                perror("QUIC recvfrom");
                break;
            }
            
            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';
                
                // Basic QUIC packet handling - extract connection ID from first 4 bytes
                uint32_t connection_id = 0;
                if (bytes_received >= 4) {
                    memcpy(&connection_id, buffer, sizeof(uint32_t));
                    connection_id = ntohl(connection_id);
                }
                
                // Track or update connection
                auto it = quic_connections_map.find(connection_id);
                if (it == quic_connections_map.end()) {
                    // New connection
                    quic_connections_map.emplace(connection_id, QuicConnection(connection_id, client_addr));
                    quic_connections++;
                    if (quic_connections % 100 == 0) {
                        std::cout << "QUIC connections: " << quic_connections << std::endl;
                    }
                } else {
                    // Update existing connection
                    it->second.last_activity = std::chrono::steady_clock::now();
                }
                
                // Echo response with QUIC header
                char response[BUFFER_SIZE];
                memcpy(response, &connection_id, sizeof(uint32_t));
                memcpy(response + sizeof(uint32_t), "QUIC Echo: ", 11);
                memcpy(response + sizeof(uint32_t) + 11, buffer + sizeof(uint32_t), 
                       std::min((size_t)(bytes_received - sizeof(uint32_t)), 
                               (size_t)(BUFFER_SIZE - sizeof(uint32_t) - 11)));
                
                ssize_t response_size = sizeof(uint32_t) + 11 + (bytes_received - sizeof(uint32_t));
                sendto(quic_fd, response, response_size, 0,
                       (struct sockaddr*)&client_addr, client_len);
            }
        }
    }

    void close_client(int client_fd) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
        close(client_fd);
        tcp_connections--;
    }

    void cleanup() {
        if (tcp_fd != -1) close(tcp_fd);
        if (udp_fd != -1) close(udp_fd);
        if (quic_fd != -1) close(quic_fd);
        if (epoll_fd != -1) close(epoll_fd);
    }
};

int main() {
    EpollServer server;
    
    if (!server.initialize()) {
        std::cerr << "Failed to initialize server" << std::endl;
        return 1;
    }

    server.run();
    return 0;
} 