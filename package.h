#pragma once

#include <arpa/inet.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "common.h"
#include "socket_utils.h"

enum class OpCode : uint16_t {
  rpq = 1,
  wrq,
  data,
  ack,
  err,
};

uint16_t decode16Buff(char *src) { return ntohs(*(uint16_t *)src); }
OpCode decodeOpcode(char *src) { return (OpCode)decode16Buff(src); }
void encode16Buff(char *dst, uint16_t x) { *(uint16_t *)dst = htons(x); }
void encodeOpcode(char *dst, OpCode x) { encode16Buff(dst, uint16_t(x)); }
template <typename Derived>
struct PackageBase {
  OpCode opcode;
  size_t size() { return 2 + static_cast<Derived &>(*this).size(); }
  void serialize(char *dst) {
    encodeOpcode(dst, opcode);
    static_cast<Derived &>(*this).serialize(dst + 2);
  }
  Status deserialize(char *src, size_t len) {
    if (decodeOpcode(src) != opcode) {
      if (decodeOpcode(src) == OpCode::err)
        return Status::ServerErr;
      else
        return Status::PackageDeserializeErr;
    }
    return static_cast<Derived &>(*this).deserialize(src + 2, len - 2);
  }
};

struct Request : public PackageBase<Request> {
  std::string filename;
  std::string mode;
  size_t size() { return filename.size() + 1 + mode.size() + 1; }
  void serialize(char *dst) {
    memcpy(dst, filename.c_str(), filename.size());
    dst += filename.size();
    *dst = 0;
    dst++;
    memcpy(dst, mode.c_str(), mode.size());
    dst += mode.size();
    *dst = 0;
    dst++;
    return;
  }
  Status deserialize(char *src, size_t len) {
    int cnt = 0;
    for (int i = 0; i < len; i++)
      if (src[i] == 0) cnt++;
    if (cnt != 2) return Status::PackageDeserializeErr;
    filename = std::string(src);
    for (int i = 0; i < len; i++)
      if (src[i] == 0) {
        mode = std::string(src + i + 1);
        return Status::Ok;
      }
    return Status::PackageDeserializeErr;
  }
};
struct ReadRequest : public Request {
  ReadRequest() { opcode = OpCode::rpq; }
};
struct WriteRequest : public Request {
  WriteRequest() { opcode = OpCode::wrq; }
};
struct Data : public PackageBase<Data> {
  uint16_t block_num;
  std::string data;
  Data() { opcode = OpCode::data; }
  size_t size() { return 2 + data.size(); }
  void serialize(char *dst) {
    encode16Buff(dst, block_num);
    dst += 2;
    memcpy(dst, data.c_str(), data.size());
  }
  Status deserialize(char *src, size_t len) {
    block_num = decode16Buff(src);    
    src += 2;
    len -= 2;
    if (len > 512) return Status::PackageDeserializeErr;
    data.resize(len);
    for (int i = 0; i < len; i++) data[i] = src[i];
    return Status::Ok;
  }
};

struct Ack : public PackageBase<Ack> {
  uint16_t block_num;
  Ack() { opcode = OpCode::ack; }
  size_t size() { return 2; }
  void serialize(char *dst) { encode16Buff(dst, block_num); }
  Status deserialize(char *src, size_t len) {
    block_num = decode16Buff(src);
    return Status::Ok;
  }
};

struct Err : public PackageBase<Err> {
  enum class Code : uint16_t {
    undifined = 0,
    file_not_found,
    access_violation,
    disk_full,
    illegal_op,
    unknown_trans_id,
    file_exists,
    no_such_user,
  };

  Code err_code;
  std::string err_msg;
  Err() { opcode = OpCode::err; }
  size_t size() { return 2 + err_msg.size(); }
  void serialize(char *dst) {
    encode16Buff(dst, (uint16_t)err_code);
    dst += 2;
    memcpy(dst, err_msg.c_str(), err_msg.size());
  }
  Status deserialize(char *src, size_t len) {
    err_code = (Code)decode16Buff(src);
    src += 2;
    len -= 2;
    err_msg.resize(len);
    for (int i = 0; i < len; i++) err_msg[i] = src[i];
    return Status::Ok;
  }
};

template <typename Derived>
Status send_package(int sockfd, struct sockaddr* sockaddr,
                    PackageBase<Derived>& package) {
  uint rand100 = rand() % 100u;
  if (rand100 < k_drop_percent) return Status::Ok;
  char* buffer;
  buffer = (char*)malloc(package.size());
  package.serialize(buffer);
  if (-1 == sendto(sockfd, buffer, package.size(), 0,
                   (struct sockaddr*)sockaddr, sizeof(struct sockaddr)))
    return Status::SendErr;
  return Status::Ok;
}
void recv_timeout_handler(int sig) { return; }
Err last_err_package;
template <typename Derived>
Status recv_package(int sockfd, struct sockaddr* sockaddr,
                    PackageBase<Derived>& package) {
  static char buffer[k_buffer_size];
  size_t len;
  size_t addr_len = sizeof(sockaddr);

  struct sigaction timeout_action;

  timeout_action.sa_handler = recv_timeout_handler;
  timeout_action.sa_flags = 0;
  sigaction(SIGALRM, &timeout_action, 0);
  struct itimerval tick;
  tick.it_value.tv_sec = k_timeout_us / 1000000;
  tick.it_value.tv_usec = k_timeout_us % 1000000;
  tick.it_interval.tv_sec = 0;
  tick.it_interval.tv_usec = 0;

  struct itimerval zero_it;
  memset(&zero_it, 0, sizeof(zero_it));

  setitimer(ITIMER_REAL, &tick, NULL);
  errno = 0;

  while (true) {
    len = recvfrom(sockfd, buffer, k_buffer_size, 0, sockaddr,
                   (socklen_t*)&addr_len);
    if (errno != 0) {
      if (errno == EINTR) {
        //("timeout");
      }
      setitimer(ITIMER_REAL, &zero_it, NULL);
      return Status::Timeout;
    }
    Status status = package.deserialize(buffer, len);
    if (Status::Ok == status) {
      return Status::Ok;
    } else if (Status::ServerErr == status) {
      last_err_package.deserialize(buffer, len);
      return Status::ServerErr;
    } else if (Status::PackageDeserializeErr == status) {
      continue;
    }
  }
  return Status::UnknownErr;
}