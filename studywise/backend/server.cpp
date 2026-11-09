// StudyWise C++ backend
// Imperative role: HTTP request handling, validation, and workflow orchestration.
// Object-oriented role: Assessment encapsulates the submitted study-session data.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>

class Assessment {
private:
    int attempted_;
    int aiUses_;
    int total_;

public:
    Assessment(int attempted, int aiUses, int total)
        : attempted_(attempted), aiUses_(aiUses), total_(total) {}

    bool isValid() const {
        return total_ >= 1 && total_ <= 500 &&
               attempted_ >= 0 && attempted_ <= total_ &&
               aiUses_ >= 0 && aiUses_ <= 500;
    }

    int attempted() const { return attempted_; }
    int aiUses() const { return aiUses_; }
    int total() const { return total_; }
};

struct Classification {
    std::string label;
    std::string recommendation;
};

// Small JSON helpers for this focused prototype. Request values are numeric.
std::string jsonEscape(const std::string& value) {
    std::string out;
    for (char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') { out += "\\n"; continue; }
        if (c == '\r') { out += "\\r"; continue; }
        out += c;
    }
    return out;
}

bool readInteger(const std::string& body, const std::string& key, int& value) {
    std::regex pattern("\"" + key + "\"\\s*:\\s*(-?[0-9]+)");
    std::smatch match;
    if (!std::regex_search(body, match, pattern)) return false;
    try {
        value = std::stoi(match[1].str());
        return true;
    } catch (...) {
        return false;
    }
}

// Invoke the independent Racket program and read its two-line result.
bool classifyWithRacket(const Assessment& assessment, Classification& result,
                        std::string& error) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        error = "Could not create a pipe to the Racket component.";
        return false;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]); close(pipefd[1]);
        error = "Could not start the Racket component.";
        return false;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        std::string attempted = std::to_string(assessment.attempted());
        std::string aiUses = std::to_string(assessment.aiUses());
        std::string total = std::to_string(assessment.total());

        // Run from the project root, where racket/classifier.rkt is located.
        execlp("racket", "racket", "racket/classifier.rkt",
               attempted.c_str(), aiUses.c_str(), total.c_str(),
               static_cast<char*>(nullptr));
        _exit(127);
    }

    close(pipefd[1]);
    std::string output;
    char buffer[256];
    ssize_t count;
    while ((count = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        output.append(buffer, static_cast<std::size_t>(count));
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error = "Racket could not run. Install Racket and run the backend from the project root.";
        return false;
    }

    std::istringstream lines(output);
    if (!std::getline(lines, result.label) ||
        !std::getline(lines, result.recommendation) ||
        result.label.empty() || result.recommendation.empty()) {
        error = "The Racket component returned an unexpected response.";
        return false;
    }
    return true;
}

std::string httpResponse(int status, const std::string& statusText,
                         const std::string& contentType,
                         const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << " " << statusText << "\r\n"
            << "Access-Control-Allow-Origin: *\r\n"
            << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            << "Access-Control-Allow-Headers: Content-Type\r\n"
             << "Content-Type: " << contentType << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "X-Content-Type-Options: nosniff\r\n\r\n"
             << body;
    return response.str();
}

void sendAll(int client, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(client, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) return;
        sent += static_cast<std::size_t>(n);
    }
}

void handleClient(int client) {
    std::string request;
    char buffer[4096];
    std::size_t headerEnd = std::string::npos;
    std::size_t contentLength = 0;

    // Read headers first, then exactly the declared request body.
    while ((headerEnd = request.find("\r\n\r\n")) == std::string::npos) {
        ssize_t n = recv(client, buffer, sizeof(buffer), 0);
        if (n <= 0) { close(client); return; }
        request.append(buffer, static_cast<std::size_t>(n));
        if (request.size() > 65536) {
            sendAll(client, httpResponse(413, "Payload Too Large", "application/json",
                                        "{\"error\":\"Request too large.\"}"));
            close(client); return;
        }
    }

    std::string headers = request.substr(0, headerEnd);
    std::istringstream headerStream(headers);
    std::string method, path, version;
    headerStream >> method >> path >> version;

    std::smatch lengthMatch;
    if (std::regex_search(headers, lengthMatch,
                          std::regex("Content-Length:\\s*([0-9]+)", std::regex::icase))) {
        contentLength = static_cast<std::size_t>(std::stoul(lengthMatch[1].str()));
    }

    std::size_t bodyStart = headerEnd + 4;
    while (request.size() - bodyStart < contentLength) {
        ssize_t n = recv(client, buffer, sizeof(buffer), 0);
        if (n <= 0) { close(client); return; }
        request.append(buffer, static_cast<std::size_t>(n));
        if (request.size() - bodyStart > 65536) {
            sendAll(client, httpResponse(413, "Payload Too Large", "application/json",
                                        "{\"error\":\"Request too large.\"}"));
            close(client); return;
        }
    }
    std::string body = request.substr(bodyStart, contentLength);

    if (method == "OPTIONS") {
    sendAll(client, httpResponse(
        204, "No Content", "text/plain", ""
    ));
    } else if (method == "GET" && (path == "/" || path == "/index.html")) {
        std::ifstream file("index.html", std::ios::binary);
        if (!file) {
            sendAll(client, httpResponse(500, "Internal Server Error", "text/plain",
                                        "Could not find index.html. Run the server from the StudyWise project root."));
        } else {
            std::ostringstream contents; contents << file.rdbuf();
            sendAll(client, httpResponse(200, "OK", "text/html; charset=utf-8", contents.str()));
        }
    } else if (method == "GET" && path == "/api/health") {
        sendAll(client, httpResponse(200, "OK", "application/json",
                                    "{\"status\":\"ok\",\"service\":\"StudyWise C++ backend\"}"));
    } else if (method == "POST" && path == "/api/assess") {
        int attempted = 0, aiUses = 0, total = 0;
        if (!readInteger(body, "attempted", attempted) ||
            !readInteger(body, "aiUses", aiUses) ||
            !readInteger(body, "total", total)) {
            sendAll(client, httpResponse(400, "Bad Request", "application/json",
                                        "{\"error\":\"Provide integer fields: attempted, aiUses, and total.\"}"));
        } else {
            Assessment assessment(attempted, aiUses, total);
            if (!assessment.isValid()) {
                sendAll(client, httpResponse(400, "Bad Request", "application/json",
                    "{\"error\":\"Invalid values. Total must be 1–500, attempts 0–total, and AI uses 0–500.\"}"));
            } else {
                Classification classification;
                std::string error;
                if (!classifyWithRacket(assessment, classification, error)) {
                    sendAll(client, httpResponse(503, "Service Unavailable", "application/json",
                                                 "{\"error\":\"" + jsonEscape(error) + "\"}"));
                } else {
                    std::ostringstream json;
                    json << "{\"classification\":\"" << jsonEscape(classification.label)
                         << "\",\"recommendation\":\"" << jsonEscape(classification.recommendation)
                         << "\",\"engine\":\"Racket\"}";
                    sendAll(client, httpResponse(200, "OK", "application/json", json.str()));
                }
            }
        }
    } else {
        sendAll(client, httpResponse(404, "Not Found", "application/json",
                                    "{\"error\":\"Route not found.\"}"));
    }
    close(client);
}

int main(int argc, char* argv[]) {
    const char* renderPort = std::getenv("PORT");
    int port = renderPort ? std::atoi(renderPort) : 8080;

    if (argc > 1) {
        port = std::atoi(argv[1]);
    }
    if (port < 1024 || port > 65535) {
        std::cerr << "Choose a port between 1024 and 65535.\n";
        return 1;
    }

    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) { perror("socket"); return 1; }

    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        perror("bind"); close(server); return 1;
    }
    if (listen(server, 20) < 0) {
        perror("listen"); close(server); return 1;
    }

    std::cout << "StudyWise running at http://localhost:" << port
              << "\nRun from the project root. Press Ctrl+C to stop.\n";
    while (true) {
        int client = accept(server, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept"); continue;
        }
        handleClient(client);
    }
    close(server);
    return 0;
}
