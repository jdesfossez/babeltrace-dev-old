-- REDIS_SCHED_PROCESS_FORK
-- KEYS: hostname:session_name
-- ARGS: timestamp, parent_pid, child_comm, child_tid, child_pid, cpu_id

local timestamp = ARGV[1]
local parent_pid = ARGV[2]
local child_comm = ARGV[3]
local child_tid = ARGV[4]
local child_pid = ARGV[5]
local cpu_id = ARGV[6]

local subtid = redis.call("LINDEX", KEYS[1]..":tids:"..child_pid, -1)
if not subtid then
	subtid = 0
else
	subtid = subtid + 1
end

if child_tid == child_pid then
	redis.call("SADD", KEYS[1]..":pids", child_pid)
	local s = redis.call("LINDEX", KEYS[1]..":pids:"..child_pid, -1)
	if not s then
		s = 0
	else
		s = s + 1
	end
	redis.call("RPUSH", KEYS[1]..":pids:"..child_pid, s)
	redis.call("SADD", KEYS[1]..":pids:"..child_pid..":"..s..":threads",
		child_tid..":"..subtid)
	redis.call("SET", KEYS[1]..":pids:"..child_tid..":"..s..":procname", child_comm)
	redis.call("SET", KEYS[1]..":pids:"..child_tid..":"..s..":created",
	timestamp..":"..cpu_id)
end

local s1 = redis.call("LINDEX", KEYS[1]..":pids:"..child_pid, -1)
s1 = tostring(s1)

redis.call("SADD", KEYS[1]..":tids", child_tid)

redis.call("RPUSH", KEYS[1]..":tids:"..child_tid, subtid)
redis.call("SET", KEYS[1]..":tids:"..child_tid..":"..subtid..":pid", child_pid..":"..s1)
redis.call("SET", KEYS[1]..":tids:"..child_tid..":"..subtid..":procname", child_comm)
redis.call("SET", KEYS[1]..":tids:"..child_tid..":"..subtid..":created",
	timestamp..":"..cpu_id)

local event = timestamp..":cpu"..cpu_id
redis.call("RPUSH", KEYS[1]..":events", event)
redis.call("SET", KEYS[1]..":events:"..event..":parent_pid", parent_pid)
redis.call("SET", KEYS[1]..":events:"..event..":child_comm", child_comm)
redis.call("SET", KEYS[1]..":events:"..event..":child_tid", child_tid)
redis.call("SET", KEYS[1]..":events:"..event..":child_pid", child_pid)
redis.call("SET", KEYS[1]..":events:"..event..":event_name", "sched_process_fork")

return 0
