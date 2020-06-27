// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <tftp/tftp.h>
#include <unittest/unittest.h>

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <atomic>

// This test simulates a tftp file transfer by running two threads. Both the
// file and transport interfaces are implemented in memory buffers.

typedef enum { DIR_SEND, DIR_RECEIVE } xfer_dir_t;

struct test_params {
  xfer_dir_t direction;
  uint32_t filesz;
  uint16_t winsz;
  uint16_t blksz;
};

static uint8_t* src_file;
static uint8_t* dst_file;

/* FAUX FILES INTERFACE */

typedef struct {
  uint8_t* buf;
  char filename[PATH_MAX + 1];
  size_t filesz;
} file_info_t;

// Allocate our src and dst buffers, filling both with random values.
int initialize_files(struct test_params* tp) {
  src_file = reinterpret_cast<uint8_t*>(malloc(tp->filesz));
  dst_file = reinterpret_cast<uint8_t*>(malloc(tp->filesz));

  if (!src_file || !dst_file) {
    return 1;
  }

  int* src_as_ints = (int*)src_file;
  int* dst_as_ints = (int*)dst_file;

  size_t ndx;
  srand(0);
  for (ndx = 0; ndx < tp->filesz / sizeof(int); ndx++) {
    src_as_ints[ndx] = rand();
    dst_as_ints[ndx] = rand();
  }
  for (ndx = (tp->filesz / sizeof(int)) * sizeof(int); ndx < tp->filesz; ndx++) {
    src_file[ndx] = static_cast<uint8_t>(rand());
    dst_file[ndx] = static_cast<uint8_t>(rand());
  }

  return 0;
}

int compare_files(size_t filesz) { return memcmp(src_file, dst_file, filesz); }

const char* file_get_filename(file_info_t* file_info) { return file_info->filename; }

ssize_t file_open_read(const char* filename, void* file_cookie) {
  auto* file_info = reinterpret_cast<file_info_t*>(file_cookie);
  file_info->buf = src_file;
  strncpy(file_info->filename, filename, PATH_MAX);
  file_info->filename[PATH_MAX] = '\0';
  return file_info->filesz;
}

tftp_status file_open_write(const char* filename, size_t size, void* file_cookie) {
  auto* file_info = reinterpret_cast<file_info_t*>(file_cookie);
  file_info->buf = dst_file;
  file_info->filename[PATH_MAX] = '\0';
  strncpy(file_info->filename, filename, PATH_MAX);
  return TFTP_NO_ERROR;
}

// Every SHORT_READ_FREQ writes we will read a smaller amount instead, to verify behavior when
// a read operation returns a length less than the requested amount.
#define SHORT_READ_FREQ 10

tftp_status file_read(void* data, size_t* length, off_t offset, void* file_cookie) {
  auto* file_info = reinterpret_cast<file_info_t*>(file_cookie);
  if ((size_t)offset > file_info->filesz) {
    // Something has gone wrong in libtftp
    return TFTP_ERR_INTERNAL;
  }
  if ((offset + *length) > file_info->filesz) {
    *length = file_info->filesz - offset;
  }
  static size_t read_count = 0;
  if (read_count++ % SHORT_READ_FREQ == 0) {
    *length /= 2;
  }
  memcpy(data, &file_info->buf[offset], *length);
  return TFTP_NO_ERROR;
}

// Every SHORT_WRITE_FREQ writes we will write a smaller amount instead, to verify behavior when
// a write operation returns a length less than the requested amount.
#define SHORT_WRITE_FREQ 10

tftp_status file_write(const void* data, size_t* length, off_t offset, void* file_cookie) {
  auto* file_info = reinterpret_cast<file_info_t*>(file_cookie);
  if (((size_t)offset > file_info->filesz) || ((offset + *length) > file_info->filesz)) {
    // Something has gone wrong in libtftp
    return TFTP_ERR_INTERNAL;
  }
  static size_t write_count = 0;
  if (write_count++ % SHORT_WRITE_FREQ == 0) {
    *length /= 2;
  }
  memcpy(&file_info->buf[offset], data, *length);
  return TFTP_NO_ERROR;
}

void file_close(void* file_cookie) {}

/* FAUX SOCKET INTERFACE */

#define FAKE_SOCK_BUF_SZ 65536
typedef struct {
  uint8_t buf[FAKE_SOCK_BUF_SZ];
  size_t size = FAKE_SOCK_BUF_SZ;
  std::atomic<size_t> read_ndx;
  std::atomic<size_t> write_ndx;
} fake_socket_t;
static fake_socket_t client_out_socket;
static fake_socket_t server_out_socket;

typedef struct {
  fake_socket_t* in_sock;
  fake_socket_t* out_sock;
} transport_info_t;

void clear_sockets(void) {
  client_out_socket.read_ndx.store(0);
  client_out_socket.write_ndx.store(0);
  server_out_socket.read_ndx.store(0);
  server_out_socket.write_ndx.store(0);
}

// Initialize "sockets" for either client or server.
void transport_init(transport_info_t* transport_info, bool is_server) {
  if (is_server) {
    transport_info->in_sock = &client_out_socket;
    transport_info->out_sock = &server_out_socket;
  } else {
    transport_info->in_sock = &server_out_socket;
    transport_info->out_sock = &client_out_socket;
  }
}

// Write to our circular message buffer.
void write_to_buf(fake_socket_t* sock, void* data, size_t size) {
  uint8_t* in_buf = reinterpret_cast<uint8_t*>(data);
  uint8_t* out_buf = sock->buf;
  size_t curr_offset = sock->write_ndx.load() % sock->size;
  if (curr_offset + size <= sock->size) {
    memcpy(&out_buf[curr_offset], in_buf, size);
  } else {
    size_t first_size = sock->size - curr_offset;
    size_t second_size = size - first_size;
    memcpy(out_buf + curr_offset, in_buf, first_size);
    memcpy(out_buf, in_buf + first_size, second_size);
  }
  sock->write_ndx.fetch_add(size);
}

// Send a message. Note that the buffer's read_ndx and write_ndx don't wrap,
// which makes it easier to recognize underflow.
tftp_status transport_send(void* data, size_t len, void* transport_cookie) {
  auto* transport_info = reinterpret_cast<transport_info_t*>(transport_cookie);
  fake_socket_t* sock = transport_info->out_sock;
  while ((sock->write_ndx.load() + sizeof(len) + len - sock->read_ndx.load()) > sock->size) {
    // Wait for the other thread to catch up
    usleep(10);
  }
  write_to_buf(sock, &len, sizeof(len));
  write_to_buf(sock, data, len);
  return TFTP_NO_ERROR;
}

// Read from our circular message buffer. If |move_ptr| is false, just peeks at
// the data (reads without updating the read pointer).
void read_from_buf(fake_socket_t* sock, void* data, size_t size, bool move_ptr) {
  uint8_t* in_buf = sock->buf;
  uint8_t* out_buf = reinterpret_cast<uint8_t*>(data);
  size_t curr_offset = sock->read_ndx.load() % sock->size;
  if (curr_offset + size <= sock->size) {
    memcpy(out_buf, &in_buf[curr_offset], size);
  } else {
    size_t first_size = sock->size - curr_offset;
    size_t second_size = size - first_size;
    memcpy(out_buf, in_buf + curr_offset, first_size);
    memcpy(out_buf + first_size, in_buf, second_size);
  }
  if (move_ptr) {
    sock->read_ndx.fetch_add(size);
  }
}

// Receive a message. Note that the buffer's read_ndx and write_ndx don't
// wrap, which makes it easier to recognize underflow.
int transport_recv(void* data, size_t len, bool block, void* transport_cookie) {
  auto* transport_info = reinterpret_cast<transport_info_t*>(transport_cookie);
  if (block) {
    while ((transport_info->in_sock->read_ndx.load() + sizeof(size_t)) >=
           transport_info->in_sock->write_ndx.load()) {
      usleep(10);
    }
  } else if ((transport_info->in_sock->read_ndx.load() + sizeof(size_t)) >=
             transport_info->in_sock->write_ndx.load()) {
    return TFTP_ERR_TIMED_OUT;
  }
  size_t block_len;
  read_from_buf(transport_info->in_sock, &block_len, sizeof(block_len), false);
  if (block_len > len) {
    return TFTP_ERR_BUFFER_TOO_SMALL;
  }
  transport_info->in_sock->read_ndx.fetch_add(sizeof(block_len));
  read_from_buf(transport_info->in_sock, data, block_len, true);
  return static_cast<int>(block_len);
}

int transport_timeout_set(uint32_t timeout_ms, void* transport_cookie) { return 0; }

/// SEND THREAD

bool run_client_test(struct test_params* tp) {
  BEGIN_HELPER;

  // Configure TFTP session
  tftp_session* session;
  size_t session_size = tftp_sizeof_session();
  void* session_buf = malloc(session_size);
  ASSERT_NONNULL(session_buf, "memory allocation failed");

  tftp_status status = tftp_init(&session, session_buf, session_size);
  ASSERT_EQ(status, TFTP_NO_ERROR, "unable to initialize a tftp session");

  // Configure file interface
  file_info_t file_info;
  file_info.filesz = tp->filesz;
  tftp_file_interface file_callbacks = {file_open_read, file_open_write, file_read, file_write,
                                        file_close};
  status = tftp_session_set_file_interface(session, &file_callbacks);
  ASSERT_EQ(status, TFTP_NO_ERROR, "could not set file interface");

  // Configure transport interface
  transport_info_t transport_info;
  transport_init(&transport_info, false);

  tftp_transport_interface transport_callbacks = {transport_send, transport_recv,
                                                  transport_timeout_set};
  status = tftp_session_set_transport_interface(session, &transport_callbacks);
  ASSERT_EQ(status, TFTP_NO_ERROR, "could not set transport interface");

  // Allocate intermediate buffers
  size_t buf_sz = tp->blksz > PATH_MAX ? tp->blksz + 2 : PATH_MAX + 2;
  char* msg_in_buf = reinterpret_cast<char*>(malloc(buf_sz));
  ASSERT_NONNULL(msg_in_buf, "memory allocation failure");
  char* msg_out_buf = reinterpret_cast<char*>(malloc(buf_sz));
  ASSERT_NONNULL(msg_out_buf, "memory allocation failure");

  char err_msg_buf[128];

  // Set our preferred transport options
  tftp_set_options(session, &tp->blksz, NULL, &tp->winsz);

  tftp_request_opts opts = {};
  opts.inbuf = msg_in_buf;
  opts.inbuf_sz = buf_sz;
  opts.outbuf = msg_out_buf;
  opts.outbuf_sz = buf_sz;
  opts.err_msg = err_msg_buf;
  opts.err_msg_sz = sizeof(err_msg_buf);

  if (tp->direction == DIR_SEND) {
    status = tftp_push_file(session, &transport_info, &file_info, "abc.txt", "xyz.txt", &opts);
    EXPECT_GE(status, 0, "failed to send file");
  } else {
    status = tftp_pull_file(session, &transport_info, &file_info, "abc.txt", "xyz.txt", &opts);
    EXPECT_GE(status, 0, "failed to receive file");
  }

  free(session);
  END_HELPER;
}

void* tftp_client_main(void* arg) {
  auto* tp = reinterpret_cast<test_params*>(arg);
  run_client_test(tp);
  pthread_exit(NULL);
}

/// RECV THREAD

bool run_server_test(struct test_params* tp) {
  BEGIN_HELPER;

  // Configure TFTP session
  tftp_session* session;
  size_t session_size = tftp_sizeof_session();
  void* session_buf = malloc(session_size);
  ASSERT_NONNULL(session_buf, "memory allocation failed");

  tftp_status status = tftp_init(&session, session_buf, session_size);
  ASSERT_EQ(status, TFTP_NO_ERROR, "unable to initiate a tftp session");

  // Configure file interface
  file_info_t file_info;
  file_info.filesz = tp->filesz;
  tftp_file_interface file_callbacks = {file_open_read, file_open_write, file_read, file_write,
                                        file_close};
  status = tftp_session_set_file_interface(session, &file_callbacks);
  ASSERT_EQ(status, TFTP_NO_ERROR, "could not set file interface");

  // Configure transport interface
  transport_info_t transport_info;
  transport_init(&transport_info, true);
  tftp_transport_interface transport_callbacks = {transport_send, transport_recv,
                                                  transport_timeout_set};
  status = tftp_session_set_transport_interface(session, &transport_callbacks);
  ASSERT_EQ(status, TFTP_NO_ERROR, "could not set transport interface");

  // Allocate intermediate buffers
  size_t buf_sz = tp->blksz > PATH_MAX ? tp->blksz + 2 : PATH_MAX + 2;
  char* msg_in_buf = reinterpret_cast<char*>(malloc(buf_sz));
  ASSERT_NONNULL(msg_in_buf, "memory allocation failure");
  char* msg_out_buf = reinterpret_cast<char*>(malloc(buf_sz));
  ASSERT_NONNULL(msg_out_buf, "memory allocation failure");

  char err_msg_buf[128];
  tftp_handler_opts opts = {.inbuf = msg_in_buf,
                            .inbuf_sz = buf_sz,
                            .outbuf = msg_out_buf,
                            .outbuf_sz = &buf_sz,
                            .err_msg = err_msg_buf,
                            .err_msg_sz = sizeof(err_msg_buf)};
  do {
    status = tftp_service_request(session, &transport_info, &file_info, &opts);
  } while (status == TFTP_NO_ERROR);
  EXPECT_EQ(status, TFTP_TRANSFER_COMPLETED, "failed to receive file");
  free(session);
  END_HELPER;
}

void* tftp_server_main(void* arg) {
  auto* tp = reinterpret_cast<test_params*>(arg);
  run_server_test(tp);
  pthread_exit(NULL);
}

bool run_one_test(struct test_params* tp) {
  BEGIN_TEST;
  int init_result = initialize_files(tp);
  ASSERT_EQ(init_result, 0, "failure to initialize state");

  clear_sockets();

  pthread_t client_thread, server_thread;
  pthread_create(&client_thread, NULL, tftp_client_main, tp);
  pthread_create(&server_thread, NULL, tftp_server_main, tp);

  pthread_join(client_thread, NULL);
  pthread_join(server_thread, NULL);

  int compare_result = compare_files(tp->filesz);
  EXPECT_EQ(compare_result, 0, "output file mismatch");
  END_TEST;
}

bool test_tftp_send_file(void) {
  struct test_params tp = {.direction = DIR_SEND, .filesz = 1000000, .winsz = 20, .blksz = 1000};
  return run_one_test(&tp);
}

bool test_tftp_send_file_wrapping_block_count(void) {
  // Wraps block count 4 times
  struct test_params tp = {.direction = DIR_SEND, .filesz = 2100000, .winsz = 9999, .blksz = 8};
  return run_one_test(&tp);
}

bool test_tftp_send_file_lg_window(void) {
  // Make sure that a window size > 255 works properly
  struct test_params tp = {.direction = DIR_SEND, .filesz = 1000000, .winsz = 1024, .blksz = 1024};
  return run_one_test(&tp);
}

bool test_tftp_receive_file(void) {
  struct test_params tp = {.direction = DIR_RECEIVE, .filesz = 1000000, .winsz = 20, .blksz = 1000};
  return run_one_test(&tp);
}

bool test_tftp_receive_file_wrapping_block_count(void) {
  // Wraps block count 4 times
  struct test_params tp = {.direction = DIR_RECEIVE, .filesz = 2100000, .winsz = 8192, .blksz = 8};
  return run_one_test(&tp);
}

bool test_tftp_receive_file_lg_window(void) {
  // Make sure that a window size > 255 works properly
  struct test_params tp = {
      .direction = DIR_RECEIVE, .filesz = 1000000, .winsz = 1024, .blksz = 1024};
  return run_one_test(&tp);
}

BEGIN_TEST_CASE(tftp_transfer_file)
RUN_TEST(test_tftp_send_file)
RUN_TEST(test_tftp_send_file_wrapping_block_count)
RUN_TEST(test_tftp_send_file_lg_window)
RUN_TEST(test_tftp_receive_file)
RUN_TEST(test_tftp_receive_file_wrapping_block_count)
RUN_TEST(test_tftp_receive_file_lg_window)
END_TEST_CASE(tftp_transfer_file)
