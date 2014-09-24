#!/usr/bin/python

import redis
import time

NSEC_PER_SEC = 1000000000

def ns_to_hour_nsec(ns):
    d = time.localtime(ns/NSEC_PER_SEC)
    return "%02d:%02d:%02d.%09d" % (d.tm_hour, d.tm_min, d.tm_sec, ns % NSEC_PER_SEC)

def ns_to_sec(ns):
    return "%lu.%09u" % (ns/NSEC_PER_SEC, ns % NSEC_PER_SEC)

def run():
    r = redis.Redis("localhost")

    hostname = r.smembers("hostnames").pop()
    session = r.smembers(hostname + ":sessions").pop()
    root_key = "%s:%s" % (hostname, session)

    print("State of session %s on host %s" % (session, hostname))
    events = r.lrange(root_key + ":events", 0, -1)
    for e in events:
        ts = int(e.split(":")[0])
        cpu_id = e.split(":")[1][3:]
        name = r.get(root_key + ":events:" + e + ":event_name")
        payload = ""
        if name == "sys_open":
            path = r.get(root_key + ":events:" + e + ":path")
            rcompleted = r.get(root_key + ":events:" + e + ":completed")
            completed = rcompleted.split(":")[0]
            delta = ns_to_sec(int(completed) - ts)
            result_fd = r.get(root_key + ":events:" + rcompleted + ":ret")
            payload = "filename = \"%s\", flags = ?, mode = ?, ret = { duration = %ss, fd = %s }" % (path, delta, result_fd)
        elif name == "sys_close":
            fd = r.get(root_key + ":events:" + e + ":fd")
            completed = r.get(root_key + ":events:" + e + ":completed")
            completed = completed.split(":")[0]
            delta = ns_to_sec(int(completed) - ts)
            payload = "fd = %s, ret = { duration = %ss }" % (fd, delta)
        elif name == "exit_syscall":
            continue
            ret = r.get(root_key + ":events:" + e + ":ret")
            enter_event = r.get(root_key + ":events:" + e + ":enter_event")
            enter_ts = enter_event.split(":")[0]
            delta = ns_to_sec(ts - int(enter_ts))
            oldname = r.get(root_key + ":events:" + enter_event + ":event_name")
            payload = "ret = %s, event = \"%s\", latency = %ss" % (ret, oldname, delta)
        elif name == "sched_process_free":
            comm = r.get(root_key + ":events:" + e + ":comm")
            tid = r.get(root_key + ":events:" + e + ":tid")
            payload = "comm = \"%s\", tid = %s, prio = ? " % (comm, tid)
        elif name == "sched_process_fork":
            parent_pid = r.get(root_key + ":events:" + e + ":parent_pid")
            child_comm = r.get(root_key + ":events:" + e + ":child_comm")
            child_tid = r.get(root_key + ":events:" + e + ":child_tid")
            child_pid = r.get(root_key + ":events:" + e + ":child_pid")
            payload = "parent_comm = \"?\", parent_tid = ?, parent_pid = %s, child_comm = \"%s\", child_tid = %s, child_pid = %s" % \
                    (parent_pid, child_comm, child_tid, child_pid)

        tid = r.get(root_key + ":events:" + e + ":tid")
        otid = tid
        pid = "?"
        if tid:
            procname = r.get(root_key + ":tids:" + tid + ":procname")
            if not procname:
                procname = "?"
            tid = tid.split(":")[0]
            pid = r.get(root_key + ":tids:" + otid + ":pid")
            if not pid:
                pid = "?"
            else:
                pid = pid.split(":")[0]
        else:
            procname = "?"
            tid = "?"

        print("[%s] %s %s: { cpu_id = %s }, { tid = %s, procname = \"%s\", pid = %s }, { %s }" %
                (ns_to_hour_nsec(ts), hostname, name, cpu_id, tid, procname, pid,
                    payload))

if __name__ == "__main__":
    run()
