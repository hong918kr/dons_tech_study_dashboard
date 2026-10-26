class DNode:
	def __init__(self, data):
		self.data = data
		self.next = None
		self.prev = None

class LinkedList:
	def __init__(self):
		self.head = None

	def append (self, data):
		new_node = DNode(data)

		if self.head is None:
			self.head = new_node
			return
		current = self.head
		while current.next is not None:
			current = current.next
		current.next = new_node
		new_node.prev = current

	def delete(self, key):
		current = self.head

		while current is not None:
			if current.data == key:

				if current == self.head:
					self.head = current.next
					if self.head is not None:
						self.head.prev = None

				else:
					current.prev.next = current.next
					if current.next is not None:
						current.next.prev = current.prev

				current = None
				return 

			current = current.next
