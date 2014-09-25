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
redis.call("SET", KEYS[1]..":events:"..event..":tid", t)
local last_sched = redis.call("GET", KEYS[1]..":cpus:"..cpu_id..":last_sched_ts")
redis.call("SET", KEYS[1]..":events:"..event..":sched_in", last_sched)

redis.call("SADD", KEYS[1]..":events:"..event..":attrs", "event_name", "path", "tid",
	"last_sched_ts", "sched_in")

return 0
