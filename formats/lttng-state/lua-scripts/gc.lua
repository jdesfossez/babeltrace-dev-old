-- REDIS_GC
-- KEYS: hostname:session_name
-- ARGS: timestamp

local timestamp = ARGV[1]

-- keep 2 seconds of history
local max_history = 2 * 1000000000

local gc, clear_fd, clear_attr_list, clear_event, handle_exit_syscall

function clear_attr_list(key)
	local attrs = redis.call("SMEMBERS", key..":attrs")
	for k,att in pairs(attrs) do
		redis.call("DEL", key..":"..att)
	end
	redis.call("DEL", key..":attrs")
	redis.log(redis.LOG_WARNING, "  - clearing attr list "..key..":attrs")
end

function clear_fd(tid, pid, fd, subfd)
	local proc_key, fd_key, tmp
	if not pid then
		proc_key = KEYS[1]..":tids:"..tid
	else
		proc_key = KEYS[1]..":pids:"..pid
	end
	fd_key = proc_key..":fds:"..fd..":"..subfd
	redis.log(redis.LOG_WARNING, " - DO STUFF, on "..fd_key)

	local exit_syscall_open = redis.call("GET", fd_key..":created")
	local open = redis.call("GET", KEYS[1]..":events:"..exit_syscall_open..":enter_event")
	if exit_syscall_open then
		clear_event(exit_syscall_open)
	end
	if open then
		clear_event(open)
	end

	-- FIXME: clear ops related to subfd
	clear_attr_list(fd_key)
	redis.log(redis.LOG_WARNING, "  - lrem "..fd..", "..subfd)
	redis.call("LREM", proc_key..":fds:"..fd, 1, subfd)

	tmp = redis.call("LLEN", proc_key..":fds:"..fd)
	if tmp == 0 then
		redis.call("DEL", proc_key..":fds:"..fd)
		redis.call("DEL", proc_key..":fds:"..fd..":ops")
		redis.call("SREM", proc_key..":fds", fd)
	end
end

function clear_event(event)
	clear_attr_list(KEYS[1]..":events:"..event)
	redis.call("LREM", KEYS[1]..":events", 1, event)
end

function handle_exit_syscall(event)
	local oldev = redis.call("GET",
		KEYS[1]..":events:"..event..":enter_event")
	local exit_syscall_entry = redis.call("GET",
		KEYS[1]..":events:"..oldev..":event_name")
	if exit_syscall_entry == "sys_open" then
		redis.log(redis.LOG_WARNING, " - would move to archive")
	elseif exit_syscall_entry == "sys_close" then
		local subfd = redis.call("GET",
			KEYS[1]..":events:"..oldev..":subfd")
		local fd = redis.call("GET",
			KEYS[1]..":events:"..oldev..":fd")
		local tid = redis.call("GET",
			KEYS[1]..":events:"..oldev..":tid")
		local pid = redis.call("GET",
			KEYS[1]..":tids:"..tid..":pid")
		clear_fd(tid, pid, fd, subfd)
		-- delete the sys_close event
		clear_event(oldev)
		-- delete the exit_syscall from sys_close event
		clear_event(event)
	end
end

function gc(timestamp)
	local events = redis.call("LRANGE", KEYS[1]..":events", 0, 100)
	for i,j in pairs(events) do
		local oldts, oldcpu
		oldts, oldcpu = j:match("([^,]+):([^,]+)")
		redis.log(redis.LOG_WARNING, "gc "..oldts..", "..timestamp)
		if timestamp - oldts < max_history then
			return 2
		end
		local event_name = redis.call("GET",
			KEYS[1]..":events:"..j..":event_name")
		redis.log(redis.LOG_WARNING, "gc "..oldts..", "..event_name)
		if event_name == "exit_syscall" then
			handle_exit_syscall(j)
		elseif event_name == "sys_open" or event_name == "sys_close" then
			redis.log(redis.LOG_WARNING, " - would move to archive")
		end
	end
	return -1
end

return gc(timestamp)
