-- REDIS_GC
-- KEYS: hostname:session_name
-- ARGS: timestamp

local timestamp = ARGV[1]

-- keep 2 seconds of history
local max_history = 2 * 1000000000

local gc, clear_fd, clear_attr_list, clear_event, handle_exit_syscall,
	handle_process_free, clear_tid, clear_pid, clear_all_fds

function clear_attr_list(key)
	local attrs = redis.call("SMEMBERS", key..":attrs")
	for k,att in pairs(attrs) do
		redis.call("DEL", key..":"..att)
	end
	redis.call("DEL", key..":attrs")
	redis.log(redis.LOG_WARNING, "  - clearing attr list "..key..":attrs")
end

function clear_fd(proc_key, fd, subfd)
	local fd_key, tmp
	fd_key = proc_key..":fds:"..fd..":"..subfd

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
		local proc_key
		if not pid then
			proc_key = KEYS[1]..":tids:"..tid
		else
			proc_key = KEYS[1]..":pids:"..pid
		end
		clear_fd(proc_key, fd, subfd)
		-- delete the sys_close event
		clear_event(oldev)
		-- delete the exit_syscall from sys_close event
		clear_event(event)
	end
end

function clear_tid(tid_key, subtid)
	local subtid
	redis.call("LREM", tid_key, 1, subtid)
	redis.call("DEL", tid_key..":procname")
	redis.call("DEL", tid_key..":created")
	redis.call("DEL", tid_key..":pid")
	redis.call("DEL", tid_key..":terminated")
end

function clear_all_fds(proc_key)
	local fds = redis.call("SMEMBERS", proc_key..":fds")
	for f,att in pairs(fds) do
		redis.log(redis.LOG_WARNING, "LA1 "..proc_key..":fds:"..att)
		local subfds = redis.call("LRANGE", proc_key..":fds:"..att, 0, -1)
		for s,att2 in pairs(subfds) do
			redis.log(redis.LOG_WARNING, "LA2 "..att2)
			clear_fd(proc_key, att, att2)
		end
	end
end

function clear_pid(cpid, pid)
	local pid_key = KEYS[1]..":pids:"..pid

	local threads = redis.call("SMEMBERS", pid_key..":threads")
	for t,att in pairs(threads) do
		local ctid, subtid
		ctid, subtid = att:match("([^,]+):([^,]+)")
		clear_tid(KEYS[1]..":tids:"..att, subtid)
	end
	clear_all_fds(pid_key)

	redis.call("DEL", pid_key..":procname")
	redis.call("DEL", pid_key..":created")
	redis.call("DEL", pid_key..":threads")
end

function handle_process_free(event)
	local tid = redis.call("GET", KEYS[1]..":events:"..event..":tid")
	local tid_key =  KEYS[1]..":tids:"..tid
	local ctid, subtid
	ctid, subtid = tid:match("([^,]+):([^,]+)")

	local fork_event = redis.call("GET", tid_key..":created")
	local pid = redis.call("GET", tid_key..":pid")
	local cpid, subpid
	cpid, subpid = pid:match("([^,]+):([^,]+)")

	-- if it is the main thread, clear all the threads and fds
	if cpid == ctid then
		clear_pid(cpid, pid)
		redis.call("LREM", KEYS[1]..":pids:"..cpid, 1, subpid)
		local exists = redis.call("GET", KEYS[1]..":pids:"..ctid)
		if not exists then
			redis.call("SREM", KEYS[1]..":pids", cpid)
		end
	end

	clear_tid(tid_key, subtid)
	redis.call("LREM", KEYS[1]..":tids:"..ctid, 1, subtid)
	local exists = redis.call("GET", KEYS[1]..":tids:"..ctid)
	if not exists then
		redis.call("SREM", KEYS[1]..":tids", ctid)
	end

	clear_event(fork_event)
	clear_event(event)
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
		elseif event_name == "sched_process_free" then
			handle_process_free(j)
		elseif event_name == "sys_open" or event_name == "sys_close" then
			redis.log(redis.LOG_WARNING, " - would move to archive")
		end
	end
	return -1
end

return gc(timestamp)
