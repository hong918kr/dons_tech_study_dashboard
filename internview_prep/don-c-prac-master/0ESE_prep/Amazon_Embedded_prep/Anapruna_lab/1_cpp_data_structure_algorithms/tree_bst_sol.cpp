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
    BST() : root(nullptr), count(0) {}
    ~BST() { destroy(root); }

    void insert(int val) {
        root = insert_rec(root, val);
    }

    bool remove(int val) {
        bool removed = false;
        root = remove_rec(root, val, removed);
        if (removed) count--;
        return removed;
    }

    bool find(int val) const {
        TreeNode* cur = root;
        while (cur) {
            if (val == cur->data) return true;
            if (val < cur->data) cur = cur->left;
            else cur = cur->right;
        }
        return false;
    }

    int find_min() const {
        if (!root) throw runtime_error("Empty tree");
        TreeNode* cur = root;
        while (cur->left) cur = cur->left;
        return cur->data;
    }

    int find_max() const {
        if (!root) throw runtime_error("Empty tree");
        TreeNode* cur = root;
        while (cur->right) cur = cur->right;
        return cur->data;
    }

    void inorder() const { inorder(root); cout << endl; }
    void preorder() const { preorder(root); cout << endl; }
    void postorder() const { postorder(root); cout << endl; }
    int size() const { return count; }

private:
    TreeNode* root;
    int count;

    void destroy(TreeNode* node) {
        if (!node) return;
        destroy(node->left);
        destroy(node->right);
        delete node;
    }

    TreeNode* insert_rec(TreeNode* node, int val) {
        if (!node) {
            count++;
            return new TreeNode(val);
        }
        if (val < node->data)
            node->left = insert_rec(node->left, val);
        else if (val > node->data)
            node->right = insert_rec(node->right, val);
        // 중복값은 무시
        return node;
    }

    TreeNode* remove_rec(TreeNode* node, int val, bool& removed) {
        if (!node) return nullptr;
        if (val < node->data) {
            node->left = remove_rec(node->left, val, removed);
        } else if (val > node->data) {
            node->right = remove_rec(node->right, val, removed);
        } else {
            removed = true;
            // case 1: no child
            if (!node->left && !node->right) {
                delete node;
                return nullptr;
            }
            // case 2: one child
            if (!node->left) {
                TreeNode* tmp = node->right;
                delete node;
                return tmp;
            }
            if (!node->right) {
                TreeNode* tmp = node->left;
                delete node;
                return tmp;
            }
            // case 3: two children
            TreeNode* succ = node->right;
            while (succ->left) succ = succ->left;
            node->data = succ->data;
            node->right = remove_rec(node->right, succ->data, removed = false);
        }
        return node;
    }

    void inorder(TreeNode* node) const {
        if (!node) return;
        inorder(node->left);
        cout << node->data << " ";
        inorder(node->right);
    }
    void preorder(TreeNode* node) const {
        if (!node) return;
        cout << node->data << " ";
        preorder(node->left);
        preorder(node->right);
    }
    void postorder(TreeNode* node) const {
        if (!node) return;
        postorder(node->left);
        postorder(node->right);
        cout << node->data << " ";
    }
};

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