/*
 * This file is part of lem-dbus.
 * Copyright 2011 Emil Renner Berthing
 *
 * lem-dbus is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * lem-dbus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with lem-dbus. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ADD_H
#define _ADD_H

unsigned int
lem_dbus_add_arguments(lua_State *L, int start,
                       const char *signature, DBusMessage *msg);

#endif
