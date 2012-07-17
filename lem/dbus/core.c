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

#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include <lem.h>
#include <dbus/dbus.h>

#ifdef AMALG
#include <expat.h>

#define EXPORT static

#include "add.c"
#include "push.c"
#include "parse.c"

#else

#include "add.h"
#include "push.h"
#include "parse.h"

#endif

#if !(LUA_VERSION_NUM >= 502)
#define lua_getuservalue lua_getfenv
#define lua_setuservalue lua_setfenv
#endif

#define LEM_DBUS_BUS_OBJECT   1
#define LEM_DBUS_MESSAGE_META 2
#define LEM_DBUS_SIGNAL_TABLE 3
#define LEM_DBUS_OBJECT_TABLE 4
#define LEM_DBUS_TOP          4

struct bus_object {
	DBusConnection *conn;
};
#define bus_unbox(T, idx) (((struct bus_object *)lua_touserdata(T, idx))->conn)

struct watch {
	struct ev_io ev;
	DBusConnection *conn;
	DBusWatch *watch;
};

struct timeout {
	struct ev_timer ev;
	DBusConnection *conn;
	DBusTimeout *timeout;
};

static void
watch_handler(EV_P_ struct ev_io *ev, int revents)
{
	struct watch *w = (struct watch *)ev;
	unsigned int flags = 0;

	if (revents & EV_READ)
		flags |= DBUS_WATCH_READABLE;
	if (revents & EV_WRITE)
		flags |= DBUS_WATCH_WRITABLE;
	if (revents & EV_ERROR)
		flags |= DBUS_WATCH_ERROR;

	lem_debug("flags = %s", flags & DBUS_WATCH_ERROR ? "ERROR" :
	                        flags & DBUS_WATCH_READABLE ? "READ" : "WRITE");

	(void)dbus_watch_handle(w->watch, flags);

	if (dbus_connection_get_dispatch_status(w->conn)
	    == DBUS_DISPATCH_DATA_REMAINS) {
		while (dbus_connection_dispatch(w->conn)
		       == DBUS_DISPATCH_DATA_REMAINS);
	}
}

static void
timeout_handler(EV_P_ struct ev_timer *ev, int revents)
{
	struct timeout *t = (struct timeout *)ev;

	(void)revents;

	lem_debug("timeout");

	(void)dbus_timeout_handle(t->timeout);

	if (dbus_connection_get_dispatch_status(t->conn)
	    == DBUS_DISPATCH_DATA_REMAINS) {
		while (dbus_connection_dispatch(t->conn)
		       == DBUS_DISPATCH_DATA_REMAINS);
	}
}

static int
flags_to_revents(unsigned int flags)
{
	int revents = 0;

	if (flags & DBUS_WATCH_READABLE)
		revents |= EV_READ;
	if (flags & DBUS_WATCH_WRITABLE)
		revents |= EV_WRITE;

	return revents;
}

static dbus_bool_t
watch_add(DBusWatch *watch, void *data)
{
	struct watch *w;

	lem_debug("watch = %p, fd = %d, flags = %s, enabled = %s",
	          (void *)watch,
		  dbus_watch_get_unix_fd(watch),
	          dbus_watch_get_flags(watch) & DBUS_WATCH_READABLE ? "READ" : "WRITE",
		  dbus_watch_get_enabled(watch) ? "true" : "false");

	w = lem_xmalloc(sizeof(struct watch));
	ev_io_init(&w->ev, watch_handler, dbus_watch_get_unix_fd(watch),
	           flags_to_revents(dbus_watch_get_flags(watch)));
	w->conn = data;
	w->watch = watch;
	dbus_watch_set_data(watch, w, NULL);

	if (dbus_watch_get_enabled(watch))
		ev_io_start(LEM_ &w->ev);

	return TRUE;
}

static void
watch_remove(DBusWatch *watch, void *data)
{
	struct watch *w;

	(void)data;

	lem_debug("watch = %p, fd = %d, flags = %s, enabled = %s",
	          (void *)watch,
		  dbus_watch_get_unix_fd(watch),
	          dbus_watch_get_flags(watch) & DBUS_WATCH_READABLE ? "READ" : "WRITE",
		  dbus_watch_get_enabled(watch) ? "true" : "false");

	w = dbus_watch_get_data(watch);
	ev_io_stop(LEM_ &w->ev);
	free(w);
}

static void
watch_toggle(DBusWatch *watch, void *data)
{
	struct watch *w;

	(void)data;

	lem_debug("watch = %p, fd = %d, flags = %s, enabled = %s",
	          (void *)watch,
		  dbus_watch_get_unix_fd(watch),
	          dbus_watch_get_flags(watch) & DBUS_WATCH_READABLE ? "READ" : "WRITE",
		  dbus_watch_get_enabled(watch) ? "true" : "false");

	w = dbus_watch_get_data(watch);
	if (dbus_watch_get_enabled(watch)) {
		if (ev_is_active(&w->ev))
			ev_io_stop(LEM_ &w->ev);

		ev_io_set(LEM_ &w->ev, w->ev.fd,
		          flags_to_revents(dbus_watch_get_flags(watch)));
		ev_io_start(LEM_ &w->ev);
	} else
		ev_io_stop(LEM_ &w->ev);
}

static dbus_bool_t
timeout_add(DBusTimeout *timeout, void *data)
{
	struct timeout *t;
	ev_tstamp interval = dbus_timeout_get_interval(timeout);

	lem_debug("timeout = %p, interval = %d, enabled = %s",
	          (void *)timeout,
	          dbus_timeout_get_interval(timeout),
	          dbus_timeout_get_enabled(timeout) ? "true" : "false");

	t = lem_xmalloc(sizeof(struct timeout));
	interval = ((ev_tstamp)dbus_timeout_get_interval(timeout))/1000.0;
	ev_timer_init(&t->ev, timeout_handler, interval, interval);
	t->conn = data;
	t->timeout = timeout;

	dbus_timeout_set_data(timeout, t, NULL);

	if (dbus_timeout_get_enabled(timeout))
		ev_timer_start(LEM_ &t->ev);

	return TRUE;
}

static void
timeout_remove(DBusTimeout *timeout, void *data)
{
	struct timeout *t;

	(void)data;

	lem_debug("timeout = %p, interval = %d, enabled = %s",
	          (void *)timeout,
	          dbus_timeout_get_interval(timeout),
	          dbus_timeout_get_enabled(timeout) ? "true" : "false");

	t = dbus_timeout_get_data(timeout);
	ev_timer_stop(LEM_ &t->ev);
	free(t);
}

static void
timeout_toggle(DBusTimeout *timeout, void *data)
{
	struct timeout *t;

	(void)data;

	lem_debug("timeout = %p, interval = %d, enabled = %s",
	          (void *)timeout,
	          dbus_timeout_get_interval(timeout),
	          dbus_timeout_get_enabled(timeout) ? "true" : "false");

	t = dbus_timeout_get_data(timeout);
	if (dbus_timeout_get_enabled(timeout)) {
		ev_tstamp interval =
			((ev_tstamp)dbus_timeout_get_interval(timeout))/1000.0;

		if (ev_is_active(&t->ev))
			ev_timer_stop(LEM_ &t->ev);

		ev_timer_set(&t->ev, interval, interval);
		ev_timer_start(LEM_ &t->ev);
	} else
		ev_timer_stop(LEM_ &t->ev);
}

/*
 * Bus:signaltable()
 *
 * argument 1: bus object
 */
static int
bus_signaltable(lua_State *T)
{
	luaL_checktype(T, 1, LUA_TUSERDATA);
	if (bus_unbox(T, 1) == NULL) {
		lua_pushnil(T);
		lua_pushliteral(T, "closed");
		return 2;
	}

	lua_getuservalue(T, 1);
	lua_rawgeti(T, -1, 2);
	return 1;
}

/*
 * Bus:objecttable()
 *
 * argument 1: bus object
 */
static int
bus_objecttable(lua_State *T)
{
	luaL_checktype(T, 1, LUA_TUSERDATA);
	if (bus_unbox(T, 1) == NULL) {
		lua_pushnil(T);
		lua_pushliteral(T, "closed");
		return 2;
	}

	lua_getuservalue(T, 1);
	lua_rawgeti(T, -1, 3);
	return 1;
}

/*
 * Bus:send_signal()
 *
 * argument 1: bus object
 * argument 2: path
 * argument 3: interface
 * argument 4: name
 * argument 5: signature (optional)
 * ...
 */
static int
bus_signal(lua_State *T)
{
	DBusConnection *conn;
	const char *path;
	const char *interface;
	const char *name;
	const char *signature;
	DBusMessage *msg;

	luaL_checktype(T, 1, LUA_TUSERDATA);
	path      = luaL_checkstring(T, 2);
	interface = luaL_checkstring(T, 3);
	name      = luaL_checkstring(T, 4);
	signature = luaL_optstring(T, 5, NULL);

	conn = bus_unbox(T, 1);
	if (conn == NULL) {
		lua_pushnil(T);
		lua_pushliteral(T, "closed");
		return 2;
	}

	lem_debug("%s, %s, %s", path, interface, name);
	msg = dbus_message_new_signal(path, interface, name);
	if (msg == NULL)
		goto oom;

	if (signature && signature[0] != '\0' &&
	    lem_dbus_add_arguments(T, 6, signature, msg))
		return luaL_error(T, "%s", lua_tostring(T, -1));

	if (!dbus_connection_send(conn, msg, NULL))
		goto oom;

	dbus_message_unref(msg);
	lua_pushboolean(T, 1);
	return 1;

oom:
	if (msg)
		dbus_message_unref(msg);
	lua_pushnil(T);
	lua_pushliteral(T, "out of memory");
	return 2;
}

static void
bus_call_cb(DBusPendingCall *pending, void *data)
{
	lua_State *T = data;
	DBusMessage *msg = dbus_pending_call_steal_reply(pending);
	int nargs;

	dbus_pending_call_unref(pending);

	lem_debug("received return(%s)", dbus_message_get_signature(msg));

	if (msg == NULL) {
		lua_pushnil(T);
		lua_pushliteral(T, "null reply");
		nargs = 2;
	} else {
		switch (dbus_message_get_type(msg)) {
		case DBUS_MESSAGE_TYPE_METHOD_RETURN:
			nargs = lem_dbus_push_arguments(T, msg);
			break;

		case DBUS_MESSAGE_TYPE_ERROR:
			lua_pushnil(T);
			{
				DBusError err;

				dbus_error_init(&err);
				dbus_set_error_from_message(&err, msg);
				lua_pushstring(T, err.message);
				dbus_error_free(&err);
			}
			nargs = 2;
			break;

		default:
			lua_pushnil(T);
			lua_pushliteral(T, "unknown reply");
			nargs = 2;
		}
		dbus_message_unref(msg);
	}

	lem_queue(T, nargs);
}

/*
 * Bus:call()
 *
 * argument 1: bus object
 * argument 2: destination
 * argument 3: path
 * argument 4: interface
 * argument 5: method
 * argument 6: signature (optional)
 * ...
 */
static int
bus_call(lua_State *T)
{
	DBusConnection *conn;
	const char *destination;
	const char *path;
	const char *interface;
	const char *method;
	const char *signature;
	DBusMessage *msg;
	DBusPendingCall *pending;

	luaL_checktype(T, 1, LUA_TUSERDATA);
	destination = luaL_checkstring(T, 2);
	path        = luaL_checkstring(T, 3);
	interface   = luaL_checkstring(T, 4);
	method      = luaL_checkstring(T, 5);
	signature   = luaL_optstring(T, 6, NULL);

	conn = bus_unbox(T, 1);
	if (conn == NULL) {
		lua_pushnil(T);
		lua_pushliteral(T, "closed");
		return 2;
	}

	lem_debug("calling\n  %s\n  %s\n  %s\n  %s(%s)",
	          destination, path, interface, method,
		  signature ? signature : "");

	/* create a new method call and check for errors */
	msg = dbus_message_new_method_call(destination,
	                                   path,
	                                   interface,
	                                   method);
	if (msg == NULL)
		goto oom;

	/* add arguments if a signature was provided */
	if (signature && signature[0] != '\0' &&
	    lem_dbus_add_arguments(T, 7, signature, msg))
		return luaL_error(T, "%s", lua_tostring(T, -1));

	if (!dbus_connection_send_with_reply(conn, msg, &pending, -1))
		goto oom;

	if (!dbus_pending_call_set_notify(pending, bus_call_cb, T, NULL))
		goto oom;

	dbus_message_unref(msg);
	return lua_yield(T, 0);

oom:
	if (msg)
		dbus_message_unref(msg);
	lua_pushnil(T);
	lua_pushliteral(T, "out of memory");
	return 2;
}

static DBusHandlerResult
signal_handler(lua_State *S, DBusMessage *msg)
{
	lua_State *T;
	const char *path = dbus_message_get_path(msg);
	const char *interface = dbus_message_get_interface(msg);
	const char *member = dbus_message_get_member(msg);

	lem_debug("received signal\n  %s\n  %s\n  %s(%s)",
	          path, interface, member,
	          dbus_message_get_signature(msg));

	/* NOTE: this magic string representation of an
	 * incoming signal must match the one in the Lua code */
	lua_pushfstring(S, "%s\n%s\n%s",
	                path      ? path      : "",
	                interface ? interface : "",
	                member    ? member    : "");

	lua_rawget(S, LEM_DBUS_SIGNAL_TABLE);
	if (lua_type(S, -1) != LUA_TFUNCTION) {
		lua_settop(S, LEM_DBUS_TOP);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	/* create new thread */
	T = lem_newthread();
	lua_xmove(S, T, 1);

	lem_queue(T, lem_dbus_push_arguments(T, msg));

	return DBUS_HANDLER_RESULT_HANDLED;
}

struct message_object {
	DBusMessage *msg;
};

static int
message_gc(lua_State *T)
{
	struct message_object *m = lua_touserdata(T, 1);

	if (m->msg) {
		dbus_message_unref(m->msg);
		m->msg = NULL;
	}

	return 0;
}

static int
message_reply(lua_State *T)
{
	DBusConnection *conn = bus_unbox(T, lua_upvalueindex(1));
	struct message_object *m;
	DBusMessage *msg;
	DBusMessage *reply;

	if (conn == NULL) /* connection closed */
		return 0;

	m = lua_touserdata(T, lua_upvalueindex(2));
	msg = m->msg;
	if (msg == NULL)
		return luaL_error(T, "send reply called twice");

	m->msg = NULL;

	/* check if the method returned an error */
	if (lua_gettop(T) > 0 && lua_isnil(T, 1)) {
		const char *name = luaL_checkstring(T, 2);
		const char *message = luaL_optstring(T, 3, NULL);

		if (message && message[0] == '\0')
			message = NULL;

		reply = dbus_message_new_error(msg, name, message);
		dbus_message_unref(msg);
		if (reply == NULL)
			return 0;
	} else {
		const char *signature = luaL_optstring(T, 1, NULL);

		reply = dbus_message_new_method_return(msg);
		dbus_message_unref(msg);
		if (reply == NULL)
			return 0;

		if (signature && signature[0] != '\0' &&
		    lem_dbus_add_arguments(T, 2, signature, reply)) {
			dbus_message_unref(reply);
			/* add_arguments() pushes its own error message */
			return luaL_error(T, "%s", lua_tostring(T, -1));
		}
	}

	dbus_connection_send(conn, reply, NULL);
	dbus_message_unref(reply);
	return 0;
}

static DBusHandlerResult
method_call_handler(lua_State *S, DBusMessage *msg)
{
	lua_State *T;
	struct message_object *m;
	const char *path = dbus_message_get_path(msg);
	const char *interface = dbus_message_get_interface(msg);
	const char *member = dbus_message_get_member(msg);

	lem_debug("received call\n  %s\n  %s\n  %s(%s)",
	          path, interface, member,
		  dbus_message_get_signature(msg));

	lua_pushstring(S, path ? path : "");
	lua_rawget(S, LEM_DBUS_OBJECT_TABLE);
	if (lua_type(S, -1) != LUA_TTABLE) {
		lua_settop(S, LEM_DBUS_TOP);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	lua_pushfstring(S, "%s.%s",
	                interface ? interface : "",
	                member    ? member    : "");
	lua_rawget(S, -2);
	if (lua_type(S, -1) != LUA_TFUNCTION) {
		lua_settop(S, LEM_DBUS_TOP);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	/* create new thread */
	T = lem_newthread();
	lua_pushvalue(S, LEM_DBUS_BUS_OBJECT);
	lua_xmove(S, T, 2);
	lua_settop(S, LEM_DBUS_TOP);

	/* push the send_reply function */
	m = lua_newuserdata(T, sizeof(struct message_object));
	m->msg = msg;
	dbus_message_ref(msg);

	/* set metatable */
	lua_pushvalue(S, LEM_DBUS_MESSAGE_META);
	lua_xmove(S, T, 1);
	lua_setmetatable(T, -2);

	lua_pushcclosure(T, message_reply, 2);

	lem_queue(T, lem_dbus_push_arguments(T, msg) + 1);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
message_filter(DBusConnection *conn, DBusMessage *msg, void *data)
{
	lua_State *S = data;

	(void)conn;

	switch (dbus_message_get_type(msg)) {
	case DBUS_MESSAGE_TYPE_SIGNAL:
		return signal_handler(S, msg);
	case DBUS_MESSAGE_TYPE_METHOD_CALL:
		return method_call_handler(S, msg);
	}

	lem_debug("Hmm.. received %s message",
	          dbus_message_type_to_string(dbus_message_get_type(msg)));
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/*
 * DBus.__gc()
 *
 * argument 1: bus object
 */
static int
bus_gc(lua_State *T)
{
	DBusConnection *conn = bus_unbox(T, 1);

	lem_debug("collecting DBus connection");

	if (conn) {
		dbus_connection_close(conn);
		dbus_connection_unref(conn);
	}

	return 0;
}

/*
 * Bus:close()
 *
 * argument 1: bus object
 */
static int
bus_close(lua_State *T)
{
	struct bus_object *obj;

	luaL_checktype(T, 1, LUA_TUSERDATA);
	obj = lua_touserdata(T, 1);
	if (obj->conn == NULL) {
		lua_pushnil(T);
		lua_pushliteral(T, "closed");
		return 2;
	}

	lem_debug("closing DBus connection");

	dbus_connection_close(obj->conn);
	dbus_connection_unref(obj->conn);
	obj->conn = NULL;

	lua_getuservalue(T, 1);

	lua_pushnil(T);
	lua_rawseti(T, -2, 1);
	lua_pushnil(T);
	lua_rawseti(T, -2, 2);

	lua_pushboolean(T, 1);
	return 1;
}

/*
 * open()
 *
 * argument 1: uri to connect to
 */
static int
bus_open(lua_State *T)
{
	const char *uri;
	DBusError err;
	DBusConnection *conn;
	struct bus_object *obj;
	lua_State *S;

	uri = luaL_checkstring(T, 1);
	lem_debug("opening %s", uri);

	dbus_error_init(&err);
	conn = dbus_connection_open_private(uri, &err);

	if (dbus_error_is_set(&err)) {
		lua_pushnil(T);
		lua_pushstring(T, err.message);
		dbus_error_free(&err);
		return 2;
	}

	if (conn == NULL) {
		lua_pushnil(T);
		lua_pushliteral(T, "error opening connection");
		return 2;
	}

	dbus_connection_set_exit_on_disconnect(conn, FALSE);

	/* set watch functions */
	if (!dbus_connection_set_watch_functions(conn,
	                                         watch_add,
	                                         watch_remove,
	                                         watch_toggle,
	                                         conn, NULL)) {
		dbus_connection_close(conn);
		dbus_connection_unref(conn);
		lua_pushnil(T);
		lua_pushliteral(T, "error setting watch functions");
		return 2;
	}

	/* set timout functions */
	if (!dbus_connection_set_timeout_functions(conn,
	                                           timeout_add,
	                                           timeout_remove,
	                                           timeout_toggle,
	                                           conn, NULL)) {
		dbus_connection_close(conn);
		dbus_connection_unref(conn);
		lua_pushnil(T);
		lua_pushliteral(T, "error setting timeout functions");
		return 2;
	}

	/* create new userdata for the bus */
	obj = lua_newuserdata(T, sizeof(struct bus_object));
	obj->conn = conn;

	/* set the metatable */
	lua_pushvalue(T, lua_upvalueindex(1));
	lua_setmetatable(T, -2);

	/* create uservalue table */
	lua_createtable(T, 2, 0);

	/* create signal handler thread */
	S = lua_newthread(T);
	lua_rawseti(T, -2, 1);

	/* put a reference to bus object and
	 * message metatable on thread */
	lua_pushvalue(T, -2);
	lua_pushvalue(T, lua_upvalueindex(2));
	lua_xmove(T, S, 2);

	/* create signal handler table */
	lua_newtable(S);
	lua_pushvalue(S, -1);
	lua_xmove(S, T, 1);
	lua_rawseti(T, -2, 2);

	/* create object path table */
	lua_newtable(S);
	lua_pushvalue(S, -1);
	lua_xmove(S, T, 1);
	lua_rawseti(T, -2, 3);

	/* set uservalue table */
	lua_setuservalue(T, -2);

	/* set the message filter */
	if (!dbus_connection_add_filter(conn, message_filter, S, NULL)) {
		dbus_connection_close(conn);
		dbus_connection_unref(conn);
		lua_pushnil(T);
		lua_pushliteral(T, "out of memory");
		return 2;
	}

	/* return the bus object */
	return 1;
}

#define set_dbus_string_constant(L, name) \
	lua_pushliteral(L, #name); \
	lua_pushliteral(L, DBUS_##name); \
	lua_rawset(L, -3)
#define set_dbus_number_constant(L, name) \
	lua_pushliteral(L, #name); \
	lua_pushnumber(L, (lua_Number)DBUS_##name); \
	lua_rawset(L, -3)

int
luaopen_lem_dbus_core(lua_State *L)
{
	luaL_Reg bus_funcs[] = {
		{ "__gc",        bus_gc },
		{ "signaltable", bus_signaltable },
		{ "objecttable", bus_objecttable },
		{ "call",        bus_call },
		{ "signal",      bus_signal },
		{ "close",       bus_close },
		{ NULL,          NULL }
	};
	luaL_Reg *p;

	/* create a table for this module */
	lua_newtable(L);

	/* create the Bus metatable */
	lua_newtable(L);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	/* push the Bus metatable as upvalue 1 of open() */
	lua_pushvalue(L, -1);

	/* create metatable for message objects */
	lua_createtable(L, 0, 2);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	/* insert garbage collection function */
	lua_pushcfunction(L, message_gc);
	lua_setfield(L, -2, "__gc");

	/* insert the open() function
	 * upvalue 1: Bus metatable
	 * upvalue 2: message metatable
	 */
	lua_pushcclosure(L, bus_open, 2);
	lua_setfield(L, -3, "open");

	/* insert Bus methods */
	for (p = bus_funcs; p->name; p++) {
		lua_pushcfunction(L, p->func);
		lua_setfield(L, -2, p->name);
	}

	/* insert the Bus metatable */
	lua_setfield(L, -2, "Bus");

	/* create the Proxy metatable */
	lua_newtable(L);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	/* create the Method metatable */
	lua_newtable(L);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	/* create the Signal metatable */
	lua_newtable(L);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	/* insert the parse function */
	lua_pushvalue(L, -2); /* upvalue 1: Method */
	lua_pushvalue(L, -2); /* upvalue 2: Signal */
	lua_pushcclosure(L, lem_dbus_proxy_parse, 2);
	lua_setfield(L, -4, "parse");

	/* insert the Signal metatable */
	lua_setfield(L, -4, "Signal");

	/* insert the Method metatable */
	lua_setfield(L, -3, "Method");

	/* insert the Proxy metatable */
	lua_setfield(L, -2, "Proxy");

	/* insert constants */
	set_dbus_string_constant(L, SERVICE_DBUS);
	set_dbus_string_constant(L, PATH_DBUS);
	set_dbus_string_constant(L, INTERFACE_DBUS);
	set_dbus_string_constant(L, INTERFACE_INTROSPECTABLE);
	set_dbus_string_constant(L, INTERFACE_PROPERTIES);
	set_dbus_string_constant(L, INTERFACE_PEER);
	set_dbus_string_constant(L, INTERFACE_LOCAL);

	set_dbus_number_constant(L, NAME_FLAG_ALLOW_REPLACEMENT);
	set_dbus_number_constant(L, NAME_FLAG_REPLACE_EXISTING);
	set_dbus_number_constant(L, NAME_FLAG_DO_NOT_QUEUE);

	set_dbus_number_constant(L, REQUEST_NAME_REPLY_PRIMARY_OWNER);
	set_dbus_number_constant(L, REQUEST_NAME_REPLY_IN_QUEUE);
	set_dbus_number_constant(L, REQUEST_NAME_REPLY_EXISTS);
	set_dbus_number_constant(L, REQUEST_NAME_REPLY_ALREADY_OWNER);

	set_dbus_number_constant(L, RELEASE_NAME_REPLY_RELEASED);
	set_dbus_number_constant(L, RELEASE_NAME_REPLY_NON_EXISTENT);
	set_dbus_number_constant(L, RELEASE_NAME_REPLY_NOT_OWNER);

	set_dbus_number_constant(L, START_REPLY_SUCCESS);
	set_dbus_number_constant(L, START_REPLY_ALREADY_RUNNING);

	return 1;
}
