// Copyright 2020 Charles Tytler

#pragma comment(lib, "Ws2_32.lib")

#include "pch.h"

#include "DcsSocket.h"

#include <WS2tcpip.h>

// Set default timeout for socket.
DWORD socket_timeout_ms = 100;

DcsSocket::DcsSocket(const std::string &ip_address, const std::string &ip_address_partner, const std::string &rx_port, const std::string &tx_port) {
    // Detect any missing input settings.
    if (rx_port.empty() || tx_port.empty() || ip_address.empty() || ip_address_partner.empty()) {
        const std::string error_msg =
            "Missing values from requested IP: " + ip_address + " Partner IP: " + ip_address_partner + " Rx_Port: " + rx_port + " Tx_Port: " + tx_port;
        throw std::runtime_error(error_msg);
    }

    // Initialize Windows Sockets DLL to version 2.2.
    WSADATA wsaData;
    const auto err = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (err != 0) {
        const std::string error_msg =
            "Could not startup Windows socket library -- WSA Error: " + std::to_string(WSAGetLastError());
        throw std::runtime_error(error_msg);
    }

    // Define socket address info settings for UDP protocol.
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    // Define local receive port.
    addrinfo *local_port;
    const auto getaddr_result = getaddrinfo(ip_address.c_str(), rx_port.c_str(), &hints, &local_port);
    if (getaddr_result != 0) {
        const std::string error_msg = "Could not get valid address info from requested IP: " + ip_address + " Partner IP: " + ip_address_partner +
                                      " Rx_Port: " + rx_port + " Tx_Port: " + tx_port +
                                      " -- WSA Error: " + std::to_string(WSAGetLastError());
        WSACleanup();
        throw std::runtime_error(error_msg);
    }

    // Bind local socket to receive port.
    socket_id_ = socket(local_port->ai_family, local_port->ai_socktype, local_port->ai_protocol);
    setsockopt(socket_id_, SOL_SOCKET, SO_RCVTIMEO, (const char *)&socket_timeout_ms, sizeof(socket_timeout_ms));
    const auto bind_result = bind(socket_id_, local_port->ai_addr, static_cast<int>(local_port->ai_addrlen));
    freeaddrinfo(local_port);

    if (bind_result == SOCKET_ERROR) {
        const std::string error_msg =
            "Could not bind UDP address to socket -- WSA Error: " + std::to_string(WSAGetLastError());
        closesocket(socket_id_);
        WSACleanup();
        throw std::runtime_error(error_msg);
    }

    if (tx_port != "dynamic") {
        // Define send destination port.
        addrinfo *send_to_port;
        getaddrinfo(ip_address_partner.c_str(), tx_port.c_str(), &hints, &send_to_port);
        dest_addr_ = *send_to_port->ai_addr;
        dest_addr_len_ = static_cast<int>(send_to_port->ai_addrlen);
        freeaddrinfo(send_to_port);
    }
}

DcsSocket::~DcsSocket() {
    // Delete opened socket.
    closesocket(socket_id_);
    WSACleanup();
}

std::stringstream DcsSocket::DcsReceive() {
    // Sender address - dummy variable as it is unused outside recvfrom.
    sockaddr sender_addr;
    int sender_addr_size = sizeof(sender_addr);

    // Receive next UDP message.
    constexpr int MAX_UDP_MSG_SIZE = 1024; // Maximum UDP buffer size to read.
    char msg[MAX_UDP_MSG_SIZE] = {};
    (void)recvfrom(socket_id_, msg, MAX_UDP_MSG_SIZE, 0, &sender_addr, &sender_addr_size);

    if (dest_addr_len_ == 0) {
        dest_addr_ = sender_addr;
        dest_addr_len_ = sender_addr_size;
    }

    std::stringstream ss;
    ss << msg;
    return ss;
}

void DcsSocket::DcsSend(const std::string &message) {
    (void)sendto(socket_id_, message.c_str(), static_cast<int>(message.length()), 0, &dest_addr_, dest_addr_len_);
}
