#include "Tools.h"

#include <stdlib.h>
#include <arpa/inet.h>
#include <netdb.h>

using namespace amp;

std::string Tools::getLocalIp() {
    return "0.0.0.0";

#if 0
    if(!Tools::isRunningInDocker()) {
        return "127.0.0.1";
    }

    std::string ret;

    const char *hostname = "host.docker.internal";
    struct addrinfo hints, *res, *p;
    char ipstr[INET_ADDRSTRLEN];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // IPv4 only, like getent ahostsv4
    hints.ai_socktype = SOCK_STREAM; // doesn't matter for getaddrinfo

    int status = getaddrinfo(hostname, NULL, &hints, &res);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return ret;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        struct sockaddr_in *addr = (struct sockaddr_in*)p->ai_addr;
        inet_ntop(AF_INET, &addr->sin_addr, ipstr, sizeof ipstr);
        ret = ipstr;
        printf("IPv4 address: %s\n", ipstr);
        break; // take the first one, like your awk 'NR==1'
    }

    freeaddrinfo(res);

    return ret;
#endif

}

void Tools::abort() {
    ::abort();
}
