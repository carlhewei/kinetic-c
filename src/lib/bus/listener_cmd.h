/**
 * Copyright 2013-2015 Seagate Technology LLC.
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not
 * distributed with this file, You can obtain one at
 * https://mozilla.org/MP:/2.0/.
 *
 * This program is distributed in the hope that it will be useful,
 * but is provided AS-IS, WITHOUT ANY WARRANTY; including without
 * the implied warranty of MERCHANTABILITY, NON-INFRINGEMENT or
 * FITNESS FOR A PARTICULAR PURPOSE. See the Mozilla Public
 * License for more details.
 *
 * See www.openkinetic.org for more project information
 */

#ifndef LISTENER_CMD_H
#define LISTENER_CMD_H

#include "listener_internal_types.h"

/** Notify the listener's caller that a command has completed. */
void ListenerCmd_NotifyCaller(listener *l, int fd);

/** Process incoming commands, if any. */
void ListenerCmd_CheckIncomingMessages(listener *l, int *res);

#endif
