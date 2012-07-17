#!/usr/bin/env lem
--
-- This file is part of lem-dbus
-- Copyright 2011 Emil Renner Berthing
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

print('Entered ' .. arg[0])

local dbus  = require 'lem.dbus'

print 'Opening session bus'
local bus = assert(dbus.session())

if arg[1] == 'quit' then
	print("Calling method Quit()")

	local _, err = bus:call(
		'org.lua.TestScript',
		'/org/lua/LEM/TestObject',
		'org.lua.LEM.TestInterface',
		'Quit'
	)

	if err then	print("Got error: '"..err.."'") end
else
	local argument = arg[1] or 'Hello World!'

	print("Calling method Test('" .. argument .. "')")

	local res, err = bus:call(
		'org.lua.TestScript',
		'/org/lua/LEM/TestObject',
		'org.lua.LEM.TestInterface',
		'Test',	's', argument
	)

	if not res then
		print("Got error: '"..err.."'")
	else
		print("Got reply: '"..tostring(res).."'")
	end

end

print 'Closing session bus'
assert(bus:close())

print('Exiting ' .. arg[0])

-- vim: syntax=lua ts=2 sw=2 noet:
