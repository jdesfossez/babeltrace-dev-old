-- REDIS_SYS_CLOSE
-- KEYS: hostname:session_name
-- ARGS: timestamp, cpu_id, fd

local timestamp = ARGV[1]
local cpu_id = ARGV[2]
local fd = ARGV[3]

local t = redis.call("GET", KEYS[1]..":cpus:"..cpu_id..":current_tid")
if not t then
	return 1
end

local event = timestamp..":cpu"..cpu_id
redis.call("SET", KEYS[1]..":tids:"..t..":current_syscall", event)

redis.call("RPUSH", KEYS[1]..":events", event)
redis.call("SET", KEYS[1]..":events:"..event..":event_name", "sys_close")
redis.call("SET", KEYS[1]..":events:"..event..":fd", fd)
redis.call("SET", KEYS[1]..":events:"..event..":tid", t)

return 0
