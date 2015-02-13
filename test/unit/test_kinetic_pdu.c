/*
* kinetic-c
* Copyright (C) 2014 Seagate Technology.
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
#include "unity_helper.h"
#include "kinetic_types.h"
#include "kinetic_types_internal.h"
#include "kinetic_pdu.h"
#include "kinetic_nbo.h"
#include "kinetic_proto.h"
#include "kinetic_logger.h"
#include "mock_kinetic_session.h"
#include "mock_kinetic_message.h"
#include "mock_kinetic_socket.h"
#include "mock_kinetic_hmac.h"
#include "mock_kinetic_controller.h"
#include "mock_kinetic_allocator.h"
#include "mock_kinetic_pdu_unpack.h"
#include "mock_bus.h"
#include "byte_array.h"
#include "protobuf-c/protobuf-c.h"

#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>

static KineticPDU PDU;
static KineticConnection Connection;
static KineticResponse Response;
static KineticSession Session;
static ByteArray Key;
static uint8_t ValueBuffer[KINETIC_OBJ_SIZE];
static ByteArray Value = {.data = ValueBuffer, .len = sizeof(ValueBuffer)};

#define SI_BUF_SIZE (sizeof(socket_info) + 2 * PDU_PROTO_MAX_LEN)
static uint8_t si_buf[SI_BUF_SIZE];

void setUp(void)
{
    KineticLogger_Init("stdout", 3);

    // Create and configure a new Kinetic protocol instance
    Key = ByteArray_CreateWithCString("some valid HMAC key...");
    Session = (KineticSession) {
        .config = (KineticSessionConfig) {
            .port = 1234,
            .host = "valid-host.com",
            .hmacKey = (ByteArray) {.data = &Session.config.keyData[0], .len = Key.len},
        }
    };
    memcpy(Session.config.hmacKey.data, Key.data, Key.len);

    memset(&Connection, 0, sizeof(Connection));
    Connection.connected = true;
    Connection.socket = 456;
    Connection.pSession = &Session;

    KineticPDU_Init(&PDU, &Connection);
    ByteArray_FillWithDummyData(Value);

    memset(si_buf, 0, SI_BUF_SIZE);
}

void tearDown(void)
{
    memset(&Response, 0, sizeof(Response));
    KineticLogger_Close();
}

void test_KineticPDUHeader_should_have_correct_byte_packed_size(void)
{
    TEST_ASSERT_EQUAL(1 + 4 + 4, PDU_HEADER_LEN);
    TEST_ASSERT_EQUAL(PDU_HEADER_LEN, sizeof(KineticPDUHeader));
}

void test_KineteicPDU_PDU_PROTO_MAX_LEN_should_be_1MB(void)
{
    TEST_ASSERT_EQUAL(1024 * 1024, PDU_PROTO_MAX_LEN);
}

void test_KineteicPDU_KINETIC_OBJ_SIZE_should_be_1MB(void)
{
    TEST_ASSERT_EQUAL(1024 * 1024, KINETIC_OBJ_SIZE);
}

void test_KineticPDU_KINETIC_OBJ_SIZE_should_be_the_sum_of_header_protobuf_and_value_max_lengths(void)
{
    TEST_ASSERT_EQUAL(PDU_HEADER_LEN + PDU_PROTO_MAX_LEN + KINETIC_OBJ_SIZE, PDU_MAX_LEN);
}

void test_KineticPDU_GetKeyValue_should_return_NULL_if_message_has_no_KeyValue(void)
{

    KineticProto_Command Command;
    memset(&Command, 0, sizeof(Command));
    KineticProto_Command_KeyValue* keyValue = NULL;

    keyValue = KineticPDU_GetKeyValue(NULL);
    TEST_ASSERT_NULL(keyValue);

    Response.command = NULL;
    keyValue = KineticPDU_GetKeyValue(&Response);
    TEST_ASSERT_NULL(keyValue);

    Response.command = &Command;
    Command.body = NULL;
    keyValue = KineticPDU_GetKeyValue(&Response);
    TEST_ASSERT_NULL(keyValue);
}


void test_KineticPDU_GetKeyRange_should_return_the_KineticProto_Command_Range_from_the_message_if_avaliable(void)
{
    KineticPDU_InitWithCommand(&PDU, &Connection);
    KineticProto_Command_Range* range = NULL;

    range = KineticPDU_GetKeyRange(NULL);
    TEST_ASSERT_NULL(range);

    range = KineticPDU_GetKeyRange(&Response);
    TEST_ASSERT_NULL(range);
    
    KineticProto_Message Message;
    memset(&Message, 0, sizeof(Message));
    Response.proto = &Message;
    range = KineticPDU_GetKeyRange(&Response);
    TEST_ASSERT_NULL(range);

    KineticProto_Command Command;
    memset(&Command, 0, sizeof(Command));
    Response.command = &Command;
    range = KineticPDU_GetKeyRange(&Response);
    TEST_ASSERT_NULL(range);

    KineticProto_Command_Body Body;
    memset(&Body, 0, sizeof(Body));
    Response.command->body = &Body;

    KineticProto_Command_Range Range;
    memset(&Range, 0, sizeof(Range));
    Body.range = &Range;
    range = KineticPDU_GetKeyRange(&Response);
    TEST_ASSERT_EQUAL_PTR(&Range, range);
}

bool unpack_header(uint8_t const * const read_buf,
    size_t const read_size, KineticPDUHeader * const header);

void test_unpack_header_should_fail_if_the_header_is_the_wrong_size(void)
{
    KineticPDUHeader header = {0};
    KineticPDUHeader header_out = {0};
    TEST_ASSERT_FALSE(unpack_header((uint8_t *)&header, sizeof(header) - 1, &header_out));
    TEST_ASSERT_FALSE(unpack_header((uint8_t *)&header, sizeof(header) + 1, &header_out));
}

void test_unpack_header_should_reject_header_with_excessively_large_sizes(void)
{
    KineticPDUHeader header = {0};
    KineticPDUHeader header_out = {0};

    header.protobufLength = PDU_PROTO_MAX_LEN + 1;
    TEST_ASSERT_FALSE(unpack_header((uint8_t *)&header, sizeof(header), &header_out));

    header.protobufLength = PDU_PROTO_MAX_LEN;
    header.valueLength = PDU_PROTO_MAX_LEN + 1;
    TEST_ASSERT_FALSE(unpack_header((uint8_t *)&header, sizeof(header), &header_out));
}

void test_unpack_header_should_unpack_header_fields_from_read_buf(void)
{
    uint8_t read_buf[] = {
        0xa0,                       // version prefix
        0x00, 0x01, 0x23, 0x45,     // protobuf length
        0x00, 0x02, 0x34, 0x56,     // value length
    };
    KineticPDUHeader header_out = {0};

    TEST_ASSERT(unpack_header(read_buf, sizeof(header_out), &header_out));
    TEST_ASSERT_EQUAL(0xa0, header_out.versionPrefix);
    TEST_ASSERT_EQUAL(0x12345, header_out.protobufLength);
    TEST_ASSERT_EQUAL(0x23456, header_out.valueLength);
}

bus_sink_cb_res_t sink_cb(uint8_t *read_buf,
    size_t read_size, void *socket_udata);

void test_sink_cb_should_reset_uninitialized_socket_state(void)
{
    socket_info *si = (socket_info *)si_buf;
    *si = (socket_info){
        .state = STATE_UNINIT,
        .accumulated = 0xFFFFFFFF,
    };
    KineticConnection connection = { .si = si };
    
    bus_sink_cb_res_t res = sink_cb(NULL, 0, &connection);

    TEST_ASSERT_EQUAL(0, si->accumulated);
    TEST_ASSERT_EQUAL(UNPACK_ERROR_UNDEFINED, si->unpack_status);

    /* Expect next read to be a header. */
    TEST_ASSERT_EQUAL(sizeof(KineticPDUHeader), res.next_read);
    TEST_ASSERT_EQUAL(NULL, res.full_msg_buffer);
    TEST_ASSERT_EQUAL(STATE_AWAITING_HEADER, si->state);
}

void test_sink_cb_should_expose_invalid_header_error(void)
{
    socket_info *si = (socket_info *)si_buf;
    *si = (socket_info){
        .state = STATE_AWAITING_HEADER,
        .accumulated = 0,
    };
    KineticConnection connection = { .si = si };
    KineticPDUHeader bad_header;
    memset(&bad_header, 0xFF, sizeof(bad_header));

    bus_sink_cb_res_t res = sink_cb((uint8_t *)&bad_header, sizeof(bad_header), &connection);
    TEST_ASSERT_EQUAL(sizeof(KineticPDUHeader), res.next_read);
    TEST_ASSERT_EQUAL(UNPACK_ERROR_INVALID_HEADER, si->unpack_status);
    TEST_ASSERT_EQUAL(si, res.full_msg_buffer);
    
    TEST_ASSERT_EQUAL(0, si->accumulated);
    TEST_ASSERT_EQUAL(STATE_AWAITING_HEADER, si->state);
}

void test_sink_cb_should_transition_to_awaiting_body_state_with_good_header(void)
{
    socket_info *si = (socket_info *)si_buf;
    *si = (socket_info){
        .state = STATE_AWAITING_HEADER,
        .accumulated = 0,
    };
    KineticConnection connection = { .si = si };
    uint8_t read_buf[] = {
        0xa0,                       // version prefix
        0x00, 0x00, 0x00, 0x7b,     // protobuf length
        0x00, 0x00, 0x01, 0xc8,     // value length
    };

    bus_sink_cb_res_t res = sink_cb(read_buf, sizeof(read_buf), &connection);
    TEST_ASSERT_EQUAL(123 + 456, res.next_read);
    TEST_ASSERT_EQUAL(NULL, res.full_msg_buffer);
    TEST_ASSERT_EQUAL(STATE_AWAITING_BODY, si->state);
    TEST_ASSERT_EQUAL(0, si->accumulated);
    TEST_ASSERT_EQUAL(UNPACK_ERROR_SUCCESS, si->unpack_status);
}

void test_sink_cb_should_accumulate_partially_recieved_header(void)
{
    socket_info *si = (socket_info *)si_buf;
    *si = (socket_info){
        .state = STATE_AWAITING_HEADER,
        .accumulated = 0,
    };
    KineticConnection connection = { .si = si };
    uint8_t read_buf1[] = {
        0xa0,                       // version prefix
        0x00, 0x00, 0x00, 0x7b,     // protobuf length
    };
    uint8_t read_buf2[] = {
        0x00, 0x00, 0x01, 0xc8,     // value length
    };

    bus_sink_cb_res_t res = sink_cb(read_buf1, sizeof(read_buf1), &connection);
    TEST_ASSERT_EQUAL(4, res.next_read);
    TEST_ASSERT_EQUAL(STATE_AWAITING_HEADER, si->state);
    TEST_ASSERT_EQUAL(5, si->accumulated);
    res = sink_cb(read_buf2, sizeof(read_buf2), &connection);
    TEST_ASSERT_EQUAL(123 + 456, res.next_read);
    TEST_ASSERT_EQUAL(NULL, res.full_msg_buffer);
    TEST_ASSERT_EQUAL(STATE_AWAITING_BODY, si->state);
    TEST_ASSERT_EQUAL(0, si->accumulated);
    TEST_ASSERT_EQUAL(UNPACK_ERROR_SUCCESS, si->unpack_status);
}

void test_sink_cb_should_accumulate_partially_received_body(void)
{
    /* Include trailing memory for si's .buf[]. */
    socket_info *si = (socket_info *)si_buf;

    si->state = STATE_AWAITING_BODY;
    si->header.protobufLength = 0x01;
    si->header.valueLength = 0x02;

    KineticConnection connection = { .si = si };
    uint8_t buf[] = {0xaa, 0xbb};

    bus_sink_cb_res_t res = sink_cb((uint8_t *)&buf, sizeof(buf), &connection);
    TEST_ASSERT_EQUAL(STATE_AWAITING_BODY, si->state);
    TEST_ASSERT_EQUAL(2, si->accumulated);
    TEST_ASSERT_EQUAL(1, res.next_read);
    TEST_ASSERT_EQUAL(NULL, res.full_msg_buffer);
}

void test_sink_cb_should_yield_fully_received_body(void)
{
    socket_info *si = (socket_info *)si_buf;
    si->state = STATE_AWAITING_BODY;
    si->header.protobufLength = 0x01;
    si->header.valueLength = 0x02;

    KineticConnection connection = { .si = si };
    uint8_t buf[] = {0xaa, 0xbb, 0xcc};

    bus_sink_cb_res_t res = sink_cb((uint8_t *)&buf, sizeof(buf), &connection);
    TEST_ASSERT_EQUAL(STATE_AWAITING_HEADER, si->state);
    TEST_ASSERT_EQUAL(sizeof(KineticPDUHeader), res.next_read);
    TEST_ASSERT_EQUAL(si, res.full_msg_buffer);
}

bus_unpack_cb_res_t unpack_cb(void *msg, void *socket_udata);

void test_unpack_cb_should_expose_error_codes(void)
{
    KineticConnection con = {.socket = 123};
    socket_info *si = (socket_info *)si_buf;
    *si = (socket_info){
        .state = STATE_AWAITING_HEADER,
        .accumulated = 0,
        .unpack_status = UNPACK_ERROR_UNDEFINED,
    };
    
    enum unpack_error error_states[] = {
        UNPACK_ERROR_UNDEFINED,
        UNPACK_ERROR_INVALID_HEADER,
        UNPACK_ERROR_PAYLOAD_MALLOC_FAIL,
    };

    for (size_t i = 0; i < sizeof(error_states) / sizeof(error_states[0]); i++) {
        si->unpack_status = error_states[i];
        bus_unpack_cb_res_t res = unpack_cb((void *)si, &con);
        TEST_ASSERT_FALSE(res.ok);
        TEST_ASSERT_EQUAL(error_states[i], res.u.error.opaque_error_id);
    }
}

void test_unpack_cb_should_expose_alloc_failure(void)
{
    KineticConnection con = {.socket = 123};
    socket_info si = {
        .state = STATE_AWAITING_HEADER,
        .accumulated = 0,
        .unpack_status = UNPACK_ERROR_SUCCESS,
        .header = {
            .valueLength = 8,
        },
    };
    
    KineticAllocator_NewKineticResponse_ExpectAndReturn(8, NULL);
    bus_unpack_cb_res_t res = unpack_cb((void*)&si, &con);
    TEST_ASSERT_FALSE(res.ok);
    TEST_ASSERT_EQUAL(UNPACK_ERROR_PAYLOAD_MALLOC_FAIL, res.u.error.opaque_error_id);
}

void test_unpack_cb_should_skip_empty_commands(void)
{
    /* Include trailing memory for si's .buf[]. */
    KineticConnection con = {.socket = 123};
    socket_info *si = (socket_info *)si_buf;
    si->state = STATE_AWAITING_HEADER;
    si->unpack_status = UNPACK_ERROR_SUCCESS,
    si->header.protobufLength = 0x01;
    si->header.valueLength = 0x01;
    si->header.protobufLength = 0x01;
    si->buf[0] = 0x00;
    si->buf[1] = 0xee;

    uint8_t response_buf[sizeof(KineticResponse) + 128];
    memset(response_buf, 0, sizeof(response_buf));
    KineticResponse *response = (KineticResponse *)response_buf;

    KineticAllocator_NewKineticResponse_ExpectAndReturn(1, response);

    KineticProto_Message Proto;
    memset(&Proto, 0, sizeof(Proto));
    Proto.has_commandBytes = false;

    KineticPDU_unpack_message_ExpectAndReturn(NULL, si->header.protobufLength,
        si->buf, &Proto);

    bus_unpack_cb_res_t res = unpack_cb(si, &con);

    TEST_ASSERT_EQUAL(0xee, response->value[0]);

    TEST_ASSERT(res.ok);
    TEST_ASSERT_EQUAL(response, res.u.success.msg);
}

void test_unpack_cb_should_unpack_command_bytes(void)
{
    KineticConnection con = {.socket = 123};
    socket_info *si = (socket_info *)si_buf;
    si->state = STATE_AWAITING_HEADER;
    si->unpack_status = UNPACK_ERROR_SUCCESS,
    si->header.protobufLength = 0x01;
    si->header.valueLength = 0x08;
    si->header.protobufLength = 0x02;
    si->buf[0] = 0x00;
    si->buf[1] = 0x01;
    si->buf[2] = 0xee;

    uint8_t response_buf[sizeof(KineticResponse) + 1];
    memset(response_buf, 0, sizeof(response_buf));
    KineticResponse *response = (KineticResponse *)response_buf;

    KineticAllocator_NewKineticResponse_ExpectAndReturn(8, response);

    KineticProto_Message Proto;
    memset(&Proto, 0, sizeof(Proto));
    Proto.has_commandBytes = true;
    Proto.commandBytes.data = (uint8_t *)"data";
    Proto.commandBytes.len = 4;

    KineticPDU_unpack_message_ExpectAndReturn(NULL, si->header.protobufLength,
        si->buf, &Proto);

    KineticProto_Command Command;
    memset(&Command, 0, sizeof(Command));
    KineticProto_Command_Header Header;
    memset(&Header, 0, sizeof(Header));
    Command.header = &Header;
    response->header.valueLength = 1;
    Header.ackSequence = 0x12345678;

    KineticPDU_unpack_command_ExpectAndReturn(NULL, Proto.commandBytes.len,
        Proto.commandBytes.data, &Command);

    bus_unpack_cb_res_t res = unpack_cb(si, &con);

    TEST_ASSERT_EQUAL(0xee, response->value[0]);

    TEST_ASSERT(res.ok);
    TEST_ASSERT_EQUAL(response, res.u.success.msg);
    TEST_ASSERT_EQUAL(0x12345678, res.u.success.seq_id);
}
