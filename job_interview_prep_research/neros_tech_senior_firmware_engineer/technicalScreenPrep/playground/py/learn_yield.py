class RingBuffer:

    def __init__(self, capacity):
        self.capacity = capacity
        self._buf = [None] * capacity
        self._head = 0
        self._count = 0
        self._dropped = 0

    def __iter__(self):
        t = self._tail()
        for i in range(self._count):
            yield self._buf[(t+i) % self.capacity]
    def __len__(self):
        return self._count
    def full(self):
        return self._count == self.capacity
    def empty(self):
        return self._count == 0
    def _tail(self):
        return (self._head - self._count) % self.capacity
    
    def push(self, item):
        if self.full():
            return False
        self._buf[self._head] = item
        self._head = (self._head + 1) & self.capacity
        self._count += 1
        return True
        

    def pop(self):
        if self.empty():
            return False
        t = self._tail()
        item, self._buf[t] = self._buf[t], None
        self._count -= 1
        return item
        
    def peek(self):
        if self.empty():
            return False
        return self._buf[self._tail()]

    