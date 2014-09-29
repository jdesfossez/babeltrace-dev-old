#!/usr/bin/python

import redis
import time

NSEC_PER_SEC = 1000000000

def ns_to_hour_nsec(ns):
    d = time.localtime(ns/NSEC_PER_SEC)
    return "%02d:%02d:%02d.%09d" % (d.tm_hour, d.tm_min, d.tm_sec, ns % NSEC_PER_SEC)

def ns_to_sec(ns):
    return "%lu.%09u" % (ns/NSEC_PER_SEC, ns % NSEC_PER_SEC)

def trim_ts(ts):
    if ts:
        return ts.split(":")[0]
    else:
        return "unknown"

def get_proc_base_infos(r, key):
    procname = r.get("%s:procname" % (key))
    pcreated = r.get("%s:created" % (key)).split(":")[0]
    return "\t- %s created at %s" % (procname, pcreated)

def get_fds(r, fd_key):
    fds = r.smembers("%s" % (fd_key))
    for f in fds:
        print("\t\t- FD %s" % (f))
        subfds = r.lrange("%s:%s" % (fd_key, f), 0, -1)
        for s in subfds:
            subfd_key = "%s:%s:%s" % (fd_key, f, s)
            path = r.get("%s:path" % subfd_key)
            opened = trim_ts(r.get("%s:created" % subfd_key))
            closed = trim_ts(r.get("%s:closed" % subfd_key))
            print("\t\t\t- %s, opened at %s, closed at %s" % \
                    (path, opened, closed))

def get_proc_list(r, session):
    pids = r.smembers(session + ":pids")
    for i in pids:
        print("- Pid %s" % i)
        sub = r.lrange(session + ":pids:" + i, 0, -1)
        for j in sub:
            print(get_proc_base_infos(r, "%s:pids:%s:%s" % (session, i, j)))
            threads = r.smembers("%s:pids:%s:%s:threads" % (session, i, j))
            for t in threads:
                tname = r.get("%s:tids:%s:procname" % (session, t))
                tcreated = r.get("%s:tids:%s:created" % (session, t)).split(":")[0]
                print("\t\t- Thread %s (%s) created at %s" % \
                        (t.split(":")[0], tname, tcreated))
            fd_key = "%s:pids:%s:%s:fds" % (session, i, j)
            get_fds(r, fd_key)

    # threads not connected to a process
    tids = r.smembers(session + ":tids")
    for i in tids:
        sub = r.lrange(session + ":tids:" + i, 0, -1)
        for j in sub:
            pid = r.get("%s:tids:%s:%s:pid" % (session, i, j))
            if pid:
                continue
            print("- Tid %s" % i)
            print(get_proc_base_infos(r, "%s:tids:%s:%s" % (session, i, j)))
            fd_key = "%s:tids:%s:%s:fds" % (session, i, j)
            get_fds(r, fd_key)

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
            if not rcompleted:
                continue
            completed = rcompleted.split(":")[0]
            delta = ns_to_sec(int(completed) - ts)
            result_fd = r.get(root_key + ":events:" + rcompleted + ":ret")
            payload = "filename = \"%s\", flags = ?, mode = ?, ret = { " \
                    "duration = %ss, fd = %s }" % (path, delta, result_fd)
        elif name == "sys_close":
            fd = r.get(root_key + ":events:" + e + ":fd")
            completed = r.get(root_key + ":events:" + e + ":completed")
            completed = completed.split(":")[0]
            delta = ns_to_sec(int(completed) - ts)
            payload = "fd = %s, ret = { duration = %ss }" % (fd, delta)
        elif name == "exit_syscall":
#            continue
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
            payload = "parent_comm = \"?\", parent_tid = ?, parent_pid = %s, " \
                    "child_comm = \"%s\", child_tid = %s, child_pid = %s" % \
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
    print "\nDetailled state :"
    get_proc_list(r, root_key)

if __name__ == "__main__":
    run()
