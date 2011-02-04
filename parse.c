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

#ifndef ALLINONE
#include <string.h>
#include <lem.h>
#include <expat.h>

#define EXPORT
#endif

/*
 * Maximum length of a signature string, see
 * http://dbus.freedesktop.org/doc/dbus-specification.html
 */
#define SIG_MAXLENGTH 256

struct parsedata {
	lua_State *L;
	unsigned int level;
	unsigned int interface;
	enum {
		TAG_NONE   = 0,
		TAG_METHOD = 1,
		TAG_SIGNAL = 2
	} type;
	char signature[SIG_MAXLENGTH];
	char *sig_next;
	char result[SIG_MAXLENGTH];
	char *res_next;
};

static void
start_element_handler(void *data,
                      const XML_Char *name, const XML_Char **atts)
{
	struct parsedata *pd = data;

	pd->level++;

	switch (pd->level) {
	case 2:
		if (strcmp(name, "interface"))
			return;

		if (!*atts)
			return;
		while (strcmp(*atts, "name")) {
			atts += 2;
			if (!*atts)
				return;
		}

		/* push the interface name */
		atts++;
		lua_pushstring(pd->L, *atts);

		pd->interface = 1;
		break;

	case 3:
		if (!pd->interface)
			return;
		if (!strcmp(name, "method"))
			pd->type = TAG_METHOD;
		else if (!strcmp(name, "signal"))
			pd->type = TAG_SIGNAL;
		else
			return;

		if (!*atts)
			return;
		while (strcmp(*atts, "name")) {
			atts += 2;
			if (!*atts)
				return;
		}

		/* push the method name */
		atts++;
		lua_pushstring(pd->L, *atts);

		/* check if the field is already set */
		lua_pushvalue(pd->L, 5);
		lua_gettable(pd->L, 1);
		if (!lua_isnil(pd->L, 6)) {
			/* if it is, don't add this method/signal */
			lua_settop(pd->L, 4);
			pd->type = TAG_NONE;
			return;
		}
		lua_settop(pd->L, 5);

		/* create a new method/signal table */
		lua_createtable(pd->L, 0, 4);

		/* ..and set the metatable */
		lua_pushvalue(pd->L, lua_upvalueindex(pd->type));
		lua_setmetatable(pd->L, 6);

		break;

	case 4:
		if (pd->type == TAG_NONE || strcmp(name, "arg"))
			return;

		{
			unsigned int out = 0;
			const char *type = NULL;

			while (*atts) {
				if (!strcmp(*atts, "type")) {
					atts++;
					type = *atts;
					atts++;
				} else if (!strcmp(*atts, "direction")) {
					atts++;
					if (strcmp(*atts, "in"))
						out = 1;
					atts++;
				} else
					atts += 2;
			}

			if (!type)
				return;

			if (out) {
				while (*type)
					*pd->res_next++ = *type++;
			} else {
				while (*type)
					*pd->sig_next++ = *type++;
			}
		}
	}
}

static void
end_element_handler(void *data, const XML_Char *name)
{
	struct parsedata *pd = data;

	pd->level--;

	switch (pd->level) {
	case 1:
		if (!pd->interface || strcmp(name, "interface"))
			return;

		lua_settop(pd->L, 3);

		pd->interface = 0;
		break;

	case 2:
		if (pd->type == TAG_NONE)
			return;

		*pd->sig_next = *pd->res_next = '\0';

		lua_pushvalue(pd->L, 5); /* method/signal name */
		lua_setfield(pd->L, 6, "name");
		lua_pushvalue(pd->L, 4); /* interface */
		lua_setfield(pd->L, 6, "interface");
		lua_pushlstring(pd->L, pd->signature,
				pd->sig_next - pd->signature);
		lua_setfield(pd->L, 6, "signature");

		switch (pd->type) {
		case TAG_METHOD:
			lua_pushlstring(pd->L, pd->result,
					pd->res_next - pd->result);
			lua_setfield(pd->L, 6, "result");
			break;
		default: /* TAG_SIGNAL */
			lua_pushvalue(pd->L, 3); /* object name */
			lua_setfield(pd->L, 6, "object");
			break;
		}

		lua_settable(pd->L, 1);
		pd->res_next = pd->result;
		pd->sig_next = pd->signature;
		pd->type = TAG_NONE;
	}
}

/*
 * Proxy:parse()
 *
 * upvalue 1: Method
 * upvalue 2: Signal
 *
 * argument 1: proxy
 * argument 2: xml string
 */
EXPORT int
lem_dbus_proxy_parse(lua_State *L)
{
	XML_Parser p;
	struct parsedata pd;
	const char *xml;

	/* drop extra arguments */
	lua_settop(L, 2);

	/* get the xml string */
	xml = luaL_checkstring(L, 2);

	/* put the object name on the stack */
	lua_getfield(L, 1, "object");
	if (lua_isnil(L, 3))
		return luaL_argerror(L, 2, "no object set in the proxy");

	/* create parser and initialise it */
	p = XML_ParserCreate("UTF-8");
	if (!p) {
		lua_pushnil(L);
		lua_pushliteral(L, "out of memory");
		return 2;
	}

	pd.L = L;
	pd.level = 0;
	pd.interface = 0;
	pd.type = 0;
	pd.signature[0] = '\0';
	pd.result[0] = '\0';
	pd.sig_next = pd.signature;
	pd.res_next = pd.result;

	XML_SetUserData(p, &pd);
	XML_SetElementHandler(p, start_element_handler,
	                      end_element_handler);

	/* now parse the xml document inserting methods as we go */
	if (!XML_Parse(p, xml, strlen(xml), 1)) {
		lem_debug("parse error at line %d:\n%s\n",
		          (int)XML_GetCurrentLineNumber(p),
		          XML_ErrorString(XML_GetErrorCode(p)));
		lua_pushnil(L);
		lua_pushliteral(L, "error parsing introspection data");
		return 2;
	}

	/* free the parser */
	XML_ParserFree(p);

	/* return true */
	lua_pushboolean(L, 1);
	return 1;
}
