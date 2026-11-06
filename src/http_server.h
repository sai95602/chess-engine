// Dependency-free HTTP/1.1 REST API around the engine.
#pragma once

#include <map>
#include <string>

struct HttpResponse {
    int         status = 200;
    std::string body;
    std::string contentType = "application/json";
};

// Pure request handler (also used by tests). query holds decoded URL parameters.
HttpResponse handle_api_request(const std::string& method, const std::string& path,
                                const std::map<std::string, std::string>& query, const std::string& body);

// Blocks forever serving requests on the given port.
int run_http_server(const std::string& host, int port);
