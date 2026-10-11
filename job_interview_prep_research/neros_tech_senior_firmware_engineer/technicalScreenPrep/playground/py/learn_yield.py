class RingBuffer:
    def __init__(self, capacity, overwrite=False):
        self.capacity = capacity
        self._head=0
        self._count=0
        self._droppoed=0
        self._buf = [None] * capacity

    def __len__(self):
        return self._count
    def _tail(self):
        return (self._head - self.count) % self.capacity
        
    def empty(self):
        return self._count == 0
    def full(self):
        return self._count == self.capacity
    def push(self, item):
        if self.pull():
            return False
        self._buf[self._head] = item
        self._count +=1
        self._head = (self._head + 1) % self.capacity
        
        
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
        t = self._tail()
        return self._buf[t]
        
    def __iter__(self):
        t = self._tail()
        for i in range(self._count):
            yield self._buf[(t+i) % self.capacity]