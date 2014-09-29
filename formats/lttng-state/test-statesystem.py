#!/usr/bin/python

import redis
import time

from lua_scripts import *

NSEC_PER_SEC = 1000000000

def ns_to_hour_nsec(ns):
    d = time.localtime(ns/NSEC_PER_SEC)
    return "%02d:%02d:%02d.%09d" % (d.tm_hour, d.tm_min, d.tm_sec, ns % NSEC_PER_SEC)

def ns_to_sec(ns):
    return "%lu.%09u" % (ns/NSEC_PER_SEC, ns % NSEC_PER_SEC)

def run():
    r = redis.Redis("localhost")

    r.flushdb()
    hostname = "test"
    session_name = "testsession"
    r.sadd("hostnames", hostname)
    r.sadd("test:sessions", session_name)
    s = "%s:%s" % (hostname, session_name)

    r.evalsha(REDIS_SCHED_PROCESS_FORK, 1, s, 1000000000, 1, "new_child", 99, 99, 0)
    r.evalsha(REDIS_SCHED_SWITCH, 1, s, 1000000001, "oldproc", 1, "new_child", 99, 0)
    r.evalsha(REDIS_SYS_OPEN, 1, s, 1000000002, 0, "/tmp/bla")
    r.evalsha(REDIS_EXIT_SYSCALL, 1, s, 1000000003, 0, 3)

    r.evalsha(REDIS_SYS_OPEN, 1, s, 2000000002, 0, "/tmp/bla2")
    r.evalsha(REDIS_EXIT_SYSCALL, 1, s, 2000000003, 0, 4)

    r.evalsha(REDIS_SYS_OPEN, 1, s, 3000000002, 0, "/tmp/bla3")
    r.evalsha(REDIS_EXIT_SYSCALL, 1, s, 3000000003, 0, 5)
    r.evalsha(REDIS_SYS_CLOSE, 1, s, 3000000004, 0, 5)
    r.evalsha(REDIS_EXIT_SYSCALL, 1, s, 3000000005, 0, 0)
    r.evalsha(REDIS_SYS_OPEN, 1, s, 3000000006, 0, "/tmp/bla4")
    r.evalsha(REDIS_EXIT_SYSCALL, 1, s, 3000000007, 0, 5)
    r.evalsha(REDIS_SYS_CLOSE, 1, s, 3000000008, 0, 5)
    r.evalsha(REDIS_EXIT_SYSCALL, 1, s, 3000000009, 0, 0)

    r.evalsha(REDIS_SYS_CLOSE, 1, s, 4000000000, 0, 4)
    r.evalsha(REDIS_EXIT_SYSCALL, 1, s, 4000000001, 0, 0)

if __name__ == "__main__":
    run()
