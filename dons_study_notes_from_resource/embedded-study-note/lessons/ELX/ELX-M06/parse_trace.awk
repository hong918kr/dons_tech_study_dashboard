# parse_trace.awk — pair `irq_handler_entry name=bh_cmp` with the first event
# that shows the bottom half actually starting, for one mechanism.
# usage: awk -v mech=tasklet -f parse_trace.awk /sys/kernel/tracing/trace
# ftrace text timestamps are microseconds (6 decimals), so resolution = 1 us.
function pop(cpu,    t) {
	if (cpu in q) { t = q[cpu]; delete q[cpu]; return t }
	return -1
}
/^#/ { next }
{
	# "fire-73 [002] d.h.1  12.345678: event: ..."
	for (i = 1; i <= NF; i++) if ($i ~ /^\[[0-9]+\]$/) { cpu = $i; break }
	for (i = 1; i <= NF; i++) if ($i ~ /^[0-9]+\.[0-9]+:$/) { ts = $i; break }
	sub(/:$/, "", ts)
	us = ts * 1000000
}
/irq_handler_entry:/ && /name=bh_cmp/ { q[cpu] = us; last = us; next }
mech == "tasklet" && /softirq_entry:/ && /vec=6/ { t = pop(cpu); if (t >= 0) add(us - t); next }
(mech == "wq_sys" || mech == "wq_hi" || mech == "wq_bh") && /workqueue_execute_start:/ && /bhc_work_fn/ {
	t = last; if (t >= 0) { add(us - t); last = -1 } next
}
mech == "threaded" && /sched_switch:/ && /next_comm=irq\// {
	t = last; if (t >= 0) { add(us - t); last = -1 } next
}
function add(d) { n++; v[n] = d }
END {
	for (i = 2; i <= n; i++) {
		x = v[i]; j = i - 1
		while (j >= 1 && v[j] > x) { v[j + 1] = v[j]; j-- }
		v[j + 1] = x
	}
	if (n == 0) { printf "n=0 p50=- p99=- max=-\n"; exit }
	p50 = v[int(n * 0.5) + 1]; p99i = int(n * 0.99) + 1; if (p99i > n) p99i = n
	printf "n=%d p50=%d p99=%d max=%d\n", n, p50 + 0.5, v[p99i] + 0.5, v[n] + 0.5
}
