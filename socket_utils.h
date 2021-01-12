#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

struct sockaddr_in make_addr(char* addr, int port) {
  struct sockaddr_in serv_addr;
  bzero((char*)&serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = inet_addr(addr);
  if (INADDR_NONE == serv_addr.sin_addr.s_addr) {
    puts("invaild address!");
    exit(1);
  }
  serv_addr.sin_port = htons(port);
  return serv_addr;
}

int host_UDPsocket(int port) {
  int sockfd;
  struct sockaddr_in host_addr;
  if ((sockfd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
    puts("create local socket failed");
    exit(1);
  }
  bzero((char*)&host_addr, sizeof(host_addr));
  host_addr.sin_family = AF_INET;
  host_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  host_addr.sin_port = htons(port);

  if (bind(sockfd, (struct sockaddr*)&host_addr, sizeof(host_addr)) < 0) {
    puts("bind local socket failed");
    return -1;
  }
  return sockfd;
}
