-- REDIS_SCHED_PROCESS_FREE
-- KEYS: hostname:session_name
-- ARGS: timestamp, cpu_id, comm, tid

local timestamp = ARGV[1]
local cpu_id = ARGV[2]
local comm = ARGV[3]
local tid = ARGV[4]

local s = redis.call("LINDEX", KEYS[1]..":tids:"..tid, -1)
if not s then
	return nil
end

local t = tid..":"..s
redis.call("SET", KEYS[1]..":tids:"..t..":terminated", timestamp)

local event = timestamp..":cpu"..cpu_id
redis.call("RPUSH", KEYS[1]..":events", event)
redis.call("SET", KEYS[1]..":events:"..event..":comm", comm)
redis.call("SET", KEYS[1]..":events:"..event..":tid", t)
redis.call("SET", KEYS[1]..":events:"..event..":event_name", "sched_process_free")

return 0
