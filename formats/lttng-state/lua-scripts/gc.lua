-- REDIS_GC
-- KEYS: hostname:session_name
-- ARGS: timestamp

local timestamp = ARGV[1]

-- keep 2 seconds of history
local max_history = 2 * 1000000000

local gc

function gc(timestamp)
	local events = redis.call("LRANGE", KEYS[1]..":events", 0, 100)
	for i,j in pairs(events) do
		local oldts, oldcpu
		oldts, oldcpu = j:match("([^,]+):([^,]+)")
		if timestamp - oldts < max_history then
			return 0
		end
		local event_name = redis.call("GET", KEYS[1]..":events:"..j..":event_name")
		redis.log(redis.LOG_WARNING, "gc "..oldts..", "..event_name)
		if event_name == "exit_syscall" then
			local oldev = redis.call("GET", KEYS[1]..":events:"..j..":enter_event")
			local enter = redis.call("GET", KEYS[1]..":events:"..oldev..":event_name")
			redis.log(redis.LOG_WARNING, " - exit from "..enter..", "..i)
			if enter == "sys_open" then
				redis.log(redis.LOG_WARNING, " - would move to archive")
			elseif enter == "sys_close" then
				local subfd = redis.call("GET", KEYS[1]..":events:"..oldev..":subfd")
				local fd = redis.call("GET", KEYS[1]..":events:"..oldev..":fd")
				local tid = redis.call("GET", KEYS[1]..":events:"..oldev..":tid")
				local pid = redis.call("GET", KEYS[1]..":tids:"..tid..":pid")
				local fd_key
				if not pid then
					fd_key = KEYS[1]..":tids:"..tid..":fds:"..fd..":"..subfd
				else
					fd_key = KEYS[1]..":pids:"..pid..":fds:"..fd..":"..subfd
				end
				redis.log(redis.LOG_WARNING, " - DO STUFF, on "..fd_key)
			end
		elseif event_name == "sys_open" or event_name == "sys_close" then
			redis.log(redis.LOG_WARNING, " - would move to archive")
		end
	end
	return -1
end

return gc(timestamp)
