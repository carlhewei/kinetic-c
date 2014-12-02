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

#include "kinetic_client.h"
#include "kinetic_types_internal.h"
#include "kinetic_connection.h"
#include "kinetic_controller.h"
#include "kinetic_operation.h"
#include "kinetic_logger.h"
#include <stdlib.h>
#include <sys/time.h>

void KineticClient_Init(const char* log_file, int log_level)
{
    KineticLogger_Init(log_file, log_level);
}

void KineticClient_Shutdown(void)
{
    KineticLogger_Close();
}

KineticStatus KineticClient_Connect(const KineticSession* config,
                                    KineticSessionHandle* handle)
{
    if (handle == NULL) {
        LOG0("Session handle is NULL!");
        return KINETIC_STATUS_SESSION_EMPTY;
    }
    *handle = KINETIC_HANDLE_INVALID;

    if (config == NULL) {
        LOG0("KineticSession is NULL!");
        return KINETIC_STATUS_SESSION_EMPTY;
    }

    if (strlen(config->host) == 0) {
        LOG0("Host is empty!");
        return KINETIC_STATUS_HOST_EMPTY;
    }

    if (config->hmacKey.len < 1 || config->hmacKey.data == NULL) {
        LOG0("HMAC key is NULL or empty!");
        return KINETIC_STATUS_HMAC_EMPTY;
    }

    // Obtain a new connection/handle
    *handle = KineticConnection_NewConnection(config);
    if (*handle == KINETIC_HANDLE_INVALID) {
        LOG0("Failed connecting to device!");
        return KINETIC_STATUS_SESSION_INVALID;
    }
    KineticConnection* connection = KineticConnection_FromHandle(*handle);
    if (connection == NULL) {
        LOG0("Failed getting valid connection from handle!");
        return KINETIC_STATUS_CONNECTION_ERROR;
    }

    // Create the connection
    KineticStatus status = KineticConnection_Connect(connection);
    if (status != KINETIC_STATUS_SUCCESS) {
        LOGF0("Failed creating connection to %s:%d", config->host, config->port);
        KineticConnection_FreeConnection(handle);
        *handle = KINETIC_HANDLE_INVALID;
        return status;
    }

    // Wait for initial unsolicited status to be received in order to obtain connectionID
    while(connection->connectionID == 0) {sleep(1);}

    return status;
}

KineticStatus KineticClient_Disconnect(KineticSessionHandle* const handle)
{
    if (*handle == KINETIC_HANDLE_INVALID) {
        LOG0("Invalid KineticSessionHandle specified!");
        return KINETIC_STATUS_SESSION_INVALID;
    }
    KineticConnection* connection = KineticConnection_FromHandle(*handle);
    if (connection == NULL) {
        LOG0("Failed getting valid connection from handle!");
        return KINETIC_STATUS_CONNECTION_ERROR;
    }

    // Disconnect
    KineticStatus status = KineticConnection_Disconnect(connection);
    if (status != KINETIC_STATUS_SUCCESS) {LOG0("Disconnection failed!");}
    KineticConnection_FreeConnection(handle);
    *handle = KINETIC_HANDLE_INVALID;

    return status;
}

KineticStatus KineticClient_NoOp(KineticSessionHandle handle)
{
    assert(handle != KINETIC_HANDLE_INVALID);

    KineticOperation* operation = KineticController_CreateOperation(handle);
    if (operation == NULL) {return KINETIC_STATUS_MEMORY_ERROR;}

    KineticOperation_BuildNoop(operation);
    return KineticController_ExecuteOperation(operation, NULL);
}

KineticStatus KineticClient_Put(KineticSessionHandle handle,
                                KineticEntry* const entry,
                                KineticCompletionClosure* closure)
{
    assert(handle != KINETIC_HANDLE_INVALID);
    assert(entry != NULL);
    assert(entry->value.array.data != NULL);

    KineticOperation* operation = KineticController_CreateOperation(handle);
    if (operation == NULL) {return KINETIC_STATUS_MEMORY_ERROR;}

    // Initialize request
    KineticOperation_BuildPut(operation, entry);

    // Execute the operation
    return KineticController_ExecuteOperation(operation, closure);
}

KineticStatus KineticClient_Flush(KineticSessionHandle handle,
                                  KineticCompletionClosure* closure)
{
    assert(handle != KINETIC_HANDLE_INVALID);
    KineticOperation* operation = KineticController_CreateOperation(handle);
    if (operation == NULL) { return KINETIC_STATUS_MEMORY_ERROR; }

    // Initialize request
    KineticOperation_BuildFlush(operation);

    // Execute the operation
    return KineticController_ExecuteOperation(operation, closure);
}

static bool has_key(KineticEntry* const entry)
{
    return entry->key.array.data != NULL;
}

static bool has_value_buffer(KineticEntry* const entry)
{
    return entry->value.array.data != NULL;
}

typedef enum {
    CMD_GET,
    CMD_GET_NEXT,
    CMD_GET_PREVIOUS,
} GET_COMMAND;

static KineticStatus handle_get_command(GET_COMMAND cmd,
                                        KineticSessionHandle handle,
                                        KineticEntry* const entry,
                                        KineticCompletionClosure* closure)
{
    assert(handle != KINETIC_HANDLE_INVALID);
    assert(entry != NULL);

    if (!has_key(entry)) { return KINETIC_STATUS_MISSING_KEY; }
    if (!has_value_buffer(entry) && !entry->metadataOnly) {
        return KINETIC_STATUS_MISSING_VALUE_BUFFER;
    }

    KineticOperation* operation = KineticController_CreateOperation(handle);
    if (operation == NULL) {return KINETIC_STATUS_MEMORY_ERROR;}

    // Initialize request
    switch (cmd)
    {
    case CMD_GET:
        KineticOperation_BuildGet(operation, entry);
        break;
    case CMD_GET_NEXT:
        KineticOperation_BuildGetNext(operation, entry);
        break;
    case CMD_GET_PREVIOUS:
        KineticOperation_BuildGetPrevious(operation, entry);
        break;
    default:
        assert(false);
    }

    // Execute the operation
    return KineticController_ExecuteOperation(operation, closure);
}

KineticStatus KineticClient_Get(KineticSessionHandle handle,
                                KineticEntry* const entry,
                                KineticCompletionClosure* closure)
{
    return handle_get_command(CMD_GET, handle, entry, closure);
}

KineticStatus KineticClient_GetPrevious(KineticSessionHandle handle,
                                        KineticEntry* const entry,
                                        KineticCompletionClosure* closure)
{
    return handle_get_command(CMD_GET_PREVIOUS, handle, entry, closure);
}

KineticStatus KineticClient_GetNext(KineticSessionHandle handle,
                                    KineticEntry* const entry,
                                    KineticCompletionClosure* closure)
{
    return handle_get_command(CMD_GET_NEXT, handle, entry, closure);
}

KineticStatus KineticClient_Delete(KineticSessionHandle handle,
                                   KineticEntry* const entry,
                                   KineticCompletionClosure* closure)
{
    assert(handle != KINETIC_HANDLE_INVALID);
    assert(entry != NULL);

    KineticOperation* operation = KineticController_CreateOperation(handle);
    if (operation == NULL) {return KINETIC_STATUS_MEMORY_ERROR;}

    // Initialize request
    KineticOperation_BuildDelete(operation, entry);

    // Execute the operation
    return KineticController_ExecuteOperation(operation, closure);
}

KineticStatus KineticClient_GetKeyRange(KineticSessionHandle handle,
                                        KineticKeyRange* range,
                                        ByteBufferArray* keys,
                                        KineticCompletionClosure* closure)
{
    assert(handle != KINETIC_HANDLE_INVALID);
    assert(range != NULL);
    assert(keys != NULL);
    assert(keys->buffers != NULL);
    assert(keys->count > 0);

    KineticOperation* operation = KineticController_CreateOperation(handle);
    if (operation == NULL) {return KINETIC_STATUS_MEMORY_ERROR;}

    // Initialize request
    KineticOperation_BuildGetKeyRange(operation, range, keys);

    // Execute the operation
    return KineticController_ExecuteOperation(operation, closure);
}

KineticStatus KineticClient_GetLog(KineticSessionHandle handle,
                                   KineticDeviceInfo_Type type,
                                   KineticDeviceInfo** info,
                                   KineticCompletionClosure* closure)
{
    assert(handle != KINETIC_HANDLE_INVALID);
    assert(info != NULL);

    KineticOperation* operation = KineticController_CreateOperation(handle);
    if (operation == NULL) {return KINETIC_STATUS_MEMORY_ERROR;}

    // Initialize request
    KineticOperation_BuildGetLog(operation, type, info);

    // Execute the operation
    return KineticController_ExecuteOperation(operation, closure);
}

KineticStatus KineticClient_P2POperation(KineticSessionHandle handle,
                                         KineticP2P_Operation* const p2pOp,
                                         KineticCompletionClosure* closure)
{
    assert(handle != KINETIC_HANDLE_INVALID);
    assert(p2pOp != NULL);

    KineticOperation* operation = KineticController_CreateOperation(handle);
    if (operation == NULL) {return KINETIC_STATUS_MEMORY_ERROR;}

    // Initialize request
    KineticOperation_BuildP2POperation(operation, p2pOp);

    // Execute the operation
    return KineticController_ExecuteOperation(operation, closure);
}

KineticStatus KineticClient_InstantSecureErase(KineticSessionHandle handle)
{
    assert(handle != KINETIC_HANDLE_INVALID);

    KineticOperation* operation = KineticController_CreateOperation(handle);
    if (operation == NULL) {return KINETIC_STATUS_MEMORY_ERROR;}

    KineticOperation_BuildInstantSecureErase(operation);
    return KineticController_ExecuteOperation(operation, NULL);
}
