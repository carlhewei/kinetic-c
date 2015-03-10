/*
* kinetic-c-client
* Copyright (C) 2015 Seagate Technology.
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*/
#include "unity.h"
#include "bus.h"
#include "bus_inward.h"
#include "atomic.h"

#include <pthread.h>

#include "mock_bus_poll.h"
#include "mock_syscall.h"
#include "mock_send.h"
#include "mock_listener.h"
#include "mock_listener_task.h"
#include "mock_threadpool.h"
#include "mock_bus_ssl.h"
#include "mock_util.h"
#include "mock_yacht.h"
#include "yacht_internals.h"

extern boxed_msg *test_box;
extern void *value;
extern void *old_value;
extern void *unused;
extern connection_info *test_ci;
extern int completion_pipe;

void free_connection_cb(void *value, void *udata);

void setUp(void) {
    test_box = NULL;
    value = NULL;
    old_value = NULL;
    test_ci = NULL;
    completion_pipe = -1;
}

void tearDown(void) {}

void test_bus_send_request_should_reject_bad_arguments(void)
{
    struct bus b = {
        .log_level = 0,
    };
    TEST_ASSERT_FALSE(bus_send_request(NULL, NULL));
    TEST_ASSERT_FALSE(bus_send_request(&b, NULL));

    bus_user_msg msg = {
        .fd = -1,
    };
    TEST_ASSERT_FALSE(bus_send_request(&b, &msg));
}

void test_bus_send_request_should_return_false_if_allocation_fails(void)
{
    test_box = NULL;  // simulate allocation fail
    struct bus b = {
        .log_level = 0,
    };
    bus_user_msg msg = {
        .fd = 123,
    };
    TEST_ASSERT_FALSE(bus_send_request(&b, &msg));
}

void test_bus_send_request_should_reject_send_on_unregistered_socket(void)
{
    struct bus b = {
        .log_level = 0,
    };
    bus_user_msg msg = {
        .fd = 123,
    };
    test_box = calloc(1, sizeof(*test_box));
    TEST_ASSERT(test_box);
    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b.fd_set_lock, NULL));

    struct yacht fake_yacht = { .size = 0, };

    b.fd_set = &fake_yacht;
    TEST_ASSERT(b.fd_set);

    yacht_get_ExpectAndReturn(b.fd_set, msg.fd, &value, false);
    TEST_ASSERT_FALSE(bus_send_request(&b, &msg));

    TEST_ASSERT_EQUAL(0, pthread_mutex_destroy(&b.fd_set_lock));
}

void test_bus_send_request_should_reject_equal_sequence_IDs(void)
{
    struct bus b = {
        .log_level = 0,
    };
    bus_user_msg msg = {
        .fd = 123,
        .seq_id = 3,
    };
    test_box = calloc(1, sizeof(*test_box));
    TEST_ASSERT(test_box);
    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b.fd_set_lock, NULL));

    struct yacht fake_yacht = { .size = 0, };

    b.fd_set = &fake_yacht;
    TEST_ASSERT(b.fd_set);

    connection_info fake_ci = {
        .largest_wr_seq_id_seen = msg.seq_id,
    };
    value = &fake_ci;
    yacht_get_ExpectAndReturn(b.fd_set, msg.fd, &value, true);

    TEST_ASSERT_FALSE(bus_send_request(&b, &msg));

    TEST_ASSERT_EQUAL(0, pthread_mutex_destroy(&b.fd_set_lock));
}

void test_bus_send_request_should_reject_lower_sequence_IDs(void)
{
    struct bus b = {
        .log_level = 0,
    };
    bus_user_msg msg = {
        .fd = 123,
        .seq_id = 3,
    };
    test_box = calloc(1, sizeof(*test_box));
    TEST_ASSERT(test_box);
    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b.fd_set_lock, NULL));

    struct yacht fake_yacht = { .size = 0, };

    b.fd_set = &fake_yacht;
    TEST_ASSERT(b.fd_set);

    connection_info fake_ci = {
        .largest_wr_seq_id_seen = msg.seq_id + 1,
    };
    value = &fake_ci;
    yacht_get_ExpectAndReturn(b.fd_set, msg.fd, &value, true);

    TEST_ASSERT_FALSE(bus_send_request(&b, &msg));

    TEST_ASSERT_EQUAL(0, pthread_mutex_destroy(&b.fd_set_lock));
}

void test_bus_send_request_should_expose_callee_send_rejection(void)
{
    struct bus b = {
        .log_level = 0,
    };
    bus_user_msg msg = {
        .fd = 123,
        .seq_id = 3,
    };
    test_box = calloc(1, sizeof(*test_box));
    TEST_ASSERT(test_box);
    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b.fd_set_lock, NULL));

    struct yacht fake_yacht = { .size = 0, };

    b.fd_set = &fake_yacht;
    TEST_ASSERT(b.fd_set);

    connection_info fake_ci = {
        .largest_wr_seq_id_seen = msg.seq_id - 1,
    };
    value = &fake_ci;
    yacht_get_ExpectAndReturn(b.fd_set, msg.fd, &value, true);

    send_do_blocking_send_ExpectAndReturn(&b, test_box, false);
    TEST_ASSERT_FALSE(bus_send_request(&b, &msg));

    TEST_ASSERT_EQUAL(0, pthread_mutex_destroy(&b.fd_set_lock));
}

void test_bus_send_request_should_return_true_on_successful_delivery_queueing(void)
{
    struct bus b = {
        .log_level = 0,
    };
    bus_user_msg msg = {
        .fd = 123,
        .seq_id = 3,
    };
    test_box = calloc(1, sizeof(*test_box));
    TEST_ASSERT(test_box);
    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b.fd_set_lock, NULL));

    struct yacht fake_yacht = { .size = 0, };

    b.fd_set = &fake_yacht;
    TEST_ASSERT(b.fd_set);

    connection_info fake_ci = {
        .largest_wr_seq_id_seen = msg.seq_id - 1,
    };
    value = &fake_ci;
    yacht_get_ExpectAndReturn(b.fd_set, msg.fd, &value, true);

    send_do_blocking_send_ExpectAndReturn(&b, test_box, true);
    TEST_ASSERT_TRUE(bus_send_request(&b, &msg));

    TEST_ASSERT_EQUAL(0, pthread_mutex_destroy(&b.fd_set_lock));
}

void test_bus_register_socket_should_expose_memory_failures(void)
{
    struct listener fake_listener;
    struct listener *listeners[] = {
        &fake_listener,
    };
    struct bus b = {
        .listener_count = 1,
        .listeners = listeners,
    };
    fake_listener.bus = &b;

    TEST_ASSERT_FALSE(bus_register_socket(&b, BUS_SOCKET_PLAIN, 4, NULL));
}

void test_bus_register_socket_should_expose_SSL_connection_failure(void)
{
    struct listener fake_listener;
    struct listener *listeners[] = {
        &fake_listener,
    };
    struct bus b = {
        .listener_count = 1,
        .listeners = listeners,
    };
    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b.fd_set_lock, NULL));
    fake_listener.bus = &b;
    test_ci = calloc(1, sizeof(*test_ci));

    bus_ssl_connect_ExpectAndReturn(&b, 35, NULL);
    TEST_ASSERT_FALSE(bus_register_socket(&b, BUS_SOCKET_SSL, 35, NULL));
    TEST_ASSERT_EQUAL(0, pthread_mutex_destroy(&b.fd_set_lock));
}

void test_bus_register_socket_should_expose_hash_table_failure(void)
{
    struct listener fake_listener;
    struct listener *listeners[] = {
        &fake_listener,
    };
    struct bus b = {
        .listener_count = 1,
        .listeners = listeners,
    };
    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b.fd_set_lock, NULL));
    fake_listener.bus = &b;
    test_ci = calloc(1, sizeof(*test_ci));

    struct yacht fake_yacht = { .size = 0, };
    b.fd_set = &fake_yacht;
    yacht_set_ExpectAndReturn(b.fd_set, 35, test_ci, &old_value, false);
    TEST_ASSERT_FALSE(bus_register_socket(&b, BUS_SOCKET_PLAIN, 35, NULL));
}

void test_bus_register_socket_should_expose_listener_add_socket_failure(void)
{
    struct listener fake_listener;
    struct listener *listeners[] = {
        &fake_listener,
    };
    struct bus b = {
        .listener_count = 1,
        .listeners = listeners,
    };
    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b.fd_set_lock, NULL));
    fake_listener.bus = &b;
    test_ci = calloc(1, sizeof(*test_ci));

    struct yacht fake_yacht = { .size = 0, };
    b.fd_set = &fake_yacht;
    yacht_set_ExpectAndReturn(b.fd_set, 35, test_ci, &old_value, true);
    listener_add_socket_ExpectAndReturn(&fake_listener, test_ci, &completion_pipe, false);
    
    TEST_ASSERT_FALSE(bus_register_socket(&b, BUS_SOCKET_PLAIN, 35, NULL));
}

void test_bus_register_socket_should_expose_poll_error(void)
{
    struct listener fake_listener;
    struct listener *listeners[] = {
        &fake_listener,
    };
    struct bus b = {
        .listener_count = 1,
        .listeners = listeners,
    };
    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b.fd_set_lock, NULL));
    fake_listener.bus = &b;
    test_ci = calloc(1, sizeof(*test_ci));

    struct yacht fake_yacht = { .size = 0, };
    b.fd_set = &fake_yacht;
    yacht_set_ExpectAndReturn(b.fd_set, 35, test_ci, &old_value, true);
    listener_add_socket_ExpectAndReturn(&fake_listener, test_ci, &completion_pipe, true);
    completion_pipe = 123;
    bus_poll_on_completion_ExpectAndReturn(&b, 123, false);

    TEST_ASSERT_FALSE(bus_register_socket(&b, BUS_SOCKET_PLAIN, 35, NULL));
}

void test_bus_register_socket_should_successfully_add_plain_socket(void)
{
    struct listener fake_listener;
    struct listener *listeners[] = {
        &fake_listener,
    };
    struct bus b = {
        .listener_count = 1,
        .listeners = listeners,
    };
    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b.fd_set_lock, NULL));
    fake_listener.bus = &b;
    test_ci = calloc(1, sizeof(*test_ci));

    struct yacht fake_yacht = { .size = 0, };
    b.fd_set = &fake_yacht;
    yacht_set_ExpectAndReturn(b.fd_set, 35, test_ci, &old_value, true);
    listener_add_socket_ExpectAndReturn(&fake_listener, test_ci, &completion_pipe, true);
    completion_pipe = 123;
    bus_poll_on_completion_ExpectAndReturn(&b, 123, true);

    TEST_ASSERT_TRUE(bus_register_socket(&b, BUS_SOCKET_PLAIN, 35, NULL));
    TEST_ASSERT_EQUAL(35, test_ci->fd);
}

void test_bus_register_socket_should_successfully_add_SSL_socket(void)
{
    struct listener fake_listener;
    struct listener *listeners[] = {
        &fake_listener,
    };
    struct bus b = {
        .listener_count = 1,
        .listeners = listeners,
    };
    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b.fd_set_lock, NULL));
    fake_listener.bus = &b;
    test_ci = calloc(1, sizeof(*test_ci));

    SSL fake_ssl;
    bus_ssl_connect_ExpectAndReturn(&b, 35, &fake_ssl);

    struct yacht fake_yacht = { .size = 0, };
    b.fd_set = &fake_yacht;
    yacht_set_ExpectAndReturn(b.fd_set, 35, test_ci, &old_value, true);
    listener_add_socket_ExpectAndReturn(&fake_listener, test_ci, &completion_pipe, true);
    completion_pipe = 123;
    bus_poll_on_completion_ExpectAndReturn(&b, 123, true);

    TEST_ASSERT_TRUE(bus_register_socket(&b, BUS_SOCKET_SSL, 35, NULL));
    TEST_ASSERT_EQUAL(&fake_ssl, test_ci->ssl);
}

void test_bus_release_socket_should_expose_listener_remove_socket_failure(void)
{
    struct listener fake_listener;
    struct listener *listeners[] = {
        &fake_listener,
    };
    struct bus b = {
        .listener_count = 1,
        .listeners = listeners,
    };
    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b.fd_set_lock, NULL));
    fake_listener.bus = &b;

    int fd = 3;
    listener_remove_socket_ExpectAndReturn(&fake_listener, fd, &completion_pipe, false);

    void *old_udata = NULL;
    TEST_ASSERT_FALSE(bus_release_socket(&b, fd, &old_udata));
}

void test_bus_release_socket_should_expose_poll_IO_error(void)
{
    struct listener fake_listener;
    struct listener *listeners[] = {
        &fake_listener,
    };
    struct bus b = {
        .listener_count = 1,
        .listeners = listeners,
    };
    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b.fd_set_lock, NULL));
    fake_listener.bus = &b;

    int fd = 3;
    listener_remove_socket_ExpectAndReturn(&fake_listener, fd, &completion_pipe, true);
    bus_poll_on_completion_ExpectAndReturn(&b, completion_pipe, false);

    void *old_udata = NULL;
    TEST_ASSERT_FALSE(bus_release_socket(&b, fd, &old_udata));
}

void test_bus_release_socket_should_expose_hash_table_error(void)
{
    struct listener fake_listener;
    struct listener *listeners[] = {
        &fake_listener,
    };
    struct bus b = {
        .listener_count = 1,
        .listeners = listeners,
    };
    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b.fd_set_lock, NULL));
    fake_listener.bus = &b;

    int fd = 3;
    listener_remove_socket_ExpectAndReturn(&fake_listener, fd, &completion_pipe, true);
    bus_poll_on_completion_ExpectAndReturn(&b, completion_pipe, true);

    struct yacht fake_yacht = { .size = 0, };
    b.fd_set = &fake_yacht;
    yacht_remove_ExpectAndReturn(b.fd_set, fd, &old_value, false);

    void *old_udata = NULL;
    TEST_ASSERT_FALSE(bus_release_socket(&b, fd, &old_udata));
}

void test_bus_release_socket_should_expose_SSL_disconnection_failure(void)
{
    struct listener fake_listener;
    struct listener *listeners[] = {
        &fake_listener,
    };
    struct bus b = {
        .listener_count = 1,
        .listeners = listeners,
    };
    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b.fd_set_lock, NULL));
    fake_listener.bus = &b;

    int fd = 3;
    listener_remove_socket_ExpectAndReturn(&fake_listener, fd, &completion_pipe, true);
    bus_poll_on_completion_ExpectAndReturn(&b, completion_pipe, true);

    struct yacht fake_yacht = { .size = 0, };
    b.fd_set = &fake_yacht;
    yacht_remove_ExpectAndReturn(b.fd_set, fd, &old_value, true);

    SSL fake_ssl;
    test_ci = calloc(1, sizeof(connection_info));
    old_value = test_ci;
    test_ci->ssl = &fake_ssl;

    bus_ssl_disconnect_ExpectAndReturn(&b, test_ci->ssl, false);

    void *old_udata = NULL;
    TEST_ASSERT_FALSE(bus_release_socket(&b, fd, &old_udata));
}

void test_bus_release_socket_should_return_true_on_successful_socket_disconnection(void)
{
    struct listener fake_listener;
    struct listener *listeners[] = {
        &fake_listener,
    };
    struct bus b = {
        .listener_count = 1,
        .listeners = listeners,
    };
    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b.fd_set_lock, NULL));
    fake_listener.bus = &b;

    int fd = 3;
    listener_remove_socket_ExpectAndReturn(&fake_listener, fd, &completion_pipe, true);
    bus_poll_on_completion_ExpectAndReturn(&b, completion_pipe, true);

    test_ci = calloc(1, sizeof(connection_info));
    old_value = test_ci;
    test_ci->ssl = BUS_NO_SSL;

    struct yacht fake_yacht = { .size = 0, };
    b.fd_set = &fake_yacht;
    yacht_remove_ExpectAndReturn(b.fd_set, fd, &old_value, true);

    void *old_udata = NULL;
    TEST_ASSERT_TRUE(bus_release_socket(&b, fd, &old_udata));
}

void test_bus_release_socket_should_return_true_on_successful_SSL_socket_disconnection(void)
{
    struct listener fake_listener;
    struct listener fake_listener2;
    struct listener *listeners[] = {
        &fake_listener,
        &fake_listener2,
    };
    struct bus b = {
        .listener_count = 2,
        .listeners = listeners,
    };
    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b.fd_set_lock, NULL));
    fake_listener.bus = &b;

    int fd = 3;
    listener_remove_socket_ExpectAndReturn(&fake_listener2, fd, &completion_pipe, true);
    bus_poll_on_completion_ExpectAndReturn(&b, completion_pipe, true);

    struct yacht fake_yacht = { .size = 0, };
    b.fd_set = &fake_yacht;
    yacht_remove_ExpectAndReturn(b.fd_set, fd, &old_value, true);

    SSL fake_ssl;
    test_ci = calloc(1, sizeof(connection_info));
    old_value = test_ci;
    test_ci->ssl = &fake_ssl;

    bus_ssl_disconnect_ExpectAndReturn(&b, test_ci->ssl, true);

    void *old_udata = NULL;
    TEST_ASSERT_TRUE(bus_release_socket(&b, fd, &old_udata));
}

void test_bus_shutdown_should_be_idempotent(void)
{
    struct bus b = {
        .shutdown_state = SHUTDOWN_STATE_SHUTTING_DOWN,
    };
    TEST_ASSERT_FALSE(bus_shutdown(&b));

    b.shutdown_state = SHUTDOWN_STATE_HALTED;
    TEST_ASSERT_FALSE(bus_shutdown(&b));
}

void test_bus_shutdown_should_expose_listener_shut_down_failure(void)
{
    struct listener fake_listener;
    struct listener fake_listener2;
    struct listener *listeners[] = {
        &fake_listener,
        &fake_listener2,
    };
    bool joined[] = {false, false};
    struct bus b = {
        .shutdown_state = SHUTDOWN_STATE_RUNNING,
        .listener_count = 2,
        .listeners = listeners,
        .joined = joined,
    };

    struct yacht fake_yacht = { .size = 0, };
    b.fd_set = &fake_yacht;
    yacht_free_Expect(b.fd_set, free_connection_cb, &b);

    listener_shutdown_ExpectAndReturn(b.listeners[0], &completion_pipe, false);

    TEST_ASSERT_FALSE(bus_shutdown(&b));
    TEST_ASSERT_EQUAL(SHUTDOWN_STATE_RUNNING, b.shutdown_state);
}

void test_bus_shutdown_should_expose_poll_IO_error(void)
{
    struct listener fake_listener;
    struct listener fake_listener2;
    struct listener *listeners[] = {
        &fake_listener,
        &fake_listener2,
    };
    bool joined[] = {false, false};
    struct bus b = {
        .shutdown_state = SHUTDOWN_STATE_RUNNING,
        .listener_count = 2,
        .listeners = listeners,
        .joined = joined,
    };

    struct yacht fake_yacht = { .size = 0, };
    b.fd_set = &fake_yacht;
    yacht_free_Expect(b.fd_set, free_connection_cb, &b);

    listener_shutdown_ExpectAndReturn(b.listeners[0], &completion_pipe, true);
    bus_poll_on_completion_ExpectAndReturn(&b, completion_pipe, false);

    TEST_ASSERT_FALSE(bus_shutdown(&b));
    TEST_ASSERT_EQUAL(SHUTDOWN_STATE_RUNNING, b.shutdown_state);
}

void test_bus_shutdown_should_expose_pthread_join_failure(void)
{
    struct listener fake_listener;
    struct listener fake_listener2;
    struct listener *listeners[] = {
        &fake_listener,
        &fake_listener2,
    };
    bool joined[] = {false, false};
    pthread_t threads[2];
    struct bus b = {
        .shutdown_state = SHUTDOWN_STATE_RUNNING,
        .listener_count = 2,
        .listeners = listeners,
        .joined = joined,
        .threads = threads,        
    };

    struct yacht fake_yacht = { .size = 0, };
    b.fd_set = &fake_yacht;
    yacht_free_Expect(b.fd_set, free_connection_cb, &b);

    listener_shutdown_ExpectAndReturn(b.listeners[0], &completion_pipe, true);
    bus_poll_on_completion_ExpectAndReturn(&b, completion_pipe, true);
    syscall_pthread_join_ExpectAndReturn(b.threads[0], &unused, -1);

    TEST_ASSERT_FALSE(bus_shutdown(&b));
    TEST_ASSERT_EQUAL(SHUTDOWN_STATE_RUNNING, b.shutdown_state);
}

void test_bus_shutdown_should_shut_down_internal_resources(void)
{
    struct listener fake_listener;
    struct listener fake_listener2;
    struct listener *listeners[] = {
        &fake_listener,
        &fake_listener2,
    };
    bool joined[] = {false, false};
    pthread_t threads[2];
    struct bus b = {
        .shutdown_state = SHUTDOWN_STATE_RUNNING,
        .listener_count = 2,
        .listeners = listeners,
        .joined = joined,
        .threads = threads,
    };

    struct yacht fake_yacht = { .size = 0, };
    b.fd_set = &fake_yacht;
    yacht_free_Expect(b.fd_set, free_connection_cb, &b);

    for (int i = 0; i < 2; i++) {
        listener_shutdown_ExpectAndReturn(b.listeners[i], &completion_pipe, true);
        bus_poll_on_completion_ExpectAndReturn(&b, completion_pipe, true);
        syscall_pthread_join_ExpectAndReturn(b.threads[i], &unused, 0);
    }

    TEST_ASSERT_TRUE(bus_shutdown(&b));
    TEST_ASSERT_EQUAL(SHUTDOWN_STATE_HALTED, b.shutdown_state);
}

void test_bus_free_should_call_shutdown_if_not_shut_down(void)
{
    struct listener fake_listener;
    struct listener fake_listener2;

    struct listener **listeners = calloc(2, sizeof(*listeners));
    listeners[0] = &fake_listener;
    listeners[1] = &fake_listener2;

    bool *joined = calloc(2, sizeof(bool));
    pthread_t *threads = calloc(2, sizeof(*threads));
    struct bus *b = calloc(1, sizeof(*b));

    b->shutdown_state = SHUTDOWN_STATE_RUNNING;
    b->listener_count = 2;
    b->listeners = listeners;
    b->joined = joined;
    b->threads = threads;

    struct yacht fake_yacht = { .size = 0, };
    b->fd_set = &fake_yacht;
    yacht_free_Expect(b->fd_set, free_connection_cb, b);

    for (int i = 0; i < 2; i++) {
        listener_shutdown_ExpectAndReturn(b->listeners[i], &completion_pipe, true);
        bus_poll_on_completion_ExpectAndReturn(b, completion_pipe, true);
        syscall_pthread_join_ExpectAndReturn(b->threads[i], &unused, 0);
    }

    struct threadpool fake_threadpool = {
        .max_delay = 100,
    };
    b->threadpool = &fake_threadpool;

    listener_free_Expect(b->listeners[0]);
    listener_free_Expect(b->listeners[1]);
    threadpool_shutdown_ExpectAndReturn(b->threadpool, false, true);
    threadpool_free_Expect(b->threadpool);

    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b->fd_set_lock, NULL));
    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b->log_lock, NULL));
    bus_ssl_ctx_free_Expect(b);
    bus_free(b);
}

void test_bus_free_should_free_bus(void)
{
    struct listener fake_listener;
    struct listener fake_listener2;

    struct listener **listeners = calloc(2, sizeof(*listeners));
    listeners[0] = &fake_listener;
    listeners[1] = &fake_listener2;

    bool *joined = calloc(2, sizeof(bool));
    pthread_t *threads = calloc(2, sizeof(*threads));
    struct bus *b = calloc(1, sizeof(*b));

    b->shutdown_state = SHUTDOWN_STATE_HALTED;
    b->listener_count = 2;
    b->listeners = listeners;
    b->joined = joined;
    b->threads = threads;

    struct threadpool fake_threadpool = {
        .max_delay = 100,
    };
    b->threadpool = &fake_threadpool;

    listener_free_Expect(b->listeners[0]);
    listener_free_Expect(b->listeners[1]);
    threadpool_shutdown_ExpectAndReturn(b->threadpool, false, true);
    threadpool_free_Expect(b->threadpool);

    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b->fd_set_lock, NULL));
    TEST_ASSERT_EQUAL(0, pthread_mutex_init(&b->log_lock, NULL));
    bus_ssl_ctx_free_Expect(b);
    bus_free(b);
}