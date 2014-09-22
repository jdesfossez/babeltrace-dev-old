-- REDIS_SCHED_PROCESS_FORK
-- KEYS: hostname:session_name
-- ARGS: timestamp, child_comm, child_tid, parent_pid

local timestamp = ARGV[1]
local parent_pid = ARGV[2]
local child_comm = ARGV[3]
local child_tid = ARGV[4]
local child_pid = ARGV[5]

if child_tid == child_pid then
	redis.call("SADD", KEYS[1]..":pids", child_pid)
	local s = redis.call("LINDEX", KEYS[1]..":pids:"..child_pid, -1)
	if not s then
		s = 0
	else
		s = s + 1
	end
	redis.call("RPUSH", KEYS[1]..":pids:"..child_pid, s)
	redis.call("SADD", KEYS[1]..":pids:"..child_pid..":"..s..":threads", child_tid)
	redis.call("SET", KEYS[1]..":pids:"..child_tid..":"..s..":procname", child_comm)
end

local s1 = redis.call("LINDEX", KEYS[1]..":pids:"..child_pid, -1)

redis.call("SADD", KEYS[1]..":tids", child_tid)
local s = redis.call("LINDEX", KEYS[1]..":tids:"..child_pid, -1)
if not s then
	s = 0
else
	s = s + 1
end

redis.call("RPUSH", KEYS[1]..":tids:"..child_tid, s)
redis.call("SET", KEYS[1]..":tids:"..child_tid..":"..s..":pid", child_pid..":"..s1)
redis.call("SET", KEYS[1]..":tids:"..child_tid..":"..s..":procname", child_comm)
redis.call("SET", KEYS[1]..":tids:"..child_tid..":"..s..":created", timestamp)

return 0
