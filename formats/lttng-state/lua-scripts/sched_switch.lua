-- REDIS_SCHED_SWITCH
-- KEYS: hostname:session_name
-- ARGS: timestamp, prev_comm, prev_tid, next_comm, next_tid, cpu_id

local timestamp = ARGV[1]
local prev_comm = ARGV[2]
local prev_tid = ARGV[3]
local next_comm = ARGV[4]
local next_tid = ARGV[5]
local cpu_id = ARGV[6]

local s = redis.call("LINDEX", KEYS[1]..":tids:"..next_tid, -1)

if not s then
	s = 0
	redis.call("SADD", KEYS[1]..":tids", next_tid)
	redis.call("RPUSH", KEYS[1]..":tids:"..next_tid, s)
	redis.call("SET", KEYS[1]..":tids:"..next_tid..":"..s..":procname", next_comm)
	redis.call("SET", KEYS[1]..":tids:"..next_tid..":"..s..":created", timestamp)
end

redis.call("SET", KEYS[1]..":cpus:"..cpu_id..":current_tid", next_tid..":"..s)

return 0
