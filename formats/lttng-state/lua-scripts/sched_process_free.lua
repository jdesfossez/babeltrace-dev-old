-- REDIS_SCHED_PROCESS_FREE
-- KEYS: hostname:session_name
-- ARGS: timestamp, comm, tid

local timestamp = ARGV[1]
local comm = ARGV[2]
local tid = ARGV[3]

local s = redis.call("LINDEX", KEYS[1]..":tids:"..tid, -1)
if not s then
	return nil
end

local t = tid..":"..s
redis.call("SET", KEYS[1]..":tids:"..t..":terminated", timestamp)

return 0
