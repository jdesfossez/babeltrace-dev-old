-- REDIS_SYS_OPEN
-- KEYS: hostname:session_name
-- ARGS: timestamp, cpu_id, filename

local timestamp = ARGV[1]
local cpu_id = ARGV[2]
local filename = ARGV[3]

local t = redis.call("GET", KEYS[1]..":cpus:"..cpu_id..":current_tid")
if not t then
	return 1
end

local event = timestamp..":cpu"..cpu_id
redis.call("SET", KEYS[1]..":tids:"..t..":current_syscall", event)

redis.call("RPUSH", KEYS[1]..":events", event)
redis.call("SET", KEYS[1]..":events:"..event..":event_name", "sys_open")
redis.call("SET", KEYS[1]..":events:"..event..":path", filename)

return 0
