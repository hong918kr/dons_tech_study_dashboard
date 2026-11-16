info threads
thread apply all bt
frame 0
info locals
p *s
p/x s->tx
p $_siginfo.si_signo
p/x $_siginfo._sifields._sigfault.si_addr
info registers pc sp
x/6gx s
x/s s->name
thread find rx_parser
tfaas p self->name
tfaas p last_len
p g_trace[1]
