#!/usr/bin/python

import redis
import time

NSEC_PER_SEC = 1000000000

def ns_to_hour_nsec(ns):
    d = time.localtime(ns/NSEC_PER_SEC)
    return "%02d:%02d:%02d.%09d" % (d.tm_hour, d.tm_min, d.tm_sec, ns % NSEC_PER_SEC)

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
            payload = "filename = \"%s\", flags = ?, mode = ?" % (path)
        elif name == "sys_close":
            fd = r.get(root_key + ":events:" + e + ":fd")
            payload = "fd = %s" % fd
        elif name == "exit_syscall":
            ret = r.get(root_key + ":events:" + e + ":ret")
            payload = "ret = %s" % ret
        elif name == "sched_process_free":
            comm = r.get(root_key + ":events:" + e + ":comm")
            tid = r.get(root_key + ":events:" + e + ":tid")
            payload = "comm = \"%s\", tid = %s, prio = ? " % (comm, tid)
        # FIXME fork :  parent_pid, child_comm, child_tid, child_pid

        tid = r.get(root_key + ":events:" + e + ":tid")
        if tid:
            procname = r.get(root_key + ":tids:" + tid + ":procname")
            tid = tid.split(":")[0]
        else:
            procname = "?"
            tid = "?"


        print("[%s] %s %s: { cpu_id = %s }, { tid = %s, procname = \"%s\", pid = %s }, { %s }" %
                (ns_to_hour_nsec(ts), hostname, name, cpu_id, tid, procname, "?",
                    payload))

if __name__ == "__main__":
    run()
