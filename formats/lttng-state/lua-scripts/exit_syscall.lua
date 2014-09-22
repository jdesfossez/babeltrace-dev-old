-- REDIS_EXIT_SYSCALL
-- KEYS: hostname:session_name
-- ARGS: timestamp, cpu_id, ret

local timestamp = ARGV[1]
local cpu_id = ARGV[2]
local ret = ARGV[3]

-- function forward definitions
local do_sys_open, do_sys_close

function do_sys_open(t, ret, event, timestamp, cpu_id)
	-- we could track open errors here
	if tonumber(ret) < 0 then
		return 0
	end
	local pid = redis.call("GET", KEYS[1]..":tids:"..t..":pid")
	local base_key = ""
	if not pid then
		base_key = KEYS[1]..":tids:"..t
	else
		base_key = KEYS[1]..":pids:"..pid
	end
	redis.call("SADD", base_key..":fds", ret)
	redis.call("SADD", base_key..":fds:"..ret..":ops", timestamp..":"..cpu_id)
	local fd_index = redis.call("LINDEX", base_key..":fds:"..ret, -1)
	if not fd_index then
		fd_index = 0
	else
		-- FIXME close the previous one ?
		fd_index = fd_index + 1
	end
	redis.call("RPUSH", base_key..":fds:"..ret, fd_index)

	base_key = base_key..":fds:"..ret..":"..fd_index
	redis.call("SADD", base_key..":ops", timestamp..":"..cpu_id)
	redis.call("SET", base_key..":created", timestamp..":"..cpu_id)
	local path = redis.call("GET", KEYS[1]..":events:"..event..":path")
	redis.call("SET", base_key..":path", path)
end

function do_sys_close(t, ret, event, timestamp, cpu_id)
	-- we could track close errors here
	if tonumber(ret) < 0 then
		return 0
	end
	local pid = redis.call("GET", KEYS[1]..":tids:"..t..":pid")
	local base_key = ""
	if not pid then
		base_key = KEYS[1]..":tids:"..t
	else
		base_key = KEYS[1]..":pids:"..pid
	end
	local fd = redis.call("GET", KEYS[1]..":events:"..event..":fd")
	redis.call("SADD", base_key..":fds:"..fd..":ops", timestamp..":"..cpu_id)
	local fd_index = redis.call("LINDEX", base_key..":fds:"..fd, -1)
	if not fd_index then
		-- the FD has never been opened
		return 0
	end

	base_key = base_key..":fds:"..fd..":"..fd_index
	redis.call("SADD", base_key..":ops", timestamp..":"..cpu_id)
	redis.call("SET", base_key..":closed", timestamp..":"..cpu_id)
	--redis.log(redis.LOG_WARNING,"close4XXX "..ret)
end

local t = redis.call("GET", KEYS[1]..":cpus:"..cpu_id..":current_tid")
if not t then
	return nil
end

local event = redis.call("GET", KEYS[1]..":tids:"..t..":current_syscall")
if not event then
	return nil
end

local name = redis.call("GET", KEYS[1]..":events:"..event..":event_name")
if name == "sys_open" then
	do_sys_open(t, ret, event, timestamp, cpu_id)
elseif name == "sys_close" then
	do_sys_close(t, ret, event, timestamp, cpu_id)
else
	return 0
end

redis.call("DEL", KEYS[1]..":tids:"..t..":current_syscall")

event = timestamp..":cpu"..cpu_id
redis.call("RPUSH", KEYS[1]..":events", event)
redis.call("SET", KEYS[1]..":events:"..event..":event_name", "exit_syscall")
redis.call("SET", KEYS[1]..":events:"..event..":ret", ret)
redis.log(redis.LOG_WARNING,"close4XXX "..KEYS[1]..":events:"..event..":ret")

return 0
