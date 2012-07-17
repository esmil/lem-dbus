--
-- This file is part of lem-dbus.
-- Copyright 2011-2012 Emil Renner Berthing
--
-- lem-dbus is free software: you can redistribute it and/or
-- modify it under the terms of the GNU General Public License as
-- published by the Free Software Foundation, either version 3 of
-- the License, or (at your option) any later version.
--
-- lem-dbus is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
-- GNU General Public License for more details.
--
-- You should have received a copy of the GNU General Public License
-- along with lem-dbus. If not, see <http://www.gnu.org/licenses/>.
--

local M = require 'lem.dbus.core'

do
	local getenv = os.getenv
	local open = M.open

	function M.session()
		local uri = getenv('DBUS_SESSION_BUS_ADDRESS')
		if not uri then
			return nil, 'environment variable DBUS_SESSION_BUS_ADDRESS undefined'
		end

		local bus, err = open(uri)
		if not bus then return nil, err end

		local name, err = bus:Hello()
		if not name then return nil, err end

		return bus, name
	end

	function M.system()
		local uri = getenv('DBUS_SYSTEM_BUS_ADDRESS')
			or 'unix:path=/var/run/dbus/system_bus_socket'

		local bus, err = open(uri)
		if not bus then return nil, err end

		local name, err = bus:Hello()
		if not name then return nil, err end

		return bus, name
	end
end

function M.newerror(name)
	if name == nil or name == '' then
		name = 'org.freedesktop.DBus.Error.Failed'
	end
	return function(message)
		if message then
			return nil, name, message
		else
			return nil, name
		end
	end
end

do
	local Method = M.Method
	local function newmethod(interface, name, signature, result)
		return setmetatable({
			interface = interface,
			name = name,
			signature = signature,
			result = result
		}, Method)
	end
	M.newmethod = newmethod

	function M.Proxy:addmethod(interface, name, signature, result)
		self[name] = newmethod(interface, name, signature, result)
	end
end

do
	local call = M.Bus.call
	function M.Method.__call(method, proxy, ...)
		return call(
			proxy.bus, proxy.target, proxy.object,
			method.interface, method.name,
			method.signature, ...)
	end

	local target, object, interface =
		M.SERVICE_DBUS, M.PATH_DBUS, M.INTERFACE_DBUS

	function M.Bus:Hello()
		return call(self, target, object, interface,
			'Hello')
	end

	function M.Bus:RequestName(name, flags)
		return call(self, target, object, interface,
			'RequestName', 'su', name, flags or 0)
	end

	function M.Bus:ReleaseName(name)
		return call(self, target, object, interface,
			'ReleaseName', 's', name)
	end

	function M.Bus:AddMatch(rule)
		return call(self, target, object, interface,
			'AddMatch', 's', rule)
	end

	function M.Bus:RemoveMatch(rule)
		return call(self, target, object, interface,
			'RemoveMatch', 's', rule)
	end
end

do
	local setmetatable = setmetatable
	local Proxy = M.Proxy

	local function newproxy(bus, target, object)
		return setmetatable({
			bus = bus,
			target = target,
			object = object
		}, Proxy)
	end
	M.Bus.newproxy = newproxy

	local Introspect = M.newmethod(M.INTERFACE_INTROSPECTABLE, 'Introspect')
	M.Introspect = Introspect

	function M.Bus:autoproxy(target, object)
		local proxy = newproxy(self, target, object)

		local r, err = Introspect(proxy)
		if not r then return nil, err end

		local r, err = proxy:parse(r)
		if not r then return nil, err end

		return proxy
	end
end

do
	local assert, getmetatable, type = assert, getmetatable, type
	local format = string.format
	local Bus = M.Bus

	function Bus:registersignal(object, interface, name, f)
		assert(getmetatable(self) == Bus,
			'bad argument #1 (expected a DBus connection)')
		if type(object) == 'table' then
			local t = object
			f = interface
			object = t.object
			interface = t.interface
			name = t.name
		end
		assert(type(object) == 'string',
			'bad argument #2 (string expected, got '..type(object))
		assert(type(interface) == 'string',
			'bad argument #3 (string expected, got '..type(interface))
		assert(type(name) == 'string',
			'bad argument #4 (string expected, got '..type(name))
		assert(type(f) == 'function',
			'bad argument #5 (function expected, got '..type(f))

		local t, err = self:signaltable()
		if not t then return nil, err end

		-- this magic string representation of an incoming
		-- signal must match the one in the C code
		local s = format('%s\n%s\n%s', object, interface, name)

		if t[s] == nil then
			local r, err = self:AddMatch(
				format("type='signal',path='%s',interface='%s',member='%s'",
					object, interface, name))
			if err then return nil, err end
		end

		t[s] = f

		return true
	end
end

do
	local assert, getmetatable, type = assert, getmetatable, type
	local format = string.format
	local Bus = M.Bus

	function Bus:unregistersignal(object, interface, name)
		assert(getmetatable(self) == Bus,
			'bad argument #1 (expected a DBus connection)')
		if type(object) == 'table' then
			local t = object
			object = t.object
			interface = t.interface
			name = t.name
		end
		assert(type(object) == 'string',
			'bad argument #2 (string expected, got '..type(object))
		assert(type(interface) == 'string',
			'bad argument #3 (string expected, got '..type(interface))
		assert(type(name) == 'string',
			'bad argument #4 (string expected, got '..type(name))

		local t, err = self:signaltable()
		if not t then return nil, err end

		-- this magic string representation of an incoming
		-- signal must match the one in the C code
		local s = format('%s\n%s\n%s', object, interface, name)

		assert(t[s] ~= nil, 'signal not set')

		local r, err = self:RemoveMatch(
			format("type='signal',path='%s',interface='%s',member='%s'",
				object, interface, name))
		if err then return nil, err end

		t[s] = nil

		return true
	end
end

do
	local assert, getmetatable = assert, getmetatable
	local pairs, concat = pairs, table.concat

	local Object = {}
	Object.__index = Object

	M.Object = Object

	local function set_root_introspect(objects)
		local t, l = { '<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN" "http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">\n<node>'}, 1
		local function write(s, ...)
			if s == nil then return end
			l = l+1
			t[l] = s
			return write(...)
		end

		n = 0
		for path, _ in pairs(objects) do
			path = path:match('/*(.*)')
			if path ~= '' then
				n = n+1
				write('<node name="', path, '" />')
			end
		end

		if n == 0 then
			objects['/'] = nil
			return
		end

		write('</node>')

		xml = concat(t)
		local function introspect(reply)
			return reply('s', xml)
		end

		local methods = objects['/']
		if not methods then
			objects['/'] = {
				['org.freedesktop.DBus.Introspectable.Introspect'] = introspect
			}
		else
			methods['org.freedesktop.DBus.Introspectable.Introspect'] = introspect
		end
	end

	function M.Bus:registerobject(obj)
		assert(getmetatable(obj) == Object, 'bad argument #2 (expected an Object)')
		local objects, err = self:objecttable()
		if not objects then return nil, err end
		objects[obj.path] = obj.lookup
		set_root_introspect(objects)
		return true
	end

	function M.Bus:unregisterobject(obj)
		assert(getmetatable(obj) == Object, 'bad argument #2 (expected an Object)')
		local objects, err = self:objecttable()
		if not objects then return nil, err end
		objects[obj.path] = nil
		set_root_introspect(objects)
		return true
	end

	local sub, concat = string.sub, table.concat

	local function value_end(i, sig)
		local char = sub(sig, i, i)
		if char == 'a' then
			return value_end(i+1, sig)
		elseif char == '(' then
			i = i+1
			local n = #sig -- don't loop forever on broken signature
			repeat
				i = value_end(i, sig) + 1
			until i > n or sub(sig, i, i) == ')'
		elseif char == '{' then
			i = value_end(i+1, sig)
			i = value_end(i+1, sig) + 1
		end

		return i
	end

	local function method_xml(name, in_sig, out_sig)
		local t, l = { '<method name="', name, '">' }, 3

		local function write(s, ...)
			if s == nil then return end
			l = l+1
			t[l] = s
			return write(...)
		end

		local i, n = 1, #in_sig
		while i <= n do
			local j = value_end(i, in_sig)
			write('<arg direction="in" type="', sub(in_sig, i, j), '" />')
			i = j+1
		end
		i, n = 1, #out_sig
		while i <= n do
			local j = value_end(i, out_sig)
			write('<arg direction="out" type="', sub(out_sig, i, j), '" />')
			i = j+1
		end
		write('</method>')

		return concat(t)
	end

	function Object:addmethod(interface, name, in_sig, out_sig, f)
		if not in_sig  then in_sig  = '' end
		if not out_sig then out_sig = '' end

		self.lookup[interface..'.'..name] = function(reply, ...) reply(f(...)) end
		self.xml = nil

		local xml = method_xml(name, in_sig, out_sig)
		local interfaces = self.interfaces
		local methods = interfaces[interface]
		if methods then
			methods[name] = xml
		else
			interfaces[interface] = { [name] = xml }
		end
	end

	local pairs = pairs

	local function generate_xml(interfaces)
		local t, l = { '<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN" "http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">\n<node>'}, 1

		local function write(s, ...)
			if s == nil then return end
			l = l+1
			t[l] = s
			return write(...)
		end

		for interface, methods in pairs(interfaces) do
			write('<interface name="', interface, '">')
			for _, xml in pairs(methods) do
				write(xml)
			end
			write('</interface>')
		end

		write('</node>')

		return concat(t)
	end

	local setmetatable = setmetatable

	function M.newobject(path)
		assert(path and path ~= '' and sub(path, 1, 1) == '/', 'illegal object path')
		local t
		t = {
			path = path,
			lookup = {
				['org.freedesktop.DBus.Introspectable.Introspect'] = function(reply)
					local xml = t.xml
					if xml == nil then
						xml = generate_xml(t.interfaces)
						t.xml = xml
					end

					return reply('s', xml)
				end
			},
			interfaces = {
				['org.freedesktop.DBus.Introspectable'] = {
					['Introspect'] = '<method name="Introspect"><arg name="xml" direction="out" type="s" /></method>'
				}
			}
		}
		return setmetatable(t, Object)
	end
end

return M

-- vim: syntax=lua ts=2 sw=2 noet:
