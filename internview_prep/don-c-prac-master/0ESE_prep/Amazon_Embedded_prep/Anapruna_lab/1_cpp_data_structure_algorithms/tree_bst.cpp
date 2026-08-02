#include <iostream>
#include <cassert>
using namespace std;

// 이진 탐색 트리 노드
struct TreeNode {
    int data;
    TreeNode* left;
    TreeNode* right;
    TreeNode(int d) : data(d), left(nullptr), right(nullptr) {}
};

// BST 클래스
class BST {
public:
    BST();
    ~BST();
    void insert(int val);           // 값 삽입
    bool remove(int val);           // 값 삭제
    bool find(int val) const;       // 값 존재 여부
    int find_min() const;           // 최소값
    int find_max() const;           // 최대값
    void inorder() const;           // 중위 순회
    void preorder() const;          // 전위 순회
    void postorder() const;         // 후위 순회
    int size() const;               // 노드 개수

private:
    TreeNode* root;
    int count;
    void destroy(TreeNode* node);
    void inorder(TreeNode* node) const;
    void preorder(TreeNode* node) const;
    void postorder(TreeNode* node) const;
};

// TODO: 각 함수 구현

// 테스트 코드
int main() {
    BST tree;
    assert(tree.size() == 0);

    tree.insert(10);
    tree.insert(5);
    tree.insert(20);
    tree.insert(3);
    tree.insert(7);
    tree.insert(15);
    tree.insert(30);

    assert(tree.size() == 7);
    assert(tree.find(10));
    assert(tree.find(7));
    assert(!tree.find(100));
    assert(tree.find_min() == 3);
    assert(tree.find_max() == 30);

    cout << "Inorder: "; tree.inorder();    // 3 5 7 10 15 20 30
    cout << "Preorder: "; tree.preorder();  // 10 5 3 7 20 15 30
    cout << "Postorder: "; tree.postorder();// 3 7 5 15 30 20 10

    assert(tree.remove(10)); // root 삭제
    assert(tree.size() == 6);
    assert(!tree.find(10));
    assert(tree.find_min() == 3);
    assert(tree.find_max() == 30);

    cout << "All tests PASS" << endl;
    return 0;
}

/*
문제 예시:
- 이진 탐색 트리(BST) 클래스를 구현하라.
- insert, remove, find, find_min, find_max, inorder, preorder, postorder, size 멤버함수를 제공하라.
- main에서 위 함수들을 테스트하는 코드를 작성하라.
*/