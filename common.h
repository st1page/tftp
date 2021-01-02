#pragma once
const uint k_drop_percent = 7;
const int k_timeout_ms = 1000;
const int k_timeout_us = k_timeout_ms*1000;
const int k_max_contimeouts = 10;
const int k_buffer_size = 1024;
const int k_data_slice_size = 512;
enum class Status{
  Ok = 0,
  PackageDeserializeErr,
  SendErr,
  Timeout,
  ServerErr,
  UnknownErr,
};