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

-- Import modules
local utils = require 'lem.utils'
local dbus  = require 'lem.dbus'

-- Initialise and get handle for the session bus
print "Opening session bus"
local bus, name = dbus.session()
if not bus then error(name) end
print("Acquired unique name '" .. name .. "' on the session bus")


-- Set the connection name
print("Asking for 'org.lua.TestScript'")
if assert(bus:RequestName('org.lua.TestScript', dbus.NAME_FLAG_DO_NOT_QUEUE))
		~= dbus.REQUEST_NAME_REPLY_PRIMARY_OWNER then
	print "Couldn't get the name org.lua.TestScript."
	print "Perhaps another instance is running?"
	bus:close() -- not really needed, will be closed by the garbage collector
	utils.exit(1)
end
print "Acquired"

-- Define pretty-printing function for later use
local function stringify(t)
	for i = 1, #t do
		local ti = t[i]
		if type(ti) == 'string' then
			t[i] = ('%q'):format(ti)
		else
			t[i] = tostring(ti)
		end
	end

	return table.concat(t, ', ')
end

-- Listen for TestSignal from the object /org/lua/LEM/TestObject
-- on the interface org.lua.LEM.TestInterface.
-- As a backdoor to ending this script in a nice way use:
-- dbus-send --session --type=signal /org/lua/LEM/TestObject \
--   org.lua.LEM.TestInterface.TestSignal string:'quit'

assert(bus:registersignal(
	'/org/lua/LEM/TestObject',
	'org.lua.LEM.TestInterface',
	'TestSignal',
	function(arg1, ...)
		print("Received TestSignal(" .. stringify{arg1, ...} .. ")")
		if arg1 == 'quit' then
			print " ..exiting"
			bus:close()
		end
	end
))

-- Create object to export
local obj = dbus.newobject('/org/lua/LEM/TestObject')

-- This method takes any value(s) and returns a
-- string describing how you called it
obj:addmethod('org.lua.LEM.TestInterface', 'Test', 'v', 's',
function(...)
	local s = 'You called Test(' .. stringify{...} .. ')'

	print(s)
	return 's', s
end)

-- This method always errors with the message 'Hello World!'
do
	local e = dbus.newerror()
	obj:addmethod('org.lua.LEM.TestInterface', 'Error', '', '(ss)',
	function()
		print "Error() method called"
		return e('Hello World!')
	end)
end

-- This method shows that it is of course possible to call other
-- DBus methods within method handlers
do
	local DBus = assert(bus:autoproxy('org.freedesktop.DBus', '/org/freedesktop/DBus'))

	obj:addmethod('org.lua.LEM.TestInterface', 'ListNames', '', 'as',
	function()
		print "ListNames() method called.."
		local list = assert(DBus:ListNames())
		print " ..got list. Returning it."
		return 'as', list
	end)
end

-- This method unregisters the exported object
-- WARNING: you won't be able to call any methods on it afterwards
obj:addmethod('org.lua.LEM.TestInterface', 'Unregister', '', 's',
function()
	print "Unregistering object"
	local ok, msg = bus:unregisterobject(obj)
	if not ok then return 's', msg end

	return 's', 'ok'
end)

-- This method quits the script
obj:addmethod('org.lua.LEM.TestInterface', 'Quit', '', '',
function()
	print "Quit() method called, bye o/"
	utils.spawn(function() bus:close() end)
end)

-- Register our object
assert(bus:registerobject(obj))

-- Now listen for signals and method calls
assert(bus:listen())

-- vim: syntax=lua ts=2 sw=2 noet:
