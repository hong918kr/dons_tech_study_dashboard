"""06 · BST — 삽입 · 검색 · 중위 순회
prompt: Node(val) 로 이진 탐색 트리. insert(root, val) → root, contains(root, val) → bool, inorder(root) → 정렬된 리스트.
note: 05 이진 탐색과 같은 생각 — 작으면 왼쪽, 크면 오른쪽. 균형이면 O(log n), 한쪽으로 치우치면 O(n)
note: 중위 순회(왼쪽 → 나 → 오른쪽) 결과는 항상 정렬돼 있다 → 테스트에서 이걸로 트리 전체를 검증
"""
# --- solution ---
class Node:
    def __init__(self, val):
        self.val = val
        self.left = None
        self.right = None


def insert(root, val):
    if root is None:
        return Node(val)
    if val < root.val:
        root.left = insert(root.left, val)
    elif val > root.val:
        root.right = insert(root.right, val)
    return root


def contains(root, val):
    while root:
        if val == root.val:
            return True
        root = root.left if val < root.val else root.right
    return False


def inorder(root):
    return inorder(root.left) + [root.val] + inorder(root.right) if root else []


def test_bst():
    root = None
    for v in [5, 3, 8, 1, 4, 3]:
        root = insert(root, v)
    assert inorder(root) == [1, 3, 4, 5, 8]          # 정렬 + 중복 무시
    assert contains(root, 4) and not contains(root, 7)


if __name__ == "__main__":
    test_bst()
    print("PASS")
# --- end ---
